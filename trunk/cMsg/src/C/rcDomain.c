/*----------------------------------------------------------------------------*
 *                                                                            *
 *  Copyright (c) 2006        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 7-Apr-2006, Jefferson Lab                                    *
 *                                                                            *
 *    Authors: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 *
 * Description:
 *
 *  Implements the rc or runcontrol domain used by CODA components  
 *  (but not the server). The runcontrol server side uses 2 domains
 *  to communicate with CODA components: rcTcp and rcUdp domains.
 *
 *----------------------------------------------------------------------------*/

/**
 * @file
 * This file contains the rc domain implementation of the cMsg user API.
 * This a messaging system programmed by the Data Acquisition Group at Jefferson
 * Lab. The rc domain allows CODA components to communicate with runcontrol.
 * The server which communicates with this cMsg user must use 2 domains, the 
 * rcTcp and rcUdp domains. The rc domain relies heavily on the cMsg domain
 * code which it uses as a library. It mainly relies on that code to implement
 * subscriptions/callbacks.
 */  
 

#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#include "errors.h"
#include "cMsgPrivate.h"
#include "cMsg.h"
#include "cMsgNetwork.h"
#include "rwlock.h"
#include "regex.h"
#include "cMsgDomain.h"

#ifdef VXWORKS
#include <vxWorks.h>
#endif



/**
 * Structure for arg to be passed to receiver thread.
 * Allows data to flow back and forth with receiver thread.
 */
typedef struct receiverArg_t {
    int sockfd;
    unsigned short port;
    socklen_t len;
    struct sockaddr_in clientaddr;
} receiverArg;

/* built-in limits */
/** Maximum number of domains for each client to connect to at once. */
#define MAXDOMAINS_RC 10
/** Number of seconds to wait for cMsgClientListeningThread threads to start. */
#define WAIT_FOR_THREADS 10

/* global variables */
/** Store information about each rc domain connected to. */
cMsgDomainInfo rcDomains[MAXDOMAINS_RC];

/** Function in cMsgServer.c which implements the network listening thread of a client. */
void *rcClientListeningThread(void *arg);

/* local variables */
/** Is the one-time initialization done? */
static int oneTimeInitialized = 0;

/** Pthread mutex to protect one-time initialization and the local generation of unique numbers. */
static pthread_mutex_t generalMutex = PTHREAD_MUTEX_INITIALIZER;

/** Id number which uniquely defines a subject/type pair. */
static int subjectTypeId = 1;

/** Size of buffer in bytes for sending messages. */
static int initialMsgBufferSize = 1500;

/**
 * Read/write lock to prevent connect or disconnect from being
 * run simultaneously with any other function.
 */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond   = PTHREAD_COND_INITIALIZER;


/* Local prototypes */

static void  staticMutexLock(void);
static void  staticMutexUnlock(void);
static void *receiver(void *arg);
static void  defaultShutdownHandler(void *userArg);
static int   parseUDL(const char *UDL, char **host,
                      unsigned short *port, char **UDLRemainder);

/* Prototypes of the 14 functions which implement the standard tasks in cMsg. */
static int   cmsgd_connect(const char *myUDL, const char *myName,
                           const char *myDescription,
                           const char *UDLremainder, void **domainId);
static int   cmsgd_send(void *domainId, void *msg);
static int   cmsgd_syncSend(void *domainId, void *msg, int *response);
static int   cmsgd_flush(void *domainId);
static int   cmsgd_subscribe(void *domainId, const char *subject, const char *type,
                             cMsgCallbackFunc *callback, void *userArg,
                             cMsgSubscribeConfig *config, void **handle);
static int   cmsgd_unsubscribe(void *domainId, void *handle);
static int   cmsgd_subscribeAndGet(void *domainId, const char *subject, const char *type,
                                   const struct timespec *timeout, void **replyMsg);
static int   cmsgd_sendAndGet(void *domainId, void *sendMsg,
                              const struct timespec *timeout, void **replyMsg);
static int   cmsgd_start(void *domainId);
static int   cmsgd_stop(void *domainId);
static int   cmsgd_disconnect(void *domainId);
static int   cmsgd_shutdownClients(void *domainId, const char *client, int flag);
static int   cmsgd_shutdownServers(void *domainId, const char *server, int flag);
static int   cmsgd_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler,
                                      void *userArg);

/** List of the functions which implement the standard cMsg tasks in the cMsg domain. */
static domainFunctions functions = {cmsgd_connect, cmsgd_send,
                                    cmsgd_syncSend, cmsgd_flush,
                                    cmsgd_subscribe, cmsgd_unsubscribe,
                                    cmsgd_subscribeAndGet, cmsgd_sendAndGet,
                                    cmsgd_start, cmsgd_stop, cmsgd_disconnect,
                                    cmsgd_shutdownClients, cmsgd_shutdownServers,
                                    cmsgd_setShutdownHandler};

/* CC domain type */
domainTypeInfo rcDomainTypeInfo = {
  "rc",
  &functions
};


/*-------------------------------------------------------------------*/


/**
 * This routine is called once to connect to an RC domain. It is called
 * by the user through top-level cMsg API, "cMsgConnect()".
 * The argument "myUDL" is the Universal Domain Locator used to uniquely
 * identify the RC server to connect to.
 * It has the form:<p>
 *       <b>cMsg:rc://host:port/</b><p>
 * where the first "cMsg:" is optional. Both the host and port are also
 * optional. If a host is NOT specified, a broadcast on the local subnet
 * is used to locate the server. If the port is omitted, a default port
 * (RC_BROADCAST_PORT) is used.
 *
 * If successful, this routine fills the argument "domainId", which identifies
 * the connection uniquely and is required as an argument by many other routines.
 *
 * @param myUDL the Universal Domain Locator used to uniquely identify the rc
 *        server to connect to
 * @param myName name of this client
 * @param myDescription description of this client
 * @param UDLremainder partially parsed (initial cMsg:rc:// stripped off)
 *                     UDL which gets passed down from the API level (cMsgConnect())
 * @param domainId pointer to integer which gets filled with a unique id referring
 *        to this connection.
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_FORMAT if the UDL is malformed
 * @returns CMSG_OUT_OF_RANGE if the port specified in the UDL is out-of-range
 * @returns CMSG_OUT_OF_MEMORY if the allocating memory for message buffer failed
 * @returns CMSG_LIMIT_EXCEEDED if the maximum number of domain connections has
 *          been exceeded
 * @returns CMSG_SOCKET_ERROR if the listening thread finds all the ports it tries
 *                            to listen on are busy, or socket options could not be set.
 *                            If udp socket to server could not be created or connect failed.
 * @returns CMSG_NETWORK_ERROR if no connection to the rc server can be made,
 *                             or a communication error with server occurs.
 */   
static int cmsgd_connect(const char *myUDL, const char *myName, const char *myDescription,
                         const char *UDLremainder, void **domainId) {
  
    unsigned short serverPort;
    char  *serverHost, *expid, buffer[1024];
    int    err, status, len, expidLen, expidLenNet, tcpServerPort;
    int    i, id = -1;
    int    outGoing[2];
    char   temp[CMSG_MAXHOSTNAMELEN];
    char  *portEnvVariable=NULL;
    unsigned short startingPort;
    cMsgDomainInfo *domain;
    cMsgThreadInfo *threadArg;
    int    hz, num_try, try_max;
    struct timespec waitForThread;
    
    pthread_t thread;
    receiverArg arg;
    
    struct timespec wait, time;
    struct sockaddr_in servaddr;
    int numLoops;
    int gotResponse = 0;
        
       
    /* clear array */
    bzero(buffer, 1024);
    
    /* parse the UDLRemainder to get the host and port but ignore everything else */
    err = parseUDL(UDLremainder, &serverHost, &serverPort, NULL);
    if (err != CMSG_OK) {
        return(err);
    }

    /* First, grab lock for thread safety. This lock must be held until
     * the initialization is completely finished. But just hold through
     * the whole routine anyway since we do cMsgDomainClear's if there is an
     * error.
     */
    staticMutexLock();
    
    /* do one time initialization */
    if (!oneTimeInitialized) {
        /* clear domain arrays */
        for (i=0; i<MAXDOMAINS_RC; i++) {
            cMsgDomainInit(&rcDomains[i], 0);
        }
        oneTimeInitialized = 1;
    }

    /* find the first available place in the "rcDomains" array */
    for (i=0; i<MAXDOMAINS_RC; i++) {
        if (rcDomains[i].initComplete > 0) {
            continue;
        }
        cMsgDomainClear(&rcDomains[i]);
        id = i;
        break;
    }

    /* exceeds number of domain connections allowed */
    if (id < 0) {
        staticMutexUnlock();
        return(CMSG_LIMIT_EXCEEDED);
    }

    /* allocate memory for message-sending buffer */
    rcDomains[id].msgBuffer     = (char *) malloc(initialMsgBufferSize);
    rcDomains[id].msgBufferSize = initialMsgBufferSize;
    if (rcDomains[id].msgBuffer == NULL) {
        cMsgDomainClear(&rcDomains[id]);
        staticMutexUnlock();
        return(CMSG_OUT_OF_MEMORY);
    }

    /* reserve this element of the "cMsgDomains" array */
    rcDomains[id].initComplete = 1;

    /* save ref to self */
    rcDomains[id].id = id;

    /* store our host's name */
    gethostname(temp, CMSG_MAXHOSTNAMELEN);
    rcDomains[id].myHost = (char *) strdup(temp);

    /* store names, can be changed until server connection established */
    rcDomains[id].name        = (char *) strdup(myName);
    rcDomains[id].udl         = (char *) strdup(myUDL);
    rcDomains[id].description = (char *) strdup(myDescription);

    /*--------------------------------------------------------------------------
     * First find a port on which to receive incoming messages.
     * Do this by trying to open a listening socket at a given
     * port number. If that doesn't work add one to port number
     * and try again.
     * 
     * But before that, define a port number from which to start looking.
     * If CMSG_PORT is defined, it's the starting port number.
     * If CMSG_PORT is NOT defind, start at CMSG_CLIENT_LISTENING_PORT (2345).
     *-------------------------------------------------------------------------/

    /* pick starting port number */
    domain = &rcDomains[id];
    if ( (portEnvVariable = getenv("CMSG_PORT")) == NULL ) {
        startingPort = CMSG_CLIENT_LISTENING_PORT;
        if (cMsgDebug >= CMSG_DEBUG_WARN) {
            fprintf(stderr, "cmsgd_connectImpl: cannot find CMSG_PORT env variable, first try port %hu\n", startingPort);
        }
    }
    else {
        i = atoi(portEnvVariable);
        if (i < 1025 || i > 65535) {
            startingPort = CMSG_CLIENT_LISTENING_PORT;
            if (cMsgDebug >= CMSG_DEBUG_WARN) {
                fprintf(stderr, "cmsgd_connect: CMSG_PORT contains a bad port #, first try port %hu\n", startingPort);
            }
        }
        else {
            startingPort = i;
        }
    }
    
    /* get listening port and socket for this application */
    if ( (err = cMsgGetListeningSocket(CMSG_BLOCKING,
                                       startingPort,
                                       &domain->listenPort,
                                       &domain->listenSocket)) != CMSG_OK) {
        cMsgDomainClear(&rcDomains[id]);
        staticMutexUnlock();
        return(err);
    }

    /* launch pend thread and start listening on receive socket */
    threadArg = (cMsgThreadInfo *) malloc(sizeof(cMsgThreadInfo));
    if (threadArg == NULL) {
        cMsgDomainClear(&rcDomains[id]);
        staticMutexUnlock();
        return(CMSG_OUT_OF_MEMORY);  
    }
    threadArg->isRunning = 0;
    threadArg->listenFd  = domain->listenSocket;
    threadArg->blocking  = CMSG_NONBLOCKING;
    status = pthread_create(&domain->pendThread, NULL,
                            cMsgClientListeningThread, (void *) threadArg);
    if (status != 0) {
        err_abort(status, "Creating message listening thread");
    }

    /*
     * Wait for flag to indicate thread is actually running before
     * continuing on. This thread must be running before we talk to
     * the rc server since the server tries to communicate with
     * the listening thread.
     */

  #ifdef VXWORKS
    hz = sysClkRateGet();
  #else
    /* get system clock rate - probably 100 Hz */
    hz = 100;
    hz = sysconf(_SC_CLK_TCK);
  #endif
    /* wait up to WAIT_FOR_THREADS seconds for a thread to start */
    try_max = hz * WAIT_FOR_THREADS;
    num_try = 0;
    waitForThread.tv_sec  = 0;
    waitForThread.tv_nsec = 1000000000/hz;

    while((threadArg->isRunning != 1) && (num_try++ < try_max)) {
        nanosleep(&waitForThread, NULL);
    }
    if (num_try > try_max) {
        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "cmsgd_connect, cannot start listening thread\n");
        }
        exit(-1);
    }

    /* free mem allocated for the argument passed to listening thread */
    free(threadArg);

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "cmsgd_connect: created listening thread\n");
    }

    /*-------------------------------------------------------
     * Talk to runcontrol server
     *-------------------------------------------------------*/
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(serverPort);
    
    /* create UDP socket */
    domain->sendSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (domain->sendSocket < 0) {
        cMsgDomainClear(&rcDomains[id]);
        staticMutexUnlock();
        return(CMSG_SOCKET_ERROR);
    }

    /*
     * We send 2 items explicitly:
     *   1) TCP server port, and
     *   2) EXPID (experiment id string)
     * The host we're sending from gets sent for free
     * as does the UDP port we're sending from.
     */
    expid       = getenv("EXPID");
    expidLen    = strlen(expid);
    expidLenNet = htonl(expidLen);
/*printf("GOT EXPID = %s\n", expid); */
    
    /* tcp port */
    outGoing[0] = htonl((int) tcpServerPort);
    /* length of "expid" string */
    outGoing[1] = htonl(expidLen);

    /* copy data into a single buffer */
    memcpy(buffer, (void *)outGoing, sizeof(outGoing));
    len = sizeof(outGoing);
    memcpy(buffer+len, (const void *)expid, expidLen);
    len += expidLen;
    buffer[len] = '\0'; /* add null to end of string */
    len += 1;        /* total length of message */
    
    if ( inet_pton(AF_INET, serverHost,  &servaddr.sin_addr) < 1) {
        cMsgDomainClear(&rcDomains[id]);
        staticMutexUnlock();
        return(CMSG_NETWORK_ERROR);
    }
    
    /* create a thread which will receive any responses to our broadcast */
    bzero(&arg.clientaddr, sizeof(arg.clientaddr));
    arg.len = sizeof(arg.clientaddr);
    arg.port = 0;
    arg.sockfd = domain->sendSocket;
    arg.clientaddr.sin_family = AF_INET;
    
    status = pthread_create(&thread, NULL, receiver, (void *)(&arg) );
    if (status != 0) {
        err_abort(status, "Creating keep alive thread");
    }
    
    /* wait for a response for 2 seconds */
    wait.tv_sec  = 2;
    wait.tv_nsec = 0;
    
    numLoops = 5;
    while (!gotResponse && numLoops > 0) {
        /* send UDP  packet to rc server */
        sendto(domain->sendSocket, (void *)buffer, len, 0, (SA *) &servaddr,
               sizeof(servaddr));

        cMsgGetAbsoluteTime(&wait, &time);
        status = pthread_cond_timedwait(&cond, &mutex, &time);
        if (status == ETIMEDOUT) {
            numLoops--;
            continue;
        }
        if (status != 0) {
            err_abort(status, "pthread_cond_timedwait");
        }
        gotResponse = 1;
    }

    if (!gotResponse) {
printf("Got no response\n");
        cMsgDomainClear(&rcDomains[id]);
        staticMutexUnlock();
        return(CMSG_NETWORK_ERROR);
    }
    else {
printf("Got a response!\n");
printf("main connect thread: clientaddr = %p, len = %d\n", &(arg.clientaddr), arg.len);    
    }

    /*
     * Create a UDP "connection". This means all subsequent sends are to be
     * done with the "send" and not the "sendto" function. The benefit is 
     * that the udp socket does not have to connect and disconnect
     * for each message sent.
     */
printf("main connect thread: sending to port = %hu\n", ntohs(arg.clientaddr.sin_port));    
    err = connect(domain->sendSocket, (SA *)&arg.clientaddr, sizeof(arg.clientaddr));
    /*err = connect(sockfd, (SA *)&arg.clientaddr, arg.len);*/
    if (err < 0) {
printf("Cannot do \"connect\" on udp socket = %d\n", domain->sendSocket);
perror("connect");
        cMsgDomainClear(&rcDomains[id]);
        staticMutexUnlock();
        return(CMSG_SOCKET_ERROR);
    }
    
    *domainId = (void *) id;
        
    /* install default shutdown handler (exits program) */
    cmsgd_setShutdownHandler((void *)id, defaultShutdownHandler, NULL);

    rcDomains[id].gotConnection = 1;
    
    /* no more mutex protection is necessary */
    staticMutexUnlock();

    return(CMSG_OK);
}

/*-------------------------------------------------------------------*/

/**
 * This routine starts a thread to receive a return UDP packet from
 * the server due to our initial uni/broadcast.
 */
static void *receiver(void *arg) {

    receiverArg *threadArg = (receiverArg *) arg;
    char buf[1024];
    int err;
    struct sockaddr_in clientaddr;
    socklen_t len;
    struct timespec wait;
    
    wait.tv_sec  = 0;
    wait.tv_nsec = 100000000;
    
    /*
     * Wait .1 sec for the main thread to hit the timed wait
     * before we look for a response.
     */
    nanosleep(&wait, NULL);
    
    /* ignore error as it will be caught later */   
    recvfrom(threadArg->sockfd, (void *)buf, 1024, 0,
             (SA *) &threadArg->clientaddr, &(threadArg->len));
   
    /* Tell main thread we are done. */
    pthread_cond_signal(&cond);
    
    return NULL;
}


/*-------------------------------------------------------------------*/


/**
 * This routine sends a msg to the specified rc server. It is called
 * by the user through cMsgSend() given the appropriate UDL. It is asynchronous
 * and should rarely block. It will only block if the udp socket buffer has
 * reached it limit which is not likely. In this domain cMsgFlush() does nothing
 * and does not need to be called for the message to be sent immediately.<p>
 *
 * @param domainId id of the domain connection
 * @param vmsg pointer to a message structure
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the message argument is null
 * @returns CMSG_LIMIT_EXCEEDED if the message text field is > 1500 bytes (1 packet)
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
static int cmsgd_send(void *domainId, void *vmsg) {  

    int err=CMSG_OK, len, lenText;
    int outGoing[2];
    char buffer[2048];
    cMsgMessage_t *msg = (cMsgMessage_t *) vmsg;
    cMsgDomainInfo *domain = &rcDomains[(int)domainId];
    
    /* clear array */
    bzero(buffer, 2048);
        
    /* check args */
    if (msg == NULL) return(CMSG_BAD_ARGUMENT);
    
    if (msg->text == NULL) {
      lenText = 0;
    }
    else {
      lenText = strlen(msg->text);
    }
    
    if (lenText > 1500) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cmsgd_send: may NOT write > 1 packet (1500 bytes)\n");
      }
      return(CMSG_LIMIT_EXCEEDED);
    }

    /* Cannot run this while connecting/disconnecting */
    cMsgConnectReadLock(domain);
    
    if (domain->initComplete != 1) {
      cMsgConnectReadUnlock(domain);
      return(CMSG_NOT_INITIALIZED);
    }
    if (domain->gotConnection != 1) {
      cMsgConnectReadUnlock(domain);
      return(CMSG_LOST_CONNECTION);
    }
printf("Try sending on udp socket = %d\n", domain->sendSocket);

    /* flag */
    outGoing[0] = htonl(1);
    /* length of "text" string */
    outGoing[1] = htonl(lenText);

    /* copy data into a single buffer */
    memcpy(buffer, (void *)outGoing, sizeof(outGoing));
    len = sizeof(outGoing);
    memcpy(buffer+len, (void *)msg->text, lenText);
    len += lenText;
    buffer[len] = '\0'; /* add null to end of string */
    len += 1;           /* total length of message   */

    /* Send data over (connected) socket. No mutex protection
     * needed here since "write" is atomic and only sends a
     * single packet.
     */
    if (cMsgTcpWrite(domain->sendSocket, (void *) buffer, len) != len) {
    /*if (send(fd, (void *) buffer, len, 0) != len) {*/
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cmsgd_send: write failure\n");
      }
      err = CMSG_NETWORK_ERROR;
    }
    
    /* done protecting communications */
    cMsgConnectReadUnlock(domain);
    
    return(err);

}


/*-------------------------------------------------------------------*/


/** syncSend is not implemented in the rc domain. */
static int cmsgd_syncSend(void *domainId, void *vmsg, int *response) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/** subscribeAndGet is not implemented in the rc domain. */
static int cmsgd_subscribeAndGet(void *domainId, const char *subject, const char *type,
                                 const struct timespec *timeout, void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/** sendAndGet is not implemented in the rc domain. */
static int cmsgd_sendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout,
                            void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/** flush does nothing in the rc domain. */
static int cmsgd_flush(void *domainId) {  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine subscribes to messages of the given subject and type.
 * When a message is received, the given callback is passed the message
 * pointer and the userArg pointer and then is executed. A configuration
 * structure is given to determine the behavior of the callback.
 * This routine is called by the user through cMsgSubscribe() given the
 * appropriate UDL. Only 1 subscription for a specific combination of
 * subject, type, callback and userArg is allowed.
 *
 * @param domainId id of the domain connection
 * @param subject subject of messages subscribed to
 * @param type type of messages subscribed to
 * @param callback pointer to callback to be executed on receipt of message
 * @param userArg user-specified pointer to be passed to the callback
 * @param config pointer to callback configuration structure
 * @param handle pointer to handle (void pointer) to be used for unsubscribing
 *               from this subscription
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the ubject, type, or callback are null
 * @returns CMSG_OUT_OF_MEMORY if all available subscription memory has been used
 * @returns CMSG_ALREADY_EXISTS if an identical subscription already exists
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
static int cmsgd_subscribe(void *domainId, const char *subject, const char *type, cMsgCallbackFunc *callback,
                           void *userArg, cMsgSubscribeConfig *config, void **handle) {

    int i, j, iok=0, jok=0, uniqueId, status, err=CMSG_OK;
    cMsgDomainInfo *domain  = &rcDomains[(int)domainId];
    subscribeConfig *sConfig = (subscribeConfig *) config;
    cbArg *cbarg;
    struct iovec iov[3];
    int fd = domain->sendSocket;


    /* check args */  
    if ( (cMsgCheckString(subject) != CMSG_OK ) ||
         (cMsgCheckString(type)    != CMSG_OK ) ||
         (callback == NULL)                    ) {
      return(CMSG_BAD_ARGUMENT);
    }
    
    cMsgConnectReadLock(domain);

    if (domain->initComplete != 1) {
      cMsgConnectReadUnlock(domain);
      return(CMSG_NOT_INITIALIZED);
    }
    if (domain->gotConnection != 1) {
      cMsgConnectReadUnlock(domain);
      return(CMSG_LOST_CONNECTION);
    }

    /* use default configuration if none given */
    if (config == NULL) {
      sConfig = (subscribeConfig *) cMsgSubscribeConfigCreate();
    }

    /* make sure subscribe and unsubscribe are not run at the same time */
    cMsgSubscribeMutexLock(domain);

    /* add to callback list if subscription to same subject/type exists */
    iok = jok = 0;
    for (i=0; i<CMSG_MAX_SUBSCRIBE; i++) {
      if (domain->subscribeInfo[i].active == 0) {
        continue;
      }

      if ((strcmp(domain->subscribeInfo[i].subject, subject) == 0) && 
          (strcmp(domain->subscribeInfo[i].type, type) == 0) ) {

        iok = 1;
        jok = 0;

        /* scan through callbacks looking for duplicates */ 
        for (j=0; j<CMSG_MAX_CALLBACK; j++) {
	  if (domain->subscribeInfo[i].cbInfo[j].active == 0) {
            continue;
          }

          if ( (domain->subscribeInfo[i].cbInfo[j].callback == callback) &&
               (domain->subscribeInfo[i].cbInfo[j].userArg  ==  userArg))  {

            cMsgSubscribeMutexUnlock(domain);
            cMsgConnectReadUnlock(domain);
            return(CMSG_ALREADY_EXISTS);
          }
        }

        /* scan through callbacks looking for empty space */ 
        for (j=0; j<CMSG_MAX_CALLBACK; j++) {
	  if (domain->subscribeInfo[i].cbInfo[j].active == 0) {

            domain->subscribeInfo[i].cbInfo[j].active   = 1;
	    domain->subscribeInfo[i].cbInfo[j].callback = callback;
	    domain->subscribeInfo[i].cbInfo[j].userArg  = userArg;
            domain->subscribeInfo[i].cbInfo[j].head     = NULL;
            domain->subscribeInfo[i].cbInfo[j].tail     = NULL;
            domain->subscribeInfo[i].cbInfo[j].quit     = 0;
            domain->subscribeInfo[i].cbInfo[j].messages = 0;
            domain->subscribeInfo[i].cbInfo[j].config   = *sConfig;
            
            domain->subscribeInfo[i].numCallbacks++;
            
            cbarg = (cbArg *) malloc(sizeof(cbArg));
            if (cbarg == NULL) {
              cMsgSubscribeMutexUnlock(domain);
              cMsgConnectReadUnlock(domain);
              return(CMSG_OUT_OF_MEMORY);  
            }                        
            cbarg->domainId = (int) domainId;
            cbarg->subIndex = i;
            cbarg->cbIndex  = j;
            cbarg->domain   = domain;
            
            if (handle != NULL) {
              *handle = (void *)cbarg;
            }

            /* start callback thread now */
            status = pthread_create(&domain->subscribeInfo[i].cbInfo[j].thread,
                                    NULL, cMsgCallbackThread, (void *) cbarg);
            if (status != 0) {
              err_abort(status, "Creating callback thread");
            }

            /* release allocated memory */
            if (config == NULL) {
              cMsgSubscribeConfigDestroy((cMsgSubscribeConfig *) sConfig);
            }

	    jok = 1;
            break;
	  }
        }
        break;
      }
    }

    if ((iok == 1) && (jok == 0)) {
      cMsgSubscribeMutexUnlock(domain);
      cMsgConnectReadUnlock(domain);
      return(CMSG_OUT_OF_MEMORY);
    }
    if ((iok == 1) && (jok == 1)) {
      cMsgSubscribeMutexUnlock(domain);
      cMsgConnectReadUnlock(domain);
      return(CMSG_OK);
    }

    /* no match, make new entry */
    iok = 0;
    for (i=0; i<CMSG_MAX_SUBSCRIBE; i++) {
      int len, lenSubject, lenType;
      int outGoing[6];

      if (domain->subscribeInfo[i].active != 0) {
        continue;
      }

      domain->subscribeInfo[i].active  = 1;
      domain->subscribeInfo[i].subject = (char *) strdup(subject);
      domain->subscribeInfo[i].type    = (char *) strdup(type);
      domain->subscribeInfo[i].subjectRegexp = cMsgStringEscape(subject);
      domain->subscribeInfo[i].typeRegexp    = cMsgStringEscape(type);
      domain->subscribeInfo[i].cbInfo[0].active   = 1;
      domain->subscribeInfo[i].cbInfo[0].callback = callback;
      domain->subscribeInfo[i].cbInfo[0].userArg  = userArg;
      domain->subscribeInfo[i].cbInfo[0].head     = NULL;
      domain->subscribeInfo[i].cbInfo[0].tail     = NULL;
      domain->subscribeInfo[i].cbInfo[0].quit     = 0;
      domain->subscribeInfo[i].cbInfo[0].messages = 0;
      domain->subscribeInfo[i].cbInfo[0].config   = *sConfig;

      domain->subscribeInfo[i].numCallbacks++;

      cbarg = (cbArg *) malloc(sizeof(cbArg));
      if (cbarg == NULL) {
        cMsgSubscribeMutexUnlock(domain);
        cMsgConnectReadUnlock(domain);
        return(CMSG_OUT_OF_MEMORY);  
      }                        
      cbarg->domainId = (int) domainId;
      cbarg->subIndex = i;
      cbarg->cbIndex  = 0;
      cbarg->domain   = domain;
      
      if (handle != NULL) {
        *handle = (void *)cbarg;
      }

      /* start callback thread now */
      status = pthread_create(&domain->subscribeInfo[i].cbInfo[0].thread,
                              NULL, cMsgCallbackThread, (void *) cbarg);                              
      if (status != 0) {
        err_abort(status, "Creating callback thread");
      }

      /* release allocated memory */
      if (config == NULL) {
        cMsgSubscribeConfigDestroy((cMsgSubscribeConfig *) sConfig);
      }

      iok = 1;

      /*
       * Pick a unique identifier for the subject/type pair, and
       * send it to the domain server & remember it for future use
       * Mutex protect this operation as many cmsgd_connect calls may
       * operate in parallel on this static variable.
       */
      staticMutexLock();
      uniqueId = subjectTypeId++;
      staticMutexUnlock();
      domain->subscribeInfo[i].id = uniqueId;

      break;
    } /* for i */

    /* done protecting subscribe */
    cMsgSubscribeMutexUnlock(domain);
    cMsgConnectReadUnlock(domain);

    if (iok == 0) {
      err = CMSG_OUT_OF_MEMORY;
    }

    return(err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine unsubscribes to messages of the given handle (which
 * represents a given subject, type, callback, and user argument).
 * This routine is called by the user through
 * cMsgUnSubscribe() given the appropriate UDL. In this domain cMsgFlush()
 * does nothing and does not need to be called for cmsgd_unsubscribe to be
 * started immediately.
 *
 * @param domainId id of the domain connection
 * @param handle void pointer obtained from cmsgd_subscribe
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the handle or its subject, type, or callback are null,
 *                            or the given subscription (thru handle) does not have
 *                            an active subscription or callbacks
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
static int cmsgd_unsubscribe(void *domainId, void *handle) {

    int i, j, status, err=CMSG_OK;
    cMsgDomainInfo *domain = &rcDomains[(int)domainId];
    int fd = domain->sendSocket;
    struct iovec iov[3];
    cbArg           *cbarg;
    subInfo         *subscriptionInfo;
    subscribeCbInfo *callbackInfo;


    /* check args */
    if (handle == NULL) {
      return(CMSG_BAD_ARGUMENT);  
    }

    cbarg = (cbArg *)handle;

    if (cbarg->domainId != (int)domainId  ||
        cbarg->subIndex < 0 ||
        cbarg->cbIndex  < 0 ||
        cbarg->subIndex >= CMSG_MAX_SUBSCRIBE ||
        cbarg->cbIndex  >= CMSG_MAX_CALLBACK    ) {
      return(CMSG_BAD_ARGUMENT);    
    }

    /* convenience variables */
    subscriptionInfo = &domain->subscribeInfo[cbarg->subIndex];
    callbackInfo     = &subscriptionInfo->cbInfo[cbarg->cbIndex];  

    /* if subscription has no active callbacks ... */
    if (!subscriptionInfo->active ||
        !callbackInfo->active     ||
         subscriptionInfo->numCallbacks < 1) {
      return(CMSG_BAD_ARGUMENT);  
    }

    /* gotta have subject, type, and callback */
    if ( (cMsgCheckString(subscriptionInfo->subject) != CMSG_OK ) ||
         (cMsgCheckString(subscriptionInfo->type)    != CMSG_OK ) ||
         (callbackInfo->callback == NULL)                    )  {
      return(CMSG_BAD_ARGUMENT);
    }
    
    cMsgConnectReadLock(domain);

    if (domain->initComplete != 1) {
      cMsgConnectReadUnlock(domain);
      return(CMSG_NOT_INITIALIZED);
    }
    if (domain->gotConnection != 1) {
      cMsgConnectReadUnlock(domain);
      return(CMSG_LOST_CONNECTION);
    }

    /* make sure subscribe and unsubscribe are not run at the same time */
    cMsgSubscribeMutexLock(domain);
        
    /* Delete entry if there was at least 1 callback
     * to begin with and now there are none for this subject/type.
     */
    if (subscriptionInfo->numCallbacks - 1 < 1) {
      /* do the unsubscribe. */
      free(subscriptionInfo->subject);
      free(subscriptionInfo->type);
      free(subscriptionInfo->subjectRegexp);
      free(subscriptionInfo->typeRegexp);
      /* set these equal to NULL so they aren't freed again later */
      subscriptionInfo->subject       = NULL;
      subscriptionInfo->type          = NULL;
      subscriptionInfo->subjectRegexp = NULL;
      subscriptionInfo->typeRegexp    = NULL;
      /* make array space available for another subscription */
      subscriptionInfo->active        = 0;
    }
    
    /* free mem */
    free(cbarg);
    
    /* one less callback */
    subscriptionInfo->numCallbacks--;

    /* tell callback thread to end */
    callbackInfo->quit = 1;

    /* wakeup callback thread */
    status = pthread_cond_broadcast(&callbackInfo->cond);
    if (status != 0) {
      err_abort(status, "Failed callback condition signal");
    }
    
    /*
     * Once this subscription wakes up it sets the array location
     * as inactive/available (callbackInfo->active = 0). Don't do
     * that yet as another subscription may be done
     * (and set subscription->active = 1) before it wakes up
     * and thus not end itself.
     */
    
    /* done protecting unsubscribe */
    cMsgSubscribeMutexUnlock(domain);
    cMsgConnectReadUnlock(domain);

    return(err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine enables the receiving of messages and delivery to callbacks.
 * The receiving of messages is disabled by default and must be explicitly
 * enabled.
 *
 * @param domainId id of the domain connection
 *
 * @returns CMSG_OK if successful
 */   
static int cmsgd_start(void *domainId) {
  rcDomains[(int)domainId].receiveState = 1;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine disables the receiving of messages and delivery to callbacks.
 * The receiving of messages is disabled by default. This routine only has an
 * effect when cMsgReceiveStart() was previously called.
 *
 * @param domainId id of the domain connection
 *
 * @returns CMSG_OK if successful
 */   
static int cmsgd_stop(void *domainId) {
  rcDomains[(int)domainId].receiveState = 0;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine disconnects the client from the RC server.
 *
 * @param domainId id of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 */   
static int cmsgd_disconnect(void *domainId) {

    cMsgDomainInfo *domain = &rcDomains[(int)domainId];
    int i, j, status;
    int fd = domain->sendSocket;
    subscribeCbInfo *subscription;

    if (domain->initComplete != 1) return(CMSG_NOT_INITIALIZED);

    /* When changing initComplete / connection status, protect it */
    cMsgConnectWriteLock(domain);

    domain->gotConnection = 0;

    /* close sending socket */
    close(domain->sendSocket);

    /* close receiving socket */
    close(domain->receiveSocket);

    /* stop listening and client communication threads */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cmsgd_disconnect:cancel listening & client threads\n");
    }

    pthread_cancel(domain->pendThread);
    /* close listening socket */
    close(domain->listenSocket);

    /* terminate all callback threads */
    for (i=0; i<CMSG_MAX_SUBSCRIBE; i++) {
      /* if there is a subscription ... */
      if (domain->subscribeInfo[i].active == 1)  {
        /* search callback list */
        for (j=0; j<CMSG_MAX_CALLBACK; j++) {
          /* convenience variable */
          subscription = &domain->subscribeInfo[i].cbInfo[j];

	  if (subscription->active == 1) {          

            /* tell callback thread to end */
            subscription->quit = 1;

            if (cMsgDebug >= CMSG_DEBUG_INFO) {
              fprintf(stderr, "cmsgd_disconnect:wake up callback thread\n");
            }

            /* wakeup callback thread */
            status = pthread_cond_broadcast(&subscription->cond);
            if (status != 0) {
              err_abort(status, "Failed callback condition signal");
            }
	  }
        }
      }
    }

    /* give the above threads a chance to quit before we reset everytbing */
    sleep(1);

    /* protect the domain array when freeing up a space */
    staticMutexLock();
    /* free memory (non-NULL items), reset variables*/
    cMsgDomainClear(domain);
    staticMutexUnlock();

    cMsgConnectWriteUnlock(domain);

    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/*   shutdown handler functions                                      */
/*-------------------------------------------------------------------*/


/**
 * This routine is the default shutdown handler function.
 * @param userArg argument to shutdown handler 
 */   
static void defaultShutdownHandler(void *userArg) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "Ran default shutdown handler\n");
    }
    exit(-1);      
}


/*-------------------------------------------------------------------*/


/**
 * This routine sets the shutdown handler function.
 *
 * @param domainId id of the domain connection
 * @param handler shutdown handler function
 * @param userArg argument to shutdown handler 
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 */   
static int cmsgd_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler,
                                    void *userArg) {
  
  cMsgDomainInfo *domain = &rcDomains[(int)domainId];

  if (domain->initComplete != 1) return(CMSG_NOT_INITIALIZED);
  
  domain->shutdownHandler = handler;
  domain->shutdownUserArg = userArg;
      
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/


/** shutdownClients is not implemented in the rc domain. */
static int cmsgd_shutdownClients(void *domainId, const char *client, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/** shutdownServers is not implemented in the rc domain. */
static int cmsgd_shutdownServers(void *domainId, const char *server, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * This routine parses, using regular expressions, the RC domain
 * portion of the UDL sent from the next level up" in the API.
 */
static int parseUDL(const char *UDLR,
                    char **host,
                    unsigned short *port,
                    char **junk) {

    int        err, len, bufLength, Port, index;
    unsigned int i;
    char       *p, *udl, *udlLowerCase, *udlRemainder, *pswd;
    char       *buffer;
    const char *pattern = "(([a-zA-Z]+[a-zA-Z0-9\\.\\-]*)|([0-9]+\\.[0-9\\.]+))?:?([0-9]+)?/?(.*)";
    regmatch_t matches[6]; /* we have 6 potential matches: 1 whole, 5 sub */
    regex_t    compiled;
    
    if (UDLR == NULL) {
        return (CMSG_BAD_FORMAT);
    }
    
    /* make a copy */        
    udlRemainder = (char *) strdup(UDLR);
/*printf("parseUDLregex: udl remainder = %s\n", udlRemainder);*/   
 
    /* make a big enough buffer to construct various strings, 256 chars minimum */
    len       = strlen(udlRemainder) + 1;
    bufLength = len < 256 ? 256 : len;    
    buffer    = (char *) malloc(bufLength);
    if (buffer == NULL) {
      return(CMSG_OUT_OF_MEMORY);
    }
    
    /* RC domain UDL is of the form:
     *        cMsg:CC://<host>:<port>/<junk>
     *
     * where we ignore <junk>. But this routine is only passed:
     *       <host>:<port>:/<junk>
     * 
     * Remember that for this domain:
     * 1) port is optional with a default of RC_BROADCAST_PORT (6543)
     * 2) host is optional with a default of 255.255.255.255 (broadcast)
     *    and may be "localhost" or in dotted decimal form
     */

    /* compile regular expression */
    err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED);
    if (err != 0) {
        free(buffer);
        return (CMSG_ERROR);
    }
    
    /* find matches */
    err = cMsgRegexec(&compiled, udlRemainder, 6, matches, 0);
    if (err != 0) {
        /* no match */
        free(buffer);
        return (CMSG_BAD_FORMAT);
    }
    
    /* free up memory */
    cMsgRegfree(&compiled);
            
    /* find host name, default = 255.255.255.255 (broadcast) */
    if (matches[1].rm_so > -1) {
       buffer[0] = 0;
       len = matches[1].rm_eo - matches[1].rm_so;
       strncat(buffer, udlRemainder+matches[1].rm_so, len);
                
        /* if the host is "localhost", find the actual host name */
        if (strcmp(buffer, "localhost") == 0) {
/*printf("parseUDLregex: host = localhost\n");*/
            /* get canonical local host name */
            if (cMsgLocalHost(buffer, bufLength) != CMSG_OK) {
                /* error finding local host so just broadcast */
                strncat(buffer, "255.255.255.255", 15);
                buffer[15] = '\0';
            }
        }
    }
    else {
        strncat(buffer, "255.255.255.255", 15);
        buffer[15] = '\0';
    }
    
    if (host != NULL) {
        *host = (char *)strdup(buffer);
    }    
/*printf("parseUDLregex: host = %s\n", buffer);*/


    /* find port */
    index = 4;
    if (matches[index].rm_so < 0) {
        /* no match for port so use default */
/*printf("parseUDLregex: no port found, use default\n");*/
        Port = RC_BROADCAST_PORT;
        if (cMsgDebug >= CMSG_DEBUG_WARN) {
            fprintf(stderr, "parseUDLregex: guessing that the name server port is %d\n",
                   Port);
        }
    }
    else {
/*printf("parseUDLregex: found port\n");*/
        buffer[0] = 0;
        len = matches[index].rm_eo - matches[index].rm_so;
        strncat(buffer, udlRemainder+matches[index].rm_so, len);        
        Port = atoi(buffer);        
    }

    if (Port < 1024 || Port > 65535) {
      if (host != NULL) free((void *) *host);
      free(udl);
      free(buffer);
      return (CMSG_OUT_OF_RANGE);
    }
               
    if (port != NULL) {
      *port = Port;
    }
/*printf("parseUDLregex: port = %hu\n", Port );*/

    /* find junk */
    index++;
    buffer[0] = 0;
    if (matches[index].rm_so < 0) {
        /* no match */
        if (junk != NULL) {
            *junk = NULL;
        }
    }
    else {
        len = matches[index].rm_eo - matches[index].rm_so;
        strncat(buffer, udlRemainder+matches[index].rm_so, len);
                
        if (junk != NULL) {
            *junk = (char *) strdup(buffer);
        }        
/*printf("parseUDLregex: remainder = %s, len = %d\n", buffer, len);*/
    }


    /* UDL parsed ok */
/*printf("DONE PARSING UDL\n");*/
    free(buffer);
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/*   miscellaneous local functions                                   */
/*-------------------------------------------------------------------*/


/**
 * This routine locks the pthread mutex used when creating unique id numbers
 * and doing the one-time intialization. */
static void staticMutexLock(void) {

  int status = pthread_mutex_lock(&generalMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the pthread mutex used when creating unique id numbers
 * and doing the one-time intialization. */
static void staticMutexUnlock(void) {

  int status = pthread_mutex_unlock(&generalMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}
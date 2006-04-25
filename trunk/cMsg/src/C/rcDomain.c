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
 */  
 

#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#include "errors.h"
#include "cMsgPrivate.h"
#include "cMsgBase.h"
#include "cMsgNetwork.h"
#include "rwlock.h"
#include "regex.h"
#include "cMsgDomain.h"

#ifdef VXWORKS
#include <vxWorks.h>
#endif


/* for c++ */
#ifdef __cplusplus
extern "C" {
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
                             cMsgCallback *callback, void *userArg,
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


static int cmsgd_connect(const char *myUDL, const char *myName, const char *myDescription,
                         const char *UDLremainder, void **domainId) {
  
    unsigned short serverPort;
    char  *serverHost, *expid, buffer[1024];
    int    err, status, len, expidLen, expidLenNet, sockfd, tcpServerPort;
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
    int numLoops = 5;
    int gotResponse = 0;
        
       
    /* clear array */
    bzero(buffer, 1024);
    
    parseUDL(myUDL, &serverHost, &serverPort, NULL);

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
      return(err);
    }

    /* launch pend thread and start listening on receive socket */
    threadArg = (cMsgThreadInfo *) malloc(sizeof(cMsgThreadInfo));
    if (threadArg == NULL) {
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
     * broadcast on local subnet to find CodaComponent server
     *-------------------------------------------------------
     */
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(serverPort);
    
    /* create socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if ( sockfd < 0) {
        /* error */
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
printf("GOT EXPID = %s\n", expid);
    
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
    
    /* broadcast if no host given, otherwise unicast */     
    if (strlen(serverHost) < 1) {
        if ( inet_pton(AF_INET, "255.255.255.255",  &servaddr.sin_addr) < 1) {
          /* error */
        }
    }
    else {
        if ( inet_pton(AF_INET, serverHost,  &servaddr.sin_addr) < 1) {
          /* error */
        }
    }
    
    /* create a thread which will receive any responses to our broadcast */
    bzero(&arg.clientaddr, sizeof(arg.clientaddr));
    arg.len = sizeof(arg.clientaddr);
    arg.port = 0;
    arg.sockfd = sockfd;
    arg.clientaddr.sin_family = AF_INET;
    
    status = pthread_create(&thread, NULL, receiver, (void *)(&arg) );
    if (status != 0) {
        err_abort(status, "Creating keep alive thread");
    }
    
    /* wait for a response for 2 seconds */
    wait.tv_sec  = 2;
    wait.tv_nsec = 0;
    
    while (!gotResponse && numLoops > 0) {
        /* send UDP  packet to rc server */
        sendto(sockfd, (void *)buffer, len, 0, (SA *) &servaddr, sizeof(servaddr));

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
        staticMutexUnlock();
        return(CMSG_NETWORK_ERROR);
    }
    else {
printf("Got a response!\n");
printf("main connect thread: clientaddr = %p, len = %d\n", &(arg.clientaddr), arg.len);    
    }
printf("main connect thread: returned len = %d, sizeof(clientaddr) = %d\n",
       arg.len, sizeof(arg.clientaddr));    

    /*
     * Create a UDP "connection". This means all subsequent sends are to be
     * done with the "send" and not the "sendto" function. The benefit is 
     * that the udp socket does not have to connect, send, and disconnect
     * for each message sent.
     */
printf("main connect thread: sending to port = %hu\n", ntohs(arg.clientaddr.sin_port));    
    err = connect(sockfd, (SA *)&arg.clientaddr, sizeof(arg.clientaddr));
    /*err = connect(sockfd, (SA *)&arg.clientaddr, arg.len);*/
    if (err < 0) {
printf("Cannot do \"connect\" on udp socket = %d\n", sockfd);
perror("connect");
        staticMutexUnlock();
        return(CMSG_SOCKET_ERROR);
    }
    
    *domainId = (void *) sockfd;
        
    staticMutexUnlock();

    return(CMSG_OK);
}

/*-------------------------------------------------------------------*/

/**
 * This routine starts a thread to receive a return UDP packet from
 * the server due to our initial broadcast.
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
        
    err = recvfrom(threadArg->sockfd, (void *)buf, 1024, 0,
                  (SA *) &threadArg->clientaddr, &(threadArg->len));
    if (err < 0) {
        /* error */
    }
    
    /* Tell main thread we are done. */
    pthread_cond_signal(&cond);
    
    return NULL;
}


/*-------------------------------------------------------------------*/


static int cmsgd_send(void *domainId, void *vmsg) {  

    int err=CMSG_OK, len, lenText, fd;
    int outGoing[2];
    char buffer[2048];
    cMsgMessage *msg = (cMsgMessage *) vmsg;
    cMsgDomainInfo *domain = &rcDomains[(int)domainId];
    
    /* clear array */
    bzero(buffer, 2048);
        
    /* Cannot run this while connecting/disconnecting */
    cMsgConnectReadLock(domain);
    
    fd = (int) domainId;
printf("Try sending on udp socket = %d\n", fd);

    if (msg->text == NULL) {
      lenText = 0;
    }
    else {
      lenText = strlen(msg->text);
    }

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
    len += 1;        /* total length of message */

    /* send data over (connected) socket */
    if (cMsgTcpWrite(fd, (void *) buffer, len) != len) {
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


static int cmsgd_disconnect(void *domainId) {
    /* cannot run this simultaneously with any other routine */
    int fd;
    cMsgDomainInfo *domain = &rcDomains[(int)domainId];
    
    cMsgConnectWriteLock(domain);  
      fd = (int) domainId;
      close(fd);
    cMsgConnectWriteUnlock(domain);
    
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int cmsgd_syncSend(void *domainId, void *vmsg, int *response) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int cmsgd_subscribeAndGet(void *domainId, const char *subject, const char *type,
                                 const struct timespec *timeout, void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int cmsgd_sendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout,
                            void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


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
 * appropriate UDL. In this domain cMsgFlush() does nothing and does not
 * need to be called for the subscription to be started immediately.
 * Only 1 subscription for a specific combination of subject, type, callback
 * and userArg is allowed.
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
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement
 *                               subscribe
 * @returns CMSG_ALREADY_EXISTS if an identical subscription already exists
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
static int cmsgd_subscribe(void *domainId, const char *subject, const char *type, cMsgCallback *callback,
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
 * @returns CMSG_BAD_ARGUMENT if the ubject, type, or callback are null
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement
 *                               unsubscribe
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
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
/*   shutdown handler functions                                      */
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


static int cmsgd_shutdownClients(void *domainId, const char *client, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int cmsgd_shutdownServers(void *domainId, const char *server, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * This routine parses, using regular expressions, the cMsg domain
 * portion of the UDL sent from the next level up" in the API.
 */
static int parseUDL(const char *UDL,
                    char **host,
                    unsigned short *port,
                    char **UDLRemainder) {

    int        err, len, bufLength, Port, index;
    unsigned int i;
    char       *p, *udl, *udlLowerCase, *udlRemainder, *pswd;
    char       *buffer;
    const char *pattern = "(([a-zA-Z]+[a-zA-Z0-9\\.\\-]*)|([0-9]+\\.[0-9\\.]+))?:?([0-9]+)?/?(.*)";
    regmatch_t matches[6]; /* we have 6 potential matches: 1 whole, 5 sub */
    regex_t    compiled;
    
    if (UDL == NULL) {
        return (CMSG_BAD_FORMAT);
    }
    
    /* make a copy */
    udl = (char *) strdup(UDL);
    
    /* make a copy in all lower case */
    udlLowerCase = (char *) strdup(UDL);
    for (i=0; i<strlen(udlLowerCase); i++) {
      udlLowerCase[i] = tolower(udlLowerCase[i]);
    }
  
    /* strip off the beginning cMsg:CC:// */
    p = strstr(udlLowerCase, "cc://");
    if (p == NULL) {
      free(udl);
      free(udlLowerCase);
      return(CMSG_BAD_ARGUMENT);  
    }
    index = (int) (p - udlLowerCase);
    free(udlLowerCase);
    
    udlRemainder = udl + index + 5;
/*printf("parseUDLregex: udl remainder = %s\n", udlRemainder);*/   
 
    /* make a big enough buffer to construct various strings, 256 chars minimum */
    len       = strlen(udlRemainder) + 1;
    bufLength = len < 256 ? 256 : len;    
    buffer    = (char *) malloc(bufLength);
    if (buffer == NULL) {
      free(udl);
      return(CMSG_OUT_OF_MEMORY);
    }
    
    /* cMsg domain UDL is of the form:
     *        cMsg:CC://<host>:<port>
     * 
     * Remember that for this domain:
     * 1) the first "cMsg:" is optional
     * 1) port is necessary
     * 2) host is NOT necessary and may be "localhost" and may also includes dots (.)
     */

    /* compile regular expression */
    err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED);
    if (err != 0) {
        free(udl);
        free(buffer);
        return (CMSG_ERROR);
    }
    
    /* find matches */
    err = cMsgRegexec(&compiled, udlRemainder, 6, matches, 0);
    if (err != 0) {
        /* no match */
        free(udl);
        free(buffer);
        return (CMSG_BAD_FORMAT);
    }
    
    /* free up memory */
    cMsgRegfree(&compiled);
            
    /* find host name */
    if (matches[1].rm_so > -1) {
       buffer[0] = 0;
       len = matches[1].rm_eo - matches[1].rm_so;
       strncat(buffer, udlRemainder+matches[1].rm_so, len);
                
        /* if the host is "localhost", find the actual host name */
        if (strcmp(buffer, "localhost") == 0) {
/*printf("parseUDLregex: host = localhost\n");*/
            /* get canonical local host name */
            if (cMsgLocalHost(buffer, bufLength) != CMSG_OK) {
                /* error */
                host = NULL;
            }
        }
        
        if (host != NULL) {
            *host = (char *)strdup(buffer);
        }
/*printf("parseUDLregex: host = %s\n", buffer);*/
    }


    /* find port */
    index = 4;
    if (matches[index].rm_so < 0) {
        /* no match for port so use default */
/*printf("parseUDLregex: no port found, use default\n");*/
        Port = CC_BROADCAST_PORT;
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

    /* find subdomain remainder */
    index++;
    buffer[0] = 0;
    if (matches[index].rm_so < 0) {
        /* no match */
        if (UDLRemainder != NULL) {
            *UDLRemainder = NULL;
        }
    }
    else {
        len = matches[index].rm_eo - matches[index].rm_so;
        strncat(buffer, udlRemainder+matches[index].rm_so, len);
                
        if (UDLRemainder != NULL) {
            *UDLRemainder = (char *) strdup(buffer);
        }        
/*printf("parseUDLregex: remainder = %s, len = %d\n", buffer, len);*/
    }


    /* UDL parsed ok */
/*printf("DONE PARSING UDL\n");*/
    free(udl);
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




#ifdef __cplusplus
}
#endif

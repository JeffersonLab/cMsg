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
 * The server(s) which communicate with this cMsg user must use 2 domains, the
 * rcm and rcs domains. The rc domain relies heavily on the cMsg domain
 * code which it uses as a library. It mainly relies on that code to implement
 * subscriptions/callbacks.
 */  
 

#ifdef VXWORKS
#include <vxWorks.h>
#include <sysLib.h>
#include <sockLib.h>
#include <hostLib.h>
#else
#include <strings.h>
#include <sys/time.h>    /* struct timeval */
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>

#include "cMsgPrivate.h"
#include "cMsg.h"
#include "cMsgNetwork.h"
#include "rwlock.h"
#include "regex.h"
#include "cMsgDomain.h"



/**
 * Structure for arg to be passed to receiver/multicast threads.
 * Allows data to flow back and forth with these threads.
 */
typedef struct thdArg_t {
    int sockfd;
    socklen_t len;
    unsigned short port;
    struct sockaddr_in addr;
    struct sockaddr_in *paddr;
    int   bufferLen;
    char *expid;
    char *buffer;
} thdArg;

/* built-in limits */
/** Number of seconds to wait for cMsgClientListeningThread threads to start. */
#define WAIT_FOR_THREADS 10

/* global variables */
/** Function in cMsgServer.c which implements the network listening thread of a client. */
void *rcClientListeningThread(void *arg);

/* local variables */
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
static void *receiverThd(void *arg);
static void *multicastThd(void *arg);
static int   udpSend(cMsgDomainInfo *domain, cMsgMessage_t *msg);
static void  defaultShutdownHandler(void *userArg);
static int   parseUDL(const char *UDLR, char **host,
                      unsigned short *port, char **expid,
                      int  *multicastTO, int *connectTO, char **junk);
                      
/* Prototypes of the 17 functions which implement the standard tasks in cMsg. */
int   cmsg_rc_connect           (const char *myUDL, const char *myName,
                                 const char *myDescription,
                                 const char *UDLremainder, void **domainId);
int   cmsg_rc_reconnect         (void *domainId);
int   cmsg_rc_send              (void *domainId, void *msg);
int   cmsg_rc_syncSend          (void *domainId, void *msg, const struct timespec *timeout,
                                 int *response);
int   cmsg_rc_flush             (void *domainId, const struct timespec *timeout);
int   cmsg_rc_subscribe         (void *domainId, const char *subject, const char *type,
                                 cMsgCallbackFunc *callback, void *userArg,
                                 cMsgSubscribeConfig *config, void **handle);
int   cmsg_rc_unsubscribe       (void *domainId, void *handle);
int   cmsg_rc_subscriptionPause (void *domainId, void *handle);
int   cmsg_rc_subscriptionResume(void *domainId, void *handle);
int   cmsg_rc_subscriptionQueueClear(void *domainId, void *handle);
int   cmsg_rc_subscriptionQueueCount(void *domainId, void *handle, int *count);
int   cmsg_rc_subscriptionQueueIsFull(void *domainId, void *handle, int *full);
int   cmsg_rc_subscriptionMessagesTotal(void *domainId, void *handle, int *total);
int   cmsg_rc_subscribeAndGet   (void *domainId, const char *subject, const char *type,
                                 const struct timespec *timeout, void **replyMsg);
int   cmsg_rc_sendAndGet        (void *domainId, void *sendMsg,
                                 const struct timespec *timeout, void **replyMsg);
int   cmsg_rc_monitor           (void *domainId, const char *command, void **replyMsg);
int   cmsg_rc_start             (void *domainId);
int   cmsg_rc_stop              (void *domainId);
int   cmsg_rc_disconnect        (void **domainId);
int   cmsg_rc_shutdownClients   (void *domainId, const char *client, int flag);
int   cmsg_rc_shutdownServers   (void *domainId, const char *server, int flag);
int   cmsg_rc_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg);
int   cmsg_rc_isConnected       (void *domainId, int *connected);
int   cmsg_rc_setUDL            (void *domainId, const char *udl, const char *remainder);
int   cmsg_rc_getCurrentUDL     (void *domainId, const char **udl);

/** List of the functions which implement the standard cMsg tasks in the cMsg domain. */
static domainFunctions functions = {cmsg_rc_connect, cmsg_rc_reconnect,
                                    cmsg_rc_send, cmsg_rc_syncSend, cmsg_rc_flush,
                                    cmsg_rc_subscribe, cmsg_rc_unsubscribe,
                                    cmsg_rc_subscriptionPause, cmsg_rc_subscriptionResume,
                                    cmsg_rc_subscriptionQueueClear, cmsg_rc_subscriptionMessagesTotal,
                                    cmsg_rc_subscriptionQueueCount, cmsg_rc_subscriptionQueueIsFull,
                                    cmsg_rc_subscribeAndGet, cmsg_rc_sendAndGet,
                                    cmsg_rc_monitor, cmsg_rc_start,
                                    cmsg_rc_stop, cmsg_rc_disconnect,
                                    cmsg_rc_shutdownClients, cmsg_rc_shutdownServers,
                                    cmsg_rc_setShutdownHandler, cmsg_rc_isConnected,
                                    cmsg_rc_setUDL, cmsg_rc_getCurrentUDL};

/* rc domain type */
domainTypeInfo rcDomainTypeInfo = {
  "rc",
  &functions
};

/*-------------------------------------------------------------------*/


/**
 * This routine resets the UDL, but is <b>NOT</b> implemented in this domain.
 *
 * @param domainId id of the domain connection
 * @param newUDL new UDL
 * @param newRemainder new UDL remainder
 *
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */
int cmsg_rc_setUDL(void *domainId, const char *newUDL, const char *newRemainder) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * This routine gets the UDL current used in the existing connection.
 *
 * @param udl pointer filled in with current UDL (do not write to this
              pointer)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if domainId arg is NULL
 */
int cmsg_rc_getCurrentUDL(void *domainId, const char **udl) {
    cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
    
    /* check args */
    if (domain == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }
    if (udl != NULL) *udl = domain->udl;
    return(CMSG_OK);
}



/*-------------------------------------------------------------------*/


/**
 * This routine tells whether a client is connected or not.
 *
 * @param domain id of the domain connection
 * @param connected pointer whose value is set to 1 if this client is connected,
 *                  else it is set to 0
 *
 * @returns CMSG_OK
 */
int cmsg_rc_isConnected(void *domainId, int *connected) {
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  
  if (domain == NULL) {
    if (connected != NULL) {
      *connected = 0;
    }
    return(CMSG_OK);
  }
             
  cMsgConnectReadLock(domain);

  if (connected != NULL) {
    *connected = domain->gotConnection;
  }
  
  cMsgConnectReadUnlock(domain);
    
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine is called once to connect to an RC domain. It is called
 * by the user through top-level cMsg API, "cMsgConnect()".
 * The argument "myUDL" is the Universal Domain Locator used to uniquely
 * identify the RC server to connect to.
 * It has the form:<p>
 *       <b>cMsg:rc://&lt;host&gt;:&lt;port&gt;/</b><p>
 * where the first "cMsg:" is optional. Both the host and port are also
 * optional. If a host is NOT specified, a multicast on the local subnet
 * is used to locate the server. If the port is omitted, a default port
 * (RC_MULTICAST_PORT) is used.
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
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if the EXPID is not defined
 * @returns CMSG_BAD_FORMAT if UDLremainder arg is not in the proper format
 * @returns CMSG_BAD_ARGUMENT if UDLremainder arg is NULL
 * @returns CMSG_ABORT if RC Multicast server aborts connection before rc server
 *                     can complete it
 * @returns CMSG_OUT_OF_RANGE if the port specified in the UDL is out-of-range
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 * @returns CMSG_TIMEOUT if timed out of wait for either response to multicast or
 *                       for rc server to complete connection
 *
 * @returns CMSG_SOCKET_ERROR if the listening thread finds all the ports it tries
 *                            to listen on are busy, tcp socket to rc server could
 *                            not be created, udp socket for sending to rc server
 *                            could not be created, could not connect udp socket to
 *                            rc server once created, udp socket for multicasting
 *                            could not be created, or socket options could not be set.
 *                            
 * @returns CMSG_NETWORK_ERROR if host name in UDL or rc server's host could not be resolved, or
 *                             no connection to the rc server can be made, or
 *                             a communication error with server occurs.
 */   
int cmsg_rc_connect(const char *myUDL, const char *myName, const char *myDescription,
                    const char *UDLremainder, void **domainId) {
  
    unsigned short serverPort;
    char  *serverHost, *expid=NULL, buffer[1024];
    int    err, status, len, expidLen, nameLen;
    int    i, index=0, outGoing[7], multicastTO=0, connectTO=0;
    char   temp[CMSG_MAXHOSTNAMELEN];
    char  *portEnvVariable=NULL;
    unsigned char ttl = 32;
    unsigned short startingPort;
    cMsgDomainInfo *domain;
    cMsgThreadInfo *threadArg;
    int    hz, num_try, try_max;
    struct timespec waitForThread;
    
    pthread_t rThread, bThread;
    thdArg    rArg,    bArg;
    
    struct timespec wait, time;
    struct sockaddr_in servaddr, addr;
    int    gotResponse=0;
    const int size=CMSG_BIGSOCKBUFSIZE; /* bytes */
        
    /* for connecting to rc Server w/ TCP */
    hashNode *hashEntries = NULL;
    int hashEntryCount=0, haveHashEntries=0, gotValidRcServerHost=0;
    char *rcServerHost = NULL;
    struct timeval tv = {0, 300000}; /* 0.3 sec wait for rc Server to respond */
    
    /* clear array */
    memset((void *)buffer, 0, 1024);
    
    /* parse the UDLRemainder to get the host and port but ignore everything else */
    err = parseUDL(UDLremainder, &serverHost, &serverPort,
                   &expid, &multicastTO, &connectTO, NULL);
    if (err != CMSG_OK) {
        return(err);
    }

    /*
     * The EXPID is obtained from the UDL, but if it's not defined there
     * then use the environmental variable EXPID 's value.
     */
    if (expid == NULL) {
        expid = getenv("EXPID");
        if (expid != NULL) {
            expid = (char *)strdup(expid);
        }
        else {
            /* if expid not defined anywhere, return error */
printf("EXPID is not set!\n");
            return(CMSG_ERROR);
        }
    }

    /* allocate struct to hold connection info */
    domain = (cMsgDomainInfo *) calloc(1, sizeof(cMsgDomainInfo));
    if (domain == NULL) {
        free(serverHost);
        free(expid);
        return(CMSG_OUT_OF_MEMORY);  
    }
    cMsgDomainInit(domain);  

    /* allocate memory for message-sending buffer */
    domain->msgBuffer     = (char *) malloc(initialMsgBufferSize);
    domain->msgBufferSize = initialMsgBufferSize;
    if (domain->msgBuffer == NULL) {
        cMsgDomainFree(domain);
        free(domain);
        free(serverHost);
        free(expid);
        return(CMSG_OUT_OF_MEMORY);
    }

    /* store our host's name */
    gethostname(temp, CMSG_MAXHOSTNAMELEN);
    domain->myHost = (char *) strdup(temp);

    /* store names, can be changed until server connection established */
    domain->name        = (char *) strdup(myName);
    domain->udl         = (char *) strdup(myUDL);
    domain->description = (char *) strdup(myDescription);

    /*--------------------------------------------------------------------------
     * First find a port on which to receive incoming messages.
     * Do this by trying to open a listening socket at a given
     * port number. If that doesn't work add one to port number
     * and try again.
     * 
     * But before that, define a port number from which to start looking.
     * If CMSG_PORT is defined, it's the starting port number.
     * If CMSG_PORT is NOT defind, start at RC_CLIENT_LISTENING_PORT (45800).
     *-------------------------------------------------------------------------*/
    
    /* pick starting port number */
    if ( (portEnvVariable = getenv("CMSG_RC_CLIENT_PORT")) == NULL ) {
        startingPort = RC_CLIENT_LISTENING_PORT;
        if (cMsgDebug >= CMSG_DEBUG_WARN) {
            fprintf(stderr, "cmsg_rc_connectImpl: cannot find CMSG_PORT env variable, first try port %hu\n", startingPort);
        }
    }
    else {
        i = atoi(portEnvVariable);
        if (i < 1025 || i > 65535) {
            startingPort = RC_CLIENT_LISTENING_PORT;
            if (cMsgDebug >= CMSG_DEBUG_WARN) {
                fprintf(stderr, "cmsg_rc_connect: CMSG_PORT contains a bad port #, first try port %hu\n", startingPort);
            }
        }
        else {
            startingPort = i;
        }
    }
     
    /* get listening port and socket for this application */
    if ( (err = cMsgNetGetListeningSocket(0, startingPort, 0, 0, 1,
                                          &domain->listenPort,
                                          &domain->listenSocket)) != CMSG_OK) {
        cMsgDomainFree(domain);
        free(domain);
        free(serverHost);
        free(expid);
        return(err); /* CMSG_SOCKET_ERROR if cannot find available port */
    }
    
printf("rc connect: create listening socket on port %d\n", domain->listenPort );

    /* launch pend thread and start listening on receive socket */
    threadArg = (cMsgThreadInfo *) malloc(sizeof(cMsgThreadInfo));
    if (threadArg == NULL) {
        cMsgDomainFree(domain);
        free(domain);
        free(serverHost);
        free(expid);
        return(CMSG_OUT_OF_MEMORY);  
    }
    threadArg->isRunning   = 0;
    threadArg->thdstarted  = 0;
    threadArg->listenFd    = domain->listenSocket;
    threadArg->blocking    = CMSG_NONBLOCKING;
    threadArg->domain      = domain;
    
    /* Block SIGPIPE for this and all spawned threads. */
    cMsgBlockSignals(domain);

printf("rc connect: start pend thread\n");
    status = pthread_create(&domain->pendThread, NULL,
                            rcClientListeningThread, (void *) threadArg);
    if (status != 0) {
        cmsg_err_abort(status, "Creating TCP message listening thread");
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
    hz = (int) sysconf(_SC_CLK_TCK);
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
            fprintf(stderr, "cmsg_rc_connect, cannot start listening thread\n");
        }
        exit(-1);
    }

    /* Mem allocated for the argument passed to listening thread is 
     * now freed in the pthread cancellation cleanup handler.in
     * rcDomainListenThread.c
     */
    /*free(threadArg);*/

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "cmsg_rc_connect: created listening thread\n");
    }

    /*-------------------------------------------------------
     * Talk to runcontrol multicast server
     *-------------------------------------------------------*/
    
    memset((void *)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port   = htons(serverPort);
    
    /* create UDP socket */
    domain->sendSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (domain->sendSocket < 0) {
        cMsgRestoreSignals(domain);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        free(serverHost);
        free(expid);
        return(CMSG_SOCKET_ERROR);
    }

    /* Set TTL to 32 so it will make it through routers. */
    err = setsockopt(domain->sendSocket, IPPROTO_IP, IP_MULTICAST_TTL, (void *) &ttl, sizeof(ttl));
    if (err < 0) {
        close(domain->sendSocket);
        cMsgRestoreSignals(domain);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        free(serverHost);
        free(expid);
        return(CMSG_SOCKET_ERROR);
    }

    if ( (err = cMsgNetStringToNumericIPaddr(serverHost, &servaddr)) != CMSG_OK ) {
        close(domain->sendSocket);
        cMsgRestoreSignals(domain);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        free(serverHost);
        free(expid);
        /* only possible errors are:
           CMSG_NETWORK_ERROR if the numeric address could not be obtained
           CMSG_OUT_OF_MEMORY if out of memory
        */
        return(err);
    }
    
    /*
     * We send 4 items explicitly:
     *   1) Type of multicast (rc or cMsg domain),
     *   1) TCP listening port of this client,
     *   2) name of this client, &
     *   2) EXPID (experiment id string)
     * The host we're sending from gets sent for free
     * as does the UDP port we're sending from.
     */

printf("rc connect: sending info (listening tcp port = %d, expid = %s) to server on port = %hu on host %s\n",
        ((int) domain->listenPort), expid, serverPort, serverHost);
    
    nameLen  = strlen(myName);
    expidLen = strlen(expid);
    
    /* magic #s */
    outGoing[0] = htonl(CMSG_MAGIC_INT1);
    outGoing[1] = htonl(CMSG_MAGIC_INT2);
    outGoing[2] = htonl(CMSG_MAGIC_INT3);
    /* type of multicast */
    outGoing[3] = htonl(RC_DOMAIN_MULTICAST);
    /* tcp port */
    outGoing[4] = htonl((int) domain->listenPort);
    /* length of "myName" string */
    outGoing[5] = htonl(nameLen);
    /* length of "expid" string */
    outGoing[6] = htonl(expidLen);

    /* copy data into a single buffer */
    memcpy(buffer, (void *)outGoing, sizeof(outGoing));
    len = sizeof(outGoing);
    memcpy(buffer+len, (const void *)myName, nameLen);
    len += nameLen;
    memcpy(buffer+len, (const void *)expid, expidLen);
    len += expidLen;
        
    free(serverHost);
    
    /* create and start a thread which will receive any responses to our multicast */
    memset((void *)&rArg.addr, 0, sizeof(rArg.addr));
    rArg.len             = (socklen_t) sizeof(rArg.addr);
    rArg.port            = serverPort;
    rArg.expid           = expid;
    rArg.sockfd          = domain->sendSocket;
    rArg.addr.sin_family = AF_INET;
    
/*printf("rc connect: will create receiver thread\n");*/
    status = pthread_create(&rThread, NULL, receiverThd, (void *)(&rArg));
    if (status != 0) {
        cmsg_err_abort(status, "Creating multicast response receiving thread");
    }
    
    /* create and start a thread which will multicast every second */
    bArg.len       = (socklen_t) sizeof(servaddr);
    bArg.sockfd    = domain->sendSocket;
    bArg.paddr     = &servaddr;
    bArg.buffer    = buffer;
    bArg.bufferLen = len;
    
/*printf("rc connect: will create sender thread\n");*/
    status = pthread_create(&bThread, NULL, multicastThd, (void *)(&bArg));
    if (status != 0) {
        cmsg_err_abort(status, "Creating multicast sending thread");
    }
    
    /* Wait for a response. If multicastTO is given in the UDL, use that.
     * The default wait or the wait if multicastTO is set to 0, is forever.
     * Round things to the nearest second since we're only multicasting a
     * message every second anyway.
     */    
    if (multicastTO > 0) {
        wait.tv_sec  = multicastTO;
        wait.tv_nsec = 0;
        cMsgGetAbsoluteTime(&wait, &time);
        
        status = pthread_mutex_lock(&mutex);
        if (status != 0) {
            cmsg_err_abort(status, "pthread_mutex_lock");
        }
 
/*printf("rc connect: wait %d seconds for multicast server to answer\n", multicastTO);*/
        status = pthread_cond_timedwait(&cond, &mutex, &time);
        if (status == ETIMEDOUT) {
          /* stop receiving thread */
          pthread_cancel(rThread);
        }
        else if (status != 0) {
            cmsg_err_abort(status, "pthread_cond_timedwait");
        }
        else {
            gotResponse = 1;
        }
        
        status = pthread_mutex_unlock(&mutex);
        if (status != 0) {
            cmsg_err_abort(status, "pthread_mutex_lock");
        }
    }
    else {
        status = pthread_mutex_lock(&mutex);
        if (status != 0) {
            cmsg_err_abort(status, "pthread_mutex_lock");
        }
 
/*printf("rc connect: wait forever for multicast server to answer\n");*/
        status = pthread_cond_wait(&cond, &mutex);
        if (status != 0) {
            cmsg_err_abort(status, "pthread_cond_timedwait");
        }
        gotResponse = 1;
        
        status = pthread_mutex_unlock(&mutex);
        if (status != 0) {
            cmsg_err_abort(status, "pthread_mutex_lock");
        }
    }
    
    /* stop multicasting thread */
    pthread_cancel(bThread);
    free(expid);
    
    if (!gotResponse) {
/*printf("rc connect: got no response\n");*/
        close(domain->sendSocket);
        cMsgRestoreSignals(domain);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        return(CMSG_TIMEOUT);
    }
    
/*printf("rc connect: got a response from mcast server, now wait for connect to finish\n");*/

    /* Wait for a special message to come in to the TCP listening thread.
     * The message will contain the host and UDP port of the destination
     * of our subsequent sends. If connectTO is given in the UDL, use that.
     * Otherwise, wait for a maximum of 30 seconds before timing out of this
     * connect call.
     */
     
    if (connectTO > 0) {
        wait.tv_sec  = connectTO;
        wait.tv_nsec = 0;
    }
        
    cMsgMutexLock(&domain->rcConnectMutex);
    
    if (connectTO > 0) {
/*printf("rc connect: wait on latch for connect to finish in %d seconds\n", connectTO);*/
        status = cMsgLatchAwait(&domain->syncLatch, &wait);
    }
    else {
/*printf("rc connect: wait on latch FOREVER for connect to finish\n");*/
        status = cMsgLatchAwait(&domain->syncLatch, NULL);
    }
/*printf("rc connect: got a response from rc server, 3-way connect finished, now make 2 connections to rc server\n");*/

    /* Told by multicast server to stop waiting for the
     * rc Server to finish the connection. */
    if (domain->rcConnectAbort) {
/*printf("rc connect: told to abort connect by RC Multicast server\n");*/
        close(domain->sendSocket);
        cMsgRestoreSignals(domain);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        return(CMSG_ABORT);
    }
    
    if (status < 1 || !domain->rcConnectComplete) {
/*printf("rc connect: wait timeout or rcConnectComplete is not 1\n");*/
        close(domain->sendSocket);
        cMsgRestoreSignals(domain);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        return(CMSG_TIMEOUT);
    }
        
    close(domain->sendSocket);

    cMsgMutexUnlock(&domain->rcConnectMutex);

    /* create TCP sending socket and store */
/*printf("rc connect: will make tcp connection to RC server\n");*/

    /* The rc Server may have multiple network interfaces.
     * The ip address of each is stored in a hash table.
     * Try one at a time to see which we can use to connect. */
    haveHashEntries = hashGetAll(&domain->rcIpAddrTable, &hashEntries, &hashEntryCount);

    /* if there are no hash table entries, try old way */
    if (!haveHashEntries || hashEntryCount < 1) {
/*printf("rc connect: try old way to make tcp connection to RC server = %s\n", domain->sendHost);*/
        rcServerHost = domain->sendHost;
        if ( (err = cMsgNetTcpConnect(rcServerHost, NULL, (unsigned short) domain->sendPort,
                                      CMSG_BIGSOCKBUFSIZE, 0, 1, &domain->sendSocket, NULL)) != CMSG_OK) {
            if (hashEntries != NULL) free(hashEntries);
            cMsgRestoreSignals(domain);
            pthread_cancel(domain->pendThread);
            cMsgDomainFree(domain);
            free(domain);
            /*
             * Besides out-of-mem or bad arg we can have:
             * CMSG_SOCKET_ERROR if socket could not be created or socket options could not be set.
             * CMSG_NETWORK_ERROR if sendHost name could not be resolved or could not connect.
             */
            return(err);
        }
    }
    else {
        for(i=0; i < hashEntryCount; i++) {
            rcServerHost = hashEntries[i].key;
/*printf("rc connect: try making tcp connection to RC server = %s w/ TO = %u sec, %u msec\n", rcServerHost,
  (uint_32) ((&tv)->tv_sec),(uint_32) ((&tv)->tv_usec));*/
            if ((err = cMsgNetTcpConnectTimeout(rcServerHost, (unsigned short) domain->sendPort,
                 CMSG_BIGSOCKBUFSIZE, 0, 1, &tv, &domain->sendSocket, NULL)) == CMSG_OK) {
                gotValidRcServerHost = 1;
                printf("rc connect: SUCCESS connecting to %s\n", rcServerHost);
                break;
            }
            printf("rc connect: failed trying to connect to %s w/ TO = %u msec\n", rcServerHost, tv.tv_usec);
        }

        if (!gotValidRcServerHost) {
            if (hashEntries != NULL) free(hashEntries);
            cMsgRestoreSignals(domain);
            pthread_cancel(domain->pendThread);
            cMsgDomainFree(domain);
            free(domain);
            return(err);
        }
    }

    /* Even though we free hash entries array, rcServerHost
     * is still pointing to valid string inside hashtable. */
    if (hashEntries != NULL) free(hashEntries);

    /*
     * Create a new UDP "connection". This means all subsequent sends are to
     * be done with the "send" and not the "sendto" function. The benefit is 
     * that the udp socket does not have to connect and disconnect for each
     * message sent.
     */
    memset((void *)&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(domain->sendUdpPort);
    
    /* create new UDP socket for sends */
    if (domain->sendUdpSocket > -1) {
        close(domain->sendUdpSocket); /* close old UDP socket */
    }
    domain->sendUdpSocket = socket(AF_INET, SOCK_DGRAM, 0);

    if (domain->sendUdpSocket < 0) {
        cMsgRestoreSignals(domain);
        close(domain->sendSocket);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        return(CMSG_SOCKET_ERROR);
    }

    /* set send buffer size */
    err = setsockopt(domain->sendUdpSocket, SOL_SOCKET, SO_SNDBUF, (char*) &size, sizeof(size));
    if (err < 0) {
        cMsgRestoreSignals(domain);
        close(domain->sendSocket);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        return(CMSG_SOCKET_ERROR);
    }

    /* convert string host into binary numeric host */
    if ( (err = cMsgNetStringToNumericIPaddr(rcServerHost, &addr)) != CMSG_OK ) {
        cMsgRestoreSignals(domain);
        close(domain->sendUdpSocket);
        close(domain->sendSocket);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        return(err);
    }

/*rintf("rc connect: try UDP connection rc server on port = %hu\n", ntohs(addr.sin_port));*/
    err = connect(domain->sendUdpSocket, (SA *)&addr, sizeof(addr));
    if (err < 0) {
        cMsgRestoreSignals(domain);
        close(domain->sendUdpSocket);
        close(domain->sendSocket);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        return(CMSG_SOCKET_ERROR);
    }
   
    /* return id */
    *domainId = (void *) domain;
        
    /* install default shutdown handler (exits program) */
    cmsg_rc_setShutdownHandler((void *)domain, defaultShutdownHandler, NULL);

    domain->gotConnection = 1;
/*printf("rc connect: DONE\n");*/
    
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine reconnects the client to the RC server.
 *
 * @param domainId id of the domain connection
 *
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */
int cmsg_rc_reconnect(void *domainId) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/

/**
 * This routine starts a thread to receive a return UDP packet from
 * the server due to our initial uni/multicast.
 */
static void *receiverThd(void *arg) {

    thdArg *threadArg = (thdArg *) arg;
    int  ints[6], magic[3], port, len1, len2, minMsgSize;
    char buf[1024], *pbuf, *tmp, *host=NULL, *expid=NULL;
    ssize_t len;

    minMsgSize = 6*sizeof(int);
    
    /* release resources when done */
    pthread_detach(pthread_self());
    
    while (1) {
        /* zero buffer */
        memset((void *)buf,0,1024);

        /* ignore error as it will be caught later */   
        len = recvfrom(threadArg->sockfd, (void *)buf, 1024, 0,
                       (SA *) &threadArg->addr, &(threadArg->len));
        
        /* server is sending:
         *         -> 3 magic ints
         *         -> port server is listening on
         *         -> length of server's host name
         *         -> length of server's expid
         *         -> server's host name
         *         -> server's expid
         */
        if (len < minMsgSize) {
          /*printf("receiverThd: got packet that's too small\n");*/
          continue;
        }
        
        pbuf = buf;
        memcpy(ints, pbuf, sizeof(ints));
        pbuf += sizeof(ints);

        magic[0] = ntohl(ints[0]);
        magic[1] = ntohl(ints[1]);
        magic[2] = ntohl(ints[2]);
        if (magic[0] != CMSG_MAGIC_INT1 ||
            magic[1] != CMSG_MAGIC_INT2 ||
            magic[2] != CMSG_MAGIC_INT3)  {
          printf("receiverThd: got bogus magic # response to multicast\n");
          continue;
        }

        port = ntohl(ints[3]);

        if (port != threadArg->port) {
          printf("receiverThd: got bogus port response to multicast\n");
          continue;
        }

        len1 = ntohl(ints[4]);
        len2 = ntohl(ints[5]);
        
        if (len < minMsgSize + len1 + len2) {
          printf("receiverThd: got packet that's too small or internal error\n");
          continue;
        }

        /* host */
        if (len1 > 0) {
          if ( (tmp = (char *) malloc(len1+1)) == NULL) {
            printf("receiverThd: out of memory\n");
            exit(-1);
          }
          /* read host string into memory */
          memcpy(tmp, pbuf, len1);
          /* add null terminator to string */
          tmp[len1] = 0;
          /* store string */
          host = tmp;
          /* go to next string */
          pbuf += len1;
/*printf("host = %s\n", host);*/
        }
                
        if (len2 > 0) {
          if ( (tmp = (char *) malloc(len2+1)) == NULL) {
            printf("receiverThd: out of memory\n");
            exit(-1);
          }
          memcpy(tmp, pbuf, len2);
          tmp[len2] = 0;
          expid = tmp;
          pbuf += len2;
/*printf("expid = %s\n", expid);*/
          if (strcmp(expid, threadArg->expid) != 0) {
            printf("receiverThd: got bogus expid response to multicast\n");
            continue;
          }
        }
/*
printf("Multicast response from: %s, on port %hu, listening port = %d, host = %s, expid = %s\n",
                inet_ntoa(threadArg->addr.sin_addr),
                ntohs(threadArg->addr.sin_port),
                port, host, expid);
*/                        
        /* Tell main thread we are done. */
        pthread_cond_signal(&cond);
        break;
    }
    
    pthread_exit(NULL);
    return NULL;
}


/*-------------------------------------------------------------------*/

/**
 * This routine starts a thread to multicast a UDP packet to the server
 * every second.
 */
static void *multicastThd(void *arg) {

    char **ifNames;
    int i, err, useDefaultIf=0, count;
    thdArg *threadArg = (thdArg *) arg;
    struct timespec wait = {0, 100000000}; /* 0.1 sec */
    
    /* release resources when done */
    pthread_detach(pthread_self());
    
    /* A slight delay here will help the main thread (calling connect)
     * to be already waiting for a response from the server when we
     * multicast to the server here (prompting that response). This
     * will help insure no responses will be lost.
     */
    nanosleep(&wait, NULL);
    
    err = cMsgNetGetIfNames(&ifNames, &count);
    if (err != CMSG_OK || count < 1 || ifNames == NULL) {
        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "multicastThd: cannot find network interface info, use defaults\n");
        }
        useDefaultIf = 1;
    }

    while (1) {

        if (useDefaultIf) {
            sendto(threadArg->sockfd, (void *)threadArg->buffer, threadArg->bufferLen, 0,
                   (SA *) threadArg->paddr, threadArg->len);
        }
        else {
            for (i=0; i < count; i++) {
//                if (cMsgDebug >= CMSG_DEBUG_INFO) {
                    printf("multicastThd: send mcast on interface %s\n", ifNames[i]);
//                }

                /* set socket to send over this interface */
                err = cMsgNetMcastSetIf(threadArg->sockfd, ifNames[i], 0);
                if (err != CMSG_OK) continue;
    
/*printf("Send multicast to RC Multicast server\n");*/
                sendto(threadArg->sockfd, (void *)threadArg->buffer, threadArg->bufferLen, 0,
                       (SA *) threadArg->paddr, threadArg->len);
            }
        }
      
        sleep(1);
    }
    
    /* free memory */
    if (ifNames != NULL) {
        for (i=0; i < count; i++) {
            free(ifNames[i]);
        }
        free(ifNames);
    }

    pthread_exit(NULL);
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
 * This routine only sends the following messge fields:
 * <ol>
 *   <li>cMsg version</li>
 *   <li>user int</li>
 *   <li>info</li>
 *   <li>sender time</li>
 *   <li>user time</li>
 *   <li>sender</li>
 *   <li>subject</li>
 *   <li>type</li>
 *   <li>payload text</li>
 *   <li>text</li>
 *   <li>byte array</li>
 * </ol>
 * @param domainId id of the domain connection
 * @param vmsg pointer to a message structure
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id or message argument is null
 * @returns CMSG_LIMIT_EXCEEDED if the message text field is > 1500 bytes (1 packet)
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
int cmsg_rc_send(void *domainId, void *vmsg) {
  
  cMsgMessage_t *msg = (cMsgMessage_t *) vmsg;
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  int len, lenSender, lenSubject, lenType, lenText, lenPayloadText, lenByteArray;
  int err=CMSG_OK, fd, highInt, lowInt, msgType, getResponse, outGoing[16];
  ssize_t sendLen;
  uint64_t llTime;
  struct timespec now;

  
  if (domain == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
      
  /* check args */
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  
  if ( (cMsgCheckString(msg->subject) != CMSG_OK ) ||
       (cMsgCheckString(msg->type)    != CMSG_OK )    ) {
    return(CMSG_BAD_ARGUMENT);
  }

  /* use UDP to send to rc Server */
  if (msg->udpSend) {
    return udpSend(domain, msg);
  }

  fd = domain->sendSocket;

  if (msg->text == NULL) {
    lenText = 0;
  }
  else {
    lenText = strlen(msg->text);
  }

  /* RC domain keeps no history */
  
  /* length of "payloadText" string */
  if (msg->payloadText == NULL) {
    lenPayloadText = 0;
  }
  else {
    lenPayloadText = strlen(msg->payloadText);
  }

  cMsgGetGetResponse(vmsg, &getResponse);
  msgType = CMSG_SUBSCRIBE_RESPONSE;
  if (getResponse) {
/*printf("Sending a GET response with senderToken = %d\n",msg->senderToken);*/
      msgType = CMSG_GET_RESPONSE;
  }

  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(msgType);
  /* reserved for future use */
  outGoing[2] = htonl(CMSG_VERSION_MAJOR);
  /* user int */
  outGoing[3] = htonl(msg->userInt);
  /* bit info */
  outGoing[4] = htonl(msg->info);
  /* senderToken */
  outGoing[5] = htonl(msg->senderToken);

  /* time message sent (right now) */
  clock_gettime(CLOCK_REALTIME, &now);
  /* convert to milliseconds */
  llTime  = ((uint64_t)now.tv_sec * 1000) +
            ((uint64_t)now.tv_nsec/1000000);
  highInt = (int) ((llTime >> 32) & 0x00000000FFFFFFFF);
  lowInt  = (int) (llTime & 0x00000000FFFFFFFF);
  outGoing[6] = htonl(highInt);
  outGoing[7] = htonl(lowInt);

  /* user time */
  llTime  = ((uint64_t)msg->userTime.tv_sec * 1000) +
            ((uint64_t)msg->userTime.tv_nsec/1000000);
  highInt = (int) ((llTime >> 32) & 0x00000000FFFFFFFF);
  lowInt  = (int) (llTime & 0x00000000FFFFFFFF);
  outGoing[8] = htonl(highInt);
  outGoing[9] = htonl(lowInt);

  /* length of "sender" string */
  lenSender    = strlen(domain->name);
  outGoing[10] = htonl(lenSender);

  /* length of "subject" string */
  lenSubject   = strlen(msg->subject);
  outGoing[11] = htonl(lenSubject);

  /* length of "type" string */
  lenType      = strlen(msg->type);
  outGoing[12] = htonl(lenType);

  /* length of "payloadText" string */
  outGoing[13] = htonl(lenPayloadText);

  /* length of "text" string */
  outGoing[14] = htonl(lenText);

  /* length of byte array */
  lenByteArray = msg->byteArrayLength;
  outGoing[15] = htonl(lenByteArray);

  /* total length of message (minus first int) is first item sent */
  len = sizeof(outGoing) + lenSubject + lenType +
        lenSender + lenPayloadText + lenText + lenByteArray;
  outGoing[0] = htonl((int) (len - sizeof(int)));

  if (len > BIGGEST_UDP_PACKET_SIZE) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_rc_send: packet size too big\n");
    }
    return(CMSG_LIMIT_EXCEEDED);
  }

  /* Cannot run this while connecting/disconnecting */
  cMsgConnectReadLock(domain);

  if (domain->gotConnection != 1) {
    cMsgConnectReadUnlock(domain);
    return(CMSG_LOST_CONNECTION);
  }

  /* Make send socket communications thread-safe. That
   * includes protecting the one buffer being used.
   */
  cMsgSocketMutexLock(domain);

  /* allocate more memory for message-sending buffer if necessary */
  if (domain->msgBufferSize < len) {
    free(domain->msgBuffer);
    domain->msgBufferSize = len + 1024; /* give us 1kB extra */
    domain->msgBuffer = (char *) malloc(domain->msgBufferSize);
    if (domain->msgBuffer == NULL) {
      cMsgSocketMutexUnlock(domain);
      cMsgConnectReadUnlock(domain);
      return(CMSG_OUT_OF_MEMORY);
    }
  }

  /* copy data into a single static buffer */
  memcpy(domain->msgBuffer, (void *)outGoing, sizeof(outGoing));
  len = sizeof(outGoing);
  memcpy(domain->msgBuffer+len, (void *)domain->name, lenSender);
  len += lenSender;
  memcpy(domain->msgBuffer+len, (void *)msg->subject, lenSubject);
  len += lenSubject;
  memcpy(domain->msgBuffer+len, (void *)msg->type, lenType);
  len += lenType;
  memcpy(domain->msgBuffer+len, (void *)msg->payloadText, lenPayloadText);
  len += lenPayloadText;
  memcpy(domain->msgBuffer+len, (void *)msg->text, lenText);
  len += lenText;
  memcpy(domain->msgBuffer+len, (void *)&((msg->byteArray)[msg->byteArrayOffset]), lenByteArray);
  len += lenByteArray;   

/*printf("cmsg_rc_send: TCP, fd = %d\n", fd);*/
  /* send data over TCP socket */
  sendLen = cMsgNetTcpWrite(fd, (void *) domain->msgBuffer, len);
  if (sendLen != len) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_rc_send: write failure\n");
    }
    err = CMSG_NETWORK_ERROR;
  }

  /* done protecting communications */
  cMsgSocketMutexUnlock(domain);
  cMsgConnectReadUnlock(domain);
  
  return(err);
}

/*-------------------------------------------------------------------*/


/** This routine sends a msg to the specified rc server using UDP. */
static int udpSend(cMsgDomainInfo *domain, cMsgMessage_t *msg) {

  int len, lenSender, lenSubject, lenType, lenText, lenPayloadText, lenByteArray;
  int err=CMSG_OK, fd, highInt, lowInt, msgType, getResponse, outGoing[19];
  ssize_t sendLen;
  uint64_t llTime;
  struct timespec now;
        
  
  fd = domain->sendUdpSocket;
  
  if (msg->text == NULL) {
    lenText = 0;
  }
  else {
    lenText = strlen(msg->text);
  }

  /* RC domain keeps no history */

  /* length of "payloadText" string */
  if (msg->payloadText == NULL) {
    lenPayloadText = 0;
  }
  else {
    lenPayloadText = strlen(msg->payloadText);
  }

  cMsgGetGetResponse((void *)msg, &getResponse);
  msgType = CMSG_SUBSCRIBE_RESPONSE;
  if (getResponse) {
    /*printf("Sending a GET response with senderToken = %d\n",msg->senderToken);*/
    msgType = CMSG_GET_RESPONSE;
  }

  /* send magic numbers to filter out garbage on receiving end */
  outGoing[0] = htonl(CMSG_MAGIC_INT1);
  outGoing[1] = htonl(CMSG_MAGIC_INT2);
  outGoing[2] = htonl(CMSG_MAGIC_INT3);
  
  /* message id (in network byte order) to domain server */
  outGoing[4] = htonl(msgType);
  /* reserved for future use */
  outGoing[5] = htonl(CMSG_VERSION_MAJOR);
  /* user int */
  outGoing[6] = htonl(msg->userInt);
  /* bit info */
  outGoing[7] = htonl(msg->info);
  /* senderToken */
  outGoing[8] = htonl(msg->senderToken);

  /* time message sent (right now) */
  clock_gettime(CLOCK_REALTIME, &now);
  /* convert to milliseconds */
  llTime  = ((uint64_t)now.tv_sec * 1000) +
      ((uint64_t)now.tv_nsec/1000000);
  highInt = (int) ((llTime >> 32) & 0x00000000FFFFFFFF);
  lowInt  = (int) (llTime & 0x00000000FFFFFFFF);
  outGoing[9]  = htonl(highInt);
  outGoing[10] = htonl(lowInt);

  /* user time */
  llTime  = ((uint64_t)msg->userTime.tv_sec * 1000) +
      ((uint64_t)msg->userTime.tv_nsec/1000000);
  highInt = (int) ((llTime >> 32) & 0x00000000FFFFFFFF);
  lowInt  = (int) (llTime & 0x00000000FFFFFFFF);
  outGoing[11] = htonl(highInt);
  outGoing[12] = htonl(lowInt);

  /* length of "sender" string */
  lenSender    = strlen(domain->name);
  outGoing[13] = htonl(lenSender);

  /* length of "subject" string */
  lenSubject   = strlen(msg->subject);
  outGoing[14] = htonl(lenSubject);

  /* length of "type" string */
  lenType      = strlen(msg->type);
  outGoing[15] = htonl(lenType);

  /* length of "payloadText" string */
  outGoing[16] = htonl(lenPayloadText);

  /* length of "text" string */
  outGoing[17] = htonl(lenText);

  /* length of byte array */
  lenByteArray = msg->byteArrayLength;
  outGoing[18] = htonl(lenByteArray);

  len = sizeof(outGoing) + lenSubject + lenType +
        lenSender + lenPayloadText + lenText + lenByteArray;
  /* total length of message (minus magic numbers and first int which is length)
   * is first item sent */
  outGoing[3] = htonl((int) (len - 4*sizeof(int)));

  if (len > BIGGEST_UDP_PACKET_SIZE) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_rc_send: packet size too big\n");
    }
    return(CMSG_LIMIT_EXCEEDED);
  }

  /* Cannot run this while connecting/disconnecting */
  cMsgConnectReadLock(domain);

  if (domain->gotConnection != 1) {
    cMsgConnectReadUnlock(domain);
    return(CMSG_LOST_CONNECTION);
  }

  /* Make send socket communications thread-safe. That
   * includes protecting the one buffer being used.
   */
  cMsgSocketMutexLock(domain);

  /* allocate more memory for message-sending buffer if necessary */
  if (domain->msgBufferSize < len) {
    free(domain->msgBuffer);
    domain->msgBufferSize = len + 1024; /* give us 1kB extra */
    domain->msgBuffer = (char *) malloc(domain->msgBufferSize);
    if (domain->msgBuffer == NULL) {
      cMsgSocketMutexUnlock(domain);
      cMsgConnectReadUnlock(domain);
      return(CMSG_OUT_OF_MEMORY);
    }
  }

  /* copy data into a single static buffer */
  memcpy(domain->msgBuffer, (void *)outGoing, sizeof(outGoing));
  len = sizeof(outGoing);
  memcpy(domain->msgBuffer+len, (void *)domain->name, lenSender);
  len += lenSender;
  memcpy(domain->msgBuffer+len, (void *)msg->subject, lenSubject);
  len += lenSubject;
  memcpy(domain->msgBuffer+len, (void *)msg->type, lenType);
  len += lenType;
  memcpy(domain->msgBuffer+len, (void *)msg->payloadText, lenPayloadText);
  len += lenPayloadText;
  memcpy(domain->msgBuffer+len, (void *)msg->text, lenText);
  len += lenText;
  memcpy(domain->msgBuffer+len, (void *)&((msg->byteArray)[msg->byteArrayOffset]), lenByteArray);
  len += lenByteArray;

/*printf("cmsg_rc_send: UDP, fd = %d\n", fd);*/
  /* send data over UDP socket */
  sendLen = send(fd, (void *) domain->msgBuffer, len, 0);
  if (sendLen != len) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_rc_send: write failure\n");
    }
    err = CMSG_NETWORK_ERROR;
  }

  /* done protecting communications */
  cMsgSocketMutexUnlock(domain);
  cMsgConnectReadUnlock(domain);

  return(err);
}


/*-------------------------------------------------------------------*/


/** syncSend is not implemented in the rc domain. */
int cmsg_rc_syncSend(void *domainId, void *vmsg, const struct timespec *timeout, int *response) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/** subscribeAndGet is not implemented in the rc domain. */
int cmsg_rc_subscribeAndGet(void *domainId, const char *subject, const char *type,
                                 const struct timespec *timeout, void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/** sendAndGet is not implemented in the rc domain. */
int cmsg_rc_sendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout,
                            void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/** flush does nothing in the rc domain. */
int cmsg_rc_flush(void *domainId, const struct timespec *timeout) {
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
 * @returns CMSG_BAD_ARGUMENT if the id, subject, type, or callback are null
 * @returns CMSG_OUT_OF_MEMORY if all available memory has been used
 * @returns CMSG_ALREADY_EXISTS if an identical subscription already exists
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
int cmsg_rc_subscribe(void *domainId, const char *subject, const char *type,
                      cMsgCallbackFunc *callback, void *userArg,
                      cMsgSubscribeConfig *config, void **handle) {

    int uniqueId, status, err=CMSG_OK, newSub=0;
    int startRetries = 0;
    cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
    subscribeConfig *sConfig = (subscribeConfig *) config;
    cbArg *cbarg;
    pthread_attr_t threadAttribute;
    char *subKey;
    subInfo *sub;
    subscribeCbInfo *cb, *cbItem;
    void *p;
    struct timespec wait = {0,10000000}; /* .01 sec */

    /* check args */  
    if (domain == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }

    if ( (cMsgCheckString(subject) != CMSG_OK ) ||
         (cMsgCheckString(type)    != CMSG_OK ) ||
         (callback == NULL)                    ) {
        return(CMSG_BAD_ARGUMENT);
    }

    /* Create a unique string which is subject"type since the quote char is not allowed
    * in either subject or type. This unique string will be the key in a hash table
    * with the value being a structure holding subscription info.
    */
    subKey = (char *) calloc(1, strlen(subject) + strlen(type) + 2);
    if (subKey == NULL) {
        return(CMSG_OUT_OF_MEMORY);
    }
    sprintf(subKey, "%s\"%s", subject, type);

    cMsgConnectReadLock(domain);

    if (domain->gotConnection != 1) {
        cMsgConnectReadUnlock(domain);
        return(CMSG_LOST_CONNECTION);
    }

    /* use default configuration if none given */
    if (config == NULL) {
        sConfig = (subscribeConfig *) cMsgSubscribeConfigCreate();
    }

    /* make sure subscribe and unsubscribe are not run at the same time */
    cMsgSubscribeWriteLock(domain);

    /* if this subscription does NOT exist (try finding in hash table), make one */
    if (hashLookup(&domain->subscribeTable, subKey, &p)) {
      sub = (subInfo *)p; /* avoid compiler warning */
    }
    else {
      sub = (subInfo *) calloc(1, sizeof(subInfo));
      if (sub == NULL) {
        cMsgSubscribeWriteUnlock(domain);
        cMsgConnectReadUnlock(domain);
        free(subKey);
        return(CMSG_OUT_OF_MEMORY);
      }
      cMsgSubscribeInfoInit(sub);
      sub->subject = (char *) strdup(subject);
      sub->type    = (char *) strdup(type);
      cMsgSubscriptionSetRegexpStuff(sub);
      /* put it in hash table */
      hashInsert(&domain->subscribeTable, subKey, sub, NULL);

      newSub = 1;
    }

    /* Now add a callback */
    cb = (subscribeCbInfo *) calloc(1, sizeof(subscribeCbInfo));
    if (cb == NULL) {
      cMsgSubscribeWriteUnlock(domain);
      cMsgConnectReadUnlock(domain);
      if (newSub) {
        cMsgSubscribeInfoFree(sub);
        free(sub);
      }
      free(subKey);
      return(CMSG_OUT_OF_MEMORY);
    }
    cMsgCallbackInfoInit(cb);
    cb->callback = callback;
    cb->userArg  = userArg;
    cb->config   = *sConfig;

    /* store callback in subscription's linked list */
    cbItem = sub->callbacks;
    if (cbItem == NULL) {
      sub->callbacks = cb;
    }
    else {
      while (cbItem != NULL) {
        if (cbItem->next == NULL) {
          break;
        }
        cbItem = cbItem->next;
      }
      cbItem->next = cb;
    }
            
    /* keep track of how many callbacks this subscription has */
    sub->numCallbacks++;
      
    /* give caller info so subscription can be unsubscribed later */
    cbarg = (cbArg *) malloc(sizeof(cbArg));
    if (cbarg == NULL) {
      cMsgSubscribeWriteUnlock(domain);
      cMsgConnectReadUnlock(domain);
      if (cbItem == NULL) {
        sub->callbacks = NULL;
      }
      else {
        cbItem->next = NULL;
      }
      if (newSub) {
        cMsgSubscribeInfoFree(sub);
        free(sub);
      }
      cMsgCallbackInfoFree(cb);
      free(cb);
      free(subKey);
      return(CMSG_OUT_OF_MEMORY);
    }
    
    cbarg->domainId = (uintptr_t) domainId;
    cbarg->sub    = sub;
    cbarg->cb     = cb;
    cbarg->key    = subKey;
    cbarg->domain = domain;
            
    if (handle != NULL) {
      *handle = (void *)cbarg;
    }
            
    /* init thread attributes */
    pthread_attr_init(&threadAttribute);
            
#ifdef VXWORKS
    /* Make 30k bytes the default stack size in vxworks (instead
     * of the normal 20k) since people have been running out of
     * stack memory.
    */
    if (cb->config.stackSize == 0) {
  pthread_attr_setstacksize(&threadAttribute, CMSG_VX_DEFAULT_STACK_SIZE);
    }
#endif
    /* if stack size of this thread is set, include in attribute */
    if (cb->config.stackSize > 0) {
      pthread_attr_setstacksize(&threadAttribute, cb->config.stackSize);
    }

    /* start callback thread now */
    status = pthread_create(&cb->thread,
                             &threadAttribute, cMsgCallbackThread, (void *)cbarg);
    if (status != 0) {
      cmsg_err_abort(status, "Creating callback thread");
    }

    /* help new thread start */
    sched_yield();

    /* release allocated memory */
    pthread_attr_destroy(&threadAttribute);
    if (config == NULL) {
      cMsgSubscribeConfigDestroy((cMsgSubscribeConfig *) sConfig);
    }

    /* wait up to 1 sec for callback thread to start, then just go on */
    while (!cb->started && startRetries < 100) {
/*printf("cmsg_rc_subscribe: wait another .01 sec for cb thread to start\n");*/
        nanosleep(&wait, NULL);
        startRetries++;
    }

    /*
     * Pick a unique identifier for the subject/type pair, and
     * send it to the domain server & remember it for future use
     * Mutex protect this operation as many cmsg_rc_connect calls may
     * operate in parallel on this static variable.
     */
    staticMutexLock();
    uniqueId = subjectTypeId++;
    staticMutexUnlock();
    sub->id = uniqueId;

    /* done protecting subscribe */
    cMsgSubscribeWriteUnlock(domain);
    cMsgConnectReadUnlock(domain);

    return(err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine unsubscribes to messages of the given handle (which
 * represents a given subject, type, callback, and user argument).
 * This routine is called by the user through
 * cMsgUnSubscribe() given the appropriate UDL. In this domain cMsgFlush()
 * does nothing and does not need to be called for cmsg_rc_unsubscribe to be
 * started immediately.
 *
 * @param domainId id of the domain connection
 * @param handle void pointer obtained from cmsg_rc_subscribe
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id, handle or its subject, type, or callback are null,
 *                            or the given subscription (thru handle) does not have
 *                            an active subscription or callbacks
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
int cmsg_rc_unsubscribe(void *domainId, void *handle) {

    int status, err=CMSG_OK;
    cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
    cbArg           *cbarg;
    subInfo         *sub;
    subscribeCbInfo *cb, *cbItem, *cbPrev;
    cMsgMessage_t *msg, *nextMsg;
    void *p;

    /* check args */
    if (domain == NULL || handle == NULL) {
      return(CMSG_BAD_ARGUMENT);  
    }

    cbarg = (cbArg *)handle;
    if (cbarg->domainId != (uintptr_t)domainId) {
      return(CMSG_BAD_ARGUMENT);
    }

    /* convenience variables */
    sub = cbarg->sub;
    cb  = cbarg->cb;

    /* if subscription has no active callbacks, or valid sub, valid type, or callback */
    if ( (sub->numCallbacks < 1)                     ||
         (cMsgCheckString(sub->subject) != CMSG_OK ) ||
         (cMsgCheckString(sub->type)    != CMSG_OK ) ||
         (cb->callback == NULL)                    )   {
      return(CMSG_BAD_ARGUMENT);
    }
    
    cMsgConnectReadLock(domain);

    if (domain->gotConnection != 1) {
      cMsgConnectReadUnlock(domain);
      return(CMSG_LOST_CONNECTION);
    }

    /* make sure subscribe and unsubscribe are not run at the same time */
    cMsgSubscribeWriteLock(domain);
        
    /* Delete entry if there was at least 1 callback
     * to begin with and now there are none for this subject/type.
     */
    if (sub->numCallbacks - 1 < 1) {
      /* remove subscription from the hash table */
      hashRemove(&domain->subscribeTable, cbarg->key, NULL);      
      /* set resources free */
      cMsgSubscribeInfoFree(sub);
      free(sub);
    }
    /* just remove callback from list */
    else {
      cbPrev = cbItem = sub->callbacks;
      while (cb != cbItem) {
        if (cbItem == NULL) break;
        cbPrev = cbItem;
        cbItem = cbItem->next;
      }
      if (cbItem != NULL) {
        /* if removing first item in list ... */
        if (cbItem == sub->callbacks) {
          sub->callbacks = cbItem->next;
        }
        else {
          cbPrev->next = cbItem->next;
        }
      }
      else {
/*printf("cmsg_rc_unsubscribe: no cbs in sub list (?!), cannot remove cb\n");*/
      }
      
      /* one less callback */
      sub->numCallbacks--;
    }
    
    /* ensure new value of cb->quit is picked up by callback thread */
    cMsgMutexLock(&cb->mutex);
    
    /* Release all messages held in callback thread's queue.
     * Do that now, so we don't have to deal with full Q's
     * causing all kinds of delays.*/
    msg = cb->head; /* get first message in linked list */
    while (msg != NULL) {
      nextMsg = msg->next;
      p = (void *)msg; /* get rid of compiler warnings */
      cMsgFreeMessage(&p);
      msg = nextMsg;
    }
    cb->messages = 0;
    cb->head = NULL;
    cb->tail = NULL;
    
    /* tell callback thread to end */
    cb->quit = 1;
    
    /* Signal to cMsgRunCallbacks in case the callback's Q is full and
    * the message-receiving thread is blocked trying to put another message in. */
    status = pthread_cond_signal(&cb->takeFromQ);
    if (status != 0) {
        cmsg_err_abort(status, "Failed callbacks condition signal");
    }
    
    /* Signal to callback thread to cancel all worker threads, then kill self. */
    status = pthread_cond_signal(&cb->checkQ);
    if (status != 0) {
        cmsg_err_abort(status, "Failed callbacks condition signal");
    }

    cMsgMutexUnlock(&cb->mutex);

    /* done protecting unsubscribe */
    cMsgSubscribeWriteUnlock(domain);
    cMsgConnectReadUnlock(domain);

    /* Free arg mem */
    free(cbarg->key);
    free(cbarg);

    return(err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine pauses the delivery of messages to the given subscription callback.
 *
 * @param domainId id of the domain connection
 * @param handle void pointer to subscription info obtained from {@link cmsg_cmsg_subscribe}
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id/handle is bad or handle is NULL
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement pause
 */
int cmsg_rc_subscriptionPause(void *domainId, void *handle) {

    cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
    cbArg           *cbarg;
    subscribeCbInfo *cb;
  
    /* check args */
    if (domain == NULL || handle == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }

    cbarg = (cbArg *)handle;
    if (cbarg->domainId != (uintptr_t)domainId) {
        return(CMSG_BAD_ARGUMENT);
    }
       
    /* convenience variables */
    cb = cbarg->cb;

    cMsgMutexLock(&cb->mutex);
    
    /* tell callback thread to pause */
    cb->pause = 1;

    cMsgMutexUnlock(&cb->mutex);
      
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine resumes the delivery of messages to the given subscription callback.
 *
 * @param domainId id of the domain connection
 * @param handle void pointer to subscription info obtained from {@link cmsg_cmsg_subscribe}
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id/handle is bad or handle is NULL
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement pause
 */
int cmsg_rc_subscriptionResume(void *domainId, void *handle) {

    cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
    cbArg           *cbarg;
    subscribeCbInfo *cb;
    struct timespec wait = {1,0};

    /* check args */
    if (domain == NULL || handle == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }

    cbarg = (cbArg *)handle;
    if (cbarg->domainId != (uintptr_t)domainId) {
        return(CMSG_BAD_ARGUMENT);
    }

    /* convenience variables */
    cb = cbarg->cb;

    cMsgMutexLock(&cb->mutex);
    
    /* tell callback thread to resume */
    cb->pause = 0;
    
    /* wakeup the paused callback */
    cMsgLatchCountDown(&cb->pauseLatch, &wait);

    cMsgMutexUnlock(&cb->mutex);
      
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine returns the number of messages currently in a subscription callback's queue.
 *
 * @param domainId id of the domain connection
 * @param handle void pointer to subscription info obtained from {@link cmsg_cmsg_subscribe}
 * @param count int pointer filled in with number of messages in subscription callback's queue
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id/handle is bad or handle is NULL
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement pause
 */
int cmsg_rc_subscriptionQueueCount(void *domainId, void *handle, int *count) {

    cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
    cbArg           *cbarg;
    subscribeCbInfo *cb;

    /* check args */
    if (domain == NULL || handle == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }

    cbarg = (cbArg *)handle;
    if (cbarg->domainId != (uintptr_t)domainId) {
        return(CMSG_BAD_ARGUMENT);
    }

    /* convenience variables */
    cb = cbarg->cb;

    cMsgMutexLock(&cb->mutex);
    
    /* tell callback thread to resume */
    if (count != NULL) *count = cb->messages;
    
    cMsgMutexUnlock(&cb->mutex);
    
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine returns true(1) if a subscription callback's queue is full, else false(0).
 *
 * @param domainId id of the domain connection
 * @param handle void pointer to subscription info obtained from {@link cmsg_cmsg_subscribe}
 * @param full int pointer filled in with 1 if subscription callback's queue full, else 0
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id/handle is bad or handle is NULL
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement pause
 */
int cmsg_rc_subscriptionQueueIsFull(void *domainId, void *handle, int *full) {

    cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
    cbArg           *cbarg;
    subscribeCbInfo *cb;

    /* check args */
    if (domain == NULL || handle == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }

    cbarg = (cbArg *)handle;
    if (cbarg->domainId != (uintptr_t)domainId) {
        return(CMSG_BAD_ARGUMENT);
    }


    /* convenience variables */
    cb = cbarg->cb;

    cMsgMutexLock(&cb->mutex);
    
    /* tell callback thread to resume */
    if (full != NULL) *full = cb->fullQ;
    
    cMsgMutexUnlock(&cb->mutex);
    
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine clears a subscription callback's queue of all messages.
 *
 * @param domainId id of the domain connection
 * @param handle void pointer to subscription info obtained from {@link cmsg_cmsg_subscribe}
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id/handle is bad or handle is NULL
 */
int cmsg_rc_subscriptionQueueClear(void *domainId, void *handle) {

    int i, status;
    cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
    cbArg           *cbarg;
    subscribeCbInfo *cb;
    cMsgMessage_t *head;
    void *p;
  
    /* check args */
    if (domain == NULL || handle == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }

    cbarg = (cbArg *)handle;
    if (cbarg->domainId != (uintptr_t)domainId) {
        return(CMSG_BAD_ARGUMENT);
    }

    /* convenience variables */
    cb = cbarg->cb;

    cMsgMutexLock(&cb->mutex);
    
    head = cb->head;
    while (head != NULL) {
        cb->head = cb->head->next;
        p = (void *)head; /* get rid of compiler warnings */
        cMsgFreeMessage(&p);
        cb->messages--;
        cb->fullQ = 0;
        head = cb->head;
    }
    cb->head = NULL;
    
    /* wakeup cMsgRunCallbacks thread if trying to add item to full cue */
    status = pthread_cond_signal(&cb->takeFromQ);
    if (status != 0) {
        cmsg_err_abort(status, "Failed callback condition signal in rc subQclear");
    }
  
    cMsgMutexUnlock(&cb->mutex);
    
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine returns the total number of messages sent to a subscription callback.
 *
 * @param domainId id of the domain connection
 * @param handle void pointer to subscription info obtained from {@link cmsg_cmsg_subscribe}
 * @param total int pointer filled in with total number of messages sent to a subscription callback
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id/handle is bad or handle is NULL
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement pause
 */
int cmsg_rc_subscriptionMessagesTotal(void *domainId, void *handle, int *total) {

    cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
    cbArg           *cbarg;
    subscribeCbInfo *cb;

    /* check args */
    if (domain == NULL || handle == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }

    cbarg = (cbArg *)handle;
    if (cbarg->domainId != (uintptr_t)domainId) {
        return(CMSG_BAD_ARGUMENT);
    }

    /* convenience variables */
    cb = cbarg->cb;

    cMsgMutexLock(&cb->mutex);
    
    /* tell callback thread to resume */
    if (total != NULL) *total = cb->msgCount;
    
    cMsgMutexUnlock(&cb->mutex);
    
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * The monitor function is not implemented in the rc domain.
 */   
int cmsg_rc_monitor(void *domainId, const char *command, void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
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
 * @returns CMSG_BAD_ARGUMENT if domainId is null
 */   
int cmsg_rc_start(void *domainId) {

  if (domainId == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  ((cMsgDomainInfo *) domainId)->receiveState = 1;
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
 * @returns CMSG_BAD_ARGUMENT if domainId is null
 */   
int cmsg_rc_stop(void *domainId) {

  if (domainId == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  ((cMsgDomainInfo *) domainId)->receiveState = 0;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine disconnects the client from the RC server.
 *
 * @param domainId pointer to id of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if domainId or the pointer it points to is NULL
 */   
int cmsg_rc_disconnect(void **domainId) {

    cMsgDomainInfo *domain;
    int i, tblSize, status, loops=0, domainUsed=1;
    subscribeCbInfo *cb, *cbNext;
    subInfo *sub;
    struct timespec wait = {0, 10000000}; /* 0.01 sec */
    hashNode *entries = NULL;
    cMsgMessage_t *msg, *nextMsg;
    void *p;

    if (domainId == NULL) return(CMSG_BAD_ARGUMENT);
    domain = (cMsgDomainInfo *) (*domainId);
    if (domain == NULL) return(CMSG_BAD_ARGUMENT);
    
    /* When changing initComplete / connection status, protect it */
    cMsgConnectWriteLock(domain);

    domain->gotConnection = 0;

    /* close TCP sending socket */
    close(domain->sendSocket);

    /* close UDP sending socket */
    close(domain->sendUdpSocket);

    /* close listening socket */
    close(domain->listenSocket);

    /* Don't want callbacks' queues to be full so remove all messages.
     * Don't quit callback threads yet since that frees callback memory
     * and threads may still be iterating through callbacks.
     * Don't remove subscriptions now since a callback queue may be full
     * which means the pend thread is stuck and has the subscribe read mutex.
     * To remove subscriptions we need to grab the subscribe
     * write mutex which would not be possible in that case.
     * So free up pend thread in case it's stuck and remove
      * callback threads and subscriptions later. */
    cMsgSubscribeReadLock(domain);

    /* get client subscriptions */
    hashGetAll(&domain->subscribeTable, &entries, &tblSize);

    /* if there are subscriptions ... */
    if (entries != NULL) {
        /* for each client subscription ... */
        for (i=0; i<tblSize; i++) {
            sub = (subInfo *)entries[i].data;
            cb = sub->callbacks;

            /* for each callback ... */
            while (cb != NULL) {
                /* ensure new value of cb->quit is picked up by callback thread */
                cMsgMutexLock(&cb->mutex);

                /* Release all messages held in callback thread's queue.
                * Do that now, so we don't have to deal with full Q's
                * causing all kinds of delays.*/
                msg = cb->head; /* get first message in linked list */
                while (msg != NULL) {
                    nextMsg = msg->next;
                    p = (void *)msg;
                    cMsgFreeMessage(&p);
                    msg = nextMsg;
                }
                cb->messages = 0;
                cb->head = NULL;
                cb->tail = NULL;
    
                /* Signal to cMsgRunCallbacks in case the callback's Q is full and the
                * message-receiving thread is blocked trying to put another message in. */
                status = pthread_cond_signal(&cb->takeFromQ);
                if (status != 0) {
                    cmsg_err_abort(status, "Failed callbacks condition signal");
                }

                cMsgMutexUnlock(&cb->mutex);

                cb = cb->next;
            } /* next callback */
        } /* next subscription */
        free(entries);
    } /* if there are subscriptions */

    cMsgSubscribeReadUnlock(domain);
    sched_yield();

    /* stop listening and client communication threads */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cmsg_rc_disconnect: cancel listening & client threads\n");
    }

    /* cancel msg receiving thread */
    pthread_cancel(domain->pendThread);

    /* give pend thread chance to return */
    sched_yield();
    
    /* Make sure this thread is really dead.
     * This thread only returns after thread
     * it spawned returns. */
    pthread_join(domain->pendThread, &p);

    /* terminate all callback threads */

    /* Don't want incoming msgs to be delivered to callbacks, remove them. */
    cMsgSubscribeWriteLock(domain);

    /* get client subscriptions */
    hashClear(&domain->subscribeTable, &entries, &tblSize);

    /* if there are subscriptions ... */
    if (entries != NULL) {
        /* for each client subscription ... */
        for (i=0; i<tblSize; i++) {
            sub = (subInfo *)entries[i].data;
            cb = sub->callbacks;

            /* for each callback ... */
            while (cb != NULL) {
                /* ensure new value of cb->quit is picked up by callback thread */
                cMsgMutexLock(&cb->mutex);

                /* once the callback thread is woken up, it will free cb memory,
                 * so store anything from that struct locally, NOW. */
                cbNext = cb->next;
    
                /* tell callback thread to end gracefully */
                cb->quit = 1;
    
                /* Signal to cb thread to quit (which then cancels cb worker threads). */
                status = pthread_cond_signal(&cb->checkQ);
                if (status != 0) {
                    cmsg_err_abort(status, "Failed callbacks condition signal");
                }

                /* free mem that normally is freed in unsubscribe */
                free(((cbArg *)(cb->cbarg))->key);
                free(cb->cbarg);
                
                cMsgMutexUnlock(&cb->mutex);

                cb = cbNext;
            } /* next callback */

            free(entries[i].key);
            cMsgSubscribeInfoFree(sub);
            free(sub);
        } /* next subscription */
        free(entries);
    } /* if there are subscriptions */

    cMsgSubscribeWriteUnlock(domain);
    sched_yield();
    
    /* Unblock SIGPIPE */
    cMsgRestoreSignals(domain);
    
    cMsgConnectWriteUnlock(domain);

    /* We've keep track of wether our internally spawned threads are still
     * using the domain struct/memory or not. Wait till it's no longer used
     * (or 1 second has elapsed) then free it. */
    cMsgMutexLock(&domain->syncSendMutex); /* use mutex unused elsewhere in rc domain */
    domainUsed = domain->functionsRunning;
    cMsgMutexUnlock(&domain->syncSendMutex);
/*printf("disconnect: domainUsed = %d\n", domainUsed);*/
    while (domainUsed > 0 && loops < 100) {
        nanosleep(&wait, NULL);
        loops++;
        cMsgMutexLock(&domain->syncSendMutex);
        domainUsed = domain->functionsRunning;
        cMsgMutexUnlock(&domain->syncSendMutex);
/*printf("disconnect: domainUsed = %d\n", domainUsed);*/
    }

    /* Clean up memory */
    cMsgDomainFree(domain);
    free(domain);
    *domainId = NULL;

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
 * @returns CMSG_BAD_ARGUMENT if the id is null
 */   
int cmsg_rc_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler,
                               void *userArg) {
  
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  
  if (domain == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
    
  domain->shutdownHandler = handler;
  domain->shutdownUserArg = userArg;
      
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/


/** shutdownClients is not implemented in the rc domain. */
int cmsg_rc_shutdownClients(void *domainId, const char *client, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/** shutdownServers is not implemented in the rc domain. */
int cmsg_rc_shutdownServers(void *domainId, const char *server, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * This routine parses, using regular expressions, the RC domain
 * portion of the UDL sent from the next level up" in the API.
 *
 * Runcontrol domain UDL is of the form:<p>
 *   <b>cMsg:rc://&lt;host&gt;:&lt;port&gt;/&lt;expid&gt;?multicastTO=&lt;timeout&gt;&connectTO=&lt;timeout&gt;</b><p>
 *
 * For the cMsg domain the UDL has the more specific form:<p>
 *   <b>cMsg:cMsg://&lt;host&gt;:&lt;port&gt;/&lt;subdomainType&gt;/&lt;subdomain remainder&gt;?tag=value&tag2=value2 ...</b><p>
 *
 * Remember that for this domain:
 *<ul>
 *<li>1) host is required and may also be "multicast", "localhost", or in dotted decimal form<p>
 *<li>2) port is optional with a default of {@link RC_MULTICAST_PORT}<p>
 *<li>3) the experiment id or expid is required, it is NOT taken from the environmental variable EXPID<p>
 *<li>4) multicastTO is the time to wait in seconds before connect returns a
 *       timeout when a rc multicast server does not answer<p>
 *<li>5) connectTO is the time to wait in seconds before connect returns a
 *       timeout while waiting for the rc server to send a special (tcp)
 *       concluding connect message<p>
 *</ul><p>
 *
 * 
 * @param UDLR  udl to be parsed
 * @param host  pointer filled in with host
 * @param port  pointer filled in with port
 * @param expid pointer filled in with expid tag's value if it exists
 * @param multicastTO pointer filled in with multicast timeout tag's value if it exists
 * @param connectTO   pointer filled in with connect timeout tag's value if it exists
 * @param remainder   pointer filled in with the part of the udl after host:port/ if it exists,
 *                    else it is set to NULL
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_FORMAT if UDLR arg is not in the proper format
 * @returns CMSG_BAD_ARGUMENT if UDLR arg is NULL
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 * @returns CMSG_OUT_OF_RANGE if port is an improper value
 */
static int parseUDL(const char *UDLR,
                    char **host,
                    unsigned short *port,
                    char **expid,
                    int  *multicastTO,
                    int  *connectTO,
                    char **remainder) {

    int        err, Port, index;
    size_t     len, bufLength;
    char       *udlRemainder, *val;
    char       *buffer;
    const char *pattern = "([^:/?]+):?([0-9]+)?/([^?&]+)(.*)";
    regmatch_t matches[5]; /* we have 5 potential matches: 1 whole, 4 sub */
    regex_t    compiled;
    
    if (UDLR == NULL) {
      return (CMSG_BAD_ARGUMENT);
    }
    
    /* make a copy */        
    udlRemainder = (char *) strdup(UDLR);
    if (udlRemainder == NULL) {
      return(CMSG_OUT_OF_MEMORY);
    }
/*printf("parseUDL: udl remainder = %s\n", udlRemainder);*/
 
    /* make a big enough buffer to construct various strings, 256 chars minimum */
    len       = strlen(udlRemainder) + 1;
    bufLength = len < 256 ? 256 : len;    
    buffer    = (char *) malloc(bufLength);
    if (buffer == NULL) {
      free(udlRemainder);
      return(CMSG_OUT_OF_MEMORY);
    }
    
    /* compile regular expression */
    err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED);
    /* this error will never happen since it's already been determined to work */
    if (err != 0) {
        free(udlRemainder);
        free(buffer);
        return (CMSG_ERROR);
    }
    
    /* find matches */
    err = cMsgRegexec(&compiled, udlRemainder, 5, matches, 0);
    if (err != 0) {
        /* no match */
        free(udlRemainder);
        free(buffer);
        return (CMSG_BAD_FORMAT);
    }
    
    /* free up memory */
    cMsgRegfree(&compiled);
            
    /* find host name, default => multicast */
    index = 1;
    if (matches[index].rm_so > -1) {
       buffer[0] = 0;
       len = matches[index].rm_eo - matches[index].rm_so;
       strncat(buffer, udlRemainder+matches[index].rm_so, len);
                
        /* if the host is "localhost", find the actual host name */
        if (strcasecmp(buffer, "localhost") == 0) {
            /* get canonical local host name */
            if (cMsgNetLocalHost(buffer, bufLength) != CMSG_OK) {
                /* error finding local host so just multicast */
                buffer[0] = 0;
                strcat(buffer, RC_MULTICAST_ADDR);
            }
        }
        else if (strcasecmp(buffer, "multicast") == 0) {
          buffer[0] = 0;
          strcat(buffer, RC_MULTICAST_ADDR);
        }
    }
    else {
        free(udlRemainder);
        free(buffer);
/*printf("parseUDL: host required in UDL\n");*/
        return (CMSG_BAD_FORMAT);
    }
    
    if (host != NULL) {
        *host = (char *)strdup(buffer);
    }    
/*printf("parseUDL: host = %s\n", buffer);*/


    /* find port */
    index = 2;
    if (matches[index].rm_so < 0) {
        /* no match for port so use default */
        Port = RC_MULTICAST_PORT;
        if (cMsgDebug >= CMSG_DEBUG_WARN) {
            fprintf(stderr, "parseUDLregex: guessing that the name server port is %d\n",
                   Port);
        }
    }
    else {
        buffer[0] = 0;
        len = matches[index].rm_eo - matches[index].rm_so;
        strncat(buffer, udlRemainder+matches[index].rm_so, len);        
        Port = atoi(buffer);        
    }

    if (Port < 1024 || Port > 65535) {
      if (host != NULL) free((void *) *host);
      free(udlRemainder);
      free(buffer);
      return (CMSG_OUT_OF_RANGE);
    }
               
    if (port != NULL) {
      *port = Port;
    }
/*printf("parseUDL: port = %hu\n", Port);*/

    /* find expid */
    index++;
    buffer[0] = 0;
    if (matches[index].rm_so < 0) {
        free(udlRemainder);
        free(buffer);
/*printf("parseUDL: expid required in UDL\n");*/
        return (CMSG_BAD_FORMAT);
    }
    else {
        len = matches[index].rm_eo - matches[index].rm_so;
        strncat(buffer, udlRemainder+matches[index].rm_so, len);
                
        if (expid != NULL) {
            *expid = (char *) strdup(buffer);
        }
    }
/*printf("parseUDL: expid = %s\n", buffer);*/

    /* find remainder */
    index++;
    buffer[0] = 0;
    if (matches[index].rm_so < 0) {
        /* no match */
        len = 0;
        if (remainder != NULL) {
            *remainder = NULL;
        }
    }
    else {
        len = matches[index].rm_eo - matches[index].rm_so;
        strncat(buffer, udlRemainder+matches[index].rm_so, len);
                
        if (remainder != NULL) {
            *remainder = (char *) strdup(buffer);
        }
/*printf("parseUDL: remainder = %s, len = %d\n", buffer, len);*/
    }

    len = strlen(buffer);
    while (len > 0) {
        val = strdup(buffer);

        /* look for ?multicastTO=value& or &multicastTO=value& */
        pattern = "multicastTO=([0-9]+)&?";

        /* compile regular expression */
        cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);

        /* find matches */
        err = cMsgRegexec(&compiled, val, 2, matches, 0);
        
        /* if there's a match ... */
        if (err == 0) {
            /* find timeout (in milliseconds) */
            if (matches[1].rm_so >= 0) {
               int t;
               buffer[0] = 0;
               len = matches[1].rm_eo - matches[1].rm_so;
               strncat(buffer, val+matches[1].rm_so, len);
               t = atoi(buffer);
               if (t < 1) t=0;
               if (multicastTO != NULL) {
                 *multicastTO = t;
               }        
/*printf("parseUDL: multicast timeout = %d\n", t);*/
            }
        }
        
        
        /* free up memory */
        cMsgRegfree(&compiled);
       
        /* now look for ?connectTO=value& or &connectTO=value& */
        pattern = "connectTO=([0-9]+)&?";

        /* compile regular expression */
        cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);

        /* find matches */
        err = cMsgRegexec(&compiled, val, 2, matches, 0);
        
        /* if there's a match ... */
        if (err == 0) {
            /* find timeout (in milliseconds) */
            if (matches[1].rm_so >= 0) {
               int t;
               buffer[0] = 0;
               len = matches[1].rm_eo - matches[1].rm_so;
               strncat(buffer, val+matches[1].rm_so, len);
               t = atoi(buffer);
               if (t < 1) t=0;
               if (connectTO != NULL) {
                 *connectTO = t;
               }        
/*printf("parseUDL: connection timeout = %d\n", t);*/
            }
        }
        
        /* free up memory */
        cMsgRegfree(&compiled);
        free(val);
        
        break;
    }

    /* UDL parsed ok */
/*printf("DONE PARSING UDL\n");*/
    free(udlRemainder);
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
    cmsg_err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the pthread mutex used when creating unique id numbers
 * and doing the one-time intialization. */
static void staticMutexUnlock(void) {

  int status = pthread_mutex_unlock(&generalMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed mutex unlock");
  }
}

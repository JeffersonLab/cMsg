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
 

#include <strings.h>
#include <sys/time.h>    /* struct timeval */
#ifdef linux
  #include <sys/types.h>   /* getpid() */
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
#include "cMsgRegex.h"
#include "cMsgDomain.h"



/**
 * Structure for arg to be passed to multicast thread.
 * Allows data to flow back and forth with this thread.
 */
typedef struct thdArg_t {
    int count;
    int sockfd;
    socklen_t len;
    int port;
    struct sockaddr_in addr;
    struct sockaddr_in *paddr;
    int   bufferLen;
    char *buffer;
    char *senderHost;
    codaIpList *ipList;
} thdArg;


/* built-in limits */
/** Number of seconds to wait for rcClientListeningThread threads to start. */
#define WAIT_FOR_THREADS 10

/* global variables */
/** Function in rcDomainListenThread.c which implements the network listening thread of a client. */
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
static void *multicastThd(void *arg);
static int   udpSend(cMsgDomainInfo *domain, cMsgMessage_t *msg);
static void  defaultShutdownHandler(void *userArg);
static int   parseUDL(const char *UDLR, char **host,
                      unsigned short *port, char **expid,
                      int *connectTO, char **junk, char **ip);
                      
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
int   cmsg_rc_getServerHost     (void *domainId, const char **ipAddress);
int   cmsg_rc_getServerPort     (void *domainId, int *port);
int   cmsg_rc_getInfo           (void *domainId, const char *command, char **string);

/** List of the functions which implement the standard cMsg tasks in this domain. */
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
                                    cmsg_rc_setUDL, cmsg_rc_getCurrentUDL,
                                    cmsg_rc_getServerHost, cmsg_rc_getServerPort,
                                    cmsg_rc_getInfo};
                                    
/* rc domain type */
domainTypeInfo rcDomainTypeInfo = {
  "rc",
  &functions
};

/*-------------------------------------------------------------------*/


/**
* This routine does general I/O and returns a string for each string argument.
*
* @param domain id of the domain connection
* @param command command whose value determines what is returned in string arg
* @param string  pointer which gets filled in with a return string
*
* @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
*/
int cmsg_rc_getInfo(void *domainId, const char *command, char **string) {
    return(CMSG_NOT_IMPLEMENTED);
}


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
 * Do NOT write into or free the returned char pointer.
 *
 * @param domainId id of the domain connection
 * @param udl pointer filled in with current UDL (do not write to this
 *            pointer)
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
 * This routine gets the IP address (in dotted-decimal form) that the
 * client used to make the TCP socket connection to the rc server.
 * Do NOT write into or free the returned char pointer.
 *
 * @param domainId id of the domain connection
 * @param ipAddress pointer filled in with IP address of server
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if domainId arg is NULL
 */
int cmsg_rc_getServerHost(void *domainId, const char **ipAddress) {
    cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
    
    /* check args */
    if (domain == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }
    if (ipAddress != NULL) *ipAddress = domain->sendHost;
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
* This routine gets the port that the client used to make the
* TCP socket connection to the rc server.
*
* @param domainId id of the domain connection
* @param udl pointer filled in with cMsg server TCP port
*
* @returns CMSG_OK if successful
* @returns CMSG_BAD_ARGUMENT if domainId arg is NULL
*/
int cmsg_rc_getServerPort(void *domainId, int *port) {
    cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
    
    /* check args */
    if (domain == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }
    if (port != NULL) *port = domain->sendPort;
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
 *    <b>cMsg:rc://&lt;host&gt;:&lt;port&gt;/&lt;expid&gt;&connectTO=&lt;timeout&gt;</b><p>
 * where:
 *<ol>
 *<li>host is required and may also be "multicast", "localhost", or in dotted decimal form<p>
 *<li>port is optional with a default of {@link RC_MULTICAST_PORT}<p>
 *<li>the experiment id or expid is required, it is NOT taken from the environmental variable EXPID<p>
 *<li>connectTO is the time to wait in seconds before connect returns a
 *       timeout while waiting for the rc server to send a special (tcp)
 *       concluding connect message. Defaults to 5 seconds.<p>
 *</ol><p>
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
 * @returns CMSG_BAD_FORMAT if UDLremainder arg is not in the proper format (sg. expid not defined)
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
 *                             specified ip addr in UDL is not local, or
 *                             no connection to the rc server can be made, or
 *                             a communication error with server occurs.
 */   
int cmsg_rc_connect(const char *myUDL, const char *myName, const char *myDescription,
                    const char *UDLremainder, void **domainId) {
  
    unsigned short serverPort;
    char  *serverHost, *ipForRcServer=NULL, *expid=NULL, *buffer;
    int    err, status, len, localPort, i, connectTO=0, off=0;
    int32_t outGoing[9];
    size_t expidLen,  nameLen;
    char   temp[CMSG_MAXHOSTNAMELEN];
    char  *portEnvVariable=NULL;
    unsigned char ttl = 32;
    unsigned short startingPort;
    cMsgDomainInfo *domain;
    cMsgThreadInfo *threadArg;
    int    hz, num_try, try_max;
    struct timespec waitForThread;
    
    pthread_t bThread;
    thdArg    bArg;
    
    struct timespec wait;
    struct sockaddr_in *servaddr, addr, localaddr;
    const int size = CMSG_BIGSOCKBUFSIZE; /* bytes */
        
    /* for connecting to rc Server w/ TCP */
    struct timeval tv = {0, 300000}; /* 0.3 sec wait for rc Server to respond */


    /* create buffer */
    buffer = (char *) calloc(1, 1024);
    if (buffer == NULL) {
        return(CMSG_OUT_OF_MEMORY);
    }

    /* create network address structure */
    servaddr = (struct sockaddr_in *) calloc(1, sizeof(struct sockaddr_in));
    if (servaddr == NULL) {
        free(buffer);
        return(CMSG_OUT_OF_MEMORY);
    }

    /* parse the UDLRemainder to get the host, port, expid, connect timeout, and ip for rc server */
    err = parseUDL(UDLremainder, &serverHost, &serverPort,
                   &expid, &connectTO, NULL, &ipForRcServer);
    if (err != CMSG_OK) {
        free(buffer);
        free(servaddr);
        return(err);
    }

    /* allocate struct to hold connection info */
    domain = (cMsgDomainInfo *) calloc(1, sizeof(cMsgDomainInfo));
    if (domain == NULL) {
        free(serverHost);
        free(expid);
        free(buffer);
        free(servaddr);
        if (ipForRcServer != NULL) free(ipForRcServer);
        return(CMSG_OUT_OF_MEMORY);  
    }
    cMsgDomainInit(domain);  

    /* allocate memory for message-sending buffer */
    domain->msgBuffer     = (char *) malloc((size_t) initialMsgBufferSize);
    domain->msgBufferSize = initialMsgBufferSize;
    if (domain->msgBuffer == NULL) {
        cMsgDomainFree(domain);
        free(domain);
        free(serverHost);
        free(expid);
        free(buffer);
        free(servaddr);
        if (ipForRcServer != NULL) free(ipForRcServer);
        return(CMSG_OUT_OF_MEMORY);
    }

    /* store our host's name */
    gethostname(temp, CMSG_MAXHOSTNAMELEN);
    domain->myHost = strdup(temp);

    /* store names, can be changed until server connection established */
    domain->name        = strdup(myName);
    domain->udl         = strdup(myUDL);
    domain->description = strdup(myDescription);

    /* store items parsed from UDL for use in monitor() */
    domain->expid       = expid;
    domain->serverHost  = serverHost;
    domain->localPort   = serverPort;

    /*--------------------------------------------------------------------------
     * First find a port on which to receive incoming messages.
     * Do this by trying to open a listening socket at a given
     * port number. If that doesn't work add one to port number
     * and try again.
     * 
     * But before that, define a port number from which to start looking.
     * If CMSG_PORT is defined, it's the starting port number.
     * If CMSG_PORT is NOT defind, start at RC_TCP_CLIENT_LISTENING_PORT (45800).
     *-------------------------------------------------------------------------*/
    
    /* pick starting port number */
    if ( (portEnvVariable = getenv("CMSG_RC_CLIENT_PORT")) == NULL ) {
        startingPort = RC_TCP_CLIENT_LISTENING_PORT;
        if (cMsgDebug >= CMSG_DEBUG_WARN) {
            fprintf(stderr, "cmsg_rc_connectImpl: cannot find CMSG_PORT env variable, first try port %hu\n", startingPort);
        }
    }
    else {
        i = atoi(portEnvVariable);
        if (i < 1025 || i > 65535) {
            startingPort = RC_TCP_CLIENT_LISTENING_PORT;
            if (cMsgDebug >= CMSG_DEBUG_WARN) {
                fprintf(stderr, "cmsg_rc_connect: CMSG_PORT contains a bad port #, first try port %hu\n", startingPort);
            }
        }
        else {
            startingPort = (unsigned short)i;
        }
    }
     
    /* get listening port and socket for this application */
    if ( (err = cMsgNetGetListeningSocket(0, startingPort, 0, 0, 1,
                                          &domain->listenPort,
                                          &domain->listenSocket)) != CMSG_OK) {
        cMsgDomainFree(domain);
        free(domain);
        free(buffer);
        free(servaddr);
        if (ipForRcServer != NULL) free(ipForRcServer);
        return(err); /* CMSG_SOCKET_ERROR if cannot find available port */
    }

    /* launch pend thread and start listening on receive socket */
    threadArg = (cMsgThreadInfo *) malloc(sizeof(cMsgThreadInfo));
    if (threadArg == NULL) {
        cMsgDomainFree(domain);
        free(domain);
        free(buffer);
        free(servaddr);
        if (ipForRcServer != NULL) free(ipForRcServer);
        return(CMSG_OUT_OF_MEMORY);
    }
    threadArg->isRunning   = 0;
    threadArg->thdstarted  = 0;
    threadArg->listenFd    = domain->listenSocket;
    threadArg->blocking    = CMSG_NONBLOCKING;
    threadArg->domain      = domain;
    
    /* Block SIGPIPE for this and all spawned threads. */
    cMsgBlockSignals(domain);

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

    /* get system clock rate - probably 100 Hz */
    /*hz = 100;*/
    hz = (int) sysconf(_SC_CLK_TCK);
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

    /* Mem allocated for the argument passed to listening thread is now freed
     * in the pthread cancellation cleanup handler in rcDomainListenThread.c */

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "cmsg_rc_connect: created listening thread\n");
    }

    /*-------------------------------------------------------
     * Talk to runcontrol multicast server
     *-------------------------------------------------------*/
    
    /* create UDP socket */
    domain->sendSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (domain->sendSocket < 0) {
        cMsgRestoreSignals(domain);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        free(buffer);
        free(servaddr);
        if (ipForRcServer != NULL) free(ipForRcServer);
        return(CMSG_SOCKET_ERROR);
    }

    /* Give each local socket a unique port on a single host.
     * SO_REUSEPORT not defined in Redhat 5. I think off is default anyway. */
#ifdef SO_REUSEPORT
    setsockopt(domain->sendSocket, SOL_SOCKET, SO_REUSEPORT, (void *)(&off), sizeof(int));
#endif

    /* Bind local end of socket to this port */
    memset((void *)&localaddr, 0, sizeof(localaddr));
    localaddr.sin_family = AF_INET;
    localaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    /* Pick local port for socket to avoid being assigned a port
       to which cMsgServerFinder is multicasting. */
    for (localPort = UDP_CLIENT_LISTENING_PORT; localPort < 65535; localPort++) {
        localaddr.sin_port = htons((uint16_t)localPort);
        if (bind(domain->sendSocket, (struct sockaddr *)&localaddr, sizeof(localaddr)) == 0) {
fprintf(stderr, "\ncmsg_rc_connect: bound to local port %d\n\n", localPort);
            break;
        }
    }
    /* If  bind always failed, then ephemeral port will be used. */

    /* Send to this address */
    servaddr->sin_family = AF_INET;
    servaddr->sin_port   = htons(serverPort);

    /* Set TTL to 32 so it will make it through routers. */
    err = setsockopt(domain->sendSocket, IPPROTO_IP, IP_MULTICAST_TTL, (void *) &ttl, sizeof(ttl));
    if (err < 0) {
        close(domain->sendSocket);
        cMsgRestoreSignals(domain);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        free(buffer);
        free(servaddr);
        if (ipForRcServer != NULL) free(ipForRcServer);
        return(CMSG_SOCKET_ERROR);
    }

    if ( (err = cMsgNetStringToNumericIPaddr(serverHost, servaddr)) != CMSG_OK ) {
        close(domain->sendSocket);
        cMsgRestoreSignals(domain);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        free(buffer);
        free(servaddr);
        if (ipForRcServer != NULL) free(ipForRcServer);
        /* only possible errors are:
            CMSG_NETWORK_ERROR if the numeric address could not be obtained
            CMSG_OUT_OF_MEMORY if out of memory
         */
        return(err);
    }
    
    /*
     * We send some items explicitly:
     *   1) 3 magic integers,
     *   2) cMsg version,
     *   3) Type of multicast (rc or cMsg domain),
     *   4) TCP listening port of this client,
     *   5) unique sender id integer (current time in millisec)
     *   6) name of this client, &
     *   7) EXPID (experiment id string)
     *   8) list of IP and broadcast addresses
     * The UDP port we're sending from is already part of the UDP packet
     */

/*printf("rc connect: sending info (listening tcp port = %d, expid = %s) to server on port = %hu on host %s\n",
        ((int) domain->listenPort), expid, serverPort, serverHost);*/
    
    nameLen  = strlen(myName);
    expidLen = strlen(expid);
    
    /* magic #s */
    outGoing[0] = htonl(CMSG_MAGIC_INT1);
    outGoing[1] = htonl(CMSG_MAGIC_INT2);
    outGoing[2] = htonl(CMSG_MAGIC_INT3);
    /* cMsg version */
    outGoing[3] = htonl(CMSG_VERSION_MAJOR);
    /* type of multicast */
    outGoing[4] = htonl(RC_DOMAIN_MULTICAST);
    /* tcp port */
    outGoing[5] = htonl((uint32_t) domain->listenPort);
    
    /* use current time as unique indentifier */
#ifdef Darwin
    {
        struct timeval now;
        int64_t millisec;
        gettimeofday(&now, NULL);
        millisec =  now.tv_sec * 1000 + now.tv_usec / 1000;
        outGoing[6] = htonl((int32_t) millisec);
    }
#else
    {
        struct timespec now;
        int64_t millisec;
        clock_gettime(CLOCK_REALTIME, &now);
        millisec = (int64_t) (now.tv_sec * 1000 + now.tv_nsec / 1000000);
        outGoing[6] = htonl((uint32_t) millisec);
    }
#endif

    /* length of "myName" string */
    outGoing[7] = htonl((uint32_t)nameLen);
    /* length of "expid" string */
    outGoing[8] = htonl((uint32_t)expidLen);

    /* copy data into a single buffer */
    memcpy(buffer, (void *)outGoing, sizeof(outGoing));
    len = sizeof(outGoing);
    memcpy(buffer+len, (const void *)myName, nameLen);
    len += nameLen;
    memcpy(buffer+len, (const void *)expid, expidLen);
    len += expidLen;

    /* send our list of presentation (dotted-decimal) IP addrs */
    {
        void *pAddrCount;
        codaIpAddr *ipAddrs;
        uint32_t  strLen, netOrderInt, addrCount=0, netOrderCounter;
        
        err = codanetGetNetworkInfo(&ipAddrs, NULL);

        /* If error getting address data, send nothing */
        if (err != CMSG_OK) {
printf("rc connect: error, no local network info available\n");
            close(domain->sendSocket);
            cMsgRestoreSignals(domain);
            pthread_cancel(domain->pendThread);
            cMsgDomainFree(domain);
            free(domain);
            free(buffer);
            free(servaddr);
            if (ipForRcServer != NULL) free(ipForRcServer);
            return(CMSG_NETWORK_ERROR);
        }

        /* keep pointer to place to write # of IP addresses */
        pAddrCount = (void *) (buffer + len);
        len += sizeof(int32_t);

        /*
         * If the user has already specified, in the UDL, which ip address for the
         * rc server to use to connect back to this client, then scan the network
         * info to check and see if it's a legitimate local address. If it is,
         * then pick out its corresponding broadcast address as well. If it isn't,
         * error.
         */
        if (ipForRcServer != NULL) {
            int foundLocalAddressMatch = 0;

            while (ipAddrs != NULL) {
                /* If user-supplied IP is a local address, we're good.
                 * Use it as the only address to send. */
                if (strcmp((const char *) ipAddrs->addr, ipForRcServer) == 0) {
                    /* send len of IP addr */
                    strLen = (uint32_t) strlen(ipAddrs->addr);
                    netOrderInt = htonl(strLen);
                    memcpy(buffer + len, (const void *) &netOrderInt, sizeof(uint32_t));
                    len += sizeof(uint32_t);

                    /* send IP addr */
                    memcpy(buffer + len, (const void *) ipAddrs->addr, strLen);
                    len += strLen;
/*printf("rc connect: sending IP addr %s to rc multicast server\n", ipAddrNext->addr);*/

                    /* send len of broadcast addr */
                    strLen = (uint32_t)strlen(ipAddrs->broadcast);
                    netOrderInt = htonl(strLen);
                    memcpy(buffer + len, (const void *) &netOrderInt, sizeof(uint32_t));
                    len += sizeof(uint32_t);

                    /* send broadcast addr */
                    memcpy(buffer + len, (const void *) ipAddrs->broadcast, strLen);
                    len += strLen;

                    addrCount = 1;
                    foundLocalAddressMatch = 1;

                    break;
                }

                ipAddrs = ipAddrs->next;
            }

            if (!foundLocalAddressMatch) {
printf("rc connect: error, IP addr %s is not local\n", ipForRcServer);
                close(domain->sendSocket);
                cMsgRestoreSignals(domain);
                pthread_cancel(domain->pendThread);
                cMsgDomainFree(domain);
                free(domain);
                free(buffer);
                free(servaddr);
                return(CMSG_NETWORK_ERROR);
            }
        }

        /* If we have not used the single, user-specified-in-UDL value
         * for our ip address, send everything */
        else {
            while (ipAddrs != NULL) {
                /* send len of IP addr */
                strLen = (uint32_t) strlen(ipAddrs->addr);
                netOrderInt = htonl(strLen);
                memcpy(buffer + len, (const void *) &netOrderInt, sizeof(int32_t));
                len += sizeof(int32_t);

                /* send IP addr */
                memcpy(buffer + len, (const void *) ipAddrs->addr, strLen);
                len += strLen;
/*printf("rc connect: sending IP addr %s to rc multicast server\n", ipAddrNext->addr);*/

                /* send len of broadcast addr */
                strLen =(uint32_t)  strlen(ipAddrs->broadcast);
                netOrderInt = htonl(strLen);
                memcpy(buffer + len, (const void *) &netOrderInt, sizeof(int32_t));
                len += sizeof(int32_t);

                /* send broadcast addr */
                memcpy(buffer + len, (const void *) ipAddrs->broadcast, strLen);
                len += strLen;
/*printf("rc connect: sending broadcast addr %s to rc multicast server\n", ipAddrNext->broadcast);*/

                addrCount++;
                ipAddrs = ipAddrs->next;
            }
        }

        /* now write out how many addrs are coming next */
        netOrderInt = htonl(addrCount);
        memcpy(pAddrCount, (const void *)&netOrderInt, sizeof(int32_t));

        /* Add counter to end of data in order to track how many multicast packets are sent */
        netOrderCounter = htonl(1);
        memcpy(buffer + len, (const void *) &netOrderCounter, sizeof(uint32_t));
        len += sizeof(uint32_t);

        /* free up mem */
        codanetFreeIpAddrs(ipAddrs);
    }

    if (ipForRcServer != NULL) free(ipForRcServer);

    /* create and start a thread which will multicast every second */
    bArg.len       = (socklen_t) (sizeof(struct sockaddr_in));
    bArg.sockfd    = domain->sendSocket;
    bArg.paddr     = servaddr;
    bArg.buffer    = buffer;
    bArg.bufferLen = len;
    
/*printf("rc connect: will create sender thread\n");*/
    status = pthread_create(&bThread, NULL, multicastThd, (void *)(&bArg));
    if (status != 0) {
        cmsg_err_abort(status, "Creating multicast sending thread");
    }
            
    /*
     * Wait for a message to come in to the TCP listening thread.
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

    /* Told by multicast server to stop waiting for the
     * rc Server to finish the connection. */
    if (domain->rcConnectAbort) {
/*printf("rc connect: told to abort connect by RC Multicast server\n");*/
        close(domain->sendSocket);
        cMsgRestoreSignals(domain);
        pthread_cancel(bThread);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        return(CMSG_ABORT);
    }
    
    if (status < 1 || !domain->rcConnectComplete) {
printf("rc connect: wait timeout or rcConnectComplete is not 1\n");
        close(domain->sendSocket);
        cMsgRestoreSignals(domain);
        pthread_cancel(bThread);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        return(CMSG_TIMEOUT);
    }
    
/*printf("rc connect: got a response from rc server, 3-way connect finished, now make 2 connections to rc server\n");*/
    
    /* stop multicasting thread */
    pthread_cancel(bThread);
        
    close(domain->sendSocket);

    cMsgMutexUnlock(&domain->rcConnectMutex);

    /* create TCP sending socket and store */
    printf("rc connect: try making tcp connection to RC server = %s w/ TO = %u sec, %u msec\n",
           domain->sendHost, (uint32_t) ((&tv)->tv_sec),(uint32_t) ((&tv)->tv_usec));

    if ((err = cMsgNetTcpConnectTimeout(domain->sendHost, (unsigned short) domain->sendPort,
                                        CMSG_BIGSOCKBUFSIZE, 0, 1, &tv, &domain->sendSocket, NULL)) == CMSG_OK) {
        printf("rc connect: SUCCESS connecting to %s, port %d\n", domain->sendHost, domain->sendPort);
    }
    else {
        printf("rc connect: failed to connect to %s\n", domain->sendHost);
        cMsgRestoreSignals(domain);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        return (err);
    }

    /*
     * Create a new UDP "connection". This means all subsequent sends are to
     * be done with the "send" and not the "sendto" function. The benefit is 
     * that the udp socket does not have to connect and disconnect for each
     * message sent.
     */
    memset((void *)&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t) domain->sendUdpPort);
    
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
    if ( (err = cMsgNetStringToNumericIPaddr(domain->sendHost, &addr)) != CMSG_OK ) {
        cMsgRestoreSignals(domain);
        close(domain->sendUdpSocket);
        close(domain->sendSocket);
        pthread_cancel(domain->pendThread);
        cMsgDomainFree(domain);
        free(domain);
        return(err);
    }

/* printf("rc connect: try UDP connection rc server on port = %hu\n", ntohs(addr.sin_port));*/
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

    {
        struct timespec delay = {1, 0}; /* 1 sec */
        nanosleep(&delay, NULL);
    }

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


/** Structure for freeing memory in cleanUpHandler. */
typedef struct freeMem_t {
    char **ifNames;
    int nameCount;
    char *buffer;
    struct sockaddr_in *pAddr;
} freeMem;


/*-------------------------------------------------------------------*
 * multicastThd needs a pthread cancellation cleanup handler.
 * This handler will be called when the multicastThd is
 * cancelled. It's only task is to free memory.
 *-------------------------------------------------------------------*/
static void cleanUpHandler(void *arg) {
    int i;
    freeMem *pMem = (freeMem *)arg;

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "cleanUpHandler: in\n");
    }

    /* release memory */
    if (pMem == NULL) return;
    if (pMem->ifNames != NULL) {
        for (i=0; i < pMem->nameCount; i++) {
            free(pMem->ifNames[i]);
        }
        free(pMem->ifNames);
    }

    free(pMem->buffer);
    free(pMem->pAddr);

    free(pMem);
}


/**
 * This routine starts a thread to multicast a UDP packet to the server
 * every second.
 */
static void *multicastThd(void *arg) {

    char **ifNames;
    freeMem *pfreeMem;
    int i, err, useDefaultIf=0, count;
    thdArg *threadArg = (thdArg *) arg;
    struct timespec  wait = {0, 100000000}; /* 0.1 sec */
    struct timespec delay = {0, 200000000}; /* 0.2 sec */
    struct timespec betweenRounds = {1, 0}; /* 1.0 sec */
    char *buffer  = threadArg->buffer;
    int bufferLen = threadArg->bufferLen;
    uint32_t packetCounter = 1, netOrderCounter;
    int counterOffset = bufferLen - sizeof(uint32_t);
    ssize_t ret;

    /* release resources when done */
    pthread_detach(pthread_self());
    
    /* A slight delay here will help the main thread (calling connect)
     * to be already waiting for a response from the server when we
     * multicast to the server here (prompting that response). This
     * will help insure no responses will be lost.
     */
    nanosleep(&wait, NULL);
    
    err = cMsgNetGetIfAndLoopbackNames(&ifNames, &count);
    if (err != CMSG_OK || count < 1 || ifNames == NULL) {
        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "multicastThd: cannot find network interface info, use defaults\n");
        }
        useDefaultIf = 1;
    }

    /* Install a cleanup handler for this thread's cancellation. 
     * Give it a pointer which points to the memory which must
     * be freed upon cancelling this thread. */

    /* Create pointer to malloced mem which will hold 2 pointers. */
    pfreeMem = (freeMem *) malloc(sizeof(freeMem));
    if (pfreeMem == NULL) {
        if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
            fprintf(stderr, "multicastThd: cannot allocate memory\n");
        }
        exit(1);
    }
    pfreeMem->ifNames = ifNames;
    pfreeMem->nameCount = count;
    pfreeMem->buffer = buffer;
    pfreeMem->pAddr = threadArg->paddr;

    pthread_cleanup_push(cleanUpHandler, (void *)pfreeMem);

    while (1) {
        if (useDefaultIf) {
            sendto(threadArg->sockfd, (void *)buffer, bufferLen, 0,
                   (SA *) threadArg->paddr, threadArg->len);
        }
        else {
            for (i=0; i < count; i++) {
                if (cMsgDebug >= CMSG_DEBUG_INFO) {
                    printf("multicastThd: send mcast on interface %s\n", ifNames[i]);
                }

                /* set socket to send over this interface */
                err = cMsgNetMcastSetIf(threadArg->sockfd, ifNames[i], 0);
                if (err != CMSG_OK) {
printf("RC client: error setting multicast socket to send over %s\n", ifNames[i]);
                    continue;
                }
    
printf("RC client: sending packet #%u over %s\n", packetCounter, ifNames[i]);
                ret = sendto(threadArg->sockfd, (void *)buffer, bufferLen, 0,
                             (SA *) threadArg->paddr, threadArg->len);

                packetCounter++;
                /* Send in network byte order which is big end first */
                buffer[counterOffset  ] = (char)(packetCounter >> 24);
                buffer[counterOffset+1] = (char)(packetCounter >> 16);
                buffer[counterOffset+2] = (char)(packetCounter >>  8);
                buffer[counterOffset+3] = (char)(packetCounter      );

                /* Wait 0.2 second between multicasting on each interface */
                if (count > 1 && i < (count - 1)) {
                    nanosleep(&delay, NULL);
                }
            }
        }

        /* Wait 1 second between rounds */
        nanosleep(&betweenRounds, NULL);
    }
    
 printf("Send multicast: exiting!\n");
    /* on some operating systems (Linux) this call is necessary - calls cleanup handler */
    pthread_cleanup_pop(1);

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
  size_t len, lenSender, lenSubject, lenType, lenByteArray, lenText, lenPayloadText;
  uint32_t highInt, lowInt;
  int err=CMSG_OK, fd, msgType, getResponse, outGoing[16];
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
  outGoing[1] = htonl((uint32_t) msgType);
  /* reserved for future use */
  outGoing[2] = htonl(CMSG_VERSION_MAJOR);
  /* user int */
  outGoing[3] = htonl((uint32_t) msg->userInt);
  /* bit info */
  outGoing[4] = htonl((uint32_t) msg->info);
  /* senderToken */
  outGoing[5] = htonl((uint32_t) msg->senderToken);

  /* time message sent (right now) */
  clock_gettime(CLOCK_REALTIME, &now);
  /* convert to milliseconds */
  llTime  = ((uint64_t)now.tv_sec * 1000) +
            ((uint64_t)now.tv_nsec/1000000);
  highInt = (uint32_t) ((llTime >> 32) & 0x00000000FFFFFFFF);
  lowInt  = (uint32_t) (llTime & 0x00000000FFFFFFFF);
  outGoing[6] = htonl(highInt);
  outGoing[7] = htonl(lowInt);

  /* user time */
  llTime  = ((uint64_t)msg->userTime.tv_sec * 1000) +
            ((uint64_t)msg->userTime.tv_nsec/1000000);
  highInt = (uint32_t) ((llTime >> 32) & 0x00000000FFFFFFFF);
  lowInt  = (uint32_t) (llTime & 0x00000000FFFFFFFF);
  outGoing[8] = htonl(highInt);
  outGoing[9] = htonl(lowInt);

  /* length of "sender" string */
  lenSender    = strlen(domain->name);
  outGoing[10] = htonl((uint32_t) lenSender);

  /* length of "subject" string */
  lenSubject   = strlen(msg->subject);
  outGoing[11] = htonl((uint32_t) lenSubject);

  /* length of "type" string */
  lenType      = strlen(msg->type);
  outGoing[12] = htonl((uint32_t) lenType);

  /* length of "payloadText" string */
  outGoing[13] = htonl((uint32_t) lenPayloadText);

  /* length of "text" string */
  outGoing[14] = htonl((uint32_t) lenText);

  /* length of byte array */
  lenByteArray = (size_t) msg->byteArrayLength;
  outGoing[15] = htonl((uint32_t) lenByteArray);

  /* total length of message (minus first int) is first item sent */
  len = sizeof(outGoing) + lenSubject + lenType +
        lenSender + lenPayloadText + lenText + lenByteArray;
  outGoing[0] = htonl((uint32_t) (len - sizeof(int)));

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
    domain->msgBufferSize = (int) (len + 1024); /* give us 1kB extra */
    domain->msgBuffer = (char *) malloc((size_t) domain->msgBufferSize);
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
  sendLen = cMsgNetTcpWrite(fd, (void *) domain->msgBuffer, (int)len);
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

  size_t len, lenSender, lenSubject, lenType, lenText, lenPayloadText, lenByteArray;
  uint32_t highInt, lowInt;
  int err=CMSG_OK, fd, msgType, getResponse, outGoing[19];
  ssize_t sendLen;
  uint64_t llTime;
  struct timespec now;

//    cMsgMessage_t *msg = (cMsgMessage_t *) vmsg;
//    cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
//    size_t len, lenSender, lenSubject, lenType, lenByteArray, lenText, lenPayloadText;
//    uint32_t highInt, lowInt;
//    int err=CMSG_OK, fd, msgType, getResponse, outGoing[16];
//    ssize_t sendLen;
//    uint64_t llTime;
//    struct timespec now;

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
  outGoing[4] = htonl((uint32_t) msgType);
  /* reserved for future use */
  outGoing[5] = htonl(CMSG_VERSION_MAJOR);
  /* user int */
  outGoing[6] = htonl((uint32_t) msg->userInt);
  /* bit info */
  outGoing[7] = htonl((uint32_t) msg->info);
  /* senderToken */
  outGoing[8] = htonl((uint32_t) msg->senderToken);

  /* time message sent (right now) */
  clock_gettime(CLOCK_REALTIME, &now);
  /* convert to milliseconds */
  llTime  = ((uint64_t)now.tv_sec * 1000) +
      ((uint64_t)now.tv_nsec/1000000);
  highInt = (uint32_t) ((llTime >> 32) & 0x00000000FFFFFFFF);
  lowInt  = (uint32_t) (llTime & 0x00000000FFFFFFFF);
  outGoing[9]  = htonl(highInt);
  outGoing[10] = htonl(lowInt);

  /* user time */
  llTime  = ((uint64_t)msg->userTime.tv_sec * 1000) +
      ((uint64_t)msg->userTime.tv_nsec/1000000);
  highInt = (uint32_t) ((llTime >> 32) & 0x00000000FFFFFFFF);
  lowInt  = (uint32_t) (llTime & 0x00000000FFFFFFFF);
  outGoing[11] = htonl(highInt);
  outGoing[12] = htonl(lowInt);

  /* length of "sender" string */
  lenSender    = strlen(domain->name);
  outGoing[13] = htonl((uint32_t) lenSender);

  /* length of "subject" string */
  lenSubject   = strlen(msg->subject);
  outGoing[14] = htonl((uint32_t) lenSubject);

  /* length of "type" string */
  lenType      = strlen(msg->type);
  outGoing[15] = htonl((uint32_t) lenType);

  /* length of "payloadText" string */
  outGoing[16] = htonl((uint32_t) lenPayloadText);

  /* length of "text" string */
  outGoing[17] = htonl((uint32_t) lenText);

  /* length of byte array */
  lenByteArray = (size_t) msg->byteArrayLength;
  outGoing[18] = htonl((uint32_t) lenByteArray);

  len = sizeof(outGoing) + lenSubject + lenType +
        lenSender + lenPayloadText + lenText + lenByteArray;
  /* total length of message (minus magic numbers and first int which is length)
   * is first item sent */
  outGoing[3] = htonl((uint32_t) (len - 4*sizeof(int)));

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
    domain->msgBufferSize = (int) (len + 1024); /* give us 1kB extra */
    domain->msgBuffer = (char *) malloc((size_t) domain->msgBufferSize);
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
      sub->subject = strdup(subject);
      sub->type    = strdup(type);
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

    int status;
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
    if (total != NULL) *total = (int)cb->msgCount;
    
    cMsgMutexUnlock(&cb->mutex);
    
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/

/**
 * This routine starts a thread to receive a return UDP packet from
 * the server due to our initial multicast.
 */
static void *receiverThd(void *arg) {
    int i, status, udpPort, ipLen, ipCount, cMsgVersion, hostLen, expidLen, magicInt[3];
    thdArg *threadArg = (thdArg *) arg;
    char buffer[1024], *tmp, *pchar, *host;
    ssize_t len;
    codaIpList *listHead=NULL, *listEnd=NULL, *listItem;
    socklen_t sockLen;
    struct sockaddr_in addr;

    /* release resources when done */
    pthread_detach(pthread_self());

    /*----------------------------------------*/
    /* Sync with thread that spawned this one */
    status = pthread_mutex_lock(&mutex);
    if (status != 0) {
        cmsg_err_abort(status, "pthread_mutex_lock");
    }

    /* Tell main thread we are started. */
    status = pthread_cond_signal(&cond);
    if (status != 0) {
        cmsg_err_abort(status, "pthread_cond_signal");
    }

    status = pthread_mutex_unlock(&mutex);
    if (status != 0) {
        cmsg_err_abort(status, "pthread_mutex_unlock");
    }
    /*----------------------------------------*/

    memset((void *)&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;

    nextPacket:
    while (1) {
        /* zero buffer */
        pchar = memset((void *) buffer, 0, 1024);

        /* ignore error as it will be caught later */
        len = recvfrom(threadArg->sockfd, (void *) buffer, 1024, 0, (SA *) &addr, &sockLen);
/*
printf("Multicast response from: %s, on port %hu, with msg len = %hd\n",
                inet_ntoa(addr.sin_addr),
                ntohs(addr.sin_port), len);
*/
        /* server is sending 9 ints + string */
        if (len < 9 * sizeof(int)) continue;

        /* The server is sending back its 1) port, 2) host name length, 3) host name */
        memcpy(magicInt, pchar, 3 * sizeof(int));
        magicInt[0] = ntohl((uint32_t)magicInt[0]);
        magicInt[1] = ntohl((uint32_t)magicInt[1]);
        magicInt[2] = ntohl((uint32_t)magicInt[2]);
        pchar += 3 * sizeof(int);

        if ((magicInt[0] != CMSG_MAGIC_INT1) ||
            (magicInt[1] != CMSG_MAGIC_INT2) ||
            (magicInt[2] != CMSG_MAGIC_INT3)) {
            printf("  Multicast response has wrong magic numbers, ignore packet\n");
            continue;
        }

        memcpy(&cMsgVersion, pchar, sizeof(int));
        cMsgVersion = ntohl((uint32_t)cMsgVersion);
        pchar += sizeof(int);

        memcpy(&udpPort, pchar, sizeof(int));
        udpPort = ntohl((uint32_t)udpPort);
        pchar += sizeof(int);

        memcpy(&hostLen, pchar, sizeof(int));
        hostLen = ntohl((uint32_t)hostLen);
        pchar += sizeof(int);

        memcpy(&expidLen, pchar, sizeof(int));
        expidLen = ntohl((uint32_t)expidLen);
        pchar += sizeof(int);

        if (cMsgVersion != CMSG_VERSION_MAJOR) {
            printf("  rc multicast server is wrong cmsg version (%d), should be %d, ignore packet\n", cMsgVersion,
                   CMSG_VERSION_MAJOR);
            continue;
        }

        if ((udpPort < 1024) || (udpPort > 65535)) {
            printf("  bad port value, ignore packet\n");
            continue;
        }

        /*  read host string  */
        if (hostLen > 0) {
            if ((tmp = (char *) malloc((size_t)(hostLen + 1))) == NULL) {
                printf("  out of memory, quit program\n");
                exit(-1);
            }
            /* read sender string into memory */
            memcpy(tmp, pchar, (size_t)hostLen);
            /* add null terminator to string */
            tmp[hostLen] = 0;
            /* store string in msg structure */
            host = tmp;
            /* go to next string */
            pchar += hostLen;
            /* printf("sender = %s\n", tmp); */
        }
        else {
            host = NULL;
        }

        /*  skip over expid   */
        if (expidLen > 0) {
            pchar += expidLen;
        }

        /*--------------------------*/
        /*  how many IP addresses?  */
        /*--------------------------*/
        memcpy(&ipCount, pchar, sizeof(int));
        ipCount = ntohl((uint32_t)ipCount);
        pchar += sizeof(int);

        if ((ipCount < 0) || (ipCount > 50)) {
            printf("  bad number of IP addresses, ignore packet\n");
            ipCount = 0;
            continue;
        }

        listHead = NULL;

        for (i=0; i < ipCount; i++) {
            /* Create address item */
            listItem = (codaIpList *) calloc(1, sizeof(codaIpList));
            if (listItem == NULL) {
                printf("  out of memory, quit program\n");
                exit(-1);
            }

            /* First read in IP address len */
            memcpy(&ipLen, pchar, sizeof(int));
            ipLen = ntohl((uint32_t)ipLen);
            pchar += sizeof(int);

            /* IP address is in dot-decimal format */
            if (ipLen < 7 || ipLen > 20) {
                printf("  Multicast response has wrong format, ignore packet\n");
                codanetFreeAddrList(listHead);
                if (host != NULL) free(host);
                goto nextPacket;
            }

            /* Put address into a list for later sorting */
            memcpy(listItem->addr , pchar, (size_t)ipLen);
            listItem->addr[ipLen] = 0;
            pchar += ipLen;

            /* Skip over the corresponding broadcast or network address */
            memcpy(&ipLen, pchar, sizeof(int));
            ipLen = ntohl((uint32_t)ipLen);
            pchar += sizeof(int);

            if (ipLen < 7 || ipLen > 20) {
                printf("  Multicast response has wrong format, ignore packet\n");
                codanetFreeAddrList(listHead);
                if (host != NULL) free(host);
                goto nextPacket;
            }

            pchar += ipLen;

/*printf("Found ip = %s\n", listItem->addr);*/

            /* Put address item into a list for later sorting */
            if (listHead == NULL) {
                listHead = listEnd = listItem;
            }
            else {
                listEnd->next = listItem;
                listEnd = listItem;
            }
        }

        /* send info back to calling function */
        threadArg->senderHost = host;
        threadArg->ipList = listHead;
        threadArg->count  = ipCount;

        /* Tell main thread we are done. */
        pthread_cond_signal(&cond);
        break;
    }

    pthread_exit(NULL);
    return NULL;
}


/*-------------------------------------------------------------------*/


/**
 * This routine is a synchronous call to receive a message containing monitoring data
 * from the rc multicast server specified in the UDL.
 * In this case, the "monitoring" data is just the host on which the rc multicast
 * server is running. It can be found by calling the
 * {@link getSenderHost()} method of the returned message.
 * Get the whole list of server IP addresses in the
 * String array payload item called "IpAddresses". This is useful when trying to find
 * the location of a particular AFECS (runcontrol) platform.
 *
 * @param  domainId id of the domain connection
 * @param  command time in milliseconds to wait for a response to multicasts (1000 default),
 *         0 means wait forever
 * @param  replyMsg pointer which gets filled in with pointer to message containing the
 *         host running the rc multicast server contacted (in senderHost field);
 *         NULL if no response is found
 *
 * @return CMSG_OK is successful
 * @return CMSG_BAD_ARGUMENT if domainId or replyMsg arg is NULL
 * @return CMSG_OUT_OF_MEMORY if memory cannot be allocated
 * @return CMSG_SOCKET_ERROR if trouble with socket IO
 */
int cmsg_rc_monitor(void *domainId, const char *command, void **replyMsg) {


    cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;

    char   *host=NULL, buffer[1024];
    int    status, err, len, sockfd, nameLen, expidLen, outGoing[9], multicastTO, gotResponse = 0;
    unsigned char ttl = 32;
    thdArg    rArg;
    pthread_t rThread;
    codaIpList *listHead=NULL, *listItem;
    struct timespec wait, time;
    struct sockaddr_in servaddr;


    if (domain == NULL || replyMsg == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }

    /******************************************/
    /* Prepare packet for rc multicast server */
    /******************************************/

    /* Clear buffer */
    memset((void *)buffer, 0, 1024);

    /* Create UDP socket for multicasting */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return(CMSG_SOCKET_ERROR);
    }

    /* Set TTL to 32 so it will make it through routers. */
    err = setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL, (void *) &ttl, sizeof(ttl));
    if (err < 0){
        close(sockfd);
        return(CMSG_SOCKET_ERROR);
    }

    /* send packet to host & port parsed from UDL */
    memset((void *)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port   = htons((uint16_t) (domain->localPort));
    if ( (err = cMsgNetStringToNumericIPaddr(domain->serverHost, &servaddr)) != CMSG_OK ) {
        /* an error should never be returned here */
        close(sockfd);
        return(err);
    }

    nameLen  = (int) strlen(domain->name);
    expidLen = (int) strlen(domain->expid);

    /*
     * We send these items explicitly:
     *   1) 3 magic ints for connection protection
     *   2) cMsg version
     *   3) command saying we're probing rc server
     *   4) port = 0 means this is an rc client calling monitor() to generate probe
     *   5) id not used, = 0
     *   6) length of name
     *   7) length of expid
     * The host we're sending from gets sent for free
     * as does the UDP port we're sending from.
     */

    /* magic ints */
    outGoing[0] = htonl(CMSG_MAGIC_INT1);
    outGoing[1] = htonl(CMSG_MAGIC_INT2);
    outGoing[2] = htonl(CMSG_MAGIC_INT3);
    /* cMsg version */
    outGoing[3] = htonl(CMSG_VERSION_MAJOR);
    /* type of message */
    outGoing[4] = htonl(RC_DOMAIN_MULTICAST_PROBE);
    /* "port" of value 0 tells rc server this is a probe from an RC client monitor() call */
    outGoing[5] = 0;
    /* id not used in probe => 0 */
    outGoing[6] = 0;
    /* length of "name" string */
    outGoing[7] = htonl((uint32_t)nameLen);
    /* length of "expid" string */
    outGoing[8] = htonl((uint32_t)expidLen);

    /* copy data into a single buffer */
    len = sizeof(outGoing);

    memcpy(buffer, (void *)outGoing, (size_t)len);

    if (nameLen > 0) {
        memcpy(buffer+len, (const void *)domain->name, (size_t)nameLen);
        len += nameLen;
    }

    if (expidLen > 0) {
        memcpy(buffer+len, (const void *)domain->expid, (size_t)expidLen);
        len += expidLen;
    }

    /**************************************/
    /*   Spawn response-reading thread    */
    /**************************************/
    status = pthread_mutex_lock(&mutex);
    if (status != 0) {
        cmsg_err_abort(status, "pthread_mutex_lock");
    }

    rArg.sockfd = sockfd;

    status = pthread_create(&rThread, NULL, receiverThd, (void *)(&rArg));
    if (status != 0) {
        cmsg_err_abort(status, "Creating multicast response receiving thread");
    }
    sched_yield();

    /* Wait for spawned thread to tell me it has started */
    status = pthread_cond_wait(&cond, &mutex);
    if (status != 0) {
        cmsg_err_abort(status, "pthread_cond_wait");
    }

    status = pthread_mutex_unlock(&mutex);
    if (status != 0) {
        cmsg_err_abort(status, "pthread_mutex_unlock");
    }

    /**************************************/
    /* Send packet to rc multicast server */
    /**************************************/

/*printf("Send multicast to cMsg server on socket %d\n", threadArg->sockfd);*/
    sendto(sockfd, (void *)buffer, (size_t)len, 0, (SA *) &servaddr, (socklen_t) sizeof(servaddr));

    /***********************/
    /* Wait for a response */
    /***********************/

    /* command in time in milliseconds to wait for a response */
    int millisecWait = 1000;
    if (command != NULL) {
        millisecWait = atoi(command);
        if (millisecWait < 0) millisecWait = 1000;
    }
    multicastTO = millisecWait/1000;

    if (multicastTO > 0) {
        wait.tv_sec  = multicastTO;
        wait.tv_nsec = 0;
        cMsgGetAbsoluteTime(&wait, &time);

        status = pthread_mutex_lock(&mutex);
        if (status != 0) {
            cmsg_err_abort(status, "pthread_mutex_lock");
        }

/*printf("Wait %d seconds for multicast to be answered\n", multicastTO);*/
        status = pthread_cond_timedwait(&cond, &mutex, &time);
        if (status == ETIMEDOUT) {
            /* stop receiving thread */
            pthread_cancel(rThread);
            sched_yield();
        }
        else if (status != 0) {
            cmsg_err_abort(status, "pthread_cond_timedwait");
        }
        else {
            gotResponse = 1;
        }

        status = pthread_mutex_unlock(&mutex);
        if (status != 0) {
            cmsg_err_abort(status, "pthread_mutex_unlock");
        }
    }
    else {
        status = pthread_mutex_lock(&mutex);
        if (status != 0) {
            cmsg_err_abort(status, "pthread_mutex_lock");
        }

/*printf("Wait forever for multicast to be answered\n");*/
        status = pthread_cond_wait(&cond, &mutex);
        if (status != 0) {
            cmsg_err_abort(status, "pthread_cond_wait");
        }

        gotResponse = 1;

        status = pthread_mutex_unlock(&mutex);
        if (status != 0) {
            cmsg_err_abort(status, "pthread_mutex_unlock");
        }
    }

    /* If we timed out ... */
    if (!gotResponse) {
        codanetFreeAddrList(listHead);
        *replyMsg = NULL;
        return (CMSG_OK);
    }

    /**************************************/
    /* Package response into cMsg message */
    /**************************************/

    cMsgMessage_t *msg = cMsgCreateMessage();
    if (msg == NULL) {
        codanetFreeAddrList(listHead);
        if (host != NULL) free(host);
        return (CMSG_OUT_OF_MEMORY);
    }

    /* Keep no history with msg */
    cMsgSetHistoryLengthMax(msg, 0);

    msg->senderHost = rArg.senderHost;
    listHead = rArg.ipList;

    /* Add payload item - array of IP addresses */
    if (listHead != NULL) {
        int count = 0;
        const char* ipAddrs[rArg.count];
        listItem = listHead;
        while (listItem != NULL) {
            ipAddrs[count++] = listItem->addr;
            listItem = listItem->next;
        }

        /* Array is copied into msg */
        cMsgAddStringArray(msg, "IpAddresses", ipAddrs, rArg.count);
        codanetFreeAddrList(listHead);
    }

    *replyMsg = msg;
    return (CMSG_OK);
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
    int i, tblSize, status, loops=0, domainUsed;
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
 *   <b>cMsg:rc://&lt;host&gt;:&lt;port&gt;/&lt;expid&gt;?connectTO=&lt;timeout&gt;&ip=&lt;address&gt;</b><p>
 *
 * For the cMsg domain the UDL has the more specific form:<p>
 *   <b>cMsg:cMsg://&lt;host&gt;:&lt;port&gt;/&lt;subdomainType&gt;/&lt;subdomain remainder&gt;?tag=value&tag2=value2 ...</b><p>
 *
 * Remember that for this domain:
 *<ul>
 *<li>host is required and may also be "multicast", "localhost", or in dotted decimal form<p>
 *<li>port is optional with a default of {@link RC_MULTICAST_PORT}<p>
 *<li>the experiment id or expid is required, it is NOT taken from the environmental variable EXPID<p>
 *<li>connectTO (optional) is the time to wait in seconds before connect returns a
 *    timeout while waiting for the rc server to send a special (tcp)
 *    concluding connect message. Defaults to 30 seconds.<p>
 *<li>ip (optional) is ip address in dot-decimal format which the rc server
 *    or agent must use to connect to this rc client.<p>
 *</ul><p>
 *
 * @param UDLR  udl to be parsed
 * @param host  pointer filled in with host
 * @param port  pointer filled in with port
 * @param expid pointer filled in with expid tag's value if it exists
 * @param connectTO   pointer filled in with connect timeout tag's value if it exists
 * @param remainder   pointer filled in with the part of the udl after host:port/expid if it exists,
 *                    else it is set to NULL
 * @param ip    pointer filled in with ip address to use for connecting back to this client
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
                    int  *connectTO,
                    char **remainder,
                    char **ip) {

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
    udlRemainder = strdup(UDLR);
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
            if (cMsgNetLocalHost(buffer, (int)bufLength) != CMSG_OK) {
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
        *host = strdup(buffer);
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
      *port = (unsigned short)Port;
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
            *expid = strdup(buffer);
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
            *remainder = strdup(buffer);
        }
/*printf("parseUDL: remainder = %s, len = %d\n", buffer, len);*/
    }

    len = strlen(buffer);
    while (len > 0) {
        val = strdup(buffer);
       
        /* now look for ?connectTO=value& or &connectTO=value& */
        pattern = "connectTO=([0-9]+)&?";

        /* compile regular expression */
        cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);

        /* find matches */
        err = cMsgRegexec(&compiled, val, 2, matches, 0);
        
        /* if there's a match ... */
        if (err == 0 && matches[1].rm_so >= 0) {
            /* find timeout (in milliseconds) */
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
        else if (connectTO != NULL) {
            *connectTO = 30;
        }

        /* free up memory */
        cMsgRegfree(&compiled);


        /* find ip parameter if it exists, look for ip=<value> */
        pattern = "ip=([0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3})";

        /* compile regular expression */
        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            free(val);
            break;
        }

        /* find matches */
        err = cMsgRegexec(&compiled, val, 2, matches, 0);
        /* if there's a match ... */
        if (err == 0 && matches[1].rm_so >= 0) {
            int pos=0;
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            pos = matches[1].rm_eo;
            strncat(buffer, val+matches[1].rm_so, len);
            if (ip != NULL) {
                *ip = strdup(buffer);
            }
printf("parseUDL: ip = %s\n", buffer);
        }
        else if (ip != NULL) {
            *ip = NULL;
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

/*----------------------------------------------------------------------------*
 *                                                                            *
 *  Copyright (c) 2014        Jefferson Science Associates,                   *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 15-Aug-2014, Jefferson Lab                                   *
 *                                                                            *
 *    Authors: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, #10           *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 *
 * Description:
 *
 *  Implements the emu domain used by CODA components - for communication between
 *  Rocs & Ebs and between Ebs and other Ebs. The idea is to avoid using ET
 *  systems to transfer data and use a simple socket.
 *
 *----------------------------------------------------------------------------*/

/**
 * @file
 * This file contains the emu domain implementation of the cMsg user API.
 * This a messaging system programmed by the Data Acquisition Group at Jefferson
 * Lab. The emu domain allows CODA components between themselves - for for
 * communication between Rocs & Ebs and between Ebs and other Ebs. The idea is to
 * avoid using an ET system to transfer data and use a simple socket.
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
#include "cMsgNetwork.h"
#include "cMsgRegex.h"
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
    cMsgDomainInfo *domain;
} thdArg;

/* built-in limits */
/** Number of seconds to wait for emuClientListeningThread threads to start. */
#define WAIT_FOR_THREADS 10

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
static int parseUDL(const char *UDLR, unsigned short *port,
                    char **expid, char **compName, int  *codaId,
                    int  *timeout, int  *bufSize, int  *tcpSend,
                    int  *noDelay, char** subnet, char **remainder);
                      
/* Prototypes of the 28 functions which implement the standard tasks in cMsg. */
int   cmsg_emu_connect           (const char *myUDL, const char *myName,
                                  const char *myDescription,
                                  const char *UDLremainder, void **domainId);
int   cmsg_emu_reconnect         (void *domainId);
int   cmsg_emu_send              (void *domainId, void *msg);
int   cmsg_emu_syncSend          (void *domainId, void *msg, const struct timespec *timeout,
                                  int *response);
int   cmsg_emu_flush             (void *domainId, const struct timespec *timeout);
int   cmsg_emu_subscribe         (void *domainId, const char *subject, const char *type,
                                  cMsgCallbackFunc *callback, void *userArg,
                                  cMsgSubscribeConfig *config, void **handle);
int   cmsg_emu_unsubscribe       (void *domainId, void *handle);
int   cmsg_emu_subscriptionPause (void *domainId, void *handle);
int   cmsg_emu_subscriptionResume(void *domainId, void *handle);
int   cmsg_emu_subscriptionQueueClear(void *domainId, void *handle);
int   cmsg_emu_subscriptionQueueCount(void *domainId, void *handle, int *count);
int   cmsg_emu_subscriptionQueueIsFull(void *domainId, void *handle, int *full);
int   cmsg_emu_subscriptionMessagesTotal(void *domainId, void *handle, int *total);
int   cmsg_emu_subscribeAndGet   (void *domainId, const char *subject, const char *type,
                                  const struct timespec *timeout, void **replyMsg);
int   cmsg_emu_sendAndGet        (void *domainId, void *sendMsg,
                                  const struct timespec *timeout, void **replyMsg);
int   cmsg_emu_monitor           (void *domainId, const char *command, void **replyMsg);
int   cmsg_emu_start             (void *domainId);
int   cmsg_emu_stop              (void *domainId);
int   cmsg_emu_disconnect        (void **domainId);
int   cmsg_emu_shutdownClients   (void *domainId, const char *client, int flag);
int   cmsg_emu_shutdownServers   (void *domainId, const char *server, int flag);
int   cmsg_emu_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg);
int   cmsg_emu_isConnected       (void *domainId, int *connected);
int   cmsg_emu_setUDL            (void *domainId, const char *udl, const char *remainder);
int   cmsg_emu_getCurrentUDL     (void *domainId, const char **udl);
int   cmsg_emu_getServerHost     (void *domainId, const char **ipAddress);
int   cmsg_emu_getServerPort     (void *domainId, int *port);
int   cmsg_emu_getInfo           (void *domainId, const char *command, char **string);

/** List of the functions which implement the standard cMsg tasks in this domain. */
static domainFunctions functions = {cmsg_emu_connect, cmsg_emu_reconnect,
                                    cmsg_emu_send, cmsg_emu_syncSend, cmsg_emu_flush,
                                    cmsg_emu_subscribe, cmsg_emu_unsubscribe,
                                    cmsg_emu_subscriptionPause, cmsg_emu_subscriptionResume,
                                    cmsg_emu_subscriptionQueueClear, cmsg_emu_subscriptionMessagesTotal,
                                    cmsg_emu_subscriptionQueueCount, cmsg_emu_subscriptionQueueIsFull,
                                    cmsg_emu_subscribeAndGet, cmsg_emu_sendAndGet,
                                    cmsg_emu_monitor, cmsg_emu_start,
                                    cmsg_emu_stop, cmsg_emu_disconnect,
                                    cmsg_emu_shutdownClients, cmsg_emu_shutdownServers,
                                    cmsg_emu_setShutdownHandler, cmsg_emu_isConnected,
                                    cmsg_emu_setUDL, cmsg_emu_getCurrentUDL,
                                    cmsg_emu_getServerHost, cmsg_emu_getServerPort,
                                    cmsg_emu_getInfo};
                                    
/* emu domain type */
domainTypeInfo emuDomainTypeInfo = {
  "emu",
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
int cmsg_emu_getInfo(void *domainId, const char *command, char **string) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
* This routine resets the server host anme, but is <b>NOT</b> implemented in this domain.
* @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
*/
int cmsg_emu_getServerHost(void *domainId, const char **ipAddress) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
* This routine resets server socket port, but is <b>NOT</b> implemented in this domain.
* @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
*/
int cmsg_emu_getServerPort(void *domainId, int *port) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * This routine resets the UDL, but is <b>NOT</b> implemented in this domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */
int cmsg_emu_setUDL(void *domainId, const char *newUDL, const char *newRemainder) {
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
int cmsg_emu_getCurrentUDL(void *domainId, const char **udl) {
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
int cmsg_emu_isConnected(void *domainId, int *connected) {
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
 * This routine is called once to connect to an emu domain. It is called
 * by the user through top-level cMsg API, "cMsgConnect()".
 * The argument "myUDL" is the Universal Domain Locator used to uniquely
 * identify the emu server to connect to.
 * Emu domain UDL is of the form:<p>
 *   <b>cMsg:emu://&lt;port&gt;/&lt;expid&gt;?codaId=&lt;id&gt;&timeout=&lt;sec&gt;&bufSize=&lt;size&gt;&tcpSend=&lt;size&gt;&subnet=&lt;subnet&gt;&noDelay</b><p>
 *
 * Remember that for this domain:
 *<ol>
 *<li>multicast address is always 239.230.0.0<p>
 *<li>port (of emu domain server) is required<p>
 *<li>expid is required<p>
 *<li>codaId (coda id of data sender) is required<p>
 *<li>optional timeout (sec) to connect to emu server, default = 0 (wait forever)<p>
 *<li>optional bufSize (max size in bytes of a single send), min = 1KB, default = 2.1MB<p>
 *<li>optional tcpSend is the TCP send buffer size in bytes, min = 1KB<p>
 *<li>optional subnet is the preferred subnet used to connect to server<p>
 *<li>optional noDelay is the TCP no-delay parameter turned on<p>
 *</ol><p>
 *
 * If successful, this routine fills the argument "domainId", which identifies
 * the connection uniquely and is required as an argument by many other routines.
 *
 * @param myUDL the Universal Domain Locator used to uniquely identify the emu
 *        server to connect to
 * @param myName name of this client
 * @param myDescription description of this client
 * @param UDLremainder partially parsed (initial cMsg:emu:// stripped off)
 *                     UDL which gets passed down from the API level (cMsgConnect())
 * @param domainId pointer to integer which gets filled with a unique id referring
 *        to this connection.
 *
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_FORMAT if UDLremainder arg is not in the proper format
 * @returns CMSG_BAD_ARGUMENT if UDLremainder arg is NULL
 * @returns CMSG_ABORT if emu Multicast server aborts connection before emu server
 *                     can complete it
 * @returns CMSG_OUT_OF_RANGE if the port specified in the UDL is out-of-range
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 * @returns CMSG_TIMEOUT if timed out of wait for either response to multicast or
 *                       for emu server to complete connection
 *
 * @returns CMSG_SOCKET_ERROR if the listening thread finds all the ports it tries
 *                            to listen on are busy, tcp socket to emu server could
 *                            not be created, udp socket for sending to emu server
 *                            could not be created, could not connect udp socket to
 *                            emu server once created, udp socket for multicasting
 *                            could not be created, or socket options could not be set.
 *                            
 * @returns CMSG_NETWORK_ERROR if host name in UDL or emu server's host could not be resolved, or
 *                             no connection to the emu server can be made, or
 *                             a communication error with server occurs.
 */   
int cmsg_emu_connect(const char *myUDL, const char *myName, const char *myDescription,
                     const char *UDLremainder, void **domainId) {
  
    unsigned short serverPort;
    char   *expid=NULL,*subnet=NULL, *componentName=NULL, buffer[1024];
    int     err, status, len, i, index=0, localPort;
    size_t  expidLen, nameLen;
    int32_t outGoing[7];
    int     myCodaId, multicastTO, dataBufSize, tcpSendBufSize=0, tcpNoDelay=0, off=0;
    char    temp[CMSG_MAXHOSTNAMELEN];
    unsigned char ttl = 32;
    cMsgDomainInfo *domain;

    pthread_t rThread, bThread;
    thdArg    rArg,    bArg;
    
    struct timespec wait, time;
    struct sockaddr_in servaddr, localaddr;
    int    gotResponse=0;

    /* for connecting to emu Server w/ TCP */
    hashNode *hashEntries = NULL;
    int hashEntryCount=0, gotValidEmuServerHost=0;
    char *emuServerHost = NULL;
    struct timeval tv;

    /* clear array */
    memset((void *)buffer, 0, 1024);
    
    /* parse the UDLRemainder to get the port, expid, component name, codaId,
       timeout, dataBufSize, TCP sendBufSize, TCP noDelay, and preferred subnet */
    err = parseUDL(UDLremainder, &serverPort, &expid, &componentName, &myCodaId, &multicastTO,
                   &dataBufSize, &tcpSendBufSize, &tcpNoDelay, &subnet, NULL);
    if (err != CMSG_OK) {
        return(err);
    }

    /* time for emu Server to respond */
    tv.tv_sec = multicastTO;
    tv.tv_usec = 0;

    /* allocate struct to hold connection info */
    domain = (cMsgDomainInfo *) calloc(1, sizeof(cMsgDomainInfo));
    if (domain == NULL) {
        free(expid);
        return(CMSG_OUT_OF_MEMORY);  
    }
    cMsgDomainInit(domain);  

    /* allocate memory for message-sending buffer */
    domain->msgBuffer     = (char *) malloc((size_t)initialMsgBufferSize);
    domain->msgBufferSize = initialMsgBufferSize;
    if (domain->msgBuffer == NULL) {
        cMsgDomainFree(domain);
        free(domain);
        free(expid);
        return(CMSG_OUT_OF_MEMORY);
    }

    /* store our host's name */
    gethostname(temp, CMSG_MAXHOSTNAMELEN);
    domain->myHost = strdup(temp);

    /* store names, can be changed until server connection established */
    domain->name        = strdup(myName);
    domain->udl         = strdup(myUDL);
    domain->description = strdup(myDescription);


    /*-------------------------------------------------------
     * Talk to emu multicast server
     *-------------------------------------------------------*/
    
    /* create UDP socket */
    domain->sendSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (domain->sendSocket < 0) {
        cMsgRestoreSignals(domain);
        cMsgDomainFree(domain);
        free(domain);
        free(expid);
        return(CMSG_SOCKET_ERROR);
    }

    /* Give each local socket a unique port on a single host. */
    setsockopt(domain->sendSocket, SOL_SOCKET, SO_REUSEPORT, (void *)(&off), sizeof(int));

    memset((void *)&localaddr, 0, sizeof(localaddr));
    localaddr.sin_family = AF_INET;
    localaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    /* Pick local port for socket to avoid being assigned a port
       to which cMsgServerFinder is multicasting. */
    for (localPort = UDP_CLIENT_LISTENING_PORT; localPort < 65535; localPort++) {
        localaddr.sin_port = htons((uint16_t)localPort);
        if (bind(domain->sendSocket, (struct sockaddr *)&localaddr, sizeof(localaddr)) == 0) {
            break;
        }
    }
    /* If  bind always failed, then ephemeral port will be used. */

    memset((void *)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port   = htons(serverPort);

    /* Set TTL to 32 so it will make it through routers. */
    err = setsockopt(domain->sendSocket, IPPROTO_IP, IP_MULTICAST_TTL, (void *) &ttl, sizeof(ttl));
    if (err < 0) {
        close(domain->sendSocket);
        cMsgRestoreSignals(domain);
        cMsgDomainFree(domain);
        free(domain);
        free(expid);
        return(CMSG_SOCKET_ERROR);
    }

    if ( (err = cMsgNetStringToNumericIPaddr(EMU_MULTICAST_ADDR, &servaddr)) != CMSG_OK ) {
        close(domain->sendSocket);
        cMsgRestoreSignals(domain);
        cMsgDomainFree(domain);
        free(domain);
        free(expid);
        /* only possible errors are:
           CMSG_NETWORK_ERROR if the numeric address could not be obtained
           CMSG_OUT_OF_MEMORY if out of memory
        */
        return(err);
    }
    
    /*
     * We send 4 items:
     *   1) Type of multicast (emu domain multicast from client),
     *   2) major cMsg version number,
     *   3) name of destination CODA component, &
     *   4) EXPID (experiment id string)
     * The host we're sending from gets sent for free
     * as does the UDP port we're sending from.
     */

printf("emu connect: multicast info (expid = %s) to server on port = %hu (%s)\n",
        expid, serverPort, EMU_MULTICAST_ADDR);
    
    nameLen  = strlen(componentName);
    expidLen = strlen(expid);
    
    /* magic #s */
    outGoing[0] = htonl(CMSG_MAGIC_INT1);
    outGoing[1] = htonl(CMSG_MAGIC_INT2);
    outGoing[2] = htonl(CMSG_MAGIC_INT3);
    /* type of multicast */
    outGoing[3] = htonl(EMU_DOMAIN_MULTICAST);
    /* cMsg version */
    outGoing[4] = htonl(CMSG_VERSION_MAJOR);
    /* length of "myName" string */
    outGoing[5] = htonl((uint32_t)nameLen);
    /* length of "expid" string */
    outGoing[6] = htonl((uint32_t)expidLen);

    /* copy data into a single buffer */
    memcpy(buffer, (void *)outGoing, sizeof(outGoing));
    len = sizeof(outGoing);
    memcpy(buffer+len, (const void *)componentName, nameLen);
    len += nameLen;
    memcpy(buffer+len, (const void *)expid, expidLen);
    len += expidLen;
    
    /* create and start a thread which will receive any responses to our multicast */
    memset((void *)&rArg.addr, 0, sizeof(rArg.addr));
    rArg.len             = (socklen_t) sizeof(rArg.addr);
    rArg.port            = serverPort;
    rArg.expid           = expid;
    rArg.sockfd          = domain->sendSocket;
    rArg.domain          = domain;
    rArg.addr.sin_family = AF_INET;
    
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
 
printf("emu connect: wait %d seconds for multicast server to answer\n", multicastTO);
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
 
printf("emu connect: wait forever for multicast server to answer\n");
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
printf("emu connect: got no response\n");
        close(domain->sendSocket);
        cMsgRestoreSignals(domain);
        cMsgDomainFree(domain);
        free(domain);
        return(CMSG_TIMEOUT);
    }
    
printf("emu connect: got a response from mcast server\n");       
    close(domain->sendSocket);
 
    /* create TCP sending socket and store */

    /* The emu Server may have multiple network interfaces.
     * The ip address of each is stored in a hash table.
     * Try one at a time to see which we can use to connect. */

    if (!gotValidEmuServerHost) {
        for (i=0; i < hashEntryCount; i++) {
            emuServerHost = hashEntries[i].key;
printf("emu connect: from IP list, try making tcp connection to emu server = %s w/ TO = %u sec\n",
       emuServerHost, (uint32_t) ((&tv)->tv_sec));

//            if ((err = cMsgNetTcpConnectTimeout(emuServerHost, (unsigned short) domain->sendPort,
//                tcpSendBufSize , 0, tcpNoDelay, &tv, &domain->sendSocket, NULL)) == CMSG_OK) {
            if ((err = cMsgNetTcpConnect(emuServerHost, NULL, (unsigned short) domain->sendPort,
                tcpSendBufSize , 0, tcpNoDelay, &domain->sendSocket, NULL)) == CMSG_OK) {

                gotValidEmuServerHost = 1;
                printf("emu connect: SUCCESS connecting to %s\n", emuServerHost);
                break;
            }
printf("emu connect: failed to connect to %s on port %hu\n", emuServerHost, domain->sendPort);
        }

        if (!gotValidEmuServerHost) {
            if (hashEntries != NULL) free(hashEntries);
            cMsgRestoreSignals(domain);
            cMsgDomainFree(domain);
            free(domain);
            return(err);
        }
    }

    /* Even though we free hash entries array, emuServerHost
     * is still pointing to valid string inside hashtable. */
    if (hashEntries != NULL) free(hashEntries);

    /* Clear & free memory in table */
    hashClear(&domain->rcIpAddrTable, &hashEntries, &hashEntryCount);
    if (hashEntries != NULL) {
        for (i=0; i < hashEntryCount; i++) {
            free(hashEntries[i].key);
        }
        free(hashEntries);
     }


    /* Talk to the server */
    /* magic #s are already set */
    /* cMsg major version */
    outGoing[3] = htonl(CMSG_VERSION_MAJOR);
    /* my coda Id */
    outGoing[4] = htonl((uint32_t)myCodaId);
    /* max size data buffer in bytes in one cMsg message */
    outGoing[5] = htonl((uint32_t)dataBufSize);

    /* send data over TCP socket */
    len = cMsgNetTcpWrite(domain->sendSocket, (void *) outGoing, 6*sizeof(int32_t));
    if (len !=  6*sizeof(int32_t)) {
        printf("emu connect: write to server failure\n");
        return(CMSG_NETWORK_ERROR);
    }

    /* return id */
    *domainId = (void *) domain;
        
    /* install default shutdown handler (exits program) */
    cmsg_emu_setShutdownHandler((void *)domain, defaultShutdownHandler, NULL);

    domain->gotConnection = 1;
    
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine reconnects the client to the emu server.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */
int cmsg_emu_reconnect(void *domainId) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/

/**
 * This routine starts a thread to receive a return UDP packet from
 * the server due to our initial multicast.
 */
static void *receiverThd(void *arg) {

    thdArg *threadArg = (thdArg *) arg;
    int32_t ints[5], length;
    int     i, magic[3], addressCount, port, minMsgSize;
    char    buf[1024], *pbuf, *tmp;
    ssize_t len;
    cMsgDomainInfo *domain = threadArg->domain;

    minMsgSize = 5*sizeof(int32_t);
    
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
         *         -> TCP port server is listening on
         *         -> number of IP addresses to follow
         *         -> in a loop:
         *           -> len of server's IP address string
         *           -> server's IP address in dotted-decimal format
         *           -> len of server's broadcast address string
         *           -> server's broadcast address in dotted-decimal format
         */
        if (len < minMsgSize) {
          /*printf("receiverThd: got packet that's too small\n");*/
          continue;
        }
        
        pbuf = buf;
        memcpy(ints, pbuf, sizeof(ints));
        pbuf += sizeof(ints);
/*
printf("receiverThd: packet from host %s on port %hu\n",
                inet_ntoa(threadArg->addr.sin_addr), ntohs(threadArg->addr.sin_port));
*/
        magic[0] = ntohl((uint32_t)ints[0]);
        magic[1] = ntohl((uint32_t)ints[1]);
        magic[2] = ntohl((uint32_t)ints[2]);
        if (magic[0] != CMSG_MAGIC_INT1 ||
            magic[1] != CMSG_MAGIC_INT2 ||
            magic[2] != CMSG_MAGIC_INT3)  {
          printf("receiverThd: received bad magic # response to multicast\n");
          continue;
        }

        port = ntohl((uint32_t)ints[3]);
        if (port != threadArg->port) {
          printf("receiverThd: received bad port response to multicast (%d)\n", port);
          continue;
        }
        domain->sendPort = port;

        addressCount = ntohl((uint32_t)ints[4]);
        if (addressCount < 1) {
          printf("receiverThd: received bad IP addresse count (%d)\n", addressCount);
          continue;
        }

        /* clear table containing all ip addresses of server */
        hashClear(&domain->rcIpAddrTable, NULL, NULL);

        for (i=0; i < addressCount; i++) {
            
            /* get IP address and store in hash table */
            
            /* # of chars in string stored in 32 bit int */
            length = *((int32_t *) pbuf);
            length = ntohl((uint32_t)length);
            pbuf += sizeof(int32_t);

            if (length > 0) {
                if ( (tmp = (char *) malloc(length + 1)) == NULL) {
                    pthread_exit(NULL);
                    return NULL;
                }
                memcpy(tmp, pbuf, length);
                tmp[length] = 0;
                pbuf += length;
/*printf("receiverThd: emu server ip addr = %s\n", tmp);*/
                hashInsert(&domain->rcIpAddrTable, tmp, NULL, NULL);
                free(tmp);
            }
            
            /* get broadcast address and store in hash table */
            
            length = *((int32_t *) pbuf);
            length = ntohl((uint32_t)length);
            pbuf += sizeof(int32_t);
            
            if (length > 0) {
                if ( (tmp = (char *) malloc(length + 1)) == NULL) {
                    pthread_exit(NULL);
                    return NULL;
                }
                memcpy(tmp, pbuf, length);
                tmp[length] = 0;
                pbuf += length;
/*printf("receiverThd: emu server broadcast addr = %s\n", tmp);*/
                hashInsert(&domain->rcIpAddrTable, tmp, NULL, NULL);
                free(tmp);
            }
            
            
            
            
        }              
                        
        /* Tell main thread we are done. */
        pthread_cond_signal(&cond);
        break;
    }
    
    pthread_exit(NULL);
    return NULL;
}


/*-------------------------------------------------------------------*/


/** Structure for freeing memory in cleanUpHandler. */
typedef struct freeMem_t {
    char **ifNames;
    int nameCount;
} freeMem;


/*-------------------------------------------------------------------*
 * multicastThd needs a pthread cancellation cleanup handler.
 * This handler will be called when the multicastThd is
 * cancelled. It's task is to free memory.
 *-------------------------------------------------------------------*/
static void cleanUpHandler(void *arg) {
  int i;  
  freeMem *pMem = (freeMem *)arg;
    
  /* release memory */
  if (pMem == NULL) return;
  if (pMem->ifNames != NULL) {
      for (i=0; i < pMem->nameCount; i++) {
          free(pMem->ifNames[i]);
      }
      free(pMem->ifNames);
  }

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
    pthread_cleanup_push(cleanUpHandler, (void *)pfreeMem);

    while (1) {
        int sleepCount = 0;

        if (useDefaultIf) {
            sendto(threadArg->sockfd, (void *)threadArg->buffer, threadArg->bufferLen, 0,
                   (SA *) threadArg->paddr, threadArg->len);
        }
        else {
            for (i=0; i < count; i++) {
                if (cMsgDebug >= CMSG_DEBUG_INFO) {
                    printf("multicastThd: send mcast on interface %s\n", ifNames[i]);
                }

                /* set socket to send over this interface */
                err = cMsgNetMcastSetIf(threadArg->sockfd, ifNames[i], 0);
                if (err != CMSG_OK) continue;
    
/*printf("Send multicast: to emu Multicast server on %s\n", ifNames[i]);*/
                sendto(threadArg->sockfd, (void *)threadArg->buffer, threadArg->bufferLen, 0,
                       (SA *) threadArg->paddr, threadArg->len);

                /* Wait 0.2 second between multicasting on each interface */
                nanosleep(&delay, NULL);
                sleepCount++;
            }
        }
      
        if (sleepCount < 1) sleep(1);
    }

    /* on some operating systems (Linux) this call is necessary - calls cleanup handler */
    pthread_cleanup_pop(1);
  
    pthread_exit(NULL);
    return NULL;
}


/*-------------------------------------------------------------------*/


/**
 * This routine sends a msg to the specified emu server. The userInt is sent
 * and must contain the type of message in the least significant byte and
 * the eventType in the next byte. It also sends the byte array. It is called
 * by the user through cMsgSend() given the appropriate UDL.
 * It will only block if the socket buffer has reached its limit.
 * In this domain cMsgFlush() does nothing
 * and does not need to be called for the message to be sent immediately.
 * Also, in this domain, this routine is NOT threadsafe which is done for speed.
 * It may NOT be simultaneously called with other sends.<p>
 *
 * This routine only sends the following messge fields:
 * <ol>
 *   <li>user int</li>
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
int cmsg_emu_send(void *domainId, void *vmsg) {
  
  cMsgMessage_t *msg = (cMsgMessage_t *) vmsg;
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  int lenByteArray, err=CMSG_OK, fd;
  int32_t outGoing[2];
  ssize_t sendLen;

  
  if (domain == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
      
  /* check args */
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  
  fd = domain->sendSocket;

  /* Type of message is in 1st (lowest) byte, source (Emu's EventType) of message is in 2nd byte */
  outGoing[0] = htonl((uint32_t)msg->userInt);

  /* Length of byte array (not including this int) */
  lenByteArray = msg->byteArrayLength;
  outGoing[1] = htonl((uint32_t)lenByteArray);
 
  if (domain->gotConnection != 1) {
    return(CMSG_LOST_CONNECTION);
  }

  /* Cannot run this while connecting/disconnecting */
  cMsgConnectReadLock(domain);

  if (domain->gotConnection != 1) {
    cMsgConnectReadUnlock(domain);
    return(CMSG_LOST_CONNECTION);
  }

/*printf("cmsg_emu_send: TCP, cmd = %d, len = %d\n", msg->userInt, lenByteArray);*/
  /* send integer data */
  sendLen = cMsgNetTcpWrite(fd, (void *) outGoing, sizeof(outGoing));
  if (sendLen != sizeof(outGoing)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_emu_send: write failure\n");
    }
    err = CMSG_NETWORK_ERROR;
  }

  /* send binary data */
  sendLen = cMsgNetTcpWrite(fd, (void *) msg->byteArray, lenByteArray);
  if (sendLen != lenByteArray) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_emu_send: write failure\n");
    }
    err = CMSG_NETWORK_ERROR;
  }

  /* done protecting communications */
  cMsgConnectReadUnlock(domain);
 
  return(err);
}


/*-------------------------------------------------------------------*/


/** syncSend is not implemented in the emu domain. */
int cmsg_emu_syncSend(void *domainId, void *vmsg, const struct timespec *timeout, int *response) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/** subscribeAndGet is not implemented in the emu domain. */
int cmsg_emu_subscribeAndGet(void *domainId, const char *subject, const char *type,
                                 const struct timespec *timeout, void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * sendAndGet is not implemented in the emu domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */
int cmsg_emu_sendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout,
                            void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/** flush does nothing in the emu domain. */
int cmsg_emu_flush(void *domainId, const struct timespec *timeout) {
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * subscribe is not implemented in the emu domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */   
int cmsg_emu_subscribe(void *domainId, const char *subject, const char *type,
                      cMsgCallbackFunc *callback, void *userArg,
                      cMsgSubscribeConfig *config, void **handle) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * unsubscribe is not implemented in the emu domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */   
int cmsg_emu_unsubscribe(void *domainId, void *handle) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * subscriptionPause is not implemented in the emu domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */ 
int cmsg_emu_subscriptionPause(void *domainId, void *handle) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * subscriptionResume is not implemented in the emu domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */ 
int cmsg_emu_subscriptionResume(void *domainId, void *handle) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * subscriptionQueueCount is not implemented in the emu domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */ 
int cmsg_emu_subscriptionQueueCount(void *domainId, void *handle, int *count) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * subscriptionQueueIsFull is not implemented in the emu domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */ 
int cmsg_emu_subscriptionQueueIsFull(void *domainId, void *handle, int *full) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * subscriptionQueueClear is not implemented in the emu domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */
int cmsg_emu_subscriptionQueueClear(void *domainId, void *handle) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * subscriptionMessagesTotal is not implemented in the emu domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */
int cmsg_emu_subscriptionMessagesTotal(void *domainId, void *handle, int *total) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * monitor is not implemented in the emu domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */  
int cmsg_emu_monitor(void *domainId, const char *command, void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * start is not implemented in the emu domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */   
int cmsg_emu_start(void *domainId) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * stop is not implemented in the emu domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */   
int cmsg_emu_stop(void *domainId) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * This routine disconnects the client from the emu server.
 *
 * @param domainId pointer to id of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if domainId or the pointer it points to is NULL
 */   
int cmsg_emu_disconnect(void **domainId) {

    cMsgDomainInfo *domain;

    if (domainId == NULL) return(CMSG_BAD_ARGUMENT);
    domain = (cMsgDomainInfo *) (*domainId);
    if (domain == NULL) return(CMSG_BAD_ARGUMENT);
    
    /* When changing initComplete / connection status, protect it */
    cMsgConnectWriteLock(domain);

    domain->gotConnection = 0;

    /* close TCP sending socket */
    close(domain->sendSocket);

    /* stop listening and client communication threads */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cmsg_emu_disconnect: cancel listening & client threads\n");
    }
   
    /* Unblock SIGPIPE */
    cMsgRestoreSignals(domain);
    
    cMsgConnectWriteUnlock(domain);

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
int cmsg_emu_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler,
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


/**
 * shutdownClients is not implemented in the emu domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */
int cmsg_emu_shutdownClients(void *domainId, const char *client, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * shutdownServers is not implemented in the emu domain.
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */
int cmsg_emu_shutdownServers(void *domainId, const char *server, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * This routine parses, using regular expressions, the emu domain
 * portion of the UDL sent from the next level up" in the API.
 *
 * Emu domain UDL is of the form:<p>
 *   <b>cMsg:emu://&lt;port&gt;/&lt;expid&gt;/&lt;compName&gt;?codaId=&lt;id&gt;&timeout=&lt;sec&gt;&bufSize=&lt;size&gt;&tcpSend=&lt;size&gt;&subnet=&lt;subnet&gt;&noDelay</b><p>
 *
 * Remember that for this domain:
 *<ol>
 *<li>multicast address is always 239.230.0.0<p>
 *<li>port (of emu domain server) is required<p>
 *<li>expid is required<p>
 *<li>compName is required - destination CODA component name<p>
 *<li>codaId (coda id of data sender) is required<p>
 *<li>optional timeout (sec) to connect to emu server, default = 0 (wait forever)<p>
 *<li>optional bufSize (max size in bytes of a single send), min = 1KB, default = 2.1MB<p>
 *<li>optional tcpSend is the TCP send buffer size in bytes, min = 1KB<p>
 *<li>optional subnet is the preferred subnet used to connect to server<p>
 *<li>optional noDelay is the TCP no-delay parameter turned on<p>
 *</ol><p>
 *
 * @param UDLR       partial UDL to be parsed
 * @param port       pointer filled in with port
 * @param expid      pointer filled in with expid, allocates mem and must be freed by caller
 * @param compName   pointer filled in with component name, allocates mem and must be freed by caller
 * @param codaId     pointer filled in with codaId
 * @param timeout    pointer filled in with timeout if it exists
 * @param bufSize    pointer filled in with bufSize if it exists
 * @param tcpSend    pointer filled in with tcpSend if it exists
 * @param noDelay    pointer filled in with 1 if noDelay exists, else 0
 * @param subnet     pointer filled in with preferred subnet, allocates mem and must be freed by caller
 * @param remainder  pointer filled in with the part of the udl after host:port/expid if it exists,
 *                   else it is set to NULL; allocates mem and must be freed by caller if not NULL
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_FORMAT if UDLR arg is not in the proper format
 * @returns CMSG_BAD_ARGUMENT if UDLR arg is NULL
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 * @returns CMSG_OUT_OF_RANGE if port is an improper value
 */
static int parseUDL(const char *UDLR,
                    unsigned short *port,
                    char **expid,
                    char **compName,
                    int  *codaId,
                    int  *timeout,
                    int  *bufSize,
                    int  *tcpSend,
                    int  *noDelay,
                    char **subnet,
                    char **remainder) {

    int        err, Port, index;
    size_t     len, bufLength;
    char       *udlRemainder, *val, *myExpid = NULL, *myComponent=NULL;
    char       *buffer;
    const char *pattern = "([0-9]+)/([^/]+)/([^?&]+)(.*)";
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
    	cMsgRegfree(&compiled);
        return (CMSG_BAD_FORMAT);
    }
    
    /* free up memory */
    cMsgRegfree(&compiled);
    
	/*************/        
    /* find port */
	/*************/        
    index = 1;
    if (matches[index].rm_so < 0) {
        /* no match for port & NO DEFAULT as all EBs,ERs must be different from each other */
printf("parseUDL: port required in UDL\n");
        free(udlRemainder);
        free(buffer);
        return (CMSG_BAD_FORMAT);
    }
    else {
        buffer[0] = 0;
        len = matches[index].rm_eo - matches[index].rm_so;
        strncat(buffer, udlRemainder+matches[index].rm_so, len);        
        Port = atoi(buffer);        
    }

    if (Port < 1024 || Port > 65535) {
      free(udlRemainder);
      free(buffer);
      return (CMSG_OUT_OF_RANGE);
    }
               
    if (port != NULL) {
      *port = (unsigned short)Port;
    }
/*printf("parseUDL: port = %hu\n", Port);*/

    /**************/
    /* find expid */
    /**************/
    index++;
    buffer[0] = 0;
    if (matches[index].rm_so < 0) {
        free(udlRemainder);
        free(buffer);
        printf("parseUDL: expid required in UDL\n");
        return (CMSG_BAD_FORMAT);
    }
    else {
        len = matches[index].rm_eo - matches[index].rm_so;
        strncat(buffer, udlRemainder+matches[index].rm_so, len);
        myExpid = strdup(buffer);

        if (expid != NULL) {
            *expid = myExpid;
        }
    }
/*printf("parseUDL: expid = %s\n", myExpid);*/

    /***********************/
    /* find component name */
    /***********************/
    index++;
    buffer[0] = 0;
    if (matches[index].rm_so < 0) {
        free(udlRemainder);
        free(buffer);
        free(myExpid);
        printf("parseUDL: destination component name required in UDL\n");
        return (CMSG_BAD_FORMAT);
    }
    else {
        len = matches[index].rm_eo - matches[index].rm_so;
        strncat(buffer, udlRemainder+matches[index].rm_so, len);
        myComponent = strdup(buffer);

        if (compName != NULL) {
            *compName = myComponent;
        }
    }
/*printf("parseUDL: component name = %s\n", myComponent);*/

    /******************/
    /* find remainder */
	/******************/        
    index++;
    buffer[0] = 0;
    if (matches[index].rm_so < 0) {
        free(udlRemainder);
        free(buffer);
        free(myExpid);
        free(myComponent);
printf("parseUDL: codaId required in UDL\n");
        return (CMSG_BAD_FORMAT);
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
    /* 9 chars min => ?codaId=1 */
    if (len < 9) { 
        if (remainder != NULL) free(remainder);
        free(udlRemainder);
    	free(buffer);
        free(myExpid);
        free(myComponent);
        cMsgRegfree(&compiled);
printf("parseUDL: \"codaId\" must be set in UDL\n");
       return(CMSG_BAD_FORMAT);
    }

	/**********************************/        
    /* find codaId=value (required) */
	/**********************************/        
    val = strdup(buffer);
    pattern = "[&?]codaId=([0-9]+)";

    /* compile regular expression */
    cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);

    /* find matches */
    err = cMsgRegexec(&compiled, val, 2, matches, 0);
        
    /* if there's a match ... */
    if (err == 0) {
        /* find id */
        if (matches[1].rm_so >= 0) {
            int t;
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            strncat(buffer, val+matches[1].rm_so, len);
            t = atoi(buffer);
            if (codaId != NULL) {
                *codaId = t;
            }        
/*printf("parseUDL: codaId = %d\n", t);*/
        }
        else {
            if (remainder != NULL) free(remainder);
    	    free(udlRemainder);
    	    free(buffer);
            free(myExpid);
            free(myComponent);
		    free(val);
            cMsgRegfree(&compiled);
printf("parseUDL: UDL needs to specify \"codaId\"\n");
    	    return(CMSG_BAD_FORMAT);
        }
	}

    cMsgRegfree(&compiled);    

     
	/******************************/        
    /* now look for timeout=value */
 	/******************************/        
    pattern = "[?&]timeout=([0-9]+)";

    cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
    err = cMsgRegexec(&compiled, val, 2, matches, 0);
    if (err == 0) {
        /* find bufSize (in bytes) */
        if (matches[1].rm_so >= 0) {
           int t;
           buffer[0] = 0;
           len = matches[1].rm_eo - matches[1].rm_so;
           strncat(buffer, val+matches[1].rm_so, len);
           t = atoi(buffer);
           if (timeout != NULL) {
             *timeout = t;
           }        
/*printf("parseUDL: timeout (sec) = %d\n", t);*/
        }
        else if (timeout != NULL) {
            /* default to infinite wait */
            *timeout = 0;
        }
    }
    else if (timeout != NULL) {
        *timeout = 0;
    }

    /* free up memory */
    cMsgRegfree(&compiled);
    
    
	/******************************/        
    /* now look for bufSize=value */
 	/******************************/        
    pattern = "[?&]bufSize=([0-9]+)";

    cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
    err = cMsgRegexec(&compiled, val, 2, matches, 0);
    if (err == 0) {
        /* find bufSize (in bytes) */
        if (matches[1].rm_so >= 0) {
           int t;
           buffer[0] = 0;
           len = matches[1].rm_eo - matches[1].rm_so;
           strncat(buffer, val+matches[1].rm_so, len);
           t = atoi(buffer);
           if (t < 1024) t=1024;
           if (bufSize != NULL) {
             *bufSize = t;
           }        
/*printf("parseUDL: bufSize (bytes) = %d\n", t);*/
        }
        else if (bufSize != NULL) {
            /* default to 2.1MB */
            *bufSize = 2100000;
        }
    }
    else if (bufSize != NULL) {
        *bufSize = 2100000;
    }

    /* free up memory */
    cMsgRegfree(&compiled);
        

	/******************************/        
    /* now look for tcpSend=value */
 	/******************************/        
    pattern = "[?&]tcpSend=([0-9]+)";

    cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
    err = cMsgRegexec(&compiled, val, 2, matches, 0);
    if (err == 0) {
        /* find tcpSend buffer (in bytes) */
        if (matches[1].rm_so >= 0) {
           int t;
           buffer[0] = 0;
           len = matches[1].rm_eo - matches[1].rm_so;
           strncat(buffer, val+matches[1].rm_so, len);
           t = atoi(buffer);
           if (t < 1024) t=1024;
           if (tcpSend != NULL) {
             *tcpSend = t;
           }        
/*printf("parseUDL: tcpSend buffer (bytes) = %d\n", t);*/
        }
    }

    /* free up memory */
    cMsgRegfree(&compiled);
     
    /*****************************/        
    /* now look for subnet=value */
    /*****************************/
    pattern = "[?&]subnet=((?:[0-9]{1,3}.){3}[0-9]{1,3})";
    
    cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
    err = cMsgRegexec(&compiled, val, 2, matches, 0);
    if (err == 0) {
        /* find subnet */
        if (matches[1].rm_so >= 0) {
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            strncat(buffer, val+matches[1].rm_so, len);
            if (subnet != NULL) {
                *subnet = (char *) strdup(buffer);
            }        
printf("parseUDL: preferred subnet = %s\n", buffer);
        }
    }
    
    /* free up memory */
    cMsgRegfree(&compiled);
        
    /************************/        
    /* now look for noDelay */
 	/************************/        
    pattern = "[?&]noDelay";

    cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
    err = cMsgRegexec(&compiled, val, 1, matches, 0);
    if (err == 0) {
       if (noDelay != NULL) {
           *noDelay = 1;
       }
/*printf("parseUDL: noDelay = on\n");*/
    }
    else if (noDelay != NULL) {
       *noDelay = 0;
    }        

    /* free up memory */
    cMsgRegfree(&compiled);
        

    /* UDL parsed ok */
    free(val);
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

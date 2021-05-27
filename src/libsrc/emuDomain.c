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
 *----------------------------------------------------------------------------*/

/**
 * @file
 * This file contains the emu domain implementation of the cMsg user API.
 * This a messaging system programmed by the Data Acquisition Group at Jefferson
 * Lab. The emu domain allows CODA components between themselves - for
 * communication between Rocs & Ebs and between Ebs and other Ebs. The idea is to
 * avoid using an ET system to transfer data and use simple sockets.
 */  
 

#include <strings.h>
#include <sys/time.h>    /* struct timeval */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>

#include "cMsgPrivate.h"
#include "cMsgNetwork.h"
#include "cMsgRegex.h"
#include "cMsgDomain.h"
#include "emuDomain.h"



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
    char *buffer;
    cMsgDomainInfo *domain;
    codaIpList *ipList;
} thdArg;

/* built-in limits */
/** Number of seconds to wait for emuClientListeningThread threads to start. */
#define WAIT_FOR_THREADS 10

/* local variables */

/** Size of buffer in bytes for sending messages. */
static int initialMsgBufferSize = 1500;

/* Direct connect stuff. */
static int haveDestinationInfo = 0;
static codaIpList *directIpList = NULL;

/* Local prototypes */
static void  staticMutexLock(void);
static void  staticMutexUnlock(void);
static void *receiverThd(void *arg);
static void *multicastThd(void *arg);
static int   udpSend(cMsgDomainInfo *domain, cMsgMessage_t *msg);
static void  defaultShutdownHandler(void *userArg);
static int parseUDL(const char *UDLR,
                    char *serverIP,
                    unsigned short *port,
                    char **expid,
                    char **compName,
                    int  *multicasting,
                    int  *codaId,
                    int  *timeout,
                    int  *bufSize,
                    int  *tcpSend,
                    int  *noDelay,
                    int  *sockets,
                    char **subnet,
                    char **remainder);
static int directConnect(cMsgDomainInfo *domain, char *serverIP, char *subnet,
                         unsigned short serverPort, int tcpSendBufSize,
                         int tcpNoDelay, int socketCount, int codaId,
                         int dataBufSiz);


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
             
  /*cMsgConnectReadLock(domain);*/

  if (connected != NULL) {
    *connected = domain->gotConnection;
  }
  
  /*cMsgConnectReadUnlock(domain);*/
    
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
    int     err, status, len, i, localPort, socketCount=1;
    size_t  expidLen, nameLen;
    int32_t outGoing[8];
    int     myCodaId, multicasting, multicastTO, dataBufSize, tcpSendBufSize=0, tcpNoDelay=0, off=0, noPrefSubnetMatch=0;
    char    temp[CMSG_MAXHOSTNAMELEN], serverIP[CODA_IPADDRSTRLEN];
    unsigned char ttl = 3;
    cMsgDomainInfo *domain;
    codaIpList *ipList, *orderedIpList;
    codaIpAddr *ipAddrs = NULL;

    pthread_t rThread, bThread;
    thdArg    rArg,    bArg;

    struct timeval tv;
    struct timespec wait;
    struct sockaddr_in servaddr, localaddr;

    /* clear array */
    memset((void *)buffer, 0, 1024);

    /* parse the UDLRemainder to get the server IP, port, expid, component name, if mulitcasting, codaId,
       timeout, dataBufSize, TCP sendBufSize, TCP noDelay, and preferred subnet */
    err = parseUDL(UDLremainder, serverIP, &serverPort, &expid, &componentName, &multicasting, &myCodaId, &multicastTO,
                   &dataBufSize, &tcpSendBufSize, &tcpNoDelay, &socketCount, &subnet, NULL);
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
        free(componentName);
        if (subnet != NULL) free(subnet);
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
        free(componentName);
        if (subnet != NULL) free(subnet);
        return(CMSG_OUT_OF_MEMORY);
    }

    /* store our host's name */
    gethostname(temp, CMSG_MAXHOSTNAMELEN);
    domain->myHost = strdup(temp);

    /* store names, can be changed until server connection established */
    domain->name            = strdup(myName);
    domain->udl             = strdup(myUDL);
    domain->description     = strdup(myDescription);

    /* store # of sockets to make */
    domain->sendSocketCount = socketCount;

    if (!multicasting) {
        /* If server IP explicitly given in UDL or IP info from run control was stored by
         * calling setDirectConnectDestination, use that to connect directly. */
        if (strlen(serverIP) > 0 || haveDestinationInfo) {
            err = directConnect(domain, serverIP, subnet, serverPort, tcpSendBufSize,
                                tcpNoDelay, socketCount, myCodaId, dataBufSize);
            /* return id */
            *domainId = (void *) domain;
            if (err != CMSG_OK) {
                printf("emu connect: Making a direct connection failed, err = %d\n", err);
            }
            else {
                printf("emu connect: Making a direct connection is successful\n");
            }
            return err;
        }

printf("emu connect: Making a direct connection failed, try multicasting\n");
        /* We run into a contradiction here. The user wants to make a direct connection,
              * but setDirectConnectDestination has not yet been called with the proper info
              * to make it happen. So we default to multicasting...
              */
        multicasting = 1;
        strcpy(serverIP, EMU_MULTICAST_ADDR);
    }

    /*-------------------------------------------------------
     * Talk to emu multicast server
     *-------------------------------------------------------*/
    
    /* create UDP socket */
    domain->sendSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (domain->sendSocket < 0) {
        cMsgDomainFree(domain);
        free(domain);
        free(expid);
        free(componentName);
        if (subnet != NULL) free(subnet);
        return(CMSG_SOCKET_ERROR);
    }

    /* Give each local socket a unique port on a single host.
     * SO_REUSEPORT not defined in Redhat 5. I think off is default anyway. */
#ifdef SO_REUSEPORT
    setsockopt(domain->sendSocket, SOL_SOCKET, SO_REUSEPORT, (void *)(&off), sizeof(int));
#endif

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

    /* Set TTL to 3 so it will make it through 3 routers but no further. */
    err = setsockopt(domain->sendSocket, IPPROTO_IP, IP_MULTICAST_TTL, (void *) &ttl, sizeof(ttl));
    if (err < 0) {
        close(domain->sendSocket);
        cMsgDomainFree(domain);
        free(domain);
        free(expid);
        free(componentName);
        if (subnet != NULL) free(subnet);
        return(CMSG_SOCKET_ERROR);
    }

    /* Change the multicast address (either default or given in UDL) into binary form */
    if ( (err = cMsgNetStringToNumericIPaddr(serverIP, &servaddr)) != CMSG_OK ) {
        close(domain->sendSocket);
        cMsgDomainFree(domain);
        free(domain);
        free(expid);
        free(componentName);
        if (subnet != NULL) free(subnet);
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

/*printf("emu connect: multicast info (expid = %s) to server on port = %hu (%s)\n",
        expid, serverPort, EMU_MULTICAST_ADDR);*/
    
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
    memcpy(buffer, (void *)outGoing, 7*sizeof(int32_t));
    len = 7*sizeof(int32_t);
    memcpy(buffer+len, (const void *)componentName, nameLen);
    len += nameLen;
    memcpy(buffer+len, (const void *)expid, expidLen);
    len += expidLen;
    
    /* create and start a thread which will receive any responses to our multicast */
    memset((void *)&rArg.addr, 0, sizeof(rArg.addr));
    rArg.len             = (socklen_t) sizeof(rArg.addr);
    rArg.port            = serverPort;
    rArg.sockfd          = domain->sendSocket;
    rArg.domain          = domain;
    rArg.addr.sin_family = AF_INET;
    rArg.ipList          = NULL;
    
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

    free(expid);
    free(componentName);

    /* Wait for a response. If multicastTO is given in the UDL, use that.
      * The default wait or the wait if multicastTO is set to 0, is forever.
      * Round things to the nearest second since we're only multicasting a
      * message every second anyway.
      */
    if (multicastTO > 0) {
        wait.tv_sec  = multicastTO;
        wait.tv_nsec = 0;

        status = cMsgLatchAwait(&domain->syncLatch, &wait);
    }
    else {
        status = cMsgLatchAwait(&domain->syncLatch, NULL);
    }

    if (status < 1 || rArg.ipList == NULL) {
        printf("emu connect: wait timeout\n");
        close(domain->sendSocket);
        pthread_cancel(bThread);
        cMsgDomainFree(domain);
        free(domain);
        if (subnet != NULL) free(subnet);
        return(CMSG_TIMEOUT);
    }

    ipList = rArg.ipList;
        
    /* stop multicasting thread */
    pthread_cancel(bThread);

/*printf("emu connect: got a response from mcast server\n");*/
    close(domain->sendSocket);

    /*-------------------------------*/
    /* connect & talk to emu server  */
    /*-------------------------------*/

    /* Get local network info */
    cMsgNetGetNetworkInfo(&ipAddrs, NULL);

    /* Order the server IP list according to the given preferred subnet, if any */
    orderedIpList = cMsgNetOrderIpAddrs(ipList, ipAddrs, subnet, &noPrefSubnetMatch);
    if (noPrefSubnetMatch) {
        printf("emu connect: preferred subnet = %s, but no local interface on that subnet\n", subnet);
    }

    if (subnet != NULL) free(subnet);
    cMsgNetFreeIpAddrs(ipAddrs);
    codanetFreeAddrList(ipList);

    /* malloc memory needed to store all socket fds */
    domain->sendSockets = (int *) malloc(socketCount * sizeof(int));
    if (domain->sendSockets == NULL) {
        cMsgDomainFree(domain);
        free(domain);
        codanetFreeAddrList(orderedIpList);
        return(CMSG_OUT_OF_MEMORY);
    }

    /* Try all IP addresses in list until one works */
    ipList = orderedIpList;
    nextIp:
    while (orderedIpList != NULL) {

        err = CMSG_OK;

        /* For each socket, make a connection */
        for (i=0; i < socketCount; i++) {

            /* First connect to server host & port */
            printf("emu connect: try connecting to ip = %s, port = %d ...\n",
                   orderedIpList->addr, serverPort);

            err = cMsgNetTcpConnectTimeout(orderedIpList->addr, serverPort,
                                           tcpSendBufSize, 0, tcpNoDelay,
                                           &tv, &domain->sendSockets[i], NULL);

            if (err != CMSG_OK) {
                /* If there is another address to try, try it */
                if (orderedIpList->next != NULL) {
                    orderedIpList = orderedIpList->next;
                    //continue;
                    goto nextIp;
                }
                /* if we've tried all addresses in the list, return an error */
                else {
                    cMsgDomainFree(domain);
                    free(domain);
                    codanetFreeAddrList(ipList);
                    return (err);
                }
            }
        }

        printf("             Connected\n");

        /* Quit loop if we've made a connection to server, store good address */
        if (domain->serverHost != NULL) {
            free(domain->serverHost);
        }
        domain->serverHost = strdup(orderedIpList->addr);
        break;
    }

    codanetFreeAddrList(ipList);

    /* Talk to the server */
    /* magic #s are already set */
    /* cMsg major version */
    outGoing[3] = htonl(CMSG_VERSION_MAJOR);
    /* my coda Id */
    outGoing[4] = htonl((uint32_t)myCodaId);
    /* max size data buffer in bytes in one cMsg message */
    outGoing[5] = htonl((uint32_t)dataBufSize);
    /* total number of sockets that will be connecting */
    outGoing[6] = htonl((uint32_t)socketCount);

    for (i=0; i <socketCount; i++) {
        /* place of this socket relative to others: 1, 2, ... */
        outGoing[7] = htonl((uint32_t) (i+1));

        /* send data over TCP socket */
        len = cMsgNetTcpWrite(domain->sendSockets[i], (void *) outGoing, 8*sizeof(int32_t));
        if (len != 8*sizeof(int32_t)) {
            cMsgDomainFree(domain);
            free(domain);
            printf("emu connect: write to server failure\n");
            return (CMSG_NETWORK_ERROR);
        }
    }

    /* return id */
    *domainId = (void *) domain;
        
    /* install default shutdown handler (exits program) */
    cmsg_emu_setShutdownHandler((void *)domain, defaultShutdownHandler, NULL);

    domain->gotConnection = 1;
    
    return(CMSG_OK);
}


/**
 * This routine stores the lists of an emu TCP server's host IP and broadcast addresses.
 * For use when making a direct connection. This data is stored in a static location.
 * This should not present a problem as this code is only used on the ROC so no chance
 * multiple ROCs using the same library.<p>
 *
 * Setting both length args to zero, erases all destination info.
 *
 * @param ip     array of pointers to ip addresses in dot-decimal form.
 * @param broad  array of pointers to broadcast addresses in dot-decimal form
 *               corresponding to ip addresses.
 * @param count  number of array entries.
 */
void setDirectConnectDestination(const char **ip, const char **broad, int count) {
    codaIpList *ipList = NULL;

    /* Clear memory */
    cMsgNetFreeAddrList(directIpList);
    haveDestinationInfo = 0;
    directIpList = NULL;

    /* Store the addresses in a linked list of structures */
    ipList = cMsgNetAddToAddrList(NULL, ip, broad, count);
    if (ipList == NULL) {
        return;
    }
    directIpList = ipList;
    haveDestinationInfo = 1;
}


/**
 * This routine is called if connecting directly to an emu domain TCP server.
 *
 * @param domain          pointer to emu domain info.
 * @param serverIP        if the server's IP address is given explicitly in UDL for a direct connection,
 *                        this is it
 * @param subnet          the subnet corresponding to the serverIP address
 * @param serverPort      the TCP server port to connect to
 * @param tcpSendBufSize  the TCP send buffer size in bytes (0 = OS default).
 * @param tcpNoDelay      if true, turn on TCP no delay
 * @param socketCount     number of socket connections to TCP server
 * @param codaId          coda id of this component
 * @param dataBufSize     max size in bytes of a single send
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
static int directConnect(cMsgDomainInfo *domain, char *serverIP, char *subnet,
                         unsigned short serverPort, int tcpSendBufSize,
                         int tcpNoDelay, int socketCount, int codaId,
                         int dataBufSize) {

    int     err, len, i, noPrefSubnetMatch=0;
    int32_t outGoing[8];
    struct timeval tv;
    char *outgoingIp = NULL;
    codaIpList *ipList = NULL, *orderedIpList;
    codaIpAddr *ipAddrs = NULL;

    /* 20 sec timeout to connect to server */
    tv.tv_sec = 20;
    tv.tv_usec = 0;


    /* Malloc memory needed to store all socket fds */
    domain->sendSockets = (int *) malloc(socketCount * sizeof(int));
    if (domain->sendSockets == NULL) {
        cMsgDomainFree(domain);
        free(domain);
        return(CMSG_OUT_OF_MEMORY);
    }

    /*------------------------*/
    /* connect to emu server  */
    /*------------------------*/

    /* If the emu server's IP address was explicitly set in the UDL, use that */
    if (strlen(serverIP) > 0) {
        /* Do we force the communication to go over a specific network interface?
         * If the preferred subnet is the same at the current IP's subnet, yes. */
        if (subnet != NULL) {
            /* won't return error */
            if (outgoingIp != NULL) free(outgoingIp);
            cMsgNetGetMatchingLocalIpAddress(subnet, &outgoingIp);
        }

        /* First connect to server host & port */
/*printf("emu connect: try connecting %d socket(s) directly to ip = %s, port = %d thru local ip = %s ...\n",
               socketCount, serverIP, serverPort, outgoingIp); */

        /* For each socket, make a connection */
        for (i = 0; i < socketCount; i++) {
            err = cMsgNetTcpConnectTimeout2(serverIP, outgoingIp, serverPort,
                                            tcpSendBufSize, 0, tcpNoDelay,
                                            &tv, &domain->sendSockets[i], NULL);

            if (err != CMSG_OK) {
                cMsgDomainFree(domain);
                free(domain);
                if (outgoingIp != NULL) free(outgoingIp);
                return (err);
            }
        }

        printf("          Connected\n");

        /* Store good address */
        if (domain->serverHost != NULL) {
            free(domain->serverHost);
        }
        domain->serverHost = strdup(serverIP);
    }

    /* If the emu server's IP address was specified in the UDL as "direct" use list RC sent */
    else {
        /* Get local network info */
        cMsgNetGetNetworkInfo(&ipAddrs, NULL);

        /* Order the server IP list according to the given destination & preferred subnet, if any */
        orderedIpList = cMsgNetOrderIpAddrs(directIpList, ipAddrs, subnet, &noPrefSubnetMatch);
        if (noPrefSubnetMatch) {
            printf("emu connect: preferred subnet = %s, but no local interface on that subnet\n", subnet);
        }

        /* Try all IP addresses in list until one works */
        ipList = orderedIpList;


        nextIp:
        while (orderedIpList != NULL) {

            if (subnet != NULL) {
                /* won't return error */
                if (outgoingIp != NULL) free(outgoingIp);
                cMsgNetGetMatchingLocalIpAddress(subnet, &outgoingIp);
            }

            /* First connect to server host & port */
/*printf("emu connect: try connecting %d socket(s) directly to ip = %s, port = %d thru local ip = %s ...\n",
                   socketCount, orderedIpList->addr, serverPort, outgoingIp);*/

            /* For each socket, make a connection */
            for (i = 0; i < socketCount; i++) {

                err = cMsgNetTcpConnectTimeout2(orderedIpList->addr, outgoingIp, serverPort,
                                                tcpSendBufSize, 0, tcpNoDelay,
                                                &tv, &domain->sendSockets[i], NULL);

                if (err != CMSG_OK) {
                    /* If there is another address to try, try it */
                    if (orderedIpList->next != NULL) {
                        orderedIpList = orderedIpList->next;
                        //continue;
                        goto nextIp;
                    }
                        /* if we've tried all addresses in the list, return an error */
                    else {
                        cMsgDomainFree(domain);
                        free(domain);
                        codanetFreeAddrList(ipList);
                        if (outgoingIp != NULL) free(outgoingIp);
                        return (err);
                    }
                }
            }

            printf("          Connected\n");

            /* Quit loop if we've made a connection to server, store good address */
            if (domain->serverHost != NULL) {
                free(domain->serverHost);
            }
            domain->serverHost = strdup(orderedIpList->addr);
            break;
        }

        codanetFreeAddrList(ipList);
        cMsgNetFreeIpAddrs(ipAddrs);
    }

    if (subnet != NULL) free(subnet);
    if (outgoingIp != NULL) free(outgoingIp);

    /*---------------------*/
    /* talk to emu server  */
    /*---------------------*/
    /* magic #s */
    outGoing[0] = htonl(CMSG_MAGIC_INT1);
    outGoing[1] = htonl(CMSG_MAGIC_INT2);
    outGoing[2] = htonl(CMSG_MAGIC_INT3);
    /* cMsg major version */
    outGoing[3] = htonl(CMSG_VERSION_MAJOR);
    /* my coda Id */
    outGoing[4] = htonl((uint32_t) codaId);
    /* max size data buffer in bytes in one cMsg message */
    outGoing[5] = htonl((uint32_t)dataBufSize);
    /* total number of sockets that will be connecting */
    outGoing[6] = htonl((uint32_t)socketCount);

    for (i=0; i <socketCount; i++) {
        /* place of this socket relative to others: 1, 2, ... */
        outGoing[7] = htonl((uint32_t) (i+1));

        /* send data over TCP socket */
        len = cMsgNetTcpWrite(domain->sendSockets[i], (void *) outGoing, 8*sizeof(int32_t));
        if (len != 8*sizeof(int32_t)) {
            cMsgDomainFree(domain);
            free(domain);
            printf("emu connect: write to server failure\n");
            return (CMSG_NETWORK_ERROR);
        }
    }

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
    int32_t ints[5];
    int     i, magic[3], addressCount, port, totalLen, ipLen;
    char    buf[1024], *pbuf;
    ssize_t len;
    cMsgDomainInfo *domain = threadArg->domain;
    codaIpList *listHead=NULL, *listEnd=NULL, *listItem=NULL;
    struct timespec timeout = {0,1};

    totalLen = 5*sizeof(int32_t);
    
    /* release resources when done */
    pthread_detach(pthread_self());

    nextPacket:
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
        if (len < totalLen) {
          /*printf("receiverThd: got packet that's too small\n");*/
          continue;
        }
        
        pbuf = buf;
        memcpy(ints, pbuf, sizeof(ints));
        pbuf += sizeof(ints);

/*printf("receiverThd: packet from host %s on port %hu with %d bytes\n",
                inet_ntoa(threadArg->addr.sin_addr), ntohs(threadArg->addr.sin_port), (int) len);*/

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

        /*--------------------------*/
        /*  how many IP addresses?  */
        /*--------------------------*/
        addressCount = ntohl((uint32_t)ints[4]);
        if (addressCount < 1) {
          printf("receiverThd: received bad IP addresse count (%d)\n", addressCount);
          continue;
        }

        if (addressCount < 1) {
            printf("receiverThd: bad number of IP addresses (%d), ignore packet\n", addressCount);
            addressCount = 0;
            continue;
        }

/*printf("receiverThd: port = %d, # addrs = %d\n", port, addressCount);*/
        listHead = NULL;

        for (i=0; i < addressCount; i++) {
            /******************/
            /*   IP address   */
            /******************/

            /* Check for 1 int of data */
            totalLen +=  sizeof(int);
            if (len < totalLen) {
                printf("receiverThd: multicast response has too little data, ignore packet\n");
                codanetFreeAddrList(listHead);
                free(listItem);
                goto nextPacket;
            }

            /* Create address item */
            listItem = (codaIpList *) calloc(1, sizeof(codaIpList));
            if (listItem == NULL) {
                printf("receiverThd: out of memory, quit program\n");
                exit(-1);
            }

            /* First read in IP address len */
            memcpy(&ipLen, pbuf, sizeof(int));
            ipLen = ntohl((uint32_t)ipLen);
            pbuf += sizeof(int);
/*printf("receiverThd: iplen = %d\n", ipLen);*/

            /* Check to see if IP address is the right size for dot-decimal format */
            if (ipLen < 7 || ipLen > 20) {
                printf("receiverThd: multicast response has wrong format, ignore packet\n");
                codanetFreeAddrList(listHead);
                free(listItem);
                goto nextPacket;
            }

            /* Check for address worth of data */
            totalLen +=  ipLen;
            if (len < totalLen) {
                printf("receiverThd: multicast response has too little data, ignore packet\n");
                codanetFreeAddrList(listHead);
                free(listItem);
                goto nextPacket;
            }

            /* Put address into a list for later sorting */
            memcpy(listItem->addr, pbuf, (size_t)ipLen);
            listItem->addr[ipLen] = 0;
            pbuf += ipLen;

            /***********************/
            /*  Broadcast address  */
            /***********************/

            /* Check for 1 int of data */
            totalLen +=  sizeof(int);
            if (len < totalLen) {
                printf("receiverThd: multicast response has too little data, ignore packet\n");
                codanetFreeAddrList(listHead);
                free(listItem);
                goto nextPacket;
            }

            /* Len of corresponding broadcast address */
            memcpy(&ipLen, pbuf, sizeof(int));
            ipLen = ntohl((uint32_t)ipLen);
            pbuf += sizeof(int);
/*printf("receiverThd: broadcast iplen = %d\n", ipLen);*/

            /* Check to see if address is the right size for dot-decimal format */
            if (ipLen < 7 || ipLen > 20) {
                printf("receiverThd: multicast response has wrong format, ignore packet\n");
                codanetFreeAddrList(listHead);
                free(listItem);
                goto nextPacket;
            }

            /* Check for address worth of data */
            totalLen +=  ipLen;
            if (len < totalLen) {
                printf("receiverThd: multicast response has too little data, ignore packet\n");
                codanetFreeAddrList(listHead);
                free(listItem);
                goto nextPacket;
            }

            /* Put broadcast address into a list for later sorting */
            memcpy(listItem->bAddr , pbuf, (size_t)ipLen);
            listItem->bAddr[ipLen] = 0;
            pbuf += ipLen;

/*printf("receiverThd: found ip = %s, broadcast = %s\n", listItem->addr, listItem->bAddr);*/

            /* Put address item into a list for later sorting */
            if (listHead == NULL) {
                listHead = listEnd = listItem;
            }
            else {
                listEnd->next = listItem;
                listEnd = listItem;
            }
        }
                        
        /* Tell main thread we got a response. */
        threadArg->ipList = listHead;
        cMsgLatchCountDown(&domain->syncLatch, &timeout);
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
      fprintf(stderr, "cmsg_emu_send: domainId arg is NULL\n");
      return(CMSG_BAD_ARGUMENT);
  }
      
  /* check args */
  if (msg == NULL) {
      fprintf(stderr, "cmsg_emu_send: message arg pointer is NULL\n");
      return(CMSG_BAD_ARGUMENT);
  }

  /* Round-robin between all sockets to send messages over. */
  fd = domain->sendSockets[domain->socketIndex];

  /* Type of message is in 1st (lowest) byte, source (Emu's EventType) of message is in 2nd byte */
  outGoing[0] = htonl((uint32_t)msg->userInt);
  /* Length of byte array (not including this int) */
  lenByteArray = msg->byteArrayLength;
  outGoing[1] = htonl((uint32_t)lenByteArray);

  /* Cannot run this while connecting/disconnecting */
  /*cMsgConnectReadLock(domain);*/

  if (domain->gotConnection != 1) {
    /*cMsgConnectReadUnlock(domain);*/
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

  /* Next time use the next socket. */
  domain->socketIndex = (domain->socketIndex + 1) % domain->sendSocketCount;

  /* done protecting communications */
  /*cMsgConnectReadUnlock(domain);*/
 
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

    int i;
    cMsgDomainInfo *domain;

    if (domainId == NULL) return(CMSG_BAD_ARGUMENT);
    domain = (cMsgDomainInfo *) (*domainId);
    if (domain == NULL) return(CMSG_BAD_ARGUMENT);

    /* When changing initComplete / connection status, protect it */
    /*cMsgConnectWriteLock(domain);*/

    domain->gotConnection = 0;

    /* close TCP sending sockets */
    for (i=0; i < domain->sendSocketCount; i++) {
        close(domain->sendSockets[i]);
    }

    /* stop listening and client communication threads */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cmsg_emu_disconnect: cancel listening & client threads\n");
    }

    /*cMsgConnectWriteUnlock(domain);*/

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
 * Make this able to extract an optional host (previously it was always multicast).
 *
 * Emu domain UDL is of the form:<p>
 *   <b>cMsg:emu://&lt;host&gt;:&lt;port&gt;/&lt;expid&gt;/&lt;compName&gt;?codaId=&lt;id&gt;&timeout=&lt;sec&gt;&bufSize=&lt;size&gt;&tcpSend=&lt;size&gt;&subnet=&lt;subnet&gt;&sockets=&lt;count&gt;&noDelay</b><p>
 *
 * Remember that for this domain:
 *<ol>
 *<li>host is optional and may be: 1) "multicast" (default, 239.230.0.0), 2) "direct" in which case the
 *    caller must have previously called the routine setDirectConnectDestination() in order to statically
 *    store the IP addresses of the destination TCP server, or 3) a dot-decimal format IP address of the
 *    TCP server or multicast address. <p>
 *<li>port is required - UDP multicast port if host = "multicast" or multicast address, else TCP port<p>
 *<li>expid is required<p>
 *<li>compName is required - destination CODA component name<p>
 *<li>codaId (coda id of data sender) is required<p>
 *<li>optional timeout (sec) to connect to emu server, default = 0 (wait forever)<p>
 *<li>optional bufSize (max size in bytes of a single send), min = 1KB, default = 2.1MB<p>
 *<li>optional tcpSend is the TCP send buffer size in bytes, min = 1KB<p>
 *<li>optional subnet is the preferred subnet used to connect to server when multicasting,
 *             or the subnet corresponding to the host IP address if directly connecting.<p>
 *<li>optional sockets is the number of TCP sockets to use when connecting to server<p>
 *<li>optional noDelay is the TCP no-delay parameter turned on<p>
 *</ol><p>
 *
 * @param UDLR          partial UDL to be parsed
 * @param server        pointer filled in with server IP or multicast IP
 * @param port          pointer filled in with port
 * @param expid         pointer filled in with expid, allocates mem and must be freed by caller
 * @param compName      pointer filled in with component name, allocates mem and must be freed by caller
 * @param multicasting  pointer filled in with 1 if multicasting, else 0
 * @param codaId        pointer filled in with codaId
 * @param timeout       pointer filled in with timeout if it exists
 * @param bufSize       pointer filled in with bufSize if it exists
 * @param tcpSend       pointer filled in with tcpSend if it exists
 * @param noDelay       pointer filled in with 1 if noDelay exists, else 0
 * @param sockets       pointer filled in with number of sockets to make
 * @param subnet        pointer filled in with preferred subnet, allocates mem and must be freed by caller
 * @param remainder     pointer filled in with the part of the udl after host:port/expid if it exists,
 *                      else it is set to NULL; allocates mem and must be freed by caller if not NULL
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_FORMAT if UDLR arg is not in the proper format
 * @returns CMSG_BAD_ARGUMENT if UDLR arg is NULL
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 * @returns CMSG_OUT_OF_RANGE if port is an improper value
 */
static int parseUDL(const char *UDLR,
                    char *serverIP,
                    unsigned short *port,
                    char **expid,
                    char **compName,
                    int  *multicasting,
                    int  *codaId,
                    int  *timeout,
                    int  *bufSize,
                    int  *tcpSend,
                    int  *noDelay,
                    int  *sockets,
                    char **subnet,
                    char **remainder) {

    int        err, Port, index, myMulticasting=0;
    size_t     len, bufLength;
    char       *myServerIP, *udlRemainder, *val, *myExpid = NULL, *myComponent=NULL;
    char       *buffer;
    const char *pattern = "(([^:/?]+):)?([0-9]+)/([^/]+)/([^?&]+)(.*)";
    /* we have 7 potential matches: 1st = whole, 6 sub (1st sub is ignored since it has : */
    int        matchCount=7;
    regmatch_t matches[matchCount];
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
    err = cMsgRegexec(&compiled, udlRemainder, matchCount, matches, 0);
    if (err != 0) {
        /* no match */
        free(udlRemainder);
        free(buffer);
    	cMsgRegfree(&compiled);
        return (CMSG_BAD_FORMAT);
    }
    
    /* free up memory */
    cMsgRegfree(&compiled);
    /*
    int i;
    for (i=0; i < matchCount; i++) {
        if (matches[i].rm_so < 0) {
            printf("parseUDL: have NO match at i = %d\n", i);
        }
        else {
            buffer[0] = 0;
            len = matches[i].rm_eo - matches[i].rm_so;
            strncat(buffer, udlRemainder+matches[i].rm_so, len);
            printf("parseUDL: have a match at i = %d, %s\n", i, buffer);
        }
    }
     */

    /***********************************/
    /* find direct / multicast  */
    /***********************************/
    index = 2;
    buffer[0] = 0;
    if (matches[index].rm_so < 0) {
        // If server is not in UDL, multicast by default
        myServerIP = strdup("multicast");
        /*printf("parseUDL: server is NOT in UDL\n");*/
    }
    else {
        len = matches[index].rm_eo - matches[index].rm_so;
        strncat(buffer, udlRemainder+matches[index].rm_so, len);
        myServerIP = strdup(buffer);
    }

    if (strcasecmp("direct", myServerIP) == 0) {
        free((void *)myServerIP);
        myMulticasting = 0;
        myServerIP = NULL;
printf("parseUDL: server IP = \"direct\", will directly connect to emu TCP server\n");
    }
    else if (strcasecmp("multicast", myServerIP) == 0) {
        free((void *)myServerIP);
        myServerIP = strdup(EMU_MULTICAST_ADDR);
        myMulticasting = 1;
printf("parseUDL: server IP = \"multicast\", will multicast to %s\n", myServerIP);
    }
    else {
        // IS dotted decimal?
        int dec[4];
        int isDotDec = cMsgNetIsDottedDecimal(myServerIP, dec);

        // If the server IP is NOT dot decimal, print warning and go to multicasting
        if (!isDotDec) {
printf("parseUDL: server IP is NOT in dot-decimal format so multicast to %s\n", EMU_MULTICAST_ADDR);
            free((void *)myServerIP);
            myServerIP = strdup(EMU_MULTICAST_ADDR);
            myMulticasting = 1;
        }
        else {
            // Check to see if it's a multicast address
            if (dec[0] >= 224 && dec[0] <= 239) {
                printf("parseUDL: server IP is a MULTICAST addres so multicast to %s\n", myServerIP);
                myMulticasting = 1;
            }
            else {
                myMulticasting = 0;
                printf("parseUDL: server IP = %s, will directly connect\n", myServerIP);
            }
        }
    }

    if (serverIP != NULL) {
        if (myServerIP == NULL) {
            serverIP[0] = '\0';
        }
        else {
            strcpy(serverIP, myServerIP);
        }
    }

    if (multicasting != NULL) {
        *multicasting = myMulticasting;
    }
    /*printf("parseUDL: server IP = %s\n", myServerIP);*/

    /*************/
    /* find port */
	/*************/
    index++;
    if (matches[index].rm_so < 0) {
        /* no match for port & NO DEFAULT as all EBs,ERs must be different from each other */
/*printf("parseUDL: port required in UDL\n");*/
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
printf("parseUDL: port = %d\n", Port);

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
printf("parseUDL: expid = %s\n", myExpid);

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
printf("parseUDL: component name = %s\n", myComponent);

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
printf("parseUDL: codaId = %d\n", t);
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
printf("parseUDL: timeout (sec) = %d\n", t);
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
printf("parseUDL: bufSize (bytes) = %d\n", t);
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
printf("parseUDL: tcpSend buffer (bytes) = %d\n", t);
        }
    }

    /* free up memory */
    cMsgRegfree(&compiled);

    /******************************/
    /* now look for sockets=value */
    /******************************/
    pattern = "[?&]sockets=([0-9]+)";

    cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
    err = cMsgRegexec(&compiled, val, 2, matches, 0);
    if (err == 0) {
        /* find # sockets */
        if (matches[1].rm_so >= 0) {
            int t;
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            strncat(buffer, val+matches[1].rm_so, len);
            t = atoi(buffer);
            if (t < 1) t=1;
            if (sockets != NULL) {
                *sockets = t;
            }
printf("parseUDL: sockets = %d\n", t);
        }
    }

    /* free up memory */
    cMsgRegfree(&compiled);

    /*****************************/
    /* now look for subnet=value */
    /*****************************/
    pattern = "[?&]subnet=([0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3})";

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
printf("parseUDL: noDelay = on\n");
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



/**
 * This routine parses, using regular expressions, the emu domain
 * portion of the UDL sent from the next level up" in the API.
 *
 * Emu domain UDL is of the form:<p>
 *   <b>cMsg:emu://&lt;port&gt;/&lt;expid&gt;/&lt;compName&gt;?codaId=&lt;id&gt;&timeout=&lt;sec&gt;&bufSize=&lt;size&gt;&tcpSend=&lt;size&gt;&subnet=&lt;subnet&gt;&sockets=&lt;count&gt;&noDelay</b><p>
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
 *<li>optional sockets is the number of TCP sockets to use when connecting to server<p>
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
 * @param sockets    pointer filled in with number of sockets to make
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
static int parseUDLOrig(const char *UDLR,
                    unsigned short *port,
                    char **expid,
                    char **compName,
                    int  *codaId,
                    int  *timeout,
                    int  *bufSize,
                    int  *tcpSend,
                    int  *noDelay,
                    int  *sockets,
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
/*printf("parseUDL: port required in UDL\n");*/
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

    /******************************/
    /* now look for sockets=value */
    /******************************/
    pattern = "[?&]sockets=([0-9]+)";

    cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
    err = cMsgRegexec(&compiled, val, 2, matches, 0);
    if (err == 0) {
        /* find # sockets */
        if (matches[1].rm_so >= 0) {
            int t;
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            strncat(buffer, val+matches[1].rm_so, len);
            t = atoi(buffer);
            if (t < 1) t=1;
            if (sockets != NULL) {
                *sockets = t;
            }
/*printf("parseUDL: sockets = %d\n", t);*/
        }
    }

    /* free up memory */
    cMsgRegfree(&compiled);

    /*****************************/
    /* now look for subnet=value */
    /*****************************/
    pattern = "[?&]subnet=([0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3})";

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


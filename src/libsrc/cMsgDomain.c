/*----------------------------------------------------------------------------*
 *
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    E.Wolin, 15-Jul-2004, Jefferson Lab                                     *
 *                                                                            *
 *    Authors: Elliott Wolin                                                  *
 *             wolin@jlab.org                    Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-7365             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *             Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *
 *  Implements cMsg CODA domain
 *
 *
 *----------------------------------------------------------------------------*/

/**
 * @file
 * This file contains the cMsg domain implementation of the cMsg user API.
 * This a messaging system programmed by the Data Acquisition Group at Jefferson
 * Lab. The cMsg domain has a dual function. It acts as a framework so that the
 * cMsg client can connect to a variety of subdomains (messaging systems). However,
 * it also acts as a messaging system itself in the cMsg <b>subdomain</b>.
 */

/* system includes */
#include <pwd.h>
#include <sys/types.h>
#include <sys/socket.h>
#ifdef Darwin
    #include <uuid/uuid.h>
#else
    #include <sys/times.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <ctype.h>


/* package includes */
#include "cMsgNetwork.h"
#include "cMsgPrivate.h"
#include "cMsg.h"
#include "cMsgDomain.h"
#include "rwlock.h"
#include "cMsgRegex.h"
#include "cMsgCommonNetwork.h"


/**
 * Structure for arg to be passed to receiver/multicast threads.
 * Allows data to flow back and forth with these threads.
 */
typedef struct thdArg_t {
    int sockfd;
    socklen_t len;
    int port;
    struct sockaddr_in addr;
    struct sockaddr_in *paddr;
    int   bufferLen;
    char *buffer;
    codaIpList *ipList;
} thdArg;


/* built-in limits */
/** Number of seconds to wait for cMsgClientListeningThread threads to start. */
#define WAIT_FOR_THREADS 10

/* Array (one element for each connect) holding pointers to allocated memory
 * containing connection information.
 * Assume client does no more than CMSG_CONNECT_PTRS_ARRAY_SIZE concurrent connects.
 * Usage protected by memoryMutex. */
void* connectPointers[CMSG_CONNECT_PTRS_ARRAY_SIZE];

/* local variables */

/* Counter to help orderly use of connectPointers array. */
static int connectPtrsCounter = 0;

/** Is the one-time initialization done? */
static int oneTimeInitialized = 0;

/** Pthread mutex to allow freeing memory without seg faulting. */
static pthread_mutex_t memoryMutex = PTHREAD_MUTEX_INITIALIZER;

/** Pthread mutex to protect the local generation of unique numbers. */
static pthread_mutex_t numberMutex = PTHREAD_MUTEX_INITIALIZER;

/** Id number which uniquely defines a subject/type pair. */
static int subjectTypeId = 1;

/** Size of buffer in bytes for sending messages. */
static int initialMsgBufferSize = 15000;

/** Mutex for waiting for multicast response.*/
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/** Condition variable for waiting for multicast response.*/
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/* Prototypes of the functions which implement the standard cMsg tasks in the cMsg domain. */
int   cmsg_cmsg_connect           (const char *myUDL, const char *myName,
                                   const char *myDescription,
                                   const char *UDLremainder, void **domainId);
int   cmsg_cmsg_reconnect         (void *domainId);
int   cmsg_cmsg_send              (void *domainId, void *msg);
int   cmsg_cmsg_syncSend          (void *domainId, void *msg, const struct timespec *timeout,
                                   int *response);
int   cmsg_cmsg_flush             (void *domainId, const struct timespec *timeout);
int   cmsg_cmsg_subscribe         (void *domainId, const char *subject, const char *type,
                                   cMsgCallbackFunc *callback, void *userArg,
                                   cMsgSubscribeConfig *config, void **handle);
int   cmsg_cmsg_unsubscribe       (void *domainId, void *handle);
int   cmsg_cmsg_subscriptionPause (void *domainId, void *handle);
int   cmsg_cmsg_subscriptionResume(void *domainId, void *handle);
int   cmsg_cmsg_subscriptionQueueClear(void *domainId, void *handle);
int   cmsg_cmsg_subscriptionQueueCount(void *domainId, void *handle, int *count);
int   cmsg_cmsg_subscriptionQueueIsFull(void *domainId, void *handle, int *full);
int   cmsg_cmsg_subscriptionMessagesTotal(void *domainId, void *handle, int *total);
int   cmsg_cmsg_subscribeAndGet   (void *domainId, const char *subject, const char *type,
                                   const struct timespec *timeout, void **replyMsg);
int   cmsg_cmsg_sendAndGet        (void *domainId, void *sendMsg,
                                   const struct timespec *timeout, void **replyMsg);
int   cmsg_cmsg_monitor           (void *domainId, const char *command,  void **replyMsg);
int   cmsg_cmsg_start             (void *domainId);
int   cmsg_cmsg_stop              (void *domainId);
int   cmsg_cmsg_disconnect        (void **domainId);
int   cmsg_cmsg_shutdownClients   (void *domainId, const char *client, int flag);
int   cmsg_cmsg_shutdownServers   (void *domainId, const char *server, int flag);
int   cmsg_cmsg_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg);
int   cmsg_cmsg_isConnected       (void *domainId, int *connected);
int   cmsg_cmsg_setUDL            (void *domainId, const char *udl, const char *remainder);
int   cmsg_cmsg_getCurrentUDL     (void *domainId, const char **udl);
int   cmsg_cmsg_getServerHost     (void *domainId, const char **ipAddress);
int   cmsg_cmsg_getServerPort     (void *domainId, int *port);
int   cmsg_cmsg_getInfo           (void *domainId, const char *command, char **string);


/** List of the functions which implement the standard cMsg tasks in the cMsg domain. */
static domainFunctions functions = {cmsg_cmsg_connect, cmsg_cmsg_reconnect,
                                    cmsg_cmsg_send, cmsg_cmsg_syncSend, cmsg_cmsg_flush,
                                    cmsg_cmsg_subscribe, cmsg_cmsg_unsubscribe,
                                    cmsg_cmsg_subscriptionPause, cmsg_cmsg_subscriptionResume,
                                    cmsg_cmsg_subscriptionQueueClear, cmsg_cmsg_subscriptionMessagesTotal,
                                    cmsg_cmsg_subscriptionQueueCount, cmsg_cmsg_subscriptionQueueIsFull,
                                    cmsg_cmsg_subscribeAndGet, cmsg_cmsg_sendAndGet,
                                    cmsg_cmsg_monitor, cmsg_cmsg_start,
                                    cmsg_cmsg_stop, cmsg_cmsg_disconnect,
                                    cmsg_cmsg_shutdownClients, cmsg_cmsg_shutdownServers,
                                    cmsg_cmsg_setShutdownHandler, cmsg_cmsg_isConnected,
                                    cmsg_cmsg_setUDL, cmsg_cmsg_getCurrentUDL,
                                    cmsg_cmsg_getServerHost, cmsg_cmsg_getServerPort,
                                    cmsg_cmsg_getInfo};
                                    
/* cMsg domain type */
domainTypeInfo cmsgDomainTypeInfo = {
  "cmsg",
  &functions
};


/* local prototypes */

/* mutexes and read/write locks */
static void  numberMutexLock(void);
static void  numberMutexUnlock(void);
static cMsgDomainInfo* cMsgPrepareToUseMem(int index);
static void            cMsgCleanupAfterUsingMem(int index);

/* threads */
static void *keepAliveThread(void *arg);
static void *updateServerThread(void *arg);

/* failovers */
static int restoreSubscriptions(cMsgDomainInfo *domain) ;
static int failoverSuccessful(cMsgDomainInfo *domain, int waitForResubscribes);
static int resubscribe(cMsgDomainInfo *domain, subInfo *sub);
static int reconnect(void *domainId, codaIpList *ipList);
static int connectToServer(void *domainId);

/* misc */
static int  udpSend(cMsgDomainInfo *domain, intptr_t index, cMsgMessage_t *msg);
static int  sendMonitorInfo(cMsgDomainInfo *domain, int connfd);
static int  getMonitorInfo(cMsgDomainInfo *domain);
static int  partialShutdown(void *domainId, int reconnecting);
static int  totalShutdown(void *domainId);
static int  connectDirect(cMsgDomainInfo *domain, void *domainId, codaIpList *ipList);
static int  connectToDomainServer(cMsgDomainInfo *domain, void *domainId,
                                  int uniqueClientKey, int reconnecting);
static int  talkToNameServer(cMsgDomainInfo *domain, int serverfd, int *uniqueClientKey);
static int  parseUDL(const char *UDL,parsedUDL *parsedUdl);
static int  unSendAndGet(cMsgDomainInfo *domain, int id);
static int  unSubscribeAndGet(cMsgDomainInfo *domain, const char *subject,
                              const char *type, int id);
static void defaultShutdownHandler(void *userArg);
static void *receiverThd(void *arg);
static void *multicastThd(void *arg);
static int  connectWithMulticast(cMsgDomainInfo *domain,
                                 codaIpList **hostList, int *port);
                                      

/*-------------------------------------------------------------------*/


/**
* This routine resets the server host anme, but is <b>NOT</b> implemented in this domain.
* @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
*/
int cmsg_cmsg_getServerHost(void *domainId, const char **ipAddress) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
* This routine resets server socket port, but is <b>NOT</b> implemented in this domain.
* @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
*/
int cmsg_cmsg_getServerPort(void *domainId, int *port) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/

                                  
/**
 * This routine locks the pthread mutex used when freeing memory. */
void cMsgMemoryMutexLock(void) {

    int status = pthread_mutex_lock(&memoryMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed free mutex lock");
    }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the pthread mutex used when freeing memory. */
void cMsgMemoryMutexUnlock(void) {

    int status = pthread_mutex_unlock(&memoryMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed free mutex unlock");
    }
}


/*-------------------------------------------------------------------*/

/**
 * This routine locks the pthread mutex used when creating unique id numbers. */
static void numberMutexLock(void) {

    int status = pthread_mutex_lock(&numberMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed number mutex lock");
    }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the pthread mutex used when creating unique id numbers
 * and doing the one-time intialization. */
static void numberMutexUnlock(void) {

    int status = pthread_mutex_unlock(&numberMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed number mutex unlock");
    }
}


/*-------------------------------------------------------------------*/


/** This routine does bookkeeping before using allocated memory. */
static cMsgDomainInfo* cMsgPrepareToUseMem(int index) {
    cMsgDomainInfo *domain;

    cMsgMemoryMutexLock();
    domain = connectPointers[index];
/*    if (domain == NULL)
    printf("cMsgPrepareToUseMem: grabbed 1st mutex, index = %d, domain = %p\n",index, domain);*/
    /* if bad index or disconnect has already been run, bail out */
    if (domain == NULL || domain->disconnectCalled) {
        cMsgMemoryMutexUnlock();
        return(NULL);
    }
    domain->functionsRunning++;
/*printf("cMsgPrepareToUseMem: functionsRunning = %d\n",domain->functionsRunning);*/
    cMsgMemoryMutexUnlock();
    return(domain);
}


/** This routine does bookkeeping after using allocated memory. */
static void cMsgCleanupAfterUsingMem(int index) {
    cMsgDomainInfo *domain;

    cMsgMemoryMutexLock();
    domain = connectPointers[index];
    /* domain should not have been freed since we incremented functionsRunning
     * before calling this function.*/
    domain->functionsRunning--;
/*printf("cMsgCleanupAfterUsingMem: functionsRunning = %d\n",domain->functionsRunning);*/
    /* free memory if disconnect was called and no more functions using domain */
    if (domain->disconnectCalled && domain->functionsRunning < 1) {
/*printf("cMsgCleanupAfterUsingMem: freeing memory: index = %d, domain = %p\n", index, domain);*/
        cMsgDomainFree(domain);
        free(domain);
        connectPointers[index] = NULL;
    }
    cMsgMemoryMutexUnlock();
}


/*-------------------------------------------------------------------*/


/**
 * This routine restores subscriptions to a new server which replaced a
 * crashed server during failover. It is already protected by
 * cMsgConnectWriteLock when called in the keepAlive thread.
 *
 * @param domain id of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 */
static int restoreSubscriptions(cMsgDomainInfo *domain)  {
  int i, err, size;
  hashNode *entries = NULL;
  subInfo *sub;
  
  hashGetAll(&domain->subscribeTable, &entries, &size);
  
  if (entries != NULL) {
    for (i=0; i < size; i++) {
      sub = (subInfo *)entries[i].data;  /* val = subscription struct */

/*printf("\nRestore Subscription to sub = %s, type = %s\n\n", sub->subject, sub->type);*/
      err = resubscribe(domain, sub);    
      if (err != CMSG_OK) {
        free(entries);
        return(err);
      }
    }
    
    free(entries);
  }

  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/


/**
 * This routine waits a while for a possible failover to a new cMsg server 
 * before attempting to complete an interrupted command to the server or
 * before returning an error.
 *
 * @param domain id of the domain connection
 * @param waitForResubscribe 1 if waiting for all subscriptions to be
 *                           reinstated before returning, else 0
 *
 * @returns 1 if there is a connection to a cMsg server in 3 seconds or 0 if not 
 */
static int failoverSuccessful(cMsgDomainInfo *domain, int waitForResubscribes) {
    int err;
    struct timespec wait;
        
    wait.tv_sec  = 3;
    wait.tv_nsec = 0; /* 3 secs */

/*printf("IN failoverSuccessful\n");*/
    /*
     * If only 1 viable UDL is given by client, forget about
     * waiting for failovers to complete before returning an error.
     */
    if (!domain->implementFailovers) {
/*printf("   failoverSuccessful, Only 1 viable UDL given, so no failing over\n");*/
        return 0;
    }

    /*
     * Wait for 3 seconds for a new connection
     * before giving up and returning an error.
     */
    
    err = cMsgLatchAwait(&domain->syncLatch, &wait);
/*printf("   failoverSuccessful, DONE waiting on latch, gotConnection = %d\n", domain->gotConnection);*/
    /* if latch reset or timedout, return false */
    if (err < 1) {
      return 0;
    }

    if (waitForResubscribes) {
       if (domain->gotConnection && domain->resubscribeComplete) return 1;
    }
    else {
       if (domain->gotConnection) return 1;
    }
    
    return 0;
}


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
int cmsg_cmsg_getInfo(void *domainId, const char *command, char **string) {
    return(CMSG_NOT_IMPLEMENTED);
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
int cmsg_cmsg_isConnected(void *domainId, int *connected) {
    intptr_t index;
    cMsgDomainInfo *domain;

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) {
        if (connected != NULL) *connected = 0;
        return(CMSG_OK);
    }

    cMsgMemoryMutexLock();
    domain = connectPointers[index];
    if (domain == NULL || domain->disconnectCalled) {
        if (connected != NULL) *connected = 0;
        cMsgMemoryMutexUnlock();
        return(CMSG_OK);
    }
    if (connected != NULL) *connected = domain->gotConnection;
    cMsgMemoryMutexUnlock();
    
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine resets the UDL (may be a semicolon separated list of single UDLs).
 * If a reconnect is done, the new UDLs will be used in the connection(s).
 *
 * @param domainId id of the domain connection
 * @param newUDL new UDL
 * @param newRemainder new UDL remainder (not used in this routine/domain)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_FORMAT if newUDL is not in proper format
 * @returns CMSG_BAD_ARGUMENT if domainId is bad
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 */
int cmsg_cmsg_setUDL(void *domainId, const char *newUDL, const char *newRemainder) {
    intptr_t index;
    char *p, *udl;
    int i, j, err;
    int failoverUDLCount = 0;
    parsedUDL *pUDL;
    cMsgDomainInfo *domain;

    /*
     * The UDL may be a semicolon separated list of UDLs, separate them and
     * store them for future use in failovers.
     */

    /* On first pass, just do a count. */
    udl = strdup(newUDL);
    p = strtok(udl, ";");
    while (p != NULL) {
        failoverUDLCount++;
        p = strtok(NULL, ";");
    }
    free(udl);
/*printf("setUDL: found %d UDLs\n", failoverUDLCount);*/

    if (failoverUDLCount < 1) {
        return(CMSG_BAD_FORMAT);
    }

    /* Now that we know how many UDLs there are, allocate array. */
    pUDL = (parsedUDL *) calloc((size_t)failoverUDLCount, sizeof(parsedUDL));
    if (pUDL == NULL) {
        return(CMSG_OUT_OF_MEMORY);
    }

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) {
        return(CMSG_BAD_ARGUMENT);
    }


    /* On second pass, stored parsed UDLs. */
    udl = strdup(newUDL);
    p   = strtok(udl, ";");
    i   = 0;
    while (p != NULL) {
/*printf("setUDL: try parsing udl = %s\n", p);*/
        cMsgParsedUDLInit(&pUDL[i]);
        /* Parse the UDL (Uniform Domain Locator) */
        if ( (err = parseUDL(p, &pUDL[i])) != CMSG_OK ) {
/*printf("setUDL: error parsing udl = %s\n", p);*/
            /* There's been an error parsing a UDL */
            /* free all UDL's just parsed */
            for (j=0; j<i; j++) {
/*printf("setUDL: freed parsed UDL = %s\n",pUDL[j].udl);*/
                cMsgParsedUDLFree(&pUDL[j]);
            }
            free(udl); free(pUDL);
            return(CMSG_BAD_FORMAT);
        }
        pUDL[i].udl = strdup(p);
/*printf("Found UDL = %s\n", domain->failovers[i].udl);*/
        p = strtok(NULL, ";");
        i++;
    }
    free(udl);

    /* make sure domain is usable */
    if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    
    /* Cannot run this with other functions */
    cMsgConnectWriteLock(domain);

    /* Free any previously allocated memory for failovers */
    if (domain->failovers != NULL) {
        for (j=0; j<domain->failoverSize; j++) {
            cMsgParsedUDLFree(&domain->failovers[j]);
        }
        free(domain->failovers);
    }
    
    domain->failovers = pUDL;
    domain->failoverSize = failoverUDLCount;
    domain->implementFailovers = 0;
  
    /* If we have more than one valid UDL, we can implement waiting
     * for a successful failover before aborting commands to the server
     * that were interrupted due to server failure. */
    if (failoverUDLCount > 1 ||
        domain->failovers[0].failover == CMSG_FAILOVER_CLOUD ||
        domain->failovers[0].failover == CMSG_FAILOVER_CLOUD_ONLY ) {

        /* Using failovers */
        domain->implementFailovers = 1;
    }
    
    cMsgConnectWriteUnlock(domain);
    cMsgCleanupAfterUsingMem((int)index);

    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine gets the UDL current used in the existing connection.
 *
 * @param domainId id of the domain connection
 * @param udl pointer filled in with current UDL (do NOT write to pointer)
 *            or = NULL if no connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if domainId is bad
 */
int cmsg_cmsg_getCurrentUDL(void *domainId, const char **udl) {
    intptr_t index;
    cMsgDomainInfo *domain;

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) {
        return(CMSG_BAD_ARGUMENT);
    }

    cMsgMemoryMutexLock();
    domain = connectPointers[index];
    if (domain == NULL || domain->disconnectCalled) {
        cMsgMemoryMutexUnlock();
        return(CMSG_OK);
    }
    if (udl != NULL) {
        if (domain->gotConnection) {
            *udl = domain->currentUDL.udl;
        }
        else {
            *udl = NULL;
        }
    }
    cMsgMemoryMutexUnlock();
    
    return(CMSG_OK);
}



/*-------------------------------------------------------------------*/


/**
 * This routine is called once to connect to a cMsg domain. It is called
 * by the user through top-level cMsg API, "cMsgConnect()".
 * The argument "myUDL" is the Universal Domain Locator (or can be a semicolon
 * separated list of UDLs) used to uniquely identify the cMsg server to connect to.
 * It has the form:<p>
 *       <b>cMsg:cMsg://host:port/subdomainType/namespace/?cmsgpassword=&lt;password&gt;& ... </b><p>
 * where the first "cMsg:" is optional. The subdomain is optional with
 * the default being cMsg (if nothing follows the host & port).
 * If the namespace is given, the subdomainType must be specified as well.
 * If the name server requires a password to connect, this can be specified by
 * ?cmsgpassword=&lt;password&gt; immediately after the namespace. It may also be
 * included later as one of several optional key-value pairs specified.
 *
 * If "myUDL" is a list of UDLs, the first valid one is connected to. If that
 * server fails, this client will automatically failover to the next valid
 * UDL on the list. If this client attempts and fails to connect to each
 * UDL on the list, an error is returned.
 *
 * The argument "myName" is the client's name and may be required to be
 * unique depending on the subdomainType.
 * The argument "myDescription" is an arbitrary string used to describe the
 * client.
 * If successful, this routine fills the argument "domainId", which identifies
 * the connection uniquely and is required as an argument by many other routines.
 *
 * This routine mainly does the UDL parsing. The actual connecting
 * to the name server is done in "connectDirect".
 * 
 * @param myUDL the Universal Domain Locator used to uniquely identify the cMsg
 *              server to connect to
 * @param myName name of this client
 * @param myDescription description of this client
 * @param UDLremainder partially parsed (initial cMsg:domainType:// stripped off)
 *                     UDL which gets passed down from the top API level,
 *                     (not used in this routine/domain)
 * @param domainId pointer to void pointer which gets filled with a unique id referring
 *                 to this connection.
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_FORMAT if UDLremainder arg is not in the proper format or is NULL
 * @returns CMSG_OUT_OF_MEMORY if the allocating memory failed or not enough spaces in
 *                             statically allocated arrays
 * @returns CMSG_TIMEOUT if timed out of wait for response to multicast
 * @returns CMSG_SOCKET_ERROR if udp socket for multicasting could not be created,
 *                            socket (TCP & UDP) to server could not be created or connected (UDP),
 *                            or socket options could not be set
 * 
 * @returns CMSG_NETWORK_ERROR if no connection to the name or domain servers can be made, or
 *                             a communication error with server occurs (can't read or write), or
 *                             a host name could not be resolved
 */
int cmsg_cmsg_connect(const char *myUDL, const char *myName, const char *myDescription,
                      const char *UDLremainder, void **domainId) {
        
  intptr_t index; /* int the size of a ptr (for casting) */
  int i, len, err;
  int failoverIndex=0, gotConnection = 0;
  char temp[CMSG_MAXHOSTNAMELEN];
  cMsgDomainInfo *domain;
  codaIpList *ipList = NULL, *orderedIpList = NULL;
  codaIpAddr *ipAddrs = NULL;

  /* handle memory, keep track of which functions are being called */
  cMsgMemoryMutexLock();

  /* initialize static memory once */
  if (!oneTimeInitialized) {
    for (i=0; i<CMSG_CONNECT_PTRS_ARRAY_SIZE; i++) {
        connectPointers[i] = NULL;
    }
    oneTimeInitialized = 1;
  }
    
  /* look for space in static array to keep connection ptr */
  index = -1;
  if (connectPtrsCounter >= CMSG_CONNECT_PTRS_ARRAY_SIZE) {
      connectPtrsCounter = 0;
  }
  
  tryagain:
  for (i=connectPtrsCounter; i<CMSG_CONNECT_PTRS_ARRAY_SIZE; i++) {
      /* if this index is unused ... */
      if (connectPointers[i] == NULL) {
          connectPtrsCounter++;
          index = i;
          break;
      }
  }

  /* there may be available slots we missed */
  if (index < 0 && connectPtrsCounter > 0) {
      connectPtrsCounter = 0;
      goto tryagain;
  }
 
  cMsgMemoryMutexUnlock();

  /* if no slots available .. */
  if (index < 0) {
      return(CMSG_OUT_OF_MEMORY);
  }


  /* allocate struct to hold connection info */
  domain = (cMsgDomainInfo *) calloc(1, sizeof(cMsgDomainInfo));
  if (domain == NULL) {
      return(CMSG_OUT_OF_MEMORY);
  }
  cMsgDomainInit(domain);

  /* allocate memory for message-sending buffer */
  domain->msgBuffer     = (char *) malloc((size_t)initialMsgBufferSize);
  domain->msgBufferSize = initialMsgBufferSize;
  if (domain->msgBuffer == NULL) {
      cMsgDomainFree(domain);
      free(domain);
      return(CMSG_OUT_OF_MEMORY);
  }

  /* store our host's name */
  gethostname(temp, CMSG_MAXHOSTNAMELEN);
  domain->myHost = strdup(temp);

  /* store names, can be changed until server connection established */
  domain->name        =  strdup(myName);
  domain->udl         =  strdup(myUDL);
  domain->description =  strdup(myDescription);

  connectPointers[index] = (void *)domain;
  
  /* Parse the UDL */
  if ( (err = cmsg_cmsg_setUDL((void *)index, myUDL, UDLremainder)) != CMSG_OK ) {
      cMsgDomainFree(domain);
      free(domain);
      cMsgMemoryMutexLock();
      connectPointers[index] = NULL;
      cMsgMemoryMutexUnlock();
      return(err);
  }


  /*-------------------------*/
  /* Make a real connection. */
  /*-------------------------*/

  /* Go through the UDL's until one works */
  failoverIndex = -1;
  do {
    /* try next UDL in list */
    failoverIndex++;
    /* copy this UDL's specifics into main structure */
    cMsgParsedUDLCopy(&domain->currentUDL, &domain->failovers[failoverIndex]);

    /* connect using that UDL info */
/*printf("\nTrying to connect with UDL = %s\n", domain->failovers[failoverIndex].udl);*/
    if (domain->currentUDL.mustMulticast) {
        free(domain->currentUDL.nameServerHost);
        domain->currentUDL.nameServerHost = NULL;
/*printf("Trying to connect with Multicast\n"); */
        err = connectWithMulticast(domain, &ipList, &domain->currentUDL.nameServerPort);
        if (err != CMSG_OK || ipList == NULL) {
/*printf("Error trying to connect with Multicast, err = %d\n", err);*/
            cMsgParsedUDLFree(&domain->currentUDL);
            continue;
        }

        /* Get local network info if not already done */
        if (ipAddrs == NULL) {
            cMsgNetGetNetworkInfo(&ipAddrs, NULL);
        }

        /* Order the IP list according to the given preferred subnet, if any */
        orderedIpList = cMsgNetOrderIpAddrs(ipList, ipAddrs, domain->currentUDL.subnet);
    }

    err = connectDirect(domain, (void *) index, orderedIpList);
    cMsgNetFreeAddrList(ipList);
    cMsgNetFreeAddrList(orderedIpList);
    if (err != CMSG_OK) {
        cMsgParsedUDLFree(&domain->currentUDL);
    }
    else {
      domain->failoverIndex = failoverIndex;
      gotConnection = 1;
      /* Store the host & port we used to make a connection
       * in the form of a server name (host:port) which will
       * be useful later. */
      len = (int)strlen(domain->currentUDL.nameServerHost) +
                   cMsgNumDigits(domain->currentUDL.nameServerPort, 0) + 1;
      domain->currentUDL.serverName = (char *)malloc((size_t) (len+1));
      if (domain->currentUDL.serverName == NULL) {
        cMsgDomainFree(domain);
        free(domain);
        cMsgMemoryMutexLock();
        connectPointers[index] = NULL;
        cMsgMemoryMutexUnlock();
        cMsgNetFreeIpAddrs(ipAddrs);
        return(CMSG_OUT_OF_MEMORY);
      }
      sprintf(domain->currentUDL.serverName, "%s:%d",domain->currentUDL.nameServerHost,
              domain->currentUDL.nameServerPort);
      domain->currentUDL.serverName[len] = '\0';
/*printf("cmsg_cmsg_connect: Connected, domainId = %p, domain = %p\n", domainId, domain);*/
      break;
    }
    
  } while (failoverIndex < domain->failoverSize - 1);

  /* Free up local network info */
  cMsgNetFreeIpAddrs(ipAddrs);

  if (!gotConnection) {
    cMsgDomainFree(domain);
    free(domain);
    cMsgMemoryMutexLock();
    connectPointers[index] = NULL;
    cMsgMemoryMutexUnlock();
    return(err);
  }        

  /* connection is complete */
  *domainId = (void *) index;

  /* install default shutdown handler (exits program) */
  cmsg_cmsg_setShutdownHandler((void *)domain, defaultShutdownHandler, NULL);
  
  domain->gotConnection = 1;

  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/


/**
 * This routine is called to reconnect to a cMsg domain. If there is no
 * current connection, this is like calling cmsg_cmsg_connect except that
 * all subscriptions and local threads are preserved between connections.
 * If there is a current connection, it is broken and the keep alive thread
 * handles creating a new connection. This can be effectively used by first
 * calling cmsg_cmsg_setUDL and then this function.
 *
 * @param domainId id of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the connection has already been closed with a call to disconnect
 * @returns CMSG_OUT_OF_MEMORY if the allocating memory failed
 * @returns CMSG_TIMEOUT if timed out of wait for response to multicast
 * @returns CMSG_SOCKET_ERROR if udp socket for multicasting could not be created,
 *                            socket (TCP & UDP) to server could not be created or connected (UDP),
 *                            or socket options could not be set
 * @returns CMSG_NETWORK_ERROR if no connection to the name or domain servers can be made, or
 *                             a communication error with server occurs (can't read or write), or
 *                             a host name could not be resolved
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed when trying to
 *                               resubscribe to subscriptions
 */
int cmsg_cmsg_reconnect(void *domainId) {
        
    intptr_t index;
    int len, err=CMSG_OK, outGoing[2];
    int failoverUDLCount = 0, failoverIndex=0;
    cMsgDomainInfo *domain;
    codaIpList *ipList = NULL, *orderedIpList = NULL;
    codaIpAddr *ipAddrs = NULL;

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) {
        return(CMSG_BAD_ARGUMENT);
    }

    /* make sure domain is usable */
    if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "**cmsg_cmsg_reconnect: IN, index = %d, domain = %p\n", (int)index, domain);
    }

    /* Cannot run this with other functions */
    cMsgConnectWriteLock(domain);
  
    /*-------------------------*/
    /* Make a real connection. */
    /*-------------------------*/

    /* If we're connected, and the UDL is the same, don't do anything.
     * If the UDL is different, disconnect and let the keep alive thread
     * failover to the first UDL on the list. */
    if (domain->gotConnection) {
/*printf("  cmsg_cmsg_reconnect: no connection, partialShutdown has already been run, reconnect\n");*/
    /* else if we're already connected ... */
        if ( domain->currentUDL.udl != NULL   &&
            !domain->currentUDL.mustMulticast &&
            strcmp(domain->currentUDL.udl, domain->failovers[0].udl) == 0) {
            
/*printf("  cmsg_cmsg_reconnect: already connected to this UDL and not multicasting, so return\n");*/
            cMsgConnectWriteUnlock(domain);
            cMsgCleanupAfterUsingMem((int)index);
            return(CMSG_OK);
        }
/*printf("  cmsg_cmsg_reconnect: already connected so now doing a partialShutdown then a connection\n");*/

        /* Don't allow failover to cloud. Go to the first UDL on the failover list. */
        domain->currentUDL.failover = CMSG_FAILOVER_ANY;
        domain->failoverIndex = -1;

        /* Tell server to disconnect from us, our keep alive thread will do the rest */
        outGoing[0] = htonl(4);                      /* size of msg */
        outGoing[1] = htonl(CMSG_SERVER_DISCONNECT); /* message id server */

        /* make send socket communications thread-safe */
        cMsgSocketMutexLock(domain);
  
        /* send int */
        if (cMsgNetTcpWrite(domain->sendSocket, (char*) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
            /* if there is an error we are most likely in the process of disconnecting */
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                fprintf(stderr, "  cmsg_cmsg_reconnect: write failure, but continue\n");
            }
        }

        cMsgSocketMutexUnlock(domain);
    }
    
    /* If we're not connected, the keep alive thread terminated the connection
     * and no failover happened (disconnect NOT called by user).
     * partialShutdown has been run and the keep alive thread has hit
     * bottom and is waiting for a new connection ... */
    else {
        /* Go through the UDL's until one works */
        failoverIndex = -1;
        do {
            /* try next UDL in list */
            failoverIndex++;
            /* copy this UDL's specifics into main structure */
            cMsgParsedUDLCopy(&domain->currentUDL, &domain->failovers[failoverIndex]);
    
            /* connect using that UDL info */
            /*printf("  cmsg_cmsg_reconnect: trying to connect with UDL = %s\n", domain->failovers[failoverIndex].udl);*/
            if (domain->currentUDL.mustMulticast) {
                free(domain->currentUDL.nameServerHost);
                domain->currentUDL.nameServerHost = NULL;
                /*printf("Trying to connect with Multicast\n"); */
                err = connectWithMulticast(domain, &ipList,
                                           &domain->currentUDL.nameServerPort);
                if (err != CMSG_OK || ipList == NULL) {
                    /*printf("Error trying to connect with Multicast, err = %d\n", err);*/
                    continue;
                }

                /* Get local network info if not already done */
                if (ipAddrs == NULL) {
                    cMsgNetGetNetworkInfo(&ipAddrs, NULL);
                }

                /* Order the IP list according to the given preferred subnet, if any */
                orderedIpList = cMsgNetOrderIpAddrs(ipList, ipAddrs, domain->currentUDL.subnet);
            }

            err = reconnect(domainId, orderedIpList);
            cMsgNetFreeAddrList(ipList);
            cMsgNetFreeAddrList(orderedIpList);

            if (err == CMSG_OK) {
                domain->failoverIndex = failoverIndex;
                domain->gotConnection = 1;
                /* Store the host & port we used to make a connection
                 * in the form of a server name (host:port) which will
                 * be useful later. */
                len = (int) strlen(domain->currentUDL.nameServerHost) +
                             cMsgNumDigits(domain->currentUDL.nameServerPort, 0) + 1;
                domain->currentUDL.serverName = (char *)malloc((size_t) (len+1));
                if (domain->currentUDL.serverName == NULL) {
                    cMsgConnectWriteUnlock(domain);
                    cMsgCleanupAfterUsingMem((int)index);
                    cMsgNetFreeIpAddrs(ipAddrs);
                    return CMSG_OUT_OF_MEMORY;
                }
                sprintf(domain->currentUDL.serverName, "%s:%d",domain->currentUDL.nameServerHost,
                        domain->currentUDL.nameServerPort);
                domain->currentUDL.serverName[len] = '\0';
                /*printf("  cmsg_cmsg_reconnect: connected!!\n");*/
                /* restore subscriptions which were lost when server died */
                err = restoreSubscriptions(domain);
                if (err != CMSG_OK) {
                    /*printf("Error restoring subscriptions, connection broken again, err = %d\n", err);*/
                    domain->gotConnection = 0;
                    continue;
                }

                break;
            }
    
        } while (failoverIndex < failoverUDLCount - 1);

        /* Free up local network info */
        cMsgNetFreeIpAddrs(ipAddrs);
    }


    if (!domain->gotConnection) {
        cMsgConnectWriteUnlock(domain);
        cMsgCleanupAfterUsingMem((int)index);
        return(err);
    }
  
    cMsgConnectWriteUnlock(domain);
    cMsgCleanupAfterUsingMem((int)index);

    return(CMSG_OK);
}
/*-------------------------------------------------------------------*/


/**
 * Routine to multicast in order to find the domain server from this client.
 * Once the server is found and returns its host and port, a direct connection
 * can be made.
 *
 * @param domainId id of the domain connection
 * @param hostList pointer to be filled in with ordered list of cMsg server IP addresses
                   found through multicasting
 * @param port     pointer to be filled in with cMsg server TCP port
                   found through multicasting
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_FORMAT if UDLremainder arg is not in the proper format or is NULL
 * @returns CMSG_OUT_OF_MEMORY if the allocating memory failed
 * @returns CMSG_TIMEOUT if timed out of wait for response to multicast
 * @returns CMSG_SOCKET_ERROR if udp socket for multicasting could not be created
 */
static int connectWithMulticast(cMsgDomainInfo *domain, codaIpList **hostList, int *port) {
    char   *buffer;
    int    err, status, len, passwordLen, sockfd, isLocal, localPort;
    int    outGoing[6], multicastTO=0, gotResponse=0, off=0;
    unsigned char ttl = 32;

    pthread_t rThread, bThread;
    thdArg    rArg,    bArg;
    
    struct timespec wait, time;
    struct sockaddr_in servaddr, localaddr;
     
    /*------------------------
    * Talk to cMsg server
    *------------------------*/
    
    /* create UDP socket for multicasting */
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
        if (bind(sockfd, (struct sockaddr *)&localaddr, sizeof(localaddr)) == 0) {
            break;
        }
    }
    /* If  bind always failed, then ephemeral port will be used. */

    memset((void *)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
/*printf("Multicast thd uses port %hu\n", ((uint16_t)domain->currentUDL.nameServerUdpPort));*/
    servaddr.sin_port   = htons((uint16_t) (domain->currentUDL.nameServerUdpPort));
    /* send packet to multicast address */
    if ( (err = cMsgNetStringToNumericIPaddr(CMSG_MULTICAST_ADDR, &servaddr)) != CMSG_OK ) {
        /* an error should never be returned here */
        close(sockfd);
        return(err);
    }

    /*
     * We send these items explicitly:
     *   1) 3 magic ints for connection protection
     *   2) ints describing action to be done + password len
     *   3) password
     * The host we're sending from gets sent for free
     * as does the UDP port we're sending from.
     */
    passwordLen = 0;
    if (domain->currentUDL.password != NULL) {
      passwordLen = (int)strlen(domain->currentUDL.password);
    }
    /* magic ints */
    outGoing[0] = htonl(CMSG_MAGIC_INT1);
    outGoing[1] = htonl(CMSG_MAGIC_INT2);
    outGoing[2] = htonl(CMSG_MAGIC_INT3);
    /* cMsg version */
    outGoing[3] = htonl(CMSG_VERSION_MAJOR);
    /* type of message */
    outGoing[4] = htonl(CMSG_DOMAIN_MULTICAST);
    /* length of "password" string */
    outGoing[5] = htonl((uint32_t)passwordLen);

    /* copy data into a single buffer */
    len = sizeof(outGoing);
    buffer = (char *) malloc((size_t) (len + passwordLen));
    if (buffer == NULL) {
      close(sockfd);
      return CMSG_OUT_OF_MEMORY;
    }
    memcpy(buffer, (void *)outGoing, (size_t)len);
    if (passwordLen > 0) {
      memcpy(buffer+len, (const void *)domain->currentUDL.password, (size_t)passwordLen);
      len += passwordLen;
    }
        
    /* create and start a thread which will receive any responses to our multicast */
    memset((void *)&rArg.addr, 0, sizeof(rArg.addr));
    rArg.len             = (socklen_t) sizeof(rArg.addr);
    rArg.port            = 0;
    rArg.sockfd          = sockfd;
    rArg.addr.sin_family = AF_INET;
    
    status = pthread_create(&rThread, NULL, receiverThd, (void *)(&rArg));
    if (status != 0) {
        cmsg_err_abort(status, "Creating multicast response receiving thread");
    }
    
    /* create and start a thread which will multicast every second */
    bArg.len       = (socklen_t) sizeof(servaddr);
    bArg.sockfd    = sockfd;
    bArg.paddr     = &servaddr;
    bArg.buffer    = buffer;
    bArg.bufferLen = len;
    
    status = pthread_create(&bThread, NULL, multicastThd, (void *)(&bArg));
    if (status != 0) {
        cmsg_err_abort(status, "Creating multicast sending thread");
    }
    
    /* Wait for a response. If multicastTO is given in the UDL, that is used.
     * The default wait or the wait if multicastTO is set to 0, is forever.
     * Round things to the nearest second since we're only multicasting a
     * message every second anyway.
     */
    multicastTO = domain->currentUDL.timeout;
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
            if (hostList != NULL) {
                *hostList = rArg.ipList;
            }
            if (port != NULL) {
                *port = rArg.port;
            }
/*printf("Response received w/TO, host = %s, port = %hu\n", rArg.ipList->addr, rArg.port);*/
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
 
/*printf("Wait forever for multicast to be answered\n");*/
        status = pthread_cond_wait(&cond, &mutex);
        if (status != 0) {
            cmsg_err_abort(status, "pthread_cond_timedwait");
        }
        gotResponse = 1;
        
        if (hostList != NULL) {
            *hostList = rArg.ipList;
        }
        if (port != NULL) {
            *port = rArg.port;
        }
/*printf("Response received no TO, host = %s, port = %hu\n", rArg.ipList->addr, rArg.port);*/

        status = pthread_mutex_unlock(&mutex);
        if (status != 0) {
            cmsg_err_abort(status, "pthread_mutex_lock");
        }
    }
    
    /* stop multicasting thread */
    pthread_cancel(bThread);
    sched_yield();
    
/*printf("-udp multi %d\n", sockfd);*/
    close(sockfd);
    free(buffer);
    
    if (!gotResponse) {
/*printf("Got no response\n");*/
        return(CMSG_TIMEOUT);
    }

    /* Record whether this server is local or not. */    
    cMsgNetNodeIsLocal(rArg.ipList->addr, &isLocal);
    domain->currentUDL.isLocal = isLocal;

    return(CMSG_OK);
}

/*-------------------------------------------------------------------*/

/**
 * This routine starts a thread to receive a return UDP packet from
 * the server due to our initial multicast.
 */
static void *receiverThd(void *arg) {
    int i, tcpPort, udpPort, ipLen, ipCount, magicInt[3];
    thdArg *threadArg = (thdArg *) arg;
    char buf[1024], *pchar;
    ssize_t len;
    codaIpList *listHead=NULL, *listEnd=NULL, *listItem;
    
    /* release resources when done */
    pthread_detach(pthread_self());
    
    nextPacket:
    while (1) {
        /* zero buffer */
        pchar = memset((void *)buf,0,1024);

        /* ignore error as it will be caught later */   
        len = recvfrom(threadArg->sockfd, (void *)buf, 1024, 0,
                       (SA *) &threadArg->addr, &(threadArg->len));
/*
printf("Multicast response from: %s, on port %hu, with msg len = %hd\n",
                inet_ntoa(threadArg->addr.sin_addr),
                ntohs(threadArg->addr.sin_port), len); 
*/
        /* server is sending 6 ints + string */
        if (len < 6*sizeof(int)) continue;

        /* The server is sending back its 1) port, 2) host name length, 3) host name */
        memcpy(&magicInt[0], pchar, sizeof(int));
        magicInt[0] = ntohl((uint32_t)magicInt[0]);
        pchar += sizeof(int);
        
        memcpy(&magicInt[1], pchar, sizeof(int));
        magicInt[1] = ntohl((uint32_t)magicInt[1]);
        pchar += sizeof(int);
        
        memcpy(&magicInt[2], pchar, sizeof(int));
        magicInt[2] = ntohl((uint32_t)magicInt[2]);
        pchar += sizeof(int);
        
        if ((magicInt[0] != CMSG_MAGIC_INT1) ||
            (magicInt[1] != CMSG_MAGIC_INT2) ||
            (magicInt[2] != CMSG_MAGIC_INT3))  {
printf("  Multicast response has wrong magic numbers, ignore packet\n");
          continue;
        }
       
        memcpy(&tcpPort, pchar, sizeof(int));
        tcpPort = ntohl((uint32_t)tcpPort);
        pchar += sizeof(int);
        
        memcpy(&udpPort, pchar, sizeof(int));
        udpPort = ntohl((uint32_t)udpPort);
        pchar += sizeof(int);
        
        memcpy(&ipCount, pchar, sizeof(int));
        ipCount = ntohl((uint32_t)ipCount);
        pchar += sizeof(int);
/*printf("  TCP port = %d, UDP port = %d, ip count = %d\n", tcpPort, udpPort, ipCount);*/
        
        if ( (tcpPort < 1024) || (tcpPort > 65535) || (ipCount < 0) || (ipCount > 50)) {
            printf("  bad port value, ignore packet\n");
            continue;
        }
        
        for (i=0; i < ipCount; i++) {
            
            /* Create address item */
            listItem = (codaIpList *) calloc(1, sizeof(codaIpList));
            if (listItem == NULL) {
                cMsgNetFreeAddrList(listHead);
                pthread_exit(NULL);
                return NULL;
            }
            
            /* First read in IP address len */
            memcpy(&ipLen, pchar, sizeof(int));
            ipLen = ntohl((uint32_t)ipLen);
            pchar += sizeof(int);
            
            /* IP address is in dot-decimal format */
            if (ipLen < 7 || ipLen > 20) {
                printf("  Multicast response has wrong format, ignore packet\n");
                goto nextPacket;
            }
            
            /* Put address into a list for later sorting */
            memcpy(listItem->addr , pchar, (size_t)ipLen);
            listItem->addr[ipLen] = 0;
            pchar += ipLen;
            
            /* Second read in corresponding broadcast or network address */
            memcpy(&ipLen, pchar, sizeof(int));
            ipLen = ntohl((uint32_t)ipLen);
            pchar += sizeof(int);
            
            if (ipLen < 7 || ipLen > 20) {
                printf("  Multicast response has wrong format, ignore packet\n");
                goto nextPacket;
            }
            
            memcpy(listItem->bAddr , pchar, (size_t)ipLen);
            listItem->bAddr[ipLen] = 0;
            pchar += ipLen;
            
/*printf("Found ip = %s, bcast = %s\n", listItem->addr, listItem->bAddr);*/
            
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
        threadArg->port   = tcpPort;
        threadArg->ipList = listHead;
        
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
 * every second in order to connect.
 */
static void *multicastThd(void *arg) {

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
    
    while (1) {
/*printf("Send multicast to cMsg server on socket %d\n", threadArg->sockfd);*/
      sendto(threadArg->sockfd, (void *)threadArg->buffer, threadArg->bufferLen, 0,
             (SA *) threadArg->paddr, threadArg->len);
      
      sleep(1);
    }
    
    pthread_exit(NULL);
    return NULL;
}


/*-------------------------------------------------------------------*/


/**
 * This routine is called by cmsg_cmsg_connect and does the real work of
 * connecting to the cMsg name server.
 *
 * @param domain pointer to connection information structure.
 * @param domainId connection id (index into static array)
 * @param ipList if multicasting, an ordered list of server IP addresses has been returned,
                 try them one-by-one until a connection is made
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_OUT_OF_MEMORY if the allocating memory failed
 * @returns CMSG_SOCKET_ERROR if socket (TCP & UDP) to server could not be created or connected (UDP),
 *                            or socket options could not be set
 * @returns CMSG_NETWORK_ERROR if host name could not be resolved or could not connect,
 *                             or a communication error with either server occurs (can't read or write).
 */
static int connectDirect(cMsgDomainInfo *domain, void *domainId, codaIpList *ipList) {

    int err, serverfd=0, uniqueClientKey;

    /* Block SIGPIPE for this and all spawned threads. */
    cMsgBlockSignals(domain);

    /*---------------------------------------------------------------*/
    /* connect & talk to cMsg name server to check if name is unique */
    /*---------------------------------------------------------------*/
    if (ipList != NULL) {
        /* try all IP addresses in list until one works */
        while (ipList != NULL) {
            /* first connect to server host & port (default send & rcv buf sizes) */
            err = cMsgNetTcpConnect(ipList->addr, NULL,
                                    (unsigned short) domain->currentUDL.nameServerPort,
                                    0, 0, 1, &serverfd, NULL);
            if (err != CMSG_OK) {
                /* if there is another address to try, try it */
                if (ipList->next != NULL) {
                    ipList = ipList->next;
                    continue;
                }
                /* if we've tried all addresses in the list, return an error */
                else {
                    cMsgRestoreSignals(domain);
                    return(err);
                }
            }
            
            /* quit loop if we've made a connection to server, store good address */
            if (domain->currentUDL.nameServerHost != NULL) {
                free(domain->currentUDL.nameServerHost);
            }
            domain->currentUDL.nameServerHost = strdup(ipList->addr);
            break;
        }
    }
    else {
        /* first connect to server host & port (default send & rcv buf sizes) */
        if ( (err = cMsgNetTcpConnect(domain->currentUDL.nameServerHost, NULL,
                                      (unsigned short) domain->currentUDL.nameServerPort,
                                      0, 0, 1, &serverfd, NULL)) != CMSG_OK) {
            cMsgRestoreSignals(domain);
            return(err);
        }
    }
    
    /* get host & port (domain->sendHost,sendPort) to send messages to */
    err = talkToNameServer(domain, serverfd, &uniqueClientKey);
    if (err != CMSG_OK) {
        cMsgRestoreSignals(domain);
        close(serverfd);
        return(err);
    }

    /* BUGBUG free up memory allocated in parseUDL & no longer needed */

    /* done talking to server */
    close(serverfd);

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "connectDirect: sendHost = %s, sendPort = %d\n",
                domain->currentUDL.nameServerHost, domain->sendPort);
    }

    /* make 2 connections to the domain server */
    err = connectToDomainServer(domain, domainId, uniqueClientKey, 0);
    if (err != CMSG_OK) {
        return(err);
    }

   return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine does the work of making 2 socket connections to the domain server
 * (inside cmsg name server). It also creates a UDP socket for the udpSend method.
 * This method does some tricky things due to the fact that users want to connect to
 * cMsg servers through ssh tunnels.
 *
 * @param domain pointer to connection information structure
 * @param domainId connection id (index into static array)
 * @param uniqueClientKey unique key identifing client to server when connecting
 * @param reconnecting are we connecting for the first time (0), or reconnecting (!=0)?
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_OUT_OF_MEMORY if the allocating memory failed
 * @returns CMSG_SOCKET_ERROR if socket (TCP & UDP) to server could not be created or connected (UDP),
 *                            or socket options could not be set
 * @returns CMSG_NETWORK_ERROR if host name could not be resolved or could not connect,
 *                             or a communication error with either server occurs (can't read or write).
 */
static int connectToDomainServer(cMsgDomainInfo *domain, void *domainId,
                                 int uniqueClientKey, int reconnecting) {

    int i, index, err, same, lastOption, sendLen, status, outGoing[5];
    const int size=CMSG_BIGSOCKBUFSIZE; /* bytes */
    struct sockaddr_in servaddr;
  
    /*--------------------------------------------------------------------------------------------
     * We need to do some tricky things due to the fact that people want to connect to
     * a cMsg server through SSH tunnels. The following cmd is needed to set up tunneling:
     *
     *      ssh -fNg -L <local port>:<cMsg-server-host>:<cMsg-server-port> localhost
     *
     * If we're using the default ports then the following 2 cmds need to be run:
     *
     *      ssh -fNg -L 45000:<remoteHost>:45000 localhost  // for cMsgNameServer
     *      ssh -fNg -L 45001:<remoteHost>:45001 localhost  // for domainServer
     *
     * The original connection to the cMsg name server can
     * specify its host in 2 different ways: 1) through multicasting and obtaining a list of
     * all the IP addresses of the responding server, or 2) spelling out the host in the UDL.
     *
     * In the first case, tunneling cannot be used in conjunction with multicasting. The
     * returned IP list from the server's udp listening thread is ordered with addresses
     * on any preferred local subnet (if any) first, those on local subnets next, and other
     * addresses last. A direct connection is made by trying these addresses one-by-one
     * with the successful one stored in currentUDL.nameServerHost.
     *
     * In the second case, whether tunneling is used or not, a successful direct connection
     * also stores the host used in that connection in currentParsedUDL.nameServerHost.
     *
     * Thus, in all cases, the host of the cMsg server and consequently its contained domain
     * server is found in currentUDL.nameServerHost.
     *
     * Now a word about ports:
     *
     *     The UDP port of the cMsgNameServer, used to accept multicasts, defaults to
     *     CMSG_NAME_SERVER_MULTICAST_PORT = 45000 but may be set to any valid value.
     *     The TCP port of the cMsgNameServer defaults to
     *     CMSG_NAME_SERVER_TCP_PORT = 45000 but may be set to any valid value.
     *     The TCP port of the contained DomainServer defaults to
     *     CMSG_NAME_SERVER_TCP_PORT + 1 = 45001 but may be set to any valid value.
     *
     *     If ssh tunneling/port-forwarding is used:
     *         The port used to connect to the name server must be the same as
     *         the local port specified in one of the tunneling ssh commands.
     *         And the port used to connect to the domain server must be the same as
     *         the local port specified in the other tunneling ssh command.
     *
     *     However, if tunneling is not used:
     *         The correct port of the domain server is given to the client by the
     *         server during the connect() routine (invisible to caller). It may also
     *         be specified in the UDL.
     *
     * The question is, since the cMsg software package does not always know if tunneling
     * has been used or not, how can it find the correct port for the connection to the
     * domain server (contained inside the name server)?
     *
     *     1) We do know that if we're multicasting then tunneling is not being used.
     *     2) If tunneling, the connection to the domain server must also go through
     *        an ssh tunnel. In that case, we cannot use the domainServerHost and
     *        domainServerPort returned by the name server during connect() since we
     *        need to use the same host & port specified when creating the tunnels.
     *
     * The strategy here is to test the following options for the domain port value:
     *      option 1) use the domain port specified explicitly in the UDL.
     *                This will work when not tunneling. It will also work for tunneling if
     *                the user was clever enough to use the local domain server tunneling port
     *                in the UDL, or
     *      option 2) use the name server port + 1 (default domain port), or
     *      option 3) use port returned by name server (no SSH tunnels used)
     * in that order.
     *--------------------------------------------------------------------------------------------*/
    
    
    unsigned short ports[3];
    char *host, c;
    int   isSshTunneling[3];

    lastOption = 2;

    ports[0] = (unsigned short)domain->currentUDL.domainServerPort;
    ports[1] = (unsigned short)(domain->currentUDL.nameServerPort + 1);
    ports[2] = (unsigned short)domain->sendPort;
    
    host = domain->currentUDL.nameServerHost;

    /*
    for (i=0; i<3; i++) {
        printf("connectToDomainServer:  host = %s, and port = %hu\n", host, ports[i]);
    }
    */
    
    /* We can simplify the logic if we can determine if a particular option
     * means we're tunneling or not. Assuming we actually make a connection
     * for a particular option, we can draw the following conclusions: */
    for (i=0; i < 3; i++) {
        /* assume no ssh tunneling */
        isSshTunneling[i] = 0;

        /* multicasting bypasses tunneling */
        if (domain->currentUDL.mustMulticast) {
            isSshTunneling[i] = 0;
        }
        /* 3rd option cannot be tunneling */
        else if (i == 2) {
            isSshTunneling[i] = 0;
        }
        /* If the server's actual domain port is different than the one
         * we used to connect to the domain server, assume tunneling. */
        else if (ports[i] != domain->sendPort) {
            isSshTunneling[i] = 1;
        }
        else {
            /* is the node returned by the server and the UDL node the same? */
            err = cMsgNetNodeSame(domain->sendHost, host, &same);
            /* can't resolve host name(s) so probably tunneling */
            if (err != CMSG_OK) {
                isSshTunneling[i] = 1;
            }
            /* If the actual host the server is running on is different than
             * the one we used to connect, assume tunneling. */
            else if (!same) {
                isSshTunneling[i] = 1;
            }
        }
    }

    index = 0;
    /* if no domain server port explicitly given in UDL, skip option #1 */
    if (domain->currentUDL.domainServerPort < 1) {
        index = 1;
    }
 
    /* first send magic #s to server which identifies us as real cMsg client */
    outGoing[0] = htonl(CMSG_MAGIC_INT1);
    outGoing[1] = htonl(CMSG_MAGIC_INT2);
    outGoing[2] = htonl(CMSG_MAGIC_INT3);
    /* then send our server-given id */
    outGoing[3] = htonl((uint32_t)uniqueClientKey);

    /* see if we can connect using 1 of the 3 possible means */
    domain->sendSocket = -1;
    domain->keepAliveSocket = -1;

    do {

        /* create the 2 sockets through which all future communication occurs */
        for (i=index; i < 3; i++) {
            /* create sending & receiving socket and store (128K rcv buf, 128K send buf) */
            if ( (err = cMsgNetTcpConnect(host, NULL, ports[i],
                  CMSG_BIGSOCKBUFSIZE, CMSG_BIGSOCKBUFSIZE, 1,
                  &domain->sendSocket, &domain->localPort)) != CMSG_OK) {

                domain->localPort  = 0;
                domain->sendSocket = -1;
                continue;
            }
            
            /* create keep alive socket and store (default send & rcv buf sizes) */
            if ( (err = cMsgNetTcpConnect(host, NULL, ports[i],
                  0, 0, 1, &domain->keepAliveSocket, NULL)) != CMSG_OK) {

                close(domain->sendSocket);
                domain->localPort = 0;
                domain->sendSocket = -1;
                domain->keepAliveSocket = -1;
                continue;
            }
            
            index = i;
            /*
            printf("connectToDomainServer 2: setting index to %d\n", index);
            printf("connectToDomainServer 2: using host = %s, and port = %hu\n", host, ports[i]);
            */
            break;
        }
        
        
        /* if no valid connections made, try again or quit */
        if (domain->sendSocket < 0 || domain->keepAliveSocket < 0) {            
            if (index >= lastOption) {
                err = CMSG_NETWORK_ERROR;
                goto error;
            }
            else {
                continue;
            }
        }
/*printf("connectToDomainServer: created sockets to connection handler with method #%d\n", (index + 1));*/

        /* send data over message TCP socket */
        outGoing[4] = htonl(1); /* 1 means message socket */
        sendLen = cMsgNetTcpWrite(domain->sendSocket, (void *) outGoing, sizeof(outGoing));
        if (sendLen != sizeof(outGoing)) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr,
                    "connectToDomainServer: error sending data over message socket, %s\n", strerror(errno));
            if (index >= lastOption) {
                err = CMSG_NETWORK_ERROR;
                goto error;
            }
            else {
                continue;
            }
        }

        /* Expecting one byte in return to confirm connection and make ssh port
         * forwarding fails in a timely way if no server on the other end.*/
        if (cMsgNetTcpRead(domain->sendSocket, (void *)&c, 1) != 1) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                fprintf(stderr, "connectToDomainServer: error reading message socket response byte\n");
            }
            if (index >= lastOption) {
                err = CMSG_NETWORK_ERROR;
                goto error;
            }
            else {
                continue;
            }
        }

        /* send magic #s over keep alive TCP socket */
        outGoing[4] = htonl(2); /* 2 means keepalive socket */
        sendLen = cMsgNetTcpWrite(domain->keepAliveSocket, (void *) outGoing, sizeof(outGoing));
        if (sendLen != sizeof(outGoing)) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr,
                    "connectToDomainServer: error sending data over keep alive socket, %s\n", strerror(errno));
            if (index >= lastOption) {
                err = CMSG_NETWORK_ERROR;
                goto error;
            }
            else {
                continue;
            }
        }

        /* Expecting one byte in return to confirm connection and make ssh port
         * forwarding fails in a timely way if no server on the other end.*/
        if (cMsgNetTcpRead(domain->keepAliveSocket, (void *)&c, 1) != 1) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                fprintf(stderr, "connectToDomainServer: error reading keepAlive socket response byte\n");
            }
            if (index >= lastOption) {
                err = CMSG_NETWORK_ERROR;
                goto error;
            }
            else {
                continue;
            }
        }


        
        /*----------------------------------------------------------------------------
         * If we're tunneling, we do NOT want to create a udp socket - just ignore it,
         * but the user should never use it.
         *----------------------------------------------------------------------------*/
        if (!isSshTunneling[index]) {
    
            /* create sending UDP socket */
            if ((domain->sendUdpSocket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
                if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr,
                        "connectToDomainServer: error creating UDP socket, %s\n", strerror(errno));
                if (index >= lastOption) {
                    err = CMSG_SOCKET_ERROR;
                    goto error;
                }
                else {
                    continue;
                }
            }
        
            /* set send buffer size */
            err = setsockopt(domain->sendUdpSocket, SOL_SOCKET, SO_SNDBUF, (char*) &size, sizeof(size));
            if (err < 0) {
                if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr,
                        "connectToDomainServer: setsockopt error\n");
                if (index >= lastOption) {
                    err = CMSG_SOCKET_ERROR;
                    goto error;
                }
                else {
                    continue;
                }
            }
        
#ifndef Darwin
            memset((void *)&servaddr, 0, sizeof(servaddr));
            servaddr.sin_family = AF_INET;
            servaddr.sin_port   = htons((uint16_t)domain->sendUdpPort);
        
            if ( (err = cMsgNetStringToNumericIPaddr(domain->sendHost, &servaddr)) != CMSG_OK ) {
                if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr,
                        "connectToDomainServer: host name error\n");
                if (index >= lastOption) {
                    /* err = CMSG_BAD_ARGUMENT if domain->sendHost is null
                             CMSG_OUT_OF_MEMORY if out of memory
                             CMSG_NETWORK_ERROR if the numeric address could not be obtained/resolved
                    */
                    goto error;
                }
                else {
                    continue;
                }
            }
            
            /* limits incoming packets to the host and port given - protection against port scanning */
            err = connect(domain->sendUdpSocket, (SA *) &servaddr, (socklen_t) sizeof(servaddr));
            if (err < 0) {
                if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr,
                        "connectToDomainServer: UDP connect error\n");
                if (index >= lastOption) {
                    err = CMSG_SOCKET_ERROR;
                    goto error;
                }
                else {
                    continue;
                }
            }
#endif
        }
    
        /* Launch 3 threads to handle communication on sockets. */
        if (!reconnecting) {
            /* create pend thread and start listening on send socket */
            status = pthread_create(&domain->pendThread, NULL, cMsgClientListeningThread, domainId);
            if (status != 0) {
                cmsg_err_abort(status, "Creating message listening thread");
            }
        
            /* create thread to read periodic keep alives (monitor data) from server */
            status = pthread_create(&domain->keepAliveThread, NULL, keepAliveThread, domainId);
            if (status != 0) {
                cmsg_err_abort(status, "Creating keep alive thread");
            }
        
            /* create thread to send periodic keep alives (monitor data) to server */
            status = pthread_create(&domain->updateServerThread, NULL, updateServerThread, domainId);
            if (status != 0) {
                cmsg_err_abort(status, "Creating update server thread");
            }
        }

        /* if we managed to get here, the connections were successful */
        break;
        
    } while(index++ < lastOption);

    return(CMSG_OK);

error:
    
    if (!reconnecting) cMsgRestoreSignals(domain);
    close(domain->sendSocket);
    close(domain->sendUdpSocket);
    close(domain->keepAliveSocket);
    domain->localPort  = 0;
    domain->sendSocket = -1;
    domain->sendUdpSocket = -1;
    domain->keepAliveSocket = -1;

    return (err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine is called by the keepAlive thread upon the death of the
 * cMsg server in an attempt to failover to another server whose UDL was
 * given in the original call to connect(). It is already protected
 * by cMsgConnectWriteLock() when called in keepAlive thread.
 *
 * @param domain pointer to connection info structure
 * @param ipList if multicasting, an ordered list of server IP addresses has been returned,
 *               try them one-by-one until a connection is made
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad argument
 * @returns CMSG_OUT_OF_MEMORY if the allocating memory failed
 * @returns CMSG_SOCKET_ERROR if socket options could not be set
 * @returns CMSG_NETWORK_ERROR if no connection to the name or domain servers can be made,
 *                             or a communication error with either server occurs.
 */
static int reconnect(void *domainId, codaIpList *ipList) {

    intptr_t index;
    int err, serverfd=0, uniqueClientKey;
    cMsgDomainInfo *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
    domain = connectPointers[index];
    if (domain == NULL) return(CMSG_BAD_ARGUMENT);

    /* Keep all running threads, close sockets, remove syncSends, send&Gets, sub&Gets. */
    partialShutdown(domainId, 1);
  
    /*-----------------------------------------------------*/
    /*             talk to cMsg name server                */
    /*-----------------------------------------------------*/
    if (ipList != NULL) {
        /* try all IP addresses in list until one works */
        while (ipList != NULL) {
            /* first connect to server host & port (default send & rcv buf sizes) */
            err = cMsgNetTcpConnect(ipList->addr, NULL,
                                    (unsigned short) domain->currentUDL.nameServerPort,
                                    0, 0, 1, &serverfd, NULL);
            
            ipList = ipList->next;
            
            if (err != CMSG_OK) {
                /* if there is another address to try, try it */
                if (ipList != NULL) {
                    continue;
                }
                /* if we've tried all addresses in the list, return an error */
                else {
                    return(err);
                }
            }
            
            /* quit loop if we've made a connection to server, store good address */
            if (domain->currentUDL.nameServerHost != NULL) {
                free(domain->currentUDL.nameServerHost);
            }
            domain->currentUDL.nameServerHost = strdup(ipList->addr);
            break;
        }
    }
    else {
        /* first connect to server host & port (default send & rcv buf sizes) */
        if ( (err = cMsgNetTcpConnect(domain->currentUDL.nameServerHost, NULL,
            (unsigned short) domain->currentUDL.nameServerPort,
                                      0, 0, 1, &serverfd, NULL)) != CMSG_OK) {
            return(err);
        }
    }
      
    /* get host & port (domain->sendHost,sendPort) to send messages to */
    err = talkToNameServer(domain, serverfd, &uniqueClientKey);
    if (err != CMSG_OK) {
        close(serverfd);
        return(err);
    }

    /* done talking to server */
    close(serverfd);

    /* make 2 connections to the domain server */
    err = connectToDomainServer(domain, domainId, uniqueClientKey, 1);
    if (err != CMSG_OK) {
        return(err);
    }

   domain->gotConnection = 1;
     
   return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine sends a msg to the specified cMsg domain server. It is called
 * by the user through cMsgSend() given the appropriate UDL. It is asynchronous
 * and should rarely block. It will only block if the TCP protocol is used and
 * the cMsg domain server has
 * reached it maximum number of request-handling threads and each of those threads
 * has a cue which is completely full. In this domain cMsgFlush() does nothing and
 * does not need to be called for the message to be sent immediately.<p>
 *
 * This version of this routine uses writev to write all data in one write call.
 * Another version was tried with many writes (one for ints and one for each
 * string), but the performance died sharply.
 *
 * @param domainId id of the domain connection
 * @param vmsg pointer to a message structure
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if message's payload changed while sending
 * @returns CMSG_BAD_ARGUMENT if disconnect already called, or the id is NULL,
 *                            or message argument is NULL or has NULL subject or type
 * @returns CMSG_OUT_OF_RANGE  if the message is too large to be sent by UDP
 * @returns CMSG_OUT_OF_MEMORY if the allocating memory for message buffer failed
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement sending
 *                               messages
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */
int cmsg_cmsg_send(void *domainId, void *vmsg) {

  char *payloadText;
  intptr_t index; /* int the size of a ptr (for casting) */
  uint32_t  len, lenSubject, lenType, lenPayloadText, lenText, lenByteArray, outGoing[16];
  int err, fd;
  uint32_t highInt, lowInt;
  ssize_t sendLen;
  cMsgMessage_t *msg = (cMsgMessage_t *) vmsg;
  cMsgDomainInfo *domain;
  int64_t llTime;
  struct timespec now;

  /* check args */
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);

  if ( (cMsgCheckString(msg->subject) != CMSG_OK ) ||
       (cMsgCheckString(msg->type)    != CMSG_OK )    ) {
      return(CMSG_BAD_ARGUMENT);
  }

  index = (intptr_t) domainId;
  if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

  /* make sure domain is usable */
  if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

  if (!domain->hasSend) {
      cMsgCleanupAfterUsingMem((int)index);
      return(CMSG_NOT_IMPLEMENTED);
  }

  /* send using udp socket */
  if (msg->udpSend) {
    return udpSend(domain, index, msg);
  }

  fd = domain->sendSocket;

  tryagain:

  while (1) {
    err = CMSG_OK;

    /* Cannot run this while connecting/disconnecting */
    cMsgConnectReadLock(domain);

    if (domain->gotConnection != 1) {
      cMsgConnectReadUnlock(domain);
      err = CMSG_LOST_CONNECTION;
      break;
    }

    if (msg->text == NULL) {
      lenText = 0;
    }
    else {
      lenText = (int)strlen(msg->text);
    }

    /* time message sent (right now) */
    clock_gettime(CLOCK_REALTIME, &now);
    /* convert to milliseconds */
    llTime  = ((int64_t)now.tv_sec * 1000) +
              ((int64_t)now.tv_nsec/1000000);
    
    /* update history here */
    err = cMsgAddHistoryToPayloadText(vmsg, domain->name, domain->myHost,
                                      llTime, &payloadText);
    if (err != CMSG_OK) {
      cMsgConnectReadUnlock(domain);
      cMsgCleanupAfterUsingMem((int)index);
      return(err);
    }
    
    /* length of "payloadText" string */
    if (payloadText == NULL) {
      lenPayloadText = 0;
    }
    else {
      lenPayloadText = (int)strlen(payloadText);
    }

    /* message id (in network byte order) to domain server */
    outGoing[1] = htonl(CMSG_SEND_REQUEST);
    /* reserved for future use */
    outGoing[2] = 0;
    /* user int */
    outGoing[3] = htonl((uint32_t)msg->userInt);
    /* system msg id */
    outGoing[4] = htonl((uint32_t)msg->sysMsgId);
    /* sender token */
    outGoing[5] = htonl((uint32_t)msg->senderToken);
    /* bit info */
    outGoing[6] = htonl((uint32_t)msg->info);

    highInt = (uint32_t) ((llTime >> 32) & 0x00000000FFFFFFFF);
    lowInt  = (uint32_t) (llTime & 0x00000000FFFFFFFF);
    outGoing[7] = htonl(highInt);
    outGoing[8] = htonl(lowInt);

    /* user time */
    llTime  = ((int64_t)msg->userTime.tv_sec * 1000) +
              ((int64_t)msg->userTime.tv_nsec/1000000);
    highInt = (uint32_t) ((llTime >> 32) & 0x00000000FFFFFFFF);
    lowInt  = (uint32_t) (llTime & 0x00000000FFFFFFFF);
    outGoing[9]  = htonl(highInt);
    outGoing[10] = htonl(lowInt);

    /* length of "subject" string */
    lenSubject   = (uint32_t)strlen(msg->subject);
    outGoing[11] = htonl(lenSubject);
    
    /* length of "type" string */
    lenType      = (uint32_t)strlen(msg->type);
    outGoing[12] = htonl(lenType);

    /* length of "payloadText" string */
    outGoing[13] = htonl(lenPayloadText);

    /* length of "text" string, include payload (if any) as text here */
    outGoing[14] = htonl(lenText);

    /* length of byte array */
    lenByteArray = (uint32_t)msg->byteArrayLength;
    outGoing[15] = htonl(lenByteArray);

    /* total length of message (minus first int) is first item sent */
    len = (uint32_t)sizeof(outGoing) - (uint32_t)sizeof(int) + lenSubject + lenType +
          lenPayloadText + lenText + lenByteArray;
    outGoing[0] = htonl(len);

    /* Make send socket communications thread-safe. That
     * includes protecting the one buffer being used.
     */
    cMsgSocketMutexLock(domain);

    /* allocate more memory for message-sending buffer if necessary */
    if (domain->msgBufferSize < (int)(len+sizeof(int))) {
      free(domain->msgBuffer);
      domain->msgBufferSize = len + 1024; /* give us 1kB extra */
      domain->msgBuffer = (char *) malloc((size_t)domain->msgBufferSize);
      if (domain->msgBuffer == NULL) {
        cMsgSocketMutexUnlock(domain);
        cMsgConnectReadUnlock(domain);
        cMsgCleanupAfterUsingMem((int)index);
        if (payloadText != NULL) free(payloadText);
        return(CMSG_OUT_OF_MEMORY);
      }
    }

    /* copy data into a single static buffer */
    memcpy(domain->msgBuffer, (void *)outGoing, sizeof(outGoing));
    len = sizeof(outGoing);
    memcpy(domain->msgBuffer+len, (void *)msg->subject, lenSubject);
    len += lenSubject;
    memcpy(domain->msgBuffer+len, (void *)msg->type, lenType);
    len += lenType;
    memcpy(domain->msgBuffer+len, (void *)payloadText, lenPayloadText);
    len += lenPayloadText;
    memcpy(domain->msgBuffer+len, (void *)msg->text, lenText);
    len += lenText;
    memcpy(domain->msgBuffer+len, (void *)&((msg->byteArray)[msg->byteArrayOffset]), lenByteArray);
    len += lenByteArray;   
    
    if (payloadText != NULL) free(payloadText);
    
    /* send data over TCP socket */
    sendLen = cMsgNetTcpWrite(fd, (void *) domain->msgBuffer, len);
    if (sendLen == len) {
      domain->monData.numTcpSends++;
    }
    else {
      cMsgSocketMutexUnlock(domain);
      cMsgConnectReadUnlock(domain);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cmsg_cmsg_send: write failure\n");
      }
      err = CMSG_NETWORK_ERROR;
      break;
    }

    /* done protecting communications */
    cMsgSocketMutexUnlock(domain);
    cMsgConnectReadUnlock(domain);
    break;
  }
  
  if (err!= CMSG_OK) {
    /* don't wait for resubscribes */
    if (failoverSuccessful(domain, 0)) {
       fd = domain->sendSocket;
       if (cMsgDebug >= CMSG_DEBUG_INFO) {
           fprintf(stderr, "cmsg_cmsg_send: FAILOVER SUCCESSFUL, try send again\n");
       }
       goto tryagain;
    }  
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_cmsg_send: FAILOVER NOT successful, quitting, err = %d\n", err);
    }
  }
  
  cMsgCleanupAfterUsingMem((int)index);
  
  return(err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine sends a msg to the specified cMsg domain server. It is called
 * by the user through cMsgSend() given the appropriate UDL. It is asynchronous
 * and should rarely block. It will only block if the TCP protocol is used and
 * the cMsg domain server has
 * reached it maximum number of request-handling threads and each of those threads
 * has a cue which is completely full. In this domain cMsgFlush() does nothing and
 * does not need to be called for the message to be sent immediately.<p>
 *
 * This version of this routine uses writev to write all data in one write call.
 * Another version was tried with many writes (one for ints and one for each
 * string), but the performance died sharply.
 *
 * @param domain pointer to domain info stucture
 * @param index index into local static array used to safely free memory
 * @param msg pointer to a message structure
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if message's payload changed while sending
 * @returns CMSG_BAD_ARGUMENT if the id or message argument is null or has null subject or type
 * @returns CMSG_OUT_OF_RANGE  if the message is too large to be sent by UDP
 * @returns CMSG_OUT_OF_MEMORY if the allocating memory for message buffer failed
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement sending
 *                               messages
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
static int udpSend(cMsgDomainInfo *domain, intptr_t index, cMsgMessage_t *msg) {
  
  char *payloadText;
  int err, fd;
  uint32_t len, lenSubject, lenType, lenPayloadText, lenText, lenByteArray, highInt, lowInt, outGoing[20];
  ssize_t sendLen;
  int64_t llTime;
  struct timespec now;
  
  fd = domain->sendUdpSocket;
  
  tryagain:
  
  while (1) {
    err = CMSG_OK;
    
    /* Cannot run this while connecting/disconnecting */
    cMsgConnectReadLock(domain);

    if (domain->gotConnection != 1) {
      cMsgConnectReadUnlock(domain);
      err = CMSG_LOST_CONNECTION;
      break;
    }
    
    if (msg->text == NULL) {
      lenText = 0;
    }
    else {
      lenText = (uint32_t)strlen(msg->text);
    }

    /* time message sent (right now) */
    clock_gettime(CLOCK_REALTIME, &now);
    /* convert to milliseconds */
    llTime  = ((int64_t)now.tv_sec * 1000) +
              ((int64_t)now.tv_nsec/1000000);
    
    /* update history here */
    err = cMsgAddHistoryToPayloadText((void *)msg, domain->name, domain->myHost,
                                      llTime, &payloadText);
    if (err != CMSG_OK) {
      cMsgConnectReadUnlock(domain);
      cMsgCleanupAfterUsingMem((int)index);
      return(err);
    }
    
    /* length of "payloadText" string */
    if (payloadText == NULL) {
      lenPayloadText = 0;
    }
    else {
      lenPayloadText = (uint32_t)strlen(payloadText);
    }
    
    /* send magic integers first to protect against port scanning */
    outGoing[0] = htonl(CMSG_MAGIC_INT1); /* cMsg */
    outGoing[1] = htonl(CMSG_MAGIC_INT2); /*  is  */
    outGoing[2] = htonl(CMSG_MAGIC_INT3); /* cool */
    
    /* For the server to identify which client is sending this UDP msg, send the
     * messaging-sending socket TCP port here as ID that the server can recognize.
     * Since the server can get the sending host of this UDP packet, and since
     * it already knows the ports on the TCP sockets already made, it can put
     * these 2 pieces of info together to uniquely identify the client sending
     * this msg. */
    outGoing[3] = htonl((uint32_t)domain->localPort);

    /* message id (in network byte order) to domain server */
    outGoing[5] = htonl(CMSG_SEND_REQUEST);
    /* reserved for future use */
    outGoing[6] = 0;
    /* user int */
    outGoing[7] = htonl((uint32_t)msg->userInt);
    /* system msg id */
    outGoing[8] = htonl((uint32_t)msg->sysMsgId);
    /* sender token */
    outGoing[9] = htonl((uint32_t)msg->senderToken);
    /* bit info */
    outGoing[10] = htonl((uint32_t)msg->info);

    highInt = (uint32_t) ((llTime >> 32) & 0x00000000FFFFFFFF);
    lowInt  = (uint32_t) (llTime & 0x00000000FFFFFFFF);
    outGoing[11] = htonl(highInt);
    outGoing[12] = htonl(lowInt);

    /* user time */
    llTime  = ((int64_t)msg->userTime.tv_sec * 1000) +
              ((int64_t)msg->userTime.tv_nsec/1000000);
    highInt = (uint32_t) ((llTime >> 32) & 0x00000000FFFFFFFF);
    lowInt  = (uint32_t) (llTime & 0x00000000FFFFFFFF);
    outGoing[13]  = htonl(highInt);
    outGoing[14] = htonl(lowInt);

    /* length of "subject" string */
    lenSubject   = (uint32_t)strlen(msg->subject);
    outGoing[15] = htonl(lenSubject);
    
    /* length of "type" string */
    lenType      = (uint32_t)strlen(msg->type);
    outGoing[16] = htonl(lenType);

    /* length of "payloadText" string */
    outGoing[17] = htonl(lenPayloadText);

    /* length of "text" string, include payload (if any) as text here */
    outGoing[18] = htonl(lenText);

    /* length of byte array */
    lenByteArray = (uint32_t)msg->byteArrayLength;
    outGoing[19] = htonl(lenByteArray);

    /* total length of message (ignoring first 5 ints sent) is first item sent */
    len = (uint32_t)sizeof(outGoing) - (uint32_t)(5*sizeof(int)) + lenSubject + lenType +
          lenPayloadText + lenText + lenByteArray;
    outGoing[4] = htonl(len);

    if (msg->udpSend && len > CMSG_BIGGEST_UDP_BUFFER_SIZE) {
      cMsgConnectReadUnlock(domain);
      cMsgCleanupAfterUsingMem((int)index);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cmsg_cmsg_send: message is too big for UDP packet\n");
      }
      if (payloadText != NULL) free(payloadText);
      return(CMSG_OUT_OF_RANGE);
    }

    /* Make send socket communications thread-safe. That
     * includes protecting the one buffer being used.
     */
    cMsgSocketMutexLock(domain);

    /* allocate more memory for message-sending buffer if necessary */
    if (domain->msgBufferSize < (int)(len+sizeof(int))) {
      free(domain->msgBuffer);
      domain->msgBufferSize = len + 1024; /* give us 1kB extra */
      domain->msgBuffer = (char *) malloc((size_t)domain->msgBufferSize);
      if (domain->msgBuffer == NULL) {
        cMsgSocketMutexUnlock(domain);
        cMsgConnectReadUnlock(domain);
        cMsgCleanupAfterUsingMem((int)index);
        if (payloadText != NULL) free(payloadText);
        return(CMSG_OUT_OF_MEMORY);
      }
    }

    /* copy data into a single static buffer */
    memcpy(domain->msgBuffer, (void *)outGoing, sizeof(outGoing));
    len = sizeof(outGoing);
    memcpy(domain->msgBuffer+len, (void *)msg->subject, lenSubject);
    len += lenSubject;
    memcpy(domain->msgBuffer+len, (void *)msg->type, lenType);
    len += lenType;
    memcpy(domain->msgBuffer+len, (void *)payloadText, lenPayloadText);
    len += lenPayloadText;
    memcpy(domain->msgBuffer+len, (void *)msg->text, lenText);
    len += lenText;
    memcpy(domain->msgBuffer+len, (void *)&((msg->byteArray)[msg->byteArrayOffset]), lenByteArray);
    len += lenByteArray;   
    
    if (payloadText != NULL) free(payloadText);
    
    /* send data over UDP socket */
    sendLen = send(fd, (void *)domain->msgBuffer, len, 0);      
    if (sendLen == len) {
      domain->monData.numUdpSends++;
    }
    else {
      cMsgSocketMutexUnlock(domain);
      cMsgConnectReadUnlock(domain);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cmsg_cmsg_send: write failure\n");
          perror("cmsg_cmsg_send");
      }
      err = CMSG_NETWORK_ERROR;
      break;
    }

    /* done protecting communications */
    cMsgSocketMutexUnlock(domain);
    cMsgConnectReadUnlock(domain);
    break;
  }
  
  if (err!= CMSG_OK) {
    /* don't wait for resubscribes */
    if (failoverSuccessful(domain, 0)) {
       fd = domain->sendUdpSocket;
       if (cMsgDebug >= CMSG_DEBUG_INFO) {
           fprintf(stderr, "cmsg_cmsg_send: FAILOVER SUCCESSFUL, try send again\n");
       }
       goto tryagain;
    }  
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_cmsg_send: FAILOVER NOT successful, quitting, err = %d\n", err);
    }
  }
  
  cMsgCleanupAfterUsingMem((int)index);

  return(err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine sends a msg to the specified domain server and receives a response.
 * It is a synchronous routine and as a result blocks until it receives a status
 * integer from the cMsg server. It is called by the user through cMsgSyncSend()
 * given the appropriate UDL. In this domain cMsgFlush() does nothing and
 * does not need to be called for the message to be sent immediately.
 *
 * @param domainId id of the domain connection
 * @param vmsg pointer to a message structure
 * @param timeout amount of time to wait for the response
 * @param response integer pointer that gets filled with the server's response
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id or message argument is null or has null subject or type
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement the
 *                               synchronous sending of messages
 * @returns CMSG_OUT_OF_MEMORY if allocating memory failed
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
int cmsg_cmsg_syncSend(void *domainId, void *vmsg,
                       const struct timespec *timeout, int *response) {
  
    intptr_t index; /* int the size of a ptr (for casting) */
    char *payloadText;
    uint32_t len, lenSubject, lenType, lenPayloadText, lenText, lenByteArray, highInt, lowInt, outGoing[16];
    int err, uniqueId, status, fd;
    cMsgMessage_t *msg = (cMsgMessage_t *) vmsg;
    cMsgDomainInfo *domain;
    int64_t llTime;
    struct timespec wait, now;
    char *idString=NULL;
    getInfo *info=NULL;
    /*static int count=0;*/

  
    /* check args */
    if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  
    if ( (cMsgCheckString(msg->subject) != CMSG_OK ) ||
         (cMsgCheckString(msg->type)    != CMSG_OK )    ) {
        return(CMSG_BAD_ARGUMENT);
    }

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
    
    /* make sure domain is usable */
    if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

    fd = domain->sendSocket;
  
    if (!domain->hasSyncSend) {
        cMsgCleanupAfterUsingMem((int)index);
        return(CMSG_NOT_IMPLEMENTED);
    }
 
    err = CMSG_OK;

    /* Cannot run this while connecting/disconnecting */
    cMsgConnectReadLock(domain);

    if (domain->gotConnection != 1) {
      cMsgConnectReadUnlock(domain);
      cMsgCleanupAfterUsingMem((int)index);
      return(CMSG_LOST_CONNECTION);
    }

    if (msg->text == NULL) {
      lenText = 0;
    }
    else {
      lenText = (uint32_t)strlen(msg->text);
    }

    /* time message sent (right now) */
    clock_gettime(CLOCK_REALTIME, &now);
    /* convert to milliseconds */
    llTime  = ((int64_t)now.tv_sec * 1000) +
              ((int64_t)now.tv_nsec/1000000);

    /* update history here */
    err = cMsgAddHistoryToPayloadText(vmsg, domain->name, domain->myHost,
                                      llTime, &payloadText);
    if (err != CMSG_OK) {
      cMsgConnectReadUnlock(domain);
      cMsgCleanupAfterUsingMem((int)index);
      return(err);
    }
    
    /* length of "payloadText" string */
    if (payloadText == NULL) {
      lenPayloadText = 0;
    }
    else {
      lenPayloadText = (uint32_t)strlen(payloadText);
    }
    
    /*
     * Pick a unique identifier, send it to the domain server,
     * and remember it for future use.
     * Mutex protect this operation as many calls may
     * operate in parallel on this static variable.
     */
    numberMutexLock();
    uniqueId = subjectTypeId++;
    numberMutexUnlock();
    
    /* use struct as value in hashTable */
    info = (getInfo *)malloc(sizeof(getInfo));
    if (info == NULL) {
        cMsgConnectReadUnlock(domain);
        cMsgCleanupAfterUsingMem((int)index);
        if (payloadText != NULL) free(payloadText);
        return(CMSG_OUT_OF_MEMORY);
    }
    cMsgGetInfoInit(info);
    info->id = uniqueId;
    
    /* use string generated from id as key (must be string) in hashTable */
    idString = cMsgIntChars((uint32_t)uniqueId);
    if (idString == NULL) {
        cMsgConnectReadUnlock(domain);
        cMsgCleanupAfterUsingMem((int)index);
        cMsgGetInfoFree(info);
        free(info);
        if (payloadText != NULL) free(payloadText);
        return(CMSG_OUT_OF_MEMORY);
    }
    
    /* Try inserting item into hashtable, if entry exists, pick another key */
    cMsgSyncSendMutexLock(domain); /* serialize access to syncSend hash table */
    while ( !hashInsertTry(&domain->syncSendTable, idString, (void *)info) ) {
        free(idString);
        numberMutexLock();
        uniqueId = subjectTypeId++;
        numberMutexUnlock();
        idString = cMsgIntChars((uint32_t)uniqueId);
        if (idString == NULL) {
            cMsgSyncSendMutexUnlock(domain);
            cMsgConnectReadUnlock(domain);
            cMsgCleanupAfterUsingMem((int)index);
            cMsgGetInfoFree(info);
            free(info);
            if (payloadText != NULL) free(payloadText);
            return(CMSG_OUT_OF_MEMORY);
        }
    }
    cMsgSyncSendMutexUnlock(domain);    

    outGoing[1] = htonl(CMSG_SYNC_SEND_REQUEST);
    /* unique syncSend id */
    outGoing[2] = htonl((uint32_t)uniqueId);
    /* user int */
    outGoing[3] = htonl((uint32_t)msg->userInt);
    /* system msg id */
    outGoing[4] = htonl((uint32_t)msg->sysMsgId);
    /* sender token */
    outGoing[5] = htonl((uint32_t)msg->senderToken);
    /* bit info */
    outGoing[6] = htonl((uint32_t)msg->info);

    highInt = (uint32_t) ((llTime >> 32) & 0x00000000FFFFFFFF);
    lowInt  = (uint32_t) (llTime & 0x00000000FFFFFFFF);
    outGoing[7] = htonl(highInt);
    outGoing[8] = htonl(lowInt);

    /* user time */
    llTime  = ((int64_t)msg->userTime.tv_sec * 1000) +
              ((int64_t)msg->userTime.tv_nsec/1000000);
    highInt = (uint32_t) ((llTime >> 32) & 0x00000000FFFFFFFF);
    lowInt  = (uint32_t) (llTime & 0x00000000FFFFFFFF);
    outGoing[9]  = htonl(highInt);
    outGoing[10] = htonl(lowInt);

    /* length of "subject" string */
    lenSubject   = (uint32_t)strlen(msg->subject);
    outGoing[11]  = htonl(lenSubject);
    /* length of "type" string */
    lenType      = (uint32_t)strlen(msg->type);
    outGoing[12] = htonl(lenType);

    /* length of "payloadText" string */
    outGoing[13] = htonl(lenPayloadText);

    /* length of "text" string */
    outGoing[14] = htonl(lenText);

    /* length of byte array */
    lenByteArray = (uint32_t)msg->byteArrayLength;
    outGoing[15] = htonl(lenByteArray);

    /* total length of message (minus first int) is first item sent */
    len = (uint32_t)sizeof(outGoing) - (uint32_t)sizeof(int) + lenSubject + lenType +
          lenPayloadText + lenText + lenByteArray;
    outGoing[0] = htonl(len);

    /* make send socket communications thread-safe */
    cMsgSocketMutexLock(domain);

    /* allocate more memory for message-sending buffer if necessary */
    if (domain->msgBufferSize < (int)(len+sizeof(int))) {
        free(domain->msgBuffer);
        domain->msgBufferSize = len + 1004; /* give us 1kB extra */
        domain->msgBuffer = (char *) malloc((size_t)domain->msgBufferSize);
        if (domain->msgBuffer == NULL) {
            cMsgSocketMutexUnlock(domain);
            cMsgConnectReadUnlock(domain);
            cMsgSyncSendMutexLock(domain);
            hashRemove(&domain->syncSendTable, idString, NULL);
            cMsgSyncSendMutexUnlock(domain);
            cMsgCleanupAfterUsingMem((int)index);
            cMsgGetInfoFree(info);
            free(info);
            free(idString);
            if (payloadText != NULL) free(payloadText);
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                fprintf(stderr, "cmsg_cmsg_syncSend: out of memory\n");
            }
            return(CMSG_OUT_OF_MEMORY);
        }
    }

    /* copy data into a single static buffer */
    memcpy(domain->msgBuffer, (void *)outGoing, sizeof(outGoing));
    len = sizeof(outGoing);
    memcpy(domain->msgBuffer+len, (void *)msg->subject, lenSubject);
    len += lenSubject;
    memcpy(domain->msgBuffer+len, (void *)msg->type, lenType);
    len += lenType;
    memcpy(domain->msgBuffer+len, (void *)payloadText, lenPayloadText);
    len += lenPayloadText;
    memcpy(domain->msgBuffer+len, (void *)msg->text, lenText);
    len += lenText;
    memcpy(domain->msgBuffer+len, (void *)&((msg->byteArray)[msg->byteArrayOffset]), lenByteArray);
    len += lenByteArray;   

    if (payloadText != NULL) free(payloadText);
    
    /* send data over socket */
    if (cMsgNetTcpWrite(fd, (void *) domain->msgBuffer, len) != len) {
        cMsgSocketMutexUnlock(domain);
        cMsgConnectReadUnlock(domain);
        cMsgSyncSendMutexLock(domain);
        hashRemove(&domain->syncSendTable, idString, NULL);
        cMsgSyncSendMutexUnlock(domain);
        cMsgCleanupAfterUsingMem((int)index);
        cMsgGetInfoFree(info);
        free(info);
        free(idString);
        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "cmsg_cmsg_syncSend: write failure\n");
        }
        return(CMSG_NETWORK_ERROR);
    }

    /* done protecting outgoing communications */
    cMsgSocketMutexUnlock(domain);    
    cMsgConnectReadUnlock(domain);

  
    /* Now ..., wait for asynchronous response */
  
    /* lock mutex */
    status = pthread_mutex_lock(&info->mutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed callback mutex lock");
    }

    domain->monData.syncSends++;
    domain->monData.numSyncSends++;

    /* wait while there is no message */
    while (info->msgIn == 0) {
        /* wait until signaled */
        if (timeout == NULL) {
            status = pthread_cond_wait(&info->cond, &info->mutex);
        }
        /* wait until signaled or timeout */
        else {
            cMsgGetAbsoluteTime(timeout, &wait);
            status = pthread_cond_timedwait(&info->cond, &info->mutex, &wait);
        }
    
        if (status == ETIMEDOUT) {
            break;
        }
        else if (status != 0) {
            cmsg_err_abort(status, "Failed callback cond wait");
        }

        /* quit if commanded to */
        if (info->quit) {
            break;
        }
    }

    domain->monData.syncSends--;

    /* unlock mutex */
    status = pthread_mutex_unlock(&info->mutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed callback mutex unlock");
    }

    /* If we timed out ... */
    if (info->msgIn == 0) {
/*printf("get: timed out\n");*/
        err = CMSG_TIMEOUT;
        /* if we've timed out, item has not been removed from hash table yet */
        cMsgSyncSendMutexLock(domain);
        hashRemove(&domain->syncSendTable, idString, NULL);
        cMsgSyncSendMutexUnlock(domain);
    }
    /* If we've been woken up with an error condition ... */
    else if (info->error != CMSG_OK) {
        /* in this case hash table has been cleared in reconnect */
        err = info->error;
    }
    /* If we did not timeout and everything's OK */
    else {
        if (response != NULL) *response = info->response;
        err = CMSG_OK;
    }
  
    cMsgCleanupAfterUsingMem((int)index);

    /* free up memory */
    cMsgGetInfoFree(info);
    free(info);
    free(idString);
  
/*printf("get: SUCCESS!!!\n");*/
    return(err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine gets one message from a one-shot subscription to the given
 * subject and type. It is called by the user through cMsgSubscribeAndGet()
 * given the appropriate UDL. In this domain cMsgFlush() does nothing and
 * does not need to be called for the subscription to be started immediately.
 *
 * @param domainId id of the domain connection
 * @param subject subject of message subscribed to
 * @param type type of message subscribed to
 * @param timeout amount of time to wait for the message; if NULL, wait forever
 * @param replyMsg message received
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the domainId is bad, subject, type, or replyMsg args are NULL
 * @returns CMSG_TIMEOUT if routine received no message in the specified time
 * @returns CMSG_OUT_OF_MEMORY if allocating memory failed
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement
 *                               subscribeAndGet
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
int cmsg_cmsg_subscribeAndGet(void *domainId, const char *subject, const char *type,
                              const struct timespec *timeout, void **replyMsg) {
                             
  cMsgDomainInfo *domain;
  intptr_t index;
  int err, fd, uniqueId, status;
  uint32_t len, lenSubject, lenType, outGoing[6];
  char *idString;
  getInfo *info = NULL;
  struct timespec wait;
  struct iovec iov[3];
  

  /* check args */
  if (replyMsg == NULL) return(CMSG_BAD_ARGUMENT);
  
  if ( (cMsgCheckString(subject) != CMSG_OK ) ||
       (cMsgCheckString(type)    != CMSG_OK )    ) {
      return(CMSG_BAD_ARGUMENT);
  }

  index = (intptr_t) domainId;
  if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
    
  /* make sure domain is usable */
  if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

  fd = domain->sendSocket;
  
  if (!domain->hasSubscribeAndGet) {
      cMsgCleanupAfterUsingMem((int)index);
      return(CMSG_NOT_IMPLEMENTED);
  }   
           
  cMsgConnectReadLock(domain);

  if (domain->gotConnection != 1) {
      cMsgConnectReadUnlock(domain);
      cMsgCleanupAfterUsingMem((int)index);
      return(CMSG_LOST_CONNECTION);
  }
  
  /*
   * Pick a unique identifier for the subject/type pair, and
   * send it to the domain server & remember it for future use
   * Mutex protect this operation as many cmsg_cmsg_connect calls may
   * operate in parallel on this static variable.
   */
  numberMutexLock();
  uniqueId = subjectTypeId++;
  numberMutexUnlock();

  /* use struct as value in hashTable */
  info = (getInfo *)malloc(sizeof(getInfo));
  if (info == NULL) {
      cMsgConnectReadUnlock(domain);
      cMsgCleanupAfterUsingMem((int)index);
      return(CMSG_OUT_OF_MEMORY);
  }
  cMsgGetInfoInit(info);
  info->id      = uniqueId;
  info->error   = CMSG_OK;
  info->msgIn   = 0;
  info->quit    = 0;
  info->msg     = NULL;
  info->subject = strdup(subject);
  info->type    = strdup(type);

  /* use string generated from id as key (must be string) in hashTable */
  idString = cMsgIntChars((uint32_t)uniqueId);
  if (idString == NULL) {
      cMsgConnectReadUnlock(domain);
      cMsgCleanupAfterUsingMem((int)index);
      cMsgGetInfoFree(info);
      free(info);
      return(CMSG_OUT_OF_MEMORY);
  }

  /* Try inserting item into hashtable, if entry exists, pick another key */
  cMsgSubAndGetMutexLock(domain); /* serialize access to subAndGet hash table */
  while ( !hashInsertTry(&domain->subAndGetTable, idString, (void *)info) ) {
      free(idString);
      numberMutexLock();
      uniqueId = subjectTypeId++;
      numberMutexUnlock();
      idString = cMsgIntChars((uint32_t)uniqueId);
      if (idString == NULL) {
          cMsgSubAndGetMutexUnlock(domain);
          cMsgConnectReadUnlock(domain);
          cMsgCleanupAfterUsingMem((int)index);
          cMsgGetInfoFree(info);
          free(info);
          return(CMSG_OUT_OF_MEMORY);
      }
  }
  cMsgSubAndGetMutexUnlock(domain);

  /* notify domain server */

  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(CMSG_SUBSCRIBE_AND_GET_REQUEST);
  /* unique id for receiverSubscribeId */
  outGoing[2] = htonl((uint32_t)uniqueId);
  /* length of "subject" string */
  lenSubject  = (uint32_t)strlen(subject);
  outGoing[3] = htonl(lenSubject);
  /* length of "type" string */
  lenType     = (uint32_t)strlen(type);
  outGoing[4] = htonl(lenType);
  /* length of "namespace" string (0 in this case, since
   * only used for server-to-server) */
  outGoing[5] = htonl(0);

  /* total length of message (minus first int) is first item sent */
  len = (uint32_t)sizeof(outGoing) - (uint32_t)sizeof(int) + lenSubject + lenType;
  outGoing[0] = htonl(len);

  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);

  iov[1].iov_base = (char*) subject;
  iov[1].iov_len  = lenSubject;

  iov[2].iov_base = (char*) type;
  iov[2].iov_len  = lenType;

  /* make send socket communications thread-safe */
  cMsgSocketMutexLock(domain);

  if (cMsgNetTcpWritev(fd, iov, 3, 16) == -1) {
    cMsgSocketMutexUnlock(domain);
    cMsgConnectReadUnlock(domain);
    cMsgSubAndGetMutexLock(domain);
    hashRemove(&domain->subAndGetTable, idString, NULL);
    cMsgSubAndGetMutexUnlock(domain);
    cMsgCleanupAfterUsingMem((int)index);
    cMsgGetInfoFree(info);
    free(info);
    free(idString);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "get: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* done protecting communications */
  cMsgSocketMutexUnlock(domain);
  cMsgConnectReadUnlock(domain);

  /* Now ..., wait for asynchronous response */

  /* lock mutex */
  status = pthread_mutex_lock(&info->mutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed callback mutex lock");
  }

  domain->monData.subAndGets++;
  domain->monData.numSubAndGets++;

  /* wait while there is no message */
  while (info->msgIn == 0) {
    /* wait until signaled */
    if (timeout == NULL) {
      status = pthread_cond_wait(&info->cond, &info->mutex);
    }
    /* wait until signaled or timeout */
    else {
      cMsgGetAbsoluteTime(timeout, &wait);
      status = pthread_cond_timedwait(&info->cond, &info->mutex, &wait);
    }

    if (status == ETIMEDOUT) {
      break;
    }
    else if (status != 0) {
      cmsg_err_abort(status, "Failed callback cond wait");
    }

    /* quit if commanded to */
    if (info->quit) {
      break;
    }
  }

  domain->monData.subAndGets--;

  /* unlock mutex */
  status = pthread_mutex_unlock(&info->mutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed callback mutex unlock");
  }

  /* If we timed out, tell server to forget the get. */
  if (info->msgIn == 0) {
/*printf("get: timed out\n");*/

    if (!info->quit) {
      /* remove the get from server */
      unSubscribeAndGet(domain, subject, type, uniqueId);
    }

    if (replyMsg != NULL) *replyMsg = NULL;
    err = CMSG_TIMEOUT;
    /* if we've timed out, item has not been removed from hash table yet */
    cMsgSubAndGetMutexLock(domain);
    hashRemove(&domain->subAndGetTable, idString, NULL);
    cMsgSubAndGetMutexUnlock(domain);
  }
  /* If we've been woken up for dead server ... */
  else if (info->error != CMSG_OK) {
      /* in this case hash table has been cleared in reconnect */
      if (replyMsg != NULL) *replyMsg = NULL;
      err = info->error;
  }
  /* If we did not timeout and everything's OK */
  else {
      /*
       * Don't need to make a copy of message as only 1 receipient.
       * Message was allocated in client's listening thread and user
       * must free it.
       */
      if (replyMsg != NULL) *replyMsg = info->msg;
      info->msg = NULL;
      err = CMSG_OK;
  }

  cMsgCleanupAfterUsingMem((int)index);

  /* free up memory */
  cMsgGetInfoFree(info);
  free(info);
  free(idString);

/*printf("get: SUCCESS!!!\n");*/
  return(err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine tells the cMsg server to "forget" about the cMsgSubscribeAndGet()
 * call (specified by the id argument) since a timeout occurred. Internal use
 * only.
 *
 * @param domain ptr to domain structure
 * @param subject subject of message subscribed to
 * @param type type of message subscribed to
 * @param id unique id associated with a subscribeAndGet
 *
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 */
static int unSubscribeAndGet(cMsgDomainInfo *domain, const char *subject,
                             const char *type, int id) {

  int fd, len;
  uint32_t outGoing[6], lenSubject, lenType;
  struct iovec iov[3];

  fd = domain->sendSocket;

  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(CMSG_UNSUBSCRIBE_AND_GET_REQUEST);
  /* receiverSubscribe */
  outGoing[2] = htonl((uint32_t)id);
  /* length of "subject" string */
  lenSubject  = (uint32_t)strlen(subject);
  outGoing[3] = htonl(lenSubject);
  /* length of "type" string */
  lenType     = (uint32_t)strlen(type);
  outGoing[4] = htonl(lenType);
  /* length of "namespace" string (0 in this case, since
   * only used for server-to-server) */
  outGoing[5] = htonl(0);

  /* total length of message (minus first int) is first item sent */
  len = (uint32_t)sizeof(outGoing) - (uint32_t)sizeof(int) + lenSubject + lenType;
  outGoing[0] = htonl((uint32_t)len);

  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);

  iov[1].iov_base = (char*) subject;
  iov[1].iov_len  = lenSubject;

  iov[2].iov_base = (char*) type;
  iov[2].iov_len  = lenType;

  /* make send socket communications thread-safe */
  cMsgSocketMutexLock(domain);

  if (cMsgNetTcpWritev(fd, iov, 3, 16) == -1) {
    cMsgSocketMutexUnlock(domain);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "unSubscribeAndGet: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  cMsgSocketMutexUnlock(domain);

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine gets one message from another cMsg client by sending out
 * an initial message to that responder. It is a synchronous routine that
 * fails when no reply is received with the given timeout. This function
 * can be thought of as a peer-to-peer exchange of messages.
 * One message is sent to all listeners. The first responder
 * to the initial message will have its single response message sent back
 * to the original sender. If there are no subscribers to get the sent message,
 * this routine returns CMSG_OK, but with a NULL message.
 * This routine is called by the user through
 * cMsgSendAndGet() given the appropriate UDL. In this domain cMsgFlush()
 * does nothing and does not need to be called for the mesage to be
 * sent immediately.
 *
 * @param domainId id of the domain connection
 * @param sendMsg messages to send to all listeners
 * @param timeout amount of time to wait for the response message; if NULL,
 *                wait forever
 * @param replyMsg message received from the responder; if null and return is CMSG_OK,
                   then the local server has no subsriber for the sent message.
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id, sendMsg or replyMsg arguments or the
 *                            sendMsg's subject or type are null
 * @returns CMSG_TIMEOUT if routine received no message in the specified time
 * @returns CMSG_OUT_OF_MEMORY if all available sendAndGet memory has been used
 *                             or allocating memory for message buffer failed
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement
 *                               sendAndGet
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */
int cmsg_cmsg_sendAndGet(void *domainId, void *sendMsg,
                         const struct timespec *timeout, void **replyMsg) {

  char *payloadText;
  intptr_t index;
  cMsgDomainInfo *domain;
  cMsgMessage_t *msg = (cMsgMessage_t *) sendMsg;
  int fd, err, uniqueId, status;
  uint32_t len, lenSubject, lenType, lenPayloadText, lenText, lenByteArray;
  uint32_t highInt, lowInt, outGoing[16];
  char *idString;
  getInfo *info = NULL;
  int64_t llTime;
  struct timespec wait, now;


  /* check args */
  if (sendMsg == NULL || replyMsg == NULL) return(CMSG_BAD_ARGUMENT);

  if ( (cMsgCheckString(msg->subject) != CMSG_OK ) ||
       (cMsgCheckString(msg->type)    != CMSG_OK )    ) {
      return(CMSG_BAD_ARGUMENT);
  }

  index = (intptr_t) domainId;
  if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

  /* make sure domain is usable */
  if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

  fd = domain->sendSocket;

  if (!domain->hasSendAndGet) {
      cMsgCleanupAfterUsingMem((int)index);
      return(CMSG_NOT_IMPLEMENTED);
  }

  cMsgConnectReadLock(domain);

  if (domain->gotConnection != 1) {
      cMsgConnectReadUnlock(domain);
      cMsgCleanupAfterUsingMem((int)index);
      return(CMSG_LOST_CONNECTION);
  }

  if (msg->text == NULL) {
     lenText = 0;
  }
  else {
     lenText = (uint32_t)strlen(msg->text);
  }

  /* time message sent (right now) */
  clock_gettime(CLOCK_REALTIME, &now);
  /* convert to milliseconds */
  llTime  = ((int64_t)now.tv_sec * 1000) +
            ((int64_t)now.tv_nsec/1000000);

  /* update history here */
  err = cMsgAddHistoryToPayloadText(sendMsg, domain->name, domain->myHost,
                                    llTime, &payloadText);
  if (err != CMSG_OK) {
    cMsgConnectReadUnlock(domain);
    cMsgCleanupAfterUsingMem((int)index);
    return(err);
  }

  /* length of "payloadText" string */
  if (payloadText == NULL) {
    lenPayloadText = 0;
  }
  else {
    lenPayloadText = (uint32_t)strlen(payloadText);
  }

  numberMutexLock();
  uniqueId = subjectTypeId++;
  numberMutexUnlock();

  /* use struct as value in hashTable */
  info = (getInfo *)malloc(sizeof(getInfo));
  if (info == NULL) {
    cMsgConnectReadUnlock(domain);
    cMsgCleanupAfterUsingMem((int)index);
    if (payloadText != NULL) free(payloadText);
    return(CMSG_OUT_OF_MEMORY);
  }
  cMsgGetInfoInit(info);
  info->id      = uniqueId;
  info->error   = CMSG_OK;
  info->msgIn   = 0;
  info->quit    = 0;
  info->msg     = NULL;
  info->subject = strdup(msg->subject);
  info->type    = strdup(msg->type);

  /* use string generated from id as key (must be string) in hashTable */
  idString = cMsgIntChars((uint32_t)uniqueId);
  if (idString == NULL) {
    cMsgConnectReadUnlock(domain);
    cMsgCleanupAfterUsingMem((int)index);
    cMsgGetInfoFree(info);
    free(info);
    if (payloadText != NULL) free(payloadText);
    return(CMSG_OUT_OF_MEMORY);
  }

  /* Try inserting item into hashtable, if entry exists, pick another key */
  cMsgSendAndGetMutexLock(domain); /* serialize access to sendAndGet hash table */
  while ( !hashInsertTry(&domain->sendAndGetTable, idString, (void *)info) ) {
    free(idString);
    numberMutexLock();
    uniqueId = subjectTypeId++;
    numberMutexUnlock();
    idString = cMsgIntChars((uint32_t)uniqueId);
    if (idString == NULL) {
      cMsgSendAndGetMutexUnlock(domain);
      cMsgConnectReadUnlock(domain);
      cMsgCleanupAfterUsingMem((int)index);
      cMsgGetInfoFree(info);
      free(info);
      if (payloadText != NULL) free(payloadText);
      return(CMSG_OUT_OF_MEMORY);
    }
  }
  cMsgSendAndGetMutexUnlock(domain);
  /* notify domain server */

  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(CMSG_SEND_AND_GET_REQUEST);
  /* reserved */
  outGoing[2] = 0;
  /* user int */
  outGoing[3] = htonl((uint32_t)msg->userInt);
  /* unique id (senderToken) */
  outGoing[4] = htonl((uint32_t)uniqueId);
  /* bit info */
  outGoing[5] = htonl((uint32_t)(msg->info | CMSG_IS_GET_REQUEST));

  highInt = (uint32_t) ((llTime >> 32) & 0x00000000FFFFFFFF);
  lowInt  = (uint32_t) (llTime & 0x00000000FFFFFFFF);
  outGoing[6] = htonl(highInt);
  outGoing[7] = htonl(lowInt);

  /* user time */
  llTime  = ((int64_t)msg->userTime.tv_sec * 1000) +
            ((int64_t)msg->userTime.tv_nsec/1000000);
  highInt = (uint32_t) ((llTime >> 32) & 0x00000000FFFFFFFF);
  lowInt  = (uint32_t) (llTime & 0x00000000FFFFFFFF);
  outGoing[8] = htonl(highInt);
  outGoing[9] = htonl(lowInt);

  /* length of "subject" string */
  lenSubject   = (uint32_t)strlen(msg->subject);
  outGoing[10] = htonl(lenSubject);
  /* length of "type" string */
  lenType      = (uint32_t)strlen(msg->type);
  outGoing[11] = htonl(lenType);

  /* namespace length */
  outGoing[12] = htonl(0);

  /* length of "payloadText" string */
  outGoing[13] = htonl(lenPayloadText);

  /* length of "text" string */
  outGoing[14] = htonl(lenText);

  /* length of byte array */
  lenByteArray = (uint32_t)msg->byteArrayLength;
  outGoing[15] = htonl(lenByteArray);

  /* total length of message (minus first int) is first item sent */
  len = (uint32_t)sizeof(outGoing) - (uint32_t)sizeof(int) + lenSubject + lenType +
        lenPayloadText + lenText + lenByteArray;
  outGoing[0] = htonl(len);

  /* make send socket communications thread-safe */
  cMsgSocketMutexLock(domain);

  /* allocate more memory for message-sending buffer if necessary */
  if (domain->msgBufferSize < (int)(len+sizeof(int))) {
    free(domain->msgBuffer);
    domain->msgBufferSize = len + 1004; /* give us 1kB extra */
    domain->msgBuffer = (char *) malloc((size_t)domain->msgBufferSize);
    if (domain->msgBuffer == NULL) {
      cMsgSocketMutexUnlock(domain);
      cMsgConnectReadUnlock(domain);
      cMsgSendAndGetMutexLock(domain);
      hashRemove(&domain->sendAndGetTable, idString, NULL);
      cMsgSendAndGetMutexUnlock(domain);
      cMsgCleanupAfterUsingMem((int)index);
      cMsgGetInfoFree(info);
      free(info);
      free(idString);
      if (payloadText != NULL) free(payloadText);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cmsg_cmsg_sendAndGet: out of memory\n");
      }
      return(CMSG_OUT_OF_MEMORY);
    }
  }

  /* copy data into a single static buffer */
  memcpy(domain->msgBuffer, (void *)outGoing, sizeof(outGoing));
  len = sizeof(outGoing);
  memcpy(domain->msgBuffer+len, (void *)msg->subject, lenSubject);
  len += lenSubject;
  memcpy(domain->msgBuffer+len, (void *)msg->type, lenType);
  len += lenType;
  memcpy(domain->msgBuffer+len, (void *)payloadText, lenPayloadText);
  len += lenPayloadText;
  memcpy(domain->msgBuffer+len, (void *)msg->text, lenText);
  len += lenText;
  memcpy(domain->msgBuffer+len, (void *)&((msg->byteArray)[msg->byteArrayOffset]), lenByteArray);
  len += lenByteArray;

  if (payloadText != NULL) free(payloadText);

  /* send data over socket */
  if (cMsgNetTcpWrite(fd, (void *) domain->msgBuffer, len) != len) {
    cMsgSocketMutexUnlock(domain);
    cMsgConnectReadUnlock(domain);
    cMsgSendAndGetMutexLock(domain);
    hashRemove(&domain->sendAndGetTable, idString, NULL);
    cMsgSendAndGetMutexUnlock(domain);
    cMsgCleanupAfterUsingMem((int)index);
    cMsgGetInfoFree(info);
    free(info);
    free(idString);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_cmsg_sendAndGet: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* done protecting communications */
  cMsgSocketMutexUnlock(domain);
  cMsgConnectReadUnlock(domain);

  /* Now ..., wait for asynchronous response */

  /* lock mutex */
  status = pthread_mutex_lock(&info->mutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed callback mutex lock");
  }

  domain->monData.sendAndGets++;
  domain->monData.numSendAndGets++;

  /* wait while there is no message */
  while (info->msgIn == 0) {
    /* wait until signaled */
    if (timeout == NULL) {
      status = pthread_cond_wait(&info->cond, &info->mutex);
    }
    /* wait until signaled or timeout */
    else {
      cMsgGetAbsoluteTime(timeout, &wait);
      status = pthread_cond_timedwait(&info->cond, &info->mutex, &wait);
    }

    if (status == ETIMEDOUT) {
      break;
    }
    else if (status != 0) {
      cmsg_err_abort(status, "Failed callback cond wait");
    }

    /* quit if commanded to */
    if (info->quit) {
      break;
    }
  }
  domain->monData.sendAndGets--;

  /* unlock mutex */
  status = pthread_mutex_unlock(&info->mutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed callback mutex unlock");
  }

  /* If we timed out, tell server to forget the get. */
  if (info->msgIn == 0) {
/*printf("get: timed out\n");*/
    if (!info->quit) {
      /* remove the get from server */
      unSendAndGet(domain, uniqueId);
    }

    if (replyMsg != NULL) *replyMsg = NULL;
    err = CMSG_TIMEOUT;
    /* if we've timed out, item has not been removed from hash table yet */
    cMsgSendAndGetMutexLock(domain);
    hashRemove(&domain->sendAndGetTable, idString, NULL);
    cMsgSendAndGetMutexUnlock(domain);
  }
  /* If we've been woken up for dead server ... */
  else if (info->error != CMSG_OK) {
    /* in this case hash table has been cleared in reconnect */
    if (replyMsg != NULL) *replyMsg = NULL;
    err = info->error;
  }
  /* If we did not timeout and everything's OK */
  else {
    /*
     * Return null (with CMSG_OK) if local server has no subscriber
     * to send the sendAndGet-message to.
     */
    if ( (info->msg->info & CMSG_NULL_GET_SERVER_RESPONSE) > 0) {
      if (replyMsg != NULL) *replyMsg = NULL;
    }
    else {
      /*
       * Don't need to make a copy of message as only 1 recipient.
       * Message was allocated in client's listening thread and user
       * must free it.
       */
      if (replyMsg != NULL) *replyMsg = info->msg;
      /* May not free this down below in cMsgGetInfoFree. */
      info->msg = NULL;
    }

    err = CMSG_OK;
  }

  cMsgCleanupAfterUsingMem((int)index);

  /* free up memory */
  /* be careful, cMsgGetInfoFree(info) frees info->msg */
  cMsgGetInfoFree(info);
  free(info);
  free(idString);

/*printf("get: SUCCESS!!!\n");*/
  return(err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine tells the cMsg server to "forget" about the cMsgSendAndGet()
 * call (specified by the id argument) since a timeout occurred. Internal use
 * only.
 *
 * @param domain ptr to domain structure
 * @param id unique id associated with a sendAndGet
 *
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 */
static int unSendAndGet(cMsgDomainInfo *domain, int id) {

  int fd;
  uint32_t outGoing[3];
  fd = domain->sendSocket;

  /* size of info coming - 8 bytes */
  outGoing[0] = htonl(8);
  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(CMSG_UN_SEND_AND_GET_REQUEST);
  /* senderToken id */
  outGoing[2] = htonl((uint32_t)id);

  /* make send socket communications thread-safe */
  cMsgSocketMutexLock(domain);

  /* send ints over together */
  if (cMsgNetTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
    cMsgSocketMutexUnlock(domain);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "unSendAndGet: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  cMsgSocketMutexUnlock(domain);

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This method is a synchronous call to receive a message containing monitoring
 * data which describes the state of the cMsg domain the user is connected to.
 * The time is data was sent can be obtained by calling cMsgGetSenderTime.
 * The monitoring data in xml format can be obtained by calling cMsgGetText.<p>
 *
 * This method can also be used to include a user-generated XML fragment in the
 * monitoring data reported by this client to the server. This fragment is given
 * in the "command" arg while simultaneously setting the "replyMsg" arg to NULL.
 *
 * @param domainId id of the domain connection
 * @param command  ignored unless replyMsg arg is NULL in which case it's a
 *                 string containing user-generated XML fragment in monitoring data
 * @param replyMsg message received from the domain containing monitoring data;
 *                 NULL if setting user-generated XML fragment in monitoring data
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if domainId is bad
 * @returns CMSG_OUT_OF_MEMORY if no memory available or command too big (> 8116 bytes)
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSMonitor
 */
int cmsg_cmsg_monitor(void *domainId, const char *command, void **replyMsg) {

    intptr_t index;
    cMsgDomainInfo *domain;


    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    cMsgMemoryMutexLock();

    domain = connectPointers[index];
    if (domain == NULL || domain->disconnectCalled) {
        cMsgMemoryMutexUnlock();
        return(CMSG_BAD_ARGUMENT);
    }

    /* If setting user-generated fragment in the client monitoring data */
    if (replyMsg == NULL) {
        if (domain->userXML != NULL) free(domain->userXML);

        if (command == NULL) {
            /* Remove user's XML fragment */
            domain->userXML = NULL;
        }
        else {
            /* Check to make sure xml fits in limited memory space */
            if (strlen(command) > 8116) {
                return(CMSG_OUT_OF_MEMORY);
            }
            /* Add user's XML fragment */
            domain->userXML = strdup(command);
        }
    }
    /* Else receiving msg containing monitoring data */
    else {
        cMsgMessage_t *msg = (cMsgMessage_t *) cMsgCreateMessage();
        if (msg == NULL) {
            cMsgMemoryMutexUnlock();
            return(CMSG_OUT_OF_MEMORY);
        }

        /* cMsgSetText only returns error if msg is NULL */
        cMsgSetText((void *)msg, domain->monitorXML);

        /* Return msg */
        if (replyMsg != NULL) *replyMsg = (void *)msg;
    }

    cMsgMemoryMutexUnlock();

    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine sends any pending (queued up) communication with the server.
 * In the cMsg domain, however, all sockets are set to TCP_NODELAY -- meaning
 * all writes over the socket are sent immediately. Thus, this routine does
 * nothing.
 *
 * @param domainId id of the domain connection
 * @param timeout amount of time to wait for completion
 *
 * @returns CMSG_OK always
 */
int cmsg_cmsg_flush(void *domainId, const struct timespec *timeout) {
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine subscribes to messages of the given subject and type.
 * When a message is received, the given callback is passed the message
 * pointer and the userArg pointer and then is executed. A configuration
 * structure is given to determine the behavior of the callback.
 * This routine is called by the user through cMsgSubscribe() given the
 * appropriate UDL.
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
 * @returns CMSG_BAD_ARGUMENT if the id is bad or subject, type, or callback are NULL
 * @returns CMSG_OUT_OF_MEMORY if all available subscription memory has been used
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement
 *                               subscribe
 * @returns CMSG_ALREADY_EXISTS if an identical subscription already exists
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */
int cmsg_cmsg_subscribe(void *domainId, const char *subject, const char *type,
                        cMsgCallbackFunc *callback, void *userArg,
                        cMsgSubscribeConfig *config, void **handle) {

  intptr_t index;
  int fd, uniqueId, status, err, newSub=0;
  uint32_t len, lenSubject, lenType, outGoing[6];
  int startRetries = 0;
  cMsgDomainInfo *domain;
  subscribeConfig *sConfig = (subscribeConfig *) config;
  cbArg *cbarg;
  struct iovec iov[3];
  pthread_attr_t threadAttribute;
  char *subKey;
  subInfo *sub;
  subscribeCbInfo *cb, *cbItem;
  void *p;
  struct timespec wait;

  /* wait .01 sec to check if callback thread started */
  wait.tv_sec  = 0;
  wait.tv_nsec = 10000000;


  /* check args */
  if ( (cMsgCheckString(subject) != CMSG_OK ) ||
       (cMsgCheckString(type)    != CMSG_OK ) ||
       (callback == NULL)                    ) {
      return(CMSG_BAD_ARGUMENT);
  }

  index = (intptr_t) domainId;
  if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

  /* make sure domain is usable */
  if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

  fd = domain->sendSocket;

  if (!domain->hasSubscribe) {
      cMsgCleanupAfterUsingMem((int)index);
      return(CMSG_NOT_IMPLEMENTED);
  }

  /* Create a unique string which is subject"type since the quote char is not allowed
   * in either subject or type. This unique string will be the key in a hash table
   * with the value being a structure holding subscription info.
   */
  subKey = (char *) calloc(1, strlen(subject) + strlen(type) + 2);
  if (subKey == NULL) {
      cMsgCleanupAfterUsingMem((int)index);
      return(CMSG_OUT_OF_MEMORY);
  }
  sprintf(subKey, "%s\"%s", subject, type);

  tryagain:

  while (1) {
    err = CMSG_OK;

    cMsgConnectReadLock(domain);

    if (domain->gotConnection != 1) {
      cMsgConnectReadUnlock(domain);
      err = CMSG_LOST_CONNECTION;
      break;
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
        cMsgCleanupAfterUsingMem((int)index);
        free(subKey);
        return(CMSG_OUT_OF_MEMORY);
      }
      cMsgSubscribeInfoInit(sub);
      sub->subject = strdup(subject);
      sub->type    = strdup(type);
      cMsgSubscriptionSetRegexpStuff(sub);
      newSub = 1;
    }

    /* Now add a callback */
    cb = (subscribeCbInfo *) calloc(1, sizeof(subscribeCbInfo));
    if (cb == NULL) {
      cMsgSubscribeWriteUnlock(domain);
      cMsgConnectReadUnlock(domain);
      cMsgCleanupAfterUsingMem((int)index);
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
/*printf("Creating cb thread at %p\n", cb);*/

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
      cMsgCleanupAfterUsingMem((int)index);
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

    cbarg->domainId = index;
    cbarg->sub      = sub;
    cbarg->cb       = cb;
    cbarg->key      = subKey;
    cbarg->domain   = domain;

    /* store it here in case it needs freeing - if disconnect done without unsubscribe */
    cb->cbarg = (void *)cbarg;

    if (handle != NULL) {
      *handle = (void *)cbarg;
    }

    /* init thread attributes */
    pthread_attr_init(&threadAttribute);

    /* if stack size of this thread is set, include in attribute */
    if (cb->config.stackSize > 0) {
/*printf("Setting stack size to %d\n", (int)cb->config.stackSize);*/
      pthread_attr_setstacksize(&threadAttribute, cb->config.stackSize);
    }
    else {
/*printf("Stack size is %d\n", (int)cb->config.stackSize);*/
    }

    /* if this is an existing subscription, just adding callback thread is good enough */
    if (!newSub) {
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
/*printf("cmsg_cmsg_subscribe: wait another .01 sec for cb thread to start\n");*/
            nanosleep(&wait, NULL);
            startRetries++;
        }

        domain->monData.numSubscribes++;

        cMsgSubscribeWriteUnlock(domain);
        cMsgConnectReadUnlock(domain);
        cMsgCleanupAfterUsingMem((int)index);
        return(CMSG_OK);
    }

    numberMutexLock();
    uniqueId = subjectTypeId++;
    numberMutexUnlock();
    sub->id = uniqueId;

    /* notify domain server */

    /* message id (in network byte order) to domain server */
    outGoing[1] = htonl(CMSG_SUBSCRIBE_REQUEST);
    /* unique id to domain server */
    outGoing[2] = htonl((uint32_t)uniqueId);
    /* length of "subject" string */
    lenSubject  = (uint32_t)strlen(subject);
    outGoing[3] = htonl(lenSubject);
    /* length of "type" string */
    lenType     = (uint32_t)strlen(type);
    outGoing[4] = htonl(lenType);
    /* length of "namespace" string (0 in this case, since
     * only used for server-to-server) */
    outGoing[5] = htonl(0);

    /* total length of message is first item sent */
    len = (uint32_t)sizeof(outGoing) - (uint32_t)sizeof(int) + lenSubject + lenType;
    outGoing[0] = htonl(len);

    iov[0].iov_base = (char*) outGoing;
    iov[0].iov_len  = sizeof(outGoing);

    iov[1].iov_base = (char*) subject;
    iov[1].iov_len  = lenSubject;

    iov[2].iov_base = (char*) type;
    iov[2].iov_len  = lenType;

    /* make send socket communications thread-safe */
    cMsgSocketMutexLock(domain);

    if (cMsgNetTcpWritev(fd, iov, 3, 16) == -1) {
        cMsgSocketMutexUnlock(domain);
        cMsgSubscribeWriteUnlock(domain);
        cMsgConnectReadUnlock(domain);

        /* free up stuff */
        cMsgCallbackInfoFree(cb);
        free(cb);
        cMsgSubscribeInfoFree(sub);
        free(sub);
        free(cbarg);

        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cmsg_cmsg_subscribe: write failure\n");
        }

        err = CMSG_LOST_CONNECTION;
        break;
    }

    /* done protecting communications */
    cMsgSocketMutexUnlock(domain);

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
/*printf("cmsg_cmsg_subscribe: wait another .01 sec for cb thread to start\n");*/
        nanosleep(&wait, NULL);
        startRetries++;
    }

    /* put subscription in hash table */
    hashInsert(&domain->subscribeTable, subKey, sub, NULL);

    domain->monData.numSubscribes++;

    /* done protecting subscribe */
    cMsgSubscribeWriteUnlock(domain);
    cMsgConnectReadUnlock(domain);

    break;
  } /* while(1) */

  if (err!= CMSG_OK) {
    /* don't wait for resubscribes */
    if (failoverSuccessful(domain, 0)) {
       fd = domain->sendSocket;
       if (cMsgDebug >= CMSG_DEBUG_ERROR) {
         fprintf(stderr, "cmsg_cmsg_subscribe: FAILOVER SUCCESSFUL, try subscribe again\n");
       }
       goto tryagain;
    }
    if (handle != NULL) handle = NULL;
    free(subKey);

    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_cmsg_subscribe: FAILOVER NOT successful, quitting, err = %d\n", err);
    }
  }

  cMsgCleanupAfterUsingMem((int)index);
  return(err);
}



/*-------------------------------------------------------------------*/


/**
 * This routine resubscribes existing subscriptions on another (failover)
 * server. If there is an error in any of the resubscribes, the failover
 * is scrubbed. This routine is called by the routine "restoreSubscriptions"
 * which is protected by the writeConnect lock so no other locks are
 * required.
 *
 * @param domain pointer to struct of the domain connection info
 * @param sub pointer to struct of subscription info
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 */
static int resubscribe(cMsgDomainInfo *domain, subInfo *sub) {

  struct iovec iov[3];
  uint32_t len, lenSubject, lenType, outGoing[6];
  int  uniqueId, fd = domain->sendSocket;

/*printf("resubscribe: using socket %d\n",fd);*/
  if (domain->gotConnection != 1) {
/*printf("resubscribe: NOT connected\n");*/
      return(CMSG_LOST_CONNECTION);
  }
/*printf("resubscribe: talk to server\n");*/

  /* Pick a unique identifier for the subject/type pair. */
  numberMutexLock();
  uniqueId = subjectTypeId++;
  numberMutexUnlock();
  sub->id = uniqueId;

  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(CMSG_SUBSCRIBE_REQUEST);
  /* unique id to domain server */
  outGoing[2] = htonl((uint32_t)uniqueId);
  /* length of "subject" string */
  lenSubject  = (uint32_t)strlen(sub->subject);
  outGoing[3] = htonl(lenSubject);
  /* length of "type" string */
  lenType     = (uint32_t)strlen(sub->type);
  outGoing[4] = htonl(lenType);
  /* length of "namespace" string (0 in this case, since namespace
   * only used for server-to-server) */
  outGoing[5] = htonl(0);

  /* total length of message is first item sent */
  len = (uint32_t)sizeof(outGoing) - (uint32_t)sizeof(int) + lenSubject + lenType;
  outGoing[0] = htonl(len);

  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);

  iov[1].iov_base = sub->subject;
  iov[1].iov_len  = lenSubject;

  iov[2].iov_base = sub->type;
  iov[2].iov_len  = lenType;


  if (cMsgNetTcpWritev(fd, iov, 3, 16) == -1) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_cmsg_subscribe: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine unsubscribes to messages of the given handle (which
 * represents a given subject, type, callback, and user argument).
 * This routine is called by the user through
 * cMsgUnSubscribe() given the appropriate UDL. In this domain cMsgFlush()
 * does nothing and does not need to be called for cmsg_cmsg_unsubscribe to be
 * started immediately.
 *
 * @param domainId id of the domain connection
 * @param handle void pointer obtained from {@link cmsg_cmsg_subscribe}
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id/handle is bad or handle, its subject, type, or callback are NULL,
 *                            or the given subscription (thru handle) does not have
 *                            an active subscription or callbacks
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement
 *                               unsubscribe
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */
int cmsg_cmsg_unsubscribe(void *domainId, void *handle) {

  intptr_t index;
  int fd, status, err;
  cMsgDomainInfo *domain;
  struct iovec iov[3];
  cbArg           *cbarg;
  subInfo         *sub;
  subscribeCbInfo *cb, *cbItem, *cbPrev;
  cMsgMessage_t *msg, *nextMsg;
  void *p;


  /* check args */
  if (handle == NULL) {
      return(CMSG_BAD_ARGUMENT);
  }

  index = (intptr_t) domainId;
  if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

  /* make sure domain is usable */
  if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

  fd = domain->sendSocket;

  if (!domain->hasUnsubscribe) {
      cMsgCleanupAfterUsingMem((int)index);
      return(CMSG_NOT_IMPLEMENTED);
  }

  cbarg = (cbArg *)handle;
  if (cbarg->domainId != index) {
      cMsgCleanupAfterUsingMem((int)index);
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
      cMsgCleanupAfterUsingMem((int)index);
      return(CMSG_BAD_ARGUMENT);
  }

  tryagain:
  while (1) {
    err = CMSG_OK;

    cMsgConnectReadLock(domain);

    if (domain->gotConnection != 1) {
      cMsgConnectReadUnlock(domain);
      err = CMSG_LOST_CONNECTION;
      break;
    }

    /* make sure subscribe and unsubscribe are not run at the same time */
    cMsgSubscribeWriteLock(domain);

    /* Delete entry and notify server if there was at least 1 callback
     * to begin with and now there are none for this subject/type.
     */
    if (sub->numCallbacks - 1 < 1) {

      uint32_t len, lenSubject, lenType, outGoing[6];

      /* notify server */

      /* message id (in network byte order) to domain server */
      outGoing[1] = htonl(CMSG_UNSUBSCRIBE_REQUEST);
      /* unique id associated with subject/type */
      outGoing[2] = htonl((uint32_t)sub->id);
      /* length of "subject" string */
      lenSubject  = (uint32_t)strlen(sub->subject);
      outGoing[3] = htonl(lenSubject);
      /* length of "type" string */
      lenType     = (uint32_t)strlen(sub->type);
      outGoing[4] = htonl(lenType);
      /* length of "namespace" string (0 in this case, since
       * only used for server-to-server) */
      outGoing[5] = htonl(0);

      /* total length of message (minus first int) is first item sent */
      len = (uint32_t)sizeof(outGoing) - (uint32_t)sizeof(int) + lenSubject + lenType;
      outGoing[0] = htonl(len);

      iov[0].iov_base = (char*) outGoing;
      iov[0].iov_len  = sizeof(outGoing);

      iov[1].iov_base = (char*) sub->subject;
      iov[1].iov_len  = lenSubject;

      iov[2].iov_base = (char*) sub->type;
      iov[2].iov_len  = lenType;

      /* make send socket communications thread-safe */
      cMsgSocketMutexLock(domain);

      if (cMsgNetTcpWritev(fd, iov, 3, 16) == -1) {
        cMsgSocketMutexUnlock(domain);
        cMsgSubscribeWriteUnlock(domain);
        cMsgConnectReadUnlock(domain);

        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cmsg_cmsg_unsubscribe: write failure\n");
        }
        err = CMSG_NETWORK_ERROR;
        break;
      }

       /* done protecting communications */
      cMsgSocketMutexUnlock(domain);

      /* we told the server, now do the unsubscribe */

      /* remove subscription from the hash table */
      hashRemove(&domain->subscribeTable, cbarg->key, NULL);
      /* set resources free */
      cMsgSubscribeInfoFree(sub);
      free(sub);
    }
    /* else if not notifing server, just remove callback from list */
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
/*printf("cmsg_cmsg_unsubscribe: no cbs in sub list (?!), cannot remove cb\n");*/
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

    /* tell callback thread to end gracefully */
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

    domain->monData.numUnsubscribes++;

    /* done protecting unsubscribe */
    cMsgSubscribeWriteUnlock(domain);
    cMsgConnectReadUnlock(domain);

    /* Free arg mem */
    free(cbarg->key);
    free(cbarg);

    break;

  } /* while(1) */

  if (err!= CMSG_OK) {
    /* wait awhile for possible failover && resubscribe is complete */
    if (failoverSuccessful(domain, 1)) {
       fd = domain->sendSocket;
       if (cMsgDebug >= CMSG_DEBUG_ERROR) {
         fprintf(stderr, "cmsg_cmsg_unsubscribe: FAILOVER SUCCESSFUL, try unsubscribe again\n");
       }
       goto tryagain;
    }
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_cmsg_unsubscirbe: FAILOVER NOT successful, quitting, err = %d\n", err);
    }
  }

  cMsgCleanupAfterUsingMem((int)index);
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
 */
int cmsg_cmsg_subscriptionPause(void *domainId, void *handle) {

    intptr_t index;
    cMsgDomainInfo *domain;
    cbArg           *cbarg;
    subscribeCbInfo *cb;


    /* check args */
    if (handle == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    /* make sure domain is usable */
    if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

/*    if (!domain->hasPause) {
        cMsgCleanupAfterUsingMem(index);
        return(CMSG_NOT_IMPLEMENTED);
    }*/

    cbarg = (cbArg *)handle;
    if (cbarg->domainId != index) {
        cMsgCleanupAfterUsingMem((int)index);
        return(CMSG_BAD_ARGUMENT);
    }

    /* convenience variables */
    cb = cbarg->cb;

    cMsgMutexLock(&cb->mutex);

    /* tell callback thread to pause */
    cb->pause = 1;

    cMsgMutexUnlock(&cb->mutex);

    cMsgCleanupAfterUsingMem((int)index);
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
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement resume
 */
int cmsg_cmsg_subscriptionResume(void *domainId, void *handle) {

    intptr_t index;
    cMsgDomainInfo *domain;
    cbArg           *cbarg;
    subscribeCbInfo *cb;
    struct timespec wait = {1,0};


    /* check args */
    if (handle == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    /* make sure domain is usable */
    if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

/*    if (!domain->hasResume) {
    cMsgCleanupAfterUsingMem(index);
    return(CMSG_NOT_IMPLEMENTED);
}*/

    cbarg = (cbArg *)handle;
    if (cbarg->domainId != index) {
        cMsgCleanupAfterUsingMem((int)index);
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

    cMsgCleanupAfterUsingMem((int)index);
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
int cmsg_cmsg_subscriptionQueueCount(void *domainId, void *handle, int *count) {

    intptr_t index;
    cMsgDomainInfo *domain;
    cbArg           *cbarg;
    subscribeCbInfo *cb;


    /* check args */
    if (handle == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    /* make sure domain is usable */
    if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

/*    if (!domain->hasResume) {
    cMsgCleanupAfterUsingMem(index);
    return(CMSG_NOT_IMPLEMENTED);
}*/

    cbarg = (cbArg *)handle;
    if (cbarg->domainId != index) {
        cMsgCleanupAfterUsingMem((int)index);
        return(CMSG_BAD_ARGUMENT);
    }

    /* convenience variables */
    cb = cbarg->cb;

    cMsgMutexLock(&cb->mutex);

    /* tell callback thread to resume */
    if (count != NULL) *count = cb->messages;

    cMsgMutexUnlock(&cb->mutex);

    cMsgCleanupAfterUsingMem((int)index);
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
int cmsg_cmsg_subscriptionQueueIsFull(void *domainId, void *handle, int *full) {

    intptr_t index;
    cMsgDomainInfo *domain;
    cbArg           *cbarg;
    subscribeCbInfo *cb;


    /* check args */
    if (handle == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    /* make sure domain is usable */
    if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

/*    if (!domain->hasResume) {
    cMsgCleanupAfterUsingMem(index);
    return(CMSG_NOT_IMPLEMENTED);
}*/

    cbarg = (cbArg *)handle;
    if (cbarg->domainId != index) {
        cMsgCleanupAfterUsingMem((int)index);
        return(CMSG_BAD_ARGUMENT);
    }

    /* convenience variables */
    cb = cbarg->cb;

    cMsgMutexLock(&cb->mutex);

    /* tell callback thread to resume */
    if (full != NULL) *full = cb->fullQ;

    cMsgMutexUnlock(&cb->mutex);

    cMsgCleanupAfterUsingMem((int)index);
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
int cmsg_cmsg_subscriptionQueueClear(void *domainId, void *handle) {

    int i, status;
    intptr_t index;
    cMsgDomainInfo *domain;
    cbArg           *cbarg;
    subscribeCbInfo *cb;
    cMsgMessage_t *head;
    void *p;


    /* check args */
    if (handle == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    /* make sure domain is usable */
    if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

    cbarg = (cbArg *)handle;
    if (cbarg->domainId != index) {
        cMsgCleanupAfterUsingMem((int)index);
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
        cmsg_err_abort(status, "Failed callback condition signal in subQclear");
    }

    cMsgMutexUnlock(&cb->mutex);

    cMsgCleanupAfterUsingMem((int)index);
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
int cmsg_cmsg_subscriptionMessagesTotal(void *domainId, void *handle, int *total) {

    intptr_t index;
    cMsgDomainInfo *domain;
    cbArg           *cbarg;
    subscribeCbInfo *cb;


    /* check args */
    if (handle == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    /* make sure domain is usable */
    if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

/*    if (!domain->hasResume) {
    cMsgCleanupAfterUsingMem(index);
    return(CMSG_NOT_IMPLEMENTED);
}*/

    cbarg = (cbArg *)handle;
    if (cbarg->domainId != index) {
        cMsgCleanupAfterUsingMem((int)index);
        return(CMSG_BAD_ARGUMENT);
    }

    /* convenience variables */
    cb = cbarg->cb;

    cMsgMutexLock(&cb->mutex);

    /* tell callback thread to resume */
    if (total != NULL) *total = (int)cb->msgCount;

    cMsgMutexUnlock(&cb->mutex);

    cMsgCleanupAfterUsingMem((int)index);
    return(CMSG_OK);
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
int cmsg_cmsg_start(void *domainId) {
  intptr_t index;
  cMsgDomainInfo *domain;

  index = (intptr_t) domainId;
  if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

  cMsgMemoryMutexLock();
  domain = connectPointers[index];
  /* if bad index or disconnect has already been run, bail out */
  if (domain == NULL || domain->disconnectCalled) {
      cMsgMemoryMutexUnlock();
      return(CMSG_BAD_ARGUMENT);
  }
  domain->receiveState = 1;
  cMsgMemoryMutexUnlock();

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
 * @returns CMSG_BAD_ARGUMENT if domainId is bad
 */
int cmsg_cmsg_stop(void *domainId) {
    intptr_t index;
    cMsgDomainInfo *domain;

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    cMsgMemoryMutexLock();
    domain = connectPointers[index];
    /* if bad index or disconnect has already been run, bail out */
    if (domain == NULL || domain->disconnectCalled) {
        cMsgMemoryMutexUnlock();
        return(CMSG_BAD_ARGUMENT);
    }
    domain->receiveState = 0;
    cMsgMemoryMutexUnlock();

    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine disconnects the client from the cMsg server.
 * It kills threads, frees memory, etc. Also this routine tells
 * the server to kill its connection to this client.
 *
 * @param domainId pointer to id of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if domainId or the pointer it points to is NULL
 */
int cmsg_cmsg_disconnect(void **domainId) {

    void *p;
    intptr_t index;
    int status, outGoing[2];
    cMsgDomainInfo *domain;

    if (domainId == NULL) return(CMSG_BAD_ARGUMENT);
    index = (intptr_t) *domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    cMsgMemoryMutexLock();
    domain = connectPointers[index];
    if (domain == NULL || domain->disconnectCalled) {
        return(CMSG_BAD_ARGUMENT);
        cMsgMemoryMutexUnlock();
    }
    domain->functionsRunning++;
    cMsgMemoryMutexUnlock();

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "  cmsg_cmsg_disconnect: IN, index = %d, domain = %p, funcRunning = %d\n",
                (int)index, domain, domain->functionsRunning);
    }

    cMsgConnectWriteLock(domain);

    /* tell everyone we're disconnecting */
    domain->disconnectCalled = 1;
    /* calling "disconnect" takes precedence over failing-over to another server */
    domain->implementFailovers = 0;
    /* after calling disconnect, no top level API routines should function */
    domain->gotConnection = 0;


    /* Even though the keep alive thread will normally die when disconnectCalled = 1,
     * it will not if it has already detected server death and could not failover to
     * another server. In that case it is in an infinite loop of sleeps (at the end of
     * its algorithm) and needs to be cancelled. */
    domain->killKAthread = 1;
    pthread_cancel(domain->keepAliveThread);
    sched_yield();


    /* Tell server we're disconnecting */
    /* size of msg */
    outGoing[0] = htonl(4);
    /* message id (in network byte order) to domain server */
    outGoing[1] = htonl(CMSG_SERVER_DISCONNECT);

    /* make send socket communications thread-safe */
    cMsgSocketMutexLock(domain);

    /* send int */
    if (cMsgNetTcpWrite(domain->sendSocket, (char*) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
        /* if there is an error we are most likely in the process of disconnecting */
        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "  cmsg_cmsg_disconnect: write failure, but continue\n");
        }
    }

    cMsgSocketMutexUnlock(domain);


    /* allow KA thread to continue execution if blocked on this mutex */
    cMsgConnectWriteUnlock(domain);

    /* wait here until KA thread returns */
    status = pthread_join(domain->keepAliveThread, &p);
/*printf("  cmsg_cmsg_disconnect: joined w/ KA thd, status = %d\n",status);*/

    /* make sure other threads are dead too */
    cMsgConnectWriteLock(domain);
/*printf("  cmsg_cmsg_disconnect: Calling totalShutdown to kill everything !!!\n");*/
    totalShutdown(*domainId);
    cMsgConnectWriteUnlock(domain);

    cMsgMemoryMutexLock();
    domain->functionsRunning--;
    /* if no running functions use domain, go ahead a free memory */
/*printf("  cmsg_cmsg_disconnect:: grabbed mem mutex, index = %d, domain = %p, functionsRunning = %d\n",
    index, domain, domain->functionsRunning);*/

    if (domain != NULL && domain->functionsRunning < 1) {
        /* Unblock SIGPIPE */
        cMsgRestoreSignals(domain);

        /* Clean up memory */
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "  cmsg_cmsg_disconnect: free domain memory at %p  +++++++++++++++++\n", domain);
        }

        cMsgDomainFree(domain);
        free(domain);
        connectPointers[index] = NULL;
    }
    cMsgMemoryMutexUnlock();

    /* Set id (index) to -1 so no one can use it again.
     * NOTE: this does not change copied domainId's (possibly in other threads). */
    *domainId = (void *)(-1);

    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/

/**
 * This routine is called when NOT disconnecting permanently.
 * It closes sockets and wakes up all sub&Gets, send&Gets & syncSend,
 * but retains all subscriptions, threads, and memory. After calling
 * this function a reconnect can be done.
 * <b>This routine is always called with the cMsgConnectWriteLock
 * held.</b>
 *
 * @param domainId id of the domain connection
 * @param reconnecting 0 if not reconnected, else non-zero (1)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if domainId or the pointer it points to is NULL
 */
static int partialShutdown(void *domainId, int reconnecting) {

    intptr_t index;
    int i, status, tblSize;
    cMsgDomainInfo *domain;
    getInfo *info;
    struct timespec wait = {0, 10000000}; /* 0.01 sec */
    hashNode *entries = NULL;


    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
    domain = connectPointers[index];

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "@@partialShutdown: index = %d, domain = %p\n",(int)index, domain);
    }
    /*printf("partialShutdown: CLOSING ALL SOCKETS!!!\n");*/
    /* close sending TCP socket */
    close(domain->sendSocket);
    domain->sendSocket = -1; /* don't let other threads use this value */

    /* close sending UDP socket */
    close(domain->sendUdpSocket);
    domain->sendUdpSocket = -1;

    /* close keep alive socket */
    close(domain->keepAliveSocket);
    domain->keepAliveSocket = -1;

    /* wakeup all sub&gets */
    cMsgSubAndGetMutexLock(domain);
    hashClear(&domain->subAndGetTable, &entries, &tblSize);
    cMsgSubAndGetMutexUnlock(domain);
    if (entries != NULL) {
        for (i=0; i<tblSize; i++) {
            info = (getInfo *)entries[i].data;
            info->msg   = NULL;
            info->msgIn = 0;
            info->quit  = 1;

            if (cMsgDebug >= CMSG_DEBUG_INFO) {
                fprintf(stderr, "cmsg_cmsg_disconnect: wake up a sendAndGet\n");
            }

            /* wakeup "get" */
            status = pthread_cond_signal(&info->cond);
            if (status != 0) {
                cmsg_err_abort(status, "Failed get condition signal");
            }

            free(entries[i].key);
            /* Do NOT free info or run cMsgGetInfoFree here! That's
            * done in the cmsg_cmsg_subAndGet routine. If the disconnect
            * is done early in the subAndGet, everything is cleaned up.
            * If the disconnect is done while waiting for a msg to arrive,
            * the subAndGet will timeout and call free routines.
            */
        }
        free(entries);
    }

    /* wakeup all send&gets */
    cMsgSendAndGetMutexLock(domain);
    hashClear(&domain->sendAndGetTable, &entries, &tblSize);
    cMsgSendAndGetMutexUnlock(domain);
    if (entries != NULL) {
        for (i=0; i<tblSize; i++) {
            info = (getInfo *)entries[i].data;
            info->msg   = NULL;
            info->msgIn = 0;
            info->quit  = 1;

            if (cMsgDebug >= CMSG_DEBUG_INFO) {
                fprintf(stderr, "cmsg_cmsg_disconnect:wake up a sendAndGet\n");
            }

            /* wakeup "get" */
            status = pthread_cond_signal(&info->cond);
            if (status != 0) {
                cmsg_err_abort(status, "Failed get condition signal");
            }

            free(entries[i].key);
        }
        free(entries);
    }

    /* wakeup all syncSends */
    cMsgSyncSendMutexLock(domain);
    hashClear(&domain->syncSendTable, &entries, &tblSize);
    cMsgSyncSendMutexUnlock(domain);
    if (entries != NULL) {
        for (i=0; i<tblSize; i++) {
            info = (getInfo *)entries[i].data;
            info->msg = NULL;
            info->msgIn = 0;
            info->quit  = 1;

            if (cMsgDebug >= CMSG_DEBUG_INFO) {
                fprintf(stderr, "cmsg_cmsg_disconnect:wake up a syncSend\n");
            }

            /* wakeup the syncSend */
            status = pthread_cond_signal(&info->cond);
            if (status != 0) {
                cmsg_err_abort(status, "Failed get condition signal");
            }

            free(entries[i].key);
        }
        free(entries);
    }

    if (!reconnecting) {
        /* "wakeup" all sends/syncSends that have failed and
         * are waiting to failover in failoverSuccessful. */
        cMsgLatchCountDown(&domain->syncLatch, &wait);
    }

    /*printf("  partialShutdown: done\n");*/

    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/

/**
 * This routine is called when disconnecting from the server permanently.
 * It always closes sockets, stops the message-listening and
 * server-update threads and wakes up all sub&Gets, send&Gets & syncSends.
 * It also removes subscriptions and stops callback threads.
 * <b>This routine is always called with the cMsgConnectWriteLock
 * held.</b>
 *
 * @param domainId id of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if domainId or the pointer it points to is NULL
 */
static int totalShutdown(void *domainId) {

    intptr_t index;
    int i, status, tblSize;
    cMsgDomainInfo *domain;
    subscribeCbInfo *cb, *cbNext;
    subInfo *sub;
    hashNode *entries = NULL;
    cMsgMessage_t *msg, *nextMsg;
    void *p;


    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
    domain = connectPointers[index];

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "@@totalShutdown: index = %d, domain = %p\n",(int)index, domain);
    }

    domain->gotConnection = 0;

    partialShutdown(domainId, 0);

    /* Don't want callbacks' queues to be full so remove all messages.
     * Don't quit callback threads yet since that frees callback memory
     * and the pend thread may still be iterating through callbacks.
     * Don't remove subscriptions now since a callback queue may be full
     * which means the pend thread is stuck and has the subscribe read mutex.
     * To remove subscriptions we need to grab the subscribe
     * write mutex which would not be possible in that case.
     * So free up pend thread in case it's stuck and remove
     * callback threads and subscriptions later. */
    /*printf("  totalShutdown: try grabbing sub read mutex\n");*/
    cMsgSubscribeReadLock(domain);
    /*printf("  totalShutdown: grabbed sub read mutex\n");*/

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
                /*printf("  totalShutdown: try grabbing cb mutex, mutex = %p\n", &cb->mutex);*/
                cMsgMutexLock(&cb->mutex);
                /*printf("  totalShutdown: grabbed cb mutex\n");*/

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
                /*printf("  totalShutdown: released cb mutex, mutex = %p\n",&cb->mutex);*/

                cb = cb->next;
            } /* next callback */
        } /* next subscription */
        free(entries);
    } /* if there are subscriptions */

    cMsgSubscribeReadUnlock(domain);
    sched_yield();

    /* The keep alive thread should already be ended at this point (cmsg_cmsg_disconnect). */

    /* stop thread writing keep alives to server */
    /*printf("  totalShutdown: cancel updateServer thd\n");*/
    status = pthread_cancel(domain->updateServerThread);
    /*
    if (status != 0) {
        cmsg_err_abort(status, "Failed updateServer thread cancellation");
    }*/

    /* wait here until thread writing keep alives returns */
    sched_yield();
    status = pthread_join(domain->updateServerThread, &p);
    /*
    if (status != 0 || p != PTHREAD_CANCELED) {
        cmsg_err_abort(status, "Failed updateServer thread join");
    }
    */
    /*printf("  totalShutdown: joined updateServer thread\n");*/

    /* Theoretically the stuck pend thread is free now so go ahead and cancel it. */

    /* stop msg receiving thread */
    /*printf("  totalShutdown: cancel pend thd = %p\n",&domain->pendThread);*/
    status = pthread_cancel(domain->pendThread);
    /*
    if (status != 0) {
        cmsg_err_abort(status, "Failed pend thread cancellation");
    }
    */

    /* wait here until msg receiving thread returns */
    sched_yield();
    /*printf("  totalShutdown: try joining pend thread = %p\n",&domain->pendThread);*/
    status = pthread_join(domain->pendThread, &p);
    /*
    if (status != 0 || p != PTHREAD_CANCELED) {
        cmsg_err_abort(status, "Failed pend thread join");
    }
    */
    /*printf("  totalShutdown: joined pend thread\n");*/

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
                /*printf("  totalShutdown: try grabbing cb mutex 2, mutex = %p\n", &cb->mutex);*/
                cMsgMutexLock(&cb->mutex);
                /*printf("  totalShutdown: grabbed cb mutex 2\n");*/

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
                /*printf("  totalShutdown: released cb mutex 2, mutex = %p\n", &cb->mutex);*/

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

    /*printf("  totalShutdown: done\n");*/
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
 * @returns CMSG_BAD_ARGUMENT if the id is bad
 */
int cmsg_cmsg_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg) {

    intptr_t index;
    cMsgDomainInfo *domain;

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    cMsgMemoryMutexLock();
    domain = connectPointers[index];
    if (domain == NULL || domain->disconnectCalled) {
        cMsgMemoryMutexUnlock();
        return(CMSG_BAD_ARGUMENT);
    }
    domain->shutdownHandler = handler;
    domain->shutdownUserArg = userArg;
    cMsgMemoryMutexUnlock();

    return CMSG_OK;
}


/*-------------------------------------------------------------------*/


/**
 * Method to shutdown the given clients.
 *
 * @param domainId id of the domain connection
 * @param client client(s) to be shutdown
 * @param flag   flag describing the mode of shutdown: 0 to not include self,
 *               CMSG_SHUTDOWN_INCLUDE_ME to include self in shutdown.
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id is bad
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement shutdown
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 */
int cmsg_cmsg_shutdownClients(void *domainId, const char *client, int flag) {

    intptr_t index;
    int fd;
    uint32_t len, cLen, outGoing[4];
    cMsgDomainInfo *domain;
    struct iovec iov[2];


    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

    fd = domain->sendSocket;

    if (!domain->hasShutdown) {
        cMsgCleanupAfterUsingMem((int)index);
        return(CMSG_NOT_IMPLEMENTED);
    }

    cMsgConnectWriteLock(domain);

    /* message id (in network byte order) to domain server */
    outGoing[1] = htonl(CMSG_SHUTDOWN_CLIENTS);
    outGoing[2] = htonl((uint32_t)flag);

    if (client == NULL) {
        cLen = 0;
        outGoing[3] = 0;
    }
    else {
        cLen = (uint32_t)strlen(client);
        outGoing[3] = htonl(cLen);
    }

    /* total length of message (minus first int) is first item sent */
    len = (uint32_t)sizeof(outGoing) - (uint32_t)sizeof(int) + cLen;
    outGoing[0] = htonl(len);

    iov[0].iov_base = (char*) outGoing;
    iov[0].iov_len  = sizeof(outGoing);

    iov[1].iov_base = (char*) client;
    iov[1].iov_len  = cLen;

    /* make send socket communications thread-safe */
    cMsgSocketMutexLock(domain);

    if (cMsgNetTcpWritev(fd, iov, 2, 16) == -1) {
        cMsgSocketMutexUnlock(domain);
        cMsgConnectWriteUnlock(domain);
        cMsgCleanupAfterUsingMem((int)index);
        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "cmsg_cmsg_unsubscribe: write failure\n");
        }
        return(CMSG_NETWORK_ERROR);
    }

    cMsgSocketMutexUnlock(domain);
    cMsgConnectWriteUnlock(domain);
    cMsgCleanupAfterUsingMem((int)index);

    return CMSG_OK;

}


/*-------------------------------------------------------------------*/


/**
 * Method to shutdown the given servers.
 *
 * @param domainId id of the domain connection
 * @param server server(s) to be shutdown
 * @param flag   flag describing the mode of shutdown: 0 to not include self,
 *               CMSG_SHUTDOWN_INCLUDE_ME to include self in shutdown.
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id is bad
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement shutdown
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 */
int cmsg_cmsg_shutdownServers(void *domainId, const char *server, int flag) {

    intptr_t index;
    int fd;
    uint32_t len, sLen, outGoing[4];
    cMsgDomainInfo *domain;
    struct iovec iov[2];

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = cMsgPrepareToUseMem((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);

    fd = domain->sendSocket;

    if (!domain->hasShutdown) {
        cMsgCleanupAfterUsingMem((int)index);
        return(CMSG_NOT_IMPLEMENTED);
    }

    cMsgConnectWriteLock(domain);

    /* message id (in network byte order) to domain server */
    outGoing[1] = htonl(CMSG_SHUTDOWN_SERVERS);
    outGoing[2] = htonl((uint32_t)flag);

    if (server == NULL) {
        sLen = 0;
        outGoing[3] = 0;
    }
    else {
        sLen = (uint32_t)strlen(server);
        outGoing[3] = htonl(sLen);
    }

    /* total length of message (minus first int) is first item sent */
    len = (uint32_t)sizeof(outGoing) - (uint32_t)sizeof(int) + sLen;
    outGoing[0] = htonl(len);

    iov[0].iov_base = (char*) outGoing;
    iov[0].iov_len  = sizeof(outGoing);

    iov[1].iov_base = (char*) server;
    iov[1].iov_len  = sLen;

    /* make send socket communications thread-safe */
    cMsgSocketMutexLock(domain);

    if (cMsgNetTcpWritev(fd, iov, 2, 16) == -1) {
        cMsgSocketMutexUnlock(domain);
        cMsgConnectWriteUnlock(domain);
        cMsgCleanupAfterUsingMem((int)index);
        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "cmsg_cmsg_unsubscribe: write failure\n");
        }
        return(CMSG_NETWORK_ERROR);
    }

    cMsgSocketMutexUnlock(domain);
    cMsgConnectWriteUnlock(domain);
    cMsgCleanupAfterUsingMem((int)index);

    return CMSG_OK;

}


/*-------------------------------------------------------------------*/


/**
 * This routine exchanges information with the name server.
 *
 * @param domain  pointer to element in domain info array
 * @param serverfd  socket to send to cMsg name server
 * @param uniqueClientKey pointer to int which gets filled in with unique id sent by server
 *                        to use in response communications
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server (can't read or write)
 *
 */
static int talkToNameServer(cMsgDomainInfo *domain, int serverfd, int *uniqueClientKey) {
  int err;
  uint32_t  lengthDomain, lengthSubdomain, lengthRemainder, lengthPassword;
  uint32_t  lengthHost, lengthName, lengthUDL, lengthDescription, outGoing[15], inComing[4];
  char temp[CMSG_MAXHOSTNAMELEN], atts[7];
  const char *domainType = "cMsg";
  struct iovec iov[9];
  parsedUDL *pUDL = &domain->currentUDL;

  /* first send magic #s to server which identifies us as real cMsg client */
  outGoing[0] = htonl(CMSG_MAGIC_INT1);
  outGoing[1] = htonl(CMSG_MAGIC_INT2);
  outGoing[2] = htonl(CMSG_MAGIC_INT3);
  /* message id (in network byte order) to server */
  outGoing[3] = htonl(CMSG_SERVER_CONNECT);
  /* major version number */
  outGoing[4] = htonl(CMSG_VERSION_MAJOR);
  /* minor version number */
  outGoing[5] = htonl(CMSG_VERSION_MINOR);
  /* send regime value to server */
  outGoing[6] = htonl((uint32_t)pUDL->regime);
  /* send length of password for connecting to server.*/
  if (pUDL->password == NULL) {
    lengthPassword = outGoing[7] = 0;
  }
  else {
    lengthPassword = (uint32_t)strlen(pUDL->password);
    outGoing[7]    = htonl(lengthPassword);
  }
  /* send length of the type of domain server I'm expecting to connect to.*/
  lengthDomain = (uint32_t)strlen(domainType);
  outGoing[8]  = htonl(lengthDomain);
  /* send length of the type of subdomain handler I'm expecting to use.*/
  lengthSubdomain = (uint32_t)strlen(pUDL->subdomain);
  outGoing[9] = htonl(lengthSubdomain);
  /* send length of the UDL remainder.*/
  /* this may be null */
  if (pUDL->subRemainder == NULL) {
    lengthRemainder = outGoing[10] = 0;
  }
  else {
    lengthRemainder = (uint32_t)strlen(pUDL->subRemainder);
    outGoing[10] = htonl(lengthRemainder);
  }
  /* send length of my host name to server */
  lengthHost   = (uint32_t)strlen(domain->myHost);
  outGoing[11] = htonl(lengthHost);
  /* send length of my name to server */
  lengthName   = (uint32_t)strlen(domain->name);
  outGoing[12] = htonl(lengthName);
  /* send length of my udl to server */
  lengthUDL    = (uint32_t)strlen(pUDL->udl);
  outGoing[13] = htonl(lengthUDL);
  /* send length of my description to server */
  lengthDescription = (uint32_t)strlen(domain->description);
  outGoing[14]      = htonl(lengthDescription);

  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);

  iov[1].iov_base = pUDL->password;
  iov[1].iov_len  = lengthPassword;

  iov[2].iov_base = (char*) domainType;
  iov[2].iov_len  = lengthDomain;

  iov[3].iov_base = pUDL->subdomain;
  iov[3].iov_len  = lengthSubdomain;

  iov[4].iov_base = pUDL->subRemainder;
  iov[4].iov_len  = lengthRemainder;

  iov[5].iov_base = domain->myHost;
  iov[5].iov_len  = lengthHost;

  iov[6].iov_base = domain->name;
  iov[6].iov_len  = lengthName;

  iov[7].iov_base = pUDL->udl;
  iov[7].iov_len  = lengthUDL;

  iov[8].iov_base = domain->description;
  iov[8].iov_len  = lengthDescription;

  if (cMsgNetTcpWritev(serverfd, iov, 9, 16) == -1) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "talkToNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* now read server reply */
  if (cMsgNetTcpRead(serverfd, (void *) &err, sizeof(err)) != sizeof(err)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "talkToNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  err = ntohl((uint32_t)err);

  /* if there's an error, read error string then quit */
  if (err != CMSG_OK) {
    uint32_t len;
    char *string;

    /* read length of error string */
    if (cMsgNetTcpRead(serverfd, (char*) &len, sizeof(len)) != sizeof(len)) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "talkToNameServer: read failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }
    len = ntohl(len);

    /* allocate memory for error string */
    string = (char *) malloc((size_t) (len+1));
    if (string == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "talkToNameServer: cannot allocate memory\n");
      }
      return(CMSG_OUT_OF_MEMORY);
    }

    if (cMsgNetTcpRead(serverfd, string, len) != len) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "talkToNameServer: cannot read error string\n");
      }
      free(string);
      return(CMSG_NETWORK_ERROR);
    }
    /* add null terminator to C string */
    string[len] = '\0';

    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "talkToNameServer: %s\n", string);
    }

    free(string);
    return(err);
  }

  /*
   * if everything's OK, we expect to get:
   *   1) attributes of subdomain handler
   *   2) host & port
   */

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "talkToNameServer: read subdomain handler attributes\n");
  }

  /* read whether subdomain has various functions implemented */
  if (cMsgNetTcpRead(serverfd, (char*) atts, sizeof(atts)) != sizeof(atts)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "talkToNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* store attributes of the subdomain handler being used */
  if (atts[0] == 1) domain->hasSend            = 1;
  if (atts[1] == 1) domain->hasSyncSend        = 1;
  if (atts[2] == 1) domain->hasSubscribeAndGet = 1;
  if (atts[3] == 1) domain->hasSendAndGet      = 1;
  if (atts[4] == 1) domain->hasSubscribe       = 1;
  if (atts[5] == 1) domain->hasUnsubscribe     = 1;
  if (atts[6] == 1) domain->hasShutdown        = 1;

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "talkToNameServer: read subdomain handler attributes = \n");
      fprintf(stderr, "                  hasSend = %d\n", domain->hasSend);
      fprintf(stderr, "                  hasSyncSend = %d\n", domain->hasSyncSend);
      fprintf(stderr, "                  hasSubscribeAndGet = %d\n", domain->hasSubscribeAndGet);
      fprintf(stderr, "                  hasSendAndGet = %d\n", domain->hasSendAndGet);
      fprintf(stderr, "                  hasSubscribe = %d\n", domain->hasSubscribe);
      fprintf(stderr, "                  hasUnsubscribe = %d\n", domain->hasUnsubscribe);
      fprintf(stderr, "                  hasShutdown = %d\n", domain->hasShutdown);
      fprintf(stderr, "talkToNameServer: read port and length of host from server\n");
  }

  /* read port & length of host name to send to*/
  if (cMsgNetTcpRead(serverfd, (char*) inComing, sizeof(inComing)) != sizeof(inComing)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "talkToNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  if (uniqueClientKey != NULL) {
    *uniqueClientKey  = ntohl(inComing[0]);
  }
  domain->sendPort    = (int)ntohl(inComing[1]);
  domain->sendUdpPort = (int)ntohl(inComing[2]);
  lengthHost          = ntohl(inComing[3]);

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "talkToNameServer: port = %d, host len = %d\n",
              domain->sendPort, lengthHost);
    fprintf(stderr, "talkToNameServer: read host from server\n");
  }

  /* read host name to send to */
  if (cMsgNetTcpRead(serverfd, (char*) temp, lengthHost) != lengthHost) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "talkToNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  /* be sure to null-terminate string */
  temp[lengthHost] = '\0';
  if (domain->sendHost != NULL) free(domain->sendHost);
  domain->sendHost = strdup(temp);
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "talkToNameServer: host = %s\n", domain->sendHost);
  }

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/**
 * This method reads monitoring data from the server.
 * @param domain  pointer to element in domain info array
 * @returns CMSG_OK if successful
 * @returns CMSG_NETWORK_ERROR if network read error
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 */
static int getMonitorInfo(cMsgDomainInfo *domain) {

  int err, items;
  uint32_t len;
/*printf("getMonitorInfo: domain = %p, ka socket = %d\n", domain, domain->keepAliveSocket);*/

  /* read len of monitoring data to come */
  if ((err = cMsgNetTcpRead(domain->keepAliveSocket, &len, sizeof(len))) != sizeof(len)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getMonitorInfo: read failure 1, domain = %p\n", domain);
    }
    return CMSG_NETWORK_ERROR;
  }
  len = ntohl(len);

  /* check for room in memory */
  if (len > domain->monitorXMLSize) {
    if (domain->monitorXML != NULL) free(domain->monitorXML);
    domain->monitorXML = (char *) malloc(len+1);
    domain->monitorXMLSize = len;
    if (domain->monitorXML == NULL) {
      return CMSG_OUT_OF_MEMORY;
    }
  }

  /* read monitoring data */
  if ((err = cMsgNetTcpRead(domain->keepAliveSocket, domain->monitorXML, len)) !=  len) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getMonitorInfo: read failure 2\n");
    }
    return CMSG_NETWORK_ERROR;
  }

  /* make sure the string is properly terminated */
  domain->monitorXML[len] = '\0';

  /* read number of items to come */
  if ((err = cMsgNetTcpRead(domain->keepAliveSocket, &items, sizeof(items))) != sizeof(items)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getMonitorInfo: read failure 3\n");
    }
    return CMSG_NETWORK_ERROR;
  }
  items = (int)ntohl((uint32_t)items);

  if (items > 0) {

    /* read info about servers in the cloud (for failing over to cloud members) */
    int tcpPort, udpPort, i, size;
    int numServers, isLocal;
    uint32_t hlen, plen, inComing[4];

    hashNode *entries;
    hashClear(&domain->cloudServerTable, &entries, &size);
    if (entries != NULL) {
      for (i=0; i<size; i++) {
        free(entries[i].key);
        cMsgParsedUDLFree((parsedUDL *)entries[i].data);
        free(entries[i].data);
      }
      free(entries);
    }

    /* assume no local cloud servers */
    domain->haveLocalCloudServer = 0;

    /* read number of servers to come */
    if ((err = cMsgNetTcpRead(domain->keepAliveSocket, &numServers, sizeof(numServers))) !=
               sizeof(numServers)) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "getMonitorInfo: read failure 4\n");
      }
      return CMSG_NETWORK_ERROR;
    }
    numServers = ntohl((uint32_t)numServers);
/*printf("getMonitorInfo: num servers = %d\n", numServers);*/

    if (numServers > 0) {
      for (i=0; i<numServers; i++) {
        parsedUDL *p;
        isLocal = 0;

        if ((err = cMsgNetTcpRead(domain->keepAliveSocket, inComing, sizeof(inComing))) !=
                   sizeof(inComing)) {
          if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "getMonitorInfo: read failure 5\n");
          }
          return CMSG_NETWORK_ERROR;
        }

        tcpPort = (int)ntohl(inComing[0]);
        udpPort = (int)ntohl(inComing[1]);
        hlen    = ntohl(inComing[2]); /* host string len */
        plen    = ntohl(inComing[3]); /* password string len */

        p = (parsedUDL *) calloc(1, sizeof(parsedUDL));
        if (p == NULL) {
          return CMSG_OUT_OF_MEMORY;
        }

        p->nameServerPort    = tcpPort;
        p->nameServerUdpPort = udpPort;

        /* host this cloud server is on */
        if (hlen > 0) {
          p->nameServerHost = (char *) malloc(hlen+1);
          if (p->nameServerHost == NULL) {
            free(p);
            return CMSG_OUT_OF_MEMORY;
          }

          /* read host name */
          if (cMsgNetTcpRead(domain->keepAliveSocket, p->nameServerHost, hlen) != hlen) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "getMonitorInfo: read failure 6\n");
            }
            free(p->nameServerHost);
            free(p);
            return(CMSG_NETWORK_ERROR);
          }

          /* be sure to null-terminate string */
          p->nameServerHost[hlen] = '\0';
          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "getMonitorInfo: host = %s\n", p->nameServerHost);
          }

          cMsgNetNodeIsLocal(p->nameServerHost, &isLocal);
          domain->haveLocalCloudServer |= isLocal;
        }


        /* password this cloud server requires */
        if (plen > 0) {
          p->password = (char *) malloc((size_t)(plen+1));
          if (p->password == NULL) {
            if (p->nameServerHost != NULL) free(p->nameServerHost);
            free(p);
            return CMSG_OUT_OF_MEMORY;
          }

          /* read host name */
          if (cMsgNetTcpRead(domain->keepAliveSocket, p->password, plen) != plen) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "getMonitorInfo: read failure 7\n");
            }
            if (p->nameServerHost != NULL) free(p->nameServerHost);
            free(p->password);
            free(p);
            return(CMSG_NETWORK_ERROR);
          }

          /* be sure to null-terminate string */
          p->password[plen] = '\0';
          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "getMonitorInfo: password = %s\n", p->password);
          }
        }

        /* construct cloud server name */
        len = (uint32_t)strlen(p->nameServerHost) + cMsgNumDigits(tcpPort,0) + 1;
        p->serverName = (char *)malloc(len+1);
        if (p->serverName == NULL) {
          if (p->nameServerHost != NULL) free(p->nameServerHost);
          if (p->password != NULL) free(p->password);
          free(p);
          return CMSG_OUT_OF_MEMORY;
        }
        sprintf(p->serverName, "%s:%d",p->nameServerHost, tcpPort);
        p->serverName[len] = '\0';
/*
printf("getMonitorInfo: cloud server => tcpPort = %d, udpPort = %d, host = %s, passwd = %s\n",
        tcpPort, udpPort, p->nameServerHost, p->password);
*/
        /* store info in hash table */
        hashInsert(&domain->cloudServerTable, p->serverName, (void *)p, NULL);

      }
    }
  }

  return CMSG_OK;
}


/**
 * Try to connect to new failover server. It may or may not be part of a cloud.
 *
 * @param domainId id of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no server IP addresses found in multicast response
 * @returns CMSG_BAD_ARGUMENT if arg is bad id
 * @returns CMSG_OUT_OF_MEMORY if the allocating memory failed
 * @returns CMSG_TIMEOUT if timed out of wait for response to multicast
 * @returns CMSG_SOCKET_ERROR if udp socket for multicasting could not be created,
 *                            or if socket options to TCP server connection could not be set
 * @returns CMSG_NETWORK_ERROR if no connection to the name or domain servers can be made,
 *                             or a communication error with either server occurs.
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed while
 *                               restoring subscriptions
 */
static int connectToServer(void *domainId) {
    intptr_t index;
    int err, len;
    cMsgDomainInfo *domain;
    codaIpList *ipList = NULL;

    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
    domain = connectPointers[index];
    if (domain == NULL) return(CMSG_BAD_ARGUMENT);

  /* No multicast is ever done if failing over to cloud server
   * since all cloud servers' info contains real host name and
   * TCP port only. */
  if (domain->currentUDL.mustMulticast) {
    if (domain->currentUDL.nameServerHost != NULL) {
        free(domain->currentUDL.nameServerHost);
        domain->currentUDL.nameServerHost = NULL;
    }
/*printf("KA: trying to connect with Multicast\n");*/
    err = connectWithMulticast(domain, &ipList,
                               &domain->currentUDL.nameServerPort);
    if (err != CMSG_OK) {
/*printf("KA: error trying to connect with Multicast, err = %d\n", err);*/
      /* returns CMSG_OK if successful
                CMSG_BAD_FORMAT if UDLremainder arg is not in the proper format or is NULL
                CMSG_OUT_OF_MEMORY if the allocating memory failed
                CMSG_TIMEOUT if timed out of wait for response to multicast
                CMSG_SOCKET_ERROR if udp socket for multicasting could not be created
      */
      return err;
    }
    else if (ipList == NULL) {
        return CMSG_ERROR;
    }
  }

  err = reconnect(domainId, ipList);
  cMsgNetFreeAddrList(ipList);

  if (err != CMSG_OK) {
    /* @returns CMSG_OK if successful
                CMSG_OUT_OF_MEMORY if the allocating memory failed
                CMSG_SOCKET_ERROR if socket options could not be set
                CMSG_NETWORK_ERROR if no connection to the name or domain servers can be made,
                                      or a communication error with either server occurs.
    */
    return err;
  }


  /*printf("connectToServer: Connected!!\n");*/

  /* restore subscriptions on the new server */
  if ( (err = restoreSubscriptions(domain)) != CMSG_OK) {
    /* if subscriptions fail, then we do NOT use failover server */
    partialShutdown(domainId, 1);
    /* @returns CMSG_OK if successful
                CMSG_NETWORK_ERROR if error in communicating with the server
                CMSG_LOST_CONNECTION if the network connection to the server was closed
    */
    return err;
  }
  domain->resubscribeComplete = 1;

  /* Now that we're reconnected, we must recreate the name
   * of the server that we just connected to. */
  if (domain->currentUDL.serverName != NULL) {
    free(domain->currentUDL.serverName);
  }
  len = (int) strlen(domain->currentUDL.nameServerHost) +
        cMsgNumDigits(domain->currentUDL.nameServerPort, 0) + 1;
  domain->currentUDL.serverName = (char *)malloc((size_t)(len+1));
  if (domain->currentUDL.serverName == NULL) {
    return CMSG_OUT_OF_MEMORY;
  }
  sprintf(domain->currentUDL.serverName, "%s:%d",domain->currentUDL.nameServerHost,
          domain->currentUDL.nameServerPort);
  domain->currentUDL.serverName[len] = '\0';

  return CMSG_OK;
}



/**
 * This routine is run as a thread which is used to read keep alive
 * communication (monitoring info in XML format) from a cMsg server.
 * If there is an I/O error, the other end of the socket & the server
 * is presumed dead.
 *
 * NOTE: When creating a new thread and passing the domain structure,
 * pass the original pointer to the pointer to the struct (see domainId
 * below)!! Thus when disconnect is done and the pointer to struct is
 * set to NULL, all threads will see it. This allows calls to other
 * cMsg functions detect a NULL pointer and return an error instead of
 * seg faulting.
 */
static void *keepAliveThread(void *arg) {

    intptr_t index;
    void *domainId = arg;
    cMsgDomainInfo *domain;
    int err, i, size, plen, noMoreCloudServers;
    size_t len;
    hashNode *entries = NULL;

    int failoverIndex, failedFailoverIndex, weGotAConnection = 1;
    struct timespec wait4failover   = {1,100000000}; /* 1.1 sec */
    struct timespec wait4reconnect  = {0,100000000}; /* 0.1 sec */


    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) {
       if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
           fprintf(stderr, "keepAliveThread: bad value for domain, ending thread\n");
       }
       pthread_exit(NULL);
    }

    domain = connectPointers[index];
    if (domain == NULL) {
       if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
           fprintf(stderr, "keepAliveThread: bad value for domain, ending thread\n");
       }
        pthread_exit(NULL);
    }

/*printf("ka: arg = %p, domainId = %p, domain = %p\n", arg, domainId, domain);*/

    /* periodically send a keep alive message and read response */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "keepAliveThread: keep alive thread created, socket = %d\n",
              domain->keepAliveSocket);
    }


    top:
    while (weGotAConnection) {

        while(1) {
            if (domain->killKAthread) pthread_exit(NULL);
            if (getMonitorInfo(domain) != CMSG_OK) {
            break;
          }
        }

        cMsgConnectWriteLock(domain);

        domain->gotConnection = 0;
        weGotAConnection      = 0;
/*printf("\nKA: domain server is probably dead, dis/reconnect\n");*/

        if (domain->killKAthread || domain->disconnectCalled) {
            /* disconnect will call totalShutdown */
            cMsgConnectWriteUnlock(domain);
            pthread_exit(NULL);
        }

        if (!domain->implementFailovers) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "keepAliveThread: user called disconnect and/or server terminated connection\n");
            }
            /* If server is gone (even if we have no failovers), it's still possible
             * for the user to call "reconnect" and retain his subscriptions. */
            cMsgConnectWriteUnlock(domain);
            break;
        }

        /* if we're here, disconnect was NOT called, server died and we're trying to failover */
        noMoreCloudServers = 0;
        if (hashSize(&domain->cloudServerTable) < 1) {
          noMoreCloudServers = 1;
        }

          while (!weGotAConnection) {
            /* If NOT we're failing over to the cloud first, skip this part */
            if (domain->currentUDL.failover != CMSG_FAILOVER_CLOUD &&
                domain->currentUDL.failover != CMSG_FAILOVER_CLOUD_ONLY) {
              break;
            }
/*printf("KA: trying to failover to cloud member\n");*/

            /* If we want to failover locally, but there is no local cloud server,
             * or there are no cloud servers of any kind ... */
            if ((domain->currentUDL.cloud == CMSG_CLOUD_LOCAL && !domain->haveLocalCloudServer) ||
                 noMoreCloudServers) {
/*printf("KA: No cloud members to failover to (else not the desired local ones)\n");*/
              /* try the next UDL */
              if (domain->currentUDL.failover == CMSG_FAILOVER_CLOUD) {
/*printf("KA: so go to next UDL\n");*/
                break;
              }
              /* if we must failover to cloud only, disconnect */
              else {
/*printf("KA: so just disconnect\n");*/
                partialShutdown(domainId, 0);
                cMsgConnectWriteUnlock(domain);
                pthread_exit(NULL);
              }
            }

            /* look through list of cloud servers */
            if (hashGetAll(&domain->cloudServerTable, &entries, &size) == 0) {
                partialShutdown(domainId, 0);
                cMsgConnectWriteUnlock(domain);
                pthread_exit(NULL);
            }
/*printf("KA: look thru list of cloud servers:\n");*/

            if (entries != NULL) {
              parsedUDL *pUdl;
              char *sName;
              for (i=0; i<size; i++) {

                sName = entries[i].key;
                pUdl  = (parsedUDL *)entries[i].data;

/*printf("KA: try (s)name = %s, current server name = %s\n", sName, domain->currentUDL.serverName);*/

                /*
                 * If we were connected to one of the cloud servers,
                 * and had time to receive a monitor packet from it,
                 * the list of cloud servers should NOT contain that
                 * server (the one that just failed). If there was not
                 * sufficient connection time (2 sec) to get a monitor
                 * packet before the connection died, then we have the
                 * old list of cloud servers which could contain the
                 * one that just failed. Be sure to filter that one out
                 * now and try the next one.
                 */
                if (strcmp(sName, domain->currentUDL.serverName) == 0) {
                  continue;
                }

                /* if we can failover to anything or this cloud server is local */
                if ((domain->currentUDL.cloud == CMSG_CLOUD_ANY) ||
                     pUdl->isLocal) {
                  char *newSubRemainder=NULL;

                  /*
                   * Construct our own "parsed" UDL using server's name and
                   * current values of other parameters.
                   *
                   * Be careful with the password which, I believe, is the
                   * only server-dependent part of "subRemainder" and must
                   * be explicitly substituted for.
                   *
                   * if current UDL has a password in remainder ...
                   */
                  if (domain->currentUDL.password != NULL &&
                      strlen(domain->currentUDL.password) > 0) {

                    const char *pattern = "[&\\?]cmsgpassword=([^&]+)";
                    regmatch_t matches[2]; /* we have 2 potential matches: 1 whole, 1 sub */
                    regex_t    compiled;

                    /* compile regular expression */
                    err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
                    if (err != 0) {
                        break;
                    }

                    /* try finding exact postion of match that must be there */
                    err = cMsgRegexec(&compiled, domain->currentUDL.subRemainder, 2, matches, 0);
                    /* if no match found ... */
                    if (err != 0 || matches[1].rm_so < 0) {
                      /* self-contradictory results */
                      partialShutdown(domainId, 0);
                      free(entries);
                      cMsgConnectWriteUnlock(domain);
                      pthread_exit(NULL);
                    }

                    /* replace old with new password */
                    if (pUdl->password != NULL && strlen(pUdl->password) > 0) {
/*printf("Replacing old pswd with new one\n");*/
                      /* length of existing password */
                      plen = matches[1].rm_eo - matches[1].rm_so;
                      /* len of new subRemainder string with substituted/new password */
                      len = strlen(domain->currentUDL.subRemainder) -
                                         plen + strlen(pUdl->password) + 1;

                      /* now create the new subRemainder string w/ new password */
                      newSubRemainder = (char *)calloc(1,len);
                      if (newSubRemainder == NULL) {
                        partialShutdown(domainId, 0);
                        free(entries);
                        cMsgConnectWriteUnlock(domain);
                        pthread_exit(NULL);
                      }
                      strncat(newSubRemainder, domain->currentUDL.subRemainder, matches[1].rm_so);
                      strcat (newSubRemainder, pUdl->password);
                      strcat (newSubRemainder, domain->currentUDL.subRemainder+matches[1].rm_eo);
/*printf("new subRemainder = %s\n", newSubRemainder);*/
                    }
                    /* no new password so eliminate password altogether */
                    else {
                      /* length of whole password section */
                      plen = matches[0].rm_eo - matches[0].rm_so;

                      /* len of new subRemainder string with substituted/new password */
                      len = strlen(domain->currentUDL.subRemainder) - plen + 1;

                      /* now create the new subRemainder string w/ new password */
                      newSubRemainder = (char *)calloc(1,len);
                      if (newSubRemainder == NULL) {
                        partialShutdown(domainId, 0);
                        free(entries);
                        cMsgConnectWriteUnlock(domain);
                        pthread_exit(NULL);
                      }
                      strncat(newSubRemainder, domain->currentUDL.subRemainder, matches[0].rm_so);
                      strcat (newSubRemainder, domain->currentUDL.subRemainder+matches[0].rm_eo);
/*printf("new subRemainder = %s\n", newSubRemainder);*/
                    }

                    /* free up memory */
                    cMsgRegfree(&compiled);
                  }

                  /* else if existing UDL has no password, put one on end if necessary */
                  else if (pUdl->password != NULL && strlen(pUdl->password) > 0) {
/*printf("No cmsgpassword= in udl, CONCAT\n");*/
                    /* len of new subRemainder string added password */
                    len = strlen(domain->currentUDL.subRemainder) + 15 + strlen(pUdl->password);

                    /* now create the new subRemainder string w/ password */
                    newSubRemainder = (char *)calloc(1,len);
                    if (newSubRemainder == NULL) {
                      partialShutdown(domainId, 0);
                      free(entries);
                      cMsgConnectWriteUnlock(domain);
                      pthread_exit(NULL);
                    }

                    if (strstr(domain->currentUDL.subRemainder, "?") != NULL) {
                      sprintf(newSubRemainder, "%s&cmsgpassword=%s",
                              domain->currentUDL.subRemainder, pUdl->password);
                    }
                    else {
                      sprintf(newSubRemainder, "%s?cmsgpassword=%s",
                              domain->currentUDL.subRemainder, pUdl->password);
                    }
/*printf("newSubRemainder = %s\n", newSubRemainder);*/
                  }
                  else {
                      newSubRemainder = strdup(domain->currentUDL.subRemainder);
                      if (newSubRemainder == NULL) {
                          /* printf("Malloc error\n"); */
                          partialShutdown(domainId, 0);
                          free(entries);
                          cMsgConnectWriteUnlock(domain);
                          pthread_exit(NULL);
                      }
/*printf("newSubRemainder = %s\n", newSubRemainder);*/
                  }

                  /* In failing over to a cloud member, only the udl and subRemainder may change -
                   * due to possibly needing a (different) password to connect to the new server. */
                  len = strlen(sName) + strlen(domain->currentUDL.subdomain) +
                        strlen(newSubRemainder) + 10;
                  if (domain->currentUDL.udl != NULL) free(domain->currentUDL.udl);
                  domain->currentUDL.udl = (char *)calloc(1,len);
                  if (domain->currentUDL.udl == NULL) {
                    partialShutdown(domainId, 0);
                    free(entries);
                    cMsgConnectWriteUnlock(domain);
                    pthread_exit(NULL);
                  }
                  sprintf(domain->currentUDL.udl, "cMsg://%s/%s/%s", sName,
                          domain->currentUDL.subdomain, newSubRemainder);
/*printf("KA: Construct new UDL as:\n%s\n", domain->currentUDL.udl);*/

                  if (domain->currentUDL.subRemainder != NULL) free(domain->currentUDL.subRemainder);
                  domain->currentUDL.subRemainder = newSubRemainder;

                  if (domain->currentUDL.password != NULL) free(domain->currentUDL.password);
                  if (pUdl->password != NULL) {
                    domain->currentUDL.password = strdup(pUdl->password);
                  }
                  else {
                    domain->currentUDL.password = NULL;
                  }

                  if (domain->currentUDL.nameServerHost != NULL) free(domain->currentUDL.nameServerHost);
                  if (pUdl->nameServerHost != NULL) {
                    domain->currentUDL.nameServerHost = strdup(pUdl->nameServerHost);
                  }
                  else {
                    domain->currentUDL.nameServerHost = NULL;
                  }

                  domain->currentUDL.nameServerPort = pUdl->nameServerPort;
                  /* never multicast since we get real host & TCP port from cMsg server */
                  domain->currentUDL.mustMulticast  = 0;

                  /* connect with server */
                  if ((err = connectToServer(domainId)) == CMSG_OK) {
                    /* we got ourselves a new server, boys */
                    weGotAConnection = 1;
                    domain->disconnectCalled = 0;
                    /* wait for up to 1.1 sec for waiters to respond */
                    err = cMsgLatchCountDown(&domain->syncLatch, &wait4failover);
                    if (err != 1) {
/* printf("ka: Problems with reporting back to countdowner\n"); */
                    }
                    cMsgLatchReset(&domain->syncLatch, 1, NULL);
                    free(entries);
                    cMsgConnectWriteUnlock(domain);
                    goto top;
                  }
               }
            }
            free(entries);
          } /* if entries != NULL */

          /* Went thru list of cloud servers with nothing to show for it,
           * so try next UDL in list */
          break;

        } /* while no connection */

        /* remember which UDL has just failed */
        failedFailoverIndex = domain->failoverIndex;

        /* Start by trying to connect to the first UDL on the list.
         * If we've just been connected to that UDL, try the next. */
        if (failedFailoverIndex != 0) {
            failoverIndex = 0;
        }
        else {
            failoverIndex = 1;
        }
        domain->resubscribeComplete = 0;

        /* Go through the UDL's until one works */
        while (!weGotAConnection) {

            if (failoverIndex >= domain->failoverSize) {
/* printf("ka: Ran out of UDLs to try so quit\n"); */
                break;
            }

            /* skip over UDL that failed */
            if (failoverIndex == failedFailoverIndex) {
/* printf("ka: skip over UDL that just failed\n"); */
              failoverIndex++;
              continue;
            }

            /* copy next UDL's specifics into main structure */
            cMsgParsedUDLCopy(&domain->currentUDL, &domain->failovers[failoverIndex]);

            /* connect with server */
            if ((err = connectToServer(domainId)) == CMSG_OK) {
              /* we got ourselves a new server, boys */
              weGotAConnection = 1;
              domain->disconnectCalled = 0;
            }
            else {
              failoverIndex++;
/*printf("ka: ERROR reconnecting, continue\n");*/
              continue;
            }

            domain->failoverIndex = failoverIndex;

            /* wait for up to 1.1 sec for waiters to respond */
            err = cMsgLatchCountDown(&domain->syncLatch, &wait4failover);
            if (err != 1) {
/*printf("ka: Problems with reporting back to countdowner\n");*/
            }
            cMsgLatchReset(&domain->syncLatch, 1, NULL);

        } /* while we have no connection */
        cMsgConnectWriteUnlock(domain);
    } /* while we have a connection */

    /* we have no connection so there is no current UDL */
    cMsgParsedUDLFree(&domain->currentUDL);
    cMsgParsedUDLInit(&domain->currentUDL);

    /* Shut things down a bit. */
    cMsgConnectWriteLock(domain);
    partialShutdown(domainId, 1);
    cMsgConnectWriteUnlock(domain);

    /* Wait here until reconnect succeeds, or disconnect tells us to quit. */
    /*printf("ka: wait in sleep loop\n");*/
    while (!domain->gotConnection) {
        nanosleep(&wait4reconnect, NULL);
    }
    weGotAConnection = 1;
    /*printf("ka: done with sleep loop\n");*/

    goto top;

    pthread_exit(NULL);
}


/*-------------------------------------------------------------------*/


/**
 * Routine that periodically sends statistical info to the domain server.
 * The server uses this to gauge the health of this client.
 */
static void *updateServerThread(void *arg) {

    intptr_t index;
    void *domainId = arg;
    cMsgDomainInfo *domain;
    int socket, state, status;
    unsigned int sleepTime = 2; /* 2 secs between monitoring sends */


    index = (intptr_t) domainId;
    if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) {
        if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
            fprintf(stderr, "updateServerThread: bad value for domain, ending thread (1)\n");
        }
        pthread_exit(NULL);
    }

    cMsgMemoryMutexLock();
    domain = connectPointers[index];
    if (domain == NULL || domain->disconnectCalled) {
        cMsgMemoryMutexUnlock();
        if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
            fprintf(stderr, "updateServerThread: bad value for domain, ending thread (2)\n");
        }
        pthread_exit(NULL);
    }
    cMsgMemoryMutexUnlock();

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "updateServerThread: update server thread created, socket = %d\n",
                domain->keepAliveSocket);
    }

    /* periodically send a keep alive (monitoring data) message */
    while (1) {
        /* Disable pthread cancellation until mutexes grabbed and released. */
        status = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &state);
        if (status != 0) {
            cmsg_err_abort(status, "Disabling server update thread cancelability");
        }

        /* This may change if we've failed over to another server */
/*printf("updateServer: set socket = %d\n", domain->keepAliveSocket);*/
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "updateServer: set socket = %d\n", domain->keepAliveSocket);
        }
        socket = domain->keepAliveSocket;

        sendMonitorInfo(domain, socket);
        /* Don't printout any error msg here as it is usually voluminous */

        /* re-enable pthread cancellation at deferred points (sleep) */
        status = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);
        if (status != 0) {
            cmsg_err_abort(status, "Enabling server update thread cancelability");
        }

        /* wait (pthread cancellation point) */
        sleep(sleepTime);
    }

    pthread_exit(NULL);
}


/**
 * This routine gathers and sends monitoring data to the server
 * as a response to the keep alive command.
 */
static int sendMonitorInfo(cMsgDomainInfo *domain, int connfd) {

  char *indent1 = "      ";
  char *indent2 = "        ";
  char buffer[8192], *xml, *pchar;
  int i, size, tblSize, len=0, num=0, err;
  int32_t  outInt[5];
  uint64_t out64[7];
  subInfo *sub;
  subscribeCbInfo *cb;
  monitorData *monData = &domain->monData;
  hashNode *entries = NULL;

  /* zero buffer */
  memset((void *) buffer, 0, 8192);

  /* add items leaving room for stuff at front of buffer */
  xml = buffer + sizeof(outInt) + sizeof(out64);

  /* Don't want subscriptions added or removed while iterating through them. */
  cMsgSubscribeReadLock(domain);

  /* get client subscriptions */
  hashGetAll(&domain->subscribeTable, &entries, &tblSize);

  /* for each client subscription ... */
  if (entries != NULL) {
    /* for each client subscription ... */
    for (i=0; i<tblSize; i++) {
      sub = (subInfo *)entries[i].data;

      /* adding a subscription ... */
      strcat(xml, indent1);
      strcat(xml, "<subscription subject=\"");
      strcat(xml, sub->subject);
      strcat(xml, "\" type=\"");
      strcat(xml, sub->type);
      strcat(xml, "\">\n");

      /* if the subject & type's match, run callbacks */
      cb = sub->callbacks;

      /* for each callback ... */
      while (cb != NULL) {
        strcat(xml, indent2);
        strcat(xml, "<callback id=\"");
        pchar = xml + strlen(xml);
        sprintf(pchar, "%d%s%lu%s%d", num++, "\" received=\"",
                cb->msgCount, "\" cueSize=\"", cb->messages);
        strcat(xml, "\"/>\n");

        /* go to the next callback */
        cb = cb->next;
      } /* next callback */

      strcat(xml, indent1);
      strcat(xml, "</subscription>\n");

    } /* next subscription */
    free(entries);
  } /* if there are subscriptions */

  cMsgSubscribeReadUnlock(domain);

  /*----------------------------------------------*/
  /*  Gather some basic data about this process   */
  /*----------------------------------------------*/
{
    int32_t pid;
    uid_t uid;
    struct passwd *pwd;
    char hostname[30], *login_name;
    int64_t current_time;
    float cpuTime;

    /* get pid */
    pid = getpid();

    strcat(xml, indent1);
    strcat(xml, "<pid>");
    pchar = xml + strlen(xml);
    sprintf(pchar, "%d", pid);
    strcat(xml, "</pid>\n");

    /* get login name*/
    uid = getuid();
    pwd = getpwuid(uid);
    login_name = pwd->pw_name;

    strcat(xml, indent1);
    strcat(xml, "<userName>");
    strcat(xml, login_name);
    strcat(xml, "</userName>\n");

    /* get host name */
    gethostname(hostname, 30);

    strcat(xml, indent1);
    strcat(xml, "<host>");
    strcat(xml, hostname);
    strcat(xml, "</host>\n");

    /* current time */
    current_time = (int64_t)time(NULL);

    strcat(xml, indent1);
    strcat(xml, "<time>");
    pchar = xml + strlen(xml);
    sprintf(pchar, "%d", current_time);
    strcat(xml, "</time>\n");

#ifdef linux
   {
       /* append system + user cputime for process and children */
       long clk_tck;
       struct tms tbuf;

       times(&tbuf);
       clk_tck = sysconf(_SC_CLK_TCK);
       cpuTime = ((float)(tbuf.tms_utime+tbuf.tms_stime+tbuf.tms_cutime+tbuf.tms_cstime)) /
                 (float) clk_tck;
   }
#else
    /* system + user cputime for process and children */
    cpuTime = (float)clock()/(float)CLOCKS_PER_SEC; // in seconds, -1 = error
#endif

    strcat(xml, indent1);
    strcat(xml, "<cpu>");
    pchar = xml + strlen(xml);
    sprintf(pchar, "%.4g", cpuTime);
    strcat(xml, "</cpu>\n");
}


  /*----------------------------------------------*/
  /*  Add any user-generated XML data to packet   */
  /*----------------------------------------------*/
  if (domain->userXML != NULL) {
    strcat(xml, domain->userXML);
  }
  /*----------------------------------------------*/


  /* total number of bytes to send */
  size = (int) (strlen(xml) + sizeof(outInt) - sizeof(int) + sizeof(out64));
/*
printf("sendMonitorInfo: xml len = %d, size of int arry = %d, size of 64 bit int array = %d, total size = %d\n",
        strlen(xml), sizeof(outInt), sizeof(out64), size);
*/
  outInt[0] = htonl((uint32_t)size);
  outInt[1] = htonl((uint32_t)strlen(xml));
  outInt[2] = 0; /* This is a C/C++ client (1 for java) */
  outInt[3] = htonl((uint32_t)monData->subAndGets);  /* pending sub&gets */
  outInt[4] = htonl((uint32_t)monData->sendAndGets); /* pending send&gets */

  out64[0] = hton64(monData->numTcpSends);
  out64[1] = hton64(monData->numUdpSends);
  out64[2] = hton64(monData->numSyncSends);
  out64[3] = hton64(monData->numSendAndGets);
  out64[4] = hton64(monData->numSubAndGets);
  out64[5] = hton64(monData->numSubscribes);
  out64[6] = hton64(monData->numUnsubscribes);

  memcpy(buffer,     (void *) outInt, sizeof(outInt)); /* write ints into buffer */
  len = sizeof(outInt);
  memcpy(buffer+len, (void *) out64,  sizeof(out64));  /* write 64 bit ints into buffer */
  len = size + (int)sizeof(int);
  /* xml already in buffer */

  /* Respond with monitor data. Normally this is a pthread cancellation point;
   * however, cancellation has been blocked at this point. Don't print out any
   * error here as it is voluminous. */
  if ( (err = cMsgNetTcpWrite(connfd, (void *) buffer, len)) != len) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      /*
      if (err < 0) {
        fprintf(stderr, "sendMonitorInfo: write failure, err = %d (%s)\n", err, strerror(errno));
      }
      else {
        fprintf(stderr, "sendMonitorInfo: write failure, partial (%d bytes) monitor data written\n", err);
      }
      */
    }
    return CMSG_NETWORK_ERROR;
  }

  return CMSG_OK;
}




/*-------------------------------------------------------------------*/
/*   miscellaneous local functions                                   */
/*-------------------------------------------------------------------*/

/**
 * This routine parses, using regular expressions, the Universal Domain Locator
 * (UDL) into its various components. The general cMsg domain UDL is of the form:<p>
 *
 *   <b>cMsg:cMsg://&lt;host&gt;:&lt;port&gt;/&lt;subdomainType&gt;/&lt;subdomain remainder&gt;?tag=value&tag2=value2 ...</b><p>
 *
 * <ul>
 * <li>port is not necessary to specify but is the name server's TCP port if connecting directly
 *     or the server's UDP port if multicasting. If not specified, defaults are
 *     {@link CMSG_NAME_SERVER_TCP_PORT} if connecting directly, else
 *     {@link CMSG_NAME_SERVER_MULTICAST_PORT} if multicasting<p>
 * <li>host can be "multicast", "localhost" or may also be in dotted form (129.57.35.21),
 * but may not contain a colon.<p>
 * <li>if domainType is cMsg, subdomainType is automatically set to cMsg if not given.
 *     if subdomainType is not cMsg, it is required<p>
 * <li>the domain name is case insensitive as is the subdomainType<p>
 * <li>remainder is passed on to the subdomain plug-in<p>
 * <li>client's password is in tag=value part of UDL as cmsgpassword=&lt;password&gt;<p>
 * <li>domain server port is in tag=value part of UDL as domainPort=&lt;port&gt;<p>
 * <li>multicast timeout is in tag=value part of UDL as multicastTO=&lt;time out in seconds&gt;<p>
 * <li>subnet is in tag=value part of UDL as subnet=&lt;preferred subnet in dot-decimal&gt;<p>
 * <li>failover is in tag=value part of UDL as failover=&lt;cloud, cloudonly, or any&gt;<p>
 * <li>cloud is in tag=value part of UDL as failover=&lt;local or any&gt;<p>
 * <li>the tag=value part of UDL parsed here as regime=low or regime=high means:<p>
 *   <ul>
 *   <li>low message/data throughput client if regime=low, meaning many clients are serviced
 *       by a single server thread and all msgs retain time order<p>
 *   <li>high message/data throughput client if regime=high, meaning each client is serviced
 *       by multiple threads to maximize throughput. Msgs are NOT guaranteed to be handled in
 *       time order<p>
 *   <li>if regime is not specified (default), it is assumed to be medium, where a single thread is
 *       dedicated to a single client and msgs are guaranteed to be handled in time order<p>
 *   </ul>
 * </ul>
 *
 * The first "cMsg:" is optional. The subdomainType is optional with the default being cMsg.
 * The cMsg subdomain interprets the subdoman remainder as a namespace in which  messages live
 * with no crossing of messages into other namespaces.
 *
 * @param UDL  full udl to be parsed
 * @param pUdl pointer to struct that contains all parsed UDL info
 *             password         name server password
 *             nameServerHost   host of name server
 *             nameServerPort   port of name server
 *             domainServerPort port of domain server
 *             udl              whole UDl
 *             udlRemainder     UDl with cMsg:cMsg:// removed
 *             subdomain        subdomain type
 *             subRemainder     everything after subdomain portion of UDL
 *             multicast        1 if multicast specified, else 0
 *             subnet           ip address of preferred subnet, else NULL
 *             timeout          time in seconds to wait for multicast response
 *             regime           CMSG_REGIME_LOW if regime of client will be low data througput rate
 *                              CMSG_REGIME_HIGH if regime of client will be high data througput rate
 *                              CMSG_REGIME_MEDIUM if regime of client will be medium data througput rate
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_FORMAT if UDL arg is not in the proper format (ie cannot find host,
 *                          2 passwords given)
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 * @returns CMSG_OUT_OF_RANGE if port is an improper value
 */
static int parseUDL(const char *UDL, parsedUDL *pUdl) {

    int        i, err, error, Port, index;
    int        dbg=0, mustMulticast = 0;
    size_t     len, bufLength, subRemainderLen;
    char       *p, *udl, *udlLowerCase, *udlRemainder, *remain;
    char       *buffer;
    const char *pattern = "([^:/]+):?([0-9]+)?/?([a-zA-Z0-9]+)?/?(.*)";
    regmatch_t matches[5]; /* we have 5 potential matches: 1 whole, 4 sub */
    regex_t    compiled;

    if (UDL == NULL || pUdl == NULL) {
      return (CMSG_BAD_ARGUMENT);
    }

    /* make a copy */
    udl = strdup(UDL);

    /* make a copy in all lower case */
    udlLowerCase = strdup(UDL);
    len = strlen(udlLowerCase);
    for (i=0; i<len; i++) {
      udlLowerCase[i] = (char)tolower(udlLowerCase[i]);
    }

    /* strip off the beginning cMsg:cMsg:// */
    p = strstr(udlLowerCase, "cmsg://");
    if (p == NULL) {
      free(udl);
      free(udlLowerCase);
      return(CMSG_BAD_ARGUMENT);
    }
    index = (int) (p - udlLowerCase);
    free(udlLowerCase);

    udlRemainder = udl + index + 7;
if(dbg) printf("parseUDL: udl remainder = %s\n", udlRemainder);

    pUdl->udlRemainder = strdup(udlRemainder);

    /* make a big enough buffer to construct various strings, 256 chars minimum */
    len       = strlen(udlRemainder) + 1;
    bufLength = len < 256 ? 256 : len;
    buffer    = (char *) malloc(bufLength);
    if (buffer == NULL) {
      free(udl);
      return(CMSG_OUT_OF_MEMORY);
    }

    /* compile regular expression */
    err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED);
    /* will never happen */
    if (err != 0) {
        free(udl);
        free(buffer);
        return (CMSG_ERROR);
    }

    /* find matches */
    err = cMsgRegexec(&compiled, udlRemainder, 5, matches, 0);
    if (err != 0) {
        /* no match */
        free(udl);
        free(buffer);
        return (CMSG_BAD_FORMAT);
    }

    /* free up memory */
    cMsgRegfree(&compiled);

    /* find host name */
    if (matches[1].rm_so < 0) {
        /* no match for host */
        free(udl);
        free(buffer);
        return (CMSG_BAD_FORMAT);
    }
    else {
       buffer[0] = 0;
       len = matches[1].rm_eo - matches[1].rm_so;
       strncat(buffer, udlRemainder+matches[1].rm_so, len);
       /* assume connected server is not local, set it properly after connection */
       pUdl->isLocal = 0;

        if (strcasecmp(buffer, "multicast") == 0 ||
            strcmp(buffer, CMSG_MULTICAST_ADDR) == 0) {
            mustMulticast = 1;
if(dbg) printf("set mustMulticast to true (locally in parse method)\n");
        }
        /* if the host is "localhost", find the actual host name */
        else if (strcasecmp(buffer, "localhost") == 0) {
if(dbg) printf("parseUDL: host = localhost\n");
            /* get canonical local host name */
            if (cMsgNetLocalHost(buffer, (int)bufLength) != CMSG_OK) {
                /* error */
                free(udl);
                free(buffer);
                return (CMSG_BAD_FORMAT);
            }
            pUdl->isLocal = 1;
        }
        else {
if(dbg) printf("parseUDL: host = %s, test to see if it is local\n", buffer);
            if (cMsgNetNodeIsLocal(buffer, &pUdl->isLocal) != CMSG_OK) {
                /* Could not find the given host. One possible reason
                 * is that the fully qualified name was used but this host is now
                 * on a different (home?) network. Try using the unqualified name
                 * before declaring failure.
                 */
                char *pend;
                if ( (pend = strchr(buffer, '.')) != NULL) {
                    /* shorten up the string */
                    *pend = '\0';
if(dbg) printf("parseUDL: host = %s, test to see if unqualified host is local\n", buffer);
                    if (cMsgNetNodeIsLocal(buffer, &pUdl->isLocal) != CMSG_OK) {
                        /* error */
                        free(udl);
                        free(buffer);
                        return (CMSG_BAD_FORMAT);
                    }
                }
            }
        }

        pUdl->nameServerHost = strdup(buffer);
        pUdl->mustMulticast = mustMulticast;
    }

if(dbg) printf("parseUDL: host = %s\n", buffer);
if(dbg) printf("parseUDL: mustMulticast = %d\n", mustMulticast);


    /* find port */
    if (matches[2].rm_so < 0) {
        /* no match for port so use default */
        if (mustMulticast == 1) {
            Port = CMSG_NAME_SERVER_MULTICAST_PORT;
        }
        else {
            Port = CMSG_NAME_SERVER_TCP_PORT;
        }
        if (cMsgDebug >= CMSG_DEBUG_WARN) {
            fprintf(stderr, "parseUDL: guessing that the name server port is %d\n",
                   Port);
        }
    }
    else {
        buffer[0] = 0;
        len = matches[2].rm_eo - matches[2].rm_so;
        strncat(buffer, udlRemainder+matches[2].rm_so, len);
        Port = atoi(buffer);
    }

    if (Port < 1024 || Port > 65535) {
      free(udl);
      free(buffer);
      return (CMSG_OUT_OF_RANGE);
    }

    if (mustMulticast == 1) {
      pUdl->nameServerUdpPort = Port;
if(dbg) printf("parseUDL: UDP port = %hu\n", Port );
    }
    else {
      pUdl->nameServerPort = Port;
if(dbg) printf("parseUDL: TCP port = %hu\n", Port );
    }


    /* find subdomain remainder */
    buffer[0] = 0;
    if (matches[4].rm_so < 0) {
        /* no match */
        pUdl->subRemainder = NULL;
        subRemainderLen = 0;
    }
    else {
        len = matches[4].rm_eo - matches[4].rm_so;
        subRemainderLen = len;
        strncat(buffer, udlRemainder+matches[4].rm_so, len);
        pUdl->subRemainder = strdup(buffer);
    }


    /* find subdomain */
    if (matches[3].rm_so < 0) {
        /* no match for subdomain, cMsg is default */
        pUdl->subdomain = strdup("cMsg");
    }
    else {
        /* All recognized subdomains */
        char *allowedSubdomains[] = {"LogFile", "CA", "Database",
                                     "Queue", "FileQueue", "SmartSockets",
                                     "TcpServer", "cMsg"};
        int j, foundSubD = 0;

        buffer[0] = 0;
        len = matches[3].rm_eo - matches[3].rm_so;
        strncat(buffer, udlRemainder+matches[3].rm_so, len);

        /*
         * Make sure the sub domain is recognized.
         * Because the cMsg subdomain is the only one in which a "/" is contained
         * in the remainder, and because the presence of the "cMsg" subdomain identifier
         * is optional, what will happen when it's parsed is that the namespace will be
         * interpreted as the subdomain if "cMsg" domain identifier is not there.
         * Thus we must take care of this case. If we don't recognize the subdomain,
         * assume it's the namespace of the cMsg subdomain.
         */
        for (j=0; j < 8; j++) {
            if (strcasecmp(allowedSubdomains[j], buffer) == 0) {
                foundSubD = 1;
                break;
            }
        }

        if (!foundSubD) {
            /* If here, sudomain is actually namespace and should
             * be part of subRemainder and subdomain is "cMsg" */
            if (pUdl->subRemainder == NULL || strlen(pUdl->subRemainder) < 1) {
                pUdl->subRemainder = strdup(buffer);
                /*if(dbg) printf("parseUDL: remainder null (or len 0) but set to %s\n",  pUdl->subRemainder);*/
            }
            else  {
                char *oldSubRemainder = pUdl->subRemainder;
                char *newRemainder = (char *)calloc(1, (len + subRemainderLen + 2));
                if (newRemainder == NULL) {
                    free(udl);
                    free(buffer);
                    return(CMSG_OUT_OF_MEMORY);
                }
                sprintf(newRemainder, "%s/%s", buffer, oldSubRemainder);
                pUdl->subRemainder = newRemainder;
                /*if(dbg) printf("parseUDL: remainder originally = %s, now = %s\n",oldSubRemainder, newRemainder );*/
                free(oldSubRemainder);
            }

            pUdl->subdomain = strdup("cMsg");
        }
        else {
            pUdl->subdomain = strdup(buffer);
        }
    }

if(dbg) {
    printf("parseUDL: subdomain = %s\n", pUdl->subdomain);
    printf("parseUDL: subdomain remainder = %s\n",pUdl->subRemainder);
}

    /* find optional parameters */
    error = CMSG_OK;
    len = strlen(buffer);
    while (len > 0) {
        /* find cmsgpassword parameter if it exists*/
        /* look for cmsgpassword=<value> */
        pattern = "[&\\?]cmsgpassword=([^&]+)";

        /* compile regular expression */
        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            break;
        }

        /* this is the udl remainder in which we look */
        remain = strdup(buffer);

        /* find matches */
        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match find (first) password */
        if (err == 0 && matches[1].rm_so >= 0) {
          int pos;
          buffer[0] = 0;
          len = matches[1].rm_eo - matches[1].rm_so;
          pos = matches[1].rm_eo;
          strncat(buffer, remain+matches[1].rm_so, len);
          pUdl->password = strdup(buffer);
if(dbg) printf("parseUDL: password 1 = %s\n", buffer);
    
          /* see if there is another password defined (a no-no) */
          err = cMsgRegexec(&compiled, remain+pos, 2, matches, 0);
          if (err == 0 && matches[1].rm_so >= 0) {
if(dbg) printf("Found duplicate password in UDL\n");
            /* there is another password defined, return an error */
            cMsgRegfree(&compiled);
            free(remain);
            error = CMSG_BAD_FORMAT;
            break;
          }
            
        }

        /* free up memory */
        cMsgRegfree(&compiled);
       
        /* find multicast timeout parameter if it exists */
        /* look for multicastTO=<value> */
        pattern = "[&\\?]multicastTO=([^&]+)";

        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            free(remain);
            break;
        }

        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match find timeout */
        if (err == 0 && matches[1].rm_so >= 0) {
          int pos;
          buffer[0] = 0;
          len = matches[1].rm_eo - matches[1].rm_so;
          pos = matches[1].rm_eo;
          strncat(buffer, remain+matches[1].rm_so, len);
          
          /* Since atoi doesn't catch errors, we must check to
           * see if any char is a not a number. */
          for (i=0; i<len; i++) {
            if (!isdigit(buffer[i])) {
if(dbg) printf("Got nondigit in timeout = %c\n",buffer[i]);
              cMsgRegfree(&compiled);
              free(remain);
              error = CMSG_BAD_FORMAT;
              break;
            }
          }
          
          pUdl->timeout = atoi(buffer);
          if (pUdl->timeout < 0) {
              cMsgRegfree(&compiled);
              free(remain);
              error = CMSG_BAD_FORMAT;
              break;
          }
if(dbg) printf("parseUDL: timeout = %d seconds\n", pUdl->timeout);
     
          /* see if there is another timeout defined (a no-no) */
          err = cMsgRegexec(&compiled, remain+pos, 2, matches, 0);
          if (err == 0 && matches[1].rm_so >= 0) {
if(dbg) printf("Found duplicate timeout in UDL\n");
              /* there is another timeout defined, return an error */
              cMsgRegfree(&compiled);
              free(remain);
              error = CMSG_BAD_FORMAT;
              break;
          }
        }
                
        cMsgRegfree(&compiled);
       
        /* find regime parameter if it exists */
        /* look for regime=<value> */
        pattern = "[&\\?]regime=([^&]+)";

        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            free(remain);
            break;
        }

        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match find regime */
        if (err == 0 && matches[1].rm_so >= 0) {
          int pos;
          buffer[0] = 0;
          len = matches[1].rm_eo - matches[1].rm_so;
          pos = matches[1].rm_eo;
          strncat(buffer, remain+matches[1].rm_so, len);
          if (strcasecmp(buffer, "low") == 0) {
            pUdl->regime = CMSG_REGIME_LOW;
if(dbg) printf("parseUDL: regime = low\n");
          }
          else if (strcasecmp(buffer, "high") == 0) {
            pUdl->regime = CMSG_REGIME_HIGH;
if(dbg) printf("parseUDL: regime = high\n");
          }
          else if (strcasecmp(buffer, "medium") == 0) {
            pUdl->regime = CMSG_REGIME_MEDIUM;
if(dbg) printf("parseUDL: regime = medium\n");
          }
          else {
if(dbg) printf("parseUDL: regime = %s, return error\n", buffer);
              cMsgRegfree(&compiled);
              free(remain);
              error = CMSG_BAD_FORMAT;
              break;
          }

          /* see if there is another regime defined (a no-no) */
          err = cMsgRegexec(&compiled, remain+pos, 2, matches, 0);
          if (err == 0 && matches[1].rm_so >= 0) {
if(dbg) printf("Found duplicate regime in UDL\n");
              /* there is another timeout defined, return an error */
              cMsgRegfree(&compiled);
              free(remain);
              error = CMSG_BAD_FORMAT;
              break;
          }

        }


        cMsgRegfree(&compiled);
       
        /* find failover parameter if it exists */
        /* look for failover=<value> */
        pattern = "[&\\?]failover=([^&]+)";

        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            free(remain);
            break;
        }

        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match find failover */
        if (err == 0 && matches[1].rm_so >= 0) {
          int pos;
          buffer[0] = 0;
          len = matches[1].rm_eo - matches[1].rm_so;
          pos = matches[1].rm_eo;
          strncat(buffer, remain+matches[1].rm_so, len);
          if (strcasecmp(buffer, "any") == 0) {
            pUdl->failover = CMSG_FAILOVER_ANY;
if(dbg) printf("parseUDL: failover = any\n");
          }
          else if (strcasecmp(buffer, "cloud") == 0) {
            pUdl->failover = CMSG_FAILOVER_CLOUD;
if(dbg) printf("parseUDL: failover = cloud\n");
          }
          else if (strcasecmp(buffer, "cloudonly") == 0) {
            pUdl->failover = CMSG_FAILOVER_CLOUD_ONLY;
if(dbg) printf("parseUDL: failover = cloud only\n");
          }
          else {
if(dbg) printf("parseUDL: failover = %s, return error\n", buffer);
              cMsgRegfree(&compiled);
              free(remain);
              error = CMSG_BAD_FORMAT;
              break;
          }

          /* see if there is another failover defined (a no-no) */
          err = cMsgRegexec(&compiled, remain+pos, 2, matches, 0);
          if (err == 0 && matches[1].rm_so >= 0) {
if(dbg) printf("Found duplicate failover in UDL\n");
              /* there is another failover defined, return an error */
              cMsgRegfree(&compiled);
              free(remain);
              error = CMSG_BAD_FORMAT;
              break;
          }

        }


        cMsgRegfree(&compiled);
       
        /* find failover parameter if it exists */
        /* look for cloud=<value> */
        pattern = "[&\\?]cloud=([^&]+)";

        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            free(remain);
            break;
        }

        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match find cloud */
        if (err == 0 && matches[1].rm_so >= 0) {
          int pos;
          buffer[0] = 0;
          len = matches[1].rm_eo - matches[1].rm_so;
          pos = matches[1].rm_eo;
          strncat(buffer, remain+matches[1].rm_so, len);
          if (strcasecmp(buffer, "any") == 0) {
            pUdl->cloud = CMSG_CLOUD_ANY;
if(dbg) printf("parseUDL: cloud = any\n");
          }
          else if (strcasecmp(buffer, "local") == 0) {
            pUdl->cloud = CMSG_CLOUD_LOCAL;
if(dbg) printf("parseUDL: cloud = local\n");
          }
          else {
if(dbg) printf("parseUDL: cloud = %s, return error\n", buffer);
              cMsgRegfree(&compiled);
              free(remain);
              error = CMSG_BAD_FORMAT;
              break;
          }

          /* see if there is another failover defined (a no-no) */
          err = cMsgRegexec(&compiled, remain+pos, 2, matches, 0);
          if (err == 0 && matches[1].rm_so >= 0) {
if(dbg) printf("Found duplicate cloud in UDL\n");
              /* there is another cloud defined, return an error */
              cMsgRegfree(&compiled);
              free(remain);
              error = CMSG_BAD_FORMAT;
              break;
          }

        }


        /* free up memory */
        cMsgRegfree(&compiled);
       
        /* find domain server port parameter if it exists */
        /* look for domainPort=<value> */
        pattern = "[&\\?]domainPort=([^&]+)";

        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            free(remain);
            break;
        }

        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match find port */
        if (err == 0 && matches[1].rm_so >= 0) {
            int pos;
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            pos = matches[1].rm_eo;
            strncat(buffer, remain+matches[1].rm_so, len);
          
            /* Since atoi doesn't catch errors, we must check to
             * see if any char is a not a number. */
            for (i=0; i<len; i++) {
                if (!isdigit(buffer[i])) {
if(dbg) printf("Got nondigit in port = %c\n",buffer[i]);
                    cMsgRegfree(&compiled);
                    free(remain);
                    error = CMSG_BAD_FORMAT;
                    break;
                }
            }
          
            pUdl->domainServerPort = atoi(buffer);
            if (pUdl->domainServerPort < 1024 || pUdl->domainServerPort > 65535) {
                cMsgRegfree(&compiled);
                free(remain);
                error = CMSG_BAD_FORMAT;
                break;
            }
if(dbg) printf("parseUDL: domain server port = %d\n", pUdl->domainServerPort);
     
            /* see if there is another domain server port defined (a no-no) */
            err = cMsgRegexec(&compiled, remain+pos, 2, matches, 0);
            if (err == 0 && matches[1].rm_so >= 0) {
if(dbg) printf("Found duplicate domain server port in UDL\n");
                /* there is another domain server port defined, return an error */
                cMsgRegfree(&compiled);
                free(remain);
                error = CMSG_BAD_FORMAT;
                break;
            }
        }
        
        
        /* free up memory */
        cMsgRegfree(&compiled);

        /* find preferred subnet parameter if it exists, */
        /* look for subnet=<value> */
        pattern = "[&\\?]subnet=([^&]+)";

        /* compile regular expression */
        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            free(remain);
            break;
        }

        /* find matches */
        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match find (first) subnet */
        if (err == 0 && matches[1].rm_so >= 0) {
            char *subnet = NULL;
            int pos;
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            pos = matches[1].rm_eo;
            strncat(buffer, remain+matches[1].rm_so, len);

            /* check to make sure it really is a local subnet */
            cMsgNetGetBroadcastAddress(buffer, &subnet);

            /* if it is NOT a local subnet, forget about it */
            if (subnet != NULL) {
                pUdl->subnet = subnet;
                if(dbg) printf("parseUDL: subnet 1 = %s\n", buffer);
            }
        }

        /* free up memory */
        cMsgRegfree(&compiled);
        free(remain);
        break;
    }
    
    /* UDL parsed ok */
if(dbg) printf("DONE PARSING UDL\n");
    free(udl);
    free(buffer);
    return(error);
}

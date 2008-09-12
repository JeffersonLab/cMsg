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
#ifdef VXWORKS
#include <vxWorks.h>
#include <taskLib.h>
#include <hostLib.h>
#include <timers.h>
#include <sysLib.h>
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
#include "regex.h"



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
} thdArg;

/**
 * Structure for arg to be passed to keepAlive thread.
 * Allows data to flow back and forth with these threads.
 */
typedef struct kaThdArg_t {
  void **domainId;
  cMsgDomainInfo *domain;
} kaThdArg;


/* built-in limits */
/** Number of seconds to wait for cMsgClientListeningThread threads to start. */
#define WAIT_FOR_THREADS 10

/** Number of seconds to wait for callback thread to end before cancelling. */
#define WAIT_FOR_CB_THREAD 1

/* local variables */
/** Pthread mutex to protect one-time initialization and the local generation of unique numbers. */
static pthread_mutex_t generalMutex = PTHREAD_MUTEX_INITIALIZER;

/** Id number which uniquely defines a subject/type pair. */
static int subjectTypeId = 1;

/** Size of buffer in bytes for sending messages. */
static int initialMsgBufferSize = 15000;

/** Mutex for waiting for multicast response.*/
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/** Condition variable for waiting for multicast response.*/
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

/* Prototypes of the functions which implement the standard cMsg tasks in the cMsg domain. */
int   cmsg_cmsg_connect           (const char *myUDL, const char *myName, const char *myDescription,
                                   const char *UDLremainder,void **domainId);
int   cmsg_cmsg_send              (void *domainId, void *msg);
int   cmsg_cmsg_syncSend          (void *domainId, void *msg, const struct timespec *timeout, int *response);
int   cmsg_cmsg_flush             (void *domainId, const struct timespec *timeout);
int   cmsg_cmsg_subscribe         (void *domainId, const char *subject, const char *type, cMsgCallbackFunc *callback,
                                   void *userArg, cMsgSubscribeConfig *config, void **handle);
int   cmsg_cmsg_unsubscribe       (void *domainId, void *handle);
int   cmsg_cmsg_subscribeAndGet   (void *domainId, const char *subject, const char *type,
                                   const struct timespec *timeout, void **replyMsg);
int   cmsg_cmsg_sendAndGet        (void *domainId, void *sendMsg, const struct timespec *timeout,
                                   void **replyMsg);
int   cmsg_cmsg_monitor           (void *domainId, const char *command,  void **replyMsg);
int   cmsg_cmsg_start             (void *domainId);
int   cmsg_cmsg_stop              (void *domainId);
int   cmsg_cmsg_disconnect        (void **domainId);
int   cmsg_cmsg_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg);
int   cmsg_cmsg_shutdownClients   (void *domainId, const char *client, int flag);
int   cmsg_cmsg_shutdownServers   (void *domainId, const char *server, int flag);
int   cmsg_cmsg_isConnected       (void *domainId, int *connected);


/** List of the functions which implement the standard cMsg tasks in the cMsg domain. */
static domainFunctions functions = {cmsg_cmsg_connect, cmsg_cmsg_send,
                                    cmsg_cmsg_syncSend, cmsg_cmsg_flush,
                                    cmsg_cmsg_subscribe, cmsg_cmsg_unsubscribe,
                                    cmsg_cmsg_subscribeAndGet, cmsg_cmsg_sendAndGet,
                                    cmsg_cmsg_monitor, cmsg_cmsg_start,
                                    cmsg_cmsg_stop, cmsg_cmsg_disconnect,
                                    cmsg_cmsg_shutdownClients, cmsg_cmsg_shutdownServers,
                                    cmsg_cmsg_setShutdownHandler,
                                    cmsg_cmsg_isConnected};

/* cMsg domain type */
domainTypeInfo cmsgDomainTypeInfo = {
  "cmsg",
  &functions
};



/* local prototypes */

/* mutexes and read/write locks */
static void  staticMutexLock(void);
static void  staticMutexUnlock(void);

/* threads */
static void *keepAliveThread(void *arg);
static void *updateServerThread(void *arg);

/* failovers */
static int restoreSubscriptions(cMsgDomainInfo *domain) ;
static int failoverSuccessful(cMsgDomainInfo *domain, int waitForResubscribes);
static int resubscribe(cMsgDomainInfo *domain, subInfo *sub);

/* misc */
static int  udpSend(cMsgDomainInfo *domain, cMsgMessage_t *msg);
static int  sendMonitorInfo(cMsgDomainInfo *domain, int connfd);
static int  disconnectFromKeepAlive(void **pdomainId);
static int  connectDirect(cMsgDomainInfo *domain, void **domainId, int failoverIndex);
static int  talkToNameServer(cMsgDomainInfo *domain, int serverfd, int failoverIndex);
static int  parseUDL(const char *UDL, char **password,
                           char **host, int *port,
                           char **UDLRemainder,
                           char **subdomainType,
                           char **UDLsubRemainder,
                           int   *multicast,
                           int   *timeout,
                           int   *regime);
static int  unSendAndGet(void *domainId, int id);
static int  unSubscribeAndGet(void *domainId, const char *subject,
                              const char *type, int id);
static void  defaultShutdownHandler(void *userArg);
static void *receiverThd(void *arg);
static void *multicastThd(void *arg);
static int   connectWithMulticast(cMsgDomainInfo *domain, int failoverIndex,
                                  char **host, int *port);


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

/* printf("Restore Subscription to sub = %s, type = %s\n", subject, type); */
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

/* printf("IN failoverSuccessful\n"); */
    /*
     * If only 1 viable UDL is given by client, forget about
     * waiting for failovers to complete before returning an error.
     */
    if (!domain->implementFailovers) return 0;

    /*
     * Wait for 3 seconds for a new connection
     * before giving up and returning an error.
     */
    
    err = cMsgLatchAwait(&domain->syncLatch, &wait);
/* printf("IN failoverSuccessful, cMsgLatchAwait return = %d\n", err); */
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
 * This routine tells whether a client is connected or not.
 *
 * @param domain id of the domain connection
 * @param connected pointer whose value is set to 1 if this client is connected,
 *                  else it is set to 0
 *
 * @returns CMSG_OK
 */
int cmsg_cmsg_isConnected(void *domainId, int *connected) {
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  
  if (domain == NULL) {
    if (connected != NULL) {
      *connected = 0;
    }
    return(CMSG_OK);
  }
  
  /*printf(" cmsg_cmsg_isConnected: will use domain ptr %p\n", domain); */
  cMsgConnectReadLock(domain);
  /*printf(" cmsg_cmsg_isConnected: read locked mutex\n");*/
  
  if (connected != NULL) {
    *connected = domain->gotConnection;
  }
  
  cMsgConnectReadUnlock(domain);
  
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
 *        server to connect to
 * @param myName name of this client
 * @param myDescription description of this client
 * @param UDLremainder partially parsed (initial cMsg:domainType:// stripped off)
 *                     UDL which gets passed down from the API level (cMsgConnect())
 * @param domainId pointer to integer which gets filled with a unique id referring
 *        to this connection.
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_FORMAT if UDLremainder arg is not in the proper format or is NULL
 * @returns CMSG_OUT_OF_MEMORY if the allocating memory failed
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
        
  char *p, *udl;
  int failoverUDLCount = 0, failoverIndex=0, viableUDLs = 0;
  int gotConnection = 0;        
  int i, err=CMSG_OK;
  char temp[CMSG_MAXHOSTNAMELEN];
  cMsgDomainInfo *domain;

  /* allocate struct to hold connection info */
  domain = (cMsgDomainInfo *) calloc(1, sizeof(cMsgDomainInfo));
  if (domain == NULL) {
    return(CMSG_OUT_OF_MEMORY);  
  }
  cMsgDomainInit(domain);  

  /* allocate memory for message-sending buffer */
  domain->msgBuffer     = (char *) malloc(initialMsgBufferSize);
  domain->msgBufferSize = initialMsgBufferSize;
  if (domain->msgBuffer == NULL) {
    cMsgDomainFree(domain);
    free(domain);
    return(CMSG_OUT_OF_MEMORY);
  }

  /* store our host's name */
  gethostname(temp, CMSG_MAXHOSTNAMELEN);
  domain->myHost = (char *) strdup(temp);

  /* store names, can be changed until server connection established */
  domain->name        = (char *) strdup(myName);
  domain->udl         = (char *) strdup(myUDL);
  domain->description = (char *) strdup(myDescription);

  /*
   * The UDL may be a semicolon separated list of UDLs, separate them and
   * store them for future use in failovers.
   */

  /* On first pass, just do a count. */
  udl = (char *)strdup(myUDL);        
  p = strtok(udl, ";");
  while (p != NULL) {
    failoverUDLCount++;
    p = strtok(NULL, ";");
  }
  free(udl);
/*printf("Found %d UDLs\n", failoverUDLCount);*/

  if (failoverUDLCount < 1) {
    cMsgDomainFree(domain);
    free(domain);
    return(CMSG_BAD_ARGUMENT);        
  }

  /* Now that we know how many UDLs there are, allocate array. */
  domain->failoverSize = failoverUDLCount;
  domain->failovers = (parsedUDL *) calloc(failoverUDLCount, sizeof(parsedUDL));
  if (domain->failovers == NULL) {
    cMsgDomainFree(domain);
    free(domain);
    return(CMSG_OUT_OF_MEMORY);
  }

  /* On second pass, stored parsed UDLs. */
  udl = (char *)strdup(myUDL);        
  p   = strtok(udl, ";");
  i   = 0;
  while (p != NULL) {
    /* Parse the UDL (Uniform Domain Locator) */
    if ( (err = parseUDL(p, &domain->failovers[i].password,
                            &domain->failovers[i].nameServerHost,
                            &domain->failovers[i].nameServerPort,
                            &domain->failovers[i].udlRemainder,
                            &domain->failovers[i].subdomain,
                            &domain->failovers[i].subRemainder,
                            &domain->failovers[i].mustMulticast,
                            &domain->failovers[i].timeout,
                            &domain->failovers[i].regime)) != CMSG_OK ) {

      /* There's been a parsing error, mark as invalid UDL */
      domain->failovers[i].valid = 0;
    }
    else {
      domain->failovers[i].valid = 1;
      viableUDLs++;
    }
    domain->failovers[i].udl = strdup(p);
/*printf("Found UDL = %s\n", domain->failovers[i].udl);*/
    p = strtok(NULL, ";");
    i++;
  }
  free(udl);


  /*-------------------------*/
  /* Make a real connection. */
  /*-------------------------*/

  /* If there are no viable UDLs ... */
  if (viableUDLs < 1) {
      cMsgDomainFree(domain);
      free(domain);
      return(CMSG_BAD_FORMAT);            
  }
  /* Else if there's only 1 viable UDL ... */
  else if (viableUDLs < 2) {
/*printf("Only 1 UDL = %s\n", domain->failovers[0].udl);*/

      /* Ain't using failovers */
      domain->implementFailovers = 0;
      
      /* connect using that UDL */
      if (!domain->failovers[0].valid) {
          cMsgDomainFree(domain);
          free(domain);
          return(CMSG_BAD_FORMAT);            
      }
      
      if (domain->failovers[0].mustMulticast == 1) {
        free(domain->failovers[0].nameServerHost);
/*printf("Trying to connect with Multicast 1\n");*/
        err = connectWithMulticast(domain, 0,
                             &domain->failovers[0].nameServerHost,
                             &domain->failovers[0].nameServerPort);
        if (err != CMSG_OK) {
/*printf("Error trying to connect with Multicast, err = %d\n", err);*/
          cMsgDomainFree(domain);
          free(domain);
          /* err = CMSG_SOCKET_ERROR or CMSG_TIMEOUT */
          return(err);
        }
      }
      
      err = connectDirect(domain, domainId, 0);
      domain->failoverIndex = 0;
      if (err != CMSG_OK) {
          cMsgDomainFree(domain);
          free(domain);
          /* err = CMSG_OUT_OF_MEMORY if the allocating memory failed
                   CMSG_SOCKET_ERROR if socket (TCP & UDP) to server could not be created or connected (UDP),
                                     or socket options could not be set
                   CMSG_NETWORK_ERROR if host name could not be resolved or could not connect,
                                      or a communication error with either server occurs (can't read or write).
          */
          return(err);
      }
  }
  else {
    int connectFailures = 0;

    /* We're using failovers */
    domain->implementFailovers = 1;
    
    /* Go through the UDL's until one works */
    failoverIndex = -1;
    do {
      /* check to see if UDL valid for cMsg domain */
      if (!domain->failovers[++failoverIndex].valid) {
        connectFailures++;
        continue;
      }

      /* connect using that UDL info */
/*printf("\nTrying to connect with UDL = %s\n", domain->failovers[failoverIndex].udl);*/
      if (domain->failovers[failoverIndex].mustMulticast == 1) {
        free(domain->failovers[failoverIndex].nameServerHost);
        printf("Trying to connect with Multicast 2\n");
        err = connectWithMulticast(domain, failoverIndex,
                             &domain->failovers[failoverIndex].nameServerHost,
                             &domain->failovers[failoverIndex].nameServerPort);     
        if (err != CMSG_OK) {
/*printf("Error trying to connect with Multicast, err = %d\n", err);*/
          connectFailures++;
          continue;
        }
      }

      err = connectDirect(domain, domainId, failoverIndex);
      if (err == CMSG_OK) {
        domain->failoverIndex = failoverIndex;
        gotConnection = 1;
/*printf("Connected!!\n");*/
        break;
      }

      connectFailures++;

    } while (connectFailures < failoverUDLCount);

    if (!gotConnection) {
      cMsgDomainFree(domain);
      free(domain);
      return(err);                      
    }        
  }

  /* connection is complete */
  *domainId = (void *) domain;

  /* install default shutdown handler (exits program) */
  cmsg_cmsg_setShutdownHandler((void *)domain, defaultShutdownHandler, NULL);
  
  domain->gotConnection = 1;

  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/


/**
 * Method to multicast in order to find the domain server from this client.
 * Once the server is found and returns its host and port, a direct connection
 * can be made.
 *
 */
static int connectWithMulticast(cMsgDomainInfo *domain, int failoverIndex,
                                char **host, int *port) {    
    char   buffer[1024];
    int    err, status, len, passwordLen, sockfd;
    int    outGoing[5], multicastTO=0, gotResponse=0;

    pthread_t rThread, bThread;
    thdArg    rArg,    bArg;
    
    struct timespec wait, time;
    struct sockaddr_in servaddr;
     
    /*------------------------
    * Talk to cMsg server
    *------------------------*/
    
    /* create UDP socket for multicasting */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return(CMSG_SOCKET_ERROR);
    }
       
    memset((void *)&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
/*printf("Multicast thd uses port %hu\n", ((uint16_t)domain->failovers[failoverIndex].nameServerPort));*/
    servaddr.sin_port   = htons((uint16_t) (domain->failovers[failoverIndex].nameServerPort));
    /* send packet to multicast address */
    if ( (err = cMsgStringToNumericIPaddr(CMSG_MULTICAST_ADDR, &servaddr)) != CMSG_OK ) {
      /* an error should never be returned here */
      close(sockfd);
      return(err);
    }
    
    /*
     * We send 2 items explicitly:
     *   1) int describing action to be done
     *   2) password
     * The host we're sending from gets sent for free
     * as does the UDP port we're sending from.
     */
    passwordLen = 0;
    if (domain->failovers[failoverIndex].password != NULL) {
        passwordLen = strlen(domain->failovers[failoverIndex].password);
    }
    /* magic ints */
    outGoing[0] = htonl(CMSG_MAGIC_INT1);
    outGoing[1] = htonl(CMSG_MAGIC_INT2);
    outGoing[2] = htonl(CMSG_MAGIC_INT3);
    /* type of message */
    outGoing[3] = htonl(CMSG_DOMAIN_MULTICAST);
    /* length of "password" string */
    outGoing[4] = htonl(passwordLen);

    /* copy data into a single buffer */
    memcpy(buffer, (void *)outGoing, sizeof(outGoing));
    len = sizeof(outGoing);
    if (passwordLen > 0) {
        memcpy(buffer+len, (const void *)domain->failovers[failoverIndex].password, passwordLen);
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
    multicastTO = domain->failovers[failoverIndex].timeout;
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
        }
        else if (status != 0) {
            cmsg_err_abort(status, "pthread_cond_timedwait");
        }
        else {
            gotResponse = 1;
            if (host != NULL) {
                *host = rArg.buffer;
            }
            if (port != NULL) {
                *port = rArg.port;
            }
/*printf("Response received, host = %s, port = %hu\n", rArg.buffer, rArg.port);*/
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
        
        if (host != NULL) {
            *host = rArg.buffer;
        }
        if (port != NULL) {
            *port = rArg.port;
        }
/*printf("Response received, host = %s, port = %hu\n", rArg.buffer, rArg.port);*/

        status = pthread_mutex_unlock(&mutex);
        if (status != 0) {
            cmsg_err_abort(status, "pthread_mutex_lock");
        }
    }
    
    /* stop multicasting thread */
    pthread_cancel(bThread);
    
/*printf("-udp multi %d\n", sockfd);*/
    close(sockfd);
    
    if (!gotResponse) {
/*printf("Got no response\n");*/
        return(CMSG_TIMEOUT);
    }

    return(CMSG_OK);
}

/*-------------------------------------------------------------------*/

/**
 * This routine starts a thread to receive a return UDP packet from
 * the server due to our initial multicast.
 */
static void *receiverThd(void *arg) {
    int port, nameLen, magicInt[3];
    thdArg *threadArg = (thdArg *) arg;
    char buf[1024], *pchar;
    ssize_t len;
    
    /* release resources when done */
    pthread_detach(pthread_self());
    
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
        /* server is sending 5 ints + string */
        if (len < 5*sizeof(int)) continue;

        /* The server is sending back its 1) port, 2) host name length, 3) host name */
        memcpy(&magicInt[0], pchar, sizeof(int));
        magicInt[0] = ntohl(magicInt[0]);
        pchar += sizeof(int);
        
        memcpy(&magicInt[1], pchar, sizeof(int));
        magicInt[1] = ntohl(magicInt[1]);
        pchar += sizeof(int);
        
        memcpy(&magicInt[2], pchar, sizeof(int));
        magicInt[2] = ntohl(magicInt[2]);
        pchar += sizeof(int);
        
        if ((magicInt[0] != CMSG_MAGIC_INT1) ||
            (magicInt[1] != CMSG_MAGIC_INT2) ||
            (magicInt[2] != CMSG_MAGIC_INT3))  {
/*printf("  Multicast response has wrong magic numbers\n");*/
          continue;
        }
       
        memcpy(&port, pchar, sizeof(int));
        port = ntohl(port);
        pchar += sizeof(int);
        
        memcpy(&nameLen, pchar, sizeof(int));
        nameLen = ntohl(nameLen);
        pchar += sizeof(int);
/*printf("  port = %d, len = %d\n", port, nameLen);*/
        
        if ((port < 1024 || port > 65535) ||
            (nameLen < 0 || nameLen > 1024-20)) {
            /* wrong format so ignore */
/*printf("  Multicast response has wrong format\n");*/
            continue;
        }
        
        if ((len != 5*sizeof(int) + nameLen) || (nameLen != strlen(pchar))) {
            /* wrong format so ignore */
/*printf("  Multicast response has wrong format, bad host name\n");*/
            continue;
        }
        
        /* send info back to calling function */
        threadArg->buffer = strdup(pchar);
        threadArg->port   = port;
/*printf("  Receiver thread: host = %s, port = %d\n", pchar, port);*/
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
 * @param domain pointer to pointer given to cmsg_cmsg_connect.
 * @param failoverIndex index into array of valid URL info
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_OUT_OF_MEMORY if the allocating memory failed
 * @returns CMSG_SOCKET_ERROR if socket (TCP & UDP) to server could not be created or connected (UDP),
 *                            or socket options could not be set
 * @returns CMSG_NETWORK_ERROR if host name could not be resolved or could not connect,
 *                             or a communication error with either server occurs (can't read or write).
 */   
static int connectDirect(cMsgDomainInfo *domain, void **domainId, int failoverIndex) {

  int err, serverfd, sendLen, status, outGoing[3];
  cMsgThreadInfo *threadArg;
  const int size=CMSG_BIGSOCKBUFSIZE; /* bytes */
  struct sockaddr_in  servaddr;
  kaThdArg *kaArg;
  
  /* Block SIGPIPE for this and all spawned threads. */
  cMsgBlockSignals(domain);
  
  /*---------------------------------------------------------------*/
  /* connect & talk to cMsg name server to check if name is unique */
  /*---------------------------------------------------------------*/
    
  /* first connect to server host & port (default send & rcv buf sizes) */
  if ( (err = cMsgTcpConnect(domain->failovers[failoverIndex].nameServerHost,
                             (unsigned short) domain->failovers[failoverIndex].nameServerPort,
                             0, 0, &serverfd)) != CMSG_OK) {
    cMsgRestoreSignals(domain);
    /* err = CMSG_SOCKET_ERROR if socket could not be created or socket options could not be set.
             CMSG_NETWORK_ERROR if host name could not be resolved or could not connect */
    return(err);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "connectDirect: connected to name server\n");
  }
  
  /* get host & port (domain->sendHost,sendPort) to send messages to */
  err = talkToNameServer(domain, serverfd, failoverIndex);
  if (err != CMSG_OK) {
    cMsgRestoreSignals(domain);
    close(serverfd);
    /*returns CMSG_NETWORK_ERROR if error in communicating with the server (can't read or write) */
    return(err);
  }
  
  /* BUGBUG free up memory allocated in parseUDL & no longer needed */

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "connectDirect: got host and port from name server\n");
  }
  
  /* done talking to server */
  close(serverfd);
 
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "connectDirect: closed name server socket\n");
    fprintf(stderr, "connectDirect: sendHost = %s, sendPort = %d\n",
                             domain->sendHost,
                             domain->sendPort);
  }
  
  /* create sending & receiving socket and store (128K rcv buf, 128K send buf) */
  if ( (err = cMsgTcpConnect(domain->sendHost,
                             (unsigned short) domain->sendPort,
                              CMSG_BIGSOCKBUFSIZE, CMSG_BIGSOCKBUFSIZE,
                              &domain->sendSocket)) != CMSG_OK) {
    cMsgRestoreSignals(domain);
    return(err);
  }  

  /* first send magic #s to server which identifies us as real cMsg client */
  outGoing[0] = htonl(CMSG_MAGIC_INT1);
  outGoing[1] = htonl(CMSG_MAGIC_INT2);
  outGoing[2] = htonl(CMSG_MAGIC_INT3);
  /* send data over TCP socket */
  sendLen = cMsgTcpWrite(domain->sendSocket, (void *) outGoing, sizeof(outGoing));
  if (sendLen != sizeof(outGoing)) {
    cMsgRestoreSignals(domain);
    close(domain->sendSocket);
    return(CMSG_NETWORK_ERROR);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "connectDirect: created sending/receiving socket fd = %d\n", domain->sendSocket);
  }  

  /* launch pend thread and start listening on send socket */
  threadArg = (cMsgThreadInfo *) malloc(sizeof(cMsgThreadInfo));
  if (threadArg == NULL) {
      return(CMSG_OUT_OF_MEMORY);  
  }
  threadArg->isRunning   = 0;
  threadArg->thdstarted  = 0;
  threadArg->listenFd    = domain->sendSocket;
  threadArg->blocking    = CMSG_NONBLOCKING;
  threadArg->domain      = domain;

  status = pthread_create(&domain->pendThread, NULL,
                          cMsgClientListeningThread, (void *) threadArg);
  if (status != 0) {
    cmsg_err_abort(status, "Creating message listening thread");
  }
  
  /*
   * This thread DOES NOT NEED to be running before we talk to
   * the name server. threadArg is used in cMsgClientListeningThread
   * and its cleanup handler, so do NOT free its memory!
   */  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "connectDirect: created listening thread\n");
  }

  /* create keep alive socket and store (default send & rcv buf sizes) */
  if ( (err = cMsgTcpConnect(domain->sendHost,
                             (unsigned short) domain->sendPort,
                              0, 0, &domain->keepAliveSocket)) != CMSG_OK) {
    cMsgRestoreSignals(domain);
    close(domain->sendSocket);
    pthread_cancel(domain->pendThread);
    /* Wait until the threads are really dead, cause on return from this function,
     * domain gets freed immediately and will cause seg fault if cleanup still
     * going on. */
    pthread_join(domain->pendThread, NULL);
    return(err);
  }
  
  /* send magic #s over TCP socket */
  sendLen = cMsgTcpWrite(domain->keepAliveSocket, (void *) outGoing, sizeof(outGoing));
  if (sendLen != sizeof(outGoing)) {
    cMsgRestoreSignals(domain);
    close(domain->sendSocket);
    close(domain->keepAliveSocket);
    pthread_cancel(domain->pendThread);
    pthread_join(domain->pendThread, NULL);
    return(CMSG_NETWORK_ERROR);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "connectDirect: created keepalive socket fd = %d\n",domain->keepAliveSocket );
  }
  
  /* create thread to read periodic keep alives (monitoring data) and handle dead server */
  kaArg = (kaThdArg *) malloc(sizeof(kaThdArg));
  if (kaArg == NULL) {
    cMsgRestoreSignals(domain);
    close(domain->sendSocket);
    pthread_cancel(domain->pendThread);
    pthread_join(domain->pendThread, NULL);
    return(CMSG_OUT_OF_MEMORY);
  }
  kaArg->domain   = domain;
  kaArg->domainId = domainId;
  
  status = pthread_create(&domain->keepAliveThread, NULL,
                          keepAliveThread, (void *) kaArg);
  if (status != 0) {
    cmsg_err_abort(status, "Creating keep alive thread");
  }
     
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "connectDirect: created keep alive thread\n");
  }


  /* create thread to send periodic keep alives (monitor data) to server */
  status = pthread_create(&domain->updateServerThread, NULL,
                          updateServerThread, (void *)domain);
  if (status != 0) {
    cmsg_err_abort(status, "Creating update server thread");
  }
     
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "connectDirect: created update server thread\n");
  }


  /* create sending UDP socket */
  if ((domain->sendUdpSocket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    cMsgRestoreSignals(domain);
    close(domain->keepAliveSocket);
    close(domain->sendSocket);
    /* get rid of threads we've started up */
    pthread_cancel(domain->keepAliveThread);
    pthread_join(domain->keepAliveThread, NULL);
    pthread_cancel(domain->updateServerThread);
    pthread_join(domain->updateServerThread, NULL);
    pthread_cancel(domain->pendThread);
    pthread_join(domain->pendThread, NULL);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "connectDirect: socket error, %s\n", strerror(errno));
    return(CMSG_SOCKET_ERROR);
  }

  /* set send buffer size */
  err = setsockopt(domain->sendUdpSocket, SOL_SOCKET, SO_SNDBUF, (char*) &size, sizeof(size));
  if (err < 0) {
    cMsgRestoreSignals(domain);
    close(domain->keepAliveSocket);
    close(domain->sendSocket);
    close(domain->sendUdpSocket);
    pthread_cancel(domain->keepAliveThread);
    pthread_join(domain->keepAliveThread, NULL);
    pthread_cancel(domain->updateServerThread);
    pthread_join(domain->updateServerThread, NULL);
    pthread_cancel(domain->pendThread);
    pthread_join(domain->pendThread, NULL);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "connectDirect: setsockopt error\n");
    return(CMSG_SOCKET_ERROR);
  }

  memset((void *)&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port   = htons(domain->sendUdpPort);

  if ( (err = cMsgStringToNumericIPaddr(domain->sendHost, &servaddr)) != CMSG_OK ) {
    cMsgRestoreSignals(domain);
    close(domain->keepAliveSocket);
    close(domain->sendSocket);
    close(domain->sendUdpSocket);
    pthread_cancel(domain->keepAliveThread);
    pthread_join(domain->keepAliveThread, NULL);
    pthread_cancel(domain->updateServerThread);
    pthread_join(domain->updateServerThread, NULL);
    pthread_cancel(domain->pendThread);
    pthread_join(domain->pendThread, NULL);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "connectDirect: host name error\n");
    /* err =  CMSG_BAD_ARGUMENT if domain->sendHost is null
              CMSG_OUT_OF_MEMORY if out of memory
              CMSG_NETWORK_ERROR if the numeric address could not be obtained/resolved
    */
    return(err);
  }

  err = connect(domain->sendUdpSocket, (SA *) &servaddr, (socklen_t) sizeof(servaddr));
  if (err < 0) {
    cMsgRestoreSignals(domain);
    close(domain->keepAliveSocket);
    close(domain->sendSocket);
    close(domain->sendUdpSocket);
    pthread_cancel(domain->keepAliveThread);
    pthread_join(domain->keepAliveThread, NULL);
    pthread_cancel(domain->updateServerThread);
    pthread_join(domain->updateServerThread, NULL);
    pthread_cancel(domain->pendThread);
    pthread_join(domain->pendThread, NULL);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "connectDirect: UDP connect error\n");
    return(CMSG_SOCKET_ERROR);
  }

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine is called by the keepAlive thread upon the death of the
 * cMsg server in an attempt to failover to another server whose UDL was
 * given in the original call to connect(). It is already protected
 * by cMsgConnectWriteLock() when called in keepAlive thread.
 * 
 * @param domain pointer to connection info structure
 * @param failoverIndex index into the array of parsed UDLs to which
 *                      a connection is to be attempted with this function
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_OUT_OF_MEMOERY if the allocating memory failed
 * @returns CMSG_SOCKET_ERROR if socket options could not be set
 * @returns CMSG_NETWORK_ERROR if no connection to the name or domain servers can be made,
 *                             or a communication error with either server occurs.
 */   
static int reconnect(void **domainId, int failoverIndex) {

  int i, err, serverfd, status, tblSize;
  getInfo *info;
  const int size=CMSG_BIGSOCKBUFSIZE; /* bytes */
  struct sockaddr_in  servaddr;
  cMsgThreadInfo *threadArg;
  hashNode *entries = NULL;
  cMsgDomainInfo *domain = (cMsgDomainInfo *) (*domainId);
   
  /*--------------------------------------------------------------------*/
  /* Connect to cMsg name server to check if server can be connected to.
   * If not, don't waste any more time and try the next one.            */
  /*--------------------------------------------------------------------*/
  /* connect to server host & port */
  if ( (err = cMsgTcpConnect(domain->failovers[failoverIndex].nameServerHost,
                             (unsigned short) domain->failovers[failoverIndex].nameServerPort,
                             0, 0, &serverfd)) != CMSG_OK) {
    return(err);
  }  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "reconnect: connected to name server\n");
  }

  /* The thread listening for TCP connections needs to keep running.
   * Keep all existing callback threads for the subscribes. */

  /* wakeup all sendAndGets - they can't be saved */
  hashClear(&domain->sendAndGetTable, &entries, &tblSize);
  if (entries != NULL) {
    for (i=0; i<tblSize; i++) {
      info = (getInfo *)entries[i].data;    
      info->msg = NULL;
      info->msgIn = 1;
      info->quit  = 1;
      info->error = CMSG_SERVER_DIED;

      /* wakeup the sendAndGet */
      status = pthread_cond_signal(&info->cond);
      if (status != 0) {
        cmsg_err_abort(status, "Failed get condition signal");
      }

      free(entries[i].key);
    }
    free(entries);
  }


  /* wakeup all syncSends - they can't be saved */
  hashClear(&domain->syncSendTable, &entries, &tblSize);
  if (entries != NULL) {
    for (i=0; i<tblSize; i++) {
      info = (getInfo *)entries[i].data;
      info->msg = NULL;
      info->msgIn = 1;
      info->quit  = 1;
      info->error = CMSG_SERVER_DIED;

      /* wakeup the sendAndGet */
      status = pthread_cond_signal(&info->cond);
      if (status != 0) {
        cmsg_err_abort(status, "Failed get condition signal");
      }

      free(entries[i].key);
    }
    free(entries);
  }


  /* wakeup all existing subscribeAndGets - they can't be saved */
  hashClear(&domain->subAndGetTable, &entries, &tblSize);
  if (entries != NULL) {
    for (i=0; i<tblSize; i++) {
      info = (getInfo *)entries[i].data;
      info->msg = NULL;
      info->msgIn = 1;
      info->quit  = 1;
      info->error = CMSG_SERVER_DIED;

      /* wakeup the subAndGet */
      status = pthread_cond_signal(&info->cond);
      if (status != 0) {
        cmsg_err_abort(status, "Failed get condition signal");
      }

      free(entries[i].key);
    }
    free(entries);
  }
      
  
  /*-----------------------------------------------------*/
  /* talk to cMsg name server to check if name is unique */
  /*-----------------------------------------------------*/
  /* get host & port (domain->sendHost,sendPort) to send messages to */
  err = talkToNameServer(domain, serverfd, failoverIndex);
  if (err != CMSG_OK) {
    close(serverfd);
    return(err);
  }
  
/* BUGBUG free up memory allocated in parseUDL & no longer needed */

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "reconnect: got host and port from name server\n");
  }
  
  /* done talking to server */
  close(serverfd);
   
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "reconnect: closed name server socket\n");
    fprintf(stderr, "reconnect: sendHost = %s, sendPort = %d\n",
                            domain->sendHost,
                            domain->sendPort);
  }
    
  /* create sending & receiving socket and store (128K rcv buf, 128K send buf) */
  if ( (err = cMsgTcpConnect(domain->sendHost,
                             (unsigned short) domain->sendPort,
                              CMSG_BIGSOCKBUFSIZE, CMSG_BIGSOCKBUFSIZE,
                              &domain->sendSocket)) != CMSG_OK) {
    return(err);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "reconnect: created sending/receiving socket fd = %d\n", domain->sendSocket);
  }  

  /* launch pend thread and start listening on send socket */
  threadArg = (cMsgThreadInfo *) malloc(sizeof(cMsgThreadInfo));
  if (threadArg == NULL) {
      return(CMSG_OUT_OF_MEMORY);
  }
  threadArg->isRunning   = 0;
  threadArg->thdstarted  = 0;
  threadArg->listenFd    = domain->sendSocket;
  threadArg->blocking    = CMSG_NONBLOCKING;
  threadArg->domain      = domain;

  status = pthread_create(&domain->pendThread, NULL,
                          cMsgClientListeningThread, (void *) threadArg);
  if (status != 0) {
    cmsg_err_abort(status, "Creating message listening thread");
  }
  
  /*
   * This thread DOES NOT NEED to be running before we talk to
   * the name server. threadArg is used in cMsgClientListeningThread
   * and its cleanup handler, so do NOT free its memory!
   */  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "reconnect: created listening thread\n");
  }

  /* create keep alive socket and store (default send & rcv buf sizes) */
  if ( (err = cMsgTcpConnect(domain->sendHost,
                             (unsigned short) domain->sendPort,
                              0, 0, &domain->keepAliveSocket)) != CMSG_OK) {
    close(domain->sendSocket);
    return(err);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "reconnect: created keepalive socket fd = %d\n",domain->keepAliveSocket );
  }
  
  /* create thread to read periodic keep alives (monitoring data) and handle dead server */
  status = pthread_create(&domain->keepAliveThread, NULL,
                          keepAliveThread, (void *) domainId);
  if (status != 0) {
    cmsg_err_abort(status, "Creating keep alive thread");
  }
     
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "reconnect: created keep alive thread\n");
  }


  /* create thread to send periodic keep alives (monitor data) to server */
  status = pthread_create(&domain->updateServerThread, NULL,
                          updateServerThread, (void *)domain);
  if (status != 0) {
    cmsg_err_abort(status, "Creating update server thread");
  }
     
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "reconnect: created update server thread\n");
  }


  /* create sending UDP socket */
  if ((domain->sendUdpSocket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    close(domain->keepAliveSocket);
    close(domain->sendSocket);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "reconnect: socket error, %s\n", strerror(errno));
    return(CMSG_SOCKET_ERROR);
  }

  /* set send buffer size */
  err = setsockopt(domain->sendUdpSocket, SOL_SOCKET, SO_SNDBUF, (char*) &size, sizeof(size));
  if (err < 0) {
    close(domain->keepAliveSocket);
    close(domain->sendSocket);
    close(domain->sendUdpSocket);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "reconnect: setsockopt error\n");
    return(CMSG_SOCKET_ERROR);
  }

  memset((void *)&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port   = htons(domain->sendUdpPort);

  if ( (err = cMsgStringToNumericIPaddr(domain->sendHost, &servaddr)) != CMSG_OK ) {
    close(domain->keepAliveSocket);
    close(domain->sendSocket);
    close(domain->sendUdpSocket);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "reconnect: host name error\n");
    return(CMSG_SOCKET_ERROR);
  }

  err = connect(domain->sendUdpSocket, (SA *) &servaddr, (socklen_t) sizeof(servaddr));
  if (err < 0) {
    close(domain->keepAliveSocket);
    close(domain->sendSocket);
    close(domain->sendUdpSocket);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "reconnect: UDP connect error\n");
    return(CMSG_SOCKET_ERROR);
  }
   
/* printf("reconnect END\n"); */
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
 * @returns CMSG_BAD_ARGUMENT if the id or message argument is null or has null subject or type
 * @returns CMSG_OUT_OF_RANGE  if the message is too large to be sent by UDP
 * @returns CMSG_OUT_OF_MEMORY if the allocating memory for message buffer failed
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement sending
 *                               messages
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */
int cmsg_cmsg_send(void *domainId, void *vmsg) {

  int err, len, lenSubject, lenType, lenPayloadText, lenText, lenByteArray;
  int fd, highInt, lowInt, outGoing[16];
  ssize_t sendLen;
  cMsgMessage_t *msg = (cMsgMessage_t *) vmsg;
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  uint64_t llTime;
  struct timespec now;


  if (domain == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }

  if (!domain->hasSend) {
    return(CMSG_NOT_IMPLEMENTED);
  }

  /* check args */
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);

  if ( (cMsgCheckString(msg->subject) != CMSG_OK ) ||
       (cMsgCheckString(msg->type)    != CMSG_OK )    ) {
    return(CMSG_BAD_ARGUMENT);
  }

  /* send using udp socket */
  if (msg->context.udpSend) {
    return udpSend(domain, msg);
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
      lenText = strlen(msg->text);
    }

    /* update history here */
    cMsgAddSenderToHistory(vmsg, domain->name);
    
    /* length of "payloadText" string */
    if (msg->payloadText == NULL) {
      lenPayloadText = 0;
    }
    else {
      lenPayloadText = strlen(msg->payloadText);
    }

    /* message id (in network byte order) to domain server */
    outGoing[1] = htonl(CMSG_SEND_REQUEST);
    /* reserved for future use */
    outGoing[2] = 0;
    /* user int */
    outGoing[3] = htonl(msg->userInt);
    /* system msg id */
    outGoing[4] = htonl(msg->sysMsgId);
    /* sender token */
    outGoing[5] = htonl(msg->senderToken);
    /* bit info */
    outGoing[6] = htonl(msg->info);

    /* time message sent (right now) */
    clock_gettime(CLOCK_REALTIME, &now);
    /* convert to milliseconds */
    llTime  = ((uint64_t)now.tv_sec * 1000) +
              ((uint64_t)now.tv_nsec/1000000);
    highInt = (int) ((llTime >> 32) & 0x00000000FFFFFFFF);
    lowInt  = (int) (llTime & 0x00000000FFFFFFFF);
    outGoing[7] = htonl(highInt);
    outGoing[8] = htonl(lowInt);

    /* user time */
    llTime  = ((uint64_t)msg->userTime.tv_sec * 1000) +
              ((uint64_t)msg->userTime.tv_nsec/1000000);
    highInt = (int) ((llTime >> 32) & 0x00000000FFFFFFFF);
    lowInt  = (int) (llTime & 0x00000000FFFFFFFF);
    outGoing[9]  = htonl(highInt);
    outGoing[10] = htonl(lowInt);

    /* length of "subject" string */
    lenSubject   = strlen(msg->subject);
    outGoing[11] = htonl(lenSubject);
    
    /* length of "type" string */
    lenType      = strlen(msg->type);
    outGoing[12] = htonl(lenType);

    /* length of "payloadText" string */
    outGoing[13] = htonl(lenPayloadText);

    /* length of "text" string, include payload (if any) as text here */
    outGoing[14] = htonl(lenText);

    /* length of byte array */
    lenByteArray = msg->byteArrayLength;
    outGoing[15] = htonl(lenByteArray);

    /* total length of message (minus first int) is first item sent */
    len = sizeof(outGoing) - sizeof(int) + lenSubject + lenType +
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
    
    /* send data over TCP socket */
    sendLen = cMsgTcpWrite(fd, (void *) domain->msgBuffer, len);
    if (sendLen == len) {
      domain->monData.numTcpSends++;
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
       fd = domain->sendSocket;
       if (cMsgDebug >= CMSG_DEBUG_INFO) {
           printf("cmsg_cmsg_send: FAILOVER SUCCESSFUL, try send again\n");
       }
       goto tryagain;
    }  
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      printf("cmsg_cmsg_send: FAILOVER NOT successful, quitting, err = %d\n", err);
    }
  }
  
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
static int udpSend(cMsgDomainInfo *domain, cMsgMessage_t *msg) {
  
  int err, len, lenSubject, lenType, lenPayloadText, lenText, lenByteArray;
  int fd, highInt, lowInt, outGoing[19];
  ssize_t sendLen;
  uint64_t llTime;
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
      lenText = strlen(msg->text);
    }

    /* update history here */
    cMsgAddSenderToHistory(msg, domain->name);
    
    /* length of "payloadText" string */
    if (msg->payloadText == NULL) {
      lenPayloadText = 0;
    }
    else {
      lenPayloadText = strlen(msg->payloadText);
    }

    /* send magic integers first to protect against port scanning */
    outGoing[0] = htonl(CMSG_MAGIC_INT1); /* cMsg */
    outGoing[1] = htonl(CMSG_MAGIC_INT2); /*  is  */
    outGoing[2] = htonl(CMSG_MAGIC_INT3); /* cool */
    
    /* message id (in network byte order) to domain server */
    outGoing[4] = htonl(CMSG_SEND_REQUEST);
    /* reserved for future use */
    outGoing[5] = 0;
    /* user int */
    outGoing[6] = htonl(msg->userInt);
    /* system msg id */
    outGoing[7] = htonl(msg->sysMsgId);
    /* sender token */
    outGoing[8] = htonl(msg->senderToken);
    /* bit info */
    outGoing[9] = htonl(msg->info);

    /* time message sent (right now) */
    clock_gettime(CLOCK_REALTIME, &now);
    /* convert to milliseconds */
    llTime  = ((uint64_t)now.tv_sec * 1000) +
              ((uint64_t)now.tv_nsec/1000000);
    highInt = (int) ((llTime >> 32) & 0x00000000FFFFFFFF);
    lowInt  = (int) (llTime & 0x00000000FFFFFFFF);
    outGoing[10] = htonl(highInt);
    outGoing[11] = htonl(lowInt);

    /* user time */
    llTime  = ((uint64_t)msg->userTime.tv_sec * 1000) +
              ((uint64_t)msg->userTime.tv_nsec/1000000);
    highInt = (int) ((llTime >> 32) & 0x00000000FFFFFFFF);
    lowInt  = (int) (llTime & 0x00000000FFFFFFFF);
    outGoing[12]  = htonl(highInt);
    outGoing[13] = htonl(lowInt);

    /* length of "subject" string */
    lenSubject   = strlen(msg->subject);
    outGoing[14] = htonl(lenSubject);
    
    /* length of "type" string */
    lenType      = strlen(msg->type);
    outGoing[15] = htonl(lenType);

    /* length of "payloadText" string */
    outGoing[16] = htonl(lenPayloadText);

    /* length of "text" string, include payload (if any) as text here */
    outGoing[17] = htonl(lenText);

    /* length of byte array */
    lenByteArray = msg->byteArrayLength;
    outGoing[18] = htonl(lenByteArray);

    /* total length of message (minus first int) is first item sent */
    len = sizeof(outGoing) - sizeof(int) + lenSubject + lenType +
          lenPayloadText + lenText + lenByteArray;
    outGoing[3] = htonl(len);

    if (msg->context.udpSend && len > CMSG_BIGGEST_UDP_BUFFER_SIZE) {
      cMsgConnectReadUnlock(domain);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        printf("cmsg_cmsg_send: message is too big for UDP packet\n");
      }
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
           printf("cmsg_cmsg_send: FAILOVER SUCCESSFUL, try send again\n");
       }
       goto tryagain;
    }  
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      printf("cmsg_cmsg_send: FAILOVER NOT successful, quitting, err = %d\n", err);
    }
  }
  
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
int cmsg_cmsg_syncSend(void *domainId, void *vmsg, const struct timespec *timeout, int *response) {
  
  int err, len, lenSubject, lenType, lenPayloadText, lenText, lenByteArray;
  int uniqueId, status, fd, highInt, lowInt, outGoing[16];
  cMsgMessage_t *msg = (cMsgMessage_t *) vmsg;
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  uint64_t llTime;
  struct timespec wait, now;
  char *idString=NULL;
  getInfo *info=NULL;
  /*static int count=0;*/

    
  if (domain == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  fd = domain->sendSocket;
  
  if (!domain->hasSyncSend) {
    return(CMSG_NOT_IMPLEMENTED);
  }
 
  /* check args */
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  
  if ( (cMsgCheckString(msg->subject) != CMSG_OK ) ||
       (cMsgCheckString(msg->type)    != CMSG_OK )    ) {
    return(CMSG_BAD_ARGUMENT);
  }
  
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
      lenText = strlen(msg->text);
    }

    /* update history here */
    cMsgAddSenderToHistory(vmsg, domain->name);
    
    /* length of "payloadText" string */
    if (msg->payloadText == NULL) {
      lenPayloadText = 0;
    }
    else {
      lenPayloadText = strlen(msg->payloadText);
    }
    
    /*
     * Pick a unique identifier, send it to the domain server,
     * and remember it for future use.
     * Mutex protect this operation as many calls may
     * operate in parallel on this static variable.
     */
    staticMutexLock();
    uniqueId = subjectTypeId++;
    staticMutexUnlock();
    
    /* use struct as value in hashTable */
    info = (getInfo *)malloc(sizeof(getInfo));
    if (info == NULL) {
      cMsgConnectReadUnlock(domain);
      return(CMSG_OUT_OF_MEMORY);      
    }
    cMsgGetInfoInit(info);
    info->id = uniqueId;
    
    /* use string generated from id as key (must be string) in hashTable */
    idString = cMsgIntChars(uniqueId);
    if (idString == NULL) {
      cMsgConnectReadUnlock(domain);
      cMsgGetInfoFree(info); 
      free(info);
      return(CMSG_OUT_OF_MEMORY);      
    }
    
    /* Try inserting item into hashtable, if entry exists, pick another key */
    cMsgSyncSendMutexLock(domain); /* serialize access to syncSend hash table */
    while ( !hashInsertTry(&domain->syncSendTable, idString, (void *)info) ) {
      free(idString);
      staticMutexLock();
      uniqueId = subjectTypeId++;
      staticMutexUnlock();
      idString = cMsgIntChars(uniqueId);
      if (idString == NULL) {
        cMsgSyncSendMutexUnlock(domain);
        cMsgConnectReadUnlock(domain);
        cMsgGetInfoFree(info);
        free(info);
        return(CMSG_OUT_OF_MEMORY);
      }      
    }
    cMsgSyncSendMutexUnlock(domain);
   
    outGoing[1] = htonl(CMSG_SYNC_SEND_REQUEST);
    /* unique syncSend id */
    outGoing[2] = htonl(uniqueId);
    /* user int */
    outGoing[3] = htonl(msg->userInt);
    /* system msg id */
    outGoing[4] = htonl(msg->sysMsgId);
    /* sender token */
    outGoing[5] = htonl(msg->senderToken);
    /* bit info */
    outGoing[6] = htonl(msg->info);

    /* time message sent (right now) */
    clock_gettime(CLOCK_REALTIME, &now);
    /* convert to milliseconds */
    llTime  = ((uint64_t)now.tv_sec * 1000) + ((uint64_t)now.tv_nsec/1000000);
    highInt = (int) ((llTime >> 32) & 0x00000000FFFFFFFF);
    lowInt  = (int) (llTime & 0x00000000FFFFFFFF);
    outGoing[7] = htonl(highInt);
    outGoing[8] = htonl(lowInt);

    /* user time */
    llTime  = ((uint64_t)msg->userTime.tv_sec * 1000) +
              ((uint64_t)msg->userTime.tv_nsec/1000000);
    highInt = (int) ((llTime >> 32) & 0x00000000FFFFFFFF);
    lowInt  = (int) (llTime & 0x00000000FFFFFFFF);
    outGoing[9]  = htonl(highInt);
    outGoing[10] = htonl(lowInt);

    /* length of "subject" string */
    lenSubject   = strlen(msg->subject);
    outGoing[11]  = htonl(lenSubject);
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
    len = sizeof(outGoing) - sizeof(int) + lenSubject + lenType +
          lenPayloadText + lenText + lenByteArray;
    outGoing[0] = htonl(len);

    /* make send socket communications thread-safe */
    cMsgSocketMutexLock(domain);

    /* allocate more memory for message-sending buffer if necessary */
    if (domain->msgBufferSize < (int)(len+sizeof(int))) {
      free(domain->msgBuffer);
      domain->msgBufferSize = len + 1004; /* give us 1kB extra */
      domain->msgBuffer = (char *) malloc(domain->msgBufferSize);
      if (domain->msgBuffer == NULL) {
        cMsgSocketMutexUnlock(domain);
        cMsgConnectReadUnlock(domain);
        cMsgSyncSendMutexLock(domain);
        hashRemove(&domain->syncSendTable, idString, NULL);
        cMsgSyncSendMutexUnlock(domain);
        cMsgGetInfoFree(info);
        free(info);
        free(idString);
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
    memcpy(domain->msgBuffer+len, (void *)msg->payloadText, lenPayloadText);
    len += lenPayloadText;
    memcpy(domain->msgBuffer+len, (void *)msg->text, lenText);
    len += lenText;
    memcpy(domain->msgBuffer+len, (void *)&((msg->byteArray)[msg->byteArrayOffset]), lenByteArray);
    len += lenByteArray;   

    /* send data over socket */
    if (cMsgTcpWrite(fd, (void *) domain->msgBuffer, len) != len) {
      cMsgSocketMutexUnlock(domain);
      cMsgConnectReadUnlock(domain);
      cMsgSyncSendMutexLock(domain);
      hashRemove(&domain->syncSendTable, idString, NULL);
      cMsgSyncSendMutexUnlock(domain);
      cMsgGetInfoFree(info);
      free(info);
      free(idString);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cmsg_cmsg_syncSend: write failure\n");
      }
      err = CMSG_NETWORK_ERROR;
      break;
    }

    /* done protecting outgoing communications */
    cMsgSocketMutexUnlock(domain);    
    cMsgConnectReadUnlock(domain);
    break;
  }
   
  /* Now ..., wait for asynchronous response */
  /*
  if (count++%10000 == 0) {
    printf("hashSize %d, count %d\n", hashSize(&domain->syncSendTable), count);
  }
  */
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
 * @returns CMSG_BAD_ARGUMENT if the domainId, subject, type, or replyMsg arguments are null
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
                             
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  int err, fd, uniqueId, status, len, lenSubject, lenType, outGoing[6];
  char *idString;
  getInfo *info = NULL;
  struct timespec wait;
  struct iovec iov[3];
  

  if (domain == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  fd = domain->sendSocket;
  
  if (!domain->hasSubscribeAndGet) {
    return(CMSG_NOT_IMPLEMENTED);
  }   
           
  /* check args */
  if (replyMsg == NULL) return(CMSG_BAD_ARGUMENT);
  
  if ( (cMsgCheckString(subject) != CMSG_OK ) ||
       (cMsgCheckString(type)    != CMSG_OK )    ) {
    return(CMSG_BAD_ARGUMENT);
  }

  cMsgConnectReadLock(domain);

  if (domain->gotConnection != 1) {
    cMsgConnectReadUnlock(domain);
    return(CMSG_LOST_CONNECTION);
  }
  
  /*
   * Pick a unique identifier for the subject/type pair, and
   * send it to the domain server & remember it for future use
   * Mutex protect this operation as many cmsg_cmsg_connect calls may
   * operate in parallel on this static variable.
   */
  staticMutexLock();
  uniqueId = subjectTypeId++;
  staticMutexUnlock();

  /* use struct as value in hashTable */
  info = (getInfo *)malloc(sizeof(getInfo));
  if (info == NULL) {
    cMsgConnectReadUnlock(domain);
    return(CMSG_OUT_OF_MEMORY);
  }
  cMsgGetInfoInit(info);
  info->id      = uniqueId;
  info->error   = CMSG_OK;
  info->msgIn   = 0;
  info->quit    = 0;
  info->msg     = NULL;
  info->subject = (char *) strdup(subject);
  info->type    = (char *) strdup(type);
    
  /* use string generated from id as key (must be string) in hashTable */
  idString = cMsgIntChars(uniqueId);
  if (idString == NULL) {
    cMsgConnectReadUnlock(domain);
    cMsgGetInfoFree(info);
    free(info);
    return(CMSG_OUT_OF_MEMORY);
  }

  /* Try inserting item into hashtable, if entry exists, pick another key */
  cMsgSubAndGetMutexLock(domain); /* serialize access to subAndGet hash table */
  while ( !hashInsertTry(&domain->subAndGetTable, idString, (void *)info) ) {
    free(idString);
    staticMutexLock();
    uniqueId = subjectTypeId++;
    staticMutexUnlock();
    idString = cMsgIntChars(uniqueId);
    if (idString == NULL) {
      cMsgSubAndGetMutexUnlock(domain);
      cMsgConnectReadUnlock(domain);
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
  outGoing[2] = htonl(uniqueId);  
  /* length of "subject" string */
  lenSubject  = strlen(subject);
  outGoing[3] = htonl(lenSubject);
  /* length of "type" string */
  lenType     = strlen(type);
  outGoing[4] = htonl(lenType);
  /* length of "namespace" string (0 in this case, since
   * only used for server-to-server) */
  outGoing[5] = htonl(0);

  /* total length of message (minus first int) is first item sent */
  len = sizeof(outGoing) - sizeof(int) + lenSubject + lenType;
  outGoing[0] = htonl(len);
  
  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);
  
  iov[1].iov_base = (char*) subject;
  iov[1].iov_len  = lenSubject;
  
  iov[2].iov_base = (char*) type;
  iov[2].iov_len  = lenType;
  
  /* make send socket communications thread-safe */
  cMsgSocketMutexLock(domain);
  
  if (cMsgTcpWritev(fd, iov, 3, 16) == -1) {
    cMsgSocketMutexUnlock(domain);
    cMsgConnectReadUnlock(domain);
    cMsgSubAndGetMutexLock(domain);
    hashRemove(&domain->subAndGetTable, idString, NULL);
    cMsgSubAndGetMutexUnlock(domain);
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
      unSubscribeAndGet(domainId, subject, type, uniqueId);
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
 * @param domainId id of the domain connection
 * @param subject subject of message subscribed to
 * @param type type of message subscribed to
 * @param id unique id associated with a subscribeAndGet
 *
 * @returns CMSG_BAD_ARGUMENT if the domain id is null
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 */   
static int unSubscribeAndGet(void *domainId, const char *subject, const char *type, int id) {
  
  int fd, len, outGoing[6], lenSubject, lenType;
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  struct iovec iov[3];

  if (domain == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  fd = domain->sendSocket;
  
  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(CMSG_UNSUBSCRIBE_AND_GET_REQUEST);
  /* receiverSubscribe */
  outGoing[2] = htonl(id);
  /* length of "subject" string */
  lenSubject  = strlen(subject);
  outGoing[3] = htonl(lenSubject);
  /* length of "type" string */
  lenType     = strlen(type);
  outGoing[4] = htonl(lenType);
  /* length of "namespace" string (0 in this case, since
   * only used for server-to-server) */
  outGoing[5] = htonl(0);

  /* total length of message (minus first int) is first item sent */
  len = sizeof(outGoing) - sizeof(int) + lenSubject + lenType;
  outGoing[0] = htonl(len);

  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);

  iov[1].iov_base = (char*) subject;
  iov[1].iov_len  = lenSubject;

  iov[2].iov_base = (char*) type;
  iov[2].iov_len  = lenType;

  /* make send socket communications thread-safe */
  cMsgSocketMutexLock(domain);

  if (cMsgTcpWritev(fd, iov, 3, 16) == -1) {
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
 * to the original sender. This routine is called by the user through
 * cMsgSendAndGet() given the appropriate UDL. In this domain cMsgFlush()
 * does nothing and does not need to be called for the mesage to be
 * sent immediately.
 *
 * @param domainId id of the domain connection
 * @param sendMsg messages to send to all listeners
 * @param timeout amount of time to wait for the response message; if NULL,
 *                wait forever
 * @param replyMsg message received from the responder
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
int cmsg_cmsg_sendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout,
                         void **replyMsg) {
  
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  cMsgMessage_t *msg = (cMsgMessage_t *) sendMsg;
  int err, uniqueId, status;
  int len, lenSubject, lenType, lenPayloadText, lenText, lenByteArray;
  int fd, highInt, lowInt, outGoing[16];
  char *idString;
  getInfo *info = NULL;
  uint64_t llTime;
  struct timespec wait, now;

  
  if (domain == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  fd = domain->sendSocket;
  
  if (!domain->hasSendAndGet) {
    return(CMSG_NOT_IMPLEMENTED);
  }   
           
  /* check args */
  if (sendMsg == NULL || replyMsg == NULL) return(CMSG_BAD_ARGUMENT);
  
  if ( (cMsgCheckString(msg->subject) != CMSG_OK ) ||
       (cMsgCheckString(msg->type)    != CMSG_OK )    ) {
    return(CMSG_BAD_ARGUMENT);
  }

  cMsgConnectReadLock(domain);

  if (domain->gotConnection != 1) {
    cMsgConnectReadUnlock(domain);
    return(CMSG_LOST_CONNECTION);
  }
 
  if (msg->text == NULL) {
    lenText = 0;
  }
  else {
    lenText = strlen(msg->text);
  }
  
  /* update history here */
  cMsgAddSenderToHistory(sendMsg, domain->name);
  
  /* length of "payloadText" string */
  if (msg->payloadText == NULL) {
    lenPayloadText = 0;
  }
  else {
    lenPayloadText = strlen(msg->payloadText);
  }

  staticMutexLock();
  uniqueId = subjectTypeId++;
  staticMutexUnlock();

  /* use struct as value in hashTable */
  info = (getInfo *)malloc(sizeof(getInfo));
  if (info == NULL) {
    cMsgConnectReadUnlock(domain);
    return(CMSG_OUT_OF_MEMORY);
  }
  cMsgGetInfoInit(info);
  info->id      = uniqueId;
  info->error   = CMSG_OK;
  info->msgIn   = 0;
  info->quit    = 0;
  info->msg     = NULL;
  info->subject = (char *) strdup(msg->subject);
  info->type    = (char *) strdup(msg->type);
    
  /* use string generated from id as key (must be string) in hashTable */
  idString = cMsgIntChars(uniqueId);
  if (idString == NULL) {
    cMsgConnectReadUnlock(domain);
    cMsgGetInfoFree(info);
    free(info);
    return(CMSG_OUT_OF_MEMORY);
  }

  /* Try inserting item into hashtable, if entry exists, pick another key */
  cMsgSendAndGetMutexLock(domain); /* serialize access to sendAndGet hash table */
  while ( !hashInsertTry(&domain->sendAndGetTable, idString, (void *)info) ) {
    free(idString);
    staticMutexLock();
    uniqueId = subjectTypeId++;
    staticMutexUnlock();
    idString = cMsgIntChars(uniqueId);
    if (idString == NULL) {
      cMsgSendAndGetMutexUnlock(domain);
      cMsgConnectReadUnlock(domain);
      cMsgGetInfoFree(info);
      free(info);
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
  outGoing[3] = htonl(msg->userInt);
  /* unique id (senderToken) */
  outGoing[4] = htonl(uniqueId);
  /* bit info */
  outGoing[5] = htonl(msg->info | CMSG_IS_GET_REQUEST);

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

  /* length of "subject" string */
  lenSubject   = strlen(msg->subject);
  outGoing[10] = htonl(lenSubject);
  /* length of "type" string */
  lenType      = strlen(msg->type);
  outGoing[11] = htonl(lenType);
  
  /* namespace length */
  outGoing[12] = htonl(0);
  
  /* length of "payloadText" string */
  outGoing[13] = htonl(lenPayloadText);
  
  /* length of "text" string */
  outGoing[14] = htonl(lenText);
  
  /* length of byte array */
  lenByteArray = msg->byteArrayLength;
  outGoing[15] = htonl(lenByteArray);
    
  /* total length of message (minus first int) is first item sent */
  len = sizeof(outGoing) - sizeof(int) + lenSubject + lenType +
        lenPayloadText + lenText + lenByteArray;
  outGoing[0] = htonl(len);  

  /* make send socket communications thread-safe */
  cMsgSocketMutexLock(domain);
  
  /* allocate more memory for message-sending buffer if necessary */
  if (domain->msgBufferSize < (int)(len+sizeof(int))) {
    free(domain->msgBuffer);
    domain->msgBufferSize = len + 1004; /* give us 1kB extra */
    domain->msgBuffer = (char *) malloc(domain->msgBufferSize);
    if (domain->msgBuffer == NULL) {
      cMsgSocketMutexUnlock(domain);
      cMsgConnectReadUnlock(domain);
      cMsgSendAndGetMutexLock(domain);
      hashRemove(&domain->sendAndGetTable, idString, NULL);
      cMsgSendAndGetMutexUnlock(domain);
      cMsgGetInfoFree(info);
      free(info);
      free(idString);
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
  memcpy(domain->msgBuffer+len, (void *)msg->payloadText, lenPayloadText);
  len += lenPayloadText;
  memcpy(domain->msgBuffer+len, (void *)msg->text, lenText);
  len += lenText;
  memcpy(domain->msgBuffer+len, (void *)&((msg->byteArray)[msg->byteArrayOffset]), lenByteArray);
  len += lenByteArray;   
    
  /* send data over socket */
  if (cMsgTcpWrite(fd, (void *) domain->msgBuffer, len) != len) {
    cMsgSocketMutexUnlock(domain);
    cMsgConnectReadUnlock(domain);
    cMsgSendAndGetMutexLock(domain);
    hashRemove(&domain->sendAndGetTable, idString, NULL);
    cMsgSendAndGetMutexUnlock(domain);
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
      unSendAndGet(domainId, uniqueId);
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
       * Don't need to make a copy of message as only 1 recipient.
       * Message was allocated in client's listening thread and user
       * must free it.
       */
    if (replyMsg != NULL) *replyMsg = info->msg;
    /* May not free this down below in cMsgGetInfoFree. */
    info->msg = NULL;
    err = CMSG_OK;
  }
  
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
 * @param domainId id of the domain connection
 * @param id unique id associated with a sendAndGet
 *
 * @returns CMSG_BAD_ARGUMENT  if the domain id is null
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 */   
static int unSendAndGet(void *domainId, int id) {
  
  int fd, outGoing[3];
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
    
  if (domain == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  fd = domain->sendSocket;
  
  /* size of info coming - 8 bytes */
  outGoing[0] = htonl(8);
  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(CMSG_UN_SEND_AND_GET_REQUEST);
  /* senderToken id */
  outGoing[2] = htonl(id);

  /* make send socket communications thread-safe */
  cMsgSocketMutexLock(domain);
  
  /* send ints over together */
  if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
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
 * The monitoring data in xml format can be obtained by calling cMsgGetText.
 *
 * @param domainId id of the domain connection
 * @param command string to monitor data collecting routine
 * @param replyMsg message received from the domain containing monitor data
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if domainId is NULL
 * @returns CMSG_OUT_OF_MEMORY if no memory available
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSMonitor
 */   
int cmsg_cmsg_monitor(void *domainId, const char *command, void **replyMsg) {
    
  int err;
  cMsgMessage_t *msg;
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  
    
  if (domain == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }     
    
  msg = (cMsgMessage_t *) cMsgCreateMessage();
  if (msg == NULL) {
    return(CMSG_OUT_OF_MEMORY);  
  }
  err = cMsgSetText((void *)msg, domain->monitorXML);

  if (replyMsg != NULL) *replyMsg = (void *)msg;

  return(err);  
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
 * @returns CMSG_OUT_OF_MEMORY if all available subscription memory has been used
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement
 *                               subscribe
 * @returns CMSG_ALREADY_EXISTS if an identical subscription already exists
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
int cmsg_cmsg_subscribe(void *domainId, const char *subject, const char *type,
                        cMsgCallbackFunc *callback,
                        void *userArg, cMsgSubscribeConfig *config, void **handle) {

  int fd, uniqueId, status, err, newSub=0;
  int len, lenSubject, lenType, outGoing[6];
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  subscribeConfig *sConfig = (subscribeConfig *) config;
  cbArg *cbarg;
  struct iovec iov[3];
  pthread_attr_t threadAttribute;
  char *subKey;
  subInfo *sub;
  subscribeCbInfo *cb, *cbItem;
  void *p;

  
  if (domain == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  fd = domain->sendSocket;
  
  if (!domain->hasSubscribe) {
    return(CMSG_NOT_IMPLEMENTED);
  } 
  
  /* check args */  
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

  tryagain:
      
  while (1) {
    err = CMSG_OK;
    
    cMsgConnectReadLock(domain);

    if (domain->gotConnection != 1) {
      cMsgConnectReadUnlock(domain);
      free(subKey);
      err = CMSG_LOST_CONNECTION;
      break;
    }

    /* use default configuration if none given */
    if (config == NULL) {
      sConfig = (subscribeConfig *) cMsgSubscribeConfigCreate();
    }

    /* make sure subscribe and unsubscribe are not run at the same time */
    cMsgSubscribeMutexLock(domain);

    /* if this subscription does NOT exist (try finding in hash table), make one */
    if (hashLookup(&domain->subscribeTable, subKey, &p)) {
      sub = (subInfo *)p; /* avoid compiler warning */
    }
    else {
      sub = (subInfo *) calloc(1, sizeof(subInfo));
      if (sub == NULL) {
        cMsgSubscribeMutexUnlock(domain);
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
      cMsgSubscribeMutexUnlock(domain);
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
      cMsgSubscribeMutexUnlock(domain);
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

    /* release allocated memory */
    pthread_attr_destroy(&threadAttribute);
    if (config == NULL) {
      cMsgSubscribeConfigDestroy((cMsgSubscribeConfig *) sConfig);
    }

    /* if this is an existing subscription, just adding callback is good enough */
    if (!newSub) {
      cMsgSubscribeMutexUnlock(domain);
      cMsgConnectReadUnlock(domain);
      domain->monData.numSubscribes++;
      return(CMSG_OK);
    }

    staticMutexLock();
    uniqueId = subjectTypeId++;
    staticMutexUnlock();
    sub->id = uniqueId;

    /* notify domain server */

    /* message id (in network byte order) to domain server */
    outGoing[1] = htonl(CMSG_SUBSCRIBE_REQUEST);
    /* unique id to domain server */
    outGoing[2] = htonl(uniqueId);
    /* length of "subject" string */
    lenSubject  = strlen(subject);
    outGoing[3] = htonl(lenSubject);
    /* length of "type" string */
    lenType     = strlen(type);
    outGoing[4] = htonl(lenType);
    /* length of "namespace" string (0 in this case, since
     * only used for server-to-server) */
    outGoing[5] = htonl(0);

    /* total length of message is first item sent */
    len = sizeof(outGoing) - sizeof(int) + lenSubject + lenType;
    outGoing[0] = htonl(len);

    iov[0].iov_base = (char*) outGoing;
    iov[0].iov_len  = sizeof(outGoing);

    iov[1].iov_base = (char*) subject;
    iov[1].iov_len  = lenSubject;

    iov[2].iov_base = (char*) type;
    iov[2].iov_len  = lenType;

    /* make send socket communications thread-safe */
    cMsgSocketMutexLock(domain);

    if (cMsgTcpWritev(fd, iov, 3, 16) == -1) {
        cMsgSocketMutexUnlock(domain);
        
        /* stop callback thread, should be OK as no msgs should be pending */
        cb->quit = 1;
        pthread_cancel(cb->thread);
        
        /* set resources free */
        cMsgSubscribeInfoFree(sub);
        cMsgCallbackInfoFree(cb);
        free(subKey);

        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cmsg_cmsg_subscribe: write failure\n");
        }
        
        err = CMSG_LOST_CONNECTION;
        break;
    }
    
    /* done protecting communications */
    cMsgSocketMutexUnlock(domain);
      
    domain->monData.numSubscribes++;

    /* done protecting subscribe */
    cMsgSubscribeMutexUnlock(domain);
    cMsgConnectReadUnlock(domain);
      
    break;
  } /* while(1) */

  if (err!= CMSG_OK) {
    /* don't wait for resubscribes */
    if (failoverSuccessful(domain, 0)) {
       fd = domain->sendSocket;
       printf("cmsg_cmsg_subscribe: FAILOVER SUCCESSFUL, try subscribe again\n");
       goto tryagain;
    }  
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      printf("cmsg_cmsg_subscribe: FAILOVER NOT successful, quitting, err = %d\n", err);
    }
  }

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
  int uniqueId, len, lenSubject, lenType, outGoing[6];
  int fd = domain->sendSocket;

  if (domain->gotConnection != 1) {
    return(CMSG_LOST_CONNECTION);
  }
  
  /* Pick a unique identifier for the subject/type pair. */
  staticMutexLock();
  uniqueId = subjectTypeId++;
  staticMutexUnlock();
  sub->id = uniqueId;

  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(CMSG_SUBSCRIBE_REQUEST);
  /* unique id to domain server */
  outGoing[2] = htonl(uniqueId);
  /* length of "subject" string */
  lenSubject  = strlen(sub->subject);
  outGoing[3] = htonl(lenSubject);
  /* length of "type" string */
  lenType     = strlen(sub->type);
  outGoing[4] = htonl(lenType);
  /* length of "namespace" string (0 in this case, since namespace
   * only used for server-to-server) */
  outGoing[5] = htonl(0);

  /* total length of message is first item sent */
  len = sizeof(outGoing) - sizeof(int) + lenSubject + lenType;
  outGoing[0] = htonl(len);

  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);

  iov[1].iov_base = (char*) sub->subject;
  iov[1].iov_len  = lenSubject;

  iov[2].iov_base = (char*) sub->type;
  iov[2].iov_len  = lenType;


  if (cMsgTcpWritev(fd, iov, 3, 16) == -1) {
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
 * @param handle void pointer obtained from cmsg_cmsg_subscribe
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id, handle or its subject, type, or callback are null,
 *                            or the given subscription (thru handle) does not have
 *                            an active subscription or callbacks
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement
 *                               unsubscribe
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
int cmsg_cmsg_unsubscribe(void *domainId, void *handle) {

  int fd, status, err;
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  struct iovec iov[3];
  cbArg           *cbarg;
  subInfo         *sub;
  subscribeCbInfo *cb, *cbItem, *cbPrev;
  cMsgMessage_t *msg, *nextMsg;
  void *p;
  
  if (domain == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  fd = domain->sendSocket;
  
  if (!domain->hasUnsubscribe) {
    return(CMSG_NOT_IMPLEMENTED);
  }
 
  /* check args */
  if (handle == NULL) {
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
    cMsgSubscribeMutexLock(domain);
        
    /* Delete entry and notify server if there was at least 1 callback
     * to begin with and now there are none for this subject/type.
     */
    if (sub->numCallbacks - 1 < 1) {

      int len, lenSubject, lenType;
      int outGoing[6];

      /* notify server */

      /* message id (in network byte order) to domain server */
      outGoing[1] = htonl(CMSG_UNSUBSCRIBE_REQUEST);
      /* unique id associated with subject/type */
      outGoing[2] = htonl(sub->id);
      /* length of "subject" string */
      lenSubject  = strlen(sub->subject);
      outGoing[3] = htonl(lenSubject);
      /* length of "type" string */
      lenType     = strlen(sub->type);
      outGoing[4] = htonl(lenType);
      /* length of "namespace" string (0 in this case, since
       * only used for server-to-server) */
      outGoing[5] = htonl(0);

      /* total length of message (minus first int) is first item sent */
      len = sizeof(outGoing) - sizeof(int) + lenSubject + lenType;
      outGoing[0] = htonl(len);

      iov[0].iov_base = (char*) outGoing;
      iov[0].iov_len  = sizeof(outGoing);

      iov[1].iov_base = (char*) sub->subject;
      iov[1].iov_len  = lenSubject;

      iov[2].iov_base = (char*) sub->type;
      iov[2].iov_len  = lenType;

      /* make send socket communications thread-safe */
      cMsgSocketMutexLock(domain);

      if (cMsgTcpWritev(fd, iov, 3, 16) == -1) {
        cMsgSocketMutexUnlock(domain);
        cMsgSubscribeMutexUnlock(domain);
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
        printf("cmsg_cmsg_unsubscribe: no cbs in sub list (?!), cannot remove cb\n");
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

    /* tell callback thread to end gracefully */
    cb->quit = 1;

    /* Kill callback thread. Plays same role as pthread_cond_signal.
     * Thread's cleanup handler will free cb memory, handle cb cleanup.
     */
    pthread_cancel(cb->thread);
    
    cMsgMutexUnlock(&cb->mutex);

    /* Signal to cMsgRunCallbacks in case the callback's cue is full and
    * the message-receiving thread is blocked trying to put another message in.
    * So now we tell it that there are no messages in the cue and, in fact,
    * no callback anymore. It will only wake up once we call
    * cMsgSubscribeMutexUnlock.
    */
    status = pthread_cond_signal(&domain->subscribeCond);
    if (status != 0) {
      cmsg_err_abort(status, "Failed RunCallbacks condition signal");
    }

    domain->monData.numUnsubscribes++;
    
    /* done protecting unsubscribe */
    cMsgSubscribeMutexUnlock(domain);
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
       printf("cmsg_cmsg_unsubscribe: FAILOVER SUCCESSFUL, try unsubscribe again\n");
       goto tryagain;
    }  
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      printf("cmsg_cmsg_unsubscirbe: FAILOVER NOT successful, quitting, err = %d\n", err);
    }
  }
  
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
 * @returns CMSG_BAD_ARGUMENT if domainId is null
 */   
int cmsg_cmsg_start(void *domainId) {
  
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
int cmsg_cmsg_stop(void *domainId) {
  
  if (domainId == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  ((cMsgDomainInfo *) domainId)->receiveState = 0;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine disconnects the client from the cMsg server. This is done
 * by telling the server to kill its connection to this client. This client's
 * keepAlive thread will detect this and do the real disconnect.
 *
 * @param domainId pointer to id of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if domainId or the pointer it points to is NULL
 */
int cmsg_cmsg_disconnect(void **domainId) {
  
  int outGoing[2];
  cMsgDomainInfo *domain;

  if (domainId == NULL) return(CMSG_BAD_ARGUMENT);
  domain = (cMsgDomainInfo *) (*domainId);
  if (domain == NULL) return(CMSG_BAD_ARGUMENT);
        
  cMsgConnectWriteLock(domain);

  if (!domain->gotConnection) {
    cMsgConnectWriteUnlock(domain);
    return(CMSG_OK);
  }
  
  /* Tell server we're disconnecting */  
  /* size of msg */
  outGoing[0] = htonl(4);
  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(CMSG_SERVER_DISCONNECT);

  /* make send socket communications thread-safe */
  cMsgSocketMutexLock(domain);
  
  /* send int */
  if (cMsgTcpWrite(domain->sendSocket, (char*) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
    /* if there is an error we are most likely in the process of disconnecting */
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_cmsg_disconnect: write failure, but continue\n");
    }
  }

  /* calling "disconnect" takes precedence over failing-over to another server */
  domain->implementFailovers = 0;
  
  /* after calling disconnect, no top level API routines should function */
  domain->gotConnection = 0;
  
  cMsgSocketMutexUnlock(domain);
  cMsgConnectWriteUnlock(domain);
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/

/**
 * This routine disconnects the client from the cMsg server when
 * called by the keepAlive thread.
 *
 * @param domainId id of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if domainId or the pointer it points to is NULL
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 */   
static int disconnectFromKeepAlive(void **pdomainId) {
  
  int i, status, tblSize;
  cMsgDomainInfo *domain;
  subscribeCbInfo *cb, *cbNext;
  subInfo *sub;
  getInfo *info;
  struct timespec wait4thds = {0, 200000000}; /* 0.2 sec */
  hashNode *entries = NULL;
  cMsgMessage_t *msg, *nextMsg;
  void *p;

  if (pdomainId == NULL) return(CMSG_BAD_ARGUMENT);
  domain = (cMsgDomainInfo *) (*pdomainId);
  if (domain == NULL) return(CMSG_BAD_ARGUMENT);
      
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "disconnectFromKeepAlive: IN, pdomainId = %p, *pdomainId = %p\n",
            pdomainId, *pdomainId);
  }
  cMsgConnectWriteLock(domain);
    
  domain->gotConnection = 0;

  /* stop msg receiving thread */
  pthread_cancel(domain->pendThread);
   
  /* stop thread writing keep alives to server */
  pthread_cancel(domain->updateServerThread);
   
  /* close sending socket */
  close(domain->sendSocket);
    
  /* terminate all callback threads */
  
  /* Don't want incoming msgs to be delivered to callbacks will removing them. */
  cMsgSubscribeMutexLock(domain);

  /* get client subscriptions */
  hashClear(&domain->subscribeTable, &entries, &tblSize);
  
  /* if there are subscriptions ... */
  if (entries != NULL) {
    /* for each client subscription ... */
    for (i=0; i<tblSize; i++) {
      sub = (subInfo *)entries[i].data;
      /* if the subject & type's match, run callbacks */
      cb = sub->callbacks;

      /* for each callback ... */
      while (cb != NULL) {
        /*
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "cmsg_cmsg_disconnect: callback thread = %p\n", cb);
        }
        */
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
      
        /* once the callback thread is woken up, it will free cb memory,
         * so store anything from that struct locally, NOW. */
        cbNext = cb->next;

        /* tell callback thread to end gracefully */
        cb->quit = 1;

        /* Kill callback thread. Plays same role as pthread_cond_signal.
         * Thread's cleanup handler will free cb memory, handle cb cleanup.
         */
        pthread_cancel(cb->thread);
    
        cMsgMutexUnlock(&cb->mutex);

        cb = cbNext;
      } /* next callback */
      
      free(entries[i].key);
      cMsgSubscribeInfoFree(sub);
      free(sub);
    } /* next subscription */
    
    free(entries);
  } /* if there are subscriptions */

  /* Pthread_cancelling the callback threads will not allow them to wake up
   * the runCallbacks (msg receiving) thread, which must be done in case it
   * is stuck with a full Q. Do it now.
   */
  status = pthread_cond_signal(&domain->subscribeCond);
  if (status != 0) {
    cmsg_err_abort(status, "Failed subscribe condition signal");
  }

  cMsgSubscribeMutexUnlock(domain);
  sched_yield();
  
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
    
  /* Unblock SIGPIPE */
  cMsgRestoreSignals(domain);

  cMsgConnectWriteUnlock(domain);

  /* There may be other threads that have simultaneously called send, syncSend,
   * sub&Get, send&Get, etc. They must be currently waiting on the domain->connectLock.
   * This sleep allows these threads to wake up and figure out that there is no more
   * connection before we free the memory of that lock.
   */
  nanosleep(&wait4thds, NULL);
    
  status = rwl_destroy (&domain->connectLock);
  if (status != 0) {
    cmsg_err_abort(status, "disconnectFromKeepAlive: destroying connect read/write lock");
  }

  /* Clean up memory */
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "disconnectFromKeepAlive: free domain memory at %p\n", domain);
  }
  cMsgDomainFree(domain);
  free(domain);
  
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
int cmsg_cmsg_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler,
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
 * Method to shutdown the given clients.
 *
 * @param domainId id of the domain connection
 * @param client client(s) to be shutdown
 * @param flag   flag describing the mode of shutdown: 0 to not include self,
 *               CMSG_SHUTDOWN_INCLUDE_ME to include self in shutdown.
 * 
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id is null
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement shutdown
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 */
int cmsg_cmsg_shutdownClients(void *domainId, const char *client, int flag) {
  
  int fd, len, cLen, outGoing[4];
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  struct iovec iov[2];


  if (domain == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  fd = domain->sendSocket;
  
  if (!domain->hasShutdown) {
    return(CMSG_NOT_IMPLEMENTED);
  } 
        
  cMsgConnectWriteLock(domain);
    
  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(CMSG_SHUTDOWN_CLIENTS);
  outGoing[2] = htonl(flag);
  
  if (client == NULL) {
    cLen = 0;
    outGoing[3] = 0;
  }
  else {
    cLen = strlen(client);
    outGoing[3] = htonl(cLen);
  }
  
  /* total length of message (minus first int) is first item sent */
  len = sizeof(outGoing) - sizeof(int) + cLen;
  outGoing[0] = htonl(len);
  
  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);

  iov[1].iov_base = (char*) client;
  iov[1].iov_len  = cLen;
  
  /* make send socket communications thread-safe */
  cMsgSocketMutexLock(domain);
  
  if (cMsgTcpWritev(fd, iov, 2, 16) == -1) {
    cMsgSocketMutexUnlock(domain);
    cMsgConnectWriteUnlock(domain);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_cmsg_unsubscribe: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
   
  cMsgSocketMutexUnlock(domain);  
  cMsgConnectWriteUnlock(domain);

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
 * @returns CMSG_BAD_ARGUMENT if the id is null
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement shutdown
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 */
int cmsg_cmsg_shutdownServers(void *domainId, const char *server, int flag) {
  
  int fd, len, sLen, outGoing[4];
  cMsgDomainInfo *domain = (cMsgDomainInfo *) domainId;
  struct iovec iov[2];


  if (domain == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  fd = domain->sendSocket;
  
  if (!domain->hasShutdown) {
    return(CMSG_NOT_IMPLEMENTED);
  } 
        
  cMsgConnectWriteLock(domain);
    
  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(CMSG_SHUTDOWN_SERVERS);
  outGoing[2] = htonl(flag);
  
  if (server == NULL) {
    sLen = 0;
    outGoing[3] = 0;
  }
  else {
    sLen = strlen(server);
    outGoing[3] = htonl(sLen);
  }

  /* total length of message (minus first int) is first item sent */
  len = sizeof(outGoing) - sizeof(int) + sLen;
  outGoing[0] = htonl(len);
  
  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);

  iov[1].iov_base = (char*) server;
  iov[1].iov_len  = sLen;
  
  /* make send socket communications thread-safe */
  cMsgSocketMutexLock(domain);
  
  if (cMsgTcpWritev(fd, iov, 2, 16) == -1) {
    cMsgSocketMutexUnlock(domain);
    cMsgConnectWriteUnlock(domain);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsg_cmsg_unsubscribe: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
   
  cMsgSocketMutexUnlock(domain);  
  cMsgConnectWriteUnlock(domain);

  return CMSG_OK;

}


/*-------------------------------------------------------------------*/


/**
 * This routine exchanges information with the name server.
 *
 * @param domain  pointer to element in domain info array
 * @param serverfd  socket to send to cMsg name server
 * @param failoverIndex  index into the array of parsed UDLs of the current UDL.
 * 
 * @returns CMSG_OK if successful
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server (can't read or write)
 *
 */
static int talkToNameServer(cMsgDomainInfo *domain, int serverfd, int failoverIndex) {

  int  err, lengthDomain, lengthSubdomain, lengthRemainder, lengthPassword;
  int  lengthHost, lengthName, lengthUDL, lengthDescription;
  int  outGoing[15], inComing[3];
  char temp[CMSG_MAXHOSTNAMELEN], atts[7];
  const char *domainType = "cMsg";
  struct iovec iov[9];
  parsedUDL *pUDL = &domain->failovers[failoverIndex];

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
  outGoing[6] = htonl(pUDL->regime);
  /* send length of password for connecting to server.*/
  if (pUDL->password == NULL) {
    lengthPassword = outGoing[7] = 0;
  }
  else {
    lengthPassword = strlen(pUDL->password);
    outGoing[7]    = htonl(lengthPassword);
  }
  /* send length of the type of domain server I'm expecting to connect to.*/
  lengthDomain = strlen(domainType);
  outGoing[8]  = htonl(lengthDomain);
  /* send length of the type of subdomain handler I'm expecting to use.*/
  lengthSubdomain = strlen(pUDL->subdomain);
  outGoing[9] = htonl(lengthSubdomain);
  /* send length of the UDL remainder.*/
  /* this may be null */
  if (pUDL->subRemainder == NULL) {
    lengthRemainder = outGoing[10] = 0;
  }
  else {
    lengthRemainder = strlen(pUDL->subRemainder);
    outGoing[10] = htonl(lengthRemainder);
  }
  /* send length of my host name to server */
  lengthHost   = strlen(domain->myHost);
  outGoing[11] = htonl(lengthHost);
  /* send length of my name to server */
  lengthName   = strlen(domain->name);
  outGoing[12] = htonl(lengthName);
  /* send length of my udl to server */
  lengthUDL    = strlen(pUDL->udl);
  outGoing[13] = htonl(lengthUDL);
  /* send length of my description to server */
  lengthDescription = strlen(domain->description);
  outGoing[14]      = htonl(lengthDescription);
    
  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);
  
  iov[1].iov_base = (char*) pUDL->password;
  iov[1].iov_len  = lengthPassword;
  
  iov[2].iov_base = (char*) domainType;
  iov[2].iov_len  = lengthDomain;
  
  iov[3].iov_base = (char*) pUDL->subdomain;
  iov[3].iov_len  = lengthSubdomain;
  
  iov[4].iov_base = (char*) pUDL->subRemainder;
  iov[4].iov_len  = lengthRemainder;
  
  iov[5].iov_base = (char*) domain->myHost;
  iov[5].iov_len  = lengthHost;
  
  iov[6].iov_base = (char*) domain->name;
  iov[6].iov_len  = lengthName;
  
  iov[7].iov_base = (char*) domain->udl;
  iov[7].iov_len  = lengthUDL;
  
  iov[8].iov_base = (char*) domain->description;
  iov[8].iov_len  = lengthDescription;
  
  if (cMsgTcpWritev(serverfd, iov, 9, 16) == -1) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "talkToNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
    
  /* now read server reply */
  if (cMsgTcpRead(serverfd, (void *) &err, sizeof(err)) != sizeof(err)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "talkToNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  err = ntohl(err);
    
  /* if there's an error, read error string then quit */
  if (err != CMSG_OK) {
    int   len;
    char *string;

    /* read length of error string */
    if (cMsgTcpRead(serverfd, (char*) &len, sizeof(len)) != sizeof(len)) {
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
      
    if (cMsgTcpRead(serverfd, (char*) string, len) != len) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "talkToNameServer: cannot read error string\n");
      }
      free(string);
      return(CMSG_NETWORK_ERROR);
    }
    /* add null terminator to C string */
    string[len] = 0;
    
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
  if (cMsgTcpRead(serverfd, (char*) atts, sizeof(atts)) != sizeof(atts)) {
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
    fprintf(stderr, "talkToNameServer: read port and length of host from server\n");
  }
  
  /* read port & length of host name to send to*/
  if (cMsgTcpRead(serverfd, (char*) inComing, sizeof(inComing)) != sizeof(inComing)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "talkToNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  domain->sendPort    = ntohl(inComing[0]);
  domain->sendUdpPort = ntohl(inComing[1]);
  lengthHost          = ntohl(inComing[2]);

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "talkToNameServer: port = %d, host len = %d\n",
              domain->sendPort, lengthHost);
    fprintf(stderr, "talkToNameServer: read host from server\n");
  }
  
  /* read host name to send to */
  if (cMsgTcpRead(serverfd, (char*) temp, lengthHost) != lengthHost) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "talkToNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  /* be sure to null-terminate string */
  temp[lengthHost] = 0;
  domain->sendHost = (char *) strdup(temp);
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "talkToNameServer: host = %s\n", domain->sendHost);
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


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

    kaThdArg *kaArg = (kaThdArg *) arg;
    void **domainId = kaArg->domainId;
    cMsgDomainInfo *domain = kaArg->domain;
    int socket = domain->keepAliveSocket;
    int outGoing, err, len;
    
    int failoverIndex = domain->failoverIndex;
    int connectFailures = 0;
    int weGotAConnection = 1; /* true */
    struct timespec wait;
    void *p;
    
    /* increase concurrency for this thread for early Solaris */
    int  con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);

    free(arg);

/*printf("ka: arg = %p, domainId = %p, domain = %p\n", arg, domainId, domain);*/
    wait.tv_sec  = 1;
    wait.tv_nsec = 100000000; /* 1.1 sec */

    /* release system resources when thread finishes */
    pthread_detach(pthread_self());

    /* periodically send a keep alive message and read response */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "keepAliveThread: keep alive thread created, socket = %d\n", socket);
    }
  
    /* request to send */
    outGoing = htonl(CMSG_KEEP_ALIVE);
    
    while (weGotAConnection) {
    
        while(1) {       

           /* read len of monitoring data to come */
           if ((err = cMsgTcpRead(socket, (char*) &len, sizeof(len))) != sizeof(len)) {
               /*if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                   fprintf(stderr, "keepAliveThread: read failure\n");
                 }*/
               break;
           }
           len = ntohl(len);
           
           /* check for room in memory */
           if (len > domain->monitorXMLSize) {
               if (domain->monitorXML != NULL) free(domain->monitorXML);
               domain->monitorXML = (char *) calloc(1, len+1);
               domain->monitorXMLSize = len;
               if (domain->monitorXML == NULL) {
                   if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                     fprintf(stderr, "keepAliveThread: no memory for size %d\n", (len+1));
                   }                
                   exit(-1);
               }
           }

           /* read monitoring data */
           if ((err = cMsgTcpRead(socket, domain->monitorXML, len)) !=  len) {
               /*if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                   fprintf(stderr, "keepAliveThread: read failure\n");
                 }*/
               break;
           }
        }

        /* clean up */
        close(domain->keepAliveSocket);
        close(domain->sendSocket);
        
        /* Start by trying to connect to the first UDL on the list.
         * If we've just been connected to that UDL, try the next. */
        if (failoverIndex != 0) {
            failoverIndex = -1;
        }
        connectFailures = 0;
        weGotAConnection = 0;
        domain->resubscribeComplete = 0;

        /* grab mutex to keep from conflicting with "disconnect" */
        cMsgConnectWriteLock(domain);

        while (domain->implementFailovers && !weGotAConnection) {
            if (connectFailures >= domain->failoverSize) {
/* printf("ka: Reached our limit of UDLs so quit\n"); */
              break;
            }
            
            /* Go through the UDL's until one works */
            
            /* check to see if UDL valid for cMsg domain */
            if (!domain->failovers[++failoverIndex].valid) {
              connectFailures++;
/* printf("ka: skip invalid UDL = %s\n",
            domain->failovers[failoverIndex].udl); */
              continue;
            }

            /* connect using that UDL info */
/* printf("ka: trying to reconnect with UDL = %s\n",
            domain->failovers[failoverIndex].udl); */

            if (domain->failovers[failoverIndex].mustMulticast == 1) {
              free(domain->failovers[failoverIndex].nameServerHost);
              connectWithMulticast(domain, failoverIndex,
                                   &domain->failovers[failoverIndex].nameServerHost,
                                   &domain->failovers[failoverIndex].nameServerPort);     
            }

            err = reconnect(domainId, failoverIndex);
            if (err != CMSG_OK) {
              connectFailures++;
/* printf("ka: ERROR reconnecting, continue\n"); */
              continue;
            }
/* printf("ka: Connected!!, now restore subscriptions\n"); */

            /* restore subscriptions on the new server */
            err = restoreSubscriptions(domain);            
            if (err != CMSG_OK) {
              /* if subscriptions fail, then we do NOT use this failover server */
              connectFailures++;
/* printf("ka: ERROR restoring subscriptions, continue\n"); */
              continue;
            }
            
            domain->failoverIndex = failoverIndex;
            domain->resubscribeComplete = 1;
/* printf("ka: Set domain->keepaliveSocket to %d\n", domain->keepAliveSocket); */            
            socket = domain->keepAliveSocket; 

            /* we got ourselves a new server, boys */
            weGotAConnection = 1;
            
            /* wait for up to 1.1 sec for waiters to respond */
            err = cMsgLatchCountDown(&domain->syncLatch, &wait);
            if (err != 1) {
/* printf("ka: Problems with reporting back to countdowner\n"); */            
            }
            cMsgLatchReset(&domain->syncLatch, 1, NULL);
        }
        cMsgConnectWriteUnlock(domain);
    } /* while we gotta connection */

    /* close communication socket */
    close(socket);

    /* if we've reach here, there's an error, do a disconnect */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "keepAliveThread: server is probably dead, disconnect\n");
    }
    
/* printf("\n\n\nka: DISCONNECTING \n\n\n"); */            
    p = (void *)domain; /* get rid of compiler warnings */
    disconnectFromKeepAlive(&p);
    
    /* Will set the id returned by cmsg_cmsg_connect to NULL,
     * so no one else can use it.
     */
    *domainId = NULL;
 /*printf("ka: domainId = %p, p = %p, domain = %p, setting *domainId to NULL\n", domainId, p, domain);*/
    
    sun_setconcurrency(con);
    
    return NULL;
}


/*-------------------------------------------------------------------*/


/**
 * Routine that periodically sends statistical info to the domain server.
 * The server uses this to gauge the health of this client.
 */
static void *updateServerThread(void *arg) {

    cMsgDomainInfo *domain = (cMsgDomainInfo *) arg;
    int socket, err;
    int sleepTime = 2; /* 2 secs between monitoring sends */
        
    /* increase concurrency for this thread for early Solaris */
    int  con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);

    /* release system resources when thread finishes */
    pthread_detach(pthread_self());

    /* periodically send a keep alive (monitoring data) message */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "updateServerThread: update server thread created, socket = %d\n",
              domain->keepAliveSocket);
    }
      
    while (1) {                
      /* This may change if we've failed over to another server */      
      socket = domain->keepAliveSocket; 

      err = sendMonitorInfo(domain, socket);
      if (err != CMSG_OK) {
        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "updateServerThread: write failure\n");
        }
      }
      
      /* wait */
      sleep(sleepTime);
    }
    
    sun_setconcurrency(con);
    
    return NULL;
}


/**
 * This routine gathers and sends monitoring data to the server
 * as a response to the keep alive command.
 */
static int sendMonitorInfo(cMsgDomainInfo *domain, int connfd) {

  char *indent1 = "      ";
  char *indent2 = "        ";
  char buffer[8192], *xml, *pchar;
  int i, size, tblSize, len=0, num=0, err=CMSG_OK, outInt[5];
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
  cMsgSubscribeMutexLock(domain);
  
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
        sprintf(pchar, "%d%s%llu%s%d", num++, "\" received=\"",
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

  cMsgSubscribeMutexUnlock(domain);

  /* total number of bytes to send */
  size = strlen(xml) + sizeof(outInt) - sizeof(int) + sizeof(out64);
/*
printf("sendMonitorInfo: xml len = %d, size of int arry = %d, size of 64 bit int array = %d, total size = %d\n",
        strlen(xml), sizeof(outInt), sizeof(out64), size);
*/
  outInt[0] = htonl(size);
  outInt[1] = htonl(strlen(xml));
  outInt[2] = 0; /* This is a C/C++ client (1 for java) */
  outInt[3] = htonl(monData->subAndGets);  /* pending sub&gets */
  outInt[4] = htonl(monData->sendAndGets); /* pending send&gets */
  
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
  len = size + sizeof(int);
  /* xml already in buffer */
  
  /* respond with monitor data */
  if (cMsgTcpWrite(connfd, (void *) buffer, len) != len) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "sendMonitorInfo: write failure\n");
    }
    err = CMSG_NETWORK_ERROR;
  }
  
  return err;      
}




/*-------------------------------------------------------------------*/
/*   miscellaneous local functions                                   */
/*-------------------------------------------------------------------*/


/**
 * This routine parses, using regular expressions, the cMsg domain
 * portion of the UDL sent from the next level up" in the API.
 * The full cMsg domain UDL is of the form:<p>
 *     cMsg:cMsg://&lt;host&gt;:&lt;port&gt;/&lt;subdomainType&gt;/&lt;subdomain remainder&gt;?tag=value&tag2=value2& ...<p>
 *
 * The first "cMsg:" is optional. The subdomain is optional with
 * the default being cMsg.
 *
 * Remember that for this domain:
 * 1) port is not necessary
 * 2) host can be "localhost", may include dots (.), or may be dotted decimal
 * 3) if domainType is cMsg, subdomainType is automatically set to cMsg if not given.
 *    if subdomainType is not cMsg, it is required
 * 4) remainder is past on to the subdomain plug-in
 * 5) tag/val of multicastTO=&lt;value&gt; is looked for
 * 6) tag/val of msgpassword=&lt;value&gt; is looked for
 * 7) tag/val of regime=low or regime=high is looked for
 *
 *
 * @param UDL      full udl to be parsed
 * @param password pointer filled in with password
 * @param host     pointer filled in with host
 * @param port     pointer filled in with port
 * @param UDLRemainder    pointer filled in with UDl with cMsg:cMsg:// removed
 * @param subdomainType   pointer filled in with subdomain type
 * @param UDLsubRemainder pointer filled in with everything after subdomain portion of UDL
 * @param multicast       pointer filled in with 1 if multicast specified, else 0
 * @param timeout         pointer filled in with multicast timeout if specified
 * @param regime          pointer filled in with:
 *                        CMSG_REGIME_LOW if regime of client will be low data througput rate
 *                        CMSG_REGIME_HIGH if regime of client will be high data througput rate
 *                        CMSG_REGIME_MEDIUM if regime of client will be medium data througput rate
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_FORMAT if UDL arg is not in the proper format
 * @returns CMSG_BAD_ARGUMENT if UDL arg is NULL
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 * @returns CMSG_OUT_OF_RANGE if port is an improper value
 */
static int parseUDL(const char *UDL, char **password,
                          char **host, int *port,
                          char **UDLRemainder,
                          char **subdomainType,
                          char **UDLsubRemainder,
                          int   *multicast,
                          int   *timeout,
                          int   *regime) {

    int        i, err, Port, index;
    int        mustMulticast = 0;
    size_t     len, bufLength;
    char       *p, *udl, *udlLowerCase, *udlRemainder, *remain;
    char       *buffer;
    const char *pattern = "([a-zA-Z0-9\\.\\-]+):?([0-9]+)?/?([a-zA-Z0-9]+)?/?(.*)";  
    regmatch_t matches[5]; /* we have 5 potential matches: 1 whole, 4 sub */
    regex_t    compiled;
    
    if (UDL == NULL) {
        return (CMSG_BAD_FORMAT);
    }
    
    /* make a copy */
    udl = (char *) strdup(UDL);
    
    /* make a copy in all lower case */
    udlLowerCase = (char *) strdup(UDL);
    len = strlen(udlLowerCase);
    for (i=0; i<len; i++) {
      udlLowerCase[i] = tolower(udlLowerCase[i]);
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
/* printf("parseUDL: udl remainder = %s\n", udlRemainder); */
    
    if (UDLRemainder != NULL) {
        *UDLRemainder = (char *) strdup(udlRemainder);
    }        
  
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
    /* wiil never happen */
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
                
        if (strcasecmp(buffer, "multicast") == 0 ||
            strcmp(buffer, CMSG_MULTICAST_ADDR) == 0) {
            mustMulticast = 1;
/* printf("set mustMulticast to true (locally in parse method)"); */
        }
        else if (strcasecmp(buffer, CMSG_MULTICAST_ADDR) == 0) {
            mustMulticast = 1;
/* printf("set mustMulticast to true (locally in parse method)"); */
        }
        /* if the host is "localhost", find the actual host name */
        else if (strcasecmp(buffer, "localhost") == 0) {
/* printf("parseUDL: host = localhost\n"); */
            /* get canonical local host name */
            if (cMsgLocalHost(buffer, bufLength) != CMSG_OK) {
                /* error */
                free(udl);
                free(buffer);
                return (CMSG_BAD_FORMAT);
            }
        }
        
        if (host != NULL) {
            *host = (char *)strdup(buffer);
        }
        if (multicast != NULL) {
            *multicast = mustMulticast;
        }
    }
/*
printf("parseUDL: host = %s\n", buffer);
printf("parseUDL: mustMulticast = %d\n", mustMulticast);
 */

    /* find port */
    if (matches[2].rm_so < 0) {
        /* no match for port so use default */
        if (mustMulticast == 1) {
            Port = CMSG_NAME_SERVER_MULTICAST_PORT;
        }
        else {
            Port = CMSG_NAME_SERVER_STARTING_PORT;
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
      if (host != NULL) free((void *) *host);
      free(udl);
      free(buffer);
      return (CMSG_OUT_OF_RANGE);
    }
               
    if (port != NULL) {
      *port = Port;
    }
/* printf("parseUDL: port = %hu\n", Port ); */


    /* find subdomain */
    if (matches[3].rm_so < 0) {
        /* no match for subdomain, cMsg is default */
        if (subdomainType != NULL) {
            *subdomainType = (char *) strdup("cMsg");
        }
/* printf("parseUDL: subdomain = cMsg\n"); */
    }
    else {
        buffer[0] = 0;
        len = matches[3].rm_eo - matches[3].rm_so;
        strncat(buffer, udlRemainder+matches[3].rm_so, len);
                
        if (subdomainType != NULL) {
            *subdomainType = (char *) strdup(buffer);
        }        
/* printf("parseUDL: subdomain = %s\n", buffer); */
    }


    /* find subdomain remainder */
    buffer[0] = 0;
    if (matches[4].rm_so < 0) {
        /* no match */
        if (UDLsubRemainder != NULL) {
            *UDLsubRemainder = NULL;
        }
    }
    else {
        len = matches[4].rm_eo - matches[4].rm_so;
        strncat(buffer, udlRemainder+matches[4].rm_so, len);
                
        if (UDLsubRemainder != NULL) {
            *UDLsubRemainder = (char *) strdup(buffer);
        }        
/* printf("parseUDL: subdomain remainder = %s, len = %d\n", buffer, len); */
    }


    /* find optional parameters */
    len = strlen(buffer);
    while (len > 0) {
        /* find cmsgpassword parameter if it exists*/
        /* look for ?cmsgpassword=value& or &cmsgpassword=value& */
        pattern = "[&\\?]cmsgpassword=([a-zA-Z0-9]+)&?";

        /* compile regular expression */
        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            break;
        }
        
        /* this is the udl remainder in which we look */
        remain = strdup(buffer);
        
        /* find matches */
        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match */
        if (err == 0) {
          /* find password */
          if (matches[1].rm_so >= 0) {
             buffer[0] = 0;
             len = matches[1].rm_eo - matches[1].rm_so;
             strncat(buffer, remain+matches[1].rm_so, len);
             if (password != NULL) {
               *password = (char *) strdup(buffer);
             }        
/* printf("parseUDL: password = %s\n", buffer); */
          }
        }
        
        /* free up memory */
        cMsgRegfree(&compiled);
       
        /* find multicast timeout parameter if it exists */
        /* look for ?multicastTO=value& or &multicastTO=value& */
        pattern = "[&\\?]multicastTO=([0-9]+)?";

        /* compile regular expression */
        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
            break;
        }

        /* find matches */
        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match */
        if (err == 0) {
          /* find timeout */
          if (matches[1].rm_so >= 0) {
             buffer[0] = 0;
             len = matches[1].rm_eo - matches[1].rm_so;
             strncat(buffer, remain+matches[1].rm_so, len);
             if (timeout != NULL) {
               *timeout = atoi(buffer);
             }        
/* printf("parseUDL: timeout = %d seconds\n", atoi(buffer)); */
          }
        }
                
        /* free up memory */
        cMsgRegfree(&compiled);
       
        /* find regime parameter if it exists */
        /* look for ?regime=value& or &regime=value& */
        pattern = "[&\\?]regime=(low|high|medium)";

        /* compile regular expression */
        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED | REG_ICASE);
        if (err != 0) {
          break;
        }

        /* find matches */
        err = cMsgRegexec(&compiled, remain, 2, matches, 0);
        /* if match */
        if (err == 0) {
          /* find regime */
          if (matches[1].rm_so >= 0) {
            buffer[0] = 0;
            len = matches[1].rm_eo - matches[1].rm_so;
            strncat(buffer, remain+matches[1].rm_so, len);
            if (regime != NULL) {
              if (strcasecmp(buffer, "low") == 0) {
                *regime = CMSG_REGIME_LOW;
/*printf("parseUDL: regime = low\n");*/
              }
              else if (strcasecmp(buffer, "high") == 0) {
                *regime = CMSG_REGIME_HIGH;
/*printf("parseUDL: regime = high\n");*/
              }
              else {
                *regime = CMSG_REGIME_MEDIUM;
/*printf("parseUDL: regime = medium\n");*/
              }
            }
          }
        }
        
        /* free up memory */
        cMsgRegfree(&compiled);
                                
        free(remain);
        break;
    }
    
    /* UDL parsed ok */
/* printf("DONE PARSING UDL\n"); */
    free(udl);
    free(buffer);
    return(CMSG_OK);
}

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


/*-------------------------------------------------------------------*/
#ifdef Darwin
int clock_gettime(int clk_id /*ignored*/, struct timespec *tp)
{
  struct timeval now;
    
  int rv = gettimeofday(&now, NULL);
    
  if (rv != 0) {
    return rv;
  }
    
  tp->tv_sec = now.tv_sec;
  tp->tv_nsec = now.tv_usec * 1000;
    
  return 0;
}
#endif

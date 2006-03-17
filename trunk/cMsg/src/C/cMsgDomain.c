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
#include "cMsgBase.h"
#include "cMsgDomain.h"
#include "errors.h"
#include "rwlock.h"
#include "regex.h"



/* built-in limits */
/** Maximum number of domains for each client to connect to at once. */
#define MAXDOMAINS_CODA  100
/** Number of seconds to wait for cMsgClientListeningThread threads to start. */
#define WAIT_FOR_THREADS 10

/** Store information about each cMsg domain connected to. */
cMsgDomain_CODA cMsgDomains[MAXDOMAINS_CODA];

/* local variables */
/** Is the one-time initialization done? */
static int oneTimeInitialized = 0;
/** Pthread mutex to protect the local generation of unique numbers. */
static pthread_mutex_t generalMutex = PTHREAD_MUTEX_INITIALIZER;
/**
 * Read/write lock to prevent connect or disconnect from being
 * run simultaneously with any other function.
 */
static rwLock_t connectLock = RWL_INITIALIZER; 
/** Id number which uniquely defines a subject/type pair. */
static int subjectTypeId = 1;


/** Buffer for sending messages. */
/* static char *msgBuffer;*/
/** Size of buffer in bytes for sending messages. */
static int initialMsgBufferSize = 15000;


/* for c++ */
#ifdef __cplusplus
extern "C" {
#endif

/* Prototypes of the functions which implement the standard cMsg tasks in the cMsg domain. */
static int   cmsgd_Connect           (const char *myUDL, const char *myName, const char *myDescription,
                                    const char *UDLremainder,void **domainId);
static int   cmsgd_Send              (void *domainId, void *msg);
static int   cmsgd_SyncSend          (void *domainId, void *msg, int *response);
static int   cmsgd_Flush             (void *domainId);
static int   cmsgd_Subscribe         (void *domainId, const char *subject, const char *type, cMsgCallback *callback,
                                    void *userArg, cMsgSubscribeConfig *config);
static int   cmsgd_Unsubscribe       (void *domainId, const char *subject, const char *type, cMsgCallback *callback,
                                    void *userArg);
static int   cmsgd_SubscribeAndGet   (void *domainId, const char *subject, const char *type,
                                    const struct timespec *timeout, void **replyMsg);
static int   cmsgd_SendAndGet        (void *domainId, void *sendMsg, const struct timespec *timeout,
                                    void **replyMsg);
static int   cmsgd_Start             (void *domainId);
static int   cmsgd_Stop              (void *domainId);
static int   cmsgd_Disconnect        (void *domainId);
static int   cmsgd_SetShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg);
static int   cmsgd_ShutdownClients   (void *domainId, const char *client, int flag);
static int   cmsgd_ShutdownServers   (void *domainId, const char *server, int flag);


/** List of the functions which implement the standard cMsg tasks in the cMsg domain. */
static domainFunctions functions = {cmsgd_Connect, cmsgd_Send,
                                    cmsgd_SyncSend, cmsgd_Flush,
                                    cmsgd_Subscribe, cmsgd_Unsubscribe,
                                    cmsgd_SubscribeAndGet, cmsgd_SendAndGet,
                                    cmsgd_Start, cmsgd_Stop, cmsgd_Disconnect,
                                    cmsgd_ShutdownClients, cmsgd_ShutdownServers,
                                    cmsgd_SetShutdownHandler};

/* cMsg domain type */
domainTypeInfo cmsgDomainTypeInfo = {
  "cMsg",
  &functions
};


/** Function in cMsgServer.c which implements the network listening thread of a client. */
void *cMsgClientListeningThread(void *arg);


/* local prototypes */

/* mutexes and read/write locks */
static void  mutexLock(pthread_mutex_t *mutex);
static void  mutexUnlock(pthread_mutex_t *mutex);
static void  idMutexLock(void);
static void  idMutexUnlock(void);
static void  connectReadLock(void);
static void  connectReadUnlock(void);
static void  connectWriteLock(void);
static void  connectWriteUnlock(void);
static void  socketMutexLock(cMsgDomain_CODA *domain);
static void  socketMutexUnlock(cMsgDomain_CODA *domain);
static void  syncSendMutexLock(cMsgDomain_CODA *domain);
static void  syncSendMutexUnlock(cMsgDomain_CODA *domain);
static void  subscribeMutexLock(cMsgDomain_CODA *domain);
static void  subscribeMutexUnlock(cMsgDomain_CODA *domain);
static void  countDownLatchFree(countDownLatch *latch); 
static void  countDownLatchInit(countDownLatch *latch, int count, int reInit);
static void  latchReset(countDownLatch *latch, int count, const struct timespec *timeout);
static int   latchCountDown(countDownLatch *latch, const struct timespec *timeout);
static int   latchAwait(countDownLatch *latch, const struct timespec *timeout);

/* threads */
static void *keepAliveThread(void *arg);
static void *callbackThread(void *arg);
static void *supplementalThread(void *arg);

/* initialization and freeing */
static void  domainInit(cMsgDomain_CODA *domain, int reInit);
static void  domainFree(cMsgDomain_CODA *domain);  
static void  domainClear(cMsgDomain_CODA *domain);
static void  getInfoInit(getInfo *info, int reInit);
static void  subscribeInfoInit(subInfo *info, int reInit);
static void  getInfoFree(getInfo *info);
static void  subscribeInfoFree(subInfo *info);

/* failovers */
static int restoreSubscriptions(cMsgDomain_CODA *domain) ;
static int failoverSuccessful(cMsgDomain_CODA *domain, int waitForResubscribes);
static int resubscribe(cMsgDomain_CODA *domain, const char *subject, const char *type);

/* misc */
static int disconnectFromKeepAlive(void *domainId);
static int cmsgd_ConnectImpl(int domainId, int failoverIndex);
static int talkToNameServer(cMsgDomain_CODA *domain, int serverfd, int failoverIndex);
static int parseUDLregex(const char *UDL, char **password,
                              char **host, unsigned short *port,
                              char **UDLRemainder,
                              char **subdomainType,
                              char **UDLsubRemainder);
static int   parseUDL(const char *UDLremainder, char **host, unsigned short *port,
                      char **subdomainType, char **UDLsubRemainder);
static int   unSendAndGet(void *domainId, int id);
static int   unSubscribeAndGet(void *domainId, const char *subject,
                               const char *type, int id);
static int   getAbsoluteTime(const struct timespec *deltaTime, struct timespec *absTime);
static void  defaultShutdownHandler(void *userArg);

#ifdef VXWORKS
/** Implementation of strdup() to cover vxWorks operating system. */
static char *strdup(const char *s1) {
    char *s;    
    if (s1 == NULL) return NULL;    
    if ((s = (char *) malloc(strlen(s1)+1)) == NULL) return NULL;    
    return strcpy(s, s1);
}
#endif



/*-------------------------------------------------------------------*/
/**
 * This routine restores subscriptions to a new server which replaced a crashed server
 * during failover.
 *
 * @param domainId id of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 */
static int restoreSubscriptions(cMsgDomain_CODA *domain)  {
  int i, err;
  
  /*
   * We don't want any cMsg commands to be sent to the server
   * while we are busy resubscribing to a failover server.
   */
  connectWriteLock();  

  /* for each client subscription ... */
  for (i=0; i<MAX_SUBSCRIBE; i++) {

    /* if subscription not active, forget about it */
    if (domain->subscribeInfo[i].active != 1) {
      continue;
    }
/* printf("Restore Subscription to sub = %s, type = %s\n",
                              domain->subscribeInfo[i].subject,
                              domain->subscribeInfo[i].type); */
                              
    err = resubscribe(domain, domain->subscribeInfo[i].subject,
                              domain->subscribeInfo[i].type);
    
    if (err != CMSG_OK) {
        connectWriteUnlock();  
        return(err);
    }        
  }
  
  connectWriteUnlock();  

  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/


/**
 * This routine waits a while for a possible failover to a new cMsg server 
 * before attempting to complete an interrupted command to the server or
 * before returning an error.
 *
 * @returns 1 if there is a connection to a cMsg server in 3 seconds or 0 if not 
 */
static int failoverSuccessful(cMsgDomain_CODA *domain, int waitForResubscribes) {
    int i, err;
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
    
    err = latchAwait(&domain->failoverLatch, &wait);
/* printf("IN failoverSuccessful, latchAwait return = %d\n", err); */
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
 * This routine is called once to connect to a cMsg domain. It is called
 * by the user through top-level cMsg API, "cMsgConnect()".
 * The argument "myUDL" is the Universal Domain Locator (or can be a semicolon
 * separated list of UDLs) used to uniquely identify the cMsg server to connect to.
 * It has the form:<p>
 *       <b>cMsg:cMsg://host:port/subdomainType/namespace/?cmsgpassword=<password>& ... </b><p>
 * where the first "cMsg:" is optional. The subdomain is optional with
 * the default being cMsg (if nothing follows the host & port).
 * If the namespace is given, the subdomainType must be specified as well.
 * If the name server requires a password to connect, this can be specified by
 * ?cmsgpassword=<password> immediately after the namespace. It may also be 
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
 * to the name server is done in "cmsgd_ConnectImpl".
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
 * @returns CMSG_BAD_ARGUMENT if the cMsg domain specific part of the UDL is NULL,
 *                            or the host name of the server to connect to is bad,
 * @returns CMSG_BAD_FORMAT if the UDL is malformed
 * @returns CMSG_OUT_OF_RANGE if the port specified in the UDL is out-of-range
 * @returns CMSG_OUT_OF_MEMOERY if the allocating memory for message buffer failed
 * @returns CMSG_LIMIT_EXCEEDED if the maximum number of domain connections has
 *          been exceeded
 * @returns CMSG_SOCKET_ERROR if the listening thread finds all the ports it tries
 *                            to listen on are busy, or socket options could not be
 *                            set
 * @returns CMSG_NETWORK_ERROR if no connection to the name or domain servers can be made,
 *                             or a communication error with either server occurs.
 */   
static int cmsgd_Connect(const char *myUDL, const char *myName, const char *myDescription,
                        const char *UDLremainder, void **domainId) {
        
  char *p, *udl;
  int failoverUDLCount = 0, failoverIndex=0, viableUDLs = 0;
  int gotConnection = 0;        
  int i, err, id=-1;
  char temp[CMSG_MAXHOSTNAMELEN];


  /* First, grab lock for thread safety. This lock must be held until
   * the initialization is completely finished. Otherwise, if we set
   * initComplete = 1 (so that we reserve space in the cMsgDomains array)
   * before it's finished and then release the lock, we may give an
   * "existing" connection to a user who does a second init
   * when in fact, an error may still occur in that "existing"
   * connection. Hope you caught that.
   */
  connectWriteLock();  

  /* do one time initialization */
  if (!oneTimeInitialized) {
    /* clear domain arrays */
    for (i=0; i<MAXDOMAINS_CODA; i++) {
      domainInit(&cMsgDomains[i], 0);
    }

    oneTimeInitialized = 1;
  }


  /* find the first available place in the "cMsgDomains" array */
  for (i=0; i<MAXDOMAINS_CODA; i++) {
    if (cMsgDomains[i].initComplete > 0) {
      continue;
    }
    domainClear(&cMsgDomains[i]);
    id = i;
    break;
  }


  /* exceeds number of domain connections allowed */
  if (id < 0) {
    connectWriteUnlock();
    return(CMSG_LIMIT_EXCEEDED);
  }

  /* allocate memory for message-sending buffer */
  cMsgDomains[id].msgBuffer     = (char *) malloc(initialMsgBufferSize);
  cMsgDomains[id].msgBufferSize = initialMsgBufferSize;
  if (cMsgDomains[id].msgBuffer == NULL) {
    domainClear(&cMsgDomains[id]);
    connectWriteUnlock();
    return(CMSG_OUT_OF_MEMORY);
  }

  /* reserve this element of the "cMsgDomains" array */
  cMsgDomains[id].initComplete = 1;

  /* save ref to self */
  cMsgDomains[id].id = id;

  /* store our host's name */
  gethostname(temp, CMSG_MAXHOSTNAMELEN);
  cMsgDomains[id].myHost = (char *) strdup(temp);

  /* store names, can be changed until server connection established */
  cMsgDomains[id].name        = (char *) strdup(myName);
  cMsgDomains[id].udl         = (char *) strdup(myUDL);
  cMsgDomains[id].description = (char *) strdup(myDescription);

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

  if (failoverUDLCount < 1) {
    domainClear(&cMsgDomains[id]);
    connectWriteUnlock();
    return(CMSG_ERROR);        
  }

  /* Now that we know how many UDLs there are, allocate array. */
  cMsgDomains[id].failoverSize = failoverUDLCount;
  cMsgDomains[id].failovers = (parsedUDL *) calloc(failoverUDLCount, sizeof(parsedUDL));
  if (cMsgDomains[id].failovers == NULL) {
    domainClear(&cMsgDomains[id]);
    connectWriteUnlock();
    return(CMSG_OUT_OF_MEMORY);
  }

  /* On second pass, stored parsed UDLs. */
  udl = (char *)strdup(myUDL);        
  p   = strtok(udl, ";");
  i   = 0;
  while (p != NULL) {
    /* Parse the UDL (Uniform Domain Locator) */
    if ( (err = parseUDLregex(p, &cMsgDomains[id].failovers[i].password,
                                 &cMsgDomains[id].failovers[i].nameServerHost,
                                 &cMsgDomains[id].failovers[i].nameServerPort,
                                 &cMsgDomains[id].failovers[i].udlRemainder,
                                 &cMsgDomains[id].failovers[i].subdomain,
                                 &cMsgDomains[id].failovers[i].subRemainder)) != CMSG_OK ) {

      /* There's been a parsing error, mark as invalid UDL */
      cMsgDomains[id].failovers[i].valid = 0;
    }
    else {
      cMsgDomains[id].failovers[i].valid = 1;
      viableUDLs++;
    }
    cMsgDomains[id].failovers[i].udl = strdup(p);
/* printf("Found UDL = %s\n", cMsgDomains[id].failovers[i].udl); */
    p = strtok(NULL, ";");
    i++;
  }
  free(udl);


  /*-------------------------*/
  /* Make a real connection. */
  /*-------------------------*/

  /* If there's only 1 viable UDL ... */
  if (viableUDLs < 2) {
/* printf("Only 1 UDL = %s\n", cMsgDomains[id].failovers[0].udl); */

      /* Ain't using failovers */
      cMsgDomains[id].implementFailovers = 0;
      
      /* connect using that UDL */
      if (!cMsgDomains[id].failovers[0].valid) {
          domainClear(&cMsgDomains[id]);
          connectWriteUnlock();
          return(CMSG_ERROR);            
      }
      
      err = cmsgd_ConnectImpl(id, 0);
      if (err != CMSG_OK) {
          domainClear(&cMsgDomains[id]);
          connectWriteUnlock();
          return(err);            
      }
  }
  else {
    int connectFailures = 0;

    /* We're using failovers */
    cMsgDomains[id].implementFailovers = 1;
    
    /* Go through the UDL's until one works */
    failoverIndex = -1;
    do {
      /* check to see if UDL valid for cMsg domain */
      if (!cMsgDomains[id].failovers[++failoverIndex].valid) {
        connectFailures++;
        continue;
      }

      /* connect using that UDL info */
/* printf("\nTrying to connect with UDL = %s\n",
      cMsgDomains[id].failovers[failoverIndex].udl); */

      err = cmsgd_ConnectImpl(id, failoverIndex);
      if (err == CMSG_OK) {
        cMsgDomains[id].failoverIndex = failoverIndex;
        gotConnection = 1;
/* printf("Connected!!\n"); */
        break;
      }

      connectFailures++;

    } while (connectFailures < failoverUDLCount);

    if (!gotConnection) {
      domainClear(&cMsgDomains[id]);
      connectWriteUnlock();
      return(CMSG_ERROR);                      
    }        
  }

  /* init is complete */
  *domainId = (void *)id;

  /* install default shutdown handler (exits program) */
  cmsgd_SetShutdownHandler((void *)id, defaultShutdownHandler, NULL);

  cMsgDomains[id].gotConnection = 1;

  /* no more mutex protection is necessary */
  connectWriteUnlock();

  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/


/**
 * This routine is called by cmsgd_Connect and does the real work of
 * connecting to the cMsg name server.
 * 
 * @param domainId pointer to integer which gets filled with a unique id referring
 *        to this connection.
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_OUT_OF_MEMOERY if the allocating memory for message buffer failed
 * @returns CMSG_SOCKET_ERROR if the listening thread finds all the ports it tries
 *                            to listen on are busy, or socket options could not be
 *                            set
 * @returns CMSG_NETWORK_ERROR if no connection to the name or domain servers can be made,
 *                             or a communication error with either server occurs.
 */   
static int cmsgd_ConnectImpl(int domainId, int failoverIndex) {

  int i, id=-1, err, serverfd, status, hz, num_try, try_max;
  char *portEnvVariable=NULL, temp[CMSG_MAXHOSTNAMELEN];
  unsigned short startingPort;
  mainThreadInfo *threadArg;
  struct timespec waitForThread;
  
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  parsedUDL *myParsedUDL  = &domain->failovers[failoverIndex];
  
  id = domainId;    
  /*
   * First find a port on which to receive incoming messages.
   * Do this by trying to open a listening socket at a given
   * port number. If that doesn't work add one to port number
   * and try again.
   * 
   * But before that, define a port number from which to start looking.
   * If CMSG_PORT is defined, it's the starting port number.
   * If CMSG_PORT is NOT defind, start at CMSG_CLIENT_LISTENING_PORT (2345).
   */

  /* pick starting port number */
  if ( (portEnvVariable = getenv("CMSG_PORT")) == NULL ) {
    startingPort = CMSG_CLIENT_LISTENING_PORT;
    if (cMsgDebug >= CMSG_DEBUG_WARN) {
      fprintf(stderr, "cmsgd_ConnectImpl: cannot find CMSG_PORT env variable, first try port %hu\n", startingPort);
    }
  }
  else {
    i = atoi(portEnvVariable);
    if (i < 1025 || i > 65535) {
      startingPort = CMSG_CLIENT_LISTENING_PORT;
      if (cMsgDebug >= CMSG_DEBUG_WARN) {
        fprintf(stderr, "cmsgd_ConnectImpl: CMSG_PORT contains a bad port #, first try port %hu\n", startingPort);
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
  threadArg = (mainThreadInfo *) malloc(sizeof(mainThreadInfo));
  if (threadArg == NULL) {
      return(CMSG_OUT_OF_MEMORY);  
  }
  threadArg->isRunning = 0;
  threadArg->domainId  = domainId;
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
   * the name server since the server tries to communicate with
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
      fprintf(stderr, "cmsgd_ConnectImpl, cannot start listening thread\n");
    }
    exit(-1);
  }
       
  /* free mem allocated for the argument passed to listening thread */
  free(threadArg);
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_ConnectImpl: created listening thread\n");
  }
  
  /*---------------------------------------------------------------*/
  /* connect & talk to cMsg name server to check if name is unique */
  /*---------------------------------------------------------------*/
    
  /* first connect to server host & port */
  if ( (err = cMsgTcpConnect(domain->failovers[failoverIndex].nameServerHost,
                             domain->failovers[failoverIndex].nameServerPort,
                             &serverfd)) != CMSG_OK) {
    /* stop listening & connection threads */
    pthread_cancel(domain->pendThread);
    return(err);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_ConnectImpl: connected to name server\n");
  }
  
  /* get host & port (domain->sendHost,sendPort) to send messages to */
  err = talkToNameServer(domain, serverfd, failoverIndex);
  if (err != CMSG_OK) {
    close(serverfd);
    pthread_cancel(domain->pendThread);
    return(err);
  }
  
  /* BUGBUG free up memory allocated in parseUDL & no longer needed */

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_ConnectImpl: got host and port from name server\n");
  }
  
  /* done talking to server */
  close(serverfd);
 
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_ConnectImpl: closed name server socket\n");
    fprintf(stderr, "cmsgd_ConnectImpl: sendHost = %s, sendPort = %hu\n",
                             domain->sendHost,
                             domain->sendPort);
  }
  
  /* create receiving socket and store */
  if ( (err = cMsgTcpConnect(domain->sendHost,
                             domain->sendPort,
                             &domain->receiveSocket)) != CMSG_OK) {
    pthread_cancel(domain->pendThread);
    return(err);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_ConnectImpl: created receiving socket fd = %d\n", domain->receiveSocket);
  }
    
  /* create keep alive socket and store */
  if ( (err = cMsgTcpConnect(domain->sendHost,
                             domain->sendPort,
                             &domain->keepAliveSocket)) != CMSG_OK) {
    close(domain->receiveSocket);
    pthread_cancel(domain->pendThread);
    return(err);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_ConnectImpl: created keepalive socket fd = %d\n",domain->keepAliveSocket );
  }
  
  /* create thread to send periodic keep alives and handle dead server */
  status = pthread_create(&domain->keepAliveThread, NULL,
                          keepAliveThread, (void *)domain);
  if (status != 0) {
    err_abort(status, "Creating keep alive thread");
  }
     
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_ConnectImpl: created keep alive thread\n");
  }

  /* create sending socket and store */
  if ( (err = cMsgTcpConnect(domain->sendHost,
                             domain->sendPort,
                             &domain->sendSocket)) != CMSG_OK) {
    close(domain->keepAliveSocket);
    close(domain->receiveSocket);
    pthread_cancel(domain->pendThread);
    return(err);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_ConnectImpl: created sending socket fd = %d\n", domain->sendSocket);
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine is called by cmsgd_Connect and does the real work of
 * connecting to the cMsg name server.
 * 
 * @param domainId pointer to integer which gets filled with a unique id referring
 *        to this connection.
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_OUT_OF_MEMOERY if the allocating memory for message buffer failed
 * @returns CMSG_SOCKET_ERROR if the listening thread finds all the ports it tries
 *                            to listen on are busy, or socket options could not be
 *                            set
 * @returns CMSG_NETWORK_ERROR if no connection to the name or domain servers can be made,
 *                             or a communication error with either server occurs.
 */   
static int reconnect(int domainId, int failoverIndex) {

  int i, id=-1, err, serverfd, status, hz, num_try, try_max;
  char *portEnvVariable=NULL, temp[CMSG_MAXHOSTNAMELEN];
  unsigned short startingPort;
  mainThreadInfo *threadArg;
  struct timespec waitForThread;
  getInfo *info;
  
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  parsedUDL *myParsedUDL  = &domain->failovers[failoverIndex];
  
  id = domainId;    

  
  connectWriteLock();  

  /*--------------------------------------------------------------------*/
  /* Connect to cMsg name server to check if server can be connected to.
   * If not, don't waste any more time and try the next one.            */
  /*--------------------------------------------------------------------*/
  /* connect to server host & port */
  if ( (err = cMsgTcpConnect(domain->failovers[failoverIndex].nameServerHost,
                             domain->failovers[failoverIndex].nameServerPort,
                             &serverfd)) != CMSG_OK) {
    connectWriteUnlock();
    return(err);
  }  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_ConnectImpl: connected to name server\n");
  }

  /* The thread listening for TCP connections needs to keep running.
   * Keep all existing callback threads for the subscribes. */

  /* wakeup all sendAndGets - they can't be saved */
  for (i=0; i<MAX_SEND_AND_GET; i++) {
    
      info = &domain->sendAndGetInfo[i];

      if (info->active != 1) {
        continue;
      }
    
      info->msg = NULL;
      info->msgIn = 1;
      info->quit  = 1;
      info->error = CMSG_SERVER_DIED;

      /* wakeup the sendAndGet */
      status = pthread_cond_signal(&info->cond);
      if (status != 0) {
        err_abort(status, "Failed get condition signal");
      }
  }


  /* wakeup all existing subscribeAndGets - they can't be saved */
  for (i=0; i<MAX_SUBSCRIBE_AND_GET; i++) {
    
      info = &domain->subscribeAndGetInfo[i];

      if (info->active != 1) {
        continue;
      }

      info->msg = NULL;
      info->msgIn = 1;
      info->quit  = 1;
      info->error = CMSG_SERVER_DIED;

      /* wakeup the subscribeAndGet */      
      status = pthread_cond_signal(&info->cond);
      if (status != 0) {
        err_abort(status, "Failed get condition signal");
      }
      
  }           

  /* shutdown extra listening threads, do nothing if there's an error */
  
  /* First, cancel the keepAlive responding thread. */
  pthread_cancel(domain->clientThread[1]);
  
  /* Second, GRACEFULLY shutdown the thread which receives messages.
   * This thread may be cancelled only if it's not blocked in a
   * pthread_cond_wait (which it will be if any callback's cue is
   * full and cMsgRunCallbacks tries to add another.
   * To shut it down correctly, set a flag and wake up it in case 
   * it's waiting. Give it 1/2 second and then just cancel it.
   * It is somewhat of a brute force method but it should work.
   */
  domain->killClientThread = 1;
  status = pthread_cond_signal(&domain->subscribeCond);
  if (status != 0) {
    err_abort(status, "Failed callback condition signal");
  }
  usleep(500000);
  pthread_cancel(domain->clientThread[0]);
  domain->killClientThread = 0;
  
  
  /*-----------------------------------------------------*/
  /* talk to cMsg name server to check if name is unique */
  /*-----------------------------------------------------*/
  /* get host & port (domain->sendHost,sendPort) to send messages to */
  err = talkToNameServer(domain, serverfd, failoverIndex);
  if (err != CMSG_OK) {
    connectWriteUnlock();
    close(serverfd);
    return(err);
  }
  
/* BUGBUG free up memory allocated in parseUDL & no longer needed */

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_ConnectImpl: got host and port from name server\n");
  }
  
  /* done talking to server */
  close(serverfd);
 
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_ConnectImpl: closed name server socket\n");
    fprintf(stderr, "cmsgd_ConnectImpl: sendHost = %s, sendPort = %hu\n",
                             domain->sendHost,
                             domain->sendPort);
  }
/* printf("reconnect 4\n"); */
  
  /* create receiving socket and store */
  if ( (err = cMsgTcpConnect(domain->sendHost,
                             domain->sendPort,
                             &domain->receiveSocket)) != CMSG_OK) {
    connectWriteUnlock();
    return(err);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_ConnectImpl: created receiving socket fd = %d\n", domain->receiveSocket);
  }
    
/* printf("reconnect 5\n"); */
  /* create keep alive socket and store */
  if ( (err = cMsgTcpConnect(domain->sendHost,
                             domain->sendPort,
                             &domain->keepAliveSocket)) != CMSG_OK) {
    connectWriteUnlock();
    close(domain->receiveSocket);
    return(err);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_ConnectImpl: created keepalive socket fd = %d\n",domain->keepAliveSocket );
  }
/* printf("reconnect 6\n"); */
  
  /* Do not create another keepalive thread as we already got one.
   * But we gotta use the new socket as the old socket is to the
   * old server which is gone now.
   */

  /* create sending socket and store */
  if ( (err = cMsgTcpConnect(domain->sendHost,
                             domain->sendPort,
                             &domain->sendSocket)) != CMSG_OK) {
    connectWriteUnlock();
    close(domain->keepAliveSocket);
    close(domain->receiveSocket);
    return(err);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_ConnectImpl: created sending socket fd = %d\n", domain->sendSocket);
  }
  
  connectWriteUnlock();
  
/* printf("reconnect END\n"); */
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine sends a msg to the specified cMsg domain server.  It is called
 * by the user through cMsgSend() given the appropriate UDL. It is completely
 * asynchronous and never blocks. In this domain cMsgFlush() does nothing and
 * does not need to be called for the message to be sent immediately.<p>
 * This version of this routine uses writev to write all data in one write call.
 * Another version was tried with many writes (one for ints and one for each
 * string), but the performance died sharply
 *
 * @param domainId id of the domain connection
 * @param vmsg pointer to a message structure
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_OUT_OF_MEMOERY if the allocating memory for message buffer failed
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement sending
 *                               messages
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
static int cmsgd_Send(void *domainId, void *vmsg) {
  
  int i, err, len, lenSubject, lenType, lenCreator, lenText, lenByteArray;
  int highInt, lowInt, outGoing[16];
  cMsgMessage *msg = (cMsgMessage *) vmsg;
  cMsgDomain_CODA *domain = &cMsgDomains[(int)domainId];
  int fd = domain->sendSocket;
  char *creator;
  long long llTime;
  struct timespec now;
  int status;
  static int zeroStart = 0;
  
  if (!domain->hasSend) {
    return(CMSG_NOT_IMPLEMENTED);
  }
  
  tryagain:
  while (1) {
    err = CMSG_OK;
    
    /* Cannot run this while connecting/disconnecting */
    connectReadLock();

    if (domain->initComplete != 1) {
      connectReadUnlock();
      return(CMSG_NOT_INITIALIZED);
    }
    if (domain->gotConnection != 1) {
      connectReadUnlock();
      err = CMSG_LOST_CONNECTION;
      break;
    }

    if (msg->text == NULL) {
      lenText = 0;
    }
    else {
      lenText = strlen(msg->text);
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
    llTime  = ((long long)now.tv_sec * 1000) + ((long long)now.tv_nsec/1000000);
    highInt = (int) ((llTime >> 32) & 0x00000000FFFFFFFF);
    lowInt  = (int) (llTime & 0x00000000FFFFFFFF);
    outGoing[7] = htonl(highInt);
    outGoing[8] = htonl(lowInt);

    /* user time */
    llTime  = ((long long)msg->userTime.tv_sec * 1000) +
              ((long long)msg->userTime.tv_nsec/1000000);
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

    /* send creator (this sender's name if msg created here) */
    creator = msg->creator;
    if (creator == NULL) creator = domain->name;
    /* length of "creator" string */
    lenCreator   = strlen(creator);
    outGoing[13] = htonl(lenCreator);

    /* length of "text" string */
    outGoing[14] = htonl(lenText);

    /* length of byte array */
    lenByteArray = msg->byteArrayLength;
    outGoing[15] = htonl(lenByteArray);

    /* total length of message (minus first int) is first item sent */
    len = sizeof(outGoing) - sizeof(int) + lenSubject + lenType +
          lenCreator + lenText + lenByteArray;
    outGoing[0] = htonl(len);

    /* make send socket communications thread-safe */
    socketMutexLock(domain);

    /* allocate more memory for message-sending buffer if necessary */
    if (domain->msgBufferSize < (int)(len+sizeof(int))) {
      free(domain->msgBuffer);
      domain->msgBufferSize = len + 1004; /* give us 1kB extra */
      domain->msgBuffer = (char *) malloc(domain->msgBufferSize);
      if (domain->msgBuffer == NULL) {
        socketMutexUnlock(domain);
        connectReadUnlock();
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
    memcpy(domain->msgBuffer+len, (void *)creator, lenCreator);
    len += lenCreator;
    memcpy(domain->msgBuffer+len, (void *)msg->text, lenText);
    len += lenText;
    memcpy(domain->msgBuffer+len, (void *)&((msg->byteArray)[msg->byteArrayOffset]), lenByteArray);
    len += lenByteArray;   

    /* send data over socket */
    if (cMsgTcpWrite(fd, (void *) domain->msgBuffer, len) != len) {
      socketMutexUnlock(domain);
      connectReadUnlock();
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cmsgd_Send: write failure\n");
      }
      err = CMSG_NETWORK_ERROR;
      break;
    }

    /* done protecting communications */
    socketMutexUnlock(domain);
    connectReadUnlock();
    break;

  }
  
  if (err!= CMSG_OK) {
    /* don't wait for resubscribes */
    if (failoverSuccessful(domain, 0)) {
       fd = domain->sendSocket;
       printf("cmsgd_Send: FAILOVER SUCCESSFUL, try send again\n");
       goto tryagain;
    }  
printf("cmsgd_Send: FAILOVER NOT successful, quitting, err = %d\n", err);
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
 * @param response integer pointer that gets filled with the server's response
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement the
 *                               synchronous sending of messages
 * @returns CMSG_OUT_OF_MEMOERY if the allocating memory for message buffer failed
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
static int cmsgd_SyncSend(void *domainId, void *vmsg, int *response) {
  
  int err, len, lenSubject, lenType, lenCreator, lenText, lenByteArray;
  int highInt, lowInt, outGoing[16];
  cMsgMessage *msg = (cMsgMessage *) vmsg;
  cMsgDomain_CODA *domain = &cMsgDomains[(int)domainId];
  int fd = domain->sendSocket;
  int fdIn = domain->receiveSocket;
  char *creator;
  long long llTime;
  struct timespec now;
    
  if (!domain->hasSyncSend) {
    return(CMSG_NOT_IMPLEMENTED);
  }
 
  tryagain:
  while (1) {
    err = CMSG_OK;

    /* Cannot run this while connecting/disconnecting */
    connectReadLock();

    if (domain->initComplete != 1) {
      connectReadUnlock();
      return(CMSG_NOT_INITIALIZED);
    }
    if (domain->gotConnection != 1) {
      connectReadUnlock();
      err = CMSG_LOST_CONNECTION;
      break;
    }

    if (msg->text == NULL) {
      lenText = 0;
    }
    else {
      lenText = strlen(msg->text);
    }

    /* message id (in network byte order) to domain server */
    outGoing[1] = htonl(CMSG_SYNC_SEND_REQUEST);
    /* reserved */
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
    llTime  = ((long long)now.tv_sec * 1000) + ((long long)now.tv_nsec/1000000);
    highInt = (int) ((llTime >> 32) & 0x00000000FFFFFFFF);
    lowInt  = (int) (llTime & 0x00000000FFFFFFFF);
    outGoing[7] = htonl(highInt);
    outGoing[8] = htonl(lowInt);

    /* user time */
    llTime  = ((long long)msg->userTime.tv_sec * 1000) +
              ((long long)msg->userTime.tv_nsec/1000000);
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

    /* send creator (this sender's name if msg created here) */
    creator = msg->creator;
    if (creator == NULL) creator = domain->name;
    /* length of "creator" string */
    lenCreator   = strlen(creator);
    outGoing[13] = htonl(lenCreator);

    /* length of "text" string */
    outGoing[14] = htonl(lenText);

    /* length of byte array */
    lenByteArray = msg->byteArrayLength;
    outGoing[15] = htonl(lenByteArray);

    /* total length of message (minus first int) is first item sent */
    len = sizeof(outGoing) - sizeof(int) + lenSubject + lenType +
          lenCreator + lenText + lenByteArray;
    outGoing[0] = htonl(len);

    /* make syncSends be synchronous 'cause we need a reply */
    syncSendMutexLock(domain);

    /* make send socket communications thread-safe */
    socketMutexLock(domain);

    /* allocate more memory for message-sending buffer if necessary */
    if (domain->msgBufferSize < (int)(len+sizeof(int))) {
      free(domain->msgBuffer);
      domain->msgBufferSize = len + 1004; /* give us 1kB extra */
      domain->msgBuffer = (char *) malloc(domain->msgBufferSize);
      if (domain->msgBuffer == NULL) {
        socketMutexUnlock(domain);
        syncSendMutexUnlock(domain);
        connectReadUnlock();
        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cmsgd_SyncSend: out of memory\n");
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
    memcpy(domain->msgBuffer+len, (void *)creator, lenCreator);
    len += lenCreator;
    memcpy(domain->msgBuffer+len, (void *)msg->text, lenText);
    len += lenText;
    memcpy(domain->msgBuffer+len, (void *)&((msg->byteArray)[msg->byteArrayOffset]), lenByteArray);
    len += lenByteArray;   

    /* send data over socket */
    if (cMsgTcpWrite(fd, (void *) domain->msgBuffer, len) != len) {
      socketMutexUnlock(domain);
      syncSendMutexUnlock(domain);
      connectReadUnlock();
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cmsgd_SyncSend: write failure\n");
      }
      err = CMSG_NETWORK_ERROR;
      break;
    }

    /* done protecting outgoing communications */
    socketMutexUnlock(domain);

    /* now read reply */
    if (cMsgTcpRead(fdIn, (void *) &err, sizeof(err)) != sizeof(err)) {
      syncSendMutexUnlock(domain);
      connectReadUnlock();
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cmsgd_SyncSend: read failure\n");
      }
      err = CMSG_NETWORK_ERROR;
      break;
    }

    syncSendMutexUnlock(domain);
    connectReadUnlock();
    break;
  }
  
  
  if (err!= CMSG_OK) {
    /* don't wait for resubscribes */
    if (failoverSuccessful(domain, 0)) {
       fd = domain->sendSocket;
       printf("cmsgd_SyncSend: FAILOVER SUCCESSFUL, try suncSend again\n");
       goto tryagain;
    }  
printf("cmsgd_SyncSend: FAILOVER NOT successful, quitting, err = %d\n", err);
  }

  /* return domain server's reply */  
  *response = ntohl(err);  
  
  return(CMSG_OK);
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
 * @returns CMSG_TIMEOUT if routine received no message in the specified time
 * @returns CMSG_OUT_OF_MEMORY if all available subscription memory has been used
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement
 *                               subscribeAndGet
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
static int cmsgd_SubscribeAndGet(void *domainId, const char *subject, const char *type,
                           const struct timespec *timeout, void **replyMsg) {
                             
  cMsgDomain_CODA *domain = &cMsgDomains[(int)domainId];
  int i, err, uniqueId, status, len, lenSubject, lenType;
  int gotSpot, fd = domain->sendSocket;
  int outGoing[6];
  getInfo *info = NULL;
  struct timespec wait;
  struct iovec iov[3];
  
  if (!domain->hasSubscribeAndGet) {
    return(CMSG_NOT_IMPLEMENTED);
  }   
           
  connectReadLock();

  if (domain->initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (domain->gotConnection != 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
  }
  
  /*
   * Pick a unique identifier for the subject/type pair, and
   * send it to the domain server & remember it for future use
   * Mutex protect this operation as many cmsgd_Connect calls may
   * operate in parallel on this static variable.
   */
  idMutexLock();
  uniqueId = subjectTypeId++;
  idMutexUnlock();

  /* make new entry and notify server */
  gotSpot = 0;

  for (i=0; i<MAX_SUBSCRIBE_AND_GET; i++) {
    if (domain->subscribeAndGetInfo[i].active != 0) {
      continue;
    }

    info = &domain->subscribeAndGetInfo[i];
    info->id      = uniqueId;
    info->active  = 1;
    info->error   = CMSG_OK;
    info->msgIn   = 0;
    info->quit    = 0;
    info->msg     = NULL;
    info->subject = (char *) strdup(subject);
    info->type    = (char *) strdup(type);
    gotSpot = 1;
    break;
  } 

  if (!gotSpot) {
    connectReadUnlock();
    free(info->subject);
    free(info->type);
    info->subject = NULL;
    info->type    = NULL;
    info->active  = 0;
    return(CMSG_OUT_OF_MEMORY);
  }
  
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
  socketMutexLock(domain);
  
  if (cMsgTcpWritev(fd, iov, 3, 16) == -1) {
    socketMutexUnlock(domain);
    connectReadUnlock();
    free(info->subject);
    free(info->type);
    info->subject = NULL;
    info->type    = NULL;
    info->active  = 0;
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "get: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* done protecting communications */
  socketMutexUnlock(domain);
  connectReadUnlock();
  
  /* Now ..., wait for asynchronous response */
  
  /* lock mutex */
  status = pthread_mutex_lock(&info->mutex);
  if (status != 0) {
    err_abort(status, "Failed callback mutex lock");
  }

  /* wait while there is no message */
  while (info->msgIn == 0) {
    /* wait until signaled */
    if (timeout == NULL) {
      status = pthread_cond_wait(&info->cond, &info->mutex);
    }
    /* wait until signaled or timeout */
    else {
      getAbsoluteTime(timeout, &wait);
      status = pthread_cond_timedwait(&info->cond, &info->mutex, &wait);
    }
    
    if (status == ETIMEDOUT) {
      break;
    }
    else if (status != 0) {
      err_abort(status, "Failed callback cond wait");
    }

    /* quit if commanded to */
    if (info->quit) {
      break;
    }
  }

  /* unlock mutex */
  status = pthread_mutex_unlock(&info->mutex);
  if (status != 0) {
    err_abort(status, "Failed callback mutex unlock");
  }

  /* If we timed out, tell server to forget the get. */
  if (info->msgIn == 0) {
      /*printf("get: timed out\n");*/
      
      /* remove the get from server */
      unSubscribeAndGet(domainId, subject, type, uniqueId);
      *replyMsg = NULL;
      err = CMSG_TIMEOUT;
  }
  /* If we've been woken up with an error condition ... */
  else if (info->error != CMSG_OK) {
      *replyMsg = NULL;
      err = info->error;    
  }
  /* If we did not timeout and everything's OK */
  else {
      /*
       * Don't need to make a copy of message as only 1 receipient.
       * Message was allocated in client's listening thread and user
       * must free it.
       */
      *replyMsg = info->msg;
      err = CMSG_OK;
  }
  
  /* free up memory */
  free(info->subject);
  free(info->type);
  info->subject = NULL;
  info->type    = NULL;
  info->msg     = NULL;
  info->active  = 0;

  /*printf("get: SUCCESS!!!\n");*/

  return(err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine tells the cMsg server to "forget" about the cMsgSubscribeAndGet()
 * call (specified by the id argument) since a timeout occurred.
 */   
static int unSubscribeAndGet(void *domainId, const char *subject, const char *type, int id) {
  
  int len, outGoing[6], lenSubject, lenType;
  cMsgDomain_CODA *domain = &cMsgDomains[(int)domainId];
  int fd = domain->sendSocket;
  struct iovec iov[3];

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
  socketMutexLock(domain);

  if (cMsgTcpWritev(fd, iov, 3, 16) == -1) {
    socketMutexUnlock(domain);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "unSubscribeAndGet: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  socketMutexUnlock(domain);

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
 * @returns CMSG_TIMEOUT if routine received no message in the specified time
 * @returns CMSG_OUT_OF_MEMORY if all available sendAndGet memory has been used
 *                             or allocating memory for message buffer failed
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement
 *                               sendAndGet
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
static int cmsgd_SendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout,
                      void **replyMsg) {
  
  cMsgDomain_CODA *domain  = &cMsgDomains[(int)domainId];
  cMsgMessage *msg = (cMsgMessage *) sendMsg;
  int i, err, uniqueId, status;
  int len, lenSubject, lenType, lenCreator, lenText, lenByteArray;
  int gotSpot, fd = domain->sendSocket;
  int highInt, lowInt, outGoing[16];
  getInfo *info = NULL;
  struct timespec wait;
  char *creator;
  long long llTime;
  struct timespec now;
  
  if (!domain->hasSendAndGet) {
    return(CMSG_NOT_IMPLEMENTED);
  }   
           
  connectReadLock();

  if (domain->initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (domain->gotConnection != 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
  }
 
  if (msg->text == NULL) {
    lenText = 0;
  }
  else {
    lenText = strlen(msg->text);
  }
  
  /*
   * Pick a unique identifier for the subject/type pair, and
   * send it to the domain server & remember it for future use
   * Mutex protect this operation as many cmsgd_Connect calls may
   * operate in parallel on this static variable.
   */
  idMutexLock();
  uniqueId = subjectTypeId++;
  idMutexUnlock();

  /* make new entry and notify server */
  gotSpot = 0;

  for (i=0; i<MAX_SEND_AND_GET; i++) {
    if (domain->sendAndGetInfo[i].active != 0) {
      continue;
    }

    info = &domain->sendAndGetInfo[i];
    info->id      = uniqueId;
    info->active  = 1;
    info->error   = CMSG_OK;
    info->msgIn   = 0;
    info->quit    = 0;
    info->msg     = NULL;
    info->subject = (char *) strdup(msg->subject);
    info->type    = (char *) strdup(msg->type);
    gotSpot = 1;
    break;
  }

  if (!gotSpot) {
    connectReadUnlock();
    /* free up memory */
    free(info->subject);
    free(info->type);
    info->subject = NULL;
    info->type    = NULL;
    info->active  = 0;
    return(CMSG_OUT_OF_MEMORY);
  }
  
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
  llTime  = ((long long)now.tv_sec * 1000) + ((long long)now.tv_nsec/1000000);
  highInt = (int) ((llTime >> 32) & 0x00000000FFFFFFFF);
  lowInt  = (int) (llTime & 0x00000000FFFFFFFF);
  outGoing[6] = htonl(highInt);
  outGoing[7] = htonl(lowInt);
  
  /* user time */
  llTime  = ((long long)msg->userTime.tv_sec * 1000) +
            ((long long)msg->userTime.tv_nsec/1000000);
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
  
  /* send creator (this sender's name if msg created here) */
  creator = msg->creator;
  if (creator == NULL) creator = domain->name;
  /* length of "creator" string */
  lenCreator   = strlen(creator);
  outGoing[13] = htonl(lenCreator);
  
  /* length of "text" string */
  outGoing[14] = htonl(lenText);
  
  /* length of byte array */
  lenByteArray = msg->byteArrayLength;
  outGoing[15] = htonl(lenByteArray);
    
  /* total length of message (minus first int) is first item sent */
  len = sizeof(outGoing) - sizeof(int) + lenSubject + lenType +
        lenCreator + lenText + lenByteArray;
  outGoing[0] = htonl(len);  

  /* make send socket communications thread-safe */
  socketMutexLock(domain);
  
  /* allocate more memory for message-sending buffer if necessary */
  if (domain->msgBufferSize < (int)(len+sizeof(int))) {
    free(domain->msgBuffer);
    domain->msgBufferSize = len + 1004; /* give us 1kB extra */
    domain->msgBuffer = (char *) malloc(domain->msgBufferSize);
    if (domain->msgBuffer == NULL) {
      socketMutexUnlock(domain);
      connectReadUnlock();
      free(info->subject);
      free(info->type);
      info->subject = NULL;
      info->type    = NULL;
      info->active  = 0;
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cmsgd_SendAndGet: out of memory\n");
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
  memcpy(domain->msgBuffer+len, (void *)creator, lenCreator);
  len += lenCreator;
  memcpy(domain->msgBuffer+len, (void *)msg->text, lenText);
  len += lenText;
  memcpy(domain->msgBuffer+len, (void *)&((msg->byteArray)[msg->byteArrayOffset]), lenByteArray);
  len += lenByteArray;   
    
  /* send data over socket */
  if (cMsgTcpWrite(fd, (void *) domain->msgBuffer, len) != len) {
    socketMutexUnlock(domain);
    connectReadUnlock();
    free(info->subject);
    free(info->type);
    info->subject = NULL;
    info->type    = NULL;
    info->active  = 0;
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsgd_SendAndGet: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
     
  /* done protecting communications */
  socketMutexUnlock(domain);
  connectReadUnlock();
  
  /* Now ..., wait for asynchronous response */
  
  /* lock mutex */
  status = pthread_mutex_lock(&info->mutex);
  if (status != 0) {
    err_abort(status, "Failed callback mutex lock");
  }

  /* wait while there is no message */
  while (info->msgIn == 0) {
    /* wait until signaled */
    if (timeout == NULL) {
      status = pthread_cond_wait(&info->cond, &info->mutex);
    }
    /* wait until signaled or timeout */
    else {
      getAbsoluteTime(timeout, &wait);
      status = pthread_cond_timedwait(&info->cond, &info->mutex, &wait);
    }
    
    if (status == ETIMEDOUT) {
      break;
    }
    else if (status != 0) {
      err_abort(status, "Failed callback cond wait");
    }

    /* quit if commanded to */
    if (info->quit) {
      break;
    }
  }

  /* unlock mutex */
  status = pthread_mutex_unlock(&info->mutex);
  if (status != 0) {
    err_abort(status, "Failed callback mutex unlock");
  }

  /* If we timed out, tell server to forget the get. */
  if (info->msgIn == 0) {
      /*printf("get: timed out\n");*/
      
      /* remove the get from server */
      unSendAndGet(domainId, uniqueId);

      *replyMsg = NULL;
      err = CMSG_TIMEOUT;
  }
  /* If we've been woken up with an error condition ... */
  else if (info->error != CMSG_OK) {
      *replyMsg = NULL;
      err = info->error;    
  }
  /* If we did not timeout and everything's OK */
  else {
      /*
       * Don't need to make a copy of message as only 1 receipient.
       * Message was allocated in client's listening thread and user
       * must free it.
       */
      *replyMsg = info->msg;
      err = CMSG_OK;
  }
  
  /* free up memory */
  free(info->subject);
  free(info->type);
  info->subject = NULL;
  info->type    = NULL;
  info->msg     = NULL;
  info->active  = 0;
  
  /*printf("get: SUCCESS!!!\n");*/

  return(err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine tells the cMsg server to "forget" about the cMsgSendAndGet()
 * call (specified by the id argument) since a timeout occurred.
 */   
static int unSendAndGet(void *domainId, int id) {
  
  int outGoing[3];
  cMsgDomain_CODA *domain = &cMsgDomains[(int)domainId];
  int fd = domain->sendSocket;
    
  /* size of info coming - 8 bytes */
  outGoing[0] = htonl(8);
  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(CMSG_UN_SEND_AND_GET_REQUEST);
  /* senderToken id */
  outGoing[2] = htonl(id);

  /* make send socket communications thread-safe */
  socketMutexLock(domain);
  
  /* send ints over together */
  if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
    socketMutexUnlock(domain);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "unSendAndGet: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
 
  socketMutexUnlock(domain);

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
 *
 * @returns CMSG_OK always
 */   
static int cmsgd_Flush(void *domainId) {  
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
 *
 * @returns CMSG_OK if successful
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
static int cmsgd_Subscribe(void *domainId, const char *subject, const char *type, cMsgCallback *callback,
                     void *userArg, cMsgSubscribeConfig *config) {

  int i, j, iok, jok, uniqueId, status, err;
  cMsgDomain_CODA *domain  = &cMsgDomains[(int)domainId];
  subscribeConfig *sConfig = (subscribeConfig *) config;
  cbArg *cbarg;
  struct iovec iov[3];
  int fd = domain->sendSocket;

  if (!domain->hasSubscribe) {
    return(CMSG_NOT_IMPLEMENTED);
  } 
  
  tryagain:
  while (1) {
    err = CMSG_OK;
    
    connectReadLock();

    if (domain->initComplete != 1) {
      connectReadUnlock();
      return(CMSG_NOT_INITIALIZED);
    }
    if (domain->gotConnection != 1) {
      connectReadUnlock();
      err = CMSG_LOST_CONNECTION;
      break;
    }

    /* use default configuration if none given */
    if (config == NULL) {
      sConfig = (subscribeConfig *) cMsgSubscribeConfigCreate();
    }

    /* make sure subscribe and unsubscribe are not run at the same time */
    subscribeMutexLock(domain);

    /* add to callback list if subscription to same subject/type exists */
    iok = jok = 0;
    for (i=0; i<MAX_SUBSCRIBE; i++) {
      if (domain->subscribeInfo[i].active == 0) {
        continue;
      }

      if ((strcmp(domain->subscribeInfo[i].subject, subject) == 0) && 
          (strcmp(domain->subscribeInfo[i].type, type) == 0) ) {

        iok = 1;
        jok = 0;

        /* scan through callbacks looking for duplicates */ 
        for (j=0; j<MAX_CALLBACK; j++) {
	  if (domain->subscribeInfo[i].cbInfo[j].active == 0) {
            continue;
          }

          if ( (domain->subscribeInfo[i].cbInfo[j].callback == callback) &&
               (domain->subscribeInfo[i].cbInfo[j].userArg  ==  userArg))  {

            subscribeMutexUnlock(domain);
            connectReadUnlock();
            return(CMSG_ALREADY_EXISTS);
          }
        }

        /* scan through callbacks looking for empty space */ 
        for (j=0; j<MAX_CALLBACK; j++) {
	  if (domain->subscribeInfo[i].cbInfo[j].active == 0) {

            domain->subscribeInfo[i].cbInfo[j].active   = 1;
	    domain->subscribeInfo[i].cbInfo[j].callback = callback;
	    domain->subscribeInfo[i].cbInfo[j].userArg  = userArg;
            domain->subscribeInfo[i].cbInfo[j].head     = NULL;
            domain->subscribeInfo[i].cbInfo[j].tail     = NULL;
            domain->subscribeInfo[i].cbInfo[j].quit     = 0;
            domain->subscribeInfo[i].cbInfo[j].messages = 0;
            domain->subscribeInfo[i].cbInfo[j].config   = *sConfig;
            
            cbarg = (cbArg *) malloc(sizeof(cbArg));
            if (cbarg == NULL) {
              subscribeMutexUnlock(domain);
              connectReadUnlock();
              return(CMSG_OUT_OF_MEMORY);  
            }                        
            cbarg->domainId = (int) domainId;
            cbarg->subIndex = i;
            cbarg->cbIndex  = j;

            /* start callback thread now */
            status = pthread_create(&domain->subscribeInfo[i].cbInfo[j].thread,
                                    NULL, callbackThread, (void *) cbarg);
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
      subscribeMutexUnlock(domain);
      connectReadUnlock();
      return(CMSG_OUT_OF_MEMORY);
    }
    if ((iok == 1) && (jok == 1)) {
      subscribeMutexUnlock(domain);
      connectReadUnlock();
      return(CMSG_OK);
    }

    /* no match, make new entry and notify server */
    iok = 0;
    for (i=0; i<MAX_SUBSCRIBE; i++) {
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

      cbarg = (cbArg *) malloc(sizeof(cbArg));
      if (cbarg == NULL) {
        subscribeMutexUnlock(domain);
        connectReadUnlock();
        return(CMSG_OUT_OF_MEMORY);  
      }                        
      cbarg->domainId = (int) domainId;
      cbarg->subIndex = i;
      cbarg->cbIndex  = 0;

      /* start callback thread now */
      status = pthread_create(&domain->subscribeInfo[i].cbInfo[0].thread,
                              NULL, callbackThread, (void *) cbarg);                              
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
       * Mutex protect this operation as many cmsgd_Connect calls may
       * operate in parallel on this static variable.
       */
      idMutexLock();
      uniqueId = subjectTypeId++;
      idMutexUnlock();
      domain->subscribeInfo[i].id = uniqueId;

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
      socketMutexLock(domain);

      if (cMsgTcpWritev(fd, iov, 3, 16) == -1) {
        socketMutexUnlock(domain);
        
        /* stop callback thread */
        domain->subscribeInfo[i].cbInfo[0].quit = 1;
        pthread_cancel(domain->subscribeInfo[i].cbInfo[0].thread);
        
        free(domain->subscribeInfo[i].subject);
        free(domain->subscribeInfo[i].type);
        free(domain->subscribeInfo[i].subjectRegexp);
        free(domain->subscribeInfo[i].typeRegexp);
        domain->subscribeInfo[i].subject       = NULL;
        domain->subscribeInfo[i].type          = NULL;
        domain->subscribeInfo[i].subjectRegexp = NULL;
        domain->subscribeInfo[i].typeRegexp    = NULL;
        domain->subscribeInfo[i].active = 0;
        
        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cmsgd_Subscribe: write failure\n");
        }
        
        err = CMSG_LOST_CONNECTION;
        break;
      }

      /* done protecting communications */
      socketMutexUnlock(domain);
      break;
    } /* for i */

    /* done protecting subscribe */
    subscribeMutexUnlock(domain);
    connectReadUnlock();
    break;  
  } /* while(1) */

  if (iok == 0) {
    err = CMSG_OUT_OF_MEMORY;
  }
  else if (err!= CMSG_OK) {
    /* don't wait for resubscribes */
    if (failoverSuccessful(domain, 0)) {
       fd = domain->sendSocket;
       printf("cmsgd_Subscribe: FAILOVER SUCCESSFUL, try subscribe again\n");
       goto tryagain;
    }  
printf("cmsgd_Subscribe: FAILOVER NOT successful, quitting, err = %d\n", err);
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
 * @param subject subject of messages subscribed to
 * @param type type of messages subscribed to
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 */   
static int resubscribe(cMsgDomain_CODA *domain, const char *subject, const char *type) {

  int i, uniqueId, mySubIndex=-1;
  struct iovec iov[3];
  int len, lenSubject, lenType;
  int fd = domain->sendSocket;
  int outGoing[6];

  if (domain->gotConnection != 1) {
    return(CMSG_LOST_CONNECTION);
  }

  /*
   * This routine is called by the routine "restoreSubscriptions"
   * which is protected by the connectWriteLock so no other
   * locks are required.
   */
  
  /* if an unsubscribe has been done, forget about resubscribing */
  for (i=0; i<MAX_SUBSCRIBE; i++) {
    if (domain->subscribeInfo[i].active == 0) {
      continue;
    }
    
    if ((strcmp(domain->subscribeInfo[i].subject, subject) == 0) && 
        (strcmp(domain->subscribeInfo[i].type, type) == 0) ) {
      /* subject & type exist so we're OK */
      mySubIndex = i;
      break;
    }
  }
  
  /* no subscription to this subject and type exist */
  if (mySubIndex < 0) {
    return(CMSG_OK);
  }
  
  /* Pick a unique identifier for the subject/type pair. */
  idMutexLock();
  uniqueId = subjectTypeId++;
  idMutexUnlock();
  i = mySubIndex;
  domain->subscribeInfo[i].id = uniqueId;

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
  /* length of "namespace" string (0 in this case, since namespace
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


  if (cMsgTcpWritev(fd, iov, 3, 16) == -1) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsgd_Subscribe: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine unsubscribes to messages of the given subject, type,
 * callback, and user argument. This routine is called by the user through
 * cMsgUnSubscribe() given the appropriate UDL. In this domain cMsgFlush()
 * does nothing and does not need to be called for cmsgd_Unsubscribe to be
 * started immediately.
 *
 * @param domainId id of the domain connection
 * @param subject subject of messages to unsubscribed from
 * @param type type of messages to unsubscribed from
 * @param callback pointer to callback to be removed
 * @param userArg user-specified pointer to be passed to the callback
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement
 *                               unsubscribe
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
static int cmsgd_Unsubscribe(void *domainId, const char *subject, const char *type, cMsgCallback *callback,
                           void *userArg) {

  int i, j, status, err;
  int cbCount = 0;     /* total number of callbacks for the subject/type pair of interest */
  int cbsRemoved = 0;  /* total number of callbacks removed for that subject/type pair */
  cMsgDomain_CODA *domain = &cMsgDomains[(int)domainId];
  int fd = domain->sendSocket;
  struct iovec iov[3];
  subscribeCbInfo *subscription, *matchingSub;
  
  if (!domain->hasUnsubscribe) {
    return(CMSG_NOT_IMPLEMENTED);
  }
 
  tryagain:
  while (1) {
    err = CMSG_OK;
    
    connectReadLock();

    if (domain->initComplete != 1) {
      connectReadUnlock();
      return(CMSG_NOT_INITIALIZED);
    }
    if (domain->gotConnection != 1) {
      connectReadUnlock();
      err = CMSG_LOST_CONNECTION;
      break;
    }

    /* make sure subscribe and unsubscribe are not run at the same time */
    subscribeMutexLock(domain);

    /* search entry list */
    for (i=0; i<MAX_SUBSCRIBE; i++) {

      if (domain->subscribeInfo[i].active == 0) {
        continue;
      }

      /* if there is a match with subject & type ... */
      if ( (strcmp(domain->subscribeInfo[i].subject, subject) == 0)  && 
           (strcmp(domain->subscribeInfo[i].type,    type)    == 0) )  {

        /* search callback list */
        for (j=0; j<MAX_CALLBACK; j++) {

          /* convenience variable */
          subscription = &domain->subscribeInfo[i].cbInfo[j];

          /* if the subscription is active ... */
	  if (subscription->active == 1) {

            cbCount++;

            /* if the callback and argument are identical (there will only be one)... */
	    if ( (subscription->callback == callback) &&
                 (subscription->userArg  ==  userArg))  {
             
              /* Save the subscription so we can delete it LATER,
               * after the server gets notified. That way, if there
               * is an error in communication with the server, we
               * will not have actually removed the callback yet
               * and can back out gracefully.
               */
              matchingSub = subscription;
              /* removing 1 callback */
              cbsRemoved++;
            }
	  }          
          
        } /* for j */
        break;
      }
    } /* for i */
    
    
    /* if there is no matching callback, return */
    if (cbsRemoved < 1) {
      subscribeMutexUnlock(domain);
      connectReadUnlock();
      return(CMSG_OK);   
    }
    

    /* Delete entry and notify server if there was at least 1 callback
     * to begin with and now there are none for this subject/type.
     */
    if ((cbCount > 0) && (cbCount-cbsRemoved < 1)) {

      int len, lenSubject, lenType;
      int outGoing[6];

      if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "cmsgd_Unsubscribe: send 4 ints\n");
      }

      /* notify server */

      /* message id (in network byte order) to domain server */
      outGoing[1] = htonl(CMSG_UNSUBSCRIBE_REQUEST);
      /* unique id associated with subject/type */
      outGoing[2] = htonl(domain->subscribeInfo[i].id);
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
      socketMutexLock(domain);

      if (cMsgTcpWritev(fd, iov, 3, 16) == -1) {
        socketMutexUnlock(domain);
        subscribeMutexUnlock(domain);
        connectReadUnlock();
        
        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cmsgd_Unsubscribe: write failure\n");
        }
        err = CMSG_NETWORK_ERROR;
        break;
      }

       /* done protecting communications */
      socketMutexUnlock(domain);
      
      /* We told the server, now do the unsubscribe. */
      free(domain->subscribeInfo[i].subject);
      free(domain->subscribeInfo[i].type);
      free(domain->subscribeInfo[i].subjectRegexp);
      free(domain->subscribeInfo[i].typeRegexp);
      /* set these equal to NULL so they aren't freed again later */
      domain->subscribeInfo[i].subject       = NULL;
      domain->subscribeInfo[i].type          = NULL;
      domain->subscribeInfo[i].subjectRegexp = NULL;
      domain->subscribeInfo[i].typeRegexp    = NULL;
      /* make array space available for another subscription */
      domain->subscribeInfo[i].active = 0;
      
    } /* if gotta notify server */
    
    /* tell callback thread to end */
    matchingSub->quit = 1;

    /* wakeup callback thread */
    status = pthread_cond_broadcast(&matchingSub->cond);
    if (status != 0) {
      err_abort(status, "Failed callback condition signal");
    }
    /*
     * Once this subscription wakes up it sets the array location
     * as inactive/available (subscription->active = 0). Don't do
     * that yet as another subscription may be done
     * (and set subscription->active = 1) before it wakes up
     * and thus not end itself.
     */
    
    /* done protecting unsubscribe */
    subscribeMutexUnlock(domain);
    connectReadUnlock();
  
    break;
    
  } /* while(1) */

  if (err!= CMSG_OK) {
    /* wait awhile for possible failover && resubscribe is complete */
    if (failoverSuccessful(domain, 1)) {
       fd = domain->sendSocket;
       printf("cmsgd_Unsubscribe: FAILOVER SUCCESSFUL, try unsubscribe again\n");
       goto tryagain;
    }  
printf("cmsgd_Unsubscribe: FAILOVER NOT successful, quitting, err = %d\n", err);
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
 */   
static int cmsgd_Start(void *domainId) {
  
  cMsgDomains[(int)domainId].receiveState = 1;
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
static int cmsgd_Stop(void *domainId) {
  
  cMsgDomains[(int)domainId].receiveState = 0;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine disconnects the client from the cMsg server.
 *
 * @param domainId id of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 */   
static int cmsgd_Disconnect(void *domainId) {
  
  int i, j, status, outGoing[2];
  cMsgDomain_CODA *domain = &cMsgDomains[(int)domainId];
  int fd = domain->sendSocket;
  subscribeCbInfo *subscription;
  getInfo *info;

  if (domain->initComplete != 1) return(CMSG_NOT_INITIALIZED);
  
  /* When changing initComplete / connection status, protect it */
  connectWriteLock();
  
  /*
   * If the domain server thread terminates first, our keep alive thread will
   * detect it and call this function. To prevent this, first kill our
   * keep alive thread, close the socket, then tell the server we're going away.
   */
   
  /* stop keep alive thread */
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_Disconnect:cancel keep alive thread\n");
  }
  
  /* don't care if this fails */
  pthread_cancel(domain->keepAliveThread);
  close(domain->keepAliveSocket);

  /* Tell server we're disconnecting */
  
  /* size of msg */
  outGoing[0] = htonl(4);
  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(CMSG_SERVER_DISCONNECT);

  /* make send socket communications thread-safe */
  socketMutexLock(domain);
  
  /* send int */
  if (cMsgTcpWrite(fd, (char*) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
    /*
    socketMutexUnlock(domain);
    connectWriteUnlock();
    */
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsgd_Disconnect: write failure, but continue\n");
    }
    /*return(CMSG_NETWORK_ERROR);*/
  }
 
  socketMutexUnlock(domain);

  domain->gotConnection = 0;
  
  /* close sending socket */
  close(domain->sendSocket);

  /* close receiving socket */
  close(domain->receiveSocket);

  /* stop listening and client communication threads */
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cmsgd_Disconnect:cancel listening & client threads\n");
  }
  
  pthread_cancel(domain->pendThread);
  /* close listening socket */
  close(domain->listenSocket);
  
  /* terminate all callback threads */
  for (i=0; i<MAX_SUBSCRIBE; i++) {
    /* if there is a subscription ... */
    if (domain->subscribeInfo[i].active == 1)  {
      /* search callback list */
      for (j=0; j<MAX_CALLBACK; j++) {
        /* convenience variable */
        subscription = &domain->subscribeInfo[i].cbInfo[j];
    
	if (subscription->active == 1) {          
          
          /* tell callback thread to end */
          subscription->quit = 1;
          
          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "cmsgd_Disconnect:wake up callback thread\n");
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

  /* wakeup all gets */
  for (i=0; i<MAX_SEND_AND_GET; i++) {
    
    info = &domain->sendAndGetInfo[i];
    if (info->active != 1) {
      continue;
    }
    
    /* wakeup "get" */      
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cmsgd_Disconnect:wake up a sendAndGet\n");
    }
  
    status = pthread_cond_signal(&info->cond);
    if (status != 0) {
      err_abort(status, "Failed get condition signal");
    }    
  }
  
  /* give the above threads a chance to quit before we reset everytbing */
  sleep(1);
  
  /* free memory (non-NULL items), reset variables*/
  /*
  if(msgBuffer!=NULL) free(msgBuffer);
  msgBuffer=NULL;
  */
  domainClear(domain);
  
  connectWriteUnlock();

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
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 */   
static int disconnectFromKeepAlive(void *domainId) {
  
  int i, j, status, outGoing[2];
  cMsgDomain_CODA *domain = &cMsgDomains[(int)domainId];
  subscribeCbInfo *subscription;
  getInfo *info;

  
  /* When changing initComplete / connection status, protect it */
  connectWriteLock();
     
  /* stop listening and client communication threads */
printf("disconnect: cancelling pend thread\n");
  pthread_cancel(domain->pendThread);
   
  /* close sending socket */
  close(domain->sendSocket);

  /* close receiving socket */
  close(domain->receiveSocket);
    
  
  /* terminate all callback threads */
  for (i=0; i<MAX_SUBSCRIBE; i++) {
    /* if there is a subscription ... */
    if (domain->subscribeInfo[i].active == 1)  {
      /* search callback list */
      for (j=0; j<MAX_CALLBACK; j++) {
        /* convenience variable */
        subscription = &domain->subscribeInfo[i].cbInfo[j];
    
	if (subscription->active == 1) {          
          
          /* tell callback thread to end */
          subscription->quit = 1;
          
          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "cmsgd_Disconnect:wake up callback thread\n");
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

  /* wakeup all gets */
  for (i=0; i<MAX_SEND_AND_GET; i++) {
    
    info = &domain->sendAndGetInfo[i];
    if (info->active != 1) {
      continue;
    }
    
    /* wakeup "get" */      
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cmsgd_Disconnect:wake up a sendAndGet\n");
    }
  
    status = pthread_cond_signal(&info->cond);
    if (status != 0) {
      err_abort(status, "Failed get condition signal");
    }    
  }
  
  /* give the above threads a chance to quit before we reset everytbing */
  sleep(1);
  
  /* close listening socket */
  /*close(domain->listenSocket);*/
  
  /* free memory (non-NULL items), reset variables*/
  domainClear(domain);
  
  connectWriteUnlock();
printf("disconnect: end KA thread\n");

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
   /* if (cMsgDebug >= CMSG_DEBUG_ERROR) { */
      fprintf(stderr, "Ran default shutdown handler\n");
   /* } */
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
static int cmsgd_SetShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg) {
  
  cMsgDomain_CODA *domain = &cMsgDomains[(int)domainId];

  if (domain->initComplete != 1) return(CMSG_NOT_INITIALIZED);
  
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
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement shutdown
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 */
static int cmsgd_ShutdownClients(void *domainId, const char *client, int flag) {
  
  int len, cLen, outGoing[4];
  cMsgDomain_CODA *domain = &cMsgDomains[(int)domainId];
  int fd = domain->sendSocket;
  struct iovec iov[2];

  if (!domain->hasShutdown) {
    return(CMSG_NOT_IMPLEMENTED);
  } 
  
  if (domain->initComplete != 1) return(CMSG_NOT_INITIALIZED);
      
  connectWriteLock();
    
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
  socketMutexLock(domain);
  
  if (cMsgTcpWritev(fd, iov, 2, 16) == -1) {
    socketMutexUnlock(domain);
    connectWriteUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsgd_Unsubscribe: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
   
  socketMutexUnlock(domain);  
  connectWriteUnlock();

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
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement shutdown
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 */
static int cmsgd_ShutdownServers(void *domainId, const char *server, int flag) {
  
  int len, sLen, outGoing[4];
  cMsgDomain_CODA *domain = &cMsgDomains[(int)domainId];
  int fd = domain->sendSocket;
  struct iovec iov[2];

  if (!domain->hasShutdown) {
    return(CMSG_NOT_IMPLEMENTED);
  } 
  
  if (domain->initComplete != 1) return(CMSG_NOT_INITIALIZED);
      
  connectWriteLock();
    
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
  socketMutexLock(domain);
  
  if (cMsgTcpWritev(fd, iov, 2, 16) == -1) {
    socketMutexUnlock(domain);
    connectWriteUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cmsgd_Unsubscribe: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
   
  socketMutexUnlock(domain);  
  connectWriteUnlock();

  return CMSG_OK;

}


/*-------------------------------------------------------------------*/


/** This routine exchanges information with the name server. */
static int talkToNameServer(cMsgDomain_CODA *domain, int serverfd, int failoverIndex) {

  int  err, lengthDomain, lengthSubdomain, lengthRemainder, lengthPassword;
  int  lengthHost, lengthName, lengthUDL, lengthDescription;
  int  outGoing[12], inComing[2];
  char temp[CMSG_MAXHOSTNAMELEN], atts[7];
  const char *domainType = "cMsg";
  struct iovec iov[9];
  parsedUDL *pUDL = &domain->failovers[failoverIndex];

  /* first send message id (in network byte order) to server */
  outGoing[0] = htonl(CMSG_SERVER_CONNECT);
  /* major version number */
  outGoing[1] = htonl(CMSG_VERSION_MAJOR);
  /* minor version number */
  outGoing[2] = htonl(CMSG_VERSION_MINOR);
  /* send my listening port (as an int) to server */
  outGoing[3] = htonl((int)domain->listenPort);
  /* send length of password for connecting to server.*/
  if (pUDL->password == NULL) {
    lengthPassword = outGoing[4] = 0;
  }
  else {
    lengthPassword = strlen(pUDL->password);
    outGoing[4]    = htonl(lengthPassword);
  }
  /* send length of the type of domain server I'm expecting to connect to.*/
  lengthDomain = strlen(domainType);
  outGoing[5]  = htonl(lengthDomain);
  /* send length of the type of subdomain handler I'm expecting to use.*/
  lengthSubdomain = strlen(pUDL->subdomain);
  outGoing[6] = htonl(lengthSubdomain);
  /* send length of the UDL remainder.*/
  /* this may be null */
  if (pUDL->udlRemainder == NULL) {
    lengthRemainder = outGoing[7] = 0;
  }
  else {
    lengthRemainder = strlen(pUDL->udlRemainder);
    outGoing[7] = htonl(lengthRemainder);
  }
  /* send length of my host name to server */
  lengthHost  = strlen(domain->myHost);
  outGoing[8] = htonl(lengthHost);
  /* send length of my name to server */
  lengthName  = strlen(domain->name);
  outGoing[9] = htonl(lengthName);
  /* send length of my udl to server */
  lengthUDL   = strlen(pUDL->udl);
  outGoing[10] = htonl(lengthUDL);
  /* send length of my description to server */
  lengthDescription  = strlen(domain->description);
  outGoing[11] = htonl(lengthDescription);
    
  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);
  
  iov[1].iov_base = (char*) pUDL->password;
  iov[1].iov_len  = lengthPassword;
  
  iov[2].iov_base = (char*) domainType;
  iov[2].iov_len  = lengthDomain;
  
  iov[3].iov_base = (char*) pUDL->subdomain;
  iov[3].iov_len  = lengthSubdomain;
  
  iov[4].iov_base = (char*) pUDL->udlRemainder;
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
      exit(1);
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
  domain->sendPort = (unsigned short) ntohl(inComing[0]);
  lengthHost = ntohl(inComing[1]);

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "talkToNameServer: port = %hu, host len = %d\n",
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


/*-------------------------------------------------------------------*
 * keepAliveThread is a thread used to send keep alive packets
 * to other cMsg-enabled programs. If there is no response or there is
 * an I/O error. The other end of the socket is presumed dead.
 *-------------------------------------------------------------------*/
/**
 * This routine is run as a thread which is used to send keep alive
 * communication to a cMsg server.
 */
static void *keepAliveThread(void *arg)
{
    cMsgDomain_CODA *domain = (cMsgDomain_CODA *) arg;
    int domainId = domain->id;
    int socket   = domain->keepAliveSocket;
    int outGoing, alive, err;
    
    int failoverIndex = domain->failoverIndex;
    int connectFailures = 0;
    int weGotAConnection = 1; /* true */
    int resubscriptionsComplete = 0; /* false */
    struct timespec wait;
    
    /* increase concurrency for this thread for early Solaris */
    int  con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);

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
    
        /* keep checking to see if the server/agent is alive */
        while(1) {       
           if (cMsgTcpWrite(socket, &outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
               if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                   fprintf(stderr, "keepAliveThread: error writing request\n");
               }
               break;
           }

           if ((err = cMsgTcpRead(socket, (char*) &alive, sizeof(alive))) != sizeof(alive)) {
               if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                   fprintf(stderr, "keepAliveThread: read failure\n");
               }
               break;
           }
/*printf("ka: read %d\n", ntohl(alive));*/

           /* sleep for 1 second and try again */
           sleep(1);
        }

        /* clean up */
        close(domain->keepAliveSocket);
        close(domain->receiveSocket);
        close(domain->sendSocket);
        
        /* Start by trying to connect to the first UDL on the list.
         * If we've just been connected to that UDL, try the next. */
        if (failoverIndex != 0) {
            failoverIndex = -1;
        }
        connectFailures = 0;
        weGotAConnection = 0;
        domain->resubscribeComplete = 0;

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
            domainId = domain->id;
/* printf("ka: Set domain->keepaliveSocket to %d\n", domain->keepAliveSocket); */            
            socket = domain->keepAliveSocket; 

            /* we got ourselves a new server, boys */
            weGotAConnection = 1;
            
            /* wait for up to 1.1 sec for waiters to respond */
            err = latchCountDown(&domain->failoverLatch, &wait);
            if (err != 1) {
/* printf("ka: Problems with reporting back to countdowner\n"); */            
            }
            latchReset(&domain->failoverLatch, 1, NULL);
        }
    }

    /* close communication socket */
    close(socket);

    /* if we've reach here, there's an error, do a disconnect */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "keepAliveThread: server is probably dead, disconnect\n");
    }
    
/* printf("\n\n\nka: DISCONNECTING \n\n\n"); */            
    disconnectFromKeepAlive((void *)domainId);
    
    sun_setconcurrency(con);
    
    return NULL;
}


/** This routine is run as a thread in which a single callback is executed. */
static void *callbackThread(void *arg)
{
    /* subscription information passed in thru arg */
    cbArg *cbarg = (cbArg *) arg;
    int domainId = cbarg->domainId;
    int subIndex = cbarg->subIndex;
    int cbIndex  = cbarg->cbIndex;
    cMsgDomain_CODA *domain = &cMsgDomains[domainId];
    subscribeCbInfo *cback  = &domain->subscribeInfo[subIndex].cbInfo[cbIndex];
    int i, status, need, threadsAdded, maxToAdd, wantToAdd;
    int numMsgs, numThreads;
    cMsgMessage *msg, *nextMsg;
    pthread_t thd;
    /* time_t now, t; *//* for printing msg cue size periodically */
    
    /* increase concurrency for this thread for early Solaris */
    int con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);
    
    /* for printing msg cue size periodically */
    /* now = time(NULL); */
        
    /* release system resources when thread finishes */
    pthread_detach(pthread_self());

    threadsAdded = 0;

    while(1) {
      /*
       * Take a current snapshot of the number of threads and messages.
       * The number of threads may decrease since threads die if there
       * are no messages to grab, but this is the only place that the
       * number of threads will be increased.
       */
      numMsgs = cback->messages;
      numThreads = cback->threads;
      threadsAdded = 0;
      
      /* Check to see if we need more threads to handle the load */      
      if ((!cback->config.mustSerialize) &&
          (numThreads < cback->config.maxThreads) &&
          (numMsgs > cback->config.msgsPerThread)) {

        /* find number of threads needed */
        need = cback->messages/cback->config.msgsPerThread;

        /* add more threads if necessary */
        if (need > numThreads) {
          
          /* maximum # of threads that can be added w/o exceeding config limit */
          maxToAdd  = cback->config.maxThreads - numThreads;
          /* number of threads we want to add to handle the load */
          wantToAdd = need - numThreads;
          /* number of threads that we will add */
          threadsAdded = maxToAdd > wantToAdd ? wantToAdd : maxToAdd;
                    
          for (i=0; i < threadsAdded; i++) {
            status = pthread_create(&thd, NULL, supplementalThread, arg);
            if (status != 0) {
              err_abort(status, "Creating supplemental callback thread");
            }
          }
        }
      }
           
      /* lock mutex */
/* fprintf(stderr, "  CALLBACK THREAD: will grab mutex %p\n", &cback->mutex); */
      mutexLock(&cback->mutex);
/* fprintf(stderr, "  CALLBACK THREAD: grabbed mutex\n"); */
      
      /* quit if commanded to */
      if (cback->quit) {
          /* Set flag telling thread-that-adds-messages-to-cue that
           * its time to stop. The only bug-a-boo here is that we would
           * prefer to do the following with the domain->subscribeMutex
           * grabbed.
           */
          cback->active = 0;
          
          /* Now free all the messages cued up. */
          msg = cback->head; /* get first message in linked list */
          while (msg != NULL) {
            nextMsg = msg->next;
            cMsgFreeMessage(msg);
            msg = nextMsg;
          }
          
          cback->messages = 0;
          
          /* unlock mutex */
          mutexUnlock(&cback->mutex);

          /* Signal to cMsgRunCallbacks in case the cue is full and it's
           * blocked trying to put another message in. So now we tell it that
           * there are no messages in the cue and, in fact, no callback anymore.
           */
          status = pthread_cond_signal(&domain->subscribeCond);
          if (status != 0) {
            err_abort(status, "Failed callback condition signal");
          }
          
/* printf(" CALLBACK THREAD: told to quit\n"); */
          goto end;
      }

      /* do the following bookkeeping under mutex protection */
      cback->threads += threadsAdded;

      if (threadsAdded) {
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "thds = %d\n", cback->threads);
        }
      }
      
      /* wait while there are no messages */
      while (cback->head == NULL) {
        /* wait until signaled */
/* fprintf(stderr, "  CALLBACK THREAD: cond wait, release mutex\n"); */
        status = pthread_cond_wait(&cback->cond, &cback->mutex);
        if (status != 0) {
          err_abort(status, "Failed callback cond wait");
        }
/* fprintf(stderr, "  CALLBACK THREAD woke up, grabbed mutex\n", cback->quit); */
        
        /* quit if commanded to */
        if (cback->quit) {
          /* Set flag telling thread-that-adds-messages-to-cue that
           * its time to stop. The only bug-a-boo here is that we would
           * prefer to do the following with the domain->subscribeMutex
           * grabbed.
           */
          cback->active = 0;
          
          /* Now free all the messages cued up. */
          msg = cback->head; /* get first message in linked list */
          while (msg != NULL) {
            nextMsg = msg->next;
            cMsgFreeMessage(msg);
            msg = nextMsg;
          }
          
          cback->messages = 0;
          
          /* unlock mutex */
          mutexUnlock(&cback->mutex);

          /* Signal to cMsgRunCallbacks in case the cue is full and it's
           * blocked trying to put another message in. So now we tell it that
           * there are no messages in the cue and, in fact, no callback anymore.
           */
          status = pthread_cond_signal(&domain->subscribeCond);
          if (status != 0) {
            err_abort(status, "Failed callback condition signal");
          }
          
/* printf(" CALLBACK THREAD: told to quit\n"); */
          goto end;
        }
      }
            
      /* get first message in linked list */
      msg = cback->head;

      /* if there are no more messages ... */
      if (msg->next == NULL) {
        cback->head = NULL;
        cback->tail = NULL;
      }
      /* else make the next message the head */
      else {
        cback->head = msg->next;
      }
      cback->messages--;
     
      /* unlock mutex */
/* fprintf(stderr, "  CALLBACK THREAD: message taken off cue, cue = %d\n",cback->messages);
   fprintf(stderr, "  CALLBACK THREAD: release mutex\n"); */
      mutexUnlock(&cback->mutex);
      
      /* wakeup cMsgRunCallbacks thread if trying to add item to full cue */
/* fprintf(stderr, "  CALLBACK THREAD: wake up cMsgRunCallbacks thread\n"); */
      status = pthread_cond_signal(&domain->subscribeCond);
      if (status != 0) {
        err_abort(status, "Failed callback condition signal");
      }

      /* print out number of messages in cue */      
/*       t = time(NULL);
      if (now + 3 <= t) {
        printf("  CALLBACK THD: cue size = %d\n",cback->messages);
        now = t;
      }
 */      
      /* run callback */
#ifdef	__cplusplus
      cback->callback->callback(new cMsgMessageBase(msg), cback->userArg); 
#else
/* fprintf(stderr, "  CALLBACK THREAD: will run callback\n"); */
      cback->callback(msg, cback->userArg);
/* fprintf(stderr, "  CALLBACK THREAD: just ran callback\n"); */
#endif
      
    } /* while(1) */
    
  end:
    
    free(arg);     
    sun_setconcurrency(con);
/* fprintf(stderr, "QUITTING MAIN CALLBACK THREAD\n"); */
    pthread_exit(NULL);
    return NULL;
}



/*-------------------------------------------------------------------*
 * supplementalThread is a thread used to run a callback in parallel
 * with the callbackThread. As many supplemental threads are created
 * as needed to keep the cue size manageable.
 *-------------------------------------------------------------------*/
/**
 * This routine is run as a thread in which a callback is executed in
 * parallel with other similar threads.
 */
static void *supplementalThread(void *arg)
{
    /* subscription information passed in thru arg */
    cbArg *cbarg = (cbArg *) arg;
    int domainId = cbarg->domainId;
    int subIndex = cbarg->subIndex;
    int cbIndex  = cbarg->cbIndex;
    cMsgDomain_CODA *domain = &cMsgDomains[domainId];
    subscribeCbInfo *cback  = &domain->subscribeInfo[subIndex].cbInfo[cbIndex];
    int status, empty;
    cMsgMessage *msg, *nextMsg;
    struct timespec wait, timeout;
    
    /* increase concurrency for this thread for early Solaris */
    int  con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);

    /* release system resources when thread finishes */
    pthread_detach(pthread_self());

    /* wait .2 sec before waking thread up and checking for messages */
    timeout.tv_sec  = 0;
    timeout.tv_nsec = 200000000;

    while(1) {
      
      empty = 0;
      
      /* lock mutex before messing with linked list */
      mutexLock(&cback->mutex);
      
      /* quit if commanded to */
      if (cback->quit) {
          /* Set flag telling thread-that-adds-messages-to-cue that
           * its time to stop. The only bug-a-boo here is that we would
           * prefer to do the following with the domain->subscribeMutex
           * grabbed.
           */
          cback->active = 0;
          
          /* Now free all the messages cued up. */
          msg = cback->head; /* get first message in linked list */
          while (msg != NULL) {
            nextMsg = msg->next;
            cMsgFreeMessage(msg);
            msg = nextMsg;
          }
          
          cback->messages = 0;
          
          /* unlock mutex */
          mutexUnlock(&cback->mutex);

          /* Signal to cMsgRunCallbacks in case the cue is full and it's
           * blocked trying to put another message in. So now we tell it that
           * there are no messages in the cue and, in fact, no callback anymore.
           */
          status = pthread_cond_signal(&domain->subscribeCond);
          if (status != 0) {
            err_abort(status, "Failed callback condition signal");
          }
          
          goto end;
      }

      /* wait while there are no messages */
      while (cback->head == NULL) {
        /* wait until signaled or for .2 sec, before
         * waking thread up and checking for messages
         */
        getAbsoluteTime(&timeout, &wait);        
        status = pthread_cond_timedwait(&cback->cond, &cback->mutex, &wait);
        
        /* if the wait timed out ... */
        if (status == ETIMEDOUT) {
          /* if we wake up 10 times with no messages (2 sec), quit this thread */
          if (++empty%10 == 0) {
            cback->threads--;
            if (cMsgDebug >= CMSG_DEBUG_INFO) {
              fprintf(stderr, "thds = %d\n", cback->threads);
            }
            
            /* unlock mutex & kill this thread */
            mutexUnlock(&cback->mutex);
            
            sun_setconcurrency(con);

            pthread_exit(NULL);
            return NULL;
          }

        }
        else if (status != 0) {
          err_abort(status, "Failed callback cond wait");
        }
        
        /* quit if commanded to */
        if (cback->quit) {
          /* Set flag telling thread-that-adds-messages-to-cue that
           * its time to stop. The only bug-a-boo here is that we would
           * prefer to do the following with the domain->subscribeMutex
           * grabbed.
           */
          cback->active = 0;
          
          /* Now free all the messages cued up. */
          msg = cback->head; /* get first message in linked list */
          while (msg != NULL) {
            nextMsg = msg->next;
            cMsgFreeMessage(msg);
            msg = nextMsg;
          }
          
          cback->messages = 0;
          
          /* unlock mutex */
          mutexUnlock(&cback->mutex);

          /* Signal to cMsgRunCallbacks in case the cue is full and it's
           * blocked trying to put another message in. So now we tell it that
           * there are no messages in the cue and, in fact, no callback anymore.
           */
          status = pthread_cond_signal(&domain->subscribeCond);
          if (status != 0) {
            err_abort(status, "Failed callback condition signal");
          }
          
          goto end;
        }
      }
                  
      /* get first message in linked list */
      msg = cback->head;      

      /* if there are no more messages ... */
      if (msg->next == NULL) {
        cback->head = NULL;
        cback->tail = NULL;
      }
      /* else make the next message the head */
      else {
        cback->head = msg->next;
      }
      cback->messages--;
     
      /* unlock mutex */
      mutexUnlock(&cback->mutex);
      
      /* wakeup cMsgRunCallbacks thread if trying to add item to full cue */
      status = pthread_cond_signal(&domain->subscribeCond);
      if (status != 0) {
        err_abort(status, "Failed callback condition signal");
      }

      /* run callback */
#ifdef	__cplusplus
      cback->callback->callback(new cMsgMessageBase(msg), cback->userArg);
#else
      cback->callback(msg, cback->userArg);
#endif
      
    }
    
  end:
          
    sun_setconcurrency(con);
    
    pthread_exit(NULL);
    return NULL;
}


/*-------------------------------------------------------------------*/

/**
 * This routine runs all the appropriate subscribe and subscribeAndGet
 * callbacks when a message arrives from the server. 
 */
int cMsgRunCallbacks(int domainId, cMsgMessage *msg) {

  int i, j, k, status, goToNextCallback;
  subscribeCbInfo *cback;
  getInfo *info;
  cMsgDomain_CODA *domain;
  cMsgMessage *message, *oldHead;
  struct timespec wait, timeout;
    

  /* wait 60 sec between warning messages for a full cue */
  timeout.tv_sec  = 3;
  timeout.tv_nsec = 0;
  
  domain = &cMsgDomains[domainId];
  
  /* for each subscribeAndGet ... */
  for (j=0; j<MAX_SUBSCRIBE_AND_GET; j++) {
    
    info = &domain->subscribeAndGetInfo[j];

    if (info->active != 1) {
      continue;
    }

    /* if the subject & type's match, wakeup the "subscribeAndGet */      
    if ( (cMsgStringMatches(info->subject, msg->subject) == 1) &&
         (cMsgStringMatches(info->type, msg->type) == 1)) {
/*
printf("cMsgRunCallbacks: MATCHES:\n");
printf("                  SUBJECT = msg (%s), subscription (%s)\n",
                        msg->subject, info->subject);
printf("                  TYPE    = msg (%s), subscription (%s)\n",
                        msg->type, info->type);
*/
      /* pass msg to "get" */
      /* copy message so each callback has its own copy */
      message = (cMsgMessage *) cMsgCopyMessage((void *)msg);
      if (message == NULL) {
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "cMsgRunCallbacks: out of memory\n");
        }
        return(CMSG_OUT_OF_MEMORY);
      }

      info->msg = message;
      info->msgIn = 1;

      /* wakeup "get" */      
      status = pthread_cond_signal(&info->cond);
      if (status != 0) {
        err_abort(status, "Failed get condition signal");
      }
      
    }
  }
           
  
  /* callbacks have been stopped */
  if (domain->receiveState == 0) {
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cMsgRunCallbacks: all callbacks have been stopped\n");
    }
    cMsgFreeMessage((void *)msg);
    return (CMSG_OK);
  }
   
  /* Don't want subscriptions added or removed while iterating through them. */
  subscribeMutexLock(domain);
  
  /* for each client subscription ... */
  for (i=0; i<MAX_SUBSCRIBE; i++) {

    /* if subscription not active, forget about it */
    if (domain->subscribeInfo[i].active != 1) {
      continue;
    }

    /* if the subject & type's match, run callbacks */      
    if ( (cMsgRegexpMatches(domain->subscribeInfo[i].subjectRegexp, msg->subject) == 1) &&
         (cMsgRegexpMatches(domain->subscribeInfo[i].typeRegexp, msg->type) == 1)) {
/*
printf("cMsgRunCallbacks: MATCHES:\n");
printf("                  SUBJECT = msg (%s), subscription (%s)\n",
                        msg->subject, domain->subscribeInfo[i].subject);
printf("                  TYPE    = msg (%s), subscription (%s)\n",
                        msg->type, domain->subscribeInfo[i].type);
*/
      /* search callback list */
      for (j=0; j<MAX_CALLBACK; j++) {
        /* convenience variable */
        cback = &domain->subscribeInfo[i].cbInfo[j];

	/* if there is no existing callback, look at next item ... */
        if (cback->active != 1) {
          continue;
        }

        /* copy message so each callback has its own copy */
        message = (cMsgMessage *) cMsgCopyMessage((void *)msg);
        if (message == NULL) {
          subscribeMutexUnlock(domain);
          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "cMsgRunCallbacks: out of memory\n");
          }
          return(CMSG_OUT_OF_MEMORY);
        }

        /* check to see if there are too many messages in the cue */
        if (cback->messages >= cback->config.maxCueSize) {
          /* if we may skip messages, dump oldest */
          if (cback->config.maySkip) {
/* fprintf(stderr, "cMsgRunCallbacks: cue full, skipping\n");
fprintf(stderr, "cMsgRunCallbacks: will grab mutex, %p\n", &cback->mutex); */
              /* lock mutex before messing with linked list */
              mutexLock(&cback->mutex);
/* fprintf(stderr, "cMsgRunCallbacks: grabbed mutex\n"); */

              for (k=0; k < cback->config.skipSize; k++) {
                oldHead = cback->head;
                cback->head = cback->head->next;
                cMsgFreeMessage(oldHead);
                cback->messages--;
                if (cback->head == NULL) break;
              }

              mutexUnlock(&cback->mutex);

              if (cMsgDebug >= CMSG_DEBUG_INFO) {
                fprintf(stderr, "cMsgRunCallbacks: skipped %d messages\n", (k+1));
              }
          }
          else {
/* fprintf(stderr, "cMsgRunCallbacks: cue full (%d), waiting\n", cback->messages); */
              goToNextCallback = 0;

              while (cback->messages >= cback->config.maxCueSize) {
                  /* Wait here until signaled - meaning message taken off cue or unsubscribed.
                   * There is a problem doing a pthread_cancel on this thread because
                   * the only cancellation point is the timedwait which follows. The
                   * cancellation wakes the timewait which locks the mutex and then it
                   * exits the thread. However, we do NOT want to block cancellation
                   * here just in case we need to kill things no matter what.
                   */
                  getAbsoluteTime(&timeout, &wait);        
/* fprintf(stderr, "cMsgRunCallbacks: cue full, start waiting, will UNLOCK mutex\n"); */
                  status = pthread_cond_timedwait(&domain->subscribeCond, &domain->subscribeMutex, &wait);
/* fprintf(stderr, "cMsgRunCallbacks: out of wait, mutex is LOCKED\n"); */
                  
                  /* Check to see if server died and this thread is being killed. */
                  if (domain->killClientThread == 1) {
                    subscribeMutexUnlock(domain);
                    cMsgFreeMessage((void *)message);
/* fprintf(stderr, "cMsgRunCallbacks: told to die GRACEFULLY so return error\n"); */
                    return(CMSG_SERVER_DIED);
                  }
                  
                  /* BUGBUG
                   * There is a race condition here. If an unsubscribe of the current
                   * callback was done during the above wait there may be a problem.
                   * It's possible that the array element storing the callback info
                   * would be overwritten with the new subscription. This can only
                   * happen if the new subscription sneaks in after the above wait
                   * and before the check on the next line. In any case, what could
                   * happen is that the message waiting to be put on the cue is now
                   * put on the new cue.
                   * Check for our callback being unsubscribed first.
                   */
                  if (cback->active == 0) {
	            /* if there is no callback anymore, dump message, look at next callback */
                    cMsgFreeMessage((void *)message);
                    goToNextCallback = 1;                     
/* fprintf(stderr, "cMsgRunCallbacks: unsubscribe during pthread_cond_wait\n"); */
                    break;                      
                  }

                  /* if the wait timed out ... */
                  if (status == ETIMEDOUT) {
/* fprintf(stderr, "cMsgRunCallbacks: timeout of waiting\n"); */
                      if (cMsgDebug >= CMSG_DEBUG_WARN) {
                        fprintf(stderr, "cMsgRunCallbacks: waited 1 minute for cue to empty\n");
                      }
                  }
                  /* else if error */
                  else if (status != 0) {
                    err_abort(status, "Failed callback cond wait");
                  }
                  /* else woken up 'cause msg taken off cue */
                  else {
                      break;
                  }
              }
/* fprintf(stderr, "cMsgRunCallbacks: cue was full, wokenup, there's room now!\n"); */
              if (goToNextCallback) {
                continue;
              }
           }
        } /* if too many messages in cue */

        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          if (cback->messages !=0 && cback->messages%1000 == 0)
            fprintf(stderr, "           msgs = %d\n", cback->messages);
        }

        /*
         * Add this message to linked list for this callback.
         * It will now be the responsibility of message consumer
         * to free the msg allocated here.
         */       

        mutexLock(&cback->mutex);

        /* if there are no messages ... */
        if (cback->head == NULL) {
          cback->head = message;
          cback->tail = message;
        }
        /* else put message after the tail */
        else {
          cback->tail->next = message;
          cback->tail = message;
        }

        cback->messages++;
/*printf("cMsgRunCallbacks: increase cue size = %d\n", cback->messages);*/
        message->next = NULL;

        /* unlock mutex */
/* printf("cMsgRunCallbacks: messge put on cue\n");
printf("cMsgRunCallbacks: will UNLOCK mutex\n"); */
        mutexUnlock(&cback->mutex);
/* printf("cMsgRunCallbacks: mutex is UNLOCKED, msg taken off cue, broadcast to callback thd\n"); */

        /* wakeup callback thread */
        status = pthread_cond_broadcast(&cback->cond);
        if (status != 0) {
          err_abort(status, "Failed callback condition signal");
        }

      } /* search callback list */
    } /* if subscribe sub/type matches msg sub/type */
  } /* for each cback */

  subscribeMutexUnlock(domain);
  
  /* Need to free up msg allocated by client's listening thread */
  cMsgFreeMessage((void *)msg);
  
  return (CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine wakes up the appropriate sendAndGet
 * when a message arrives from the server. 
 */
int cMsgWakeGet(int domainId, cMsgMessage *msg) {

  int i, status, delivered=0;
  getInfo *info;
  cMsgDomain_CODA *domain;
  
  domain = &cMsgDomains[domainId];
   
  /* find the right get */
  for (i=0; i<MAX_SEND_AND_GET; i++) {
    
    info = &domain->sendAndGetInfo[i];

    if (info->active != 1) {
      continue;
    }
    
/*
fprintf(stderr, "cMsgWakeGets: domainId = %d, uniqueId = %d, msg sender token = %d\n",
        domainId, info->id, msg->senderToken);
*/
    /* if the id's match, wakeup the "get" for this sub/type */
    if (info->id == msg->senderToken) {
/*fprintf(stderr, "cMsgWakeGets: match with msg token %d\n", msg->senderToken);*/
      /* pass msg to "get" */
      info->msg = msg;
      info->msgIn = 1;

      /* wakeup "get" */      
      status = pthread_cond_signal(&info->cond);
      if (status != 0) {
        err_abort(status, "Failed get condition signal");
      }
      
      /* only 1 receiver gets this message */
      delivered = 1;
      break;
    }
  }
  
  if (!delivered) {
    cMsgFreeMessage((void *)msg);
  }
  
  return (CMSG_OK);
}



/*-------------------------------------------------------------------*/
/*   miscellaneous local functions                                   */
/*-------------------------------------------------------------------*/

/** This routine translates a delta time into an absolute time for pthread_cond_wait. */
static int getAbsoluteTime(const struct timespec *deltaTime, struct timespec *absTime) {
    struct timespec now;
    long   nsecTotal;
    
    if (absTime == NULL || deltaTime == NULL) {
      return CMSG_BAD_ARGUMENT;
    }
    
    clock_gettime(CLOCK_REALTIME, &now);
    nsecTotal = deltaTime->tv_nsec + now.tv_nsec;
    if (nsecTotal >= 1000000000L) {
      absTime->tv_nsec = nsecTotal - 1000000000L;
      absTime->tv_sec  = deltaTime->tv_sec + now.tv_sec + 1;
    }
    else {
      absTime->tv_nsec = nsecTotal;
      absTime->tv_sec  = deltaTime->tv_sec + now.tv_sec;
    }
    return CMSG_OK;
}


/*-------------------------------------------------------------------*/
/**
 * This routine parses, using regular expressions, the cMsg domain
 * portion of the UDL sent from the next level up" in the API.
 */
static int parseUDLregex(const char *UDL, char **password,
                              char **host, unsigned short *port,
                              char **UDLRemainder,
                              char **subdomainType,
                              char **UDLsubRemainder) {

    int        i, err, len, bufLength, Port, index;
    char       *p, *portString, *udl, *udlLowerCase, *udlRemainder, *pswd;
    char       *buffer;
    const char *pattern = "([a-zA-Z0-9\\.]+):?([0-9]+)?/?([a-zA-Z0-9]+)?/?(.*)";  
    regmatch_t matches[5]; /* we have 5 potential matches: 1 whole, 4 sub */
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
/* printf("parseUDLregex: udl remainder = %s\n", udlRemainder); */
    
    
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
    
    /* cMsg domain UDL is of the form:
     *        cMsg:cMsg://<host>:<port>/<subdomainType>/<subdomain remainder>?tag=value&tag2=value2& ...
     * 
     * the first "cMsg:" is optional. The subdomain is optional with
     * the default being cMsg.
     *
     * Remember that for this domain:
     * 1) port is not necessary
     * 2) host can be "localhost" and may also includes dots (.)
     * 3) if domainType is cMsg, subdomainType is automatically set to cMsg if not given.
     *    if subdomainType is not cMsg, it is required
     * 4) remainder is past on to the subdomain plug-in
     */

    /* compile regular expression */
    err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED);
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
    if ((unsigned int)(matches[1].rm_so) < 0) {
        /* no match for host */
        free(udl);
        free(buffer);
        return (CMSG_BAD_FORMAT);
    }
    else {
       buffer[0] = 0;
       len = matches[1].rm_eo - matches[1].rm_so;
       strncat(buffer, udlRemainder+matches[1].rm_so, len);
                
        /* if the host is "localhost", find the actual host name */
        if (strcmp(buffer, "localhost") == 0) {
/* printf("parseUDLregex: host = localhost\n"); */
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
    }
/* printf("parseUDLregex: host = %s\n", buffer); */


    /* find port */
    if (matches[2].rm_so < 0) {
        /* no match for port so use default */
        Port = CMSG_NAME_SERVER_STARTING_PORT;
        if (cMsgDebug >= CMSG_DEBUG_WARN) {
            fprintf(stderr, "parseUDLregex: guessing that the name server port is %d\n",
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
/* printf("parseUDLregex: port = %hu\n", Port ); */


    /* find subdomain */
    if (matches[3].rm_so < 0) {
        /* no match for subdomain, cMsg is default */
        if (subdomainType != NULL) {
            *subdomainType = (char *) strdup("cMsg");
        }
/* printf("parseUDLregex: subdomain = cMsg\n"); */
    }
    else {
        buffer[0] = 0;
        len = matches[3].rm_eo - matches[3].rm_so;
        strncat(buffer, udlRemainder+matches[3].rm_so, len);
                
        if (subdomainType != NULL) {
            *subdomainType = (char *) strdup(buffer);
        }        
/* printf("parseUDLregex: subdomain = %s\n", buffer); */
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
        /*buffer[0] = 0;*/
        len = matches[4].rm_eo - matches[4].rm_so;
        strncat(buffer, udlRemainder+matches[4].rm_so, len);
                
        if (UDLsubRemainder != NULL) {
            *UDLsubRemainder = (char *) strdup(buffer);
        }        
/* printf("parseUDLregex: subdomain remainder = %s\n", buffer); */
    }


    /* find cmsgpassword parameter if it exists*/
    len = strlen(buffer);
    while (len > 0) {
        /* look for ?cmsgpassword=value& or &cmsgpassword=value& */
        pattern = "[&\\?]cmsgpassword=([a-zA-Z0-9]+)&?";

        /* compile regular expression */
        err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED);
        if (err != 0) {
            break;
        }

        /* find matches */
        pswd = strdup(buffer);
        err = cMsgRegexec(&compiled, pswd, 2, matches, 0);
        if (err != 0) {
            /* no match */
            cMsgRegfree(&compiled);
            free(pswd);
            break;
        }

        /* free up memory */
        cMsgRegfree(&compiled);

        /* find password */
        if ((unsigned int)(matches[1].rm_so) >= 0) {
           buffer[0] = 0;
           len = matches[1].rm_eo - matches[1].rm_so;
           strncat(buffer, pswd+matches[1].rm_so, len);
           if (password != NULL) {
             *password = (char *) strdup(buffer);
           }        
/* printf("parseUDLregex: password = %s\n", buffer); */
        }
        
        free(pswd);
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
 * This routine parses the cMsg domain portion of the UDL sent from the 
 * "next level up" in the API.
 */
static int parseUDL(const char *UDLremainder, char **host, unsigned short *port,
                    char **subdomainType, char **UDLsubRemainder) {

  /* note: the full cMsg UDL is of the form:
   *       cMsg:<domainType>://<host>:<port>/<subdomainType>/<remainder>
   *
   * where the first "cMsg:" is optional. The subdomain is optional with
   * the default being cMsg.
   *
   * However, we're parsing the UDL with the initial  "cMsg:<domainType>://"
   * stripped off (in the software layer one up).
   */

  int   Port;
  char *p, *portString, *udl;

  if (UDLremainder == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  /* strtok modifies the string it tokenizes, so make a copy */
  udl = (char *) strdup(UDLremainder);
  
/*printf("UDL remainder = %s\n", udl);*/
    
  /* get tokens separated by ":" or "/" */
    
  /* find host */
  if ( (p = (char *) strtok(udl, ":/")) == NULL) {
    free(udl);
    return (CMSG_BAD_FORMAT);
  }
  if (host != NULL) *host =(char *) strdup(p);
/*printf("host = %s\n", p);*/
  
  
  /* find port */
  if ( (p = (char *) strtok(NULL, "/")) == NULL) {
    if (host != NULL) free(*host);
    free(udl);
    return (CMSG_BAD_FORMAT);
  }
  portString = (char *) strdup(p);
  Port = atoi(portString);
  free(portString);
  if (port != NULL) {
    *port = Port;
  }
/*printf("port string = %s, port int = %hu\n", portString, Port);*/
  if (Port < 1024 || Port > 65535) {
    if (port != NULL) free((void *) portString);
    if (host != NULL) free((void *) *host);
    free(udl);
    return (CMSG_OUT_OF_RANGE);
  }
  
  /* find subdomain */
  if ( (p = (char *) strtok(NULL, "/")) != NULL) {
    if (subdomainType != NULL) {
      *subdomainType = (char *) strdup(p);
    }
  }
  else {
    if (subdomainType != NULL) {
        *subdomainType = (char *) strdup("cMsg");
    }
/*printf("subdomainType = cMsg\n");*/
    free(udl);
    return(CMSG_OK);
  }
  
/*printf("subdomainType = %s\n", p);*/

  /* find UDL remainder's remainder */
  if ( (p = (char *) strtok(NULL, "")) != NULL) {
    if (UDLsubRemainder != NULL) {
      *UDLsubRemainder = (char *) strdup(p);
    }
/*printf("remainder = %s\n", p);*/
  }
  
  /* UDL parsed ok */
  free(udl);
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/

/**
 * This routine initializes the structure used to implement a countdown latch.
 */
static void countDownLatchInit(countDownLatch *latch, int count, int reInit) {
    int status;
    
    latch->count   = count;
    latch->waiters = 0;
    
#ifdef VXWORKS
    /* vxworks only lets us initialize mutexes and cond vars once */
    if (reInit) return;
#endif

    status = pthread_mutex_init(&latch->mutex, NULL);
    if (status != 0) {
      err_abort(status, "countDownLatchInit:initializing mutex");
    }
        
    status = pthread_cond_init(&latch->countCond, NULL);
    if (status != 0) {
      err_abort(status, "countDownLatchInit:initializing condition var");
    } 
       
    status = pthread_cond_init(&latch->notifyCond, NULL);
    if (status != 0) {
      err_abort(status, "countDownLatchInit:initializing condition var");
    }    
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees allocated memory in a structure used to implement
 * a countdown latch.
 */
static void countDownLatchFree(countDownLatch *latch) {  
#ifdef sun    
    /* cannot destroy mutexes and cond vars in vxworks & Linux(?) */
    int status;
    
    status = pthread_mutex_destroy(&latch->mutex);
    if (status != 0) {
      err_abort(status, "countDownLatchFree:destroying cond var");
    }
    
    status = pthread_cond_destroy (&latch->countCond);
    if (status != 0) {
      err_abort(status, "countDownLatchFree:destroying cond var");
    }
    
    status = pthread_cond_destroy (&latch->notifyCond);
    if (status != 0) {
      err_abort(status, "countDownLatchFree:destroying cond var");
    }
    
#endif   
}


/*-------------------------------------------------------------------*/

/**
 * This routine initializes the structure used to handle a get - 
 * either a sendAndGet or a subscribeAndGet.
 */
static void getInfoInit(getInfo *info, int reInit) {
    int status;
    
    info->id      = 0;
    info->active  = 0;
    info->error   = CMSG_OK;
    info->msgIn   = 0;
    info->quit    = 0;
    info->type    = NULL;
    info->subject = NULL;    
    info->msg     = NULL;
    
#ifdef VXWORKS
    /* vxworks only lets us initialize mutexes and cond vars once */
    if (reInit) return;
#endif

    status = pthread_cond_init(&info->cond, NULL);
    if (status != 0) {
      err_abort(status, "getInfoInit:initializing condition var");
    }
    status = pthread_mutex_init(&info->mutex, NULL);
    if (status != 0) {
      err_abort(status, "getInfoInit:initializing mutex");
    }
}


/*-------------------------------------------------------------------*/


/** This routine initializes the structure used to handle a subscribe. */
static void subscribeInfoInit(subInfo *info, int reInit) {
    int j, status;
    
    info->id      = 0;
    info->active  = 0;
    info->type    = NULL;
    info->subject = NULL;
    info->typeRegexp    = NULL;
    info->subjectRegexp = NULL;
    
    for (j=0; j<MAX_CALLBACK; j++) {
      info->cbInfo[j].active   = 0;
      info->cbInfo[j].threads  = 0;
      info->cbInfo[j].messages = 0;
      info->cbInfo[j].quit     = 0;
      info->cbInfo[j].callback = NULL;
      info->cbInfo[j].userArg  = NULL;
      info->cbInfo[j].head     = NULL;
      info->cbInfo[j].tail     = NULL;
      info->cbInfo[j].config.init          = 0;
      info->cbInfo[j].config.maySkip       = 0;
      info->cbInfo[j].config.mustSerialize = 1;
      info->cbInfo[j].config.maxCueSize    = 100;
      info->cbInfo[j].config.skipSize      = 20;
      info->cbInfo[j].config.maxThreads    = 100;
      info->cbInfo[j].config.msgsPerThread = 150;
      
#ifdef VXWORKS
      /* vxworks only lets us initialize mutexes and cond vars once */
      if (reInit) continue;
#endif

      status = pthread_cond_init (&info->cbInfo[j].cond,  NULL);
      if (status != 0) {
        err_abort(status, "subscribeInfoInit:initializing condition var");
      }
      
      status = pthread_mutex_init(&info->cbInfo[j].mutex, NULL);
      if (status != 0) {
        err_abort(status, "subscribeInfoInit:initializing mutex");
      }
    }
}


/*-------------------------------------------------------------------*/

/**
 * This routine initializes the structure used to hold connection-to-
 * a-domain information.
 */
static void domainInit(cMsgDomain_CODA *domain, int reInit) {
  int i, status;
 
  domain->id                  = 0;

  domain->initComplete        = 0;
  domain->receiveState        = 0;
  domain->gotConnection       = 0;
      
  domain->sendSocket          = 0;
  domain->receiveSocket       = 0;
  domain->listenSocket        = 0;
  domain->keepAliveSocket     = 0;
  
  domain->sendPort            = 0;
  domain->serverPort          = 0;
  domain->listenPort          = 0;
  
  domain->hasSend             = 0;
  domain->hasSyncSend         = 0;
  domain->hasSubscribeAndGet  = 0;
  domain->hasSendAndGet       = 0;
  domain->hasSubscribe        = 0;
  domain->hasUnsubscribe      = 0;
  domain->hasShutdown         = 0;

  domain->myHost              = NULL;
  domain->sendHost            = NULL;
  domain->serverHost          = NULL;
  
  domain->name                = NULL;
  domain->udl                 = NULL;
  domain->description         = NULL;
  domain->password            = NULL;
  
  domain->failovers           = NULL;
  domain->failoverSize        = 0;
  domain->failoverIndex       = 0;
  domain->implementFailovers  = 0;
  domain->resubscribeComplete = 0;
  domain->killClientThread    = 0;
  
  domain->shutdownHandler     = NULL;
  domain->shutdownUserArg     = NULL;
  
  domain->msgBuffer           = NULL;
  domain->msgBufferSize       = 0;

  domain->msgInBuffer[0]      = NULL;
  domain->msgInBuffer[1]      = NULL;
      
  countDownLatchInit(&domain->failoverLatch, 1, reInit);

  for (i=0; i<MAX_SUBSCRIBE; i++) {
    subscribeInfoInit(&domain->subscribeInfo[i], reInit);
  }
  
  for (i=0; i<MAX_SUBSCRIBE_AND_GET; i++) {
    getInfoInit(&domain->subscribeAndGetInfo[i], reInit);
  }
  
  for (i=0; i<MAX_SEND_AND_GET; i++) {
    getInfoInit(&domain->sendAndGetInfo[i], reInit);
  }

#ifdef VXWORKS
  /* vxworks only lets us initialize mutexes and cond vars once */
  if (reInit) return;
#endif

  status = pthread_mutex_init(&domain->socketMutex, NULL);
  if (status != 0) {
    err_abort(status, "domainInit:initializing socket mutex");
  }
  
  status = pthread_mutex_init(&domain->syncSendMutex, NULL);
  if (status != 0) {
    err_abort(status, "domainInit:initializing sync send mutex");
  }
  
  status = pthread_mutex_init(&domain->subscribeMutex, NULL);
  if (status != 0) {
    err_abort(status, "domainInit:initializing subscribe mutex");
  }
  
  status = pthread_cond_init (&domain->subscribeCond,  NULL);
  if (status != 0) {
    err_abort(status, "domainInit:initializing condition var");
  }
      
}


/*-------------------------------------------------------------------*/

/**
 * This routine frees allocated memory in a structure used to hold
 * subscribe information.
 */
static void subscribeInfoFree(subInfo *info) {  
#ifdef sun    
    /* cannot destroy mutexes and cond vars in vxworks & apparently Linux */
    int j, status;

    for (j=0; j<MAX_CALLBACK; j++) {
      status = pthread_cond_destroy (&info->cbInfo[j].cond);
      if (status != 0) {
        err_abort(status, "subscribeInfoFree:destroying cond var");
      }
  
      status = pthread_mutex_destroy(&info->cbInfo[j].mutex);
      if (status != 0) {
        err_abort(status, "subscribeInfoFree:destroying mutex");
      }
  
    }
#endif   
    
    if (info->type != NULL) {
      free(info->type);
    }
    if (info->subject != NULL) {
      free(info->subject);
    }
    if (info->typeRegexp != NULL) {
      free(info->typeRegexp);
    }
    if (info->subjectRegexp != NULL) {
      free(info->subjectRegexp);
    }
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees allocated memory in a structure used to hold
 * subscribeAndGet/sendAndGet information.
 */
static void getInfoFree(getInfo *info) {  
#ifdef sun    
    /* cannot destroy mutexes and cond vars in vxworks & Linux(?) */
    int status;
    
    status = pthread_cond_destroy (&info->cond);
    if (status != 0) {
      err_abort(status, "getInfoFree:destroying cond var");
    }
    
    status = pthread_mutex_destroy(&info->mutex);
    if (status != 0) {
      err_abort(status, "getInfoFree:destroying cond var");
    }
#endif
    
    if (info->type != NULL) {
      free(info->type);
    }
    
    if (info->subject != NULL) {
      free(info->subject);
    }
    
    if (info->msg != NULL) {
      cMsgFreeMessage(info->msg);
    }

}


/*-------------------------------------------------------------------*/


/**
 * This routine frees allocated memory in a structure used to hold
 * parsed UDL information.
 */
static void parsedUDLFree(parsedUDL *p) {  
       if (p->udl            != NULL) free(p->udl);
       if (p->udlRemainder   != NULL) free(p->udlRemainder);
       if (p->subdomain      != NULL) free(p->subdomain);
       if (p->subRemainder   != NULL) free(p->subRemainder);
       if (p->password       != NULL) free(p->password);
       if (p->nameServerHost != NULL) free(p->nameServerHost);    
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees memory allocated for the structure used to hold
 * connection-to-a-domain information.
 */
static void domainFree(cMsgDomain_CODA *domain) {  
  int i;
#ifdef sun
  int status;
#endif
  
  if (domain->myHost           != NULL) free(domain->myHost);
  if (domain->sendHost         != NULL) free(domain->sendHost);
  if (domain->serverHost       != NULL) free(domain->serverHost);
  if (domain->name             != NULL) free(domain->name);
  if (domain->udl              != NULL) free(domain->udl);
  if (domain->description      != NULL) free(domain->description);
  if (domain->password         != NULL) free(domain->password);
  if (domain->msgBuffer        != NULL) free(domain->msgBuffer);
  if (domain->msgInBuffer[0]   != NULL) free(domain->msgInBuffer[0]);
  if (domain->msgInBuffer[1]   != NULL) free(domain->msgInBuffer[1]);
  
  if (domain->failovers        != NULL) {
    for (i=0; i<domain->failoverSize; i++) {       
       if (domain->failovers[i].udl            != NULL) free(domain->failovers[i].udl);
       if (domain->failovers[i].udlRemainder   != NULL) free(domain->failovers[i].udlRemainder);
       if (domain->failovers[i].subdomain      != NULL) free(domain->failovers[i].subdomain);
       if (domain->failovers[i].subRemainder   != NULL) free(domain->failovers[i].subRemainder);
       if (domain->failovers[i].password       != NULL) free(domain->failovers[i].password);
       if (domain->failovers[i].nameServerHost != NULL) free(domain->failovers[i].nameServerHost);    
    }
    free(domain->failovers);
  }
  
#ifdef sun
  /* cannot destroy mutexes in vxworks & Linux(?) */
  status = pthread_mutex_destroy(&domain->socketMutex);
  if (status != 0) {
    err_abort(status, "domainFree:destroying socket mutex");
  }
  
  status = pthread_mutex_destroy(&domain->syncSendMutex);
  if (status != 0) {
    err_abort(status, "domainFree:destroying sync send mutex");
  }
  
  status = pthread_mutex_destroy(&domain->subscribeMutex);
  if (status != 0) {
    err_abort(status, "domainFree:destroying subscribe mutex");
  }
  
  status = pthread_cond_destroy (&domain->subscribeCond);
  if (status != 0) {
    err_abort(status, "domainFree:destroying cond var");
  }
    
#endif

  countDownLatchFree(&domain->failoverLatch);
    
  for (i=0; i<MAX_SUBSCRIBE; i++) {
    subscribeInfoFree(&domain->subscribeInfo[i]);
  }
  
  for (i=0; i<MAX_SUBSCRIBE_AND_GET; i++) {
    getInfoFree(&domain->subscribeAndGetInfo[i]);
  }
  
  for (i=0; i<MAX_SEND_AND_GET; i++) {
    getInfoFree(&domain->sendAndGetInfo[i]);
  }
}


/*-------------------------------------------------------------------*/

/**
 * This routine both frees and clears the structure used to hold
 * connection-to-a-domain information.
 */
static void domainClear(cMsgDomain_CODA *domain) {
  domainFree(domain);
  domainInit(domain, 1);
}

 
/*-------------------------------------------------------------------*/

/**
 * This routine waits for the given count down latch to be counted down
 * to 0 before returning. Once the count down is at 0, it notifies the
 * "latchCountDown" caller that it got the message and then returns.
 *
 * @param latch pointer to latch structure
 * @param timeout time to wait for the count down to reach 0 before returning
 *                with a timeout code (0)
 *
 * @returns -1 if the latch is being reset
 * @returns  0 if the count down has not reached 0 before timing out
 * @returns +1 if the count down has reached 0
 */
static int latchAwait(countDownLatch *latch, const struct timespec *timeout) {
  int status;
  struct timespec wait;
  
  /* Lock mutex */
  status = pthread_mutex_lock(&latch->mutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
  
  /* if latch is being reset, return -1 (error) */
  if (latch->count < 0) {
/* printf("    latchAwait: resetting so return -1\n"); */
    status = pthread_mutex_unlock(&latch->mutex);
    if (status != 0) {
      err_abort(status, "Failed mutex unlock");
    }

    return -1;  
  }
  /* if count = 0 already, return 1 (true) */
  else if (latch->count == 0) {
/* printf("    latchAwait: count is already 0 so return 1\n"); */
    status = pthread_mutex_unlock(&latch->mutex);
    if (status != 0) {
      err_abort(status, "Failed mutex unlock");
    }
    
    return 1;
  }
  
  /* We're a waiter */
  latch->waiters++;
/* printf("    latchAwait: waiters set to %d\n",latch->waiters); */
  
  /* wait until count <= 0 */
  while (latch->count > 0) {
    /* wait until signaled */
    if (timeout == NULL) {
/* printf("    latchAwait: wait forever\n"); */
      status = pthread_cond_wait(&latch->countCond, &latch->mutex);
    }
    /* wait until signaled or timeout */
    else {
      getAbsoluteTime(timeout, &wait);
/* printf("    latchAwait: timed wait\n"); */
      status = pthread_cond_timedwait(&latch->countCond, &latch->mutex, &wait);
    }
    
    /* if we've timed out, return 0 (false) */
    if (status == ETIMEDOUT) {
/* printf("    latchAwait: timed out, return 0\n"); */
      status = pthread_mutex_unlock(&latch->mutex);
      if (status != 0) {
        err_abort(status, "Failed mutex unlock");
      }

      return 0;
    }
    else if (status != 0) {
      err_abort(status, "Failed cond wait");
    }
  }
  
  /* if latch is being reset, return -1 (error) */
  if (latch->count < 0) {
/* printf("    latchAwait: resetting so return -1\n"); */
    status = pthread_mutex_unlock(&latch->mutex);
    if (status != 0) {
      err_abort(status, "Failed mutex unlock");
    }

    return -1;  
  }
  
  /* if count down reached (count == 0) ... */
  latch->waiters--;  
/* printf("    latchAwait: waiters set to %d\n",latch->waiters); */

  /* signal that we're done */
  status = pthread_cond_broadcast(&latch->notifyCond);
  if (status != 0) {
    err_abort(status, "Failed condition broadcast");
  }
/* printf("    latchAwait: broadcasted to (notified) latchCountDowner\n"); */

  /* unlock mutex */
  status = pthread_mutex_unlock(&latch->mutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
/* printf("    latchAwait: done, return 1\n"); */
  
  return 1;
}

 
/*-------------------------------------------------------------------*/


/**
 * This routine reduces the count of a count down latch by 1.
 * Once the count down is at 0, it notifies the "latchAwait" callers
 * of the fact and then waits for those callers to notify this routine
 * that they got the message. Once all the callers have done so, this
 * routine returns.
 *
 * @param latch pointer to latch structure
 * @param timeout time to wait for the "latchAwait" callers to respond
 *                before returning with a timeout code (0)
 *
 * @returns -1 if the latch is being reset
 * @returns  0 if the "latchAwait" callers have not responded before timing out
 * @returns +1 if the count down has reached 0 and all waiters have responded
 */
static int latchCountDown(countDownLatch *latch, const struct timespec *timeout) {
  int status;
  struct timespec wait;
  
  /* Lock mutex */
  status = pthread_mutex_lock(&latch->mutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
  
  /* if latch is being reset, return -1 (false) */
  if (latch->count < 0) {
/* printf("latchCountDown: resetting so return -1\n"); */
    status = pthread_mutex_unlock(&latch->mutex);
    if (status != 0) {
      err_abort(status, "Failed mutex unlock");
    }

    return -1;  
  }  
  /* if count = 0 already, return 1 (true) */
  else if (latch->count == 0) {
/* printf("latchCountDown: count = 0 so return 1\n"); */
    status = pthread_mutex_unlock(&latch->mutex);
    if (status != 0) {
      err_abort(status, "Failed mutex unlock");
    }
    
    return 1;
  }
  
  /* We're reducing the count */
  latch->count--;
/* printf("latchCountDown: count is now %d\n", latch->count); */
  
  /* if we've reached 0, signal all waiters to wake up */
  if (latch->count == 0) {
/* printf("latchCountDown: count = 0 so broadcast to waiters\n"); */
    status = pthread_cond_broadcast(&latch->countCond);
    if (status != 0) {
      err_abort(status, "Failed condition broadcast");
    }    
  }
    
  /* wait until all waiters have reported back to us that they're awake */
  while (latch->waiters > 0) {
    /* wait until signaled */
    if (timeout == NULL) {
/* printf("latchCountDown: wait for ever\n"); */
      status = pthread_cond_wait(&latch->notifyCond, &latch->mutex);
    }
    /* wait until signaled or timeout */
    else {
      getAbsoluteTime(timeout, &wait);
/* printf("latchCountDown: timed wait\n"); */
      status = pthread_cond_timedwait(&latch->notifyCond, &latch->mutex, &wait);
    }
    
    /* if we've timed out, return 0 (false) */
    if (status == ETIMEDOUT) {
/* printf("latchCountDown: timed out\n"); */
      status = pthread_mutex_unlock(&latch->mutex);
      if (status != 0) {
        err_abort(status, "Failed mutex unlock");
      }

      return 0;
    }
    else if (status != 0) {
      err_abort(status, "Failed cond wait");
    }
    
    /* if latch is being reset, return -1 (error) */
    if (latch->count < 0) {
/* printf("latchCountDown: resetting so return -1\n"); */
      status = pthread_mutex_unlock(&latch->mutex);
      if (status != 0) {
        err_abort(status, "Failed mutex unlock");
      }

      return -1;  
    }  

  }
    
  /* unlock mutex */
  status = pthread_mutex_unlock(&latch->mutex);
  if (status != 0) {
    err_abort(status, "await: Failed mutex unlock");
  }
/* printf("latchCountDown:done, return 1\n"); */
  
  return 1;
}


/*-------------------------------------------------------------------*/


/**
 * This routine resets a count down latch to a given count.
 * The latch is disabled, the "latchAwait" and "latchCountDown"
 * callers are awakened, wait some time, and finally the count is reset.
 *
 * @param latch pointer to latch structure
 * @param count number to reset the initial count of the latch to
 * @param timeout time to wait for the "latchAwait" and "latchCountDown" callers
 *                to return errors before going ahead and resetting the count
 */
static void latchReset(countDownLatch *latch, int count, const struct timespec *timeout) {
  int status;
  struct timespec wait;

  /* Lock mutex */
  status = pthread_mutex_lock(&latch->mutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
    
  /* Disable the latch */
  latch->count = -1;
/* printf("  latchReset: count set to -1\n"); */
  
  /* signal all waiters to wake up */
  status = pthread_cond_broadcast(&latch->countCond);
  if (status != 0) {
    err_abort(status, "Failed condition broadcast");
  }
     
  /* signal all countDowners to wake up */
  status = pthread_cond_broadcast(&latch->notifyCond);
  if (status != 0) {
    err_abort(status, "Failed condition broadcast");
  }      
/* printf("  latchReset: broadcasted to count & notify cond vars\n"); */
        
  /* unlock mutex */
  status = pthread_mutex_unlock(&latch->mutex);
  if (status != 0) {
    err_abort(status, "await: Failed mutex unlock");
  }
  
  /* wait the given amount for all parties to detect the reset */
  if (timeout != NULL) {
/* printf("  latchReset: sleeping\n"); */
    nanosleep(timeout, NULL);
  }
  
  /* Lock mutex again */
  status = pthread_mutex_lock(&latch->mutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
    
  /* Reset the latch */
  latch->count = count;
/* printf("  latchReset: count set to %d\n", count); */
  
  /* unlock mutex */
  status = pthread_mutex_unlock(&latch->mutex);
  if (status != 0) {
    err_abort(status, "await: Failed mutex unlock");
  }
/* printf("  latchReset: done\n"); */

}

 
/*-------------------------------------------------------------------*/

/** This routine locks the given pthread mutex. */
static void mutexLock(pthread_mutex_t *mutex) {

  int status = pthread_mutex_lock(mutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/** This routine unlocks the given pthread mutex. */
static void mutexUnlock(pthread_mutex_t *mutex) {

  int status = pthread_mutex_unlock(mutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/

/** This routine locks the pthread mutex used when creating unique id numbers. */
static void idMutexLock(void) {

  int status = pthread_mutex_lock(&generalMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/** This routine unlocks the pthread mutex used when creating unique id numbers. */
static void idMutexUnlock(void) {

  int status = pthread_mutex_unlock(&generalMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the read lock used to allow simultaneous
 * execution of cmsgd_Send, cmsgd_SyncSend, cmsgd_Subscribe, cmsgd_Unsubscribe,
 * cmsgd_SendAndGet, and cmsgd_SubscribeAndGet, but NOT allow simultaneous
 * execution of those routines with cmsgd_Connect or cmsgd_Disconnect.
 */
static void connectReadLock(void) {

  int status = rwl_readlock(&connectLock);
  if (status != 0) {
    err_abort(status, "Failed read lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the read lock used to allow simultaneous
 * execution of cmsgd_Send, cmsgd_SyncSend, cmsgd_Subscribe, cmsgd_Unsubscribe,
 * cmsgd_SendAndGet, and cmsgd_SubscribeAndGet, but NOT allow simultaneous
 * execution of those routines with cmsgd_Connect or cmsgd_Disconnect.
 */
static void connectReadUnlock(void) {

  int status = rwl_readunlock(&connectLock);
  if (status != 0) {
    err_abort(status, "Failed read unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the write lock used to allow simultaneous
 * execution of cmsgd_Send, cmsgd_SyncSend, cmsgd_Subscribe, cmsgd_Unsubscribe,
 * cmsgd_SendAndGet, and cmsgd_SubscribeAndGet, but NOT allow simultaneous
 * execution of those routines with cmsgd_Connect or cmsgd_Disconnect.
 */
static void connectWriteLock(void) {

  int status = rwl_writelock(&connectLock);
  if (status != 0) {
    err_abort(status, "Failed read lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the write lock used to allow simultaneous
 * execution of cmsgd_Send, cmsgd_SyncSend, cmsgd_Subscribe, cmsgd_Unsubscribe,
 * cmsgd_SendAndGet, and cmsgd_SubscribeAndGet, but NOT allow simultaneous
 * execution of those routines with cmsgd_Connect or cmsgd_Disconnect.
 */
static void connectWriteUnlock(void) {

  int status = rwl_writeunlock(&connectLock);
  if (status != 0) {
    err_abort(status, "Failed read unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the pthread mutex used to make network
 * communication thread-safe.
 */
static void socketMutexLock(cMsgDomain_CODA *domain) {

  int status = pthread_mutex_lock(&domain->socketMutex);
  if (status != 0) {
    err_abort(status, "Failed socket mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the pthread mutex used to make network
 * communication thread-safe.
 */
static void socketMutexUnlock(cMsgDomain_CODA *domain) {

  int status = pthread_mutex_unlock(&domain->socketMutex);
  if (status != 0) {
    err_abort(status, "Failed socket mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/** This routine locks the pthread mutex used to serialize cmsgd_SyncSend calls. */
static void syncSendMutexLock(cMsgDomain_CODA *domain) {

  int status = pthread_mutex_lock(&domain->syncSendMutex);
  if (status != 0) {
    err_abort(status, "Failed syncSend mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/** This routine unlocks the pthread mutex used to serialize cmsgd_SyncSend calls. */
static void syncSendMutexUnlock(cMsgDomain_CODA *domain) {

  int status = pthread_mutex_unlock(&domain->syncSendMutex);
  if (status != 0) {
    err_abort(status, "Failed syncSend mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the pthread mutex used to serialize
 * cmsgd_Subscribe and cmsgd_Unsubscribe calls.
 */
static void subscribeMutexLock(cMsgDomain_CODA *domain) {

  int status = pthread_mutex_lock(&domain->subscribeMutex);
  if (status != 0) {
    err_abort(status, "Failed subscribe mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the pthread mutex used to serialize
 * cmsgd_Subscribe and cmsgd_Unsubscribe calls.
 */
static void subscribeMutexUnlock(cMsgDomain_CODA *domain) {

  int status = pthread_mutex_unlock(&domain->subscribeMutex);
  if (status != 0) {
    err_abort(status, "Failed subscribe mutex unlock");
  }
}


/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/



#ifdef __cplusplus
}
#endif


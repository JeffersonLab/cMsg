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


/* system includes */
#ifdef VXWORKS
#include <vxWorks.h>
#include <taskLib.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

/* package includes */
#include "cMsgNetwork.h"
#include "cMsg.h"
#include "cMsgPrivate.h"
#include "cMsgDomain.h"
#include "errors.h"
#include "rwlock.h"


/* built-in limits */
#define MAXDOMAINS_CODA  10
#define WAIT_FOR_THREADS 10 /* seconds to wait for thread to start */
#define CALLBACK_MSQ_CUE_MAX 50000


cMsgDomain_CODA cMsgDomains[MAXDOMAINS_CODA];

/* local variables */
static int oneTimeInitialized = 0;
static int *rsIds = NULL; /* allocate an integer array to read in receiverSubscribeIds */
static int rsIdSize = 0;  /* size of rsIds array */
static int rsIdCount = 0; /* number of rsId's received */

static pthread_mutex_t initMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t generalMutex = PTHREAD_MUTEX_INITIALIZER;
/*
 * Lock to prevent connect or disconnect from being
 * simultaneously with any other function.
 */
static rwlock_t connectLock = RWL_INITIALIZER; 

/* id which uniquely defines a subject/type pair */
static int subjectTypeId = 1;


/* temp */
static int counter = 0;


/* for c++ */
#ifdef __cplusplus
extern "C" {
#endif

static int   coda_connect(char *myUDL, char *myName, char *myDescription,
                          char *UDLremainder,int *domainId);
static int   coda_send(int domainId, void *msg);
static int   syncSend(int domainId, void *msg, int *response);
static int   flush(int domainId);
static int   subscribe(int domainId, char *subject, char *type, cMsgCallback *callback,
                       void *userArg, cMsgSubscribeConfig *config);
static int   unsubscribe(int domainId, char *subject, char *type, cMsgCallback *callback);
static int   subscribeAndGet(int domainId, char *subject, char *type,
                             struct timespec *timeout, void **replyMsg);
static int   sendAndGet(int domainId, void *sendMsg, struct timespec *timeout, void **replyMsg);
static int   start(int domainId);
static int   stop(int domainId);
static int   disconnect(int domainId);

static domainFunctions functions = {coda_connect, coda_send, syncSend, flush,
                                    subscribe, unsubscribe,
                                    subscribeAndGet, sendAndGet,
                                    start, stop, disconnect};

/* cMsg domain type */
domainTypeInfo codaDomainTypeInfo = {
  "cMsg",
  &functions
};


/* in cMsgServer.c */
void *cMsgClientListeningThread(void *arg);


/* local prototypes */
static int   getHostAndPortFromNameServer(cMsgDomain_CODA *domain, int serverfd,
                                          char *subdomain, char *UDLremainder);
/* mutexes and read/write locks */
static void  mutexLock(void);
static void  mutexUnlock(void);
static void  connectReadLock(void);
static void  connectReadUnlock(void);
static void  connectWriteLock(void);
static void  connectWriteUnlock(void);
static int   socketMutexLock(cMsgDomain_CODA *domain);
static int   socketMutexUnlock(cMsgDomain_CODA *domain);
static int   syncSendMutexLock(cMsgDomain_CODA *domain);
static int   syncSendMutexUnlock(cMsgDomain_CODA *domain);
static int   subscribeMutexLock(cMsgDomain_CODA *domain);
static int   subscribeMutexUnlock(cMsgDomain_CODA *domain);

/* threads */
static void *keepAliveThread(void *arg);
static void *callbackThread(void *arg);
static void *supplementalThread(void *arg);

/* initialization and freeing */
static void  domainInit(cMsgDomain_CODA *domain);
static void  domainFree(cMsgDomain_CODA *domain);  
static void  domainClear(cMsgDomain_CODA *domain);
static void  getInfoInit(getInfo *info);
static void  subscribeInfoInit(subscribeInfo *info);
static void  getInfoFree(getInfo *info);
static void  subscribeInfoFree(subscribeInfo *info);
static void  getInfoClear(getInfo *info);
static void  subscribeInfoClear(subscribeInfo *info);

/* misc */
static int   parseUDL(const char *UDLremainder, char **host, unsigned short *port,
                      char **subdomainType, char **UDLsubRemainder);
static int   unget(int domainId, int id);
static struct timespec getAbsoluteTime(struct timespec *deltaTime);

/*-------------------------------------------------------------------*/


static int coda_connect(char *myUDL, char *myName, char *myDescription,
                        char *UDLremainder, int *domainId) {

  int i, id=-1, err, serverfd, status, hz, num_try, try_max;
  char *portEnvVariable=NULL, temp[CMSG_MAXHOSTNAMELEN];
  char *subdomain=NULL, *UDLsubRemainder=NULL;
  unsigned short startingPort;
  mainThreadInfo *threadArg;
  struct timespec waitForThread;
  
    
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
    for (i=0; i<MAXDOMAINS_CODA; i++) domainInit(&cMsgDomains[i]);
    
    /* allocate array to read in receiverSubscribeIds */
    rsIds = (int *) calloc(100, sizeof(int));
    if (rsIds == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "coda_connect: cannot allocate memory\n");
      }
      exit(1);
    }
    rsIdSize = 100;
    oneTimeInitialized = 1;
  }

  
  /* Find an existing connection to this domain if possible.
   * This is not really necessary. Since cMsgConnect already
   * returns an existing connection for the same name, udl, and
   * description.
   */
  for (i=0; i<MAXDOMAINS_CODA; i++) {
    if (cMsgDomains[i].initComplete == 1   &&
        cMsgDomains[i].name        != NULL &&
        cMsgDomains[i].udl         != NULL)  {
        
      if ( (strcmp(cMsgDomains[i].name, myName) == 0)  &&
           (strcmp(cMsgDomains[i].udl,  myUDL ) == 0) )  {
        /* got a match */
        id = i;
        break;
      }  
    }
  }
  

  /* found the id of a valid connection - return that */
  if (id > -1) {
    *domainId = id;
    connectWriteUnlock();
    return(CMSG_OK);
  }
  

  /* no existing connection, so find the first available place in the "cMsgDomains" array */
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
  
  /* Parse the UDL (Uniform Domain Locator) remainder
   * passed down from the level above.
   */
  if ( (err = parseUDL(UDLremainder, &cMsgDomains[id].serverHost,
                       &cMsgDomains[id].serverPort, &subdomain, &UDLsubRemainder)) != CMSG_OK ) {
    /* there's been a parsing error */
    domainClear(&cMsgDomains[id]);
    connectWriteUnlock();
    return(err);
  }
  
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
      fprintf(stderr, "coda_connect: cannot find CMSG_PORT env variable, first try port %hu\n", startingPort);
    }
  }
  else {
    i = atoi(portEnvVariable);
    if (i < 1025 || i > 65535) {
      startingPort = CMSG_CLIENT_LISTENING_PORT;
      if (cMsgDebug >= CMSG_DEBUG_WARN) {
        fprintf(stderr, "coda_connect: CMSG_PORT contains a bad port #, first try port %hu\n", startingPort);
      }
    }
    else {
      startingPort = i;
    }
  }

  /* get listening port and socket for this application */
  if ( (err = cMsgGetListeningSocket(CMSG_NONBLOCKING,
                                     startingPort,
                                     &cMsgDomains[id].listenPort,
                                     &cMsgDomains[id].listenSocket)) != CMSG_OK) {
    domainClear(&cMsgDomains[id]);
    connectWriteUnlock();
    return(err);
  }

  /* launch pend thread and start listening on receive socket */
  threadArg = (mainThreadInfo *) malloc(sizeof(mainThreadInfo));
  threadArg->isRunning = 0;
  threadArg->domainId  = id;
  threadArg->listenFd  = cMsgDomains[id].listenSocket;
  threadArg->blocking  = CMSG_NONBLOCKING;
  status = pthread_create(&cMsgDomains[id].pendThread, NULL,
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
   
  /* get system clock rate - probably 100 Hz */
  hz = 100;
  hz = sysconf(_SC_CLK_TCK);
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
      fprintf(stderr, "coda_connect, cannot start listening thread\n");
    }
    exit(-1);
  }
     
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_connect: created listening thread\n");
  }
  
  /*---------------------------------------------------------------*/
  /* connect & talk to cMsg name server to check if name is unique */
  /*---------------------------------------------------------------*/
    
  /* first connect to server host & port */
  if ( (err = cMsgTcpConnect(cMsgDomains[id].serverHost,
                             cMsgDomains[id].serverPort,
                             &serverfd)) != CMSG_OK) {
    /* stop listening & connection threads */
    pthread_cancel(cMsgDomains[id].pendThread);
    domainClear(&cMsgDomains[id]);
    connectWriteUnlock();
    return(err);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_connect: connected to name server\n");
  }
  
  /* get host & port to send messages to */
  err = getHostAndPortFromNameServer(&cMsgDomains[id], serverfd, subdomain, UDLsubRemainder);
  if (err != CMSG_OK) {
    close(serverfd);
    pthread_cancel(cMsgDomains[id].pendThread);
    domainClear(&cMsgDomains[id]);
    connectWriteUnlock();
    return(err);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_connect: got host and port from name server\n");
  }
  
  /* done talking to server */
  close(serverfd);
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_connect: closed name server socket\n");
    fprintf(stderr, "coda_connect: sendHost = %s, sendPort = %hu\n",
                             cMsgDomains[id].sendHost,
                             cMsgDomains[id].sendPort);
  }
  
  /* create sending socket and store */
  if ( (err = cMsgTcpConnect(cMsgDomains[id].sendHost,
                             cMsgDomains[id].sendPort,
                             &cMsgDomains[id].sendSocket)) != CMSG_OK) {
    close(serverfd);
    pthread_cancel(cMsgDomains[id].pendThread);
    domainClear(&cMsgDomains[id]);
    connectWriteUnlock();
    return(err);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_connect: created sending socket fd = %d\n", cMsgDomains[id].sendSocket);
  }
  
  /* init is complete */
  *domainId = id ;

  if ( (err = cMsgTcpConnect(cMsgDomains[id].sendHost,
                             cMsgDomains[id].sendPort,
                             &cMsgDomains[id].keepAliveSocket)) != CMSG_OK) {
    close(cMsgDomains[id].sendSocket);
    pthread_cancel(cMsgDomains[id].pendThread);
    domainClear(&cMsgDomains[id]);
    connectWriteUnlock();
    return(err);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_connect: created keepalive socket fd = %d\n",cMsgDomains[id].keepAliveSocket );
  }
  
  /* create thread to send periodic keep alives and handle dead server */
  status = pthread_create(&cMsgDomains[id].keepAliveThread, NULL,
                          keepAliveThread, (void *)&cMsgDomains[id]);
  if (status != 0) {
    err_abort(status, "Creating keep alive thread");
  }
     
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_connect: created keep alive thread\n");
  }
  
  cMsgDomains[id].lostConnection = 0;
    
  /* no more mutex protection is necessary */
  connectWriteUnlock();
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int coda_send(int domainId, void *vmsg) {
  
  int err, lenSubject, lenType, lenText;
  int outGoing[8];
  char *subject, *type, *text;
  cMsgMessage *msg = (cMsgMessage *) vmsg;
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  int fd = domain->sendSocket;
    
  if (!domain->hasSend) {
    return(CMSG_NOT_IMPLEMENTED);
  }
  
  /* Cannot run this while connecting/disconnecting */
  connectReadLock();
  
  if (cMsgDomains[domainId].initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (cMsgDomains[domainId].lostConnection == 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
  }

  subject = cMsgGetSubject(vmsg);
  type    = cMsgGetType(vmsg);
  text    = cMsgGetText(vmsg);

  /* message id (in network byte order) to domain server */
  outGoing[0] = htonl(CMSG_SEND_REQUEST);
  /* is get response? */
  outGoing[1] = htonl(msg->getResponse);
  /* sender id */
  outGoing[2] = htonl(msg->senderId);
  /* time message sent (right now) */
  outGoing[3] = htonl((int) time(NULL));
  /* sender message id */
  outGoing[4] = htonl(msg->senderMsgId);

  /* length of "subject" string */
  lenSubject  = strlen(subject);
  outGoing[5] = htonl(lenSubject);
  /* length of "type" string */
  lenType     = strlen(type);
  outGoing[6] = htonl(lenType);
  /* length of "text" string */
  lenText     = strlen(text);
  outGoing[7] = htonl(lenText);

  /* make send socket communications thread-safe */
  socketMutexLock(domain);
  
  /* send ints over together */
  if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
    socketMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "coda_send: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* send subject */
  if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
    socketMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "coda_send: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* send type */
  if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
    socketMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "coda_send: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* send text */
  if (cMsgTcpWrite(fd, (void *) text, lenText) != lenText) {
    socketMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "coda_send: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* done protecting communications */
  socketMutexUnlock(domain);
  connectReadUnlock();

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int syncSend(int domainId, void *vmsg, int *response) {
  
  int err, lenSubject, lenType, lenText;
  int outGoing[8];
  char *subject, *type, *text;
  cMsgMessage *msg = (cMsgMessage *) vmsg;
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  int fd = domain->sendSocket;
    
  if (!domain->hasSyncSend) {
    return(CMSG_NOT_IMPLEMENTED);
  }
 
  connectReadLock();

  if (cMsgDomains[domainId].initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (cMsgDomains[domainId].lostConnection == 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
  }

  subject = cMsgGetSubject(vmsg);
  type    = cMsgGetType(vmsg);
  text    = cMsgGetText(vmsg);

  /* message id (in network byte order) to domain server */
  outGoing[0] = htonl(CMSG_SYNC_SEND_REQUEST);
  /* is get response? */
  outGoing[1] = htonl(msg->getResponse);
  /* sender id */
  outGoing[2] = htonl(msg->senderId);
  /* time message sent (right now) */
  outGoing[3] = htonl((int) time(NULL));
  /* sender message id */
  outGoing[4] = htonl(msg->senderMsgId);

  /* length of "subject" string */
  lenSubject  = strlen(subject);
  outGoing[5] = htonl(lenSubject);
  /* length of "type" string */
  lenType     = strlen(type);
  outGoing[6] = htonl(lenType);
  /* length of "text" string */
  lenText     = strlen(text);
  outGoing[7] = htonl(lenText);

  /* make syncSends be synchronous 'cause we need a reply */
  syncSendMutexLock(domain);
 
  /* make outgoing socket communications thread-safe */
  socketMutexLock(domain);
 
  /* send ints over together */
  if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
    socketMutexUnlock(domain);
    syncSendMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "syncSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
 
  /* send subject */
  if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
    socketMutexUnlock(domain);
    syncSendMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "syncSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* send type */
  if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
    socketMutexUnlock(domain);
    syncSendMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "syncSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send text */
  if (cMsgTcpWrite(fd, (void *) text, lenText) != lenText) {
    socketMutexUnlock(domain);
    syncSendMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "syncSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* done protecting outgoing communications */
  socketMutexUnlock(domain);
  
  /* now read reply */
  if (cMsgTcpRead(fd, (void *) &err, sizeof(err)) != sizeof(err)) {
    socketMutexUnlock(domain);
    syncSendMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "syncSend: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  syncSendMutexUnlock(domain);
  connectReadUnlock();

  /* return domain server's reply */  
  *response = ntohl(err);  
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int subscribeAndGet(int domainId, char *subject, char *type,
                           struct timespec *timeout, void **replyMsg) {
                             
  cMsgDomain_CODA *domain  = &cMsgDomains[domainId];
  int i, uniqueId, status, lenSubject, lenType;
  int gotSpot, fd = domain->sendSocket;
  int outGoing[4];
  getInfo *info;
  struct timespec wait;
  
  if (!domain->hasSubscribeAndGet) {
    return(CMSG_NOT_IMPLEMENTED);
  }   
           
  connectReadLock();

  if (cMsgDomains[domainId].initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (cMsgDomains[domainId].lostConnection == 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
  }
  
  /*
   * Pick a unique identifier for the subject/type pair, and
   * send it to the domain server & remember it for future use
   * Mutex protect this operation as many coda_connect calls may
   * operate in parallel on this static variable.
   */
  mutexLock();
  uniqueId = subjectTypeId++;
  mutexUnlock();

  /* make new entry and notify server */
  gotSpot = 0;

  for (i=0; i<MAX_GENERAL_GET; i++) {
    if (domain->generalGetInfo[i].active != 0) {
      continue;
    }

    info = &domain->generalGetInfo[i];
    info->id      = uniqueId;
    info->active  = 1;
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
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* notify domain server */

  /* message id (in network byte order) to domain server */
  outGoing[0] = htonl(CMSG_SUBSCRIBE_AND_GET_REQUEST);
  /* unique id for receiverSubscribeId */
  outGoing[1] = htonl(uniqueId);
  
  /* length of "subject" string */
  lenSubject  = strlen(subject);
  outGoing[2] = htonl(lenSubject);
  /* length of "type" string */
  lenType     = strlen(type);
  outGoing[3] = htonl(lenType);

  /* make send socket communications thread-safe */
  socketMutexLock(domain);
  
  /* send ints over together */
  if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
    socketMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "get: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* send subject */
  if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
    socketMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "get: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send type */
  if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
    socketMutexUnlock(domain);
    connectReadUnlock();
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
      wait = getAbsoluteTime(timeout);
      status = pthread_cond_timedwait(&info->cond, &info->mutex, &wait);
    }
    
    if (status == ETIMEDOUT) {
      info->msgIn = 1;
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

  /*
   * Check the message stored for us. If msg is null, we timed out.
   * Tell server to forget the get.
   */
  if (info->msg == NULL) {
      printf("get: timed out\n");
      
      /* free up memory */
      free(info->subject);
      free(info->type);
      info->active = 0;

      /* remove the get from server */
      unget(domainId, uniqueId);
      *replyMsg = NULL;
      return (CMSG_TIMEOUT);
  }

  /* If msg is not null... */

  /*
   * Don't need to make a copy of message as only 1 receipient.
   * Message was allocated in client's listening thread and user
   * must free it.
   */
  *replyMsg = info->msg;
  if (*replyMsg == NULL) {
    printf("get: out of memory\n");
    exit(-1);
  }
  
  /* free up memory */
  free(info->subject);
  free(info->type);
  info->active = 0;

  /*printf("get: SUCCESS!!!\n");*/

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int sendAndGet(int domainId, void *sendMsg, struct timespec *timeout,
                      void **replyMsg) {
  
  cMsgDomain_CODA *domain  = &cMsgDomains[domainId];
  char *subject, *type, *text;
  cMsgMessage *msg = (cMsgMessage *) sendMsg;
  int i, uniqueId, status, lenSubject, lenType, lenText;
  int gotSpot, fd = domain->sendSocket;
  int outGoing[8];
  getInfo *info;
  struct timespec wait;
  
  if (!domain->hasSendAndGet) {
    return(CMSG_NOT_IMPLEMENTED);
  }   
           
  subject = cMsgGetSubject(sendMsg);
  type    = cMsgGetType(sendMsg);
  text    = cMsgGetText(sendMsg);

  connectReadLock();

  if (cMsgDomains[domainId].initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (cMsgDomains[domainId].lostConnection == 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
  }

  /* watch out for null text */
  if (text == NULL) {
      msg->text = (char *)strdup("");
  }
  
  /*
   * Pick a unique identifier for the subject/type pair, and
   * send it to the domain server & remember it for future use
   * Mutex protect this operation as many coda_connect calls may
   * operate in parallel on this static variable.
   */
  mutexLock();
  uniqueId = subjectTypeId++;
  mutexUnlock();

  /* make new entry and notify server */
  gotSpot = 0;

  for (i=0; i<MAX_SPECIFIC_GET; i++) {
    if (domain->specificGetInfo[i].active != 0) {
      continue;
    }

    info = &domain->specificGetInfo[i];
    info->id      = uniqueId;
    info->active  = 1;
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
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* notify domain server */

  /* message id (in network byte order) to domain server */
  outGoing[0] = htonl(CMSG_SEND_AND_GET_REQUEST);
  /* sender id */
  outGoing[1] = htonl(msg->senderId);
  /* time message sent (right now) */
  outGoing[2] = htonl((int) time(NULL));
  /* sender message id */
  outGoing[3] = htonl(msg->senderMsgId);
  /* unique id for sender token */
  outGoing[4] = htonl(uniqueId);

  /* length of "subject" string */
  lenSubject  = strlen(subject);
  outGoing[5] = htonl(lenSubject);
  /* length of "type" string */
  lenType     = strlen(type);
  outGoing[6] = htonl(lenType);
  /* length of "text" string */
  lenText     = strlen(text);
  outGoing[7] = htonl(lenText);

  /* make send socket communications thread-safe */
  socketMutexLock(domain);
  
  /* send ints over together */
  if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
    socketMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "get: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* send subject */
  if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
    socketMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "get: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send type */
  if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
    socketMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "get: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* send text */
  if (cMsgTcpWrite(fd, (void *) text, lenText) != lenText) {
    socketMutexUnlock(domain);
    connectReadUnlock();
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
      wait = getAbsoluteTime(timeout);
      status = pthread_cond_timedwait(&info->cond, &info->mutex, &wait);
    }
    
    if (status == ETIMEDOUT) {
      info->msgIn = 1;
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

  /*
   * Check the message stored for us. If msg is null, we timed out.
   * Tell server to forget the get.
   */
  if (info->msg == NULL) {
      printf("get: timed out\n");
      
      /* free up memory */
      free(info->subject);
      free(info->type);
      info->active = 0;

      /* remove the get from server */
      unget(domainId, uniqueId);
      *replyMsg = NULL;
      return (CMSG_TIMEOUT);
  }

  /* If msg is not null... */

  /*
   * Don't need to make a copy of message as only 1 receipient.
   * Message was allocated in client's listening thread and user
   * must free it.
   */
  *replyMsg = info->msg;
  if (*replyMsg == NULL) {
    printf("get: out of memory\n");
    exit(-1);
  }
  
  /* free up memory */
  free(info->subject);
  free(info->type);
  info->active = 0;

  /*printf("get: SUCCESS!!!\n");*/

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int unget(int domainId, int id) {
  
  int outGoing[2];
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  int fd = domain->sendSocket;
    
  /* message id (in network byte order) to domain server */
  outGoing[0] = htonl(CMSG_UNGET_REQUEST);
  /* receiverSubscribe or senderToken id */
  outGoing[1] = htonl(id);

  /* make send socket communications thread-safe */
  socketMutexLock(domain);
  
  /* send ints over together */
  if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
    socketMutexUnlock(domain);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "coda_send: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
 
  socketMutexUnlock(domain);

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int flush(int domainId) {

  FILE *file;  
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  int fd = domain->sendSocket;

  if (cMsgDomains[domainId].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (cMsgDomains[domainId].lostConnection == 1) return(CMSG_LOST_CONNECTION);

  /* turn file descriptor into FILE pointer */
  file = fdopen(fd, "w");

  /* make send socket communications thread-safe */
  socketMutexLock(domain);
  /* flush outgoing buffers */
  fflush(file);
  /* done with mutex */
  socketMutexUnlock(domain);
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int subscribe(int domainId, char *subject, char *type, cMsgCallback *callback,
                     void *userArg, cMsgSubscribeConfig *config) {

  int i, j, iok, jok, uniqueId, status;
  cMsgDomain_CODA *domain  = &cMsgDomains[domainId];
  subscribeConfig *sConfig = (subscribeConfig *) config;

  if (!domain->hasSubscribe) {
    return(CMSG_NOT_IMPLEMENTED);
  } 
  
  connectReadLock();

  if (cMsgDomains[domainId].initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (cMsgDomains[domainId].lostConnection == 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
  }

  /* use default configuration if none given */
  if (config == NULL) {
    sConfig = (subscribeConfig *) cMsgSubscribeConfigCreate();
  }

  /* make sure subscribe and unsubscribe are not run at the same time */
  subscribeMutexLock(domain);
  
  /* add to callback list if subscription to same subject/type exists */
  iok = 0;
  for (i=0; i<MAX_SUBSCRIBE; i++) {
    if ((domain->subscribeInfo[i].active == 1) && 
       (strcmp(domain->subscribeInfo[i].subject, subject) == 0) && 
       (strcmp(domain->subscribeInfo[i].type, type) == 0) ) {
      iok = 1;

      jok = 0;
      for (j=0; j<MAXCALLBACK; j++) {
	if (domain->subscribeInfo[i].cbInfo[j].callback == NULL) {
	  domain->subscribeInfo[i].cbInfo[j].callback = callback;
	  domain->subscribeInfo[i].cbInfo[j].userArg  = userArg;
          domain->subscribeInfo[i].cbInfo[0].head     = NULL;
          domain->subscribeInfo[i].cbInfo[0].tail     = NULL;
          domain->subscribeInfo[i].cbInfo[0].quit     = 0;
          domain->subscribeInfo[i].cbInfo[0].messages = 0;
          domain->subscribeInfo[i].cbInfo[0].config   = *sConfig;
          
          /* start callback thread now */
          status = pthread_create(&domain->subscribeInfo[i].cbInfo[j].thread,
                                  NULL, callbackThread,
                                  (void *) &domain->subscribeInfo[i].cbInfo[j]);
          if (status != 0) {
            err_abort(status, "Creating callback thread");
          }
          
          /* release allocated memory */
          if (config == NULL) {
            cMsgSubscribeConfigDestroy((cMsgSubscribeConfig *) sConfig);
          }
          
	  jok = 1;
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
    if (domain->subscribeInfo[i].active != 0) {
      continue;
    }

    int err, lenSubject, lenType;
    int fd = domain->sendSocket;
    int outGoing[4];
    
    domain->subscribeInfo[i].active  = 1;
    domain->subscribeInfo[i].subject = (char *) strdup(subject);
    domain->subscribeInfo[i].type    = (char *) strdup(type);
    domain->subscribeInfo[i].cbInfo[0].callback = callback;
    domain->subscribeInfo[i].cbInfo[0].userArg  = userArg;
    domain->subscribeInfo[i].cbInfo[0].head     = NULL;
    domain->subscribeInfo[i].cbInfo[0].tail     = NULL;
    domain->subscribeInfo[i].cbInfo[0].quit     = 0;
    domain->subscribeInfo[i].cbInfo[0].messages = 0;
    domain->subscribeInfo[i].cbInfo[0].config   = *sConfig;
    
    /* start callback thread now */
    status = pthread_create(&domain->subscribeInfo[i].cbInfo[0].thread,
                            NULL, callbackThread,
                            (void *) &domain->subscribeInfo[i].cbInfo[0]);
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
     * Mutex protect this operation as many coda_connect calls may
     * operate in parallel on this static variable.
     */
    mutexLock();
    uniqueId = subjectTypeId++;
    mutexUnlock();
    domain->subscribeInfo[i].id = uniqueId;

    /* notify domain server */

    /* message id (in network byte order) to domain server */
    outGoing[0] = htonl(CMSG_SUBSCRIBE_REQUEST);
    /* unique id to domain server */
    outGoing[1] = htonl(uniqueId);
    /* length of "subject" string */
    lenSubject  = strlen(subject);
    outGoing[2] = htonl(lenSubject);
    /* length of "type" string */
    lenType     = strlen(type);
    outGoing[3] = htonl(lenType);

    /* make send socket communications thread-safe */
    socketMutexLock(domain);

    /* send ints over together */
    if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
      socketMutexUnlock(domain);
      subscribeMutexUnlock(domain);
      connectReadUnlock();
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "subscribe: write failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }

    /* send subject */
    if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
      socketMutexUnlock(domain);
      subscribeMutexUnlock(domain);
      connectReadUnlock();
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "subscribe: write failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }

    /* send type */
    if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
      socketMutexUnlock(domain);
      subscribeMutexUnlock(domain);
      connectReadUnlock();
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "subscribe: write failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }

    /* done protecting communications */
    socketMutexUnlock(domain);
    /* done protecting subscribe */
    subscribeMutexUnlock(domain);
    connectReadUnlock();

    return(CMSG_OK);
  }
  
  /* done protecting subscribe */
  subscribeMutexUnlock(domain);
  connectReadUnlock();
  
  /* iok == 0 here */
  return(CMSG_OUT_OF_MEMORY);
}


/*-------------------------------------------------------------------*/


static int unsubscribe(int domainId, char *subject, char *type, cMsgCallback *callback) {

  int i, j;
  int cbCount = 0;     /* total number of callbacks for the subject/type pair of interest */
  int cbsRemoved = 0;  /* total number of callbacks removed for that subject/type pair */
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  
  if (!domain->hasUnsubscribe) {
    return(CMSG_NOT_IMPLEMENTED);
  }
 
  connectReadLock();

  if (cMsgDomains[domainId].initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (cMsgDomains[domainId].lostConnection == 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
  }

  /* make sure subscribe and unsubscribe are not run at the same time */
  subscribeMutexLock(domain);
  
  /* search entry list */
  for (i=0; i<MAX_SUBSCRIBE; i++) {
    /* if there is a match with subject & type ... */
    if ( (domain->subscribeInfo[i].active == 1) && 
         (strcmp(domain->subscribeInfo[i].subject, subject) == 0)  && 
         (strcmp(domain->subscribeInfo[i].type,    type)    == 0) )  {
            
      /* search callback list */
      for (j=0; j<MAXCALLBACK; j++) {
	if (domain->subscribeInfo[i].cbInfo[j].callback != NULL) {
	  cbCount++;
          if (domain->subscribeInfo[i].cbInfo[j].callback == callback) {
            domain->subscribeInfo[i].cbInfo[j].callback == NULL;
            cbsRemoved++;
          }
	}
      }
      break;
    }
  }


  /* delete entry and notify server if there was at least 1 callback
   * to begin with and now there are none for this subject/type */
  if ((cbCount > 0) && (cbCount-cbsRemoved < 1)) {

    int err, lenSubject, lenType;
    int fd = domain->sendSocket;
    int outGoing[4];

    domain->subscribeInfo[i].active = 0;
    free(domain->subscribeInfo[i].subject);
    free(domain->subscribeInfo[i].type);

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "unsubscribe: send 4 ints\n");
    }

    /* notify server */

    /* message id (in network byte order) to domain server */
    outGoing[0] = htonl(CMSG_UNSUBSCRIBE_REQUEST);
    /* unique id associated with subject/type */
    outGoing[1] = htonl(domain->subscribeInfo[i].id);
    /* length of "subject" string */
    lenSubject  = strlen(subject);
    outGoing[2] = htonl(lenSubject);
    /* length of "type" string */
    lenType     = strlen(type);
    outGoing[3] = htonl(lenType);

    /* make send socket communications thread-safe */
    socketMutexLock(domain);

    /* send ints over together */
    if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
      socketMutexUnlock(domain);
      subscribeMutexUnlock(domain);
      connectReadUnlock();
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "unsubscribe: write failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }

    /* send subject */
    if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
      socketMutexUnlock(domain);
      subscribeMutexUnlock(domain);
      connectReadUnlock();
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "unsubscribe: write failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }

    /* send type */
    if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
      socketMutexUnlock(domain);
      subscribeMutexUnlock(domain);
      connectReadUnlock();
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "unsubscribe: write failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }

    /* done protecting communications */
    socketMutexUnlock(domain);
    /* done protecting unsubscribe */
    subscribeMutexUnlock(domain);
    connectReadUnlock();

    return(CMSG_OK);
  }

  /* done protecting unsubscribe */
  subscribeMutexUnlock(domain);
  connectReadUnlock();
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int start(int domainId) {
  
  cMsgDomains[domainId].receiveState = 1;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int stop(int domainId) {
  
  cMsgDomains[domainId].receiveState = 0;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int disconnect(int domainId) {
  
  int status, out;
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  int fd = domain->sendSocket;

  if (cMsgDomains[domainId].initComplete != 1) return(CMSG_NOT_INITIALIZED);
  
  /* When changing initComplete / connection status, protect it */
  connectWriteLock();
  
  /* message id (in network byte order) to domain server */
  out = htonl(CMSG_SERVER_DISCONNECT);

  /* make send socket communications thread-safe */
  socketMutexLock(domain);
  
  /* send ints over together */
  if (cMsgTcpWrite(fd, (void *) &out, sizeof(out)) != sizeof(out)) {
    socketMutexUnlock(domain);
    connectWriteUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "disconnect: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
 
  socketMutexUnlock(domain);

  cMsgDomains[domainId].lostConnection = 1;
  
  /* close sending and listening sockets */
  close(domain->sendSocket);
  close(domain->listenSocket);
  close(domain->keepAliveSocket);

  /* stop listening and client communication threads */
  status = pthread_cancel(domain->pendThread);
  if (status != 0) {
    err_abort(status, "Cancelling message listening & client threads");
  }
  
  /* stop keep alive thread */
  status = pthread_cancel(domain->keepAliveThread);
  if (status != 0) {
    err_abort(status, "Cancelling keep alive thread");
  }

  /* reset vars, free memory */
  domainClear(domain);
  
  connectWriteUnlock();

  return(CMSG_OK);
}



/*-------------------------------------------------------------------*/


static int getHostAndPortFromNameServer(cMsgDomain_CODA *domain, int serverfd,
                                        char *subdomain, char *UDLremainder) {

  int  err, lengthDomain, lengthSubdomain, lengthRemainder;
  int  lengthHost, lengthName, lengthUDL, lengthDescription;
  int  outgoing[9], incoming[2];
  char temp[CMSG_MAXHOSTNAMELEN], atts[6];
  char *domainType = "cMsg";

  /* first send message id (in network byte order) to server */
  outgoing[0] = htonl(CMSG_SERVER_CONNECT);
  /* send my listening port (as an int) to server */
  outgoing[1] = htonl((int)domain->listenPort);
  /* send length of the type of domain server I'm expecting to connect to.*/
  lengthDomain = strlen(domainType);
  outgoing[2]  = htonl(lengthDomain);
  /* send length of the type of subdomain handler I'm expecting to use.*/
  lengthSubdomain = strlen(subdomain);
  outgoing[3] = htonl(lengthSubdomain);
  /* send length of the UDL remainder.*/
  /* this may be null */
  if (UDLremainder == NULL) {
    lengthRemainder = outgoing[4] = 0;
  }
  else {
    lengthRemainder = strlen(UDLremainder);
    outgoing[4] = htonl(lengthRemainder);
  }
  /* send length of my host name to server */
  lengthHost  = strlen(domain->myHost);
  outgoing[5] = htonl(lengthHost);
  /* send length of my name to server */
  lengthName  = strlen(domain->name);
  outgoing[6] = htonl(lengthName);
  /* send length of my udl to server */
  lengthUDL   = strlen(domain->udl);
  outgoing[7] = htonl(lengthUDL);
  /* send length of my description to server */
  lengthDescription  = strlen(domain->description);
  outgoing[8] = htonl(lengthDescription);
    
  /* first send all the ints */
  if (cMsgTcpWrite(serverfd, (void *) outgoing, sizeof(outgoing)) != sizeof(outgoing)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
    
  /* send the type of domain server I'm expecting to connect to */
  if (cMsgTcpWrite(serverfd, (void *) domainType, lengthDomain) != lengthDomain) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* send the type of subdomain handler I'm expecting to use */
  if (cMsgTcpWrite(serverfd, (void *) subdomain, lengthSubdomain) != lengthSubdomain) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send the UDL remainder */
  if (UDLremainder != NULL) {
    if (cMsgTcpWrite(serverfd, (void *) UDLremainder, lengthRemainder) != lengthRemainder) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }
  }

  /* send my host name to server */
  if (cMsgTcpWrite(serverfd, (void *) domain->myHost, lengthHost) != lengthHost) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send my name to server */
  if (cMsgTcpWrite(serverfd, (void *) domain->name, lengthName) != lengthName) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* send my udl to server */
  if (cMsgTcpWrite(serverfd, (void *) domain->udl, lengthUDL) != lengthUDL) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
 
  /* send my description to server */
  if (cMsgTcpWrite(serverfd, (void *) domain->description, lengthDescription) != lengthDescription) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
 
  /* now read server reply */
  if (cMsgTcpRead(serverfd, (void *) &err, sizeof(err)) != sizeof(err)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  err = ntohl(err);
    
  /* if there's an error, quit */
  if (err != CMSG_OK) {
    return(err);
  }
  
  /*
   * if everything's OK, we expect to get:
   *   1) attributes of subdomain handler
   *   2) host & port
   */
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: read subdomain handler attributes\n");
  }
  
  /* read 6 chars */
  if (cMsgTcpRead(serverfd, (void *) atts, sizeof(atts)) != sizeof(atts)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: read failure\n");
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
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: read port and length of host from server\n");
  }
  
  /* read port & length of host name to send to*/
  if (cMsgTcpRead(serverfd, (void *) &incoming, sizeof(incoming)) != sizeof(incoming)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  domain->sendPort = (unsigned short) ntohl(incoming[0]);
  lengthHost = ntohl(incoming[1]);

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: port = %hu, host len = %d\n",
              domain->sendPort, lengthHost);
    fprintf(stderr, "getHostAndPortFromNameServer: read host from server\n");
  }
  
  /* read host name to send to */
  if (cMsgTcpRead(serverfd, (void *) temp, lengthHost) != lengthHost) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  /* be sure to null-terminate string */
  temp[lengthHost] = 0;
  domain->sendHost = (char *) strdup(temp);
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: host = %s\n", domain->sendHost);
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*
 * keepAliveThread is a thread used to send keep alive packets
 * to other cMsg-enabled programs. If there is no response or there is
 * an I/O error. The other end of the socket is presumed dead.
 *-------------------------------------------------------------------*/
static void *keepAliveThread(void *arg)
{
    cMsgDomain_CODA *domain = (cMsgDomain_CODA *) arg;
    int domainId = domain->id;
    int socket   = domain->keepAliveSocket;
    int request, alive, err;

    /* increase concurrency for this thread for early Solaris */
  #ifdef sun
    int  con;
    con = thr_getconcurrency();
    thr_setconcurrency(con + 1);
  #endif

    /* periodically send a keep alive message and read response */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "keepAliveThread: keep alive thread created, socket = %d\n", socket);
    }
  
    /* request to send */
    request = htonl(CMSG_KEEP_ALIVE);
    
    /* keep checking to see if the server/agent is alive */
    while(1) {
       if (cMsgDebug >= CMSG_DEBUG_INFO) {
         fprintf(stderr, "keepAliveThread: send keep alive request\n");
       }
       
       if (cMsgTcpWrite(socket, &request, sizeof(request)) != sizeof(request)) {
         if (cMsgDebug >= CMSG_DEBUG_ERROR) {
           fprintf(stderr, "keepAliveThread: error writing request\n");
         }
         break;
       }

       /* read response */
       if (cMsgDebug >= CMSG_DEBUG_INFO) {
         fprintf(stderr, "keepAliveThread: read keep alive response\n");
       }
       
       if ((err = cMsgTcpRead(socket, (void *) &alive, sizeof(alive))) != sizeof(alive)) {
         if (cMsgDebug >= CMSG_DEBUG_ERROR) {
           fprintf(stderr, "keepAliveThread: read failure\n");
         }
         break;
       }
       
       /* sleep for 3 seconds and try again */
       sleep(3);
    }
    
    /* if we've reach here, there's an error, do a disconnect */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "keepAliveThread: server is probably dead, disconnect\n");
    }
    cMsgDisconnect(domainId);
    
  #ifdef sun
    thr_setconcurrency(con);
  #endif
    
    return;
}


/*-------------------------------------------------------------------*
 * callbackThread is a thread used to run a single callback in.
 *-------------------------------------------------------------------*/
static void *callbackThread(void *arg)
{
    /* subscription information passed in thru arg */
    subscribeCbInfo *subscription = (subscribeCbInfo *) arg;
    int i, status, need, threadsAdded, maxToAdd, wantToAdd;
    int numMsgs, numThreads;
    cMsgMessage *msg;
    pthread_t thd;

    /* increase concurrency for this thread for early Solaris */
  #ifdef sun
    int  con;
    con = thr_getconcurrency();
    thr_setconcurrency(con + 1);
  #endif
        
    threadsAdded = 0;

    while(1) {
      /*
       * Take a current snapshot of the number of threads and messages.
       * The number of threads may decrease since threads die if there
       * are no messages to grab, but this is the only place that the
       * number of threads will be increased.
       */
      numMsgs = subscription->messages;
      numThreads = subscription->threads;
      threadsAdded = 0;
      
      /* Check to see if we need more threads to handle the load */      
      if ((!subscription->config.mustSerialize) &&
          (numThreads < subscription->config.maxThreads) &&
          (numMsgs > subscription->config.msgsPerThread)) {

        /* find number of threads needed (1 per 50 messages) */
        need = subscription->messages/subscription->config.msgsPerThread;

        /* add more threads if necessary */
        if (need > numThreads) {
          
          /* maximum # of threads that can be added w/o exceeding config limit */
          maxToAdd  = subscription->config.maxThreads - numThreads;
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
      status = pthread_mutex_lock(&subscription->mutex);
      if (status != 0) {
        err_abort(status, "Failed callback mutex lock");
      }
      
      /* do the following bookkeeping under mutex protection */
      subscription->threads += threadsAdded;

      if (threadsAdded) {
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "thds = %d\n", subscription->threads);
        }
      }
      
      /* wait while there are no messages */
      while (subscription->head == NULL) {
        /* wait until signaled */
        status = pthread_cond_wait(&subscription->cond, &subscription->mutex);
        if (status != 0) {
          err_abort(status, "Failed callback cond wait");
        }
        
        /* quit if commanded to */
        if (subscription->quit) {
          goto end;
        }
      }

      /*printf("     %d\n",subscription->messages);*/
      
      /* get first message in linked list */
      msg = subscription->head;

      /* if there are no more messages ... */
      if (msg->next == NULL) {
        subscription->head = NULL;
        subscription->tail = NULL;
      }
      /* else make the next message the head */
      else {
        subscription->head = msg->next;
     }
     subscription->messages--;
     
      /* unlock mutex */
      status = pthread_mutex_unlock(&subscription->mutex);
      if (status != 0) {
        err_abort(status, "Failed callback mutex unlock");
      }
      
      /* run callback */
      subscription->callback(msg, subscription->userArg);
      
      /* quit if commanded to */
      if (subscription->quit) {
        goto end;
      }
    }
    
  end:
          
  #ifdef sun
    thr_setconcurrency(con);
  #endif
    
    return;
}



/*-------------------------------------------------------------------*
 * supplementalThread is a thread used to run a callback in parallel
 * with the callbackThread. As many supplemental threads are created
 * as needed to keep the cue size manageable.
 *-------------------------------------------------------------------*/
static void *supplementalThread(void *arg)
{
    /* subscription information passed in thru arg */
    subscribeCbInfo *subscription = (subscribeCbInfo *) arg;
    int status, empty;
    cMsgMessage *msg;
    struct timespec wait, timeout;
    
    /* increase concurrency for this thread for early Solaris */
  #ifdef sun
    int  con;
    con = thr_getconcurrency();
    thr_setconcurrency(con + 1);
  #endif

    /* wait .2 sec before waking thread up and checking for messages */
    timeout.tv_sec  = 0;
    timeout.tv_nsec = 200000000;

    while(1) {
      
      empty = 0;
      
      /* lock mutex before messing with linked list */
      status = pthread_mutex_lock(&subscription->mutex);
      if (status != 0) {
        err_abort(status, "Failed callback mutex lock");
      }
      
      /* wait while there are no messages */
      while (subscription->head == NULL) {
        /* wait until signaled or for .2 sec, before
         * waking thread up and checking for messages
         */
        wait = getAbsoluteTime(&timeout);        
        status = pthread_cond_timedwait(&subscription->cond, &subscription->mutex, &wait);
        
        /* if the wait timed out ... */
        if (status == ETIMEDOUT) {
          /* if we wake up 10 times with no messages (2 sec), quit this thread */
          if (++empty%10 == 0) {
            subscription->threads--;
            if (cMsgDebug >= CMSG_DEBUG_INFO) {
              fprintf(stderr, "thds = %d\n", subscription->threads);
            }
            
            /* unlock mutex & kill this thread */
            status = pthread_mutex_unlock(&subscription->mutex);
            if (status != 0) {
              err_abort(status, "Failed callback mutex unlock");
            }
            
  #ifdef sun
            thr_setconcurrency(con);
  #endif
            return;
          }

        }
        else if (status != 0) {
          err_abort(status, "Failed callback cond wait");
        }
        
        /* quit if commanded to */
        if (subscription->quit) {
          goto end;
        }
      }

      /*printf("  S  %d\n",subscription->messages );*/
                  
      /* get first message in linked list */
      msg = subscription->head;      

      /* if there are no more messages ... */
      if (msg->next == NULL) {
        subscription->head = NULL;
        subscription->tail = NULL;
      }
      /* else make the next message the head */
      else {
        subscription->head = msg->next;
      }
      subscription->messages--;
     
      /* unlock mutex */
      status = pthread_mutex_unlock(&subscription->mutex);
      if (status != 0) {
        err_abort(status, "Failed callback mutex unlock");
      }
      
      /* run callback */
      subscription->callback(msg, subscription->userArg);
      
      /* quit if commanded to */
      if (subscription->quit) {
        goto end;
      }
    }
    
  end:
          
  #ifdef sun
    thr_setconcurrency(con);
  #endif
    
    return;
}


/*-------------------------------------------------------------------*/


int cMsgRunCallbacks(int domainId, cMsgMessage *msg) {

  int i, j, k, ii, status;
  dispatchCbInfo *dcbi;
  subscribeCbInfo *subscription;
  getInfo *info;
  pthread_t newThread;
  cMsgDomain_CODA *domain;
  cMsgMessage *message, *oldHead;
  
  domain = &cMsgDomains[domainId];
  
  /* callbacks have been stopped */
  if (domain->receiveState == 0) {
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cMsgRunCallbacks: all callbacks have been stopped\n");
    }
    free(msg);
    return (CMSG_OK);
  }
 
  /* for each matching id from server ... */
  for (ii=0; ii < rsIdCount; ii++) {

    /* search entry list */
    for (i=0; i<MAX_SUBSCRIBE; i++) {
      /* if subscription not active, forget about it */
      if (domain->subscribeInfo[i].active != 1) {
        continue;
      }

      /* if the subject/type id's match, run callbacks for this sub/type */
/*
fprintf(stderr, "cMsgRunCallbacks: cli id = %d, msg id = %d\n",
domain->subscribeInfo[i].id, rsIds[ii]);
*/
      if (domain->subscribeInfo[i].id == rsIds[ii]) {

/*fprintf(stderr, "cMsgRunCallbacks: match with msg id %d\n", rsIds[ii]);*/
        /* search callback list */
        for (j=0; j<MAXCALLBACK; j++) {
	  /* if there is an existing callback ... */
          if (domain->subscribeInfo[i].cbInfo[j].callback != NULL) {
/*fprintf(stderr, "cMsgRunCallbacks: there is a callback\n");*/

            /* copy message so each callback has its own copy */
            message = (cMsgMessage *) cMsgCopyMessage((void *)msg);

            /* convenience variable */
            subscription = &domain->subscribeInfo[i].cbInfo[j];

            /* lock mutex before messing with linked list */
            status = pthread_mutex_lock(&subscription->mutex);
            if (status != 0) {
              err_abort(status, "Failed callback mutex lock");
            }

            /* check to see if there are too many messages in the cue */
            if (subscription->messages > subscription->config.maxCueSize) {
                /* if we may skip messages, dump oldest */
                if (subscription->config.maySkip) {
                    for (k=0; k < subscription->config.skipSize; k++) {
                      oldHead = subscription->head;
                      subscription->head = subscription->head->next;
                      cMsgFreeMessage(oldHead);
                      subscription->messages--;
                      if (subscription->head == NULL) break;
                    }
                }
                else {
                  /* unlock mutex */
                  status = pthread_mutex_unlock(&subscription->mutex);
                  if (status != 0) {
                    err_abort(status, "Failed callback mutex unlock");
                  }
                  cMsgFreeMessage((void *)message);
                  cMsgFreeMessage((void *)msg);
                  return CMSG_LIMIT_EXCEEDED;
                }
            }

            if (cMsgDebug >= CMSG_DEBUG_INFO) {
              if (subscription->messages%1000 == 0)
                fprintf(stderr, "           msgs = %d\n", subscription->messages);
            }

            /* add this message to linked list for this callback */       

            /* if there are no messages ... */
            if (subscription->head == NULL) {
              subscription->head = message;
              subscription->tail = message;
            }
            /* else put message after the tail */
            else {
              subscription->tail->next = message;
              subscription->tail = message;
            }

            subscription->messages++;
            message->next = NULL;

            /* unlock mutex */
            status = pthread_mutex_unlock(&subscription->mutex);
            if (status != 0) {
              err_abort(status, "Failed callback mutex unlock");
            }

            /* wakeup callback thread */
            status = pthread_cond_signal(&subscription->cond);
            if (status != 0) {
              err_abort(status, "Failed callback condition signal");
            }
	  }
        } /* search callback list */
/* fprintf(stderr, "                : got match \n");*/
        /*break;*/
      } /* if subscribe id matches id from server */      
    } /* for each subscription */
  
    /* find any matching general gets */
    for (j=0; j<MAX_GENERAL_GET; j++) {
      if (domain->generalGetInfo[j].active != 1) {
        continue;
      }

      info = &domain->generalGetInfo[j];
/*
fprintf(stderr, "cMsgRunCallbacks G: domainId = %d, uniqueId = %d, msg id = %d\n",
          domainId, info->id, rsIds[ii]);
*/
      /* if the id's match, wakeup the "get" for this sub/type */
      if (info->id == rsIds[ii]) {
/*fprintf(stderr, "cMsgRunCallbacks G: match with msg id = %d\n", rsIds[ii]);*/
        /* pass msg to "get" */
        /* copy message so each callback has its own copy */
        message = (cMsgMessage *) cMsgCopyMessage((void *)msg);

        info->msg = message;
        info->msgIn = 1;

        /* wakeup "get" */      
        status = pthread_cond_signal(&info->cond);
        if (status != 0) {
          err_abort(status, "Failed get condition signal");
        }
      }
    }
    
    
  } /* for each id from server */

      
      


  cMsgFreeMessage((void *)msg);
  
  return (CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgWakeGets(int domainId, cMsgMessage *msg) {

  int i, j, status;
  getInfo *info;
  cMsgDomain_CODA *domain;
  cMsgMessage *message, *oldHead;
  
  domain = &cMsgDomains[domainId];
  
  /* find the right get */
  for (i=0; i<MAX_SPECIFIC_GET; i++) {
    if (domain->specificGetInfo[i].active != 1) {
      continue;
    }
    
    info = &domain->specificGetInfo[i];
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
    }
  }
  
  return (CMSG_OK);
}


/*-------------------------------------------------------------------*/

/*
 * This routine is called by a single thread spawned from the client's
 * listening thread. Since it's called serially, it can safely use
 * arrays declared at the top of the file.
 */
int cMsgReadMessage(int fd, cMsgMessage *msg) {
  
  int i, err, time, lengths[5], inComing[13];
  int memSize = CMSG_MESSAGE_SIZE;
  char *string, storage[CMSG_MESSAGE_SIZE + 1];
  
  /* Start out with an array of size CMSG_MESSAGE_SIZE + 1
   * for storing strings, If that's too small, allocate more. */
  string = storage;
  
  /* read ints first */
  if (cMsgTcpRead(fd, (void *) inComing, sizeof(inComing)) != sizeof(inComing)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: cannot read ints\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* swap to local endian */
  msg->sysMsgId            = ntohl(inComing[0]);  /*  */
  msg->getRequest          = ntohl(inComing[1]);  /*  */
  msg->getResponse         = ntohl(inComing[2]);  /*  */
  msg->senderId            = ntohl(inComing[3]);  /*  */
  msg->senderTime = (time_t) ntohl(inComing[4]);  /* time in sec since Jan 1, 1970 */
  msg->senderMsgId         = ntohl(inComing[5]);  /*  */
  msg->senderToken         = ntohl(inComing[6]);  /*  */
  lengths[0]               = ntohl(inComing[7]);  /* sender length */
  lengths[1]               = ntohl(inComing[8]);  /* senderHost length */
  lengths[2]               = ntohl(inComing[9]);  /* subject length */
  lengths[3]               = ntohl(inComing[10]); /* type length */
  lengths[4]               = ntohl(inComing[11]); /* text length */
  rsIdCount                = ntohl(inComing[12]); /* # of receiverSubscribeIds to follow */
  
  /* make sure there's enough room to read all rsIds */
  if (rsIdSize < rsIdCount) {
    free(rsIds);
    rsIds = (int *) calloc(rsIdCount, sizeof(int));
    if (rsIds == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "cMsgReadMessage: cannot allocate memory\n");
      }
      exit(1);
    }
    rsIdSize = rsIdCount;
  }
  
  /* read rsIds */
  if (cMsgTcpRead(fd, (void *) rsIds, (size_t) (sizeof(int)*rsIdCount)) != sizeof(int)*rsIdCount) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: cannot read ints\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* swap to local endian */
  for (i=0; i < rsIdCount; i++) {
     rsIds[i] = ntohl(rsIds[i]);
  } 
     
  /*--------------------*/
  /* read sender string */
  /*--------------------*/
  if (lengths[0] > memSize) {
    /* free any previously allocated memory */
    if (memSize > CMSG_MESSAGE_SIZE) {
      free((void *) string);
    }
    /* allocate more memory to accomodate larger string */
    memSize = lengths[0] + 1;
    string  = (char *) malloc((size_t) memSize);
    if (string == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "cMsgReadMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[0]) != lengths[0]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: cannot read sender\n");
    }
    if (memSize > CMSG_MESSAGE_SIZE) {
      free((void *) string);
    }
    return(CMSG_NETWORK_ERROR);
  }
  /* add null terminator to C string */
  string[lengths[0]] = 0;
  /* copy to cMsg structure */
  msg->sender = (char *) strdup(string);
  
  /*
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "    sender = %s\n", string);
  }  
  */
    
  /*------------------------*/
  /* read senderHost string */
  /*------------------------*/
  if (lengths[1] > memSize) {
    if (memSize > CMSG_MESSAGE_SIZE) {
      free((void *) string);
    }
    memSize = lengths[1] + 1;
    string  = (char *) malloc((size_t) memSize);
    if (string == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "cMsgReadMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[1]) != lengths[1]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: cannot read senderHost\n");
    }
    if (memSize > CMSG_MESSAGE_SIZE) {
      free((void *) string);
    }
    free((void *) msg->sender);
    return(CMSG_NETWORK_ERROR);
  }
  string[lengths[1]] = 0;
  msg->senderHost = (char *) strdup(string);
    
  /*
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "    sender host = %s\n", string);
  }  
  */
  
  /*---------------------*/
  /* read subject string */
  /*---------------------*/
  if (lengths[2] > memSize) {
    if (memSize > CMSG_MESSAGE_SIZE) {
      free((void *) string);
    }
    memSize = lengths[2] + 1;
    string  = (char *) malloc((size_t) memSize);
    if (string == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "cMsgReadMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[2]) != lengths[2]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: cannot read senderHost\n");
    }
    if (memSize > CMSG_MESSAGE_SIZE) {
      free((void *) string);
    }
    free((void *) msg->sender);
    free((void *) msg->senderHost);
    return(CMSG_NETWORK_ERROR);
  }
  string[lengths[2]] = 0;
  msg->subject = (char *) strdup(string);
  
  /*
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "    subject = %s\n", string);
  }  
  */ 
  
  /*------------------*/
  /* read type string */
  /*------------------*/
  if (lengths[3] > memSize) {
    if (memSize > CMSG_MESSAGE_SIZE) {
      free((void *) string);
    }
    memSize = lengths[3] + 1;
    string  = (char *) malloc((size_t) memSize);
    if (string == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "cMsgReadMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[3]) != lengths[3]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: cannot read senderHost\n");
    }
    if (memSize > CMSG_MESSAGE_SIZE) {
      free((void *) string);
    }
    free((void *) msg->sender);
    free((void *) msg->senderHost);
    free((void *) msg->subject);
    return(CMSG_NETWORK_ERROR);
  }
  string[lengths[3]] = 0;
  msg->type = (char *) strdup(string);
  
  /*
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "    type = %s\n", string);
  }  
  */ 
  
  /*------------------*/
  /* read text string */
  /*------------------*/
  if (lengths[4] > memSize) {
    if (memSize > CMSG_MESSAGE_SIZE) {
      free((void *) string);
    }
    memSize = lengths[4] + 1;
    string  = (char *) malloc((size_t) memSize);
    if (string == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "cMsgReadMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[4]) != lengths[4]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: cannot read senderHost\n");
    }
    if (memSize > CMSG_MESSAGE_SIZE) {
      free((void *) string);
    }
    free((void *) msg->sender);
    free((void *) msg->senderHost);
    free((void *) msg->subject);
    free((void *) msg->type);
    return(CMSG_NETWORK_ERROR);
  }
  string[lengths[4]] = 0;
  msg->text = (char *) strdup(string);
  
  /*
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "    text = %s\n", string);
  }
  */ 
      
  return(CMSG_OK);
}



/*-------------------------------------------------------------------*/
/*   miscellaneous local functions                                   */
/*-------------------------------------------------------------------*/

/* translate a delta time into an absolute time for pthread_cond_wait */
static struct timespec getAbsoluteTime(struct timespec *deltaTime) {
    struct timeval now;
    struct timespec absTime;
    long   nsecTotal;
    
    gettimeofday(&now, NULL);
    nsecTotal = deltaTime->tv_nsec + 1000*now.tv_usec;
    if (nsecTotal >= 1000000000L) {
      absTime.tv_nsec = nsecTotal - 1000000000L;
      absTime.tv_sec  = deltaTime->tv_sec + now.tv_sec + 1;
    }
    else {
      absTime.tv_nsec = nsecTotal;
      absTime.tv_sec  = deltaTime->tv_sec + now.tv_sec;
    }
    return absTime;
}


/*-------------------------------------------------------------------*/
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

  int i, Port;
  char *p, *portString, *udl, *pdomainType;

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


static void getInfoInit(getInfo *info) {
    info->id      = 0;
    info->active  = 0;
    info->msgIn   = 0;
    info->quit    = 0;
    info->type    = NULL;
    info->subject = NULL;    
    info->msg     = NULL;
    /* pthread_mutex_init mallocs memory */
    pthread_cond_init(&info->cond, NULL);
    pthread_mutex_init(&info->mutex, NULL);
}


/*-------------------------------------------------------------------*/


static void subscribeInfoInit(subscribeInfo *info) {
    int j;
    
    info->id      = 0;
    info->active  = 0;
    info->type    = NULL;
    info->subject = NULL;
    
    for (j=0; j<MAXCALLBACK; j++) {
      info->cbInfo[j].threads  = 0;
      info->cbInfo[j].messages = 0;
      info->cbInfo[j].callback = NULL;
      info->cbInfo[j].userArg  = NULL;
      info->cbInfo[j].head     = NULL;
      info->cbInfo[j].tail     = NULL;
      info->cbInfo[j].config.init          = 0;
      info->cbInfo[j].config.maySkip       = 0;
      info->cbInfo[j].config.mustSerialize = 1;
      info->cbInfo[j].config.maxCueSize    = 10000;
      info->cbInfo[j].config.skipSize      = 2000;
      info->cbInfo[j].config.maxThreads    = 100;
      info->cbInfo[j].config.msgsPerThread = 150;
      
      /* pthread_mutex_init mallocs memory */
      pthread_cond_init (&info->cbInfo[j].cond,  NULL);
      pthread_mutex_init(&info->cbInfo[j].mutex, NULL);
    }
}


/*-------------------------------------------------------------------*/


static void domainInit(cMsgDomain_CODA *domain) {
  int i, j;
 
  domain->id                 = 0;

  domain->initComplete       = 0;
  domain->receiveState       = 0;
  domain->lostConnection     = 1;
      
  domain->sendSocket         = 0;
  domain->listenSocket       = 0;
  domain->keepAliveSocket    = 0;
  
  domain->sendPort           = 0;
  domain->serverPort         = 0;
  domain->listenPort         = 0;
  
  domain->hasSend            = 0;
  domain->hasSyncSend        = 0;
  domain->hasSubscribeAndGet = 0;
  domain->hasSendAndGet      = 0;
  domain->hasSubscribe       = 0;
  domain->hasUnsubscribe     = 0;

  domain->myHost             = NULL;
  domain->sendHost           = NULL;
  domain->serverHost         = NULL;
  
  domain->name               = NULL;
  domain->udl                = NULL;
  domain->description        = NULL;

  /* pthread_mutex_init mallocs memory */
  pthread_mutex_init(&domain->socketMutex, NULL);
  pthread_mutex_init(&domain->syncSendMutex, NULL);
  pthread_mutex_init(&domain->subscribeMutex, NULL);
  
  for (i=0; i<MAX_SUBSCRIBE; i++) {
    subscribeInfoInit(&domain->subscribeInfo[i]);
  }
  
  for (i=0; i<MAX_GENERAL_GET; i++) {
    getInfoInit(&domain->generalGetInfo[i]);
  }
  
  for (i=0; i<MAX_SPECIFIC_GET; i++) {
    getInfoInit(&domain->specificGetInfo[i]);
  }
}


/*-------------------------------------------------------------------*/


static void subscribeInfoFree(subscribeInfo *info) {  
    int j;
    
    if (info->type != NULL) {
      free(info->type);
    }
    if (info->subject != NULL) {
      free(info->subject);
    }
    
    for (j=0; j<MAXCALLBACK; j++) {
      pthread_cond_destroy (&info->cbInfo[j].cond);
      pthread_mutex_destroy(&info->cbInfo[j].mutex);
    }    
}


/*-------------------------------------------------------------------*/


static void getInfoFree(getInfo *info) {  
    if (info->type != NULL) {
      free(info->type);
    }
    if (info->subject != NULL) {
      free(info->subject);
    }
    /*
    if (info->msg != NULL) {
      cMsgFreeMessage(info->msg);
    }
    */
    pthread_cond_destroy (&info->cond);
    pthread_mutex_destroy(&info->mutex);
}


/*-------------------------------------------------------------------*/


static void domainFree(cMsgDomain_CODA *domain) {  
  int i, j;
  
  if (domain->myHost      != NULL) free(domain->myHost);
  if (domain->sendHost    != NULL) free(domain->sendHost);
  if (domain->serverHost  != NULL) free(domain->serverHost);
  if (domain->name        != NULL) free(domain->name);
  if (domain->udl         != NULL) free(domain->udl);
  if (domain->description != NULL) free(domain->description);
  
  /* pthread_mutex_destroy frees memory */
  pthread_mutex_destroy(&domain->socketMutex);
  pthread_mutex_destroy(&domain->syncSendMutex);
  pthread_mutex_destroy(&domain->subscribeMutex);
  
  for (i=0; i<MAX_SUBSCRIBE; i++) {
    subscribeInfoFree(&domain->subscribeInfo[i]);
  }
  
  for (i=0; i<MAX_GENERAL_GET; i++) {
    getInfoFree(&domain->generalGetInfo[i]);
  }
  
  for (i=0; i<MAX_SPECIFIC_GET; i++) {
    getInfoFree(&domain->specificGetInfo[i]);
  }
}


/*-------------------------------------------------------------------*/


static void getInfoClear(getInfo *info) {
  getInfoFree(info);
  getInfoInit(info);
}


/*-------------------------------------------------------------------*/


static void subscribeInfoClear(subscribeInfo *info) {
  subscribeInfoFree(info);
  subscribeInfoInit(info);
}


/*-------------------------------------------------------------------*/


static void domainClear(cMsgDomain_CODA *domain) {
  domainFree(domain);
  domainInit(domain);
}

 
/*-------------------------------------------------------------------*/


static void mutexLock(void) {

  int status = pthread_mutex_lock(&generalMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static void mutexUnlock(void) {

  int status = pthread_mutex_unlock(&generalMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


static void connectReadLock(void) {

  int status = rwl_readlock(&connectLock);
  if (status != 0) {
    err_abort(status, "Failed read lock");
  }
}


/*-------------------------------------------------------------------*/


static void connectReadUnlock(void) {

  int status = rwl_readunlock(&connectLock);
  if (status != 0) {
    err_abort(status, "Failed read unlock");
  }
}


/*-------------------------------------------------------------------*/


static void connectWriteLock(void) {

  int status = rwl_writelock(&connectLock);
  if (status != 0) {
    err_abort(status, "Failed read lock");
  }
}


/*-------------------------------------------------------------------*/


static void connectWriteUnlock(void) {

  int status = rwl_writeunlock(&connectLock);
  if (status != 0) {
    err_abort(status, "Failed read unlock");
  }
}


/*-------------------------------------------------------------------*/


static int socketMutexLock(cMsgDomain_CODA *domain) {

  int status;
  
  status = pthread_mutex_lock(&domain->socketMutex);
  if (status != 0) {
    err_abort(status, "Failed socket mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static int socketMutexUnlock(cMsgDomain_CODA *domain) {

  int status;

  status = pthread_mutex_unlock(&domain->socketMutex);
  if (status != 0) {
    err_abort(status, "Failed socket mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


static int syncSendMutexLock(cMsgDomain_CODA *domain) {

  int status;
  
  status = pthread_mutex_lock(&domain->syncSendMutex);
  if (status != 0) {
    err_abort(status, "Failed syncSend mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static int syncSendMutexUnlock(cMsgDomain_CODA *domain) {

  int status;

  status = pthread_mutex_unlock(&domain->syncSendMutex);
  if (status != 0) {
    err_abort(status, "Failed syncSend mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


static int subscribeMutexLock(cMsgDomain_CODA *domain) {

  int status;
  
  status = pthread_mutex_lock(&domain->subscribeMutex);
  if (status != 0) {
    err_abort(status, "Failed subscribe mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static int subscribeMutexUnlock(cMsgDomain_CODA *domain) {

  int status;

  status = pthread_mutex_unlock(&domain->subscribeMutex);
  if (status != 0) {
    err_abort(status, "Failed subscribe mutex unlock");
  }
}


/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/



#ifdef __cplusplus
}
#endif


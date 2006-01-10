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


/* package includes */
#include "cMsgNetwork.h"
#include "cMsgPrivate.h"
#include "cMsgBase.h"
#include "cMsgDomain.h"
#include "errors.h"
#include "rwlock.h"



/* built-in limits */
/** Maximum number of domains for each client to connect to at once. */
#define MAXDOMAINS_CODA  10
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
static int   codaConnect(const char *myUDL, const char *myName, const char *myDescription,
                         const char *UDLremainder,int *domainId);
static int   codaSend(int domainId, void *msg);
static int   codaSyncSend(int domainId, void *msg, int *response);
static int   codaFlush(int domainId);
static int   codaSubscribe(int domainId, const char *subject, const char *type, cMsgCallback *callback,
                           void *userArg, cMsgSubscribeConfig *config);
static int   codaUnsubscribe(int domainId, const char *subject, const char *type, cMsgCallback *callback,
                             void *userArg);
static int   codaSubscribeAndGet(int domainId, const char *subject, const char *type,
                                 const struct timespec *timeout, void **replyMsg);
static int   codaSendAndGet(int domainId, void *sendMsg, const struct timespec *timeout,
                            void **replyMsg);
static int   codaStart(int domainId);
static int   codaStop(int domainId);
static int   codaDisconnect(int domainId);
static int   codaSetShutdownHandler(int domainId, cMsgShutdownHandler *handler, void *userArg);
static int   codaShutdown(int domainId, const char *client, const char *server, int flag);


/** List of the functions which implement the standard cMsg tasks in the cMsg domain. */
static domainFunctions functions = {codaConnect, codaSend,
                                    codaSyncSend, codaFlush,
                                    codaSubscribe, codaUnsubscribe,
                                    codaSubscribeAndGet, codaSendAndGet,
                                    codaStart, codaStop, codaDisconnect,
                                    codaShutdown, codaSetShutdownHandler};

/* cMsg domain type */
domainTypeInfo codaDomainTypeInfo = {
  "cMsg",
  &functions
};


/** Function in cMsgServer.c which implements the network listening thread of a client. */
void *cMsgClientListeningThread(void *arg);


/* local prototypes */

/* mutexes and read/write locks */
static void  mutexLock(void);
static void  mutexUnlock(void);
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

/* misc */
static int   talkToNameServer(cMsgDomain_CODA *domain, int serverfd,
                                          char *subdomain, char *UDLremainder);
static int   parseUDL(const char *UDLremainder, char **host, unsigned short *port,
                      char **subdomainType, char **UDLsubRemainder);
static int   unSendAndGet(int domainId, int id);
static int   unSubscribeAndGet(int domainId, const char *subject,
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
 * This routine is called once to connect to a cMsg domain. It is called
 * by the user through cMsgConnect() given the appropriate UDL.
 * The argument "myUDL" is the Universal Domain Locator used to uniquely
 * identify the cMsg server to connect to. It has the form:<p>
 *       <b>cMsg:cMsg://host:port/subdomainType/remainder</b><p>
 * where the first "cMsg:" is optional. The subdomain is optional with
 * the default being cMsg.
 * The argument "myName" is the client's name and may be required to be
 * unique depending on the subdomainType.
 * The argument "myDescription" is an arbitrary string used to describe the
 * client.
 * If successful, this routine fills the argument "domainId", which identifies
 * the connection uniquely and is required as an argument by many other routines.
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
static int codaConnect(const char *myUDL, const char *myName, const char *myDescription,
                       const char *UDLremainder, int *domainId) {

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
    /*memset((void *) cMsgDomains, 0, MAXDOMAINS_CODA*sizeof(cMsgDomain_CODA));*/
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
      fprintf(stderr, "codaConnect: cannot find CMSG_PORT env variable, first try port %hu\n", startingPort);
    }
  }
  else {
    i = atoi(portEnvVariable);
    if (i < 1025 || i > 65535) {
      startingPort = CMSG_CLIENT_LISTENING_PORT;
      if (cMsgDebug >= CMSG_DEBUG_WARN) {
        fprintf(stderr, "codaConnect: CMSG_PORT contains a bad port #, first try port %hu\n", startingPort);
      }
    }
    else {
      startingPort = i;
    }
  }

  /* get listening port and socket for this application */
  if ( (err = cMsgGetListeningSocket(CMSG_BLOCKING,
                                     startingPort,
                                     &cMsgDomains[id].listenPort,
                                     &cMsgDomains[id].listenSocket)) != CMSG_OK) {
    domainClear(&cMsgDomains[id]);
    connectWriteUnlock();
    free(subdomain);
    free(UDLsubRemainder);
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
      fprintf(stderr, "codaConnect, cannot start listening thread\n");
    }
    exit(-1);
  }
       
  /* free mem allocated for the argument passed to listening thread */
  free(threadArg);
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "codaConnect: created listening thread\n");
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
    free(subdomain);
    free(UDLsubRemainder);
    return(err);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "codaConnect: connected to name server\n");
  }
  
  /* get host & port to send messages to */
  err = talkToNameServer(&cMsgDomains[id], serverfd, subdomain, UDLsubRemainder);
  if (err != CMSG_OK) {
    close(serverfd);
    pthread_cancel(cMsgDomains[id].pendThread);
    domainClear(&cMsgDomains[id]);
    connectWriteUnlock();
    free(subdomain);
    free(UDLsubRemainder);
    return(err);
  }
  
  /* free up memory allocated in parseUDL & no longer needed */
  free(subdomain);
  free(UDLsubRemainder);

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "codaConnect: got host and port from name server\n");
  }
  
  /* done talking to server */
  close(serverfd);
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "codaConnect: closed name server socket\n");
    fprintf(stderr, "codaConnect: sendHost = %s, sendPort = %hu\n",
                             cMsgDomains[id].sendHost,
                             cMsgDomains[id].sendPort);
  }
  
  /* create receiving socket and store */
  if ( (err = cMsgTcpConnect(cMsgDomains[id].sendHost,
                             cMsgDomains[id].sendPort,
                             &cMsgDomains[id].receiveSocket)) != CMSG_OK) {
    pthread_cancel(cMsgDomains[id].pendThread);
    domainClear(&cMsgDomains[id]);
    connectWriteUnlock();
    return(err);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "codaConnect: created receiving socket fd = %d\n", cMsgDomains[id].receiveSocket);
  }
    
  /* create keep alive socket and store */
  if ( (err = cMsgTcpConnect(cMsgDomains[id].sendHost,
                             cMsgDomains[id].sendPort,
                             &cMsgDomains[id].keepAliveSocket)) != CMSG_OK) {
    close(cMsgDomains[id].receiveSocket);
    pthread_cancel(cMsgDomains[id].pendThread);
    domainClear(&cMsgDomains[id]);
    connectWriteUnlock();
    return(err);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "codaConnect: created keepalive socket fd = %d\n",cMsgDomains[id].keepAliveSocket );
  }
  
  /* create thread to send periodic keep alives and handle dead server */
  status = pthread_create(&cMsgDomains[id].keepAliveThread, NULL,
                          keepAliveThread, (void *)&cMsgDomains[id]);
  if (status != 0) {
    err_abort(status, "Creating keep alive thread");
  }
     
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "codaConnect: created keep alive thread\n");
  }

  /* create sending socket and store */
  if ( (err = cMsgTcpConnect(cMsgDomains[id].sendHost,
                             cMsgDomains[id].sendPort,
                             &cMsgDomains[id].sendSocket)) != CMSG_OK) {
    close(cMsgDomains[id].keepAliveSocket);
    close(cMsgDomains[id].receiveSocket);
    pthread_cancel(cMsgDomains[id].pendThread);
    domainClear(&cMsgDomains[id]);
    connectWriteUnlock();
    return(err);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "codaConnect: created sending socket fd = %d\n", cMsgDomains[id].sendSocket);
  }
  
  /* init is complete */
  *domainId = id ;

  /* install default shutdown handler (exits program) */
  codaSetShutdownHandler(id, defaultShutdownHandler, NULL);
    
  cMsgDomains[id].lostConnection = 0;
      
  /* no more mutex protection is necessary */
  connectWriteUnlock();

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
 * @param domainId id number of the domain connection
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
static int codaSend(int domainId, void *vmsg) {
  
  int len, lenSubject, lenType, lenCreator, lenText, lenByteArray;
  int highInt, lowInt, outGoing[16];
  cMsgMessage *msg = (cMsgMessage *) vmsg;
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  int fd = domain->sendSocket;
  char *creator;
  long long llTime;
  struct timespec now;
    
  /* msg rate measuring variables */
  /*
  static int             count=0, i, delay=0, loops=10000, ignore=5;
  static struct timespec t1, t2;
  static double          freq, freqAvg=0., deltaT, totalT=0.;
  static long long       totalC=0;
  */
  
  if (!domain->hasSend) {
    return(CMSG_NOT_IMPLEMENTED);
  }
  
  /* Cannot run this while connecting/disconnecting */
  connectReadLock();
  
  if (domain->initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (domain->lostConnection == 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
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
      fprintf(stderr, "codaSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* done protecting communications */
  socketMutexUnlock(domain);
  connectReadUnlock();

  /* calculate rate */
  /*
  if (count == 0) {
    clock_gettime(CLOCK_REALTIME, &t1);
  }

  if (count == loops-1) {
      clock_gettime(CLOCK_REALTIME, &t2);
      deltaT  = (double)(t2.tv_sec - t1.tv_sec) + 1.e-9*(t2.tv_nsec - t1.tv_nsec);
      totalT += deltaT;
      totalC += count;
      freq    = count/deltaT;
      freqAvg = (double)totalC/totalT;
      printf("Sending count = %d, %9.1f Hz, %9.1f Hz Avg.\n", count, freq, freqAvg);
      count = -1;
  }
  count++;
  */

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
 * @param domainId id number of the domain connection
 * @param vmsg pointer to a message structure
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement sending
 *                               messages
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 * @returns CMSG_LOST_CONNECTION if the network connection to the server was closed
 *                               by a call to cMsgDisconnect()
 */   
static int codaSendOrig(int domainId, void *vmsg) {
  
  int len, lenSubject, lenType, lenCreator, lenText, lenByteArray;
  int highInt, lowInt, outGoing[16];
  cMsgMessage *msg = (cMsgMessage *) vmsg;
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  int fd = domain->sendSocket;
  char *creator;
  long long llTime;
  struct timespec now;
  struct iovec iov[6];
    
  if (!domain->hasSend) {
    return(CMSG_NOT_IMPLEMENTED);
  }
  
  /* Cannot run this while connecting/disconnecting */
  connectReadLock();
  
  if (domain->initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (domain->lostConnection == 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
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
  
  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);
  
  iov[1].iov_base = (char*) msg->subject;
  iov[1].iov_len  = lenSubject;
  
  iov[2].iov_base = (char*) msg->type;
  iov[2].iov_len  = lenType;
  
  iov[3].iov_base = (char*) creator;
  iov[3].iov_len  = lenCreator;

  iov[4].iov_base = (char*) msg->text;
  iov[4].iov_len  = lenText;
  
  iov[5].iov_base = (char*) &((msg->byteArray)[msg->byteArrayOffset]);
  iov[5].iov_len  = lenByteArray;
  
  /* make send socket communications thread-safe */
  socketMutexLock(domain);
  
  if (cMsgTcpWritev(fd, iov, 6, 16) == -1) {
    socketMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "codaSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* done protecting communications */
  socketMutexUnlock(domain);
  connectReadUnlock();

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine sends a msg to the specified domain server and receives a response.
 * It is a synchronous routine and as a result blocks until it receives a status
 * integer from the cMsg server. It is called by the user through cMsgSyncSend()
 * given the appropriate UDL. In this domain cMsgFlush() does nothing and
 * does not need to be called for the message to be sent immediately.
 *
 * @param domainId id number of the domain connection
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
static int codaSyncSend(int domainId, void *vmsg, int *response) {
  
  int err, len, lenSubject, lenType, lenCreator, lenText, lenByteArray;
  int highInt, lowInt, outGoing[16];
  cMsgMessage *msg = (cMsgMessage *) vmsg;
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  int fd = domain->sendSocket;
  int fdIn = domain->receiveSocket;
  char *creator;
  long long llTime;
  struct timespec now;
    
  if (!domain->hasSyncSend) {
    return(CMSG_NOT_IMPLEMENTED);
  }
 
  connectReadLock();

  if (domain->initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (domain->lostConnection == 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
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
        fprintf(stderr, "codaSyncSend: out of memory\n");
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
      fprintf(stderr, "codaSyncSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* done protecting outgoing communications */
  socketMutexUnlock(domain);
  
  /* now read reply */
  if (cMsgTcpRead(fdIn, (void *) &err, sizeof(err)) != sizeof(err)) {
    socketMutexUnlock(domain);
    syncSendMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "codaSyncSend: read failure\n");
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


/**
 * This routine gets one message from a one-shot subscription to the given
 * subject and type. It is called by the user through cMsgSubscribeAndGet()
 * given the appropriate UDL. In this domain cMsgFlush() does nothing and
 * does not need to be called for the subscription to be started immediately.
 *
 * @param domainId id number of the domain connection
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
static int codaSubscribeAndGet(int domainId, const char *subject, const char *type,
                           const struct timespec *timeout, void **replyMsg) {
                             
  cMsgDomain_CODA *domain  = &cMsgDomains[domainId];
  int i, uniqueId, status, len, lenSubject, lenType;
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
  if (domain->lostConnection == 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
  }
  
  /*
   * Pick a unique identifier for the subject/type pair, and
   * send it to the domain server & remember it for future use
   * Mutex protect this operation as many codaConnect calls may
   * operate in parallel on this static variable.
   */
  mutexLock();
  uniqueId = subjectTypeId++;
  mutexUnlock();

  /* make new entry and notify server */
  gotSpot = 0;

  for (i=0; i<MAX_SUBSCRIBE_AND_GET; i++) {
    if (domain->subscribeAndGetInfo[i].active != 0) {
      continue;
    }

    info = &domain->subscribeAndGetInfo[i];
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
      
      /* free up memory */
      free(info->subject);
      free(info->type);
      info->subject = NULL;
      info->type    = NULL;
      info->msg     = NULL;
      info->active  = 0;

      /* remove the get from server */
      unSubscribeAndGet(domainId, subject, type, uniqueId);
      *replyMsg = NULL;
      return (CMSG_TIMEOUT);
  }

  /* If we did not timeout... */

  /*
   * Don't need to make a copy of message as only 1 receipient.
   * Message was allocated in client's listening thread and user
   * must free it.
   */
  *replyMsg = info->msg;
  
  /* free up memory */
  free(info->subject);
  free(info->type);
  info->subject = NULL;
  info->type    = NULL;
  info->msg     = NULL;
  info->active  = 0;

  /*printf("get: SUCCESS!!!\n");*/

  return(CMSG_OK);
}




/*-------------------------------------------------------------------*/


/**
 * This routine sends a msg to the specified domain server and receives a response.
 * It is a synchronous routine and as a result blocks until it receives a status
 * integer from the cMsg server. It is called by the user through cMsgSyncSend()
 * given the appropriate UDL. In this domain cMsgFlush() does nothing and
 * does not need to be called for the message to be sent immediately.
 *
 * @param domainId id number of the domain connection
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
static int codaSyncSendOrig(int domainId, void *vmsg, int *response) {
  
  int err, len, lenSubject, lenType, lenCreator, lenText, lenByteArray;
  int highInt, lowInt, outGoing[16];
  cMsgMessage *msg = (cMsgMessage *) vmsg;
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  int fd = domain->sendSocket;
  int fdIn = domain->receiveSocket;
  char *creator;
  long long llTime;
  struct timespec now;
  struct iovec iov[6];
    
  if (!domain->hasSyncSend) {
    return(CMSG_NOT_IMPLEMENTED);
  }
 
  connectReadLock();

  if (domain->initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (domain->lostConnection == 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
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
  
  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);
  
  iov[1].iov_base = (char*) msg->subject;
  iov[1].iov_len  = lenSubject;
  
  iov[2].iov_base = (char*) msg->type;
  iov[2].iov_len  = lenType;

  iov[3].iov_base = (char*) creator;
  iov[3].iov_len  = lenCreator;

  iov[4].iov_base = (char*) msg->text;
  iov[4].iov_len  = lenText;
  
  iov[5].iov_base = (char*) &((msg->byteArray)[msg->byteArrayOffset]);
  iov[5].iov_len  = lenByteArray;

  /* make send socket communications thread-safe */
  socketMutexLock(domain);
  
  if (cMsgTcpWritev(fd, iov, 6, 16) == -1) {
    socketMutexUnlock(domain);
    syncSendMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "codaSyncSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* done protecting outgoing communications */
  socketMutexUnlock(domain);
  
  /* now read reply */
  if (cMsgTcpRead(fdIn, (void *) &err, sizeof(err)) != sizeof(err)) {
    socketMutexUnlock(domain);
    syncSendMutexUnlock(domain);
    connectReadUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "codaSyncSend: read failure\n");
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


/**
 * This routine gets one message from a one-shot subscription to the given
 * subject and type. It is called by the user through cMsgSubscribeAndGet()
 * given the appropriate UDL. In this domain cMsgFlush() does nothing and
 * does not need to be called for the subscription to be started immediately.
 *
 * @param domainId id number of the domain connection
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
static int codaSubscribeAndGetOrig(int domainId, const char *subject, const char *type,
                           const struct timespec *timeout, void **replyMsg) {
                             
  cMsgDomain_CODA *domain  = &cMsgDomains[domainId];
  int i, uniqueId, status, len, lenSubject, lenType;
  int gotSpot, fd = domain->sendSocket;
  int outGoing[5];
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
  if (domain->lostConnection == 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
  }
  
  /*
   * Pick a unique identifier for the subject/type pair, and
   * send it to the domain server & remember it for future use
   * Mutex protect this operation as many codaConnect calls may
   * operate in parallel on this static variable.
   */
  mutexLock();
  uniqueId = subjectTypeId++;
  mutexUnlock();

  /* make new entry and notify server */
  gotSpot = 0;

  for (i=0; i<MAX_SUBSCRIBE_AND_GET; i++) {
    if (domain->subscribeAndGetInfo[i].active != 0) {
      continue;
    }

    info = &domain->subscribeAndGetInfo[i];
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
      
      /* free up memory */
      free(info->subject);
      free(info->type);
      info->subject = NULL;
      info->type    = NULL;
      info->msg     = NULL;
      info->active  = 0;

      /* remove the get from server */
      unSubscribeAndGet(domainId, subject, type, uniqueId);
      *replyMsg = NULL;
      return (CMSG_TIMEOUT);
  }

  /* If we did not timeout... */

  /*
   * Don't need to make a copy of message as only 1 receipient.
   * Message was allocated in client's listening thread and user
   * must free it.
   */
  *replyMsg = info->msg;
  
  /* free up memory */
  free(info->subject);
  free(info->type);
  info->subject = NULL;
  info->type    = NULL;
  info->msg     = NULL;
  info->active  = 0;

  /*printf("get: SUCCESS!!!\n");*/

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
 * @param domainId id number of the domain connection
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
static int codaSendAndGetOrig(int domainId, void *sendMsg, const struct timespec *timeout,
                      void **replyMsg) {
  
  cMsgDomain_CODA *domain  = &cMsgDomains[domainId];
  cMsgMessage *msg = (cMsgMessage *) sendMsg;
  int i, uniqueId, status;
  int len, lenSubject, lenType, lenCreator, lenText, lenByteArray;
  int gotSpot, fd = domain->sendSocket;
  int highInt, lowInt, outGoing[15];
  getInfo *info = NULL;
  struct timespec wait;
  char *creator;
  long long llTime;
  struct timespec now;
  struct iovec iov[6];
  
  if (!domain->hasSendAndGet) {
    return(CMSG_NOT_IMPLEMENTED);
  }   
           
  connectReadLock();

  if (domain->initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (domain->lostConnection == 1) {
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
   * Mutex protect this operation as many codaConnect calls may
   * operate in parallel on this static variable.
   */
  mutexLock();
  uniqueId = subjectTypeId++;
  mutexUnlock();

  /* make new entry and notify server */
  gotSpot = 0;

  for (i=0; i<MAX_SEND_AND_GET; i++) {
    if (domain->sendAndGetInfo[i].active != 0) {
      continue;
    }

    info = &domain->sendAndGetInfo[i];
    info->id      = uniqueId;
    info->active  = 1;
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
  outGoing[10]  = htonl(lenSubject);
  /* length of "type" string */
  lenType      = strlen(msg->type);
  outGoing[11] = htonl(lenType);
  
  /* send creator (this sender's name if msg created here) */
  creator = msg->creator;
  if (creator == NULL) creator = domain->name;
  /* length of "creator" string */
  lenCreator   = strlen(creator);
  outGoing[12] = htonl(lenCreator);
  
  /* length of "text" string */
  outGoing[13] = htonl(lenText);
  
  /* length of byte array */
  lenByteArray = msg->byteArrayLength;
  outGoing[14] = htonl(lenByteArray);
    
  /* total length of message (minus first int) is first item sent */
  len = sizeof(outGoing) - sizeof(int) + lenSubject + lenType +
        lenCreator + lenText + lenByteArray;
  outGoing[0] = htonl(len);
  
  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);
  
  iov[1].iov_base = (char*) msg->subject;
  iov[1].iov_len  = lenSubject;
  
  iov[2].iov_base = (char*) msg->type;
  iov[2].iov_len  = lenType;
  
  iov[3].iov_base = (char*) creator;
  iov[3].iov_len  = lenCreator;

  iov[4].iov_base = (char*) msg->text;
  iov[4].iov_len  = lenText;
  
  iov[5].iov_base = (char*) &((msg->byteArray)[msg->byteArrayOffset]);
  iov[5].iov_len  = lenByteArray;
  

  /* make send socket communications thread-safe */
  socketMutexLock(domain);
  
  if (cMsgTcpWritev(fd, iov, 6, 16) == -1) {
    socketMutexUnlock(domain);
    connectReadUnlock();
    free(info->subject);
    free(info->type);
    info->subject = NULL;
    info->type    = NULL;
    info->active  = 0;
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "codaSendAndGet: write failure\n");
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

      /* free up memory */
      free(info->subject);
      free(info->type);
      info->subject = NULL;
      info->type    = NULL;
      info->msg     = NULL;
      info->active  = 0;

      *replyMsg = NULL;
      return (CMSG_TIMEOUT);
  }

  /* If we did not timeout... */

  /*
   * Don't need to make a copy of message as only 1 receipient.
   * Message was allocated in client's listening thread and user
   * must free it.
   */
  *replyMsg = info->msg;
  
  /* free up memory */
  free(info->subject);
  free(info->type);
  info->subject = NULL;
  info->type    = NULL;
  info->msg     = NULL;
  info->active  = 0;
  
  /*printf("get: SUCCESS!!!\n");*/

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
 * @param domainId id number of the domain connection
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
static int codaSendAndGet(int domainId, void *sendMsg, const struct timespec *timeout,
                      void **replyMsg) {
  
  cMsgDomain_CODA *domain  = &cMsgDomains[domainId];
  cMsgMessage *msg = (cMsgMessage *) sendMsg;
  int i, uniqueId, status;
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
  if (domain->lostConnection == 1) {
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
   * Mutex protect this operation as many codaConnect calls may
   * operate in parallel on this static variable.
   */
  mutexLock();
  uniqueId = subjectTypeId++;
  mutexUnlock();

  /* make new entry and notify server */
  gotSpot = 0;

  for (i=0; i<MAX_SEND_AND_GET; i++) {
    if (domain->sendAndGetInfo[i].active != 0) {
      continue;
    }

    info = &domain->sendAndGetInfo[i];
    info->id      = uniqueId;
    info->active  = 1;
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
        fprintf(stderr, "codaSendAndGet: out of memory\n");
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
      fprintf(stderr, "codaSendAndGet: write failure\n");
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

      /* free up memory */
      free(info->subject);
      free(info->type);
      info->subject = NULL;
      info->type    = NULL;
      info->msg     = NULL;
      info->active  = 0;

      *replyMsg = NULL;
      return (CMSG_TIMEOUT);
  }

  /* If we did not timeout... */

  /*
   * Don't need to make a copy of message as only 1 receipient.
   * Message was allocated in client's listening thread and user
   * must free it.
   */
  *replyMsg = info->msg;
  
  /* free up memory */
  free(info->subject);
  free(info->type);
  info->subject = NULL;
  info->type    = NULL;
  info->msg     = NULL;
  info->active  = 0;
  
  /*printf("get: SUCCESS!!!\n");*/

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine tells the cMsg server to "forget" about the cMsgSendAndGet()
 * call (specified by the id argument) since a timeout occurred.
 */   
static int unSendAndGet(int domainId, int id) {
  
  int outGoing[3];
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
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
 * This routine tells the cMsg server to "forget" about the cMsgSubscribeAndGet()
 * call (specified by the id argument) since a timeout occurred.
 */   
static int unSubscribeAndGet(int domainId, const char *subject, const char *type, int id) {
  
  int len, outGoing[6], lenSubject, lenType;
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
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
 * This routine sends any pending (queued up) communication with the server.
 * In the cMsg domain, however, all sockets are set to TCP_NODELAY -- meaning
 * all writes over the socket are sent immediately. Thus, this routine does
 * nothing.
 *
 * @param domainId id number of the domain connection
 *
 * @returns CMSG_OK always
 */   
static int codaFlush(int domainId) {  
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
 * @param domainId id number of the domain connection
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
static int codaSubscribe(int domainId, const char *subject, const char *type, cMsgCallback *callback,
                     void *userArg, cMsgSubscribeConfig *config) {

  int i, j, iok, jok, uniqueId, status;
  cMsgDomain_CODA *domain  = &cMsgDomains[domainId];
  subscribeConfig *sConfig = (subscribeConfig *) config;
  struct iovec iov[3];

  if (!domain->hasSubscribe) {
    return(CMSG_NOT_IMPLEMENTED);
  } 
  
  connectReadLock();

  if (domain->initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (domain->lostConnection == 1) {
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
    int fd = domain->sendSocket;
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
     * Mutex protect this operation as many codaConnect calls may
     * operate in parallel on this static variable.
     */
    mutexLock();
    uniqueId = subjectTypeId++;
    mutexUnlock();
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
      subscribeMutexUnlock(domain);
      connectReadUnlock();
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
        fprintf(stderr, "codaSubscribe: write failure\n");
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


/**
 * This routine unsubscribes to messages of the given subject, type,
 * callback, and user argument. This routine is called by the user through
 * cMsgUnSubscribe() given the appropriate UDL. In this domain cMsgFlush()
 * does nothing and does not need to be called for codaUnsubscribe to be
 * started immediately.
 *
 * @param domainId id number of the domain connection
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
static int codaUnsubscribe(int domainId, const char *subject, const char *type, cMsgCallback *callback,
                           void *userArg) {

  int i, j, status;
  int cbCount = 0;     /* total number of callbacks for the subject/type pair of interest */
  int cbsRemoved = 0;  /* total number of callbacks removed for that subject/type pair */
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  struct iovec iov[3];
  subscribeCbInfo *subscription;
  
  if (!domain->hasUnsubscribe) {
    return(CMSG_NOT_IMPLEMENTED);
  }
 
  connectReadLock();

  if (domain->initComplete != 1) {
    connectReadUnlock();
    return(CMSG_NOT_INITIALIZED);
  }
  if (domain->lostConnection == 1) {
    connectReadUnlock();
    return(CMSG_LOST_CONNECTION);
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
          
          /* if the callback and argument are identical ... */
	  if ( (subscription->callback == callback) &&
               (subscription->userArg  ==  userArg))  {
            
            /* tell callback thread to end */
            subscription->quit = 1;

            /* wakeup callback thread */
            status = pthread_cond_broadcast(&subscription->cond);
            if (status != 0) {
              err_abort(status, "Failed callback condition signal");
            }
            
            /* mark this subscription (array location) as inactive/available */
            subscription->active = 0;
            
            /* removed 1 callback */
            cbsRemoved++;
            
            /* found the only matching callback/arg combination */
            break;
          }
	}
      }
      break;
    }
  }


  /* delete entry and notify server if there was at least 1 callback
   * to begin with and now there are none for this subject/type */
  if ((cbCount > 0) && (cbCount-cbsRemoved < 1)) {

    int len, lenSubject, lenType;
    int fd = domain->sendSocket;
    int outGoing[6];

    domain->subscribeInfo[i].active = 0;
    free(domain->subscribeInfo[i].subject);
    free(domain->subscribeInfo[i].type);
    free(domain->subscribeInfo[i].subjectRegexp);
    free(domain->subscribeInfo[i].typeRegexp);
    /* set these equal to NULL so they aren't freed again later */
    domain->subscribeInfo[i].subject       = NULL;
    domain->subscribeInfo[i].type          = NULL;
    domain->subscribeInfo[i].subjectRegexp = NULL;
    domain->subscribeInfo[i].typeRegexp    = NULL;
    
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "codaUnsubscribe: send 4 ints\n");
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
        fprintf(stderr, "codaUnsubscribe: write failure\n");
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


/**
 * This routine enables the receiving of messages and delivery to callbacks.
 * The receiving of messages is disabled by default and must be explicitly
 * enabled.
 *
 * @param domainId id number of the domain connection
 *
 * @returns CMSG_OK if successful
 */   
static int codaStart(int domainId) {
  
  cMsgDomains[domainId].receiveState = 1;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine disables the receiving of messages and delivery to callbacks.
 * The receiving of messages is disabled by default. This routine only has an
 * effect when cMsgReceiveStart() was previously called.
 *
 * @param domainId id number of the domain connection
 *
 * @returns CMSG_OK if successful
 */   
static int codaStop(int domainId) {
  
  cMsgDomains[domainId].receiveState = 0;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine disconnects the client from the cMsg server.
 *
 * @param domainId id number of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 */   
static int codaDisconnect(int domainId) {
  
  int i, j, status, outGoing[2];
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
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
    fprintf(stderr, "codaDisconnect:cancel keep alive thread\n");
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
      fprintf(stderr, "codaDisconnect: write failure, but continue\n");
    }
    /*return(CMSG_NETWORK_ERROR);*/
  }
 
  socketMutexUnlock(domain);

  domain->lostConnection = 1;
  
  /* close sending socket */
  close(domain->sendSocket);

  /* close receiving socket */
  close(domain->receiveSocket);

  /* stop listening and client communication threads */
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "codaDisconnect:cancel listening & client threads\n");
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
            fprintf(stderr, "codaDisconnect:wake up callback thread\n");
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
      fprintf(stderr, "codaDisconnect:wake up a sendAndGet\n");
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
 * @param domainId id number of the domain connection
 * @param handler shutdown handler function
 * @param userArg argument to shutdown handler 
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if the connection to the server was never made
 *                               since cMsgConnect() was never called
 */   
static int codaSetShutdownHandler(int domainId, cMsgShutdownHandler *handler, void *userArg) {
  
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];

  if (domain->initComplete != 1) return(CMSG_NOT_INITIALIZED);
  
  domain->shutdownHandler = handler;
  domain->shutdownUserArg = userArg;
      
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/


/**
 * Method to shutdown the given clients and/or servers.
 *
 * @param domainId id number of the domain connection
 * @param client client(s) to be shutdown
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
static int codaShutdown(int domainId, const char *client, const char *server, int flag) {
  
  int len, cLen, sLen, outGoing[5];
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  int fd = domain->sendSocket;
  struct iovec iov[3];

  if (!domain->hasShutdown) {
    return(CMSG_NOT_IMPLEMENTED);
  } 
  
  if (domain->initComplete != 1) return(CMSG_NOT_INITIALIZED);
      
  connectWriteLock();
    
  /* message id (in network byte order) to domain server */
  outGoing[1] = htonl(CMSG_SHUTDOWN);
  outGoing[2] = htonl(flag);
  
  if (client == NULL) {
    cLen = 0;
    outGoing[3] = 0;
  }
  else {
    cLen = strlen(client);
    outGoing[3] = htonl(cLen);
  }
  
  if (server == NULL) {
    sLen = 0;
    outGoing[4] = 0;
  }
  else {
    sLen = strlen(server);
    outGoing[4] = htonl(sLen);
  }

  /* total length of message (minus first int) is first item sent */
  len = sizeof(outGoing) - sizeof(int) + cLen + sLen;
  outGoing[0] = htonl(len);
  
  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);

  iov[1].iov_base = (char*) client;
  iov[1].iov_len  = cLen;

  iov[2].iov_base = (char*) server;
  iov[2].iov_len  = sLen;
  
  /* make send socket communications thread-safe */
  socketMutexLock(domain);
  
  if (cMsgTcpWritev(fd, iov, 3, 16) == -1) {
    socketMutexUnlock(domain);
    connectWriteUnlock();
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "codaUnsubscribe: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
   
  socketMutexUnlock(domain);  
  connectWriteUnlock();

  return CMSG_OK;

}


/*-------------------------------------------------------------------*/


/** This routine exchanges information with the name server. */
static int talkToNameServer(cMsgDomain_CODA *domain, int serverfd,
                                        char *subdomain, char *UDLremainder) {

  int  err, lengthDomain, lengthSubdomain, lengthRemainder;
  int  lengthHost, lengthName, lengthUDL, lengthDescription;
  int  outGoing[11], inComing[2];
  char temp[CMSG_MAXHOSTNAMELEN], atts[7];
  const char *domainType = "cMsg";
  struct iovec iov[8];

  /* first send message id (in network byte order) to server */
  outGoing[0] = htonl(CMSG_SERVER_CONNECT);
  /* major version number */
  outGoing[1] = htonl(CMSG_VERSION_MAJOR);
  /* minor version number */
  outGoing[2] = htonl(CMSG_VERSION_MINOR);
  /* send my listening port (as an int) to server */
  outGoing[3] = htonl((int)domain->listenPort);
  /* send length of the type of domain server I'm expecting to connect to.*/
  lengthDomain = strlen(domainType);
  outGoing[4]  = htonl(lengthDomain);
  /* send length of the type of subdomain handler I'm expecting to use.*/
  lengthSubdomain = strlen(subdomain);
  outGoing[5] = htonl(lengthSubdomain);
  /* send length of the UDL remainder.*/
  /* this may be null */
  if (UDLremainder == NULL) {
    lengthRemainder = outGoing[6] = 0;
  }
  else {
    lengthRemainder = strlen(UDLremainder);
    outGoing[6] = htonl(lengthRemainder);
  }
  /* send length of my host name to server */
  lengthHost  = strlen(domain->myHost);
  outGoing[7] = htonl(lengthHost);
  /* send length of my name to server */
  lengthName  = strlen(domain->name);
  outGoing[8] = htonl(lengthName);
  /* send length of my udl to server */
  lengthUDL   = strlen(domain->udl);
  outGoing[9] = htonl(lengthUDL);
  /* send length of my description to server */
  lengthDescription  = strlen(domain->description);
  outGoing[10] = htonl(lengthDescription);
    
  iov[0].iov_base = (char*) outGoing;
  iov[0].iov_len  = sizeof(outGoing);
  
  iov[1].iov_base = (char*) domainType;
  iov[1].iov_len  = lengthDomain;
  
  iov[2].iov_base = (char*) subdomain;
  iov[2].iov_len  = lengthSubdomain;
  
  iov[3].iov_base = (char*) UDLremainder;
  iov[3].iov_len  = lengthRemainder;
  
  iov[4].iov_base = (char*) domain->myHost;
  iov[4].iov_len  = lengthHost;
  
  iov[5].iov_base = (char*) domain->name;
  iov[5].iov_len  = lengthName;
  
  iov[6].iov_base = (char*) domain->udl;
  iov[6].iov_len  = lengthUDL;
  
  iov[7].iov_base = (char*) domain->description;
  iov[7].iov_len  = lengthDescription;
  
  if (cMsgTcpWritev(serverfd, iov, 8, 16) == -1) {
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

    /* increase concurrency for this thread for early Solaris */
    int  con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);

    /* release system resources when thread finishes */
    pthread_detach(pthread_self());

    /* periodically send a keep alive message and read response */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "keepAliveThread: keep alive thread created, socket = %d\n", socket);
    }
  
    /* request to send */
    outGoing = htonl(CMSG_KEEP_ALIVE);
    
    /* keep checking to see if the server/agent is alive */
    while(1) {
       if (cMsgDebug >= CMSG_DEBUG_INFO) {
         fprintf(stderr, "keepAliveThread: send keep alive request\n");
       }
       
       if (cMsgTcpWrite(socket, &outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
         if (cMsgDebug >= CMSG_DEBUG_ERROR) {
           fprintf(stderr, "keepAliveThread: error writing request\n");
         }
         break;
       }

       /* read response */
       if (cMsgDebug >= CMSG_DEBUG_INFO) {
         fprintf(stderr, "keepAliveThread: read keep alive response\n");
       }
       
       if ((err = cMsgTcpRead(socket, (char*) &alive, sizeof(alive))) != sizeof(alive)) {
         if (cMsgDebug >= CMSG_DEBUG_ERROR) {
           fprintf(stderr, "keepAliveThread: read failure\n");
         }
         break;
       }
       
       /* sleep for 1 second and try again */
       sleep(1);
    }
    
    /* if we've reach here, there's an error, do a disconnect */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "keepAliveThread: server is probably dead, disconnect\n");
    }
    cMsgDisconnect(domainId);
    
    sun_setconcurrency(con);
    
    return NULL;
}


/** This routine is run as a thread in which a single callback is executed. */
static void *callbackThread(void *arg)
{
    /* subscription information passed in thru arg */
    subscribeCbInfo *subscription = (subscribeCbInfo *) arg;
    int i, status, need, threadsAdded, maxToAdd, wantToAdd;
    int numMsgs, numThreads;
    cMsgMessage *msg;
    pthread_t thd;
    /*time_t now, t;*/ /* for printing msg cue size periodically */
    
    /* increase concurrency for this thread for early Solaris */
    int  con;
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
      numMsgs = subscription->messages;
      numThreads = subscription->threads;
      threadsAdded = 0;
      
      /* Check to see if we need more threads to handle the load */      
      if ((!subscription->config.mustSerialize) &&
          (numThreads < subscription->config.maxThreads) &&
          (numMsgs > subscription->config.msgsPerThread)) {

        /* find number of threads needed */
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
/*fprintf(stderr, "CALLBACK THREAD GOING INTO COND WAIT\n");*/
        status = pthread_cond_wait(&subscription->cond, &subscription->mutex);
        if (status != 0) {
          err_abort(status, "Failed callback cond wait");
        }
/*fprintf(stderr, "CALLBACK THREAD woke up and quit = %d\n", subscription->quit);*/
        
        /* quit if commanded to */
        if (subscription->quit) {
          /* unlock mutex */
          status = pthread_mutex_unlock(&subscription->mutex);
          if (status != 0) {
            err_abort(status, "Failed callback mutex unlock");
          }
/*printf("TOLD TO QUIT MAIN CALLBACK THREAD\n");*/
          goto end;
        }
      }
            
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
      
      /* wakeup cMsgRunCallbacks thread if trying to add item to full cue */
      status = pthread_cond_broadcast(&subscription->cond);
      if (status != 0) {
        err_abort(status, "Failed callback condition signal");
      }

      /* print out number of messages in cue */
      /*
      t = time(NULL);
      if (now + 5 <= t) {
        printf("     %d\n",subscription->messages);
        now = t;
      }
      */

      /* run callback */
#ifdef	__cplusplus
      subscription->callback->callback(new cMsgMessageBase(msg), subscription->userArg); 
#else
      subscription->callback(msg, subscription->userArg);
#endif
      
      /* quit if commanded to */
      if (subscription->quit) {
/*printf("TOLD TO QUIT MAIN CALLBACK THREAD\n");*/
        goto end;
      }
    }
    
  end:
          
    sun_setconcurrency(con);
    
fprintf(stderr, "QUITTING MAIN CALLBACK THREAD\n");
fflush(stderr);
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
    subscribeCbInfo *subscription = (subscribeCbInfo *) arg;
    int status, empty;
    cMsgMessage *msg;
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
      status = pthread_mutex_lock(&subscription->mutex);
      if (status != 0) {
        err_abort(status, "Failed callback mutex lock");
      }
      
      /* wait while there are no messages */
      while (subscription->head == NULL) {
        /* wait until signaled or for .2 sec, before
         * waking thread up and checking for messages
         */
        getAbsoluteTime(&timeout, &wait);        
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
            
            sun_setconcurrency(con);

/*printf("SUPPLEMENTAL CALLBACK THREAD TIMED OUT 10X - QUITTING\n");*/
            pthread_exit(NULL);
            return NULL;
          }

        }
        else if (status != 0) {
          err_abort(status, "Failed callback cond wait");
        }
        
        /* quit if commanded to */
        if (subscription->quit) {
          /* unlock mutex */
          status = pthread_mutex_unlock(&subscription->mutex);
          if (status != 0) {
            err_abort(status, "Failed callback mutex unlock");
          }
/*printf("TOLD TO QUIT SUPPLEMENTAL CALLBACK THREAD\n");*/
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
      
      /* wakeup cMsgRunCallbacks thread if trying to add item to full cue */
      status = pthread_cond_broadcast(&subscription->cond);
      if (status != 0) {
        err_abort(status, "Failed callback condition signal");
      }

      /* run callback */
#ifdef	__cplusplus
      subscription->callback->callback(new cMsgMessageBase(msg), subscription->userArg);
#else
      subscription->callback(msg, subscription->userArg);
#endif
      
      /* quit if commanded to */
      if (subscription->quit) {
/*printf("TOLD TO QUIT SUPPLEMENTAL CALLBACK THREAD\n");*/
        goto end;
      }
    }
    
  end:
          
    sun_setconcurrency(con);
    
/*fprintf(stderr, "QUITTING SUPPLEMENTAL CALLBACK THREAD\n");*/
    pthread_exit(NULL);
    return NULL;
}


/*-------------------------------------------------------------------*/

/**
 * This routine runs all the appropriate subscribe and subscribeAndGet
 * callbacks when a message arrives from the server. 
 */
int cMsgRunCallbacks(int domainId, cMsgMessage *msg) {

  int i, j, k, status;
  subscribeCbInfo *subscription;
  getInfo *info;
  cMsgDomain_CODA *domain;
  cMsgMessage *message, *oldHead;
  struct timespec wait, timeout;
    

  /* wait .1 sec for empty space on a full cue */
  timeout.tv_sec  = 0;
  timeout.tv_nsec = 100000000;
  
  domain = &cMsgDomains[domainId];
  
  /* callbacks have been stopped */
  if (domain->receiveState == 0) {
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cMsgRunCallbacks: all callbacks have been stopped\n");
    }
    cMsgFreeMessage((void *)msg);
    return (CMSG_OK);
  }
 
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
	/* if there is no existing callback look at next item ... */
        if (domain->subscribeInfo[i].cbInfo[j].callback == NULL) {
          continue;
        }

        /* copy message so each callback has its own copy */
        message = (cMsgMessage *) cMsgCopyMessage((void *)msg);
        if (message == NULL) {
          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "cMsgRunCallbacks: out of memory\n");
          }
          return(CMSG_OUT_OF_MEMORY);
        }

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
                if (cMsgDebug >= CMSG_DEBUG_INFO) {
                  fprintf(stderr, "cMsgRunCallbacks: skipped %d messages\n", (k+1));
                }
            }
            else {
/*fprintf(stderr, "cMsgRunCallbacks: cue full, waiting\n");*/

                /* wait until signaled - meaning item taken off cue */
                getAbsoluteTime(&timeout, &wait);        
                status = pthread_cond_timedwait(&subscription->cond, &subscription->mutex, &wait);

                /* if the wait timed out ... */
                if (status == ETIMEDOUT) {
/*fprintf(stderr, "cMsgRunCallbacks: cue full, timed out\n");*/
                    /* unlock mutex */
                    status = pthread_mutex_unlock(&subscription->mutex);
                    if (status != 0) {
                      err_abort(status, "Failed callback mutex unlock");
                    }
                    cMsgFreeMessage((void *)message);
                    cMsgFreeMessage((void *)msg);
                    return(CMSG_LIMIT_EXCEEDED);
                }
                else if (status != 0) {
                  err_abort(status, "Failed callback cond wait");
                }
/*fprintf(stderr, "cMsgRunCallbacks: cue full, wokenup, there's room now!\n");*/
            }
        }

        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          if (subscription->messages !=0 && subscription->messages%1000 == 0)
            fprintf(stderr, "           msgs = %d\n", subscription->messages);
        }

        /*
         * Add this message to linked list for this callback.
         * It will now be the responsibility of message consumer
         * to free the msg allocated here.
         */       

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
/*printf("cMsgRunCallbacks: cb cue size = %d\n", subscription->messages);*/
        message->next = NULL;

        /* unlock mutex */
        status = pthread_mutex_unlock(&subscription->mutex);
        if (status != 0) {
          err_abort(status, "Failed callback mutex unlock");
        }

        /* wakeup callback thread */
        status = pthread_cond_broadcast(&subscription->cond);
        if (status != 0) {
          err_abort(status, "Failed callback condition signal");
        }

      } /* search callback list */
    } /* if subscribe sub/type matches msg sub/type */
  } /* for each subscription */

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
  
  /* Need to free up msg allocated by client's listening thread */
  cMsgFreeMessage((void *)msg);
  
  return (CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine runs all the appropriate sendAndGet
 * callbacks when a message arrives from the server. 
 */
int cMsgWakeGet(int domainId, cMsgMessage *msg) {

  int i, status;
  getInfo *info;
  cMsgDomain_CODA *domain;
  
  domain = &cMsgDomains[domainId];
  
  /* message reception has been stopped */
  if (domain->receiveState == 0) {
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cMsgWakeGet: message receiption has been stopped\n");
    }
    cMsgFreeMessage((void *)msg);
    return (CMSG_OK);
  }
 
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
    }
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
 * This routine initializes the structure used to handle a get - 
 * either a sendAndGet or a subscribeAndGet.
 */
static void getInfoInit(getInfo *info, int reInit) {
    int status;
    
    info->id      = 0;
    info->active  = 0;
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
      info->cbInfo[j].config.maxCueSize    = 10000;
      info->cbInfo[j].config.skipSize      = 2000;
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
 
  domain->id                 = 0;

  domain->initComplete       = 0;
  domain->receiveState       = 0;
  domain->lostConnection     = 1;
      
  domain->sendSocket         = 0;
  domain->receiveSocket      = 0;
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
  domain->hasShutdown        = 0;

  domain->myHost             = NULL;
  domain->sendHost           = NULL;
  domain->serverHost         = NULL;
  
  domain->name               = NULL;
  domain->udl                = NULL;
  domain->description        = NULL;
  
  domain->shutdownHandler    = NULL;
  domain->shutdownUserArg    = NULL;
  
  domain->msgBuffer          = NULL;
  domain->msgBufferSize      = 0;

  domain->msgInBuffer[0]     = NULL;
  domain->msgInBuffer[1]     = NULL;

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
  if (domain->msgBuffer        != NULL) free(domain->msgBuffer);
  if (domain->msgInBuffer[0]   != NULL) free(domain->msgInBuffer[0]);
  if (domain->msgInBuffer[1]   != NULL) free(domain->msgInBuffer[1]);
  
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
#endif
    
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

/** This routine locks the pthread mutex used when creating unique id numbers. */
static void mutexLock(void) {

  int status = pthread_mutex_lock(&generalMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/** This routine unlocks the pthread mutex used when creating unique id numbers. */
static void mutexUnlock(void) {

  int status = pthread_mutex_unlock(&generalMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the read lock used to allow simultaneous
 * execution of codaSend, codaSyncSend, codaSubscribe, codaUnsubscribe,
 * codaSendAndGet, and codaSubscribeAndGet, but NOT allow simultaneous
 * execution of those routines with codaConnect or codaDisconnect.
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
 * execution of codaSend, codaSyncSend, codaSubscribe, codaUnsubscribe,
 * codaSendAndGet, and codaSubscribeAndGet, but NOT allow simultaneous
 * execution of those routines with codaConnect or codaDisconnect.
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
 * execution of codaSend, codaSyncSend, codaSubscribe, codaUnsubscribe,
 * codaSendAndGet, and codaSubscribeAndGet, but NOT allow simultaneous
 * execution of those routines with codaConnect or codaDisconnect.
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
 * execution of codaSend, codaSyncSend, codaSubscribe, codaUnsubscribe,
 * codaSendAndGet, and codaSubscribeAndGet, but NOT allow simultaneous
 * execution of those routines with codaConnect or codaDisconnect.
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


/** This routine locks the pthread mutex used to serialize codaSyncSend calls. */
static void syncSendMutexLock(cMsgDomain_CODA *domain) {

  int status = pthread_mutex_lock(&domain->syncSendMutex);
  if (status != 0) {
    err_abort(status, "Failed syncSend mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/** This routine unlocks the pthread mutex used to serialize codaSyncSend calls. */
static void syncSendMutexUnlock(cMsgDomain_CODA *domain) {

  int status = pthread_mutex_unlock(&domain->syncSendMutex);
  if (status != 0) {
    err_abort(status, "Failed syncSend mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the pthread mutex used to serialize
 * codaSubscribe and codaUnsubscribe calls.
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
 * codaSubscribe and codaUnsubscribe calls.
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


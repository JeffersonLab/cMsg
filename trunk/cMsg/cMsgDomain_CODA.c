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
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <pthread.h>
#include <time.h>

/* package includes */
#include "cMsgNetwork.h"
#include "cMsgPrivate.h"
#include "cMsg.h"
#include "cMsg_CODA.h"


/* built-in limits */
#define MAXDOMAINS_CODA  10
#define WAIT_FOR_THREADS 10 /* seconds to wait for thread to start */


cMsgDomain_CODA cMsgDomains[MAXDOMAINS_CODA];

/* local variables */
static int oneTimeInitialized = 0;
static pthread_mutex_t connectMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t generalMutex = PTHREAD_MUTEX_INITIALIZER;

/* id which uniquely defines a subject/type pair */
static int subjectTypeId = 1;


/* temp */
static int counter = 0;


/* for c++ */
#ifdef __cplusplus
extern "C" {
#endif

static int   coda_connect(char *myUDL, char *myName, char *myDescription, int *domainId);
static int   coda_send(int domainId, void *msg);
static int   flush(int domainId);
static int   subscribe(int domainId, char *subject, char *type, cMsgCallback *callback, void *userArg);
static int   unsubscribe(int domainId, char *subject, char *type, cMsgCallback *callback);
static int   get(int domainId, void *sendMsg, time_t timeout, void **replyMsg);
static int   start(int domainId);
static int   stop(int domainId);
static int   disconnect(int domainId);

static domainFunctions functions = {coda_connect, coda_send, flush, subscribe,
                                    unsubscribe, get, start, stop, disconnect};

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
static void *dispatchCallback(void *param);
static void  mutexLock(void);
static void  mutexUnlock(void);
static void  connectMutexLock(void);
static void  connectMutexUnlock(void);
static int   sendMutexLock(cMsgDomain_CODA *domain);
static int   sendMutexUnlock(cMsgDomain_CODA *domain);
static int   subscribeMutexLock(cMsgDomain_CODA *domain);
static int   subscribeMutexUnlock(cMsgDomain_CODA *domain);
static int   parseUDL(const char *UDL, char **domainType, char **host,
                      unsigned short *port, char **subdomainType, char **UDLremainder);
static void  domainInit(cMsgDomain_CODA *domain);
static void  domainFree(cMsgDomain_CODA *domain);  
static void  domainClear(cMsgDomain_CODA *domain);
static void *keepAliveThread(void *arg);
static void *callbackThread(void *arg);



/*-------------------------------------------------------------------*/


static int coda_connect(char *myUDL, char *myName, char *myDescription, int *domainId) {

  int i, id=-1, err, serverfd, status, hz, num_try, try_max;
  char *portEnvVariable=NULL, temp[CMSG_MAXHOSTNAMELEN];
  char *subdomain=NULL, *UDLremainder=NULL;
  unsigned short startingPort;
  mainThreadInfo *threadArg;
  struct timespec waitForThread;
  
    
  /* First, grab mutex for thread safety. This mutex must be held until
   * the initialization is completely finished. Otherwise, if we set
   * initComplete = 1 (so that we reserve space in the cMsgDomains array)
   * before it's finished and then release the mutex, we may give an
   * "existing" connection to a user who does a second init
   * when in fact, an error may still occur in that "existing"
   * connection. Hope you caught that.
   */
  connectMutexLock();
  

  /* do one time initialization */
  if (!oneTimeInitialized) {
    /* clear arrays */
    for (i=0; i<MAXDOMAINS_CODA; i++) domainInit(&cMsgDomains[i]);
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
        cMsgDomains[i].udl         != NULL &&
        cMsgDomains[i].description != NULL)  {
        
      if ( (strcmp(cMsgDomains[i].name,        myName       ) == 0)  &&
           (strcmp(cMsgDomains[i].udl,         myUDL        ) == 0)  &&
           (strcmp(cMsgDomains[i].description, myDescription) == 0) )  {
        /* got a match */
        id = i;
        break;
      }  
    }
  }
  

  /* found the id of a valid connection - return that */
  if (id > -1) {
    *domainId = id + DOMAIN_ID_OFFSET;
    connectMutexUnlock();
    return(CMSG_OK);
  }
  

  /* no existing connection, so find the first available place in the "cMsgDomains" array */
  for (i=0; i<MAXDOMAINS_CODA; i++) {
    if (cMsgDomains[i].initComplete > 0) {
      continue;
    }
    domainInit(&cMsgDomains[i]);
    id = i;
    break;
  }
  

  /* exceeds number of domain connections allowed */
  if (id < 0) {
    connectMutexUnlock();
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
  
  /* parse the UDL - Uniform Domain Locator */
  if ( (err = parseUDL(myUDL, NULL, &cMsgDomains[id].serverHost,
                       &cMsgDomains[id].serverPort, &subdomain, &UDLremainder)) != CMSG_OK ) {
    /* there's been a parsing error */
    connectMutexUnlock();
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
    connectMutexUnlock();
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
    connectMutexUnlock();
    return(err);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_connect: connected to name server\n");
  }
  
  /* get host & port to send messages to */
  err = getHostAndPortFromNameServer(&cMsgDomains[id], serverfd, subdomain, UDLremainder);
  if (err != CMSG_OK) {
    close(serverfd);
    pthread_cancel(cMsgDomains[id].pendThread);
    domainClear(&cMsgDomains[id]);
    connectMutexUnlock();
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
    connectMutexUnlock();
    return(err);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_connect: created sending socket fd = %d\n", cMsgDomains[id].sendSocket);
  }
  
  /* init is complete */
  *domainId = id + DOMAIN_ID_OFFSET;
  cMsgDomains[id].initComplete = 1;

  if ( (err = cMsgTcpConnect(cMsgDomains[id].sendHost,
                             cMsgDomains[id].sendPort,
                             &cMsgDomains[id].keepAliveSocket)) != CMSG_OK) {
    close(cMsgDomains[id].sendSocket);
    pthread_cancel(cMsgDomains[id].pendThread);
    domainClear(&cMsgDomains[id]);
    connectMutexUnlock();
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
  
  
  /* no more mutex protection is necessary */
  connectMutexUnlock();
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int coda_send(int domainId, void *vmsg) {
  
  int err, lenSubject, lenType, lenText;
  int outGoing[9];
  char *subject, *type, *text;
  cMsgMessage *msg = (cMsgMessage *) vmsg;
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];
  int fd = domain->sendSocket;
    
  if (!domain->hasSend) {
    return(CMSG_NOT_IMPLEMENTED);
  }
 
  if (cMsgDomains[domainId].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (cMsgDomains[domainId].lostConnection == 1) return(CMSG_LOST_CONNECTION);

  subject = cMsgGetSubject(vmsg);
  type    = cMsgGetType(vmsg);
  text    = cMsgGetText(vmsg);

  /* message id (in network byte order) to domain server */
  outGoing[0] = htonl(CMSG_SEND_REQUEST);
  /* system message id */
  outGoing[1] = htonl(msg->sysMsgId);
  /* sender id */
  outGoing[2] = htonl(msg->senderId);
  /* time message sent (right now) */
  outGoing[3] = htonl((int) time(NULL));
  /* sender message id */
  outGoing[4] = htonl(msg->senderMsgId);
  /* sender token */
  outGoing[5] = htonl(msg->senderToken);

  /* length of "subject" string */
  lenSubject  = strlen(subject);
  outGoing[6] = htonl(lenSubject);
  /* length of "type" string */
  lenType     = strlen(type);
  outGoing[7] = htonl(lenType);
  /* length of "text" string */
  lenText     = strlen(text);
  outGoing[8] = htonl(lenText);

  /* make send socket communications thread-safe */
  sendMutexLock(domain);

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_send: sending 4 ints\n");
  }
  
  /* send ints over together */
  if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
    sendMutexUnlock(domain);
    subscribeMutexUnlock(domain);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "coda_send: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_send: sending subject (%s)\n", subject);
  }
  
  /* send subject */
  if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
    sendMutexUnlock(domain);
    subscribeMutexUnlock(domain);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "coda_send: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_send: sending type (%s)\n", type);
  }
  
  /* send type */
  if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
    sendMutexUnlock(domain);
    subscribeMutexUnlock(domain);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "coda_send: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_send: sending text (%s)\n", text);
  }
  
  /* send text */
  if (cMsgTcpWrite(fd, (void *) text, lenText) != lenText) {
    sendMutexUnlock(domain);
    subscribeMutexUnlock(domain);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "coda_send: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_send: will read reply\n");
  }
  
  /* now read reply */
  /*
  if (cMsgTcpRead(fd, (void *) &err, sizeof(err)) != sizeof(err)) {
    sendMutexUnlock(domain);
    subscribeMutexUnlock(domain);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "coda_send: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  */

  /* done protecting communications */
  sendMutexUnlock(domain);

  /* return domain server's reply */
  /*
  err = ntohl(err);
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_send: read reply (%d), am done\n", err);
  }
  */
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int get(int domainId, void *sendMsg, time_t timeout, void **replyMsg) {
  return (CMSG_NOT_IMPLEMENTED);
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
  sendMutexLock(domain);
  /* flush outgoing buffers */
  fflush(file);
  /* done with mutex */
  sendMutexUnlock(domain);
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int subscribe(int domainId, char *subject, char *type, cMsgCallback *callback, void *userArg) {

  int i, j, iok, jok, uniqueId, status;
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];

  if (cMsgDomains[domainId].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (cMsgDomains[domainId].lostConnection == 1) return(CMSG_LOST_CONNECTION);

  if (!domain->hasSubscribe) {
    return(CMSG_NOT_IMPLEMENTED);
  } 
  
  /* make sure subscribe and unsubscribe are not run at the same time */
  subscribeMutexLock(domain);
  
  /* add to callback list if subscription to same subject/type exists */
  iok = 0;
  for (i=0; i<MAXSUBSCRIBE; i++) {
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
          /* start callback thread now */
          status = pthread_create(&domain->subscribeInfo[i].cbInfo[j].thread,
                                  NULL, callbackThread,
                                  (void *) &domain->subscribeInfo[i].cbInfo[j]);
          if (status != 0) {
            err_abort(status, "Creating keep alive thread");
          }
        
	  jok = 1;
	}
      }
      break;

    }
  }
fprintf(stderr, "subscribe: done with first loop\n");
  
  if ((iok == 1) && (jok == 0)) return(CMSG_OUT_OF_MEMORY);
  if ((iok == 1) && (jok == 1)) return(CMSG_OK);

  /* no match, make new entry and notify server */
  iok = 0;
  for (i=0; i<MAXSUBSCRIBE; i++) {
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
    
    /* start callback thread now */
    status = pthread_create(&domain->subscribeInfo[i].cbInfo[0].thread,
                            NULL, callbackThread,
                            (void *) &domain->subscribeInfo[i].cbInfo[0]);
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

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "subscribe: write 4 (%d, %d, %d, %d) ints to server\n",
              CMSG_SUBSCRIBE_REQUEST, uniqueId, lenSubject, lenType);
    }

    /* make send socket communications thread-safe */
    sendMutexLock(domain);

    /* send ints over together */
    if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
      sendMutexUnlock(domain);
      subscribeMutexUnlock(domain);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "subscribe: write failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "subscribe: sending subject (%s)\n", subject);
    }

    /* send subject */
    if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
      sendMutexUnlock(domain);
      subscribeMutexUnlock(domain);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "subscribe: write failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "subscribe: sending type (%s)\n", type);
    }

    /* send type */
    if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
      sendMutexUnlock(domain);
      subscribeMutexUnlock(domain);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "subscribe: write failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "subscribe: will read reply\n");
    }

    /* now read reply */
    /*
    if (cMsgTcpRead(fd, (void *) &err, sizeof(err)) != sizeof(err)) {
      sendMutexUnlock(domain);
      subscribeMutexUnlock(domain);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "subscribe: read failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }
    */

    /* done protecting communications */
    sendMutexUnlock(domain);
    /* done protecting subscribe */
    subscribeMutexUnlock(domain);

    /* return domain server's reply */
    /*
    err = ntohl(err);
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "subscribe: read reply (%d)\n", err);
    }
    */
    return(CMSG_OK);
  }
  
  /* done protecting subscribe */
  subscribeMutexUnlock(domain);
  
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
 
  /* make sure subscribe and unsubscribe are not run at the same time */
  subscribeMutexLock(domain);
  
  /* search entry list */
  for (i=0; i<MAXSUBSCRIBE; i++) {
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
    int outGoing[3];

    domain->subscribeInfo[i].active = 0;
    free(domain->subscribeInfo[i].subject);
    free(domain->subscribeInfo[i].type);

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "unsubscribe: send 4 ints\n");
    }

    /* notify server */

    /* message id (in network byte order) to domain server */
    outGoing[0] = htonl(CMSG_UNSUBSCRIBE_REQUEST);
    /* length of "subject" string */
    lenSubject  = strlen(subject);
    outGoing[1] = htonl(lenSubject);
    /* length of "type" string */
    lenType     = strlen(type);
    outGoing[2] = htonl(lenType);

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "unsubscribe: write 3 (%d, %d, %d) ints to server\n",
              CMSG_UNSUBSCRIBE_REQUEST, lenSubject, lenType);
    }

    /* make send socket communications thread-safe */
    sendMutexLock(domain);

    /* send ints over together */
    if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
      sendMutexUnlock(domain);
      subscribeMutexUnlock(domain);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "unsubscribe: write failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "unsubscribe: write subject (%s)\n", subject);
    }

    /* send subject */
    if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
      sendMutexUnlock(domain);
      subscribeMutexUnlock(domain);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "unsubscribe: write failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "unsubscribe: write type (%s)\n", type);
    }

    /* send type */
    if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
      sendMutexUnlock(domain);
      subscribeMutexUnlock(domain);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "unsubscribe: write failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }

    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "unsubscribe: will read reply\n");
    }

    /* now read reply */
    /*
    if (cMsgTcpRead(fd, (void *) &err, sizeof(err)) != sizeof(err)) {
      sendMutexUnlock(domain);
      subscribeMutexUnlock(domain);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "unsubscribe: read failure\n");
      }
      return(CMSG_NETWORK_ERROR);
    }
    */

    /* done protecting communications */
    sendMutexUnlock(domain);
    /* done protecting unsubscribe */
    subscribeMutexUnlock(domain);

    /* return domain server's reply */
    /*
    err = ntohl(err);
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "unsubscribe: read replay (%d)\n", err);
    }
    */

    return(CMSG_OK);
  }

  /* done protecting unsubscribe */
  subscribeMutexUnlock(domain);
  
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
  
  int status;
  cMsgDomain_CODA *domain = &cMsgDomains[domainId];

  if (cMsgDomains[domainId].initComplete != 1) return(CMSG_NOT_INITIALIZED);
  
  /* When changing initComplete / connection status, mutex protect it */
  connectMutexLock();
  
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
  
  connectMutexUnlock();

  return(CMSG_OK);
}



/*-------------------------------------------------------------------*/


static int getHostAndPortFromNameServer(cMsgDomain_CODA *domain, int serverfd,
                                        char *subdomain, char *UDLremainder) {

  int err, lengthDomain, lengthSubdomain, lengthRemainder, lengthHost, lengthName;
  int outgoing[7], incoming[2];
  char temp[CMSG_MAXHOSTNAMELEN], atts[5];
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
  lengthRemainder = strlen(UDLremainder);
  outgoing[4] = htonl(lengthRemainder);
  /* send length of my host name to server */
  lengthHost  = strlen(domain->myHost);
  outgoing[5] = htonl(lengthHost);
  /* send length of my name to server */
  lengthName  = strlen(domain->name);
  outgoing[6] = htonl(lengthName);
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: write 7 (%d, %d, %d, %d, %d, %d, %d) ints to server\n",
            CMSG_SERVER_CONNECT, (int) domain->listenPort,
            lengthDomain, lengthSubdomain, lengthRemainder, lengthHost, lengthName);
  }
  
  /* first send all the ints */
  if (cMsgTcpWrite(serverfd, (void *) outgoing, sizeof(outgoing)) != sizeof(outgoing)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: write 5 strings to server\n",
            domain->name);
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
  if (cMsgTcpWrite(serverfd, (void *) UDLremainder, lengthRemainder) != lengthRemainder) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
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

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: read error reply from server\n");
  }
  
  /* now read server reply */
  if (cMsgTcpRead(serverfd, (void *) &err, sizeof(err)) != sizeof(err)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  err = ntohl(err);
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: read err = %d\n", err);
  }
  
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
  
  /* read 5 chars */
  if (cMsgTcpRead(serverfd, (void *) atts, sizeof(atts)) != sizeof(atts)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* store attributes of the subdomain handler being used */
  if (atts[0] == 1) domain->hasSend        = 1;
  if (atts[1] == 1) domain->hasSyncSend    = 1;
  if (atts[2] == 1) domain->hasGet         = 1;
  if (atts[3] == 1) domain->hasSubscribe   = 1;
  if (atts[4] == 1) domain->hasUnsubscribe = 1;
  
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
    struct subscribeCbInfo_t *subscription = (struct subscribeCbInfo_t *) arg;
    int status, thereIsNoMsg=0;
    cMsgMessage *msg;

    /* increase concurrency for this thread for early Solaris */
  #ifdef sun
    int  con;
    con = thr_getconcurrency();
    thr_setconcurrency(con + 1);
  #endif
      
    while(1) {
      /* lock mutex */
      status = pthread_mutex_lock(&subscription->mutex);
      if (status != 0) {
        err_abort(status, "Failed callback mutex lock");
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
      
      /* get first message in linked list */
      msg = subscription->head;
/*
printf("callbackThd: head = %p, tail = %p, head->next = %p\n",
        subscription->head, subscription->tail, subscription->head->next);
*/       
      /* if there are no more messages ... */
      if (msg->next == NULL) {
        subscription->head = NULL;
        subscription->tail = NULL;
        subscription->messages--;
/*
printf("callbackThd: LAST MSG, # = %d, msg = %p, head = %p\n",
        subscription->messages, msg, subscription->head);
*/
      }
      /* else make the next message the head */
      else {
        subscription->head = msg->next;
        subscription->messages--;
/*
printf("callbackThd: # = %d, msg = %p, head = %p\n",
        subscription->messages, msg, subscription->head);
*/
      }
      /*subscription->messages--;*/
     
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


int cMsgRunCallbacks(int domainId, int command, cMsgMessage *msg) {

  int i, j, status;
  dispatchCbInfo *dcbi;
  struct subscribeCbInfo_t *subscription;
  pthread_t newThread;
  cMsgDomain_CODA *domain;
  cMsgMessage *message;
  
  domain = &cMsgDomains[domainId];
  
  /* callbacks have been stopped */
  if (domain->receiveState == 0) {
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cMsgRunCallbacks: all callbacks have been stopped\n");
    }
    free(msg);
    return (CMSG_OK);
  }
 
  switch(command) {

    case CMSG_SERVER_RESPONSE:
      break;

    case CMSG_GET_RESPONSE:
      break;

    /* if message for user, launch callback in new thread */
    case CMSG_SUBSCRIBE_RESPONSE:      

      /* search entry list */
      for (i=0; i<MAXSUBSCRIBE; i++) {
        /* if the subject/type id's match, run callbacks for this sub/type */
        if ( (domain->subscribeInfo[i].active == 1) &&
             (domain->subscribeInfo[i].id == msg->receiverSubscribeId)) {

          /* search callback list */
          for (j=0; j<MAXCALLBACK; j++) {
	    /* if there is an existing callback ... */
            if (domain->subscribeInfo[i].cbInfo[j].callback != NULL) {
                           
              subscription = &domain->subscribeInfo[i].cbInfo[j];
              
              status = pthread_mutex_lock(&subscription->mutex);
              if (status != 0) {
                err_abort(status, "Failed callback mutex lock");
              }

              /* copy message so each callback has its own copy */
              message = (cMsgMessage *) cMsgCopyMessage((void *)msg);
              /*message = msg;*/
              
              /* add this message to linked list for this callback */       

              /* if there are no messages ... */
              if (subscription->head == NULL) {
                subscription->head = message;
                subscription->tail = message;
/*fprintf(stderr, "cMsgRunCallbacks: add msg to HEAD (%p)\n", message);*/
              }
              /* else put message after the tail */
              else {
                subscription->tail->next = message;
                subscription->tail = message;
/*fprintf(stderr, "cMsgRunCallbacks: add msg to TAIL (%p)\n", message);*/
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

              if (cMsgDebug >= CMSG_DEBUG_INFO) {
                fprintf(stderr, "cMsgRunCallbacks, #MSGS = %d\n", subscription->messages);
              }
               
	    }
          }
        }
      }

      break;

    default:
      break;
  }
  
  cMsgFreeMessage((void *)msg);
  
  return (CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgReadMessage(int fd, cMsgMessage *msg) {
  
  int err, time, lengths[5], inComing[11];
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
  msg->receiverSubscribeId = ntohl(inComing[1]);  /* id maps to a subject/type pair */
  msg->senderId            = ntohl(inComing[2]);  /*  */
  msg->senderTime = (time_t) ntohl(inComing[3]);  /* time in sec since Jan 1, 1970 */
  msg->senderMsgId         = ntohl(inComing[4]);  /*  */
  msg->senderToken         = ntohl(inComing[5]);  /*  */
  lengths[0]               = ntohl(inComing[6]);  /* sender length */
  lengths[1]               = ntohl(inComing[7]);  /* senderHost length */
  lengths[2]               = ntohl(inComing[8]);  /* subject length */
  lengths[3]               = ntohl(inComing[9]);  /* type length */
  lengths[4]               = ntohl(inComing[10]); /* text length */
  
  /*
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "  readMessage: read ints\n");
      fprintf(stderr, "    sysMsgId = %d\n", msg->sysMsgId);
      fprintf(stderr, "    receiverSubscribeId = %d\n", msg->receiverSubscribeId);
      fprintf(stderr, "    senderId = %d\n", msg->senderId);
      fprintf(stderr, "    senderTime = %d\n", msg->senderTime);
      fprintf(stderr, "    senderMsgId = %d\n", msg->senderMsgId);
      fprintf(stderr, "    senderToken = %d\n", msg->senderToken);
      fprintf(stderr, "    sender len = %d\n",lengths[0] );
      fprintf(stderr, "    sender host len = %d\n",lengths[1] );
      fprintf(stderr, "    subject len = %d\n",lengths[2] );
      fprintf(stderr, "    type len = %d\n",lengths[3] );
      fprintf(stderr, "    text len = %d\n",lengths[4] );
  }
 */
 
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


static int parseUDL(const char *UDL, char **domainType, char **host,
                    unsigned short *port, char **subdomainType, char **UDLremainder) {

  /* note: cMsg UDL is of the form:
           cMsg:<domainType>://<host>:<port>/<subdomainType>/<remainder>
     where the first "cMsg:" is optional. If it is the cMsg domainType,
     then the subdomain is optional with the default being cMsg.
  */

  int i, Port;
  char *p, *portString, *udl, *pudl, *pdomainType;

  if (UDL  == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  /* strtok modifies the string it tokenizes, so make a copy */
  pudl = udl = (char *) strdup(UDL);
  
/*printf("UDL = %s\n", udl);*/

  /*
   * Check to see if optional "cMsg:" in front.
   * Start by looking for any occurance.
   */  
  p = strstr(udl, "cMsg:");
  
  /* if there a "cMsg:" in front ... */
  if (p == udl) {
    /* if there is still the domain before "://", strip off first "cMsg:" */
    pudl = udl+5;
    p = strstr(pudl, "//");
    if (p == pudl) {
      pudl = udl;
    }
  }
    
  /* get tokens separated by ":" or "/" */
  
  /* find domain */
  if ( (p = (char *) strtok(pudl, ":/")) == NULL) {
    free(udl);
    return (CMSG_BAD_FORMAT);
  }
  if (domainType != NULL) *domainType = (char *) strdup(p);
  pdomainType = (char *) strdup(p);
/*printf("domainType = %s\n", p);*/  
  
  /* find host */
  if ( (p = (char *) strtok(NULL, ":/")) == NULL) {
    if (domainType != NULL)  free(*domainType);
    free (pdomainType);
    free(udl);
    return (CMSG_BAD_FORMAT);
  }
  if (host != NULL) *host =(char *) strdup(p);
/*printf("host = %s\n", p);*/
  
  
  /* find port */
  if ( (p = (char *) strtok(NULL, "/")) == NULL) {
    if (host != NULL)       free(*host);
    if (domainType != NULL) free(*domainType);
    free (pdomainType);
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
    if (domainType != NULL) free((void *) *domainType);
    free (pdomainType);
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
    if ((strcmp(pdomainType, "cMsg") == 0) && (subdomainType != NULL)) {
        *subdomainType = (char *) strdup("cMsg");
    }
/*printf("subdomainType = cMsg\n");*/
    free(udl);
    return(CMSG_OK);
  }
  
/*printf("subdomainType = %s\n", p);*/

  /* find UDL remainder */
  if ( (p = (char *) strtok(NULL, "")) != NULL) {
    if (UDLremainder != NULL) {
      *UDLremainder = (char *) strdup(p);
    }
/*printf("remainder = %s\n", p);*/
  }
  
  /* UDL parsed ok */
  free (pdomainType);
  free(udl);
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/


static void domainInit(cMsgDomain_CODA *domain) {
  int i, j;
 
  domain->initComplete    = 0;
  domain->id              = 0;

  domain->receiveState    = 0;
  domain->lostConnection  = 0;
      
  domain->sendSocket      = 0;
  domain->listenSocket    = 0;
  domain->keepAliveSocket = 0;
  
  domain->sendPort        = 0;
  domain->serverPort      = 0;
  domain->listenPort      = 0;
  
  domain->hasSend         = 0;
  domain->hasSyncSend     = 0;
  domain->hasGet          = 0;
  domain->hasSubscribe    = 0;
  domain->hasUnsubscribe  = 0;

  domain->myHost          = NULL;
  domain->sendHost        = NULL;
  domain->serverHost      = NULL;
  
  domain->name            = NULL;
  domain->udl             = NULL;
  domain->description     = NULL;

  /* pthread_mutex_init mallocs memory */
  pthread_mutex_init(&domain->sendMutex, NULL);
  pthread_mutex_init(&domain->subscribeMutex, NULL);
  
  for (i=0; i<MAXSUBSCRIBE; i++) {
    domain->subscribeInfo[i].id      = 0;
    domain->subscribeInfo[i].active  = 0;
    domain->subscribeInfo[i].type    = NULL;
    domain->subscribeInfo[i].subject = NULL;
    
    for (j=0; j<MAXCALLBACK; j++) {
      domain->subscribeInfo[i].cbInfo[j].quit     = 0;
      domain->subscribeInfo[i].cbInfo[j].messages = 0;
      domain->subscribeInfo[i].cbInfo[j].callback = NULL;
      domain->subscribeInfo[i].cbInfo[j].userArg  = NULL;
      domain->subscribeInfo[i].cbInfo[j].head     = NULL;
      domain->subscribeInfo[i].cbInfo[j].tail     = NULL;
      pthread_cond_init (&domain->subscribeInfo[i].cbInfo[j].cond,  NULL);
      pthread_mutex_init(&domain->subscribeInfo[i].cbInfo[j].mutex, NULL);
    }
  }
}


/*-------------------------------------------------------------------*/


static void domainFree(cMsgDomain_CODA *domain) {  
  int i;
  
  if (domain->myHost      != NULL) free(domain->myHost);
  if (domain->sendHost    != NULL) free(domain->sendHost);
  if (domain->serverHost  != NULL) free(domain->serverHost);
  if (domain->name        != NULL) free(domain->name);
  if (domain->udl         != NULL) free(domain->udl);
  if (domain->description != NULL) free(domain->description);
  
  /* pthread_mutex_destroy frees memory */
  pthread_mutex_destroy(&domain->sendMutex);
  pthread_mutex_destroy(&domain->subscribeMutex);
  
  for (i=0; i<MAXSUBSCRIBE; i++) {
    if (domain->subscribeInfo[i].type != NULL) {
      free(domain->subscribeInfo[i].type);
    }
    if (domain->subscribeInfo[i].subject != NULL) {
      free(domain->subscribeInfo[i].subject);
    }
  }
}


/*-------------------------------------------------------------------*/


static void domainClear(cMsgDomain_CODA *domain) {
  domainFree(domain);
  domainInit(domain);
}


/*-------------------------------------------------------------------*/


static void *dispatchCallback(void *param) {
    
  dispatchCbInfo *dcbi;

  dcbi = (dispatchCbInfo*)param;
  dcbi->callback(dcbi->msg, dcbi->userArg);
  free((void *) dcbi->msg);
  free((void *) dcbi);
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


static void connectMutexLock(void) {

  int status = pthread_mutex_lock(&connectMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static void connectMutexUnlock(void) {

  int status = pthread_mutex_unlock(&connectMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


static int sendMutexLock(cMsgDomain_CODA *domain) {

  int status;
  
  status = pthread_mutex_lock(&domain->sendMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static int sendMutexUnlock(cMsgDomain_CODA *domain) {

  int status;

  status = pthread_mutex_unlock(&domain->sendMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


static int subscribeMutexLock(cMsgDomain_CODA *domain) {

  int status;
  
  status = pthread_mutex_lock(&domain->subscribeMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static int subscribeMutexUnlock(cMsgDomain_CODA *domain) {

  int status;

  status = pthread_mutex_unlock(&domain->subscribeMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/

/*-------------------------------------------------------------------*/



#ifdef __cplusplus
}
#endif


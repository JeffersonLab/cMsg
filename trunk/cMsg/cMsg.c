/*----------------------------------------------------------------------------*
 *
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    E.Wolin, 17-Jun-2004, Jefferson Lab                                     *
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
 *  Implements cMsg client api using CODA FIPA agent system
 *
 * still to do:
 *    integrate with Carl's stuff
 *    make everything thread-safe,
 *    get static assignments correct (dispatchCallback?)
 *    need to lock various parts of code, e.g. cMsgConnect, ...
 *    is atexit handler needed?
 *
 *----------------------------------------------------------------------------*/


/* system includes */
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <pthread.h>


/* package includes */
#include "cMsgNetwork.h"
#include "cMsgPrivate.h"
#include "cMsg.h"


/* built-in limits */
#define MAXSUBSCRIBE 100
#define MAXCALLBACK   10
#define MAXDOMAINS   100


/* user's domain id is index into "domains" array, offset by this amount + */
#define DOMAIN_ID_OFFSET 100


/* for dispatching callbacks in their own threads */
typedef struct dispatchCbInfo_t {
  cMsgCallback *callback;
  void *userArg;
  cMsg *msg;
} dispatchCbInfo;


/* for subscribe lists */
struct subscribeCbInfo_t {
  cMsgCallback *callback;
  void *userArg;
};

struct subscribeInfo_t {
  int  id;       /* unique id # corresponding to a unique subject/type pair */
  int  active;   /* does this subject/type have valid callbacks? */
  char *type;
  char *subject;
  struct subscribeCbInfo_t cbInfo[MAXCALLBACK];
};

/* structure containing all domain info */
typedef struct cMsgDomain_t {
  /* init state */
  int initComplete; /* 0 = No, 1 = Yes */

  /* other state variables */
  int receiveState;
  int lostConnection;
  
  int sendSocket;            /* file descriptor for TCP socket to send messages on */
  int listenSocket;          /* file descriptor for socket this program listens on for TCP connections */

  unsigned short sendPort;   /* port to send messages to */
  unsigned short serverPort; /* port cMsg name server listens on */
  unsigned short listenPort; /* port this program listens on for this domain's TCP connections */
  
  pthread_t pendThread; /* listening thread */

  char *myHost;      /* this hostname */
  char *sendHost;    /* host to send messages to */
  char *serverHost;  /* host of cMsg name server */
  
  char *type;        /* domain type (coda, JMS, SmartSockets, etc.) */
  char *name;        /* name of user (this program) */
  char *domain;      /* UDL of cMsg name server */
  char *description; /* user description */
  
  pthread_mutex_t sendMutex;      /* mutex to ensure thread-safety of send socket */
  pthread_mutex_t subscribeMutex; /* mutex to ensure thread-safety of (un)subscribes */
  
  struct subscribeInfo_t subscribeInfo[MAXSUBSCRIBE];
} cMsgDomain;


/* static variables */
static int oneTimeInitialized = 0;
static pthread_mutex_t connectMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t generalMutex = PTHREAD_MUTEX_INITIALIZER;
static cMsgDomain domains[MAXDOMAINS];
static char *excludedChars = "`\'\"";
/* id which uniquely defines a subject/type pair */
static int subjectTypeId = 1;

/* set the debug level here */
static int debug = CMSG_DEBUG_INFO;

/* for c++ */
#ifdef __cplusplus
extern "C" {
#endif


/* local prototypes */
static int checkString(char *s);
static cMsg *extractMsg(void *);
static cMsg *copyMsg(cMsg *msgIn);
static void *dispatchCallback(void *param);
static void *pend(void *param);

static void domainIdInit(int domainId);
static void domainIdFree(int domainId);
static void domainIdClear(int domainId);

static void mutexLock(void);
static void mutexUnlock(void);
static void connectMutexLock(void);
static void connectMutexUnlock(void);
static int  sendMutexLock(int id);
static int  sendMutexUnlock(int id);
static int  subscribeMutexLock(int id);
static int  subscribeMutexUnlock(int id);

static int  parseUDL(const char *UDL, char **domainType, char **host,
                     unsigned short *port, char **remainder);
static int  getHostAndPortFromNameServer(cMsgDomain domain, unsigned short port,
                                         const char *myName, int serverfd);



/*-------------------------------------------------------------------*/


int cMsgConnect(char *myDomain, char *myName, char *myDescription, int *domainId) {

  int i, id=-1, err, serverfd, status;
  char *portEnvVariable=NULL;
  unsigned short startingPort, port;
  mainThreadInfo threadArg;
  pthread_t pendThread;
  
  /* First, grab mutex for thread safety. This mutex must be held until
   * the initialization is completely finished. Otherwise, if we set
   * initComplete = 1 (so that we reserve space in the domains array)
   * before it's finished and then release the mutex, we may give an
   * "existing" connection to a user who does a second init
   * when in fact, an error may still occur in that "existing"
   * connection. Hope you caught that.
   */
  connectMutexLock();
  
  /* do one time initialization of "domains" array */
  if (!oneTimeInitialized) {
    for (i=0; i<MAXDOMAINS; i++) {
      domainIdInit(i);
    }
    oneTimeInitialized = 1;
  }
  
  /* find an existing connection to this domain if possible */
  for (i=0; i<MAXDOMAINS; i++) {
    if (domains[i].initComplete == 1   &&
        domains[i].name        != NULL &&
        domains[i].domain      != NULL &&
        domains[i].description != NULL)  {
        
      if ( (strcmp(domains[i].name,        myName       ) == 0)  &&
           (strcmp(domains[i].domain,      myDomain     ) == 0)  &&
           (strcmp(domains[i].description, myDescription) == 0) )  {
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
  
  /* no existing connection, so find the first available place in the "domains" array */
  for (i=0; i<MAXDOMAINS; i++) {
    if (domains[i].initComplete > 0) {
      continue;
    }
    domainIdInit(i);
    id = i;
  }
  
  /* exceeds number of domain connections allowed */
  if (id < 0) {
    connectMutexUnlock();
    return(CMSG_LIMIT_EXCEEDED);
  }

  /* reserve this element of the "domains" array */
  domains[id].initComplete = 1;
      
  /* check args */
  if ( (checkString(myName)        != CMSG_OK)  ||
       (checkString(myDomain)      != CMSG_OK)  ||
       (checkString(myDescription) != CMSG_OK) )  {
    domainIdInit(id);
    connectMutexUnlock();
    return(CMSG_BAD_ARGUMENT);
  }
    
  /* store names, can be changed until server connection established */
  domains[id].name        = (char *) strdup(myName);
  domains[id].domain      = (char *) strdup(myDomain);
  domains[id].description = (char *) strdup(myDescription);
  gethostname(domains[id].myHost, sizeof(domains[id].myHost));
  
  /* parse the UDL - Uniform Domain Locator */
  if ( (err = parseUDL(myDomain,
                       &domains[id].type,
                       &domains[id].serverHost,
                       &domains[id].serverPort,
                       NULL)) != CMSG_OK ) {
    /* there's been a parsing error */
    domainIdClear(id);
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
   * If CMSG_PORT is NOT defind, start at 2345.
   */

  /* pick starting port number */
  if ( (portEnvVariable = getenv("CMSG_PORT")) == NULL ) {
    startingPort = 2345;
    if (debug >= CMSG_DEBUG_WARN) {
      fprintf(stderr, "cMsgConnect: cannot find CMSG_PORT env variable, first try port %hu\n", startingPort);
    }
  }
  else {
    i = atoi(portEnvVariable);
    if (i < 1025 || i > 65535) {
      port = 2345;
      if (debug >= CMSG_DEBUG_WARN) {
        fprintf(stderr, "cMsgConnect: CMSG_PORT contains a bad port #, first try port %hu\n", startingPort);
      }
    }
    else {
      startingPort = i;
    }
  }

  /* get listening port and socket for this application */
  if ( (err = cMsgGetListeningSocket(CMSG_NONBLOCKING,
                                     startingPort,
                                     &domains[id].listenPort,
                                     &domains[id].listenSocket)) != CMSG_OK) {
    domainIdClear(id);
    connectMutexUnlock();
    return(err);
  }

  /* launch pend thread and start listening on receive socket */
  threadArg.domainId = id;
  threadArg.listenFd = domains[id].listenSocket;
  threadArg.blocking = CMSG_NONBLOCKING;
  status = pthread_create(&pendThread, NULL, cMsgServerListeningThread, (void *)&threadArg);
  if (status != 0) {
    err_abort(status, "Creating message listening thread");
  }
     
  /*---------------------------------------------------------------*/
  /* connect & talk to cMsg name server to check if name is unique */
  /*---------------------------------------------------------------*/
    
  /* first connect to server host & port */
  if ( (err = cMsgTcpConnect(domains[id].serverHost,
                             domains[id].serverPort,
                             &serverfd)) != CMSG_OK) {
    /* stop listening & connection threads */
    pthread_cancel(pendThread);
    domainIdClear(id);
    connectMutexUnlock();
    return(err);
  }
  
  /* get host & port to send messages to */
  err = getHostAndPortFromNameServer(domains[id], port, myName, serverfd);
  if (err != CMSG_OK) {
    close(serverfd);
    pthread_cancel(pendThread);
    domainIdClear(id);
    connectMutexUnlock();
    return(err);
  }
  
  /* done talking to server */
  close(serverfd);
  
  /* create sending socket and store */
  if ( (err = cMsgTcpConnect(domains[id].sendHost,
                             domains[id].sendPort,
                             &domains[id].sendSocket)) != CMSG_OK) {
    close(serverfd);
    pthread_cancel(pendThread);
    domainIdClear(id);
    connectMutexUnlock();
    return(err);
  }

  /* init is complete */
  domains[id].initComplete = 1;
  
  /* no more mutex protection is necessary */
  connectMutexUnlock();
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int getHostAndPortFromNameServer(cMsgDomain domain, unsigned short port,
                                        const char *myName, int serverfd) {
  int msgId, err, length, len;
  unsigned short portNet;
  

  /* first send message id (in network byte order) to server */
  msgId = htonl(CMSG_SERVER_CONNECT);
  if (cMsgTcpWrite(serverfd, (void *) &msgId, sizeof(msgId)) != sizeof(msgId)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send my listening port (in network byte order) to server */
  portNet = htons(port);
  if (cMsgTcpWrite(serverfd, (void *) &portNet, sizeof(portNet)) != sizeof(portNet)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send length (in network byte order) of my host name to server */
  length = strlen(domain.myHost);
  len    = htonl(length);
  if (cMsgTcpWrite(serverfd, (void *) &len, sizeof(len)) != sizeof(len)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send my host name to server */
  if (cMsgTcpWrite(serverfd, (void *) domain.myHost, sizeof(length)) != sizeof(length)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send length (in network byte order) of my name to server */
  length = strlen(myName);
  len    = htonl(length);
  if (cMsgTcpWrite(serverfd, (void *) &len, sizeof(len)) != sizeof(len)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send my name to server */
  if (cMsgTcpWrite(serverfd, (void *) myName, sizeof(length)) != sizeof(length)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* now read server reply */
  if (cMsgTcpRead(serverfd, (void *) &err, sizeof(err)) != sizeof(err)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  err = ntohl(err);
  
  /* if there's an error, quit */
  if (err != CMSG_OK) {
    return(CMSG_NETWORK_ERROR);
  }
  
  /* if everything's OK, we expect to get send host & port */
  
  /* first read port */
  if (cMsgTcpRead(serverfd, (void *) &domain.sendPort,
                  sizeof(domain.sendPort)) != sizeof(domain.sendPort)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  domain.sendPort = ntohs(domain.sendPort);
  
  /* read length of host name to send to */
  if (cMsgTcpRead(serverfd, (void *) &length, sizeof(length)) != sizeof(length)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  length = ntohl(length);

  /* read host name to send to */
  if (cMsgTcpRead(serverfd, (void *) domain.sendHost, sizeof(length)) != sizeof(length)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgSend(int domainId, char *subject, char *type, char *text) {
  
  int id = domainId - DOMAIN_ID_OFFSET;
  int fd = domains[id].sendSocket;
  int err, lenSubject, lenType, lenText;
  int outGoing[4];
  
  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[id].lostConnection == 1) return(CMSG_LOST_CONNECTION);

  /* check args */
  if ( (checkString(subject) !=0 )  ||
       (checkString(type)    !=0 )  ||
       (text == NULL)              )  {
    return(CMSG_BAD_ARGUMENT);
  }
  

  /* message id (in network byte order) to domain server */
  outGoing[0] = htonl(CMSG_SEND_REQUEST);
  /* length of "subject" string */
  lenSubject  = strlen(subject);
  outGoing[1] = htonl(lenSubject);
  /* length of "type" string */
  lenType     = strlen(type);
  outGoing[2] = htonl(lenType);
  /* length of "text" string */
  lenText     = strlen(text);
  outGoing[3] = htonl(lenText);

  /* make send socket communications thread-safe */
  sendMutexLock(id);

  /* send ints over together */
  if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
    sendMutexUnlock(id);
    subscribeMutexUnlock(id);
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send subject */
  if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
    sendMutexUnlock(id);
    subscribeMutexUnlock(id);
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send type */
  if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
    sendMutexUnlock(id);
    subscribeMutexUnlock(id);
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send text */
  if (cMsgTcpWrite(fd, (void *) text, lenText) != lenText) {
    sendMutexUnlock(id);
    subscribeMutexUnlock(id);
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* now read reply */
  if (cMsgTcpRead(fd, (void *) &err, sizeof(err)) != sizeof(err)) {
    sendMutexUnlock(id);
    subscribeMutexUnlock(id);
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgSend: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* done protecting communications */
  sendMutexUnlock(id);

  /* return domain server's reply */
  err = ntohl(err);
  return(err);
}


/*-------------------------------------------------------------------*/


int cMsgFlush(int domainId) {
  
  int id = domainId - DOMAIN_ID_OFFSET;
  int fd = domains[id].sendSocket;

  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[id].lostConnection == 1) return(CMSG_LOST_CONNECTION);

  /* make send socket communications thread-safe */
  sendMutexLock(id);
  /* flush outgoing buffers */
  flush(fd);
  /* done with mutex */
  sendMutexUnlock(id);
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgGet(int domainId, char *subject, char *type, time_t timeout, cMsg **msg) {
  
  int id = domainId - DOMAIN_ID_OFFSET;
  int fd = domains[id].sendSocket;
  int err;
  cMsg *message;

  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[id].lostConnection == 1) return(CMSG_LOST_CONNECTION);

  /* check args */
  if ( (checkString(subject) !=0 )  ||
       (checkString(type)    !=0 )  ||
       (msg == NULL)               )  {
    return(CMSG_BAD_ARGUMENT);
  }
  
  /* tell domain server to get 1 message if possible */
  
  
  /* get response from server telling us how many messges there are */
  
  /*
  message = (cMsg *) malloc(sizeof(cMsg));
  *msg = message;
  */


  return(CMSG_NOT_IMPLEMENTED);
}

 
/*-------------------------------------------------------------------*/


int cMsgSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback, void *userArg) {

  int i, j, iok, jok, uniqueId;
  int id = domainId - DOMAIN_ID_OFFSET;

  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[id].lostConnection == 1) return(CMSG_LOST_CONNECTION);

  /* check args */
  if( (checkString(subject) !=0 )  ||
      (checkString(type)    !=0 )  ||
      (callback == NULL)          )  {
    return(CMSG_BAD_ARGUMENT);
  }
  
  /* make sure subscribe and unsubscribe are not run at the same time */
  subscribeMutexLock(id);
  
  /* add to callback list if subscribe to same subject/type exists */
  iok = 0;
  for (i=0; i<MAXSUBSCRIBE; i++) {
    if ((domains[id].subscribeInfo[i].active == 1) && 
       (strcmp(domains[id].subscribeInfo[i].subject, subject) == 0) && 
       (strcmp(domains[id].subscribeInfo[i].type, type) == 0) ) {
      iok = 1;

      jok = 0;
      for (j=0; j<MAXCALLBACK; j++) {
	if (domains[id].subscribeInfo[i].cbInfo[j].callback == NULL) {
	  domains[id].subscribeInfo[i].cbInfo[j].callback = callback;
	  domains[id].subscribeInfo[i].cbInfo[j].userArg  = userArg;
	  jok = 1;
	}
      }
      break;

    }
  }
  
  if ((iok == 1) && (jok == 0)) return(CMSG_OUT_OF_MEMORY);
  if ((iok == 1) && (jok == 1)) return(CMSG_OK);

  /* no match, make new entry and notify server */
  iok = 0;
  for (i=0; i<MAXSUBSCRIBE; i++) {
    if (domains[id].subscribeInfo[i].active == 0) {

      int err, lenSubject, lenType;
      int fd = domains[id].sendSocket;
      int outGoing[4];

      domains[id].subscribeInfo[i].active  = 1;
      domains[id].subscribeInfo[i].subject = (char *) strdup(subject);
      domains[id].subscribeInfo[i].type    = (char *) strdup(type);
      domains[id].subscribeInfo[i].cbInfo[j].callback = callback;
      domains[id].subscribeInfo[i].cbInfo[j].userArg  = userArg;
      iok = 1;
      
      /*
       * Pick a unique identifier for the subject/type pair, and
       * send it to the domain server & remember it for future use
       * Mutex protect this operation as many cMsgConnect calls may
       * operate in parallel on this static variable.
       */
      mutexLock();
      uniqueId = subjectTypeId++;
      mutexUnlock();
      domains[id].subscribeInfo[i].id = uniqueId;
      
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
      sendMutexLock(id);

      /* send ints over together */
      if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
        sendMutexUnlock(id);
        subscribeMutexUnlock(id);
        if (debug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cMsgSubscribe: write failure\n");
        }
        return(CMSG_NETWORK_ERROR);
      }

      /* send subject */
      if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
        sendMutexUnlock(id);
        subscribeMutexUnlock(id);
        if (debug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cMsgSubscribe: write failure\n");
        }
        return(CMSG_NETWORK_ERROR);
      }

      /* send type */
      if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
        sendMutexUnlock(id);
        subscribeMutexUnlock(id);
        if (debug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cMsgSubscribe: write failure\n");
        }
        return(CMSG_NETWORK_ERROR);
      }
      
      /* now read reply */
      if (cMsgTcpRead(fd, (void *) &err, sizeof(err)) != sizeof(err)) {
        sendMutexUnlock(id);
        subscribeMutexUnlock(id);
        if (debug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cMsgSubscribe: read failure\n");
        }
        return(CMSG_NETWORK_ERROR);
      }
      
      /* done protecting communications */
      sendMutexUnlock(id);
      /* done protecting subscribe */
      subscribeMutexUnlock(id);
        
      /* return domain server's reply */
      err = ntohl(err);
      return(err);
    }
  }
  
  /* done protecting subscribe */
  subscribeMutexUnlock(id);
  
  /* iok == 0 here */
  return(CMSG_OUT_OF_MEMORY);
}


/*-------------------------------------------------------------------*/


int cMsgUnSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback) {

  int i, j, cbCount, cbsRemoved;
  int id = domainId - DOMAIN_ID_OFFSET;


  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[id].lostConnection == 1) return(CMSG_LOST_CONNECTION);


  /* check args */
  if( (checkString(subject) != 0)  ||
      (checkString(type)    != 0) )  {
    return(CMSG_BAD_ARGUMENT);
  }
  
  /* make sure subscribe and unsubscribe are not run at the same time */
  subscribeMutexLock(id);
  
  /* search entry list */
  for (i=0; i<MAXSUBSCRIBE; i++) {
    if ( (domains[id].subscribeInfo[i].active == 1) && 
         (strcmp(domains[id].subscribeInfo[i].subject, subject) == 0)  && 
         (strcmp(domains[id].subscribeInfo[i].type,    type)    == 0) )  {

      /* search callback list */
      cbCount = cbsRemoved = 0;
      for (j=0; j<MAXCALLBACK; j++) {
	if (domains[id].subscribeInfo[i].cbInfo[j].callback != NULL) {
	  cbCount++;
	  if (domains[id].subscribeInfo[i].cbInfo[j].callback == callback) {
            domains[id].subscribeInfo[i].cbInfo[j].callback == NULL;
            cbsRemoved++;
          }
	}
      }


      /* delete entry and notify server if there was at least 1 callback
       * to begin with and now there are none for this subject/type */
      if ((cbCount > 0) && (cbCount-cbsRemoved < 1)) {

        int err, lenSubject, lenType;
        int fd = domains[id].sendSocket;
        int outGoing[3];

 	domains[id].subscribeInfo[i].active = 0;
	free(domains[id].subscribeInfo[i].subject);
	free(domains[id].subscribeInfo[i].type);

	/* notify server */
        
        /* message id (in network byte order) to domain server */
        outGoing[0] = htonl(CMSG_UNSUBSCRIBE_REQUEST);
        /* length of "subject" string */
        lenSubject  = strlen(subject);
        outGoing[1] = htonl(lenSubject);
        /* length of "type" string */
        lenType     = strlen(type);
        outGoing[2] = htonl(lenType);
        
        /* make send socket communications thread-safe */
        sendMutexLock(id);

        /* send ints over together */
        if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
          sendMutexUnlock(id);
          subscribeMutexUnlock(id);
          if (debug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "cMsgSubscribe: write failure\n");
          }
          return(CMSG_NETWORK_ERROR);
        }

        /* send subject */
        if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
          sendMutexUnlock(id);
          subscribeMutexUnlock(id);
          if (debug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "cMsgSubscribe: write failure\n");
          }
          return(CMSG_NETWORK_ERROR);
        }

        /* send type */
        if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
          sendMutexUnlock(id);
          subscribeMutexUnlock(id);
          if (debug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "cMsgSubscribe: write failure\n");
          }
          return(CMSG_NETWORK_ERROR);
        }

        /* now read reply */
        if (cMsgTcpRead(fd, (void *) &err, sizeof(err)) != sizeof(err)) {
          sendMutexUnlock(id);
          subscribeMutexUnlock(id);
          if (debug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "cMsgSubscribe: read failure\n");
          }
          return(CMSG_NETWORK_ERROR);
        }
        
        /* done protecting communications */
        sendMutexUnlock(id);
        /* done protecting unsubscribe */
        subscribeMutexUnlock(id);
        
        /* return domain server's reply */
        err = ntohl(err);
        return(err);

      }
      break;
      
    }
  }

  /* done protecting unsubscribe */
  subscribeMutexUnlock(id);
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/* start & stop not used yet */

int cMsgReceiveStart(int domainId) {
  int id = domainId - DOMAIN_ID_OFFSET;

  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[id].lostConnection == 1) return(CMSG_LOST_CONNECTION);

  domains[id].receiveState = 1;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgReceiveStop(int domainId) {
  int id = domainId - DOMAIN_ID_OFFSET;

  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[id].lostConnection == 1) return(CMSG_LOST_CONNECTION);

  domains[id].receiveState = 0;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgFree(cMsg *msg) {

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);

  free(msg->sender);
  free(msg->senderHost);
  free(msg->receiver);
  free(msg->receiverHost);
  free(msg->domain);
  free(msg->subject);
  free(msg->type);
  free(msg->text);
  free(msg);

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgDone(int domainId) {
  
  int status;
  int id = domainId - DOMAIN_ID_OFFSET;

  if (domains[id].initComplete != 1) return(CMSG_NOT_INITIALIZED);
  
  /* When changing initComplete / connection status, mutex protect it */
  connectMutexLock();
  
  /* close sending and listening sockets */
  close(domains[id].sendSocket);
  close(domains[id].listenSocket);

  /* stop listening and client communication threads */
  status = pthread_cancel(domains[id].pendThread);
  if (status != 0) {
    err_abort(status, "Cancelling message listening & client threads");
  }

  /* reset vars, free memory */
  domainIdClear(id);
  
  connectMutexUnlock();

  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/
/* This is not a user function. It's used only by cMsgClientThread.  */

int cMsgRunCallbacks(int domainId, int command, cMsg *msg) {

  int i, j, status, id = domainId;
  dispatchCbInfo *dcbi;
  pthread_t newThread;
  
  /* callbacks have been stopped */
  if (domains[id].receiveState == 0) {
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
      if ( (domains[id].subscribeInfo[i].active == 1) &&
           (domains[id].subscribeInfo[i].id == msg->receiverSubscribeId)) {

        /* search callback list */
        for (j=0; j<MAXCALLBACK; j++) {
	  /* if there is an existing callback ... */
          if (domains[id].subscribeInfo[i].cbInfo[j].callback != NULL) {
            /* allocate memory for thread arg so it doesn't go out-of-scope */
            dcbi = (dispatchCbInfo*) malloc(sizeof(dispatchCbInfo));
            if (dcbi == NULL) {
              if (debug >= CMSG_DEBUG_SEVERE) {
                fprintf(stderr, "cMsgRunCallbacks: cannot allocate memory\n");
              }
              exit(1);
            }
            
	    dcbi->callback = domains[id].subscribeInfo[i].cbInfo[j].callback;
	    dcbi->userArg  = domains[id].subscribeInfo[i].cbInfo[j].userArg;
            /* the message was malloced in cMsgClientThread,
             * so it must be freed in the dispatch thread
             */
	    dcbi->msg = msg;
            
            /* run dispatch thread */
	    status = pthread_create(&newThread, NULL, dispatchCallback, (void *)dcbi);
            if (status != 0) {
              err_abort(status, "Create dispatch callback thread");
            }
	  }
        }
      }
    }
        
    break;

  default:
    break;
  }
  
  return (CMSG_OK);
}


/*-------------------------------------------------------------------*/
/* This routine is used in cMsgGet & cMsgClientThread                */


int readMessage(int fd, cMsg *msg) {
  
  int err, lengths[5], inComing[9];
  int memSize = CMSG_MESSAGE_SIZE;
  char *string, storage[CMSG_MESSAGE_SIZE + 1];
  
  /* Start out with an array of size CMSG_MESSAGE_SIZE + 1
   * for storing strings, If that's too small, allocate more. */
  string = storage;
  
  /* read ints first */
  if (cMsgTcpRead(fd, (void *) inComing, sizeof(inComing)) != sizeof(inComing)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "readMessage: cannot read ints\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* swap to local endian */
  msg->sysMsgId            = ntohl(inComing[0]); /*  */
  msg->receiverSubscribeId = ntohl(inComing[1]); /* id maps to a subject/type pair */
  msg->senderId            = ntohl(inComing[2]); /*  */
  msg->senderMsgId         = ntohl(inComing[3]); /*  */
  lengths[0]               = ntohl(inComing[4]); /* sender length */
  lengths[1]               = ntohl(inComing[5]); /* senderHost length */
  lengths[2]               = ntohl(inComing[6]); /* subject length */
  lengths[3]               = ntohl(inComing[7]); /* type length */
  lengths[4]               = ntohl(inComing[8]); /* text length */
  
  
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
      if (debug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "readMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[0]) != lengths[0]) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "readMessage: cannot read sender\n");
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
      if (debug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "readMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[1]) != lengths[1]) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "readMessage: cannot read senderHost\n");
    }
    if (memSize > CMSG_MESSAGE_SIZE) {
      free((void *) string);
    }
    free((void *) msg->sender);
    return(CMSG_NETWORK_ERROR);
  }
  string[lengths[1]] = 0;
  msg->senderHost = (char *) strdup(string);
    
  
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
      if (debug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "readMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[2]) != lengths[2]) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "readMessage: cannot read senderHost\n");
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
      if (debug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "readMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[3]) != lengths[3]) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "readMessage: cannot read senderHost\n");
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
      if (debug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "readMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[4]) != lengths[4]) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "readMessage: cannot read senderHost\n");
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
      

  /* reply value */
  err = htonl(CMSG_OK);

  if (cMsgTcpWrite(fd, (void *) err, sizeof(err)) != sizeof(err)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgClientThread: cannot send message reply\n");
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

  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cMsgClientThread: msg arrived: %s\n", string);
  }
 
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgPerror(int error) {

  switch(error) {

  case CMSG_OK:
    fprintf(stderr, "CMSG_OK:  action completed successfully\n");
    break;

  case CMSG_ERROR:
    fprintf(stderr, "CMSG_ERROR:  generic error return\n");
    break;

  case CMSG_NOT_IMPLEMENTED:
    fprintf(stderr, "CMSG_NOT_IMPLEMENTED:  function not implemented\n");
    break;

  case CMSG_BAD_ARGUMENT:
    fprintf(stderr, "CMSG_BAD_ARGUMENT:  one or more arguments bad\n");
    break;

  case CMSG_NAME_EXISTS:
    fprintf(stderr, "CMSG_NAME_EXISTS: another process in this project is using this name\n");
    break;

  case CMSG_NOT_INITIALIZED:
    fprintf(stderr, "CMSG_NOT_INITIALIZED:  cMsgConnect needs to be called\n");
    break;

  case CMSG_ALREADY_INIT:
    fprintf(stderr, "CMSG_ALREADY_INIT:  cMsgConnect already called\n");
    break;

  case CMSG_LOST_CONNECTION:
    fprintf(stderr, "CMSG_LOST_CONNECTION:  connection to cMsg server lost\n");
    break;

  case CMSG_TIMEOUT:
    fprintf(stderr, "CMSG_TIMEOUT:  no response from cMsg server within timeout period\n");
    break;

  case CMSG_NETWORK_ERROR:
    fprintf(stderr, "CMSG_NETWORK_ERROR:  error talking to cMsg server\n");
    break;

  case CMSG_PEND_ERROR:
    fprintf(stderr, "CMSG_PEND_ERROR:  error waiting for messages to arrive\n");
    break;

  case CMSG_ILLEGAL_MSGTYPE:
    fprintf(stderr, "CMSG_ILLEGAL_MSGTYPE:  pend received illegal message type\n");
    break;

  case CMSG_OUT_OF_MEMORY:
    fprintf(stderr, "CMSG_OUT_OF_MEMORY:  ran out of memory\n");
    break;

  default:
    fprintf(stderr, "?cMsgPerror...no such error: %d\n",error);
    break;
  }

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*
 *
 * Internal functions
 *
 *-------------------------------------------------------------------*/


/*-------------------------------------------------------------------*/
/* When calling this, worry about mutex protecting initComplete      */

static void domainIdInit(int domainId) {
  int i, j;
  
  domains[domainId].initComplete   = 0;
  
  domains[domainId].receiveState   = 0;
  domains[domainId].lostConnection = 0;
  
  domains[domainId].sendSocket     = 0;
  domains[domainId].listenSocket   = 0;
  
  domains[domainId].sendPort       = 0;
  domains[domainId].serverPort     = 0;
  domains[domainId].listenPort     = 0;
  
  domains[domainId].myHost         = NULL;
  domains[domainId].sendHost       = NULL;
  domains[domainId].serverHost     = NULL;
  domains[domainId].name           = NULL;
  domains[domainId].domain         = NULL;
  domains[domainId].description    = NULL;
  
  /* pthread_mutex_init mallocs memory */
  pthread_mutex_init(&domains[domainId].sendMutex, NULL);
  pthread_mutex_init(&domains[domainId].subscribeMutex, NULL);
  
  for (i=0; i<MAXSUBSCRIBE; i++) {
    domains[domainId].subscribeInfo[i].id      = 0;
    domains[domainId].subscribeInfo[i].active  = 0;
    domains[domainId].subscribeInfo[i].type    = NULL;
    domains[domainId].subscribeInfo[i].subject = NULL;
    
    for (j=0; j<MAXCALLBACK; j++) {
      domains[domainId].subscribeInfo[i].cbInfo[j].callback = NULL;
      domains[domainId].subscribeInfo[i].cbInfo[j].userArg  = NULL;
    }
  }
}


/*-------------------------------------------------------------------*/


static void domainIdFree(int domainId) {
  int i;
  
  if (domains[domainId].myHost      != NULL) free(domains[domainId].myHost);
  if (domains[domainId].sendHost    != NULL) free(domains[domainId].sendHost);
  if (domains[domainId].serverHost  != NULL) free(domains[domainId].serverHost);
  if (domains[domainId].name        != NULL) free(domains[domainId].name);
  if (domains[domainId].domain      != NULL) free(domains[domainId].domain);
  if (domains[domainId].description != NULL) free(domains[domainId].description);
  
  /* pthread_mutex_destroy frees memory */
  pthread_mutex_destroy(&domains[domainId].sendMutex);
  pthread_mutex_destroy(&domains[domainId].subscribeMutex);
  
  for (i=0; i<MAXSUBSCRIBE; i++) {
    if (domains[domainId].subscribeInfo[i].type != NULL) {
      free(domains[domainId].subscribeInfo[i].type);
    }
    if (domains[domainId].subscribeInfo[i].subject != NULL) {
      free(domains[domainId].subscribeInfo[i].subject);
    }
  }
}


/*-------------------------------------------------------------------*/


static void domainIdClear(int domainId) {
  domainIdFree(domainId);
  domainIdInit(domainId);
}


/*-------------------------------------------------------------------*
 * Mutex functions
 *-------------------------------------------------------------------*/


static void connectMutexLock(void)
{  
  int status = pthread_mutex_lock(&connectMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static void connectMutexUnlock(void)
{  
  int status = pthread_mutex_unlock(&connectMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


static void mutexLock(void)
{  
  int status = pthread_mutex_lock(&generalMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static void mutexUnlock(void)
{  
  int status = pthread_mutex_unlock(&generalMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


static int sendMutexLock(int domainId)
{  
  int status;
  
  if (domains[domainId].initComplete != 1) return(CMSG_NOT_INITIALIZED);
  
  status = pthread_mutex_lock(&domains[domainId].sendMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static int sendMutexUnlock(int domainId)
{  
  int status;

  if (domains[domainId].initComplete != 1) return(CMSG_NOT_INITIALIZED);

  status = pthread_mutex_unlock(&domains[domainId].sendMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


static int subscribeMutexLock(int domainId)
{  
  int status;
  
  if (domains[domainId].initComplete != 1) return(CMSG_NOT_INITIALIZED);
  
  status = pthread_mutex_lock(&domains[domainId].subscribeMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static int subscribeMutexUnlock(int domainId)
{  
  int status;

  if (domains[domainId].initComplete != 1) return(CMSG_NOT_INITIALIZED);

  status = pthread_mutex_unlock(&domains[domainId].subscribeMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/
/* An UDL is of the form:                                            */
/*      domainType://host:port/remainder                                */
/*-------------------------------------------------------------------*/


static int parseUDL(const char *UDL, char **domainType, char **host,
                    unsigned short *port, char **remainder) {

  int i;
  char *p, *portString, *udl;

  if (UDL  == NULL ||
      host == NULL ||
      port == NULL ||
      domainType == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  /* strtok modifies the string it tokenizes, so make a copy */
  udl = (char *) strdup(UDL);
  
  /* get tokens separated by ":" or "/" */
  if ( (p = (char *) strtok(udl, ":/")) == NULL) {
    free(udl);
    return (CMSG_BAD_FORMAT);
  }
  *domainType = (char *) strdup(p);
  
  if ( (p = (char *) strtok('/0', ":/")) == NULL) {
    free(*domainType);
    free(udl);
    return (CMSG_BAD_FORMAT);
  }
  *host =(char *)  strdup(p);
  
  if ( (p = (char *) strtok('/0', ":/")) == NULL) {
    free(*host);
    free(*domainType);
    free(udl);
    return (CMSG_BAD_FORMAT);
  }
  portString = (char *) strdup(p);
  *port = atoi(portString);
  if (*port < 1024 || *port > 65535) {
    free((void *) portString);
    free((void *) *host);
    free((void *) *domainType);
    free(udl);
    return (CMSG_OUT_OF_RANGE);
  }
  
  if ( (p = (char *) strtok('/0', ":/")) != NULL  && remainder != NULL) {
    *remainder = (char *) strdup(p);
  }
  
  /* UDL parsed ok */
  free(udl);
  return(CMSG_OK);
}



/*-------------------------------------------------------------------*/


static int checkString(char *s) {

  int i;

  if (s == NULL) return(CMSG_ERROR);

  /* check for printable character */
  for(i=0; i<strlen(s); i++) {
    if (isgraph(s[i])!=0) return(CMSG_ERROR);
  }

  /* check for excluded chars */
  if (strpbrk(s,excludedChars) != 0) return(CMSG_ERROR);

  /* string ok */
  return(CMSG_OK);
}



/*-------------------------------------------------------------------*/


static cMsg *extractMsg(void *something) {

  cMsg *msg;
  
  /* allocate memory for message */
  msg = (cMsg*) malloc(sizeof(cMsg));

  /* fill message fields */
  msg->domainId      =  5;
  msg->sysMsgId	     =	5;
  msg->sender  	     =	(char *) strdup("blah");
  msg->senderId      =	5;
  msg->senderHost    =	(char *) strdup("blah");
  msg->senderTime    =	time(NULL);
  msg->senderMsgId   =	5;
  msg->receiver      =	(char *) strdup("name");
  msg->receiverHost  =	(char *) strdup("host");
  msg->receiverTime  =	time(NULL);
  msg->domain	     =	(char *) strdup("blah");
  msg->subject	     =	(char *) strdup("blah");
  msg->type   	     =	(char *) strdup("blah");
  msg->text   	     =	(char *) strdup("blah");
  
  return(msg);
}



/*-------------------------------------------------------------------*/


static cMsg *copyMsg(cMsg *msgIn) {

  cMsg *msgCopy;
  
  /* allocate memory for copy of message */
  msgCopy = (cMsg*) malloc(sizeof(cMsg));


  /* fill message fields */
  msgCopy->domainId            =  msgIn->domainId;
  msgCopy->sysMsgId            =  msgIn->sysMsgId;
  msgCopy->receiverSubscribeId =  msgIn->receiverSubscribeId;
  msgCopy->sender              =  (char *) strdup(msgIn->sender);
  msgCopy->senderId            =  msgIn->senderId;
  msgCopy->senderHost          =  (char *) strdup(msgIn->senderHost);
  msgCopy->senderTime          =  msgIn->senderTime;
  msgCopy->senderMsgId         =  msgIn->senderMsgId;
  msgCopy->receiver            =  (char *) strdup(msgIn->receiver);
  msgCopy->receiverHost        =  (char *) strdup(msgIn->receiverHost);
  msgCopy->receiverTime        =  time(&msgIn->receiverTime);
  msgCopy->domain              =  (char *) strdup(msgIn->domain);
  msgCopy->subject             =  (char *) strdup(msgIn->subject);
  msgCopy->type                =  (char *) strdup(msgIn->type);
  msgCopy->text                =  (char *) strdup(msgIn->text);
  
  return(msgCopy);
}



/*-------------------------------------------------------------------*/


static void *pend(void *param) {


  dispatchCbInfo *dcbi;
  pthread_t newThread;
  int sysMsgId;


  /* wait for messages */


  /* got one, parse msgSysId and decide what to do */
  /* if user message then copy and launch callback in new thread */
  sysMsgId = 5;
  switch(sysMsgId) {

  case CMSG_SERVER_RESPONSE:
    break;

  case CMSG_KEEP_ALIVE:
    break;

  case CMSG_SHUTDOWN:
    break;

  case CMSG_GET_RESPONSE:
    break;

  case CMSG_SUBSCRIBE_RESPONSE:
    for(;;) {
      if (1) { 
	dcbi=(dispatchCbInfo*) malloc(sizeof(dispatchCbInfo));
        /*
	dcbi->callback=xxx;
	dcbi->userArg =xxx;
	dcbi->msg=extractMsg(???);
        */
	pthread_create(&newThread, NULL, dispatchCallback, (void*)dcbi);
      }
    }
    break;

  default:
    cMsgPerror(CMSG_ILLEGAL_MSGTYPE);
    break;
  }

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
/*-------------------------------------------------------------------*/
/* BUG BUG, accessor functions need to return an error if id bad     */
/* access functions */

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/


char* cMsgGetDomain(int domainId) {
  int id = domainId - DOMAIN_ID_OFFSET;
  return(domains[id].domain);
}
  
  
/*-------------------------------------------------------------------*/


char* cMsgGetName(int domainId) {
  int id = domainId - DOMAIN_ID_OFFSET;
  return(domains[id].name);
}

/*-------------------------------------------------------------------*/


char* cMsgGetDescription(int domainId) {
  int id = domainId - DOMAIN_ID_OFFSET;
  return(domains[id].description);
}


/*-------------------------------------------------------------------*/


char* cMsgGetHost(int domainId) {
  int id = domainId - DOMAIN_ID_OFFSET;
  return(domains[id].myHost);
}


/*-------------------------------------------------------------------*/


int cMsgGetSendSocket(int domainId) {
  int id = domainId - DOMAIN_ID_OFFSET;
  return(domains[id].sendSocket);
}


/*-------------------------------------------------------------------*/


int cMsgGetReceiveSocket(int domainId) {
  int id = domainId - DOMAIN_ID_OFFSET;
  return(domains[id].listenSocket);
}


/*-------------------------------------------------------------------*/


pthread_t cMsgGetPendThread(int domainId) {
  int id = domainId - DOMAIN_ID_OFFSET;
  return(domains[id].pendThread);
}


/*-------------------------------------------------------------------*/


int cMsgGetInitState(int domainId) {
  int id = domainId - DOMAIN_ID_OFFSET;
  return(domains[id].initComplete);
}


/*-------------------------------------------------------------------*/


int cMsgGetReceiveState(int domainId) {
  int id = domainId - DOMAIN_ID_OFFSET;
  return(domains[id].receiveState);
}


/*-------------------------------------------------------------------*/


#ifdef __cplusplus
}
#endif


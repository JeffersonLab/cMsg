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


/* package includes */
#include "cMsgNetwork.h"
#include "cMsgPrivate.h"
#include "cMsg.h"
#include "cMsg_CODA.h"


/* built-in limits */
#define MAXDOMAINS_CODA 10


static int   coda_connect(char *myUDL, char *myName, char *myDescription, int *domainId);
static int   coda_send(int domainId, void *msg);
static int   flush(int domainId);
static int   subscribe(int domainId, char *subject, char *type, cMsgCallback *callback, void *userArg);
static int   unsubscribe(int domainId, char *subject, char *type, cMsgCallback *callback);
static int   get(int domainId, void *sendMsg, time_t timeout, void **replyMsg);
static int   disconnect(int domainId);

static domainFunctions functions = {coda_connect, coda_send, flush, subscribe, unsubscribe, get, disconnect};

/* CODA domain type */
domainTypeInfo codaDomainTypeInfo = {
  "coda",
  &functions
};

/* local variables */
static int oneTimeInitialized = 0;
static pthread_mutex_t connectMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t generalMutex = PTHREAD_MUTEX_INITIALIZER;
static cMsgDomain_CODA domains[MAXDOMAINS_CODA];



/* id which uniquely defines a subject/type pair */
static int subjectTypeId = 1;


/* for c++ */
#ifdef __cplusplus
extern "C" {
#endif


/* in cMsgServer.c */
void *cMsgServerListeningThread(void *arg);


/* local prototypes */
static int   getHostAndPortFromNameServer(cMsgDomain_CODA *domain, int serverfd);
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
                    unsigned short *port, char **remainder);
static void  domainInit(cMsgDomain_CODA *domain);
static void  domainFree(cMsgDomain_CODA *domain);  
static void  domainClear(cMsgDomain_CODA *domain);
static void *keepAliveThread(void *arg);



/*-------------------------------------------------------------------*/


static int coda_connect(char *myUDL, char *myName, char *myDescription, int *domainId) {

  int i, id=-1, err, serverfd, status;
  char *portEnvVariable=NULL, temp[CMSG_MAXHOSTNAMELEN];
  unsigned short startingPort;
  mainThreadInfo threadArg;
  
  /* First, grab mutex for thread safety. This mutex must be held until
   * the initialization is completely finished. Otherwise, if we set
   * initComplete = 1 (so that we reserve space in the domains array)
   * before it's finished and then release the mutex, we may give an
   * "existing" connection to a user who does a second init
   * when in fact, an error may still occur in that "existing"
   * connection. Hope you caught that.
   */
  connectMutexLock();
  

  /* do one time initialization */
  if (!oneTimeInitialized) {
    /* clear arrays */
    for (i=0; i<MAXDOMAINS_CODA; i++) domainInit(&domains[i]);
    oneTimeInitialized = 1;
  }

  
  /* find an existing connection to this domain if possible */
  for (i=0; i<MAXDOMAINS_CODA; i++) {
  }
  

  /* found the id of a valid connection - return that */
  if (id > -1) {
    *domainId = id + DOMAIN_ID_OFFSET;
    connectMutexUnlock();
    return(CMSG_OK);
  }
  

  /* no existing connection, so find the first available place in the "domains" array */
  for (i=0; i<MAXDOMAINS_CODA; i++) {
    if (domains[i].initComplete > 0) {
      continue;
    }
    domainInit(&domains[i]);
    id = i;
    break;
  }
  

  /* exceeds number of domain connections allowed */
  if (id < 0) {
    connectMutexUnlock();
    return(CMSG_LIMIT_EXCEEDED);
  }


  /* reserve this element of the "domains" array */
  domains[id].initComplete = 1;
      
  /* save ref to self */
  domains[id].id = id;
          
  /* store our host's name */
  gethostname(temp, CMSG_MAXHOSTNAMELEN);
  domains[id].myHost = (char *) strdup(temp);

  /* store names, can be changed until server connection established */
  domains[id].name        = (char *) strdup(myName);
  domains[id].udl         = (char *) strdup(myUDL);
  domains[id].description = (char *) strdup(myDescription);
  
  /* parse the UDL - Uniform Domain Locator */
  if ( (err = parseUDL(myUDL, NULL, &domains[id].serverHost,
                       &domains[id].serverPort, NULL)) != CMSG_OK ) {
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
                                     &domains[id].listenPort,
                                     &domains[id].listenSocket)) != CMSG_OK) {
    domainClear(&domains[id]);
    connectMutexUnlock();
    return(err);
  }

  /* launch pend thread and start listening on receive socket */
  threadArg.domain   = &domains[id];
  threadArg.listenFd = domains[id].listenSocket;
  threadArg.blocking = CMSG_NONBLOCKING;
  status = pthread_create(&domains[id].pendThread, NULL,
                          cMsgServerListeningThread, (void *)&threadArg);
  if (status != 0) {
    err_abort(status, "Creating message listening thread");
  }
     
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_connect: created listening thread\n");
  }
  
  /*---------------------------------------------------------------*/
  /* connect & talk to cMsg name server to check if name is unique */
  /*---------------------------------------------------------------*/
    
  /* first connect to server host & port */
  if ( (err = cMsgTcpConnect(domains[id].serverHost,
                             domains[id].serverPort,
                             &serverfd)) != CMSG_OK) {
    /* stop listening & connection threads */
    pthread_cancel(domains[id].pendThread);
    domainClear(&domains[id]);
    connectMutexUnlock();
    return(err);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_connect: connected to name server\n");
  }
  
  /* get host & port to send messages to */
  err = getHostAndPortFromNameServer(&domains[id], serverfd);
  if (err != CMSG_OK) {
    close(serverfd);
    pthread_cancel(domains[id].pendThread);
    domainClear(&domains[id]);
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
                             domains[id].sendHost,
                             domains[id].sendPort);
  }
  
  /* create sending socket and store */
  if ( (err = cMsgTcpConnect(domains[id].sendHost,
                             domains[id].sendPort,
                             &domains[id].sendSocket)) != CMSG_OK) {
    close(serverfd);
    pthread_cancel(domains[id].pendThread);
    domainClear(&domains[id]);
    connectMutexUnlock();
    return(err);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_connect: created sending socket\n");
  }
  
  /* init is complete */
  *domainId = id + DOMAIN_ID_OFFSET;
  domains[id].initComplete = 1;

  /* create keep alive socket and store */
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_connect: create keepalive socket\n");
  }

  if ( (err = cMsgTcpConnect(domains[id].sendHost,
                             domains[id].sendPort,
                             &domains[id].keepAliveSocket)) != CMSG_OK) {
    close(domains[id].sendSocket);
    pthread_cancel(domains[id].pendThread);
    domainClear(&domains[id]);
    connectMutexUnlock();
    return(err);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_connect: keepalive socket fd = %d\n",domains[id].keepAliveSocket );
  }
  
  /* create thread to send periodic keep alives and handle dead server */
  status = pthread_create(&domains[id].keepAliveThread, NULL,
                          keepAliveThread, (void *)&domains[id]);
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
  int outGoing[4];
  char *subject, *type, *text;
  cMsgMessage *msg = (cMsgMessage *) vmsg;
  cMsgDomain_CODA *domain = &domains[domainId];
  int fd = domain->sendSocket;
  
  if (domains[domainId].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[domainId].lostConnection == 1) return(CMSG_LOST_CONNECTION);

  subject = cMsgGetSubject(vmsg);
  type    = cMsgGetType(vmsg);
  text    = cMsgGetText(vmsg);

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
  if (cMsgTcpRead(fd, (void *) &err, sizeof(err)) != sizeof(err)) {
    sendMutexUnlock(domain);
    subscribeMutexUnlock(domain);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "coda_send: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* done protecting communications */
  sendMutexUnlock(domain);

  /* return domain server's reply */
  err = ntohl(err);
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "coda_send: read reply (%d), am done\n", err);
  }
  
  return(err);
}


/*-------------------------------------------------------------------*/


static int get(int domainId, void *sendMsg, time_t timeout, void **replyMsg) {
  return (CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/

static int flush(int domainId) {

  FILE *file;  
  cMsgDomain_CODA *domain = &domains[domainId];
  int fd = domain->sendSocket;

  if (domains[domainId].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[domainId].lostConnection == 1) return(CMSG_LOST_CONNECTION);

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

  int i, j, iok, jok, uniqueId;
  cMsgDomain_CODA *domain = &domains[domainId];

  if (domains[domainId].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[domainId].lostConnection == 1) return(CMSG_LOST_CONNECTION);

  
  /* make sure subscribe and unsubscribe are not run at the same time */
  subscribeMutexLock(domain);
  
  /* add to callback list if subscribe to same subject/type exists */
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
    if (domain->subscribeInfo[i].active == 0) {

      int err, lenSubject, lenType;
      int fd = domain->sendSocket;
      int outGoing[4];

      domain->subscribeInfo[i].active  = 1;
      domain->subscribeInfo[i].subject = (char *) strdup(subject);
      domain->subscribeInfo[i].type    = (char *) strdup(type);
      domain->subscribeInfo[i].cbInfo[0].callback = callback;
      domain->subscribeInfo[i].cbInfo[0].userArg  = userArg;
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
        fprintf(stderr, "coda_subscribe: write 4 (%d, %d, %d, %d) ints to server\n",
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
        fprintf(stderr, "coda_subscribe: sending subject (%s)\n", subject);
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
        fprintf(stderr, "coda_subscribe: sending type (%s)\n", type);
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
        fprintf(stderr, "coda_subscribe: will read reply\n");
      }
  
      /* now read reply */
      if (cMsgTcpRead(fd, (void *) &err, sizeof(err)) != sizeof(err)) {
        sendMutexUnlock(domain);
        subscribeMutexUnlock(domain);
        if (cMsgDebug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "subscribe: read failure\n");
        }
        return(CMSG_NETWORK_ERROR);
      }
      
      /* done protecting communications */
      sendMutexUnlock(domain);
      /* done protecting subscribe */
      subscribeMutexUnlock(domain);
        
      /* return domain server's reply */
      err = ntohl(err);
      if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "coda_subscribe: read reply (%d)\n", err);
      }
  
      return(err);
    }
  }
  
  /* done protecting subscribe */
  subscribeMutexUnlock(domain);
  
  /* iok == 0 here */
  return(CMSG_OUT_OF_MEMORY);
}


/*-------------------------------------------------------------------*/


static int unsubscribe(int domainId, char *subject, char *type, cMsgCallback *callback) {

  int i, j, cbCount, cbsRemoved;
  cMsgDomain_CODA *domain = &domains[domainId];
  
  /* make sure subscribe and unsubscribe are not run at the same time */
  subscribeMutexLock(domain);
  
  /* search entry list */
  for (i=0; i<MAXSUBSCRIBE; i++) {
    if ( (domain->subscribeInfo[i].active == 1) && 
         (strcmp(domain->subscribeInfo[i].subject, subject) == 0)  && 
         (strcmp(domain->subscribeInfo[i].type,    type)    == 0) )  {

      /* search callback list */
      cbCount = cbsRemoved = 0;
      for (j=0; j<MAXCALLBACK; j++) {
	if (domain->subscribeInfo[i].cbInfo[j].callback != NULL) {
	  cbCount++;
	  if (domain->subscribeInfo[i].cbInfo[j].callback == callback) {
            domain->subscribeInfo[i].cbInfo[j].callback == NULL;
            cbsRemoved++;
          }
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

        /* send subject */
        if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
          sendMutexUnlock(domain);
          subscribeMutexUnlock(domain);
          if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "unsubscribe: write failure\n");
          }
          return(CMSG_NETWORK_ERROR);
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

        /* now read reply */
        if (cMsgTcpRead(fd, (void *) &err, sizeof(err)) != sizeof(err)) {
          sendMutexUnlock(domain);
          subscribeMutexUnlock(domain);
          if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "unsubscribe: read failure\n");
          }
          return(CMSG_NETWORK_ERROR);
        }
        
        /* done protecting communications */
        sendMutexUnlock(domain);
        /* done protecting unsubscribe */
        subscribeMutexUnlock(domain);
        
        /* return domain server's reply */
        err = ntohl(err);
        return(err);

      }
      break;
      
    }
  }

  /* done protecting unsubscribe */
  subscribeMutexUnlock(domain);
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int disconnect(int domainId) {
  
  int status;
  cMsgDomain_CODA *domain = &domains[domainId];

  if (domains[domainId].initComplete != 1) return(CMSG_NOT_INITIALIZED);
  
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

  /* reset vars, free memory */
  domainClear(domain);
  
  connectMutexUnlock();

  return(CMSG_OK);
}



/*-------------------------------------------------------------------*/


static int getHostAndPortFromNameServer(cMsgDomain_CODA *domain, int serverfd) {

  int err, lengthHost, lengthName, lengthType, outgoing[5], incoming[2];
  char temp[CMSG_MAXHOSTNAMELEN];
  char *type = "coda";

  /* first send message id (in network byte order) to server */
  outgoing[0] = htonl(CMSG_SERVER_CONNECT);
  /* send my listening port (as an int) to server */
  outgoing[1] = htonl((int)domain->listenPort);
  /* send length of the type of domain server I'm expecting to connect to.*/
  lengthType  = strlen(type);
  outgoing[2] = htonl(lengthType);
  /* send length of my host name to server */
  lengthHost  = strlen(domain->myHost);
  outgoing[3] = htonl(lengthHost);
  /* send length of my name to server */
  lengthName  = strlen(domain->name);
  outgoing[4] = htonl(lengthName);
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: write 4 (%d, %d, %d, %d) ints to server\n",
            CMSG_SERVER_CONNECT, (int) domain->listenPort, lengthHost, lengthName);
  }
  
  /* first send all the ints */
  if (cMsgTcpWrite(serverfd, (void *) outgoing, sizeof(outgoing)) != sizeof(outgoing)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: send my domain type (%s) to server\n",
            type);
  }
  
  /* send the type of domain server I'm expecting to connect to */
  if (cMsgTcpWrite(serverfd, (void *) type, lengthType) != lengthType) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: send my host name (%s) to server\n",
            domain->myHost);
  }
  
  /* send my host name to server */
  if (cMsgTcpWrite(serverfd, (void *) domain->myHost, lengthHost) != lengthHost) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: send my name (%s) to server\n",
            domain->name);
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
  
  /* if everything's OK, we expect to get send host & port */
  
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


/*-------------------------------------------------------------------*/


int cMsgRunCallbacks(cMsgDomain_CODA *domain, int command, cMsgMessage *msg) {

  int i, j, status;
  dispatchCbInfo *dcbi;
  pthread_t newThread;
  
  /* callbacks have been stopped */
  if (domain->receiveState == 0) {
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
            /* allocate memory for thread arg so it doesn't go out-of-scope */
            dcbi = (dispatchCbInfo*) malloc(sizeof(dispatchCbInfo));
            if (dcbi == NULL) {
              if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
                fprintf(stderr, "runCallbacks: cannot allocate memory\n");
              }
              exit(1);
            }
            
	    dcbi->callback = domain->subscribeInfo[i].cbInfo[j].callback;
	    dcbi->userArg  = domain->subscribeInfo[i].cbInfo[j].userArg;
            /* the message was malloced in cMsgClientThread,
             * so it must be freed in the dispatch thread
             */
	    dcbi->msg = cMsgCopyMessage(msg);
            
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


int cMsgReadMessage(int fd, cMsgMessage *msg) {
  
  int err, lengths[5], inComing[9];
  int memSize = CMSG_MESSAGE_SIZE;
  char *string, storage[CMSG_MESSAGE_SIZE + 1];
  
  /* Start out with an array of size CMSG_MESSAGE_SIZE + 1
   * for storing strings, If that's too small, allocate more. */
  string = storage;
  
  /* read ints first */
  if (cMsgTcpRead(fd, (void *) inComing, sizeof(inComing)) != sizeof(inComing)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
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
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "readMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[0]) != lengths[0]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
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
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "readMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[1]) != lengths[1]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
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
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "readMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[2]) != lengths[2]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
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
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "readMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[3]) != lengths[3]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
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
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "readMessage: cannot allocate memory\n");
      }
      exit(1);
    }
  }
  if (cMsgTcpRead(fd, string, lengths[4]) != lengths[4]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
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
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
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

  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cMsgClientThread: msg arrived: %s\n", string);
  }
 
  return(CMSG_OK);
}



/*-------------------------------------------------------------------*/
/*   miscellaneous local functions                                   */
/*-------------------------------------------------------------------*/


static int parseUDL(const char *UDL, char **domainType, char **host,
                    unsigned short *port, char **remainder) {

/* note:  CODA domain UDL is of the form:   domainType://host:port/remainder */

  int i;
  char *p, *portString, *udl;

  if (UDL  == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  /* strtok modifies the string it tokenizes, so make a copy */
  udl = (char *) strdup(UDL);
  
/*printf("UDL = %s\n", udl);*/
  /* get tokens separated by ":" or "/" */
  if ( (p = (char *) strtok(udl, ":/")) == NULL) {
    free(udl);
    return (CMSG_BAD_FORMAT);
  }
  if (domainType != NULL) *domainType = (char *) strdup(p);
/*printf("domainType = %s\n", *domainType);*/
  
  if ( (p = (char *) strtok(NULL, ":/")) == NULL) {
   if (domainType != NULL)  free(*domainType);
    free(udl);
    return (CMSG_BAD_FORMAT);
  }
  if (host != NULL) *host =(char *)  strdup(p);
/*printf("host = %s\n", *host);*/
  
  if ( (p = (char *) strtok(NULL, ":/")) == NULL) {
    if (host != NULL) free(*host);
    if (domainType != NULL) free(*domainType);
    free(udl);
    return (CMSG_BAD_FORMAT);
  }
  if (port != NULL) {
    portString = (char *) strdup(p);
    *port = atoi(portString);
  }
/*printf("port string = %s, port int = %hu\n", portString, *port);*/  
  if (*port < 1024 || *port > 65535) {
    if (port != NULL) free((void *) portString);
    if (host != NULL) free((void *) *host);
    if (domainType != NULL) free((void *) *domainType);
    free(udl);
    return (CMSG_OUT_OF_RANGE);
  }
  
  if ( (p = (char *) strtok(NULL, ":/")) != NULL  && remainder != NULL) {
    *remainder = (char *) strdup(p);
/*printf("remainder = %s\n", *remainder);*/  
  }
  
  /* UDL parsed ok */
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
      domain->subscribeInfo[i].cbInfo[j].callback = NULL;
      domain->subscribeInfo[i].cbInfo[j].userArg  = NULL;
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


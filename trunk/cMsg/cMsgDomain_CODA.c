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


/* built-in limits */
#define MAXSUBSCRIBE 100
#define MAXCALLBACK   10


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


/* CODA domain type */
domainTypeInfo codaDomainTypeInfo = {
  "coda",
  {connect,send,flush,subscribe,unsubscribe,get,disconnect}
}


/* id which uniquely defines a subject/type pair */
static int subjectTypeId = 1;


/* for c++ */
#ifdef __cplusplus
extern "C" {
#endif


/* local prototypes */
static int   getHostAndPortFromNameServer(cMsgDomain *domain, int serverfd);
static int   connect(cMsgDomain *domain, char *myDomain, char *myName, char *myDescription);
static int   send(cMsgDomain *domain, char *subject, char *type, char *text);
static int   flush(cMsgDomain *domain);
static int   subscribe(cMsgDomain *domain, char *subject, char *type, cMsgCallback *callback, void *userArg);
static int   unsubscribe(cMsgDomain *domain, char *subject, char *type, cMsgCallback *callback);
static int   get(cMsgMessage *sendMsg, cMsgMessage **replyMsg);
static int   disconnect(cMsgDomain *domain);
static void *dispatchCallback(void *param);
static void  connectMutexLock(void);
static void  connectMutexUnlock(void);
static int   sendMutexLock(cMsgDomain *domain);
static int   sendMutexUnlock(cMsgDomain *domain);
static int   subscribeMutexLock(cMsgDomain *domain);
static int   subscribeMutexUnlock(cMsgDomain *domain);
static int   readMessage(int fd, cMsg *msg);
static int   runCallbacks(cMsgDomain *domain, int command, cMsg *msg);
static void *serverListeningThread(void *arg);



/*-------------------------------------------------------------------*/


static int connect(cMsgDomain *domain, char *myDomain, char *myName, char *myDescription) {

  int i, id=-1, err, serverfd, status;
  char *portEnvVariable=NULL, temp[CMSG_MAXHOSTNAMELEN];
  unsigned short startingPort;
  mainThreadInfo threadArg;
  
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
    if (debug >= CMSG_DEBUG_WARN) {
      fprintf(stderr, "cMsgConnect: cannot find CMSG_PORT env variable, first try port %hu\n", startingPort);
    }
  }
  else {
    i = atoi(portEnvVariable);
    if (i < 1025 || i > 65535) {
      startingPort = CMSG_CLIENT_LISTENING_PORT;
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
                                     domain.listenPort,
                                     domain.listenSocket)) != CMSG_OK) {
    cMsgDomainClear(domain);
    connectMutexUnlock();
    return(err);
  }

  /* launch pend thread and start listening on receive socket */
  threadArg.listenFd = domain->listenSocket;
  threadArg.blocking = CMSG_NONBLOCKING;
  status = pthread_create(domain.pendThread, NULL,
                          serverListeningThread, (void *)&threadArg);
  if (status != 0) {
    err_abort(status, "Creating message listening thread");
  }
     
  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cMsgConnect: created listening thread\n");
  }
  
  /*---------------------------------------------------------------*/
  /* connect & talk to cMsg name server to check if name is unique */
  /*---------------------------------------------------------------*/
    
  /* first connect to server host & port */
  if ( (err = cMsgTcpConnect(domain->serverHost,
                             domain->serverPort,
                             &serverfd)) != CMSG_OK) {
    /* stop listening & connection threads */
    pthread_cancel(domain->pendThread);
    cMsgDomainClear(domain);
    connectMutexUnlock();
    return(err);
  }
  
  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cMsgConnect: connected to name server\n");
  }
  
  /* get host & port to send messages to */
  err = getHostAndPortFromNameServer(domain, serverfd);
  if (err != CMSG_OK) {
    close(serverfd);
    pthread_cancel(domain->pendThread);
    cMsgDomainClear(domain);
    connectMutexUnlock();
    return(err);
  }
  
  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cMsgConnect: got host and port from name server\n");
  }
  
  /* done talking to server */
  close(serverfd);
  
  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cMsgConnect: closed name server socket\n");
    fprintf(stderr, "cMsgConnect: sendHost = %s, sendPort = %hu\n",
                             domain->sendHost,
                             domain->sendPort);
  }
  
  /* create sending socket and store */
  if ( (err = cMsgTcpConnect(domain->sendHost,
                             domain->sendPort,
                             domain.sendSocket)) != CMSG_OK) {
    close(serverfd);
    pthread_cancel(domain->pendThread);
    cMsgDomainClear(domain);
    connectMutexUnlock();
    return(err);
  }

  
  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cMsgConnect: created sending socket\n");
  }
  
  /* init is complete */
  domain->initComplete = 1;

  /* no more mutex protection is necessary */
  connectMutexUnlock();
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int send(cMsgDomain *domain, char *subject, char *type, char *text) {
  
  int fd = domain->sendSocket;
  int err, lenSubject, lenType, lenText;
  int outGoing[4];
  

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

  /* send ints over together */
  if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
    sendMutexUnlock(domain);
    subscribeMutexUnlock(domain);
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send subject */
  if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
    sendMutexUnlock(domain);
    subscribeMutexUnlock(domain);
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send type */
  if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
    sendMutexUnlock(domain);
    subscribeMutexUnlock(domain);
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* send text */
  if (cMsgTcpWrite(fd, (void *) text, lenText) != lenText) {
    sendMutexUnlock(domain);
    subscribeMutexUnlock(domain);
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgSend: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* now read reply */
  if (cMsgTcpRead(fd, (void *) &err, sizeof(err)) != sizeof(err)) {
    sendMutexUnlock(domain);
    subscribeMutexUnlock(domain);
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgSend: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* done protecting communications */
  sendMutexUnlock(domain);

  /* return domain server's reply */
  err = ntohl(err);
  return(err);
}


/*-------------------------------------------------------------------*/


static int flush(cMsgDomain *domain) {

  int fd = domain->sendSocket;
  FILE *file;  


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


static int subscribe(cMsgDomain *domain, char *subject, char *type, cMsgCallback *callback, void *userArg) {

  int i, j, iok, jok, uniqueId;


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
      domain->subscribeInfo[i].cbInfo[j].callback = callback;
      domain->subscribeInfo[i].cbInfo[j].userArg  = userArg;
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
      sendMutexLock(domain);

      /* send ints over together */
      if (cMsgTcpWrite(fd, (void *) outGoing, sizeof(outGoing)) != sizeof(outGoing)) {
        sendMutexUnlock(domain);
        subscribeMutexUnlock(domain);
        if (debug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cMsgSubscribe: write failure\n");
        }
        return(CMSG_NETWORK_ERROR);
      }

      /* send subject */
      if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
        sendMutexUnlock(domain);
        subscribeMutexUnlock(domain);
        if (debug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cMsgSubscribe: write failure\n");
        }
        return(CMSG_NETWORK_ERROR);
      }

      /* send type */
      if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
        sendMutexUnlock(domain);
        subscribeMutexUnlock(domain);
        if (debug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cMsgSubscribe: write failure\n");
        }
        return(CMSG_NETWORK_ERROR);
      }
      
      /* now read reply */
      if (cMsgTcpRead(fd, (void *) &err, sizeof(err)) != sizeof(err)) {
        sendMutexUnlock(domain);
        subscribeMutexUnlock(domain);
        if (debug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cMsgSubscribe: read failure\n");
        }
        return(CMSG_NETWORK_ERROR);
      }
      
      /* done protecting communications */
      sendMutexUnlock(domain);
      /* done protecting subscribe */
      subscribeMutexUnlock(domain);
        
      /* return domain server's reply */
      err = ntohl(err);
      return(err);
    }
  }
  
  /* done protecting subscribe */
  subscribeMutexUnlock(domain);
  
  /* iok == 0 here */
  return(CMSG_OUT_OF_MEMORY);
}


/*-------------------------------------------------------------------*/


static int unsubscribe(cMsgDomain *domain, char *subject, char *type, cMsgCallback *callback) {

  int i, j, cbCount, cbsRemoved;


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
          if (debug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "cMsgSubscribe: write failure\n");
          }
          return(CMSG_NETWORK_ERROR);
        }

        /* send subject */
        if (cMsgTcpWrite(fd, (void *) subject, lenSubject) != lenSubject) {
          sendMutexUnlock(domain);
          subscribeMutexUnlock(domain);
          if (debug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "cMsgSubscribe: write failure\n");
          }
          return(CMSG_NETWORK_ERROR);
        }

        /* send type */
        if (cMsgTcpWrite(fd, (void *) type, lenType) != lenType) {
          sendMutexUnlock(domain);
          subscribeMutexUnlock(domain);
          if (debug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "cMsgSubscribe: write failure\n");
          }
          return(CMSG_NETWORK_ERROR);
        }

        /* now read reply */
        if (cMsgTcpRead(fd, (void *) &err, sizeof(err)) != sizeof(err)) {
          sendMutexUnlock(domain);
          subscribeMutexUnlock(domain);
          if (debug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "cMsgSubscribe: read failure\n");
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


static int disconnect(cMsgDomain *domain) {
  
  int status;


  /* When changing initComplete / connection status, mutex protect it */
  connectMutexLock();
  
  /* close sending and listening sockets */
  close(domain->sendSocket);
  close(domain->listenSocket);

  /* stop listening and client communication threads */
  status = pthread_cancel(domain->pendThread);
  if (status != 0) {
    err_abort(status, "Cancelling message listening & client threads");
  }

  /* reset vars, free memory */
  cMsgDomainClear(domain);
  
  connectMutexUnlock();

  return(CMSG_OK);
}



/*-------------------------------------------------------------------*/


static int getHostAndPortFromNameServer(cMsgDomain *domain, int serverfd) {

  int err, lengthHost, lengthName, lengthType, outgoing[5], incoming[2];
  char temp[CMSG_MAXHOSTNAMELEN];

  /* first send message id (in network byte order) to server */
  outgoing[0] = htonl(CMSG_SERVER_CONNECT);
  /* send my listening port (as an int) to server */
  outgoing[1] = htonl((int)domain->listenPort);
  /* send length of the type of domain server I'm expecting to connect to.*/
  lengthType  = strlen(domain->type);
  outgoing[2] = htonl(lengthType);
  /* send length of my host name to server */
  lengthHost  = strlen(domain->myHost);
  outgoing[3] = htonl(lengthHost);
  /* send length of my name to server */
  lengthName  = strlen(domain->name);
  outgoing[4] = htonl(lengthName);
  
  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: write 4 (%d, %d, %d, %d) ints to server\n",
            CMSG_SERVER_CONNECT, (int) domain->listenPort, lengthHost, lengthName);
  }
  
  /* first send all the ints */
  if (cMsgTcpWrite(serverfd, (void *) outgoing, sizeof(outgoing)) != sizeof(outgoing)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: send my domain type (%s) to server\n",
            domain->type);
  }
  
  /* send the type of domain server I'm expecting to connect to */
  if (cMsgTcpWrite(serverfd, (void *) domain->type, lengthType) != lengthType) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: send my host name (%s) to server\n",
            domain->myHost);
  }
  
  /* send my host name to server */
  if (cMsgTcpWrite(serverfd, (void *) domain->myHost, lengthHost) != lengthHost) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: send my name (%s) to server\n",
            domain->name);
  }
  
  /* send my name to server */
  if (cMsgTcpWrite(serverfd, (void *) domain->name, lengthName) != lengthName) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: write failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer:read error reply from server\n");
  }
  
  /* now read server reply */
  if (cMsgTcpRead(serverfd, (void *) &err, sizeof(err)) != sizeof(err)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  err = ntohl(err);
  
  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer:read err = %d\n", err);
  }
  
  /* if there's an error, quit */
  if (err != CMSG_OK) {
    return(err);
  }
  
  /* if everything's OK, we expect to get send host & port */
  
  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer:read port and length of host from server\n");
  }
  
  /* read port & length of host name to send to*/
  if (cMsgTcpRead(serverfd, (void *) &incoming, sizeof(incoming)) != sizeof(incoming)) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  domain->sendPort = (unsigned short) ntohl(incoming[0]);
  lengthHost = ntohl(incoming[1]);

  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer:port = %hu, host len = %d\n",
              domain->sendPort, lengthHost);
    fprintf(stderr, "getHostAndPortFromNameServer:read host from server\n");
  }
  
  /* read host name to send to */
  if (cMsgTcpRead(serverfd, (void *) temp, lengthHost) != lengthHost) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "getHostAndPortFromNameServer: read failure\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  /* be sure to null-terminate string */
  temp[lengthHost] = 0;
  domain->sendHost = (char *) strdup(temp);
  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "getHostAndPortFromNameServer: host = %s\n", domain->sendHost);
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int runCallbacks(cMsgDomain *domain, int command, cMsg *msg) {

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
              if (debug >= CMSG_DEBUG_SEVERE) {
                fprintf(stderr, "runCallbacks: cannot allocate memory\n");
              }
              exit(1);
            }
            
	    dcbi->callback = domain->subscribeInfo[i].cbInfo[j].callback;
	    dcbi->userArg  = domain->subscribeInfo[i].cbInfo[j].userArg;
            /* the message was malloced in cMsgClientThread,
             * so it must be freed in the dispatch thread
             */
	    dcbi->msg = copyMsg(msg);
            
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


static int readMessage(int fd, cMsg *msg) {
  
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


static int sendMutexLock(cMsgDomain *domain) {

  int status;
  
  status = pthread_mutex_lock(domain.sendMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static int sendMutexUnlock(cMsgDomain *domain) {

  int status;

  status = pthread_mutex_unlock(domain.sendMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


static int subscribeMutexLock(cMsgDomain *domain) {

  int status;
  
  status = pthread_mutex_lock(domain.subscribeMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static int subscribeMutexUnlock(cMsgDomain *domain) {

  int status;

  status = pthread_mutex_unlock(domain.subscribeMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


#ifdef __cplusplus
}
#endif


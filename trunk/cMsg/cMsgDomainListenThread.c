/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Author:  Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *      Routines for TCP server. Threads for establishing TCP
 *	communications with cMsg senders.
 *
 *----------------------------------------------------------------------------*/

#ifdef VXWORKS
#include <vxWorks.h>
#include <taskLib.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <pthread.h>

#include "errors.h"
#include "cMsgNetwork.h"
#include "cMsg.h"
#include "cMsgPrivate.h"
#include "cMsgDomain.h"


extern cMsgDomain_CODA cMsgDomains[];

/*-------------------------------------------------------------------*
 * Structure to keep track of each client thread so it can be
 * disposed of later when the user is done with cMsg.
 *-------------------------------------------------------------------*/
typedef struct cMsgClientThreadInfo_t {
  int       isUsed;     /* is this item being used - is threadId valid - (1) or not (0) ? */
  pthread_t threadId;   /* pthread id of client thread */
} cMsgClientThreadInfo;


/*-------------------------------------------------------------------*
 * Structure to pass relevant info to each thread serving
 * a cMsg connection.
 *-------------------------------------------------------------------*/
typedef struct cMsgThreadInfo_t {
  int connfd;   /* socket connection's fd */
  int domainId; /* index into the "cMsgDomains" array */
  cMsgClientThreadInfo *clientInfo; /* pointer to client thread info of this thread */
} cMsgThreadInfo;


/* set debug level here */
/* static int cMsgDebug = CMSG_DEBUG_INFO; */

/* max number of clients to track */
#define CMSG_CLIENTSMAX 1000


static int counter = 1;


/* for c++ */
#ifdef __cplusplus
extern "C" {
#endif

/* prototypes */
static void *clientThread(void *arg);
static void  cleanUpHandler(void *arg);

/*-------------------------------------------------------------------*
 * The listening thread needs a pthread cancellation cleanup handler.
 * It will be called when the cMsgClientListeningThread is canceled.
 * It's task is to remove all the client threads.
 *-------------------------------------------------------------------*/
static void cleanUpHandler(void *arg) {
  int i, status;
  cMsgClientThreadInfo *clientThreads = (cMsgClientThreadInfo *) arg;
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cMsgClientListeningThread: in cleanup handler\n");
  }

  /* for each element in the array ... */
  for (i=0; i<CMSG_CLIENTSMAX; i++) {
    /* if there's a client thread running ... */
    if (clientThreads[i].isUsed == 1) {
      /* cancel thread */
      if ( (status = pthread_cancel(clientThreads[i].threadId)) != 0) {
        err_abort(status, "Cancelling client thread");
      }
    }
  }
}


/*-------------------------------------------------------------------*
 * cMsgClientListeningThread is a listening thread used by a cMsg client
 * to make connections to other cMsg-enabled programs.
 *
 * In order to be able to kill this thread and its children threads
 * on demand, it must not block.
 * This thread may use a non-blocking socket and waits on a "select"
 * statement instead of the blocking "accept" statement.
 *-------------------------------------------------------------------*/
void *cMsgClientListeningThread(void *arg)
{
  mainThreadInfo *threadArg = (mainThreadInfo *) arg;
  int             listenFd  = threadArg->listenFd;
  int             blocking  = threadArg->blocking;
  int             i, err, endian, iov_max, index, status;
  fd_set          readSet;
  struct timeval  timeout;
  struct sockaddr_in cliaddr;
  socklen_t	  addrlen, len;
  pthread_t       threadId;
  pthread_attr_t  attr;
  /* pointer to information to be passed to threads */
  cMsgThreadInfo   *pinfo;
  /* keep info about client threads here */
  cMsgClientThreadInfo clientThreads[CMSG_CLIENTSMAX];
  
  int domainId;
  
  addrlen  = sizeof(cliaddr);
  domainId = threadArg->domainId;
    
  /* increase concurrency for this thread for early Solaris */
#ifdef sun
  int  con;
  con = thr_getconcurrency();
  thr_setconcurrency(con + 1);
#endif

  /* client thread info needs to be initialized */
  for (i=0; i<CMSG_CLIENTSMAX; i++) {
    clientThreads[i].isUsed = 0;
  }

  /* get thread attribute ready */
  if ( (status = pthread_attr_init(&attr)) != 0) {
    err_abort(status, "Init thread attribute");
  }
  if ( (status = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) != 0) {
    err_abort(status, "Set thread state detached");
  }
  
  /* install cleanup handler for this thread's cancellation */
  pthread_cleanup_push(cleanUpHandler, (void *) clientThreads);
  
  /* Tell spawning thread that we're up and running */
  threadArg->isRunning = 1;
  
  /* spawn threads to deal with each client */
  for ( ; ; ) {
    
    /* if we want things not to block, then use select */
    if (blocking == CMSG_NONBLOCKING) {
      /* Linux modifies timeout, so reset each round */
      /* 3 second timed wait on select */
      timeout.tv_sec  = 3;
      timeout.tv_usec = 0;

      FD_ZERO(&readSet);
      FD_SET(listenFd, &readSet);

      /* test to see if someone wants to shutdown this thread */
      pthread_testcancel();

      /* wait on select, this times out so it won't block */
      err = select(listenFd+1, &readSet, NULL, NULL, &timeout);
      
      /* test to see if someone wants to shutdown this thread */
      pthread_testcancel();

      /* we timed out, try again */
      if (err == 0) {
        continue;
      }
      /* got a connection */
      else if (FD_ISSET(listenFd, &readSet)) {
      }
      /* if there's an error, quit */
      else if (err < 0) {
        if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
          fprintf(stderr, "cMsgClientListeningThread: select call error: %s\n", strerror(errno));
        }
        break;
      }
      /* try again */
      else {
        continue;
      }
    }
    
    /* get things ready for accept call */
    len = addrlen;

    /* allocate argument to pass to thread */
    pinfo = (cMsgThreadInfo *) malloc(sizeof(cMsgThreadInfo));
    if (pinfo == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "cMsgClientListeningThread: cannot allocate memory\n");
      }
      exit(1);
    }
    
    /* set values to pass on to thread */
    pinfo->domainId   = domainId;
    pinfo->clientInfo = NULL;
    index = -1;
    for (i=0; i<CMSG_CLIENTSMAX; i++) {
      if (clientThreads[i].isUsed == 0) {
        pinfo->clientInfo = &clientThreads[i];
        index = i;
        break;
      }
    }

    /* wait for connection to client */
    pinfo->connfd = err = cMsgAccept(listenFd, (SA *) &cliaddr, &len);
    /* ignore errors due to client shutting down the connection before
     * it can be established on this end. (EWOULDBLOCK, ECONNABORTED,
     * EPROTO) 
     */
    if (err < 0) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cMsgClientListeningThread: error accepting client connection\n");
      }
      free(pinfo);
      continue;
    }

    /* create thread to deal with client */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cMsgClientListeningThread: accepting client connection\n");
    }
    
    /* if there's room to store thread info ... */
    if (index > -1) {
      clientThreads[index].isUsed = 1;
      status = pthread_create(&clientThreads[index].threadId, &attr, clientThread, (void *) pinfo);
      if (status != 0) {
        err_abort(status, "Create client thread");
      }
    }
    /* else if no room, create thread anyway */
    else {
      status = pthread_create(&threadId, &attr, clientThread, (void *) pinfo);
      if (status != 0) {
        err_abort(status, "Create client thread");
      }
    }
  }
  
  /* don't forget to free mem allocated for the argument passed to this routine */
  free(threadArg);
  
  /* on some operating systems (Linux) this call is necessary - calls cleanup handler */
  pthread_cleanup_pop(1);
  
  pthread_exit(NULL);
}


/*-------------------------------------------------------------------*/


static void *clientThread(void *arg)
{
  int  msgId, err, connfd, domainId, length=0;
  int  outgoing[2], incoming[2], localCount;
  char host[CMSG_MAXHOSTNAMELEN+1];
  unsigned short port;
  struct timeval timeout;
  cMsgThreadInfo *info;
  cMsgClientThreadInfo *clientInfo;
#ifdef sun
  int  con;
#endif
  
  info       = (cMsgThreadInfo *) arg;
  domainId   = info->domainId;
  connfd     = info->connfd;
  clientInfo = info->clientInfo;
  free(arg);

#ifdef sun
  /* increase concurrency for this thread */
  con = thr_getconcurrency();
  thr_setconcurrency(con + 1);
#endif

 /*--------------------------------------*/
  /* wait for and process client requests */
  /*--------------------------------------*/
  
  /* set socket timeout (10 sec) for reading commands */
  timeout.tv_sec  = 6;
  timeout.tv_usec = 0;
  
  err = setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const void *) &timeout, sizeof(timeout));
  if (err < 0) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "clientThread %d: setsockopt error\n", localCount);
    }
    return;
  }
  
  localCount = counter++;
 
  /* the command loop */
  while (1) {

    /* first, read the incoming message id */
    retry:
    err = cMsgTcpRead(connfd, &msgId, sizeof(msgId));
    if (err != sizeof(msgId)) {
      /* if there's a timeout, try again */
      if (errno == EWOULDBLOCK) {
        goto retry;
      }
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "clientThread %d: error reading command\n", localCount);
        perror("reading command");
      }
      goto end;
    }
    msgId = ntohl(msgId);

    switch (msgId) {

      case CMSG_SUBSCRIBE_RESPONSE:
      {
          cMsgMessage *message;
          message = (cMsgMessage *) cMsgCreateMessage();
          if (message == NULL) {
            if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
              fprintf(stderr, "clientThread %d: cannot allocate memory\n", localCount);
            }
            exit(1);
          }

          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "clientThread %d: subscribe response received\n", localCount);
          }
          
          /* fill in known message fields */
          message->next         = NULL;
          message->domain       = (char *) strdup("cMsg");
          message->receiverTime = time(NULL);
          message->receiver     = (char *) strdup(cMsgDomains[domainId].name);
          message->receiverHost = (char *) strdup(cMsgDomains[domainId].myHost);
          
          /* read the message */
          if ( (err = cMsgReadMessage(connfd, message)) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread %d: error reading message\n", localCount);
            }
            free((void *) message->domain);
            free((void *) message->receiver);
            free((void *) message->receiverHost);
            goto end;
          }
          
          /* run callbacks for this message */
          if ( (err = cMsgRunCallbacks(domainId, message)) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread %d: too many messages cued up\n", localCount);
            }
            goto end;
          }
      }
      break;

      case CMSG_GET_RESPONSE:
      {
          cMsgMessage *message;
          message = (cMsgMessage *) cMsgCreateMessage();
          if (message == NULL) {
            if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
              fprintf(stderr, "clientThread %d: cannot allocate memory\n", localCount);
            }
            exit(1);
          }

          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "clientThread %d: subscribe response received\n", localCount);
          }
          
          /* fill in known message fields */
          message->next         = NULL;
          message->domain       = (char *) strdup("cMsg");
          message->receiverTime = time(NULL);
          message->receiver     = (char *) strdup(cMsgDomains[domainId].name);
          message->receiverHost = (char *) strdup(cMsgDomains[domainId].myHost);
          
          /* read the message */
          if ( (err = cMsgReadMessage(connfd, message)) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread %d: error reading message\n", localCount);
            }
            free((void *) message->domain);
            free((void *) message->receiver);
            free((void *) message->receiverHost);
            goto end;
          }
          
          /* run callbacks for this message */
          if ( (err = cMsgWakeGets(domainId, message)) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread %d: too many messages cued up\n", localCount);
            }
            goto end;
          }
      }
      break;

      case  CMSG_KEEP_ALIVE:
      {
        int alive;
        
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "clientThread %d: keep alive received\n", localCount);
        }
        
        /* read an int */
        if ((err = cMsgTcpRead(connfd, &alive, sizeof(alive))) != sizeof(alive)) {
          if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "clientThread %d: error reading command\n", localCount);
          }
          goto end;
        }
    
        /* respond with ok */
        alive = htonl(CMSG_OK);
        if (cMsgTcpWrite(connfd, (void *) &alive, sizeof(alive)) != sizeof(alive)) {
          if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "clientThread %d: write failure\n", localCount);
          }
          goto end;
        }        
      }
      break;

      case  CMSG_SHUTDOWN:
      {
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "clientThread %d: told to shutdown\n", localCount);
        }
        goto end;
      }
      break;
      
      default:
         if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "clientThread %d: given nonsense message (%d)\n", localCount, msgId);
        }
     
    }

  } /* while(1) - command loop */

  /* we only end up down here if there's an error or a shutdown */
  end:
    /* client has quit or crashed, therefore clean up */
    if (clientInfo != NULL) {
      clientInfo->isUsed = 0;
    }

    fprintf(stderr, "clientThread %d: remote client connection broken\n", localCount);

    /* we are done with the socket */
    close(connfd);

#ifdef sun
    /* decrease concurrency as this thread disappears */
    con = thr_getconcurrency();
    thr_setconcurrency(con - 1);
#endif
  
    /* quit thread */
    pthread_exit(NULL);
}

#ifdef __cplusplus
}
#endif


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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/time.h>
#include <sys/select.h>
#include <pthread.h>

#include "cMsgNetwork.h"
#include "cMsgPrivate.h"
#include "cMsg.h"
#include "cMsg_CODA.h"


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
  int endian;   /* endian of server */
  int iov_max;  /* max iov size */
  cMsgClientThreadInfo *clientInfo; /* pointer to client thread info of this thread */
} cMsgThreadInfo;


/* prototypes */
static void *clientThread(void *arg);
static void  cleanUpHandler(void *arg);

/* set debug level here */
/* static int cMsgDebug = CMSG_DEBUG_INFO; */

/* max number of clients to track */
#define CMSG_CLIENTSMAX 1000


static int domainId ;
static cMsgDomain_CODA *domain;


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
  mainThreadInfo *threadarg = (mainThreadInfo *) arg;
  int             listenFd  = threadarg->listenFd;
  int             blocking  = threadarg->blocking;
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
  
  
  addrlen  = sizeof(cliaddr);
  domain   = (cMsgDomain_CODA *) threadarg->domain;
  domainId = threadarg->domainId;

  
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

  /* find servers's endian value */
  if (cMsgByteOrder(&endian) == CMSG_ERROR) {
    if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
      fprintf(stderr, "cMsgClientListeningThread: strange byteorder\n");
    }
    exit(1);
  }

  /* find servers's iov_max value */
#ifndef __APPLE__

  if ( (iov_max = sysconf(_SC_IOV_MAX)) < 0) {
    /* set it to POSIX minimum by default (it always bombs on Linux) */
    iov_max = CMSG_IOV_MAX;
  }
  
#else

  iov_max = CMSG_IOV_MAX;
  
#endif

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
fprintf(stderr, "cMsgClientListeningThread: tell folks we're running\n");
  threadarg->isRunning = 1;
  
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
    pinfo->endian     = endian;
    pinfo->iov_max    = iov_max;
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
  
  /* on some operating systems (Linux) this call is necessary - calls cleanup handler */
  pthread_cleanup_pop(1);
  
  pthread_exit(NULL);
}


/*-------------------------------------------------------------------*/


static void *clientThread(void *arg)
{
  int  msgId, err, connfd, endian, length=0;
  int  outgoing[2], incoming[2];
  char host[CMSG_MAXHOSTNAMELEN+1];
  unsigned short port;
  struct timeval timeout;
  cMsgThreadInfo *info;
  cMsgClientThreadInfo *clientInfo;
#ifdef sun
  int  con;
#endif

  info       = (cMsgThreadInfo *) arg;
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
  timeout.tv_sec  = 10;
  timeout.tv_usec = 0;
  
  err = setsockopt(connfd, SOL_SOCKET, SO_RCVTIMEO, (const void *) &timeout, sizeof(timeout));
  if (err < 0) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "clientThread: setsockopt error\n");
    }
    return;
  }

  /* the command loop */
  while (1) {

    /* first, read the incoming message id */
    retry:
    if ((err = cMsgTcpRead(connfd, &msgId, sizeof(msgId))) != sizeof(msgId)) {
      /* if there's a timeout, try again */
      if (err == EWOULDBLOCK) {
        goto retry;
      }
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "clientThread: error reading command\n");
      }
      goto end;
    }
    msgId = ntohl(msgId);


    switch (msgId) {

      case CMSG_SUBSCRIBE_RESPONSE:
      {
          cMsgMessage *message;
          message = (cMsgMessage *) malloc(sizeof(cMsgMessage));
          
          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "clientThread: subscribe response received\n");
          }
          
          /* fill in known message fields */
          message->domain       = (char *) strdup(domain->udl);
          message->receiverTime = time(NULL);
          message->receiver     = (char *) strdup(domain->name);
          message->receiverHost = (char *) strdup(domain->myHost);
          
          /* read the message */
          if ( (err = cMsgReadMessage(connfd, message)) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "  clientThread: error reading message\n");
            }
            free((void *) message->domain);
            free((void *) message->receiver);
            free((void *) message->receiverHost);
            goto end;
          }
          
          /* run callbacks for this message */
          if (cMsgDebug >= CMSG_DEBUG_INFO) {
              fprintf(stderr, "  clientThread: will run callbacks, msgId = \n", msgId);
          }
          cMsgRunCallbacks(domain, msgId, message);
      }
      break;

      case  CMSG_KEEP_ALIVE:
      {
        int alive;
        
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "clientThread: keep alive received\n");
        }
        
        /* read an int */
        if ((err = cMsgTcpRead(connfd, &alive, sizeof(alive))) != sizeof(alive)) {
          if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "clientThread: error reading command\n");
          }
          goto end;
        }
    
        /* respond with ok */
        alive = htonl(CMSG_OK);
        if (cMsgTcpWrite(connfd, (void *) &alive, sizeof(alive)) != sizeof(alive)) {
          if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "clientThread: write failure\n");
          }
          goto end;
        }

        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "clientThread: sent keep alive ok to server\n");
        }
        
      }
      break;

      case  CMSG_SHUTDOWN:
      {
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "clientThread: told to shutdown\n");
        }
        goto end;
      }
      break;
    }

  } /* while(1) - command loop */

  /* we only end up down here if there's an error or a shutdown */
  end:
    /* client has quit or crashed, therefore clean up */
    if (clientInfo != NULL) {
      clientInfo->isUsed = 0;
    }

    fprintf(stderr, "clientThread: remote client connection broken\n");

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

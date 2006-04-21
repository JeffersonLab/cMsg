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
#include <sockLib.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "errors.h"
#include "cMsgNetwork.h"
#include "cMsgPrivate.h"
#include "cMsgBase.h"
#include "cMsgDomain.h"


static int counter = 1;

/* for c++ */
#ifdef __cplusplus
extern "C" {
#endif


/**
 * Array containing a structure of information about each client
 * connection to the cMsg domain.
 */
extern cMsgDomainInfo cMsgDomains[];


/* prototypes */
static void *clientThread(void *arg);
static void  cleanUpHandler(void *arg);

#ifdef VXWORKS
static char *strdup(const char *s1) {
    char *s;    
    if (s1 == NULL) return NULL;    
    if ((s = (char *) malloc(strlen(s1)+1)) == NULL) return NULL;    
    return strcpy(s, s1);
}
#endif


/*-------------------------------------------------------------------*
 * The listening thread needs a pthread cancellation cleanup handler.
 * It will be called when the cMsgClientListeningThread is canceled.
 * It's task is to remove all the client threads.
 *-------------------------------------------------------------------*/
static void cleanUpHandler(void *arg) {
  struct timespec sTime = {0,500000000};
  cMsgDomainInfo *domain = (cMsgDomainInfo *) arg;
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cMsgClientListeningThread: in cleanup handler\n");
  }

  /* cancel threads, ignore errors */
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cMsgClientListeningThread: cancelling mesage receiving threads\n");
  }
  
  pthread_cancel(domain->clientThread[1]);
  
  /* Nornally we could just cancel the thread and if the subscription
   * mutex were locked, the reinitialization would free it. However,
   * in vxWorks, reinitialization of a pthread mutex is not allowed
   * so we want to kill the thread in a way in which the mutex will
   * not end up locked.
   */  
  domain->killClientThread = 1;
  pthread_cond_signal(&domain->subscribeCond);
  nanosleep(&sTime,NULL);
  pthread_cancel(domain->clientThread[0]);
  domain->killClientThread = 0;

}


/*-------------------------------------------------------------------*
 * cMsgClientListeningThread is a listening thread used by a cMsg client
 * to allow 2 connections from the cMsg server. One connection is for
 * keepalive commands, and the other is for everything else.
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
  int             domainId  = threadArg->domainId;
  int             err, status, connectionNumber=0;
  int             state, type, index=0;
  const int       on=1;
  fd_set          readSet;
  struct timeval  timeout;
  struct sockaddr_in cliaddr;
  socklen_t	  addrlen, len;
  pthread_attr_t  attr;
  
  /* pointer to information to be passed to threads */
  cMsgThreadInfo *pinfo;
      
  /* increase concurrency for this thread for early Solaris */
  int  con;
  con = sun_getconcurrency();
  sun_setconcurrency(con + 1);

  /* release system resources when thread finishes */
  pthread_detach(pthread_self());

  addrlen = sizeof(cliaddr);
    
  /* get thread attribute ready */
  if ( (status = pthread_attr_init(&attr)) != 0) {
    err_abort(status, "Init thread attribute");
  }
  if ( (status = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) != 0) {
    err_abort(status, "Set thread state detached");
  }
  
  /* enable pthread cancellation at deferred points like pthread_testcancel */
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);
  pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &type);
  
  
  /* install cleanup handler for this thread's cancellation */
  pthread_cleanup_push(cleanUpHandler, (void *) (&cMsgDomains[domainId]));
  
  /* Tell spawning thread that we're up and running */
  threadArg->isRunning = 1;
  
  /* spawn threads to deal with each client */
  for ( ; ; ) {
    
    /*
     * If we want things not to block, then use select.
     * Note: select can be used to place a timeout on
     * connect only when the socket is nonblocking.
     * Socket timeout options do not work with connect.
     */
    if (blocking == CMSG_NONBLOCKING) {
      /* Linux modifies timeout, so reset each round */
      /* 1 second timed wait on select */
      timeout.tv_sec  = 1;
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
    pinfo->domainId         = domainId;
    pinfo->connectionNumber = connectionNumber;
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
    
    /* don't wait for messages to cue up, send any message immediately */
    err = setsockopt(pinfo->connfd, IPPROTO_TCP, TCP_NODELAY, (char*) &on, sizeof(on));
    if (err < 0) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "cMsgClientListeningThread: error setting socket to TCP_NODELAY\n");
      }
      close(pinfo->connfd);
      free(pinfo);
      continue;
    }
    
     /* create thread to deal with client */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cMsgClientListeningThread: accepting client connection\n");
    }
    
    /* Connections come 2 at a time. If failing over, may get multiple
     * connections here but always in pairs of 2.
     * The first connection is one to receive messages and the second
     * reponds to keepAlive inquiries from the server.
     */
    status = pthread_create(&cMsgDomains[domainId].clientThread[index], &attr, clientThread, (void *) pinfo);
    if (status != 0) {
      err_abort(status, "Create client thread");
    }
    
    connectionNumber++;
    index = connectionNumber%2;
  }
  
 
  /* on some operating systems (Linux) this call is necessary - calls cleanup handler */
  pthread_cleanup_pop(1);
  
  pthread_exit(NULL);
  return NULL;
}


/*-------------------------------------------------------------------*/


static void *clientThread(void *arg)
{
  int  inComing[2];
  int  err, ok, size, msgId, connfd, domainId, connectionNumber, localCount=0;
  cMsgThreadInfo *info;
  int  con, bufSize, index;
  char *buffer;
  int acknowledge = 0;

  
  info             = (cMsgThreadInfo *) arg;
  domainId         = info->domainId;
  connfd           = info->connfd;
  connectionNumber = info->connectionNumber;
  index            = connectionNumber%2;
  free(arg);

  /* increase concurrency for this thread */
  con = sun_getconcurrency();
  sun_setconcurrency(con + 1);

  /* release system resources when thread finishes */
  pthread_detach(pthread_self());

  localCount = counter++;
  
  buffer = (char *) malloc(65536);
  if (buffer == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "clientThread %d: cannot allocate memory\n", localCount);
      }
      exit(1);
  }
  bufSize = 65536;
  cMsgDomains[domainId].msgInBuffer[index] = buffer;

  /*--------------------------------------*/
  /* wait for and process client requests */
  /*--------------------------------------*/
   
  /* Command loop */
  while (1) {

    /*
     * First, read the incoming message size. This read is also a pthread
     * cancellation point, meaning, if the main thread is cancelled, its
     * cleanup handler will cancel this one as soon as this thread hits
     * the "read" function call.
     */
     
    retry:
    
    if (cMsgTcpRead(connfd, inComing, sizeof(inComing)) != sizeof(inComing)) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "clientThread %d: error reading command\n", localCount);
      }
      /* if there's a timeout, try again */
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        /* test to see if someone wants to shutdown this thread */
        pthread_testcancel();
        goto retry;
      }
      goto end;
    }
    
    size = ntohl(inComing[0]);
    
    /* make sure we have big enough buffer */
    if (size > bufSize) {
      /* free previously allocated memory */
      free((void *) buffer);

      /* allocate more memory to accomodate larger msg */
      buffer = (char *) malloc((size_t) (size + 1000));
      if (buffer == NULL) {
        if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
          fprintf(stderr, "clientThread %d: cannot allocate %d amount of memory\n",
                  localCount, size);
        }
        goto end;
      }
      bufSize = size + 1000;
      cMsgDomains[domainId].msgInBuffer[index] = buffer;
    }
        
    /* extract command */
    msgId = ntohl(inComing[1]);
    
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
/*
          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "clientThread %d: subscribe response received\n", localCount);
          }
*/          
          /* fill in known message fields */
          message->next         = NULL;
          message->domain       = (char *) strdup("cMsg");
          clock_gettime(CLOCK_REALTIME, &message->receiverTime);
          message->receiver     = (char *) strdup(cMsgDomains[domainId].name);
          message->receiverHost = (char *) strdup(cMsgDomains[domainId].myHost);
          
          /* read the message */
          if ( cMsgReadMessage(connfd, buffer, message, &acknowledge) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread %d: error reading message\n", localCount);
            }
            free((void *) message->domain);
            free((void *) message->receiver);
            free((void *) message->receiverHost);
            goto end;
          }
          
          /* send back ok */
          if (acknowledge) {
            ok = htonl(CMSG_OK);
            if (cMsgTcpWrite(connfd, (void *) &ok, sizeof(ok)) != sizeof(ok)) {
              if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                fprintf(stderr, "clientThread %d: write failure\n", localCount);
              }
              goto end;
            }
          }       

          /* run callbacks for this message */
          err = cMsgRunCallbacks(&cMsgDomains[domainId], message);
          if (err != CMSG_OK) {
            if (err == CMSG_OUT_OF_MEMORY) {
              if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                fprintf(stderr, "clientThread %d: cannot allocate memory\n", localCount);
              }
            }
            else if (err == CMSG_LIMIT_EXCEEDED) {
              if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                fprintf(stderr, "clientThread %d: too many messages cued up\n", localCount);
              }
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
          clock_gettime(CLOCK_REALTIME, &message->receiverTime);
          message->receiver     = (char *) strdup(cMsgDomains[domainId].name);
          message->receiverHost = (char *) strdup(cMsgDomains[domainId].myHost);
          
          /* read the message */
          if ( cMsgReadMessage(connfd, buffer, message, &acknowledge) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread %d: error reading message\n", localCount);
            }
            free((void *) message->domain);
            free((void *) message->receiver);
            free((void *) message->receiverHost);
            goto end;
          }
          
          /* send back ok */
          if (acknowledge) {
            ok = htonl(CMSG_OK);
            if (cMsgTcpWrite(connfd, (void *) &ok, sizeof(ok)) != sizeof(ok)) {
              if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                fprintf(stderr, "clientThread %d: write failure\n", localCount);
              }
              goto end;
            }
          }       

          /* wakeup get caller for this message */
          cMsgWakeGet(domainId, message);
      }
      break;

      case  CMSG_KEEP_ALIVE:
      {
        int alive;
        
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "clientThread %d: keep alive received\n", localCount);
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

      case  CMSG_SHUTDOWN_CLIENTS:
      {
        if (cMsgTcpRead(connfd, &acknowledge, sizeof(acknowledge)) != sizeof(acknowledge)) {
          if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            fprintf(stderr, "clientThread %d: error reading command\n", localCount);
          }
          goto end;
        }
        acknowledge = ntohl(acknowledge);
        
        /* send back ok */
        if (acknowledge) {
          ok = htonl(CMSG_OK);
          if (cMsgTcpWrite(connfd, (void *) &ok, sizeof(ok)) != sizeof(ok)) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread %d: write failure\n", localCount);
            }
            goto end;
          }
        }       


        if (cMsgDomains[domainId].shutdownHandler != NULL) {
          cMsgDomains[domainId].shutdownHandler(cMsgDomains[domainId].shutdownUserArg);
        }
        
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "clientThread %d: told to shutdown\n", localCount);
        }
      }
      break;
      
      default:
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "clientThread %d: given unknown message (%d)\n", localCount, msgId);
        }
     
    } /* switch */

  } /* while(1) - command loop */

  /* we only end up down here if there's an error or a shutdown */
  end:
    /* client has quit or crashed, therefore clean up */
    /* fprintf(stderr, "clientThread %d: error, client's receiving connection to server broken\n", localCount); */

    /* we are done with the socket */
    close(connfd);

    /* decrease concurrency as this thread disappears */
    con = sun_getconcurrency();
    sun_setconcurrency(con - 1);
    
    /* release memory */
    free((void*)buffer);
    
    /* don't want to free the memory again */
    cMsgDomains[domainId].msgInBuffer[index] = NULL;
  
    /* quit thread */
    pthread_exit(NULL);
    return NULL;
}



#ifdef __cplusplus
}
#endif


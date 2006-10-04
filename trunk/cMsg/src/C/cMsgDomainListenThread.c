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
#include <ctype.h>
#else
#include <strings.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#include "errors.h"
#include "cMsgNetwork.h"
#include "cMsgPrivate.h"
#include "cMsg.h"
#include "cMsgDomain.h"


static int counter = 1;


/* prototypes */
static void *clientThread(void *arg);
static void  cleanUpHandler(void *arg);
static int   cMsgReadMessage(int connfd, char *buffer, cMsgMessage_t *msg, int *acknowledge);
static int   cMsgRunCallbacks(cMsgDomainInfo *domain, cMsgMessage_t *msg);
static int   cMsgWakeGet(cMsgDomainInfo *domain, cMsgMessage_t *msg);

#ifdef VXWORKS

/** Implementation of strdup for vxWorks. */
static char *strdup(const char *s1) {
    char *s;    
    if (s1 == NULL) return NULL;    
    if ((s = (char *) malloc(strlen(s1)+1)) == NULL) return NULL;    
    return strcpy(s, s1);
}

/** Implementation of strcasecmp for vxWorks. */
static int strcasecmp(const char *s1, const char *s2) {
  int i, len1, len2;
  
  /* handle NULL's */
  if (s1 == NULL && s2 == NULL) {
    return 0;
  }
  else if (s1 == NULL) {
    return -1;  
  }
  else if (s2 == NULL) {
    return 1;  
  }
  
  len1 = strlen(s1);
  len2 = strlen(s2);
  
  /* handle different lengths */
  if (len1 < len2) {
    for (i=0; i<len1; i++) {
      if (toupper((int) s1[i]) < toupper((int) s2[i])) {
        return -1;
      }
      else if (toupper((int) s1[i]) > toupper((int) s2[i])) {
         return 1;   
      }
    }
    return -1;
  }
  else if (len1 > len2) {
    for (i=0; i<len2; i++) {
      if (toupper((int) s1[i]) < toupper((int) s2[i])) {
        return -1;
      }
      else if (toupper((int) s1[i]) > toupper((int) s2[i])) {
         return 1;   
      }
    }
    return 1;  
  }
  
  /* handle same lengths */
  for (i=0; i<len1; i++) {
    if (toupper((int) s1[i]) < toupper((int) s2[i])) {
      return -1;
    }
    else if (toupper((int) s1[i]) > toupper((int) s2[i])) {
       return 1;   
    }
  }
  
  return 0;
}

#endif


/*-------------------------------------------------------------------*
 * The listening thread needs a pthread cancellation cleanup handler.
 * It will be called when the cMsgClientListeningThread is canceled.
 * It's task is to remove all the client threads.
 *-------------------------------------------------------------------*/
static void cleanUpHandler(void *arg) {
  struct timespec sTime = {0,500000000};
  cMsgThreadInfo *threadArg = (cMsgThreadInfo *) arg;
  cMsgDomainInfo *domain    = threadArg->domain;
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cMsgClientListeningThread: in cleanup handler\n");
  }

  /* cancel threads, ignore errors */
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cMsgClientListeningThread: cancelling mesage receiving threads\n");
  }
  
  if (threadArg->thd1started) {
    if (strcasecmp(threadArg->domainType, "cmsg") == 0) {
      pthread_cancel(domain->clientThread[1]);
    }
  }
  
  /* Nornally we could just cancel the thread and if the subscription
   * mutex were locked, the reinitialization would free it. However,
   * in vxWorks, reinitialization of a pthread mutex is not allowed
   * so we want to kill the thread in a way in which the mutex will
   * not end up locked.
   */  
  domain->killClientThread = 1;
  pthread_cond_signal(&domain->subscribeCond);
  nanosleep(&sTime,NULL);
  if (threadArg->thd0started) {
    pthread_cancel(domain->clientThread[0]);
  }
  domain->killClientThread = 0;
  
  /* free up arg memory */
  free(threadArg->domainType);
  free(threadArg);
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
  cMsgThreadInfo *threadArg = (cMsgThreadInfo *) arg;
  int             listenFd  = threadArg->listenFd;
  int             blocking  = threadArg->blocking;
  cMsgDomainInfo *domain    = threadArg->domain;
  int             err, status, connectionNumber=0;
  int             state, index=0;
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
  
  /* install cleanup handler for this thread's cancellation */
  pthread_cleanup_push(cleanUpHandler, arg);
  
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
    
    /* pointer to domain info */
    pinfo->domain = domain;
    /* set values to pass on to thread */
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
    
     /* send periodic (every 2 hrs.) signal to see if socket's other end is alive */
    err = setsockopt(pinfo->connfd, SOL_SOCKET, SO_KEEPALIVE, (char*) &on, sizeof(on));
    if (err < 0) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cMsgClientListeningThread: error setting socket to SO_KEEPALIVE\n");
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
    status = pthread_create(&domain->clientThread[index], &attr, clientThread, (void *) pinfo);
    if (status != 0) {
      err_abort(status, "Create client thread");
    }
    
    /* Keep track of threads that were started for use by cleanup handler. */
    if      (index == 0) threadArg->thd0started = 1;
    else if (index == 1) threadArg->thd1started = 1;
    
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cMsgClientListeningThread: started thread[%d] = %d\n",index, connectionNumber);
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
  int  err, ok, size, msgId, connfd, connectionNumber, localCount=0;
  cMsgThreadInfo *info;
  int  con, bufSize, index, otherIndex;
  char *buffer;
  int acknowledge = 0;
  cMsgDomainInfo *domain;

  
  info             = (cMsgThreadInfo *) arg;
  connfd           = info->connfd;
  connectionNumber = info->connectionNumber;
  domain           = info->domain;
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
  domain->msgInBuffer[index] = buffer;

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
      buffer = (char *) malloc(size + 1000);
      if (buffer == NULL) {
        if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
          fprintf(stderr, "clientThread %d: cannot allocate %d amount of memory\n",
                  localCount, size);
        }
        goto end;
      }
      bufSize = size + 1000;
      domain->msgInBuffer[index] = buffer;
    }
        
    /* extract command */
    msgId = ntohl(inComing[1]);
    
    switch (msgId) {

      case CMSG_SUBSCRIBE_RESPONSE:
      {
          cMsgMessage_t *message;
          message = (cMsgMessage_t *) cMsgCreateMessage();
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
          message->receiver     = (char *) strdup(domain->name);
          message->receiverHost = (char *) strdup(domain->myHost);
          
          /* read the message */
          if ( cMsgReadMessage(connfd, buffer, message, &acknowledge) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread %d: error reading message\n", localCount);
            }
            cMsgFreeMessage((void **) &message);
            goto end;
          }
          
          /* send back ok */
          if (acknowledge) {
            ok = htonl(CMSG_OK);
            if (cMsgTcpWrite(connfd, (void *) &ok, sizeof(ok)) != sizeof(ok)) {
              if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                fprintf(stderr, "clientThread %d: write failure\n", localCount);
              }
              cMsgFreeMessage((void **) &message);
              goto end;
            }
          }       

          /* run callbacks for this message */
          err = cMsgRunCallbacks(domain, message);
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
            cMsgFreeMessage((void **) &message);
            goto end;
          }
      }
      break;

      case CMSG_GET_RESPONSE:
      {
          cMsgMessage_t *message;
          message = (cMsgMessage_t *) cMsgCreateMessage();
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
          message->receiver     = (char *) strdup(domain->name);
          message->receiverHost = (char *) strdup(domain->myHost);
          
          /* read the message */
          if ( cMsgReadMessage(connfd, buffer, message, &acknowledge) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread %d: error reading message\n", localCount);
            }
            cMsgFreeMessage((void **) &message);
            goto end;
          }
          
          /* send back ok */
          if (acknowledge) {
            ok = htonl(CMSG_OK);
            if (cMsgTcpWrite(connfd, (void *) &ok, sizeof(ok)) != sizeof(ok)) {
              if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                fprintf(stderr, "clientThread %d: write failure\n", localCount);
              }
              cMsgFreeMessage((void **) &message);
              goto end;
            }
          }       

          /* wakeup get caller for this message */
          cMsgWakeGet(domain, message);
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


        if (domain->shutdownHandler != NULL) {
          domain->shutdownHandler(domain->shutdownUserArg);
        }
        
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "clientThread %d: told to shutdown\n", localCount);
        }
      }
      break;
      
      /* for RC server & RC domains only */
      case  CMSG_RC_CONNECT:
      {
          cMsgMessage_t *message;
          struct timespec wait;
          
/*printf("clientThread %d: Got CMSG_RC_CONNECT message!!!\n", localCount);*/

          message = (cMsgMessage_t *) cMsgCreateMessage();
          if (message == NULL) {
            if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
              fprintf(stderr, "clientThread %d: cannot allocate memory\n", localCount);
            }
            exit(1);
          }

          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "clientThread %d: RC Server connect received\n", localCount);
          }
                    
          /* read the message */
          if ( cMsgReadMessage(connfd, buffer, message, &acknowledge) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread %d: error reading message\n", localCount);
            }
            cMsgFreeMessage((void **) &message);
            goto end;
          }
          
          /* We need 2 pieces of info from the server: 1) server's host,
           * 2) server's UDP port. These are in the message and must be
           * recorded in the domain structure for future use.
           */
          domain->sendPort = message->userInt;
          domain->sendHost = (char *) strdup(message->senderHost);
          
          /* notify "connect" call that it may resume and end now */
          wait.tv_sec  = 1;
          wait.tv_nsec = 0;
          domain->rcConnectComplete = 1;
          cMsgLatchCountDown(&domain->syncLatch, &wait);
                    
          /* now free message */
          cMsgFreeMessage((void **) &message);
      }
      break;

      /* for RC server & RC domains only */
      case  CMSG_RC_RECONNECT:
      {
          cMsgMessage_t *message;
          struct timespec wait;
          
/*printf("clientThread %d: Got CMSG_RC_CONNECT message!!!\n", localCount);*/

          message = (cMsgMessage_t *) cMsgCreateMessage();
          if (message == NULL) {
            if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
              fprintf(stderr, "clientThread %d: cannot allocate memory\n", localCount);
            }
            exit(1);
          }

          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "clientThread %d: RC Server connect received\n", localCount);
          }
                    
          /* read the message - nothing of value in it */
          if ( cMsgReadMessage(connfd, buffer, message, &acknowledge) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread %d: error reading message\n", localCount);
            }
            cMsgFreeMessage((void **) &message);
            goto end;
          }
                              
          /* now free message */
          cMsgFreeMessage((void **) &message);
          
          /* kill other thread waiting to read from the (dead) rc server */
          otherIndex = (index == 1) ? 0 : 1;
          pthread_cancel(domain->clientThread[otherIndex]);
          /* release memory */
          free((void*)(domain->msgInBuffer[otherIndex]));
          /* don't want to free the memory again */
          domain->msgInBuffer[otherIndex] = NULL;
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
    domain->msgInBuffer[index] = NULL;
  
    /* quit thread */
    pthread_exit(NULL);
    return NULL;
}


/*-------------------------------------------------------------------*/

/*
 * This routine is called by a single thread spawned from the client's
 * listening thread. Since it's called serially, it can safely use
 * arrays declared at the top of the file.
 */
/** This routine reads a message sent from the server to the client. */
static int cMsgReadMessage(int connfd, char *buffer, cMsgMessage_t *msg, int *acknowledge) {

  uint64_t llTime;
  int  stringLen, lengths[7], inComing[18];
  char *pchar, *tmp;
    
  if (cMsgTcpRead(connfd, inComing, sizeof(inComing)) != sizeof(inComing)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: error reading message 1\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* swap to local endian */
  msg->version  = ntohl(inComing[0]);  /* major version of cMsg */
  /* second int is for future use */
  msg->userInt  = ntohl(inComing[2]);  /* user int */

  msg->info     = ntohl(inComing[3]);  /* get info */
  /*
   * Time arrives as the high 32 bits followed by the low 32 bits
   * of a 64 bit integer in units of milliseconds.
   */
  llTime = (((uint64_t) ntohl(inComing[4])) << 32) |
           (((uint64_t) ntohl(inComing[5])) & 0x00000000FFFFFFFF);
  /* turn long long into struct timespec */
  msg->senderTime.tv_sec  =  llTime/1000;
  msg->senderTime.tv_nsec = (llTime%1000)*1000000;
  
  llTime = (((uint64_t) ntohl(inComing[6])) << 32) |
           (((uint64_t) ntohl(inComing[7])) & 0x00000000FFFFFFFF);
  msg->userTime.tv_sec  =  llTime/1000;
  msg->userTime.tv_nsec = (llTime%1000)*1000000;
  
  msg->sysMsgId    = ntohl(inComing[8]);  /* system msg id */
  msg->senderToken = ntohl(inComing[9]);  /* sender token */
  lengths[0]       = ntohl(inComing[10]); /* sender length */
  lengths[1]       = ntohl(inComing[11]); /* senderHost length */
  lengths[2]       = ntohl(inComing[12]); /* subject length */
  lengths[3]       = ntohl(inComing[13]); /* type length */
  lengths[4]       = ntohl(inComing[14]); /* creator length */
  lengths[5]       = ntohl(inComing[15]); /* text length */
  lengths[6]       = ntohl(inComing[16]); /* binary length */
  *acknowledge     = ntohl(inComing[17]); /* acknowledge receipt of message? (1-y,0-n) */
  
  /* length of strings to read in */
  stringLen = lengths[0] + lengths[1] + lengths[2] +
              lengths[3] + lengths[4] + lengths[5];
  
  if (cMsgTcpRead(connfd, buffer, stringLen) != stringLen) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: error reading message 2\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* init pointer */
  pchar = buffer;

  /*--------------------*/
  /* read sender string */
  /*--------------------*/
  /* allocate memory for sender string */
  if ( (tmp = (char *) malloc(lengths[0]+1)) == NULL) {
    return(CMSG_OUT_OF_MEMORY);    
  }
  /* read sender string into memory */
  memcpy(tmp, pchar, lengths[0]);
  /* add null terminator to string */
  tmp[lengths[0]] = 0;
  /* store string in msg structure */
  msg->sender = tmp;
  /* go to next string */
  pchar += lengths[0];
  /*printf("sender = %s\n", tmp);*/
      
  /*------------------------*/
  /* read senderHost string */
  /*------------------------*/
  if ( (tmp = (char *) malloc(lengths[1]+1)) == NULL) {
    free((void *) msg->sender);
    msg->sender = NULL;
    return(CMSG_OUT_OF_MEMORY);    
  }
  memcpy(tmp, pchar, lengths[1]);
  tmp[lengths[1]] = 0;
  msg->senderHost = tmp;
  pchar += lengths[1];
  /*printf("senderHost = %s\n", tmp);*/
  
  /*---------------------*/
  /* read subject string */
  /*---------------------*/
  if ( (tmp = (char *) malloc(lengths[2]+1)) == NULL) {
    free((void *) msg->sender);
    free((void *) msg->senderHost);
    msg->sender     = NULL;
    msg->senderHost = NULL;
    return(CMSG_OUT_OF_MEMORY);    
  }
  memcpy(tmp, pchar, lengths[2]);
  tmp[lengths[2]] = 0;
  msg->subject = tmp;
  pchar += lengths[2];  
  /*printf("subject = %s\n", tmp);*/
  
  /*------------------*/
  /* read type string */
  /*------------------*/
  if ( (tmp = (char *) malloc(lengths[3]+1)) == NULL) {
    free((void *) msg->sender);
    free((void *) msg->senderHost);
    free((void *) msg->subject);
    msg->sender     = NULL;
    msg->senderHost = NULL;
    msg->subject    = NULL;
    return(CMSG_OUT_OF_MEMORY);    
  }
  memcpy(tmp, pchar, lengths[3]);
  tmp[lengths[3]] = 0;
  msg->type = tmp;
  pchar += lengths[3];    
  /*printf("type = %s\n", tmp);*/
  
  /*---------------------*/
  /* read creator string */
  /*---------------------*/
  if ( (tmp = (char *) malloc(lengths[4]+1)) == NULL) {
    free((void *) msg->sender);
    free((void *) msg->senderHost);
    free((void *) msg->subject);
    free((void *) msg->type);
    msg->sender     = NULL;
    msg->senderHost = NULL;
    msg->subject    = NULL;
    msg->type       = NULL;
    return(CMSG_OUT_OF_MEMORY);    
  }
  memcpy(tmp, pchar, lengths[4]);
  tmp[lengths[4]] = 0;
  msg->creator = tmp;
  pchar += lengths[4];    
  /*printf("creator = %s\n", tmp);*/
    
  /*------------------*/
  /* read text string */
  /*------------------*/
  if (lengths[5] > 0) {
    if ( (tmp = (char *) malloc(lengths[5]+1)) == NULL) {
      free((void *) msg->sender);
      free((void *) msg->senderHost);
      free((void *) msg->subject);
      free((void *) msg->type);
      free((void *) msg->creator);
      msg->sender     = NULL;
      msg->senderHost = NULL;
      msg->subject    = NULL;
      msg->type       = NULL;
      msg->creator    = NULL;
      return(CMSG_OUT_OF_MEMORY);    
    }
    memcpy(tmp, pchar, lengths[5]);
    tmp[lengths[5]] = 0;
    msg->text = tmp;
    pchar += lengths[5];    
    /*printf("text = %s\n", tmp);*/
  }
  
  /*-----------------------------*/
  /* read binary into byte array */
  /*-----------------------------*/
  if (lengths[6] > 0) {
    
    if ( (tmp = (char *) malloc(lengths[6])) == NULL) {
      free((void *) msg->sender);
      free((void *) msg->senderHost);
      free((void *) msg->subject);
      free((void *) msg->type);
      free((void *) msg->creator);
      msg->sender     = NULL;
      msg->senderHost = NULL;
      msg->subject    = NULL;
      msg->type       = NULL;
      msg->creator    = NULL;
      
      if (lengths[5] > 0) {
        free((void *) msg->text);
        msg->text = NULL;
      }
      return(CMSG_OUT_OF_MEMORY);    
    }
    
    if (cMsgTcpRead(connfd, tmp, lengths[6]) != lengths[6]) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cMsgReadMessage: error reading message 3\n");
      }
      
      free((void *) msg->sender);
      free((void *) msg->senderHost);
      free((void *) msg->subject);
      free((void *) msg->type);
      free((void *) msg->creator);
      msg->sender     = NULL;
      msg->senderHost = NULL;
      msg->subject    = NULL;
      msg->type       = NULL;
      msg->creator    = NULL;
      
      if (lengths[5] > 0) {
        free((void *) msg->text);
        msg->text = NULL;
      }

      return(CMSG_NETWORK_ERROR);
    }

    msg->byteArray       = tmp;
    msg->byteArrayOffset = 0;
    msg->byteArrayLength = lengths[6];
    msg->bits |= CMSG_BYTE_ARRAY_IS_COPIED; /* byte array is COPIED */
    /*        
    for (;i<lengths[6]; i++) {
        printf("%d ", (int)msg->byteArray[i]);
    }
    printf("\n");
    */
            
  }
  
      
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/


/**
 * This routine wakes up the appropriate sendAndGet
 * when a message arrives from the server. 
 */
static int cMsgWakeGet(cMsgDomainInfo *domain, cMsgMessage_t *msg) {

  int i, status, delivered=0;
  getInfo *info;  
   
  /* find the right get */
  for (i=0; i<CMSG_MAX_SEND_AND_GET; i++) {
    
    info = &domain->sendAndGetInfo[i];

    if (info->active != 1) {
      continue;
    }
    
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
    cMsgFreeMessage((void **) &msg);
  }
  
  return (CMSG_OK);
}


/*-------------------------------------------------------------------*/

/**
 * This routine runs all the appropriate subscribe and subscribeAndGet
 * callbacks when a message arrives from the server. 
 */
static int cMsgRunCallbacks(cMsgDomainInfo *domain, cMsgMessage_t *msg) {

  int i, j, k, status, goToNextCallback;
  subscribeCbInfo *cback;
  getInfo *info;
  cMsgMessage_t *message, *oldHead;
  struct timespec wait, timeout;
    

  /* wait 60 sec between warning messages for a full cue */
  timeout.tv_sec  = 3;
  timeout.tv_nsec = 0;
    
  /* for each subscribeAndGet ... */
  for (j=0; j<CMSG_MAX_SUBSCRIBE_AND_GET; j++) {
    
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
      message = (cMsgMessage_t *) cMsgCopyMessage((void *)msg);
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
    cMsgFreeMessage((void **) &msg);
    return (CMSG_OK);
  }
   
  /* Don't want subscriptions added or removed while iterating through them. */
  cMsgSubscribeMutexLock(domain);
  
  /* for each client subscription ... */
  for (i=0; i<CMSG_MAX_SUBSCRIBE; i++) {

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
      for (j=0; j<CMSG_MAX_CALLBACK; j++) {
        /* convenience variable */
        cback = &domain->subscribeInfo[i].cbInfo[j];

	/* if there is no existing callback, look at next item ... */
        if (cback->active != 1) {
          continue;
        }

        /* copy message so each callback has its own copy */
        message = (cMsgMessage_t *) cMsgCopyMessage((void *)msg);
        if (message == NULL) {
          cMsgSubscribeMutexUnlock(domain);
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
              cMsgMutexLock(&cback->mutex);
/* fprintf(stderr, "cMsgRunCallbacks: grabbed mutex\n"); */

              for (k=0; k < cback->config.skipSize; k++) {
                oldHead = cback->head;
                cback->head = cback->head->next;
                cMsgFreeMessage((void **)&oldHead);
                cback->messages--;
                if (cback->head == NULL) break;
              }

              cMsgMutexUnlock(&cback->mutex);

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
                  cMsgGetAbsoluteTime(&timeout, &wait);        
/* fprintf(stderr, "cMsgRunCallbacks: cue full, start waiting, will UNLOCK mutex\n"); */
                  status = pthread_cond_timedwait(&domain->subscribeCond, &domain->subscribeMutex, &wait);
/* fprintf(stderr, "cMsgRunCallbacks: out of wait, mutex is LOCKED\n"); */
                  
                  /* Check to see if server died and this thread is being killed. */
                  if (domain->killClientThread == 1) {
                    cMsgSubscribeMutexUnlock(domain);
                    cMsgFreeMessage((void **) &message);
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
                    cMsgFreeMessage((void **) &message);
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

        cMsgMutexLock(&cback->mutex);

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
        cMsgMutexUnlock(&cback->mutex);
/* printf("cMsgRunCallbacks: mutex is UNLOCKED, msg taken off cue, broadcast to callback thd\n"); */

        /* wakeup callback thread */
        status = pthread_cond_broadcast(&cback->cond);
        if (status != 0) {
          err_abort(status, "Failed callback condition signal");
        }

      } /* search callback list */
    } /* if subscribe sub/type matches msg sub/type */
  } /* for each cback */

  cMsgSubscribeMutexUnlock(domain);
  
  /* Need to free up msg allocated by client's listening thread */
  cMsgFreeMessage((void **) &msg);
  
  return (CMSG_OK);
}

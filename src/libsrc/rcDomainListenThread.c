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

#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include "cMsgPrivate.h"
#include "cMsg.h"
#include "cMsgNetwork.h"
#include "cMsgDomain.h"

/* for debugging threaded code */
static int counter = 1;

/* for only allowing 1 rc server to connect at one time */
static int connected = 0;

/* prototypes */
static void *clientThread(void *arg);
static void  cleanUpHandler(void *arg);
static void  cleanUpClientHandler(void *arg);

/** Structure for freeing memory and closing socket in cleanUpClientHandler. */
typedef struct freeMem_t {
    char *buffer;
    int   fd;
    cMsgDomainInfo *domain;
} freeMem;



/*-------------------------------------------------------------------*
 * The listening thread needs a pthread cancellation cleanup handler.
 * It will be called when the rcClientListeningThread is cancelled
 * (which only happens when user calls disconnect).
 * It's task is to remove all the client threads.
 *-------------------------------------------------------------------*/
static void cleanUpHandler(void *arg) {
  struct timespec sTime = {0,50000000}; /* 0.05 sec */
  cMsgThreadInfo *threadArg = (cMsgThreadInfo *) arg;
  cMsgDomainInfo *domain    = threadArg->domain;
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "rcClientListeningThread: in cleanup handler\n");
  }
  
  /* If we're in this code, the user called disconnect so go ahead and
   * cancel spawned thread. */
  nanosleep(&sTime,NULL);
  if (threadArg->thdstarted) {
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "rcClientListeningThread: cancelling mesage receiving thread\n");
    }
    pthread_cancel(domain->clientThread);
  }

  /* pend thread no longer using domain struct/memory */
  cMsgMutexLock(&domain->syncSendMutex); /* use mutex unused elsewhere in rc domain */
  domain->functionsRunning--;
  cMsgMutexUnlock(&domain->syncSendMutex);
  
  /* free up arg memory */
  free(threadArg);
}


/*-------------------------------------------------------------------*
 * A client handling thread needs a pthread cancellation cleanup handler.
 * This handler will be called when the rcClientListeningThread is
 * cancelled which, in turn, cancels each of the client handling threads. 
 * It's task is to free memory allocated for the communication buffer.
 *-------------------------------------------------------------------*/
static void cleanUpClientHandler(void *arg) {
  freeMem *pMem = (freeMem *)arg;
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "clientThread: in cleanup handler\n");
  }
  
  /* thread no longer using domain struct/memory */
  cMsgMutexLock(&pMem->domain->syncSendMutex); /* use mutex unused elsewhere in rc domain */
  pMem->domain->functionsRunning--;
  cMsgMutexUnlock(&pMem->domain->syncSendMutex);

  /* close socket */
  close(pMem->fd);

  /* release memory */
  if (pMem == NULL) return;
  if (pMem->buffer != NULL) free(pMem->buffer);
  free(pMem);
}


/*-------------------------------------------------------------------*
 * rcClientListeningThread is a listening thread used by a rc domain client
 * to allow a connection from the rc server. Note that more than one
 * connection may be made if the rc Server dies, comes back again, and
 * wants to re-establish a connection to clients.
 *
 * In order to be able to kill this thread and its children threads
 * on demand, it must not block.
 * This thread may use a non-blocking socket and waits on a "select"
 * statement instead of the blocking "accept" statement.
 *-------------------------------------------------------------------*/
void *rcClientListeningThread(void *arg)
{
  cMsgThreadInfo *threadArg = (cMsgThreadInfo *) arg;
  int             listenFd  = threadArg->listenFd;
  int             blocking  = threadArg->blocking;
  cMsgDomainInfo *domain    = threadArg->domain;
  int             err, status, state, inComing[3];
  const int       on=1;
  int             rcvBufSize = CMSG_BIGSOCKBUFSIZE;
  fd_set          readSet;
  struct timeval  timeout;
  struct sockaddr_in cliaddr;
  socklen_t	  addrlen, len;
  
  /* pointer to information to be passed to threads */
  cMsgThreadInfo *pinfo;
      
  /* release system resources when thread finishes */
  /*pthread_detach(pthread_self());*//* this thread joined by disconnect */

  addrlen = sizeof(cliaddr);
      
  /* enable pthread cancellation at deferred points like pthread_testcancel */
  status = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);  
  if (status != 0) {
    cmsg_err_abort(status, "Enabling client cancelability");
  }
  
  /* install cleanup handler for this thread's cancellation */
  pthread_cleanup_push(cleanUpHandler, arg);
  
  /* Tell spawning thread that we're up and running */
  threadArg->isRunning = 1;
  
  /* we're using domain struct/memory */
  cMsgMutexLock(&domain->syncSendMutex); /* use mutex unused elsewhere in rc domain */
  domain->functionsRunning++;
  cMsgMutexUnlock(&domain->syncSendMutex);
  
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
          fprintf(stderr, "rcClientListeningThread: select call error: %s\n", strerror(errno));
        }
        break;
      }
      /* try again */
      else {
        continue;
      }
    }
    
    /* only allow one rc server connection at one time */
    cMsgMutexLock(&domain->syncSendMutex);
    if (connected) {
        cMsgMutexUnlock(&domain->syncSendMutex);
        continue;
    }
    cMsgMutexUnlock(&domain->syncSendMutex);
    
    /* get things ready for accept call */
    len = addrlen;

    /* allocate argument to pass to thread */
    pinfo = (cMsgThreadInfo *) malloc(sizeof(cMsgThreadInfo));
    if (pinfo == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "rcClientListeningThread: cannot allocate memory\n");
      }
      exit(1);
    }
    
    /* pointer to domain info */
    pinfo->domain = domain;
    /* wait for connection to client */
    pinfo->connfd = err = cMsgNetAccept(listenFd, (SA *) &cliaddr, &len);
    pinfo->arg = threadArg;
    /* ignore errors due to client shutting down the connection before
     * it can be established on this end. (EWOULDBLOCK, ECONNABORTED,
     * EPROTO) 
     */
    if (err < 0) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "rcClientListeningThread: error accepting client connection\n");
      }
      free(pinfo);
      continue;
    }
    
    /* don't wait for messages to cue up, send any message immediately */
    err = setsockopt(pinfo->connfd, IPPROTO_TCP, TCP_NODELAY, (char*) &on, sizeof(on));
    if (err < 0) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
          fprintf(stderr, "rcClientListeningThread: error setting socket to TCP_NODELAY\n");
      }
      close(pinfo->connfd);
      free(pinfo);
      continue;
    }
    
     /* send periodic (every 2 hrs.) signal to see if socket's other end is alive */
    err = setsockopt(pinfo->connfd, SOL_SOCKET, SO_KEEPALIVE, (char*) &on, sizeof(on));
    if (err < 0) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "rcClientListeningThread: error setting socket to SO_KEEPALIVE\n");
      }
      close(pinfo->connfd);
      free(pinfo);
      continue;
    }

    /* set receive buffer size */
    err = setsockopt(pinfo->connfd, SOL_SOCKET, SO_RCVBUF, (char*) &rcvBufSize, sizeof(rcvBufSize));
    if (err < 0) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "rcClientListeningThread: error setting socket receiving buffer size\n");
      }
      close(pinfo->connfd);
      free(pinfo);
      continue;
    }

    /* read in magic numbers from rcServer ("cMsg is cool" in ascii) */
    if (cMsgNetTcpRead(pinfo->connfd, inComing, sizeof(inComing)) != sizeof(inComing)) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "rcClientListeningThread: error reading magic #s\n");
      }
      close(pinfo->connfd);
      free(pinfo);
      continue;
    }
    
    /* check magic numbers to filter out port-scanners */
    if ( (ntohl((uint32_t)inComing[0]) != CMSG_MAGIC_INT1) ||
         (ntohl((uint32_t)inComing[1]) != CMSG_MAGIC_INT2) ||
         (ntohl((uint32_t)inComing[2]) != CMSG_MAGIC_INT3)   ) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "rcClientListeningThread: wrong magic #s from connecting process\n");
      }
      close(pinfo->connfd);
      free(pinfo);
      continue;
    }    

    /* create thread to deal with client */
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "rcClientListeningThread: accepting client connection\n");
    }
    
    /* A single connection is made for the rc domain. If the rc server fails it may
     * try to reconnect to this client on its restart.
     */
    status = pthread_create(&domain->clientThread, NULL, clientThread, (void *) pinfo);
    if (status != 0) {
      cmsg_err_abort(status, "Create client thread");
    }
    
    /* Keep track of thread that was started for use by cleanup handler.
     * If rcServer dies, the thread just spawned will die. If rcServer starts
     * up again, it will reconnect and spawn another thread. There should
     * only be 1 running at a time.
     */
    threadArg->thdstarted = 1;
    
    /* only allow one rc server connection at one time */
    cMsgMutexLock(&domain->syncSendMutex);
    if (connected) {
        connected = 1;
    }
    cMsgMutexUnlock(&domain->syncSendMutex);
    
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "rcClientListeningThread: started thread\n");
    }
    
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
  int  err, size, msgId, connfd, localCount=0;
  int  status, state;
  size_t bufSize;
  cMsgThreadInfo *info, *origArg;
  char *buffer;
  freeMem *pfreeMem=NULL;
  cMsgDomainInfo *domain;
  struct timespec wait;

  
  info    = (cMsgThreadInfo *) arg;
  connfd  = info->connfd;
  domain  = info->domain;
  origArg = info->arg;
  free(arg);

  
  localCount = counter++;

  /* release system resources when thread finishes */
  pthread_detach(pthread_self());

  /* disable pthread cancellation until pointer is set and handler is installed */
  
  status = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &state);  
  if (status != 0) {
    cmsg_err_abort(status, "Disabling client cancelability");
  }
  
  /* Create pointer to malloced mem which will hold 2 pointers. */
  pfreeMem = (freeMem *) malloc(sizeof(freeMem));
  if (pfreeMem == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "clientThread %d: cannot allocate memory\n", localCount);
      }
      exit(1);
  }

  /* Create buffer which must be freed upon thread cancellation. */   
  bufSize = 65536;
  buffer  = (char *) calloc(1, bufSize);
  if (buffer == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "clientThread %d: cannot allocate memory\n", localCount);
      }
      exit(1);
  }
  
  /* Install a cleanup handler for this thread's cancellation. 
   * Give it a pointer which points to the memory which must
   * be freed upon cancelling this thread. */
  pfreeMem->domain = domain;
  pfreeMem->buffer = buffer;
  pfreeMem->fd = connfd;
  pthread_cleanup_push(cleanUpClientHandler, (void *)pfreeMem);
  
  /* enable pthread cancellation at deferred points like pthread_testcancel */
  
  status = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);  
  if (status != 0) {
    cmsg_err_abort(status, "Enabling client cancelability");
  }
  
  /* we're using domain struct/memory */
  cMsgMutexLock(&domain->syncSendMutex); /* use mutex unused elsewhere in rc domain */
  domain->functionsRunning++;
  cMsgMutexUnlock(&domain->syncSendMutex);

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
    
    if (cMsgNetTcpRead(connfd, inComing, sizeof(inComing)) != sizeof(inComing)) {
      /*
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "clientThread %d: error reading command\n", localCount);
      }
      */
      /* if there's a timeout, try again */
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        /* test to see if someone wants to shutdown this thread */
        pthread_testcancel();
        goto retry;
      }
      goto end;
    }
    
    size = ntohl((uint32_t) inComing[0]);
    
    /* extract command */
    msgId = ntohl((uint32_t) inComing[1]);

/*fprintf(stderr, "clientThread %d: size = %d bytes, msgId = %d\n", localCount, size, msgId);*/

    if (msgId != CMSG_SUBSCRIBE_RESPONSE &&
        msgId != CMSG_SYNC_SEND_REQUEST &&
        msgId != CMSG_RC_CONNECT_ABORT &&
        msgId != CMSG_RC_CONNECT) {
    
fprintf(stderr, "clientThread %d: bad command (%d), quitting thread\n", localCount, msgId);
          goto end;
    }
    
    /* make sure we have big enough buffer */
    if (size > bufSize) {
      
      /* disable pthread cancellation until pointer is set */
      
      status = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &state);  
      if (status != 0) {
        cmsg_err_abort(status, "Disabling client cancelability");
      }
      
      /* free previously allocated memory */
      free((void *) buffer);

      /* allocate more memory to accomodate larger msg */
      bufSize = (size_t)(size + 1000);
      buffer  = (char *) calloc(1, bufSize);
      pfreeMem->buffer = buffer;
      if (buffer == NULL) {
        if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
          fprintf(stderr, "clientThread %d: cannot allocate %d amount of memory\n",
                  localCount, (int)bufSize);
        }
        exit(1);
      }
      
      /* re-enable pthread cancellation */
      
      status = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);  
      if (status != 0) {
        cmsg_err_abort(status, "Reenabling client cancelability");
      }
    }
        
    
    switch (msgId) {

      case CMSG_SUBSCRIBE_RESPONSE:
      {
          void *msg;
          cMsgMessage_t *message;
          
          /* disable pthread cancellation until message memory is released or
           * we'll get a mem leak */
          status = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &state);  
          if (status != 0) {
            cmsg_err_abort(status, "Disabling client cancelability");
          }
          
          msg = cMsgCreateMessage();
          message = (cMsgMessage_t *) msg;
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
          message->next = NULL;
          clock_gettime(CLOCK_REALTIME, &message->receiverTime);
          message->domain  = strdup("rc");
          if (domain->name != NULL) {
              message->receiver = strdup(domain->name);
          }
          if (domain->myHost != NULL) {
              message->receiverHost = strdup(domain->myHost);
          }
          
          /* read the message */
          if ( cMsgReadMessage(connfd, buffer, message) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread %d: error reading message\n", localCount);
            }
            cMsgFreeMessage(&msg);
            goto end;
          }
          
          /* run callbacks for this message */
          err = cMsgRunCallbacks(domain, msg);
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
            cMsgFreeMessage(&msg);
            goto end;
          }
          
          /* re-enable pthread cancellation */
          status = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);
          if (status != 0) {
            cmsg_err_abort(status, "Reenabling client cancelability");
          }
      }
      break;

      
      /* RC server pinging this client */
      case  CMSG_SYNC_SEND_REQUEST:
      { 
			int32_t answer = htonl(1);          
      		if (cMsgNetTcpWrite(connfd, &answer, sizeof(int32_t)) != sizeof(int32_t)) {
          		if (cMsgDebug >= CMSG_DEBUG_ERROR) {
            		fprintf(stderr, "clientThread %d: write failure\n", localCount);
				}
          		goto end;
          	}
      }
      break;
      

      /* for RC server & RC domains only */
      case  CMSG_RC_CONNECT_ABORT:
      {
            domain->rcConnectAbort = 1;

            /* disable pthread cancellation to protect mutexes */
            status = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &state);
            if (status != 0) {
              cmsg_err_abort(status, "Disabling client cancelability");
            }
            
            cMsgLatchCountDown(&domain->syncLatch, &wait);
            
            /* re-enable pthread cancellation */
            status = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);
            if (status != 0) {
              cmsg_err_abort(status, "Reenabling client cancelability");
            }
      }
      break;
      

      /* from RC Broadcast server only */
      case  CMSG_RC_CONNECT:
      {
          void *msg;
          cMsgMessage_t *message;
          const char *serverIp = NULL;
          
/*printf("rc clientThread %d: Got CMSG_RC_CONNECT message\n", localCount);*/
          /* disable pthread cancellation until message memory is released or
           * we'll get a mem leak */
          status = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &state);  
          if (status != 0) {
            cmsg_err_abort(status, "Disabling client cancelability");
          }
          
          msg = cMsgCreateMessage();
          message = (cMsgMessage_t *) msg;
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
          if ( cMsgReadMessage(connfd, buffer, message) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread %d: error reading message\n", localCount);
            }
            cMsgFreeMessage(&msg);
            goto end;
          }
          
          /* We need 3 pieces of info from the server: 1) server's host,
           * 2) server's UDP port, 3) server's TCP port. These are in the message
           * and must be recorded in the domain structure for future use.
           */
         
          domain->sendUdpPort = atoi(message->text);
          domain->sendPort = message->userInt;

          /* store IP from which this connection was made */
          err = cMsgGetString(msg, "serverIp", &serverIp);
          if (err == CMSG_OK) {
              if (domain->sendHost != NULL) {
                  free(domain->sendHost);
                  domain->sendHost = strdup(serverIp);
              }
          }
          else if (message->senderHost != NULL) {
              if (domain->sendHost != NULL) free(domain->sendHost);
              domain->sendHost = strdup(message->senderHost);
          }
          else {
              domain->sendHost = strdup("unknownHost");
          }

          /* now free message */
          cMsgFreeMessage(&msg);

/*printf("rc clientThread %d: connecting, tcp port = %d, udp port = %d, senderHost = %s\n",
localCount, domain->sendPort, domain->sendUdpPort, domain->sendHost);*/
          
          /* First look to see if we are already connected.
           * If so, then the server must have died, been resurrected,
           * and is trying to RE-establish the connection.
           * If not, this is a first time connection which should
           * go ahead and complete the standard connection procedure.
           */
          if (domain->gotConnection == 0) {
/*printf("rc clientThread %d: try to connect for first time by setting latch\n", localCount);*/
            /* notify "connect" call that it may resume and end now */
            wait.tv_sec  = 1;
            wait.tv_nsec = 0;
            domain->rcConnectComplete = 1;
            cMsgLatchCountDown(&domain->syncLatch, &wait);
          }
          else {
            /*
             * Other thread waiting to read from the (dead) rc server
             * will automatically die because it will try to read from
             * dead socket and go to error handling at the end of this
             * function.
             *
             * Recreate broken udp & tcp sockets between client and server 
             * so client can communicate with server.
             *
             * Create a new UDP "connection". This means all subsequent sends are to
             * be done with the "send" and not the "sendto" function. The benefit is 
             * that the udp socket does not have to connect and disconnect for each
             * message sent.
             */
            const int sendBufSize=CMSG_BIGSOCKBUFSIZE; /* bytes */
            struct sockaddr_in addr;
            memset((void *)&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port   = htons((uint16_t)domain->sendUdpPort);
/*printf("rc clientThread %d: try to reconnect\n", localCount);*/
            
            /*
             * Don't allow changes to the sockets while communication
             * may still be going on between this client and the server.
             */
            cMsgSocketMutexLock(domain);
            
            /* create new UDP socket for sends */
            close(domain->sendUdpSocket); /* close old UDP socket */
            domain->sendUdpSocket = socket(AF_INET, SOCK_DGRAM, 0);
/*printf("rc clientThread %d: udp socket = %d, port = %d\n", localCount,
       domain->sendUdpSocket, domain->sendUdpPort);*/
            if (domain->sendUdpSocket < 0) {
                cMsgSocketMutexUnlock(domain);
                if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                  fprintf(stderr, "clientThread %d: error 1 recreating rc client's UDP send socket\n", localCount);
                }
                goto end;
            }
            
            /* set send buffer size */
            err = setsockopt(domain->sendUdpSocket, SOL_SOCKET, SO_SNDBUF, (char*) &sendBufSize, sizeof(sendBufSize));
            if (err < 0) {
                cMsgSocketMutexUnlock(domain);
                if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                  fprintf(stderr, "clientThread %d: error 2 recreating rc client's UDP send socket\n", localCount);
                }
                goto end;
            }

            if ( (err = cMsgNetStringToNumericIPaddr(domain->sendHost, &addr)) != CMSG_OK ) {
                cMsgSocketMutexUnlock(domain);
                if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                  fprintf(stderr, "clientThread %d: error 3 recreating rc client's UDP send socket\n", localCount);
                }
                goto end;
            }

/*printf("try UDP connection to port = %hu\n", ntohs(addr.sin_port));*/
            err = connect(domain->sendUdpSocket, (SA *)&addr, sizeof(addr));
            if (err < 0) {
                cMsgSocketMutexUnlock(domain);
                if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                  fprintf(stderr, "clientThread %d: error 4 recreating rc client's UDP send socket\n", localCount);
                }
                goto end;
            }

            /* create TCP sending socket and store */
            if ( (err = cMsgNetTcpConnect(domain->sendHost, NULL,
                                          (unsigned short) domain->sendPort,
                                          CMSG_BIGSOCKBUFSIZE, 0, 1,
                                          &domain->sendSocket, NULL)) != CMSG_OK) {
                cMsgSocketMutexUnlock(domain);
                if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                  fprintf(stderr, "clientThread %d: error recreating rc client's TCP send socket\n", localCount);
                }
                goto end;
            }
printf("rc clientThread %d: reconnect to host = %s, port = %d\n", localCount,
       domain->sendHost, domain->sendPort);

            /* Allow socket communications to resume. */
            cMsgSocketMutexUnlock(domain);
          }
          
          /* re-enable pthread cancellation */
          status = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);
          if (status != 0) {
               cmsg_err_abort(status, "Reenabling client cancelability");
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
/*fprintf(stderr, "clientThread %d: error, client's receiving connection to server BROKEN\n", localCount);*/
    
    /* record the fact that this thread is disappearing */
    origArg->thdstarted = 0;
    
    /* on some operating systems (Linux) this call is necessary - calls cleanup handler */
    pthread_cleanup_pop(1);
    
    /* only allow one rc server connection at one time */
    cMsgMutexLock(&domain->syncSendMutex);
    connected = 0;
    cMsgMutexUnlock(&domain->syncSendMutex);
    
    return NULL;
}

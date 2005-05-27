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

/**
 * Array containing a structure of information about each client
 * connection to the cMsg domain.
 */
extern cMsgDomain_CODA cMsgDomains[];

/**
 * This structure keeps track of each client thread so it can be
 * disposed of later when the user is done with cMsg.
 */
typedef struct cMsgClientThreadInfo_t {
  int       isUsed;     /**< Is this item being used / is threadId valid? (1-y, 0-n) */
  pthread_t threadId;   /**< Pthread id of client thread. */
} cMsgClientThreadInfo;


/**
 * This structure passes relevant info to each thread serving
 * a cMsg connection.
 */
typedef struct cMsgThreadInfo_t {
  int connfd;   /**< Socket connection's file descriptor. */
  int domainId; /**< Index into the cMsgDomains array. */
  cMsgClientThreadInfo *clientInfo; /**< Pointer to client thread info of this thread. */
} cMsgThreadInfo;


/* set debug level here for convenience (else declared global in cMsgPrivate.h */
/*static int cMsgDebug = CMSG_DEBUG_INFO;*/

/** Maximum number of clients to track. */
#define CMSG_CLIENTSMAX 1000
/** Expected maximum size of a message in bytes. */
#define CMSG_MESSAGE_SIZE 4096


static int counter = 1;
static int acknowledge = 0;


/* for c++ */
#ifdef __cplusplus
extern "C" {
#endif

/* prototypes */
static void *clientThread(void *arg);
static void  cleanUpHandler(void *arg);
static int   cMsgReadMessage(int connfd, char *buffer, cMsgMessage *msg);

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
      if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "cMsgClientListeningThread: cancelling a thread\n");
      }
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
  int             i, err, index, status;
  const int       on=1;
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
  
  /* increase concurrency for this thread for early Solaris */
  int  con;
  con = sun_getconcurrency();
  sun_setconcurrency(con + 1);

  /* release system resources when thread finishes */
  pthread_detach(pthread_self());

  addrlen  = sizeof(cliaddr);
  domainId = threadArg->domainId;
    
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
  
  /* BUG BUG: if thread is canceled, memory leak of threadArg */
  
  /* don't forget to free mem allocated for the argument passed to this routine */
  free(threadArg);
  
  /* on some operating systems (Linux) this call is necessary - calls cleanup handler */
  pthread_cleanup_pop(1);
  
  pthread_exit(NULL);
  return NULL;
}


/*-------------------------------------------------------------------*/


static void *clientThread(void *arg)
{
  int  inComing[2];
  int  err, ok, size, msgId, connfd, domainId, localCount=0;
  cMsgThreadInfo *info;
  cMsgClientThreadInfo *clientInfo;
  int  con, bufSize;
  char *buffer;

  /* msg rate measuring variables */
  /*
  int             dostring=1, count=0, i, delay=0, loops=10000, ignore=5;
  struct timespec t1, t2;
  double          freq, freqAvg=0., deltaT, totalT=0.;
  long long       totalC=0;
  */
  
  info       = (cMsgThreadInfo *) arg;
  domainId   = info->domainId;
  connfd     = info->connfd;
  clientInfo = info->clientInfo;
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
        perror("reading command");
      }
      /* if there's a timeout, try again */
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        /* test to see if someone wants to shutdown this thread */
        pthread_testcancel();
        goto retry;
      }
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "clientThread %d: error reading command\n", localCount);
        perror("reading command");
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
    }
        
    /* extract command */
    msgId = ntohl(inComing[1]);
    
    /* calculate rate */
    /*
    if (count == 0) {
      clock_gettime(CLOCK_REALTIME, &t1);
    }
    else if (count == loops-1) {
        clock_gettime(CLOCK_REALTIME, &t2);
        deltaT  = (double)(t2.tv_sec - t1.tv_sec) + 1.e-9*(t2.tv_nsec - t1.tv_nsec);
        totalT += deltaT;
        totalC += count;
        freq    = count/deltaT;
        freqAvg = (double)totalC/totalT;
        count = -1;
        printf("Listening thd count (%d) = %d, %9.1f Hz, %9.1f Hz Avg.\n", localCount, count, freq, freqAvg);
    }
    count++;
    */

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
          if ( cMsgReadMessage(connfd, buffer, message) != CMSG_OK) {
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
          err = cMsgRunCallbacks(domainId, message);
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
          if ( cMsgReadMessage(connfd, buffer, message) != CMSG_OK) {
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

      case CMSG_GET_RESPONSE_IS_NULL:
      {
          int senderToken;
          
          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "clientThread %d: subscribe is null response received\n", localCount);
          }
                    
          if (cMsgTcpRead(connfd, inComing, sizeof(inComing)) != sizeof(inComing)) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread %d: error reading command\n", localCount);
            }
            goto end;
          }

          senderToken = ntohl(inComing[0]);
          acknowledge = ntohl(inComing[1]);
          
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
          cMsgWakeGetWithNull(domainId, senderToken);
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

      case  CMSG_SHUTDOWN:
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
    if (clientInfo != NULL) {
      clientInfo->isUsed = 0;
    }

    fprintf(stderr, "clientThread %d: error, client's receiving connection to server broken\n", localCount);

    /* we are done with the socket */
    close(connfd);

    /* decrease concurrency as this thread disappears */
    con = sun_getconcurrency();
    sun_setconcurrency(con - 1);
  
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
static int cMsgReadMessage2(int connfd, char *buffer, cMsgMessage *msg) {

  long long llTime;
  int  lengths[7], inComing[18];
  char *tmp;
    
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
  llTime = (((long long) ntohl(inComing[4])) << 32) |
           (((long long) ntohl(inComing[5])) & 0x00000000FFFFFFFF);
  /* turn long long into struct timespec */
  msg->senderTime.tv_sec  =  llTime/1000;
  msg->senderTime.tv_nsec = (llTime%1000)*1000000;
  
  llTime = (((long long) ntohl(inComing[6])) << 32) |
           (((long long) ntohl(inComing[7])) & 0x00000000FFFFFFFF);
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
  acknowledge      = ntohl(inComing[17]); /* acknowledge receipt of message? (1-y,0-n) */
  
  /*--------------------*/
  /* read sender string */
  /*--------------------*/
  /* allocate memory for sender string */
  if ( (tmp = (char *) malloc((size_t) (lengths[0]+1))) == NULL) {
    return(CMSG_OUT_OF_MEMORY);    
  }
  
  /* read sender string into memory */
  if (cMsgTcpRead(connfd, tmp, lengths[0]) != lengths[0]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: error reading message 0\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  /* add null terminator to string */
  tmp[lengths[0]] = 0;
  /* store string in msg structure */
  msg->sender = tmp;
  /*printf("sender = %s\n", tmp);*/
      
  /*------------------------*/
  /* read senderHost string */
  /*------------------------*/
  if ( (tmp = (char *) malloc((size_t) (lengths[1]+1))) == NULL) {
    free((void *) msg->sender);
   
    return(CMSG_OUT_OF_MEMORY);    
  }
  
  if (cMsgTcpRead(connfd, tmp, lengths[1]) != lengths[1]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: error reading message 1\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  tmp[lengths[1]] = 0;
  msg->senderHost = tmp;
  /*printf("senderHost = %s\n", tmp);*/
  
  /*---------------------*/
  /* read subject string */
  /*---------------------*/
  if ( (tmp = (char *) malloc((size_t) (lengths[2]+1))) == NULL) {
    free((void *) msg->sender);
    free((void *) msg->senderHost);
    return(CMSG_OUT_OF_MEMORY);    
  }
  
  if (cMsgTcpRead(connfd, tmp, lengths[2]) != lengths[2]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: error reading message 2\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  tmp[lengths[2]] = 0;
  msg->subject = tmp;
  /*printf("subject = %s\n", tmp);*/
  
  /*------------------*/
  /* read type string */
  /*------------------*/
  if ( (tmp = (char *) malloc((size_t) (lengths[3]+1))) == NULL) {
    free((void *) msg->sender);
    free((void *) msg->senderHost);
    free((void *) msg->subject);
    return(CMSG_OUT_OF_MEMORY);    
  }
  
  if (cMsgTcpRead(connfd, tmp, lengths[3]) != lengths[3]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: error reading message 3\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  tmp[lengths[3]] = 0;
  msg->type = tmp;
  /*printf("type = %s\n", tmp);*/
  
  /*---------------------*/
  /* read creator string */
  /*---------------------*/
  if ( (tmp = (char *) malloc((size_t) (lengths[4]+1))) == NULL) {
    free((void *) msg->sender);
    free((void *) msg->senderHost);
    free((void *) msg->subject);
    free((void *) msg->type);
    return(CMSG_OUT_OF_MEMORY);    
  }
  
  if (cMsgTcpRead(connfd, tmp, lengths[4]) != lengths[4]) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: error reading message 4\n");
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  tmp[lengths[4]] = 0;
  msg->creator = tmp;
  /*printf("creator = %s\n", tmp);*/
    
  /*------------------*/
  /* read text string */
  /*------------------*/
  if (lengths[5] > 0) {
    if ( (tmp = (char *) malloc((size_t) (lengths[5]+1))) == NULL) {
      free((void *) msg->sender);
      free((void *) msg->senderHost);
      free((void *) msg->subject);
      free((void *) msg->type);
      free((void *) msg->creator);
      return(CMSG_OUT_OF_MEMORY);    
    }
  
    if (cMsgTcpRead(connfd, tmp, lengths[5]) != lengths[5]) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cMsgReadMessage: error reading message 5\n");
      }
      return(CMSG_NETWORK_ERROR);
    }
  
    tmp[lengths[5]] = 0;
    msg->text = tmp;
    /*printf("text = %s\n", tmp);*/
  }
  
  /*-----------------------------*/
  /* read binary into byte array */
  /*-----------------------------*/
  if (lengths[6] > 0) {
    
    if ( (tmp = (char *) malloc((size_t) (lengths[6]))) == NULL) {
      free((void *) msg->sender);
      free((void *) msg->senderHost);
      free((void *) msg->subject);
      free((void *) msg->type);
      free((void *) msg->creator);
      if (lengths[5] > 0) {
        free((void *) msg->text);
      }
      return(CMSG_OUT_OF_MEMORY);    
    }
    
    if (cMsgTcpRead(connfd, tmp, lengths[6]) != lengths[6]) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cMsgReadMessage: error reading message 3\n");
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

/*
 * This routine is called by a single thread spawned from the client's
 * listening thread. Since it's called serially, it can safely use
 * arrays declared at the top of the file.
 */
/** This routine reads a message sent from the server to the client. */
static int cMsgReadMessage(int connfd, char *buffer, cMsgMessage *msg) {

  long long llTime;
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
  llTime = (((long long) ntohl(inComing[4])) << 32) |
           (((long long) ntohl(inComing[5])) & 0x00000000FFFFFFFF);
  /* turn long long into struct timespec */
  msg->senderTime.tv_sec  =  llTime/1000;
  msg->senderTime.tv_nsec = (llTime%1000)*1000000;
  
  llTime = (((long long) ntohl(inComing[6])) << 32) |
           (((long long) ntohl(inComing[7])) & 0x00000000FFFFFFFF);
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
  acknowledge      = ntohl(inComing[17]); /* acknowledge receipt of message? (1-y,0-n) */
  
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
  if ( (tmp = (char *) malloc((size_t) (lengths[0]+1))) == NULL) {
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
  if ( (tmp = (char *) malloc((size_t) (lengths[1]+1))) == NULL) {
    free((void *) msg->sender);
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
  if ( (tmp = (char *) malloc((size_t) (lengths[2]+1))) == NULL) {
    free((void *) msg->sender);
    free((void *) msg->senderHost);
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
  if ( (tmp = (char *) malloc((size_t) (lengths[3]+1))) == NULL) {
    free((void *) msg->sender);
    free((void *) msg->senderHost);
    free((void *) msg->subject);
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
  if ( (tmp = (char *) malloc((size_t) (lengths[4]+1))) == NULL) {
    free((void *) msg->sender);
    free((void *) msg->senderHost);
    free((void *) msg->subject);
    free((void *) msg->type);
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
    if ( (tmp = (char *) malloc((size_t) (lengths[5]+1))) == NULL) {
      free((void *) msg->sender);
      free((void *) msg->senderHost);
      free((void *) msg->subject);
      free((void *) msg->type);
      free((void *) msg->creator);
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
    
    if ( (tmp = (char *) malloc((size_t) (lengths[6]))) == NULL) {
      free((void *) msg->sender);
      free((void *) msg->senderHost);
      free((void *) msg->subject);
      free((void *) msg->type);
      free((void *) msg->creator);
      if (lengths[5] > 0) {
        free((void *) msg->text);
      }
      return(CMSG_OUT_OF_MEMORY);    
    }
    
    if (cMsgTcpRead(connfd, tmp, lengths[6]) != lengths[6]) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cMsgReadMessage: error reading message 3\n");
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

/*
 * This routine is called by a single thread spawned from the client's
 * listening thread. Since it's called serially, it can safely use
 * arrays declared at the top of the file.
 */
/** This routine reads a message sent from the server to the client. */
static int cMsgReadMessageOrig(char *buffer, cMsgMessage *msg) {

  long long llTime;
  int *pint, lengths[7];
  char *pchar, *tmp;
    
  pint = (int *) buffer;
  
  /* swap to local endian */
  msg->version  = ntohl(*pint++);  /* major version of cMsg */
  /* second int is for future use */
  pint++;
  msg->userInt  = ntohl(*pint++);  /* user int */
  msg->info     = ntohl(*pint++);  /* get info */
  /*
   * Time arrives as the high 32 bits followed by the low 32 bits
   * of a 64 bit integer in units of milliseconds.
   */
  llTime = (((long long) ntohl(*pint++)) << 32) |
           (((long long) ntohl(*pint++)) & 0x00000000FFFFFFFF);
  /* turn long long into struct timespec */
  msg->senderTime.tv_sec  =  llTime/1000;
  msg->senderTime.tv_nsec = (llTime%1000)*1000000;
  
  llTime = (((long long) ntohl(*pint++)) << 32) |
           (((long long) ntohl(*pint++)) & 0x00000000FFFFFFFF);
  msg->userTime.tv_sec  =  llTime/1000;
  msg->userTime.tv_nsec = (llTime%1000)*1000000;
  
  msg->sysMsgId    = ntohl(*pint++); /* system msg id */
  msg->senderToken = ntohl(*pint++); /* sender token */
  lengths[0]       = ntohl(*pint++); /* sender length */
  lengths[1]       = ntohl(*pint++); /* senderHost length */
  lengths[2]       = ntohl(*pint++); /* subject length */
  lengths[3]       = ntohl(*pint++); /* type length */
  lengths[4]       = ntohl(*pint++); /* creator length */
  lengths[5]       = ntohl(*pint++); /* text length */
  lengths[6]       = ntohl(*pint++); /* binary length */
  acknowledge      = ntohl(*pint++); /* acknowledge receipt of message? (1-y,0-n) */
  
  pchar = (char *) pint;
  
  /*--------------------*/
  /* read sender string */
  /*--------------------*/
  /* allocate memory for sender string */
  if ( (tmp = (char *) malloc((size_t) (lengths[0]+1))) == NULL) {
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
  if ( (tmp = (char *) malloc((size_t) (lengths[1]+1))) == NULL) {
    free((void *) msg->sender);
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
  if ( (tmp = (char *) malloc((size_t) (lengths[2]+1))) == NULL) {
    free((void *) msg->sender);
    free((void *) msg->senderHost);
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
  if ( (tmp = (char *) malloc((size_t) (lengths[3]+1))) == NULL) {
    free((void *) msg->sender);
    free((void *) msg->senderHost);
    free((void *) msg->subject);
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
  if ( (tmp = (char *) malloc((size_t) (lengths[4]+1))) == NULL) {
    free((void *) msg->sender);
    free((void *) msg->senderHost);
    free((void *) msg->subject);
    free((void *) msg->type);
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
    if ( (tmp = (char *) malloc((size_t) (lengths[5]+1))) == NULL) {
      free((void *) msg->sender);
      free((void *) msg->senderHost);
      free((void *) msg->subject);
      free((void *) msg->type);
      free((void *) msg->creator);
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
    if ( (tmp = (char *) malloc((size_t) (lengths[6]))) == NULL) {
      free((void *) msg->sender);
      free((void *) msg->senderHost);
      free((void *) msg->subject);
      free((void *) msg->type);
      free((void *) msg->creator);
      if (lengths[5] > 0) {
        free((void *) msg->text);
      }
      return(CMSG_OUT_OF_MEMORY);    
    }
    memcpy(tmp, pchar, lengths[6]);
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





#ifdef __cplusplus
}
#endif


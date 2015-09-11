/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Author:  Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *      Routines for cMsg domain client to read from server.
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
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#include "cMsgNetwork.h"
#include "cMsgPrivate.h"
#include "cMsg.h"
#include "cMsgDomain.h"

/* defined in cMsgDomain.c */
extern void* connectPointers[];

/* prototypes */
static void  cleanUpHandler(void *arg);
static int   cMsgWakeGet(cMsgDomainInfo *domain, cMsgMessage_t *msg);
static int   cMsgWakeSyncSend(cMsgDomainInfo *domain, int response, int ssid);


/** Structure for freeing memory in cleanUpHandler. */
typedef struct freeMem_t {
    char *buffer;
} freeMem;


/*-------------------------------------------------------------------*/


/** This routine disables pthread cancellation. */
static void disableThreadCancellation() {
    int status, state;
    status = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &state);
    if (status != 0) {
        cmsg_err_abort(status, "Disabling listening thread cancelability");
    }
/*printf("CANCELABILITY OFF\n");*/
}

/** This routine enables pthread cancellation. */
static void enableThreadCancellation() {
    int status, state;
    /* enable pthread cancellation */
    status = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);
    if (status != 0) {
        cmsg_err_abort(status, "Enabling listening thread cancelability");
    }
/*printf("CANCELABILITY ON\n");*/
}


/*-------------------------------------------------------------------*
 * cMsgClientListeningThread needs a pthread cancellation cleanup handler.
 * This handler will be called when the cMsgClientListeningThread is
 * cancelled. It's task is to free memory allocated for the communication
 * buffer.
 *-------------------------------------------------------------------*/
static void cleanUpHandler(void *arg) {
  freeMem *pMem = (freeMem *)arg;
  /*
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cleanUpHandler: in\n");
  }
  */
  /* decrease concurrency as this thread disappears */
  sun_setconcurrency(sun_getconcurrency() - 1);
  
  /* close socket */
  /* close(pMem->fd); */ /* I don't think this is necessary, Carl 8/11/08 */

  /* release memory */
  if (pMem == NULL) return;
  if (pMem->buffer != NULL) free(pMem->buffer);
  free(pMem);
}


/*-------------------------------------------------------------------*
 * cMsgClientListeningThread is a message receiving thread used by a
 * cMsg client to read input.
 *-------------------------------------------------------------------*/
void *cMsgClientListeningThread(void *arg)
{
  intptr_t index;
  void *domainId;
  int  err, size, msgId, connfd;
  int  inComing[2], status, state;
  size_t bufSize;
  char *buffer;
  freeMem *pfreeMem=NULL;
  cMsgDomainInfo *domain;
  struct timespec wait = {0, 20000000}; /* 0.02 sec */
  
  domainId = arg;
  index = (intptr_t) domainId;
  if (index < 0 || index > CMSG_CONNECT_PTRS_ARRAY_SIZE-1) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
          fprintf(stderr, "cMsgClientListeningThread: bad value for domain, ending thread (1)\n");
      }
      pthread_exit(NULL);
  }
  
  cMsgMemoryMutexLock();
  domain = connectPointers[index];
  /* If bad index or disconnect called immediately after connect, bail out. */
  if (domain == NULL || domain->disconnectCalled) {
      cMsgMemoryMutexUnlock();
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
          fprintf(stderr, "cMsgClientListeningThread: bad value for domain, ending thread (2)\n");
      }
      pthread_exit(NULL);
  }
  cMsgMemoryMutexUnlock();

  /* increase concurrency for this thread */
  sun_setconcurrency(sun_getconcurrency() + 1);
  
  /* release system resources when thread finishes */
  /* pthread_detach(pthread_self()); */

  /* disable pthread cancellation until pointer is set and handler is installed */  
  disableThreadCancellation();
  
  /* Create pointer to malloced mem which will hold 2 pointers. */
  pfreeMem = (freeMem *) malloc(sizeof(freeMem));
  if (pfreeMem == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "clientThread: cannot allocate memory\n");
      }
      exit(1);
  }

  /* Create buffer which must be freed upon thread cancellation. */   
  bufSize = 65536;
  buffer  = (char *) calloc(1, bufSize);
  if (buffer == NULL) {
      if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
        fprintf(stderr, "clientThread: cannot allocate memory\n");
      }
      exit(1);
  }
  
  /* Install a cleanup handler for this thread's cancellation. 
   * Give it a pointer which points to the memory which must
   * be freed upon cancelling this thread. */
  /* pfreeMem->fd = connfd; */
  pfreeMem->buffer = buffer;
  pthread_cleanup_push(cleanUpHandler, (void *)pfreeMem);
  
  /* enable pthread cancellation at deferred points like pthread_testcancel */  
  enableThreadCancellation();
  
  /*--------------------------------------*/
  /* wait for and process client requests */
  /*--------------------------------------*/
   
  /* Command loop */
  while (1) {

    /*
     * First, read the incoming message size. This read is also a pthread
     * cancellation point. pthread_cancel for this thread is called from only
     * 1) connectDirect and 2) connectionShutdown
     */
     
    /* Recheck value of socket here as it may change
     * if connection to server dies and is reestablished. */
    connfd = domain->sendSocket;

    /* pthread cancellation point */
    if (cMsgNetTcpRead(connfd, inComing, sizeof(inComing)) != sizeof(inComing)) {
      /* We're in the middle of failing over, wait
       * so that we don't chew up the CPU. */
      nanosleep(&wait, NULL);
      continue;
    }
    
    size = ntohl(inComing[0]);
    
    /* make sure we have big enough buffer */
    if (size > bufSize) {
      
      /* disable pthread cancellation until pointer is set */      
      disableThreadCancellation();
      
      /* free previously allocated memory */
      free((void *) buffer);

      /* allocate more memory to accomodate larger msg */
      bufSize = size + 1000;
      buffer  = (char *) calloc(1, bufSize);
      pfreeMem->buffer = buffer;
      if (buffer == NULL) {
        if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
          fprintf(stderr, "clientThread: cannot allocate %d amount of memory\n", bufSize);
          fprintf(stderr, "clientThread: int1,2 = 0x%x, 0x%x\n", size, ntohl(inComing[1]));
        }
        exit(1);
      }
      
      /* re-enable pthread cancellation */
      enableThreadCancellation();
    }
        
    /* extract command */
    msgId = ntohl(inComing[1]);
    
    switch (msgId) {

      case CMSG_SUBSCRIBE_RESPONSE:
      {
          void *msg;
          cMsgMessage_t *message;
          
          /* disable pthread cancellation until message memory is released or
           * we'll get a mem leak */
          disableThreadCancellation();

          msg = cMsgCreateMessage();
          message = (cMsgMessage_t *) msg;
          if (message == NULL) {
            if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
              fprintf(stderr, "clientThread: cannot allocate memory\n");
            }
            exit(1);
          }
          
          /* read the message */
          if ( cMsgReadMessage(connfd, buffer, message) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread: error reading message\n");
            }
            cMsgFreeMessage(&msg);
            enableThreadCancellation();
            continue;
          }

          /* fill in known message fields */
          message->next = NULL;
          clock_gettime(CLOCK_REALTIME, &message->receiverTime);
          message->domain = (char *) strdup("cmsg");
          if (domain->name != NULL) {
              message->receiver = (char *) strdup(domain->name);
          }
          if (domain->myHost != NULL) {
              message->receiverHost = (char *) strdup(domain->myHost);
          }
          
          /* run callbacks for this message */
          err = cMsgRunCallbacks(domain, msg);
          if (err != CMSG_OK) {
            if (err == CMSG_OUT_OF_MEMORY) {
              if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                fprintf(stderr, "clientThread: cannot allocate memory\n");
              }
            }
            else if (err == CMSG_LIMIT_EXCEEDED) {
              if (cMsgDebug >= CMSG_DEBUG_ERROR) {
                fprintf(stderr, "clientThread: too many messages cued up\n");
              }
            }
            cMsgFreeMessage(&msg);
            enableThreadCancellation();
            continue;
          }

          /* re-enable pthread cancellation */
          enableThreadCancellation();
      }
      break;


      case CMSG_GET_RESPONSE:
      {
          void *msg;
          cMsgMessage_t *message;
          
          /* disable pthread cancellation until message memory is released or
           * we'll get a mem leak */
          disableThreadCancellation();
          
          msg = cMsgCreateMessage();
          message = (cMsgMessage_t *) msg;
          if (message == NULL) {
            if (cMsgDebug >= CMSG_DEBUG_SEVERE) {
              fprintf(stderr, "clientThread: cannot allocate memory\n");
            }
            exit(1);
          }

          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "clientThread: subscribe response received\n");
          }
          
          /* read the message */
          if ( cMsgReadMessage(connfd, buffer, message) != CMSG_OK) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread: error reading message\n");
            }
            cMsgFreeMessage(&msg);
            enableThreadCancellation();
            continue;
          }
          
          /* fill in known message fields */
          message->next = NULL;
          clock_gettime(CLOCK_REALTIME, &message->receiverTime);
          message->domain  = (char *) strdup("cmsg");
          if (domain->name != NULL) {
              message->receiver = (char *) strdup(domain->name);
          }
          if (domain->myHost != NULL) {
              message->receiverHost = (char *) strdup(domain->myHost);
          }
         
          /* wakeup get caller for this message */
          cMsgWakeGet(domain, message);
          
          /* re-enable pthread cancellation */
          enableThreadCancellation();
      }
      break;


      case  CMSG_SHUTDOWN_CLIENTS:
      {
          /* Disable pthread cancellation for safety's sake --
           * never know what is in the shutdown handler. */
          disableThreadCancellation();
          
          if (domain->shutdownHandler != NULL) {
              domain->shutdownHandler(domain->shutdownUserArg);
          }
          
          /* re-enable pthread cancellation */
          enableThreadCancellation();

          if (cMsgDebug >= CMSG_DEBUG_INFO) {
              fprintf(stderr, "clientThread: told to shutdown\n");
          }
      }
      break;
      
      
      /* receiving a couple ints for syncSend */      
      case  CMSG_SYNC_SEND_RESPONSE:
      {
          int response, ssid;
              
          /* this is a pthread cancellation point */
          if (cMsgNetTcpRead(connfd, inComing, sizeof(inComing)) != sizeof(inComing)) {
            if (cMsgDebug >= CMSG_DEBUG_ERROR) {
              fprintf(stderr, "clientThread: error reading sync send response\n");
            }
            continue;
          }
          response = ntohl(inComing[0]);
          ssid = ntohl(inComing[1]);
          if (cMsgDebug >= CMSG_DEBUG_INFO) {
              fprintf(stderr, "clientThread: got syncSend response from server\n");
          }
          
          /* Disable pthread cancellation until message delivered.
           * This also prevents the thread cancelling with a mutex
           * being held. */
          disableThreadCancellation();
          
          /* notify waiter that sync send response is here */
          cMsgWakeSyncSend(domain, response, ssid);

          /* re-enable pthread cancellation */
          enableThreadCancellation();
      }
      break;


      default:
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "clientThread: given unknown message (%d)\n", msgId);
        }
     
    } /* switch */

  } /* while(1) - command loop */

    
  /* on some operating systems (Linux) this call is necessary - calls cleanup handler */
  pthread_cleanup_pop(1);

  pthread_exit(NULL);
  return NULL;
}



/*-------------------------------------------------------------------*/

/**
 * This routine reads a message sent from the server to the client.
 * This routine is called by a single thread spawned from the client's
 * listening thread and is called serially.
 *
 * @param connfd socket file descriptor
 * @param buffer char array into which text is read from socket
 * @param msg pointer to message structure into which the read values are put
 *
 * @return CMSG_OK if OK
 * @return CMSG_NETWORK_ERROR  if error reading message from TCP buffer
 * @return CMSG_OUT_OF_MEMORY  if no memory available
 * @return CMSG_ALREADY_EXISTS if payload text contains name that is being used already
 * @return CMSG_BAD_FORMAT     if payload text is in the wrong format or contains values
 *                             that don't make sense
 */
int cMsgReadMessage(int connfd, char *buffer, cMsgMessage_t *msg) {

  uint64_t llTime;
  int  i, err, hasPayload, stringLen, lengths[7], inComing[17];
  char *pchar, *tmp;

  if (cMsgNetTcpRead(connfd, inComing, sizeof(inComing)) != sizeof(inComing)) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgReadMessage: error reading message 1\n");
    }
    return(CMSG_NETWORK_ERROR);
  }

  /* swap to local endian */
  msg->version  = ntohl(inComing[0]); /* major version of cMsg */
                                      /* second int is for future use */
  msg->userInt = ntohl(inComing[2]);  /* user int */
  msg->info    = ntohl(inComing[3]);  /* get info */
  /* mark message as having been sent over the wire and as having an expanded payload */
  msg->info   |= CMSG_WAS_SENT | CMSG_EXPANDED_PAYLOAD;
  cMsgHasPayload(msg, &hasPayload);   /* does message have compound payload? */

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
  lengths[4]       = ntohl(inComing[14]); /* payloadText length */
  lengths[5]       = ntohl(inComing[15]); /* text length */
  lengths[6]       = ntohl(inComing[16]); /* binary length */

  /* length of strings to read in */
  stringLen = lengths[0] + lengths[1] + lengths[2] +
              lengths[3] + lengths[4] + lengths[5];

  if (cMsgNetTcpRead(connfd, buffer, stringLen) != stringLen) {
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
  if (lengths[0] > 0) {
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
    /* printf("sender = %s\n", tmp); */
  }
  else {
    msg->sender = NULL;
  }

  /*------------------------*/
  /* read senderHost string */
  /*------------------------*/
  if (lengths[1] > 0) {
    if ( (tmp = (char *) malloc(lengths[1]+1)) == NULL) {
      if (msg->sender != NULL) free((void *) msg->sender);
      msg->sender = NULL;
      return(CMSG_OUT_OF_MEMORY);
    }
    memcpy(tmp, pchar, lengths[1]);
    tmp[lengths[1]] = 0;
    msg->senderHost = tmp;
    pchar += lengths[1];
    /* printf("senderHost = %s\n", tmp); */
  }
  else {
    msg->senderHost = NULL;
  }

  /*---------------------*/
  /* read subject string */
  /*---------------------*/
  if (lengths[2] > 0) {
    if ( (tmp = (char *) malloc(lengths[2]+1)) == NULL) {
      if (msg->sender != NULL)     free((void *) msg->sender);
      if (msg->senderHost != NULL) free((void *) msg->senderHost);
      msg->sender     = NULL;
      msg->senderHost = NULL;
      return(CMSG_OUT_OF_MEMORY);
    }
    memcpy(tmp, pchar, lengths[2]);
    tmp[lengths[2]] = 0;
    msg->subject = tmp;
    pchar += lengths[2];
/*
printf("*****   got subject = %s, len = %d\n", tmp, lengths[2]);
if (strcmp(tmp, " ") == 0) printf("subject is one space\n");
if (strcmp(tmp, "") == 0) printf("subject is blank\n");
*/
  }
  else {
    msg->subject = NULL;
/*printf("*****   got subject length %d\n", lengths[2]);*/
  }

  /*------------------*/
  /* read type string */
  /*------------------*/
  if (lengths[3] > 0) {
    if ( (tmp = (char *) malloc(lengths[3]+1)) == NULL) {
      if (msg->sender != NULL)     free((void *) msg->sender);
      if (msg->senderHost != NULL) free((void *) msg->senderHost);
      if (msg->subject != NULL)    free((void *) msg->subject);
      msg->sender     = NULL;
      msg->senderHost = NULL;
      msg->subject    = NULL;
      return(CMSG_OUT_OF_MEMORY);
    }
    memcpy(tmp, pchar, lengths[3]);
    tmp[lengths[3]] = 0;
    msg->type = tmp;
    pchar += lengths[3];
/*
printf("*****   got type = %s, len = %d\n", tmp, lengths[3]);
if (strcmp(tmp, " ") == 0) printf("type is one space\n");
if (strcmp(tmp, "") == 0) printf("type is blank\n");
*/
  }
  else {
    msg->type = NULL;
/*printf("*****   got type length = %d\n", lengths[3]);*/
  }

  /*-------------------------*/
  /* read payloadText string */
  /*-------------------------*/
  if (lengths[4] > 0) {
      //char *pt = pchar;
      err = cMsgPayloadSetAllFieldsFromText(msg, pchar);
      if (err != CMSG_OK) {
          if (msg->sender != NULL)     free((void *) msg->sender);
          if (msg->senderHost != NULL) free((void *) msg->senderHost);
          if (msg->subject != NULL)    free((void *) msg->subject);
          if (msg->type != NULL)       free((void *) msg->type);
          msg->sender     = NULL;
          msg->senderHost = NULL;
          msg->subject    = NULL;
          msg->type       = NULL;
          return(err);
      }
      pchar += lengths[4];
      //pchar = '\0';
      //printf("*****   Payload text = \n%s", pt);
      //cMsgGetPayloadText(msg, &pt);
      //printf("*****   Payload text = \n%s", pt);
  }
  else {
      msg->payload      = NULL;
      msg->payloadText  = NULL;
      msg->payloadCount = 0;
  }

  /*-------------------------------*/
  /* read text string if it exists */
  /*-------------------------------*/
  if (lengths[5] > 0) {
      if ( (tmp = (char *) malloc(lengths[5]+1)) == NULL) {
        if (msg->sender != NULL)     free((void *) msg->sender);
        if (msg->senderHost != NULL) free((void *) msg->senderHost);
        if (msg->subject != NULL)    free((void *) msg->subject);
        if (msg->type != NULL)       free((void *) msg->type);
        if (msg->payload != NULL)    cMsgPayloadReset((void *)msg);
        msg->sender     = NULL;
        msg->senderHost = NULL;
        msg->subject    = NULL;
        msg->type       = NULL;
        return(CMSG_OUT_OF_MEMORY);
      }
      memcpy(tmp, pchar, lengths[5]);
      tmp[lengths[5]] = 0;
      msg->text = tmp;
      /* printf("text = %s\n", tmp); */
      pchar += lengths[5];
  }
  else {
    msg->text = NULL;
  }

  /*-----------------------------*/
  /* read binary into byte array */
  /*-----------------------------*/
  if (lengths[6] > 0) {

    if ( (tmp = (char *) malloc(lengths[6])) == NULL) {
      if (msg->sender != NULL)     free((void *) msg->sender);
      if (msg->senderHost != NULL) free((void *) msg->senderHost);
      if (msg->subject != NULL)    free((void *) msg->subject);
      if (msg->type != NULL)       free((void *) msg->type);
      if (msg->payload != NULL)    cMsgPayloadReset((void *)msg);
      if (msg->text != NULL)       free((void *) msg->text);
      msg->sender     = NULL;
      msg->senderHost = NULL;
      msg->subject    = NULL;
      msg->type       = NULL;
      msg->text       = NULL;
      return(CMSG_OUT_OF_MEMORY);
    }

    if (cMsgNetTcpRead(connfd, tmp, lengths[6]) != lengths[6]) {
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cMsgReadMessage: error reading message 3\n");
      }
      if (msg->sender != NULL)     free((void *) msg->sender);
      if (msg->senderHost != NULL) free((void *) msg->senderHost);
      if (msg->subject != NULL)    free((void *) msg->subject);
      if (msg->type != NULL)       free((void *) msg->type);
      if (msg->payload != NULL)    cMsgPayloadReset((void *)msg);
      if (msg->text != NULL)       free((void *) msg->text);
      msg->sender     = NULL;
      msg->senderHost = NULL;
      msg->subject    = NULL;
      msg->type       = NULL;
      msg->text       = NULL;
      return(CMSG_NETWORK_ERROR);
    }

/*printf("cMsgReadMessage(): read %d bytes in msg\n", (4*17 + stringLen + lengths[6]));*/

    msg->byteArray       = tmp;
    msg->byteArrayOffset = 0;
    msg->byteArrayLength = lengths[6];
    msg->byteArrayLengthFull = lengths[6];
    msg->bits |= CMSG_BYTE_ARRAY_IS_COPIED; /* byte array is COPIED */
    /*
    for (i=0; i<lengths[6]; i++) {
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

  int status;
  getInfo *info=NULL;
  char *idString;
  void *p; /* avoid compiler warning */

  /* find the right sendAndGet */
  idString = cMsgIntChars(msg->senderToken);
  if (idString == NULL) {
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* if id not in hashTable, return */
  cMsgSendAndGetMutexLock(domain);
  if (!hashRemove(&domain->sendAndGetTable, idString, &p)) {
    cMsgSendAndGetMutexUnlock(domain);
    free(idString);
    return(CMSG_OK);
  }
  cMsgSendAndGetMutexUnlock(domain);
  info = (getInfo *)p;
  free(idString);

  if (info->id == msg->senderToken) {
    /* pass msg to "get" */
    info->msg = msg;
    info->msgIn = 1;

    /* wakeup "get" */
    status = pthread_cond_signal(&info->cond);
    if (status != 0) {
      cmsg_err_abort(status, "Failed get condition signal");
    }
  }
    
  return (CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine wakes up the appropriate syncSend
 * when a message arrives from the server. 
 *
 * @param domain pointer to domain info
 * @param response returned int from subdomain handler
 * @param ssid syncSend id
 *
 * @return CMSG_OK if OK
 * @return CMSG_OUT_OF_MEMORY if no memory available
 */
static int cMsgWakeSyncSend(cMsgDomainInfo *domain, int response, int ssid) {

  int status;
  getInfo *info=NULL;
  char *idString;
  void *p; /* avoid compiler warning */
   
  /* find the right syncSend */
  idString = cMsgIntChars(ssid);
  if (idString == NULL) {
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* if id not in hashTable, return */
  cMsgSyncSendMutexLock(domain);
  if (!hashRemove(&domain->syncSendTable, idString, &p)) {
    cMsgSyncSendMutexUnlock(domain);
    free(idString);
    return(CMSG_OK);
  }
  cMsgSyncSendMutexUnlock(domain);
  info = (getInfo *)p;
  free(idString);
  
  if (info->id == ssid) {
      info->response = response;
      info->msgIn = 1;

      /* wakeup "syncSend" */      
      status = pthread_cond_signal(&info->cond);
      if (status != 0) {
        cmsg_err_abort(status, "Failed syncSend condition signal");
      }
  }
    
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine runs all the appropriate subscribe and subscribeAndGet
 * callbacks when a message arrives from the server.
 * 
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if disconnect was called while message queue full and blocked
 * @returns CMSG_OUT_OF_MEMORY if all available memory has been used
 */
int cMsgRunCallbacks(cMsgDomainInfo *domain, void *msgArg) {

  int i, k, id, status, goToNextCallback, tblSize=0;
  subscribeCbInfo *cb;
  getInfo *info;
  subInfo *sub;
  cMsgMessage_t *message, *oldHead, *msg = (cMsgMessage_t *)msgArg;
  struct timespec wait;
  hashNode *entries = NULL;
  char *idString;
  void *p;

  /* wait 10 sec between warning messages for a full cue */
  struct timespec timeout = {12, 0};

  /* for each subscribeAndGet ... */
  cMsgSubAndGetMutexLock(domain); /* serialize access to subAndGet hash table */
  
  if (!hashGetAll(&domain->subAndGetTable, &entries, &tblSize)) {
    cMsgSubAndGetMutexUnlock(domain);
    return(CMSG_OUT_OF_MEMORY); /* ... or table is NULL pointer */
  }
  
  if (entries != NULL) {
    for (i=0; i<tblSize; i++) {
      info = (getInfo *)entries[i].data;
      id = info->id; /* store in local variable */

      /* if the subject & type's match, wakeup the "subscribeAndGet */
      if ( cMsgSubAndGetMatches(info, msg->subject, msg->type) ) {
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
          cMsgSubAndGetMutexUnlock(domain);
          return(CMSG_OUT_OF_MEMORY);
        }

        info->msg = message;
        info->msgIn = 1;

        /* wakeup "get" */
        status = pthread_cond_signal(&info->cond);
        if (status != 0) {
          cmsg_err_abort(status, "Failed get condition signal");
        }

        /** !!! NOTE !!! as soon as the subAndGet is woken up, "info"
         * is messed with and eventually freed, so don't use it beyond
         * this point
         */

        /* remove this entry from the hashTable */
        idString = cMsgIntChars(id);
        if (idString == NULL) {
          cMsgSubAndGetMutexUnlock(domain);
          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            fprintf(stderr, "cMsgRunCallbacks: out of memory\n");
          }
          return(CMSG_OUT_OF_MEMORY);
        }
        hashRemove(&domain->subAndGetTable, idString, NULL);
        free(idString);
      }
    }
    free(entries);
  }
  cMsgSubAndGetMutexUnlock(domain);

  /* callbacks have been stopped */
  if (domain->receiveState == 0) {
    if (cMsgDebug >= CMSG_DEBUG_INFO) {
      fprintf(stderr, "cMsgRunCallbacks: all callbacks have been stopped\n");
    }
    cMsgFreeMessage(&msgArg);
    return (CMSG_OK);
  }
   
  /*
   * Don't want subscriptions added or removed while iterating through them.
   * WHILE THE FOLLOWING MUTEX IS HELD, NO SUBSCRIBES OR UNSUBSCRIBES CAN BE DONE
   * AND NO DISCONNECTS CAN BE COMPLETED. This mutex is actually released when
   * a callback cue is full, allowing unsubscribes and disconnects to empty
   * the cue.
   * Also, the server update thread only iterates/reads subscription info.
   * This routine iterates through the subscriptions but also changes 2 hash tables
   * which are part of the subscription. Fortunately, this is the only place those
   * hash tables are modified. Thus both the server update thread and the pend thread
   * (which calls this routine) can simultaneously access the subscriptions with a
   * read lock. This allows us to avoid the problem of not being able to send our
   * keep alives to the server if a blocking callback's queue should fill up.
   *
   * NOTE: If a blocking callback's queue should fill up, no subscribes, unsubscribes
   * can be done and disconnects will block part way through when attempting to
   * remove all subscriptions.
   */
  cMsgSubscribeReadLock(domain);
/*  printf("in runCallbacks\n");*/
  disableThreadCancellation();

  /* get client subscriptions */
  hashGetAll(&domain->subscribeTable, &entries, &tblSize);
  /* if there are subscriptions ... */
  if (entries != NULL) {

    /* for each client subscription ... */
    for (i=0; i<tblSize; i++) {

      sub = (subInfo *)entries[i].data;

      /* if the subject & type's match, run callbacks */
      if ( cMsgSubscriptionMatches(sub, msg->subject, msg->type) ) {
        /*
        printf("cMsgRunCallbacks: MATCHES:\n");
        printf("                  SUBJECT = msg (%s), subscription (%s)\n",
        msg->subject, sub->subject);
        printf("                  TYPE    = msg (%s), subscription (%s)\n",
        msg->type, sub->type);
        */
        /* go thru callback list */
        cb = sub->callbacks;
        while (cb != NULL) {

          /* copy message so each callback has its own copy */
          message = (cMsgMessage_t *) cMsgCopyMessage((void *)msg);
          if (message == NULL) {
            cMsgSubscribeReadUnlock(domain);
            if (cMsgDebug >= CMSG_DEBUG_INFO) {
              fprintf(stderr, "cMsgRunCallbacks: out of memory\n");
            }
            return(CMSG_OUT_OF_MEMORY);
          }

          /* lock mutex before messing with callback stuff */
          /*fprintf(stderr, "cMsgRunCallbacks: will grab mutex, %p\n", &cb->mutex);*/
          cMsgMutexLock(&cb->mutex);
          /*fprintf(stderr, "cMsgRunCallbacks: grabbed mutex\n");*/

          /* check to see if there are too many messages in the cue */
          if (cb->messages >= cb->config.maxCueSize) {

            /* if we may skip messages, dump oldest */
            if (cb->config.maySkip) {
/* fprintf(stderr, "cMsgRunCallbacks: cue full, skipping\n"); */
              for (k=0; k < cb->config.skipSize; k++) {
                oldHead = cb->head;
                cb->head = cb->head->next;
                p = (void *)oldHead; /* get rid of compiler warnings */
                cMsgFreeMessage(&p);
                cb->messages--;
                cb->fullQ = 0;
                if (cb->head == NULL) break;
              }

              if (cMsgDebug >= CMSG_DEBUG_INFO) {
                fprintf(stderr, "cMsgRunCallbacks: skipped %d messages\n", (k+1));
              }
            }
            /* if we may NOT skip messages, wait for messages to be pulled off the list */
            else {
              goToNextCallback = 0;

              while (cb->messages >= cb->config.maxCueSize) {
                /* Wait here until signaled - meaning message taken off cue
                 * by callback worker thread.
                 *
                 * NOTE: The pend thread (which calls this function) has disabled
                 * thread cancellation so we cannot be pthread_cancelled.
                 */
                  /*cb->fullQ = 1;*/
                cMsgGetAbsoluteTime(&timeout, &wait);
/*fprintf(stderr, "cMsgRunCallbacks: cue full, start waiting, will UNLOCK cb mutex\n"); */
/*fprintf(stderr, "cMsgRunCallbacks: cue full (%d, %p), waiting\n", cb->messages, cb);*/
                status = pthread_cond_timedwait(&cb->takeFromQ, &cb->mutex, &wait);
/*fprintf(stderr, "cMsgRunCallbacks: done waiting (%p), cb mutex LOCKED\n", cb);*/
                /* if the wait timed out ... */
                if (status == ETIMEDOUT) {
                  if (cMsgDebug >= CMSG_DEBUG_WARN) {
                      fprintf(stderr, "cMsgRunCallbacks: cannot place incoming message on full queue, wait 10 seconds\n");
                  }
                }
                /* else if error */
                else if (status != 0) {
                  cmsg_err_abort(status, "Failed callback cond wait");
                }
                else if (domain->disconnectCalled) {
                    fprintf(stderr, "cMsgRunCallbacks: disconnect called so return\n");
                    cMsgMutexUnlock(&cb->mutex);
                    free(entries);
                    cMsgFreeMessage((void **)&message);
                    cMsgSubscribeReadUnlock(domain);
                    return(CMSG_ERROR);
                }
                /* else woken up 'cause msg taken off cue */
                else if (cb->messages < cb->config.maxCueSize) {
                    /*cb->fullQ = 0;*/
                  break;
                }
              } /* while (cb->messages >= cb->config.maxCueSize) */
              
/* fprintf(stderr, "cMsgRunCallbacks: cue was full, wokenup, there's room now!\n"); */
              if (goToNextCallback) {
                  cMsgMutexUnlock(&cb->mutex);
                  /* next callback */
                  cb = cb->next;
                  continue;
              }
            } /* may not skip messages */
          } /* if too many messages in cue */

          if (cMsgDebug >= CMSG_DEBUG_INFO) {
            if (cb->messages !=0 && cb->messages%1000 == 0)
              fprintf(stderr, "           msgs = %d\n", cb->messages);
          }

          /*
           * Add this message to linked list for this callback.
           * It will now be the responsibility of message consumer
           * to free the msg allocated here.
           */

          /* if there are no messages ... */
          if (cb->head == NULL) {
            cb->head = message;
            cb->tail = message;
          }
          /* else put message after the tail */
          else {
            cb->tail->next = message;
            cb->tail = message;
          }

          cb->messages++;
          if (cb->messages >= cb->config.maxCueSize) {
              cb->fullQ = 1;
          }
/*printf("cMsgRunCallbacks: increase cue size = %d\n", cb->messages);*/
          message->next = NULL;

          /* If there is a possibility that the callback thread will need to
           * start another thread to handle more msgs, then tell it to check. */
          if (!cb->config.mustSerialize) {
              status = pthread_cond_signal(&cb->checkQ);
              if (status != 0) {
                  cmsg_err_abort(status, "Failed callback condition signal");
              }
          }

          /* wakeup one of the worker threads */
          status = pthread_cond_signal(&cb->addToQ);
          if (status != 0) {
            cmsg_err_abort(status, "Failed callback condition signal");
          }
          
          /* unlock mutex */
          /*printf("cMsgRunCallbacks: will UNLOCK mutex\n"); */
          cMsgMutexUnlock(&cb->mutex);
          /* printf("cMsgRunCallbacks: mutex is UNLOCKED, msg put on cue, broadcast to callback thd\n"); */          

          /* go to the next callback */
          cb = cb->next;

        } /* for each callback */
        
      } /* if matching subscription */      
    } /* for each subscription */
    
    free(entries);
  } /* if there are subscriptions */
    
  cMsgSubscribeReadUnlock(domain);
/*printf("done runCallbacks\n");*/

  /* Need to free up msg allocated by client's listening thread */
  cMsgFreeMessage((void **)&msgArg);

  return (CMSG_OK);
}

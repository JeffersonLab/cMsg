/*----------------------------------------------------------------------------*
 *                                                                            *
 *  Copyright (c) 2006        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 21-Apr-2006, Jefferson Lab                                   *
 *                                                                            *
 *    Authors: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 *
 * Description:
 *
 *  Contains functions which help implement the cMsg domain but are also
 *  used in implementing the rc domain.  
 *
 *----------------------------------------------------------------------------*/

/**
 * @file
 * This file contains the functions useful to both the cMsg and rc domain
 * implementations of the cMsg user API.
 */  
 
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#include "errors.h"
#include "cMsgPrivate.h"
#include "cMsgBase.h"
#include "cMsgNetwork.h"
#include "rwlock.h"
#include "cMsgDomain.h"

#ifdef VXWORKS
#include <vxWorks.h>
#endif

/** Excluded characters from subject, type, and description strings. */
static const char *excludedChars = "`\'\"";

/*-------------------------------------------------------------------*/


/** This routine locks the given pthread mutex. */
void mutexLock(pthread_mutex_t *mutex) {

  int status = pthread_mutex_lock(mutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/** This routine unlocks the given pthread mutex. */
void mutexUnlock(pthread_mutex_t *mutex) {

  int status = pthread_mutex_unlock(mutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the read lock used to allow simultaneous
 * execution of cmsgd_send, cmsgd_syncSend, cmsgd_subscribe, cmsgd_unsubscribe,
 * cmsgd_sendAndGet, and cmsgd_subscribeAndGet, but NOT allow simultaneous
 * execution of those routines with cmsgd_disconnect.
 */
void connectReadLock(cMsgDomainInfo *domain) {

  int status = rwl_readlock(&domain->connectLock);
  if (status != 0) {
    err_abort(status, "Failed read lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the read lock used to allow simultaneous
 * execution of cmsgd_send, cmsgd_syncSend, cmsgd_subscribe, cmsgd_unsubscribe,
 * cmsgd_sendAndGet, and cmsgd_subscribeAndGet, but NOT allow simultaneous
 * execution of those routines with cmsgd_disconnect.
 */
void connectReadUnlock(cMsgDomainInfo *domain) {

  int status = rwl_readunlock(&domain->connectLock);
  if (status != 0) {
    err_abort(status, "Failed read unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the write lock used to allow simultaneous
 * execution of cmsgd_send, cmsgd_syncSend, cmsgd_subscribe, cmsgd_unsubscribe,
 * cmsgd_sendAndGet, and cmsgd_subscribeAndGet, but NOT allow simultaneous
 * execution of those routines with cmsgd_disconnect.
 */
void connectWriteLock(cMsgDomainInfo *domain) {

  int status = rwl_writelock(&domain->connectLock);
  if (status != 0) {
    err_abort(status, "Failed read lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the write lock used to allow simultaneous
 * execution of cmsgd_send, cmsgd_syncSend, cmsgd_subscribe, cmsgd_unsubscribe,
 * cmsgd_sendAndGet, and cmsgd_subscribeAndGet, but NOT allow simultaneous
 * execution of those routines with cmsgd_disconnect.
 */
void connectWriteUnlock(cMsgDomainInfo *domain) {

  int status = rwl_writeunlock(&domain->connectLock);
  if (status != 0) {
    err_abort(status, "Failed read unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the pthread mutex used to make network
 * communication thread-safe.
 */
void socketMutexLock(cMsgDomainInfo *domain) {

  int status = pthread_mutex_lock(&domain->socketMutex);
  if (status != 0) {
    err_abort(status, "Failed socket mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the pthread mutex used to make network
 * communication thread-safe.
 */
void socketMutexUnlock(cMsgDomainInfo *domain) {

  int status = pthread_mutex_unlock(&domain->socketMutex);
  if (status != 0) {
    err_abort(status, "Failed socket mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/** This routine locks the pthread mutex used to serialize cmsgd_syncSend calls. */
void syncSendMutexLock(cMsgDomainInfo *domain) {

  int status = pthread_mutex_lock(&domain->syncSendMutex);
  if (status != 0) {
    err_abort(status, "Failed syncSend mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/** This routine unlocks the pthread mutex used to serialize cmsgd_syncSend calls. */
void syncSendMutexUnlock(cMsgDomainInfo *domain) {

  int status = pthread_mutex_unlock(&domain->syncSendMutex);
  if (status != 0) {
    err_abort(status, "Failed syncSend mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the pthread mutex used to serialize
 * cmsgd_subscribe and cmsgd_unsubscribe calls.
 */
void subscribeMutexLock(cMsgDomainInfo *domain) {

  int status = pthread_mutex_lock(&domain->subscribeMutex);
  if (status != 0) {
    err_abort(status, "Failed subscribe mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the pthread mutex used to serialize
 * cmsgd_subscribe and cmsgd_unsubscribe calls.
 */
void subscribeMutexUnlock(cMsgDomainInfo *domain) {

  int status = pthread_mutex_unlock(&domain->subscribeMutex);
  if (status != 0) {
    err_abort(status, "Failed subscribe mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine checks a string given as a function argument.
 * It returns an error if it contains an unprintable character or any
 * character from a list of excluded characters (`'").
 *
 * @param s string to check
 *
 * @returns CMSG_OK if string is OK
 * @returns CMSG_ERROR if string contains excluded or unprintable characters
 */   
int checkString(const char *s) {

  int i;

  if (s == NULL) return(CMSG_ERROR);

  /* check for printable character */
  for (i=0; i<(int)strlen(s); i++) {
    if (isprint((int)s[i]) == 0) return(CMSG_ERROR);
  }

  /* check for excluded chars */
  if (strpbrk(s, excludedChars) != 0) return(CMSG_ERROR);
  
  /* string ok */
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/** This routine translates a delta time into an absolute time for pthread_cond_wait. */
int getAbsoluteTime(const struct timespec *deltaTime, struct timespec *absTime) {
    struct timespec now;
    long   nsecTotal;
    
    if (absTime == NULL || deltaTime == NULL) {
      return CMSG_BAD_ARGUMENT;
    }
    
    clock_gettime(CLOCK_REALTIME, &now);
    nsecTotal = deltaTime->tv_nsec + now.tv_nsec;
    if (nsecTotal >= 1000000000L) {
      absTime->tv_nsec = nsecTotal - 1000000000L;
      absTime->tv_sec  = deltaTime->tv_sec + now.tv_sec + 1;
    }
    else {
      absTime->tv_nsec = nsecTotal;
      absTime->tv_sec  = deltaTime->tv_sec + now.tv_sec;
    }
    return CMSG_OK;
}


/*-------------------------------------------------------------------*/


/**
 * This routine initializes the structure used to handle a get - 
 * either a sendAndGet or a subscribeAndGet.
 */
void getInfoInit(getInfo *info, int reInit) {
    int status;
    
    info->id      = 0;
    info->active  = 0;
    info->error   = CMSG_OK;
    info->msgIn   = 0;
    info->quit    = 0;
    info->type    = NULL;
    info->subject = NULL;    
    info->msg     = NULL;
    
#ifdef VXWORKS
    /* vxworks only lets us initialize mutexes and cond vars once */
    if (reInit) return;
#endif

    status = pthread_cond_init(&info->cond, NULL);
    if (status != 0) {
      err_abort(status, "getInfoInit:initializing condition var");
    }
    status = pthread_mutex_init(&info->mutex, NULL);
    if (status != 0) {
      err_abort(status, "getInfoInit:initializing mutex");
    }
}


/*-------------------------------------------------------------------*/


/** This routine initializes the structure used to handle a subscribe. */
void subscribeInfoInit(subInfo *info, int reInit) {
    int j, status;
    
    info->id            = 0;
    info->active        = 0;
    info->numCallbacks  = 0;
    info->type          = NULL;
    info->subject       = NULL;
    info->typeRegexp    = NULL;
    info->subjectRegexp = NULL;
    
    for (j=0; j<MAX_CALLBACK; j++) {
      info->cbInfo[j].active   = 0;
      info->cbInfo[j].threads  = 0;
      info->cbInfo[j].messages = 0;
      info->cbInfo[j].quit     = 0;
      info->cbInfo[j].callback = NULL;
      info->cbInfo[j].userArg  = NULL;
      info->cbInfo[j].head     = NULL;
      info->cbInfo[j].tail     = NULL;
      info->cbInfo[j].config.init          = 0;
      info->cbInfo[j].config.maySkip       = 0;
      info->cbInfo[j].config.mustSerialize = 1;
      info->cbInfo[j].config.maxCueSize    = 100;
      info->cbInfo[j].config.skipSize      = 20;
      info->cbInfo[j].config.maxThreads    = 100;
      info->cbInfo[j].config.msgsPerThread = 150;
      
#ifdef VXWORKS
      /* vxworks only lets us initialize mutexes and cond vars once */
      if (reInit) continue;
#endif

      status = pthread_cond_init (&info->cbInfo[j].cond,  NULL);
      if (status != 0) {
        err_abort(status, "subscribeInfoInit:initializing condition var");
      }
      
      status = pthread_mutex_init(&info->cbInfo[j].mutex, NULL);
      if (status != 0) {
        err_abort(status, "subscribeInfoInit:initializing mutex");
      }
    }
}


/*-------------------------------------------------------------------*/

/**
 * This routine initializes the structure used to hold connection-to-
 * a-domain information.
 */
void domainInit(cMsgDomainInfo *domain, int reInit) {
  int i, status;
 
  domain->id                  = 0;

  domain->initComplete        = 0;
  domain->receiveState        = 0;
  domain->gotConnection       = 0;
      
  domain->sendSocket          = 0;
  domain->receiveSocket       = 0;
  domain->listenSocket        = 0;
  domain->keepAliveSocket     = 0;
  
  domain->sendPort            = 0;
  domain->serverPort          = 0;
  domain->listenPort          = 0;
  
  domain->hasSend             = 0;
  domain->hasSyncSend         = 0;
  domain->hasSubscribeAndGet  = 0;
  domain->hasSendAndGet       = 0;
  domain->hasSubscribe        = 0;
  domain->hasUnsubscribe      = 0;
  domain->hasShutdown         = 0;

  domain->myHost              = NULL;
  domain->sendHost            = NULL;
  domain->serverHost          = NULL;
  
  domain->name                = NULL;
  domain->udl                 = NULL;
  domain->description         = NULL;
  domain->password            = NULL;
  
  domain->failovers           = NULL;
  domain->failoverSize        = 0;
  domain->failoverIndex       = 0;
  domain->implementFailovers  = 0;
  domain->resubscribeComplete = 0;
  domain->killClientThread    = 0;
  
  domain->shutdownHandler     = NULL;
  domain->shutdownUserArg     = NULL;
  
  domain->msgBuffer           = NULL;
  domain->msgBufferSize       = 0;

  domain->msgInBuffer[0]      = NULL;
  domain->msgInBuffer[1]      = NULL;
      
  countDownLatchInit(&domain->failoverLatch, 1, reInit);

  for (i=0; i<MAX_SUBSCRIBE; i++) {
    subscribeInfoInit(&domain->subscribeInfo[i], reInit);
  }
  
  for (i=0; i<MAX_SUBSCRIBE_AND_GET; i++) {
    getInfoInit(&domain->subscribeAndGetInfo[i], reInit);
  }
  
  for (i=0; i<MAX_SEND_AND_GET; i++) {
    getInfoInit(&domain->sendAndGetInfo[i], reInit);
  }

#ifdef VXWORKS
  /* vxworks only lets us initialize mutexes and cond vars once */
  if (reInit) return;
#endif

  status = rwl_init(&domain->connectLock);
  if (status != 0) {
    err_abort(status, "domainInit:initializing connect read/write lock");
  }
  
  status = pthread_mutex_init(&domain->socketMutex, NULL);
  if (status != 0) {
    err_abort(status, "domainInit:initializing socket mutex");
  }
  
  status = pthread_mutex_init(&domain->syncSendMutex, NULL);
  if (status != 0) {
    err_abort(status, "domainInit:initializing sync send mutex");
  }
  
  status = pthread_mutex_init(&domain->subscribeMutex, NULL);
  if (status != 0) {
    err_abort(status, "domainInit:initializing subscribe mutex");
  }
  
  status = pthread_cond_init (&domain->subscribeCond,  NULL);
  if (status != 0) {
    err_abort(status, "domainInit:initializing condition var");
  }
      
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees allocated memory in a structure used to hold
 * subscribe information.
 */
void subscribeInfoFree(subInfo *info) {  
#ifdef sun    
    /* cannot destroy mutexes and cond vars in vxworks & apparently Linux */
    int j, status;

    for (j=0; j<MAX_CALLBACK; j++) {
      status = pthread_cond_destroy (&info->cbInfo[j].cond);
      if (status != 0) {
        err_abort(status, "subscribeInfoFree:destroying cond var");
      }
  
      status = pthread_mutex_destroy(&info->cbInfo[j].mutex);
      if (status != 0) {
        err_abort(status, "subscribeInfoFree:destroying mutex");
      }
  
    }
#endif   
    
    if (info->type != NULL) {
      free(info->type);
    }
    if (info->subject != NULL) {
      free(info->subject);
    }
    if (info->typeRegexp != NULL) {
      free(info->typeRegexp);
    }
    if (info->subjectRegexp != NULL) {
      free(info->subjectRegexp);
    }
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees allocated memory in a structure used to hold
 * subscribeAndGet/sendAndGet information.
 */
void getInfoFree(getInfo *info) {  
#ifdef sun    
    /* cannot destroy mutexes and cond vars in vxworks & Linux(?) */
    int status;
    
    status = pthread_cond_destroy (&info->cond);
    if (status != 0) {
      err_abort(status, "getInfoFree:destroying cond var");
    }
    
    status = pthread_mutex_destroy(&info->mutex);
    if (status != 0) {
      err_abort(status, "getInfoFree:destroying cond var");
    }
#endif
    
    if (info->type != NULL) {
      free(info->type);
    }
    
    if (info->subject != NULL) {
      free(info->subject);
    }
    
    if (info->msg != NULL) {
      cMsgFreeMessage(info->msg);
    }

}


/*-------------------------------------------------------------------*/


/**
 * This routine frees memory allocated for the structure used to hold
 * connection-to-a-domain information.
 */
void domainFree(cMsgDomainInfo *domain) {  
  int i;
#ifdef sun
  int status;
#endif
  
  if (domain->myHost           != NULL) free(domain->myHost);
  if (domain->sendHost         != NULL) free(domain->sendHost);
  if (domain->serverHost       != NULL) free(domain->serverHost);
  if (domain->name             != NULL) free(domain->name);
  if (domain->udl              != NULL) free(domain->udl);
  if (domain->description      != NULL) free(domain->description);
  if (domain->password         != NULL) free(domain->password);
  if (domain->msgBuffer        != NULL) free(domain->msgBuffer);
  if (domain->msgInBuffer[0]   != NULL) free(domain->msgInBuffer[0]);
  if (domain->msgInBuffer[1]   != NULL) free(domain->msgInBuffer[1]);
  
  if (domain->failovers        != NULL) {
    for (i=0; i<domain->failoverSize; i++) {       
       if (domain->failovers[i].udl            != NULL) free(domain->failovers[i].udl);
       if (domain->failovers[i].udlRemainder   != NULL) free(domain->failovers[i].udlRemainder);
       if (domain->failovers[i].subdomain      != NULL) free(domain->failovers[i].subdomain);
       if (domain->failovers[i].subRemainder   != NULL) free(domain->failovers[i].subRemainder);
       if (domain->failovers[i].password       != NULL) free(domain->failovers[i].password);
       if (domain->failovers[i].nameServerHost != NULL) free(domain->failovers[i].nameServerHost);    
    }
    free(domain->failovers);
  }
  
#ifdef sun
  /* cannot destroy mutexes in vxworks & Linux(?) */
  status = pthread_mutex_destroy(&domain->socketMutex);
  if (status != 0) {
    err_abort(status, "domainFree:destroying socket mutex");
  }
  
  status = pthread_mutex_destroy(&domain->syncSendMutex);
  if (status != 0) {
    err_abort(status, "domainFree:destroying sync send mutex");
  }
  
  status = pthread_mutex_destroy(&domain->subscribeMutex);
  if (status != 0) {
    err_abort(status, "domainFree:destroying subscribe mutex");
  }
  
  status = pthread_cond_destroy (&domain->subscribeCond);
  if (status != 0) {
    err_abort(status, "domainFree:destroying cond var");
  }
    
  status = rwl_destroy (&domain->connectLock);
  if (status != 0) {
    err_abort(status, "domainFree:destroying connect read/write lock");
  }
    
#endif

  countDownLatchFree(&domain->failoverLatch);
    
  for (i=0; i<MAX_SUBSCRIBE; i++) {
    subscribeInfoFree(&domain->subscribeInfo[i]);
  }
  
  for (i=0; i<MAX_SUBSCRIBE_AND_GET; i++) {
    getInfoFree(&domain->subscribeAndGetInfo[i]);
  }
  
  for (i=0; i<MAX_SEND_AND_GET; i++) {
    getInfoFree(&domain->sendAndGetInfo[i]);
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine both frees and clears the structure used to hold
 * connection-to-a-domain information.
 */
void domainClear(cMsgDomainInfo *domain) {
  domainFree(domain);
  domainInit(domain, 1);
}

 
/*-------------------------------------------------------------------*/

/**
 * This routine initializes the structure used to implement a countdown latch.
 */
void countDownLatchInit(countDownLatch *latch, int count, int reInit) {
    int status;
    
    latch->count   = count;
    latch->waiters = 0;
    
#ifdef VXWORKS
    /* vxworks only lets us initialize mutexes and cond vars once */
    if (reInit) return;
#endif

    status = pthread_mutex_init(&latch->mutex, NULL);
    if (status != 0) {
      err_abort(status, "countDownLatchInit:initializing mutex");
    }
        
    status = pthread_cond_init(&latch->countCond, NULL);
    if (status != 0) {
      err_abort(status, "countDownLatchInit:initializing condition var");
    } 
       
    status = pthread_cond_init(&latch->notifyCond, NULL);
    if (status != 0) {
      err_abort(status, "countDownLatchInit:initializing condition var");
    }    
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees allocated memory in a structure used to implement
 * a countdown latch.
 */
void countDownLatchFree(countDownLatch *latch) {  
#ifdef sun    
    /* cannot destroy mutexes and cond vars in vxworks & Linux(?) */
    int status;
    
    status = pthread_mutex_destroy(&latch->mutex);
    if (status != 0) {
      err_abort(status, "countDownLatchFree:destroying cond var");
    }
    
    status = pthread_cond_destroy (&latch->countCond);
    if (status != 0) {
      err_abort(status, "countDownLatchFree:destroying cond var");
    }
    
    status = pthread_cond_destroy (&latch->notifyCond);
    if (status != 0) {
      err_abort(status, "countDownLatchFree:destroying cond var");
    }
    
#endif   
}


/*-------------------------------------------------------------------*/

/*
 * This routine is called by a single thread spawned from the client's
 * listening thread. Since it's called serially, it can safely use
 * arrays declared at the top of the file.
 */
/** This routine reads a message sent from the server to the client. */
int cMsgReadMessage(int connfd, char *buffer, cMsgMessage *msg, int *acknowledge) {

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


/** This routine is run as a thread in which a single callback is executed. */
void *callbackThread(void *arg)
{
    /* subscription information passed in thru arg */
    cbArg *cbarg = (cbArg *) arg;
    int domainId = cbarg->domainId;
    int subIndex = cbarg->subIndex;
    int cbIndex  = cbarg->cbIndex;
    cMsgDomainInfo *domain = cbarg->domain;
    subscribeCbInfo *cback  = &domain->subscribeInfo[subIndex].cbInfo[cbIndex];
    int i, status, need, threadsAdded, maxToAdd, wantToAdd;
    int numMsgs, numThreads;
    cMsgMessage *msg, *nextMsg;
    pthread_t thd;
    /* time_t now, t; *//* for printing msg cue size periodically */
    
    /* increase concurrency for this thread for early Solaris */
    int con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);
    
    /* for printing msg cue size periodically */
    /* now = time(NULL); */
        
    /* release system resources when thread finishes */
    pthread_detach(pthread_self());

    threadsAdded = 0;

    while(1) {
      /*
       * Take a current snapshot of the number of threads and messages.
       * The number of threads may decrease since threads die if there
       * are no messages to grab, but this is the only place that the
       * number of threads will be increased.
       */
      numMsgs = cback->messages;
      numThreads = cback->threads;
      threadsAdded = 0;
      
      /* Check to see if we need more threads to handle the load */      
      if ((!cback->config.mustSerialize) &&
          (numThreads < cback->config.maxThreads) &&
          (numMsgs > cback->config.msgsPerThread)) {

        /* find number of threads needed */
        need = cback->messages/cback->config.msgsPerThread;

        /* add more threads if necessary */
        if (need > numThreads) {
          
          /* maximum # of threads that can be added w/o exceeding config limit */
          maxToAdd  = cback->config.maxThreads - numThreads;
          /* number of threads we want to add to handle the load */
          wantToAdd = need - numThreads;
          /* number of threads that we will add */
          threadsAdded = maxToAdd > wantToAdd ? wantToAdd : maxToAdd;
                    
          for (i=0; i < threadsAdded; i++) {
            status = pthread_create(&thd, NULL, supplementalThread, arg);
            if (status != 0) {
              err_abort(status, "Creating supplemental callback thread");
            }
          }
        }
      }
           
      /* lock mutex */
/* fprintf(stderr, "  CALLBACK THREAD: will grab mutex %p\n", &cback->mutex); */
      mutexLock(&cback->mutex);
/* fprintf(stderr, "  CALLBACK THREAD: grabbed mutex\n"); */
      
      /* quit if commanded to */
      if (cback->quit) {
          /* Set flag telling thread-that-adds-messages-to-cue that
           * its time to stop. The only bug-a-boo here is that we would
           * prefer to do the following with the domain->subscribeMutex
           * grabbed.
           */
          cback->active = 0;
          
          /* Now free all the messages cued up. */
          msg = cback->head; /* get first message in linked list */
          while (msg != NULL) {
            nextMsg = msg->next;
            cMsgFreeMessage(msg);
            msg = nextMsg;
          }
          
          cback->messages = 0;
          
          /* unlock mutex */
          mutexUnlock(&cback->mutex);

          /* Signal to cMsgRunCallbacks in case the cue is full and it's
           * blocked trying to put another message in. So now we tell it that
           * there are no messages in the cue and, in fact, no callback anymore.
           */
          status = pthread_cond_signal(&domain->subscribeCond);
          if (status != 0) {
            err_abort(status, "Failed callback condition signal");
          }
          
/* printf(" CALLBACK THREAD: told to quit\n"); */
          goto end;
      }

      /* do the following bookkeeping under mutex protection */
      cback->threads += threadsAdded;

      if (threadsAdded) {
        if (cMsgDebug >= CMSG_DEBUG_INFO) {
          fprintf(stderr, "thds = %d\n", cback->threads);
        }
      }
      
      /* wait while there are no messages */
      while (cback->head == NULL) {
        /* wait until signaled */
/* fprintf(stderr, "  CALLBACK THREAD: cond wait, release mutex\n"); */
        status = pthread_cond_wait(&cback->cond, &cback->mutex);
        if (status != 0) {
          err_abort(status, "Failed callback cond wait");
        }
/* fprintf(stderr, "  CALLBACK THREAD woke up, grabbed mutex\n", cback->quit); */
        
        /* quit if commanded to */
        if (cback->quit) {
          /* Set flag telling thread-that-adds-messages-to-cue that
           * its time to stop. The only bug-a-boo here is that we would
           * prefer to do the following with the domain->subscribeMutex
           * grabbed.
           */
          cback->active = 0;
          
          /* Now free all the messages cued up. */
          msg = cback->head; /* get first message in linked list */
          while (msg != NULL) {
            nextMsg = msg->next;
            cMsgFreeMessage(msg);
            msg = nextMsg;
          }
          
          cback->messages = 0;
          
          /* unlock mutex */
          mutexUnlock(&cback->mutex);

          /* Signal to cMsgRunCallbacks in case the cue is full and it's
           * blocked trying to put another message in. So now we tell it that
           * there are no messages in the cue and, in fact, no callback anymore.
           */
          status = pthread_cond_signal(&domain->subscribeCond);
          if (status != 0) {
            err_abort(status, "Failed callback condition signal");
          }
          
/* printf(" CALLBACK THREAD: told to quit\n"); */
          goto end;
        }
      }
            
      /* get first message in linked list */
      msg = cback->head;

      /* if there are no more messages ... */
      if (msg->next == NULL) {
        cback->head = NULL;
        cback->tail = NULL;
      }
      /* else make the next message the head */
      else {
        cback->head = msg->next;
      }
      cback->messages--;
     
      /* unlock mutex */
/* fprintf(stderr, "  CALLBACK THREAD: message taken off cue, cue = %d\n",cback->messages);
   fprintf(stderr, "  CALLBACK THREAD: release mutex\n"); */
      mutexUnlock(&cback->mutex);
      
      /* wakeup cMsgRunCallbacks thread if trying to add item to full cue */
/* fprintf(stderr, "  CALLBACK THREAD: wake up cMsgRunCallbacks thread\n"); */
      status = pthread_cond_signal(&domain->subscribeCond);
      if (status != 0) {
        err_abort(status, "Failed callback condition signal");
      }

      /* print out number of messages in cue */      
/*       t = time(NULL);
      if (now + 3 <= t) {
        printf("  CALLBACK THD: cue size = %d\n",cback->messages);
        now = t;
      }
 */      
      /* run callback */
#ifdef	__cplusplus
      cback->callback->callback(new cMsgMessageBase(msg), cback->userArg); 
#else
/* fprintf(stderr, "  CALLBACK THREAD: will run callback\n"); */
      cback->callback(msg, cback->userArg);
/* fprintf(stderr, "  CALLBACK THREAD: just ran callback\n"); */
#endif
      
    } /* while(1) */
    
  end:
    
    /* don't free arg as it is used for the unsubscribe handle */
    /*free(arg);*/     
    sun_setconcurrency(con);
/* fprintf(stderr, "QUITTING MAIN CALLBACK THREAD\n"); */
    pthread_exit(NULL);
    return NULL;
}



/*-------------------------------------------------------------------*
 * supplementalThread is a thread used to run a callback in parallel
 * with the callbackThread. As many supplemental threads are created
 * as needed to keep the cue size manageable.
 *-------------------------------------------------------------------*/
/**
 * This routine is run as a thread in which a callback is executed in
 * parallel with other similar threads.
 */
void *supplementalThread(void *arg)
{
    /* subscription information passed in thru arg */
    cbArg *cbarg = (cbArg *) arg;
    int domainId = cbarg->domainId;
    int subIndex = cbarg->subIndex;
    int cbIndex  = cbarg->cbIndex;
    cMsgDomainInfo *domain = cbarg->domain;
    subscribeCbInfo *cback  = &domain->subscribeInfo[subIndex].cbInfo[cbIndex];
    int status, empty;
    cMsgMessage *msg, *nextMsg;
    struct timespec wait, timeout;
    
    /* increase concurrency for this thread for early Solaris */
    int  con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);

    /* release system resources when thread finishes */
    pthread_detach(pthread_self());

    /* wait .2 sec before waking thread up and checking for messages */
    timeout.tv_sec  = 0;
    timeout.tv_nsec = 200000000;

    while(1) {
      
      empty = 0;
      
      /* lock mutex before messing with linked list */
      mutexLock(&cback->mutex);
      
      /* quit if commanded to */
      if (cback->quit) {
          /* Set flag telling thread-that-adds-messages-to-cue that
           * its time to stop. The only bug-a-boo here is that we would
           * prefer to do the following with the domain->subscribeMutex
           * grabbed.
           */
          cback->active = 0;
          
          /* Now free all the messages cued up. */
          msg = cback->head; /* get first message in linked list */
          while (msg != NULL) {
            nextMsg = msg->next;
            cMsgFreeMessage(msg);
            msg = nextMsg;
          }
          
          cback->messages = 0;
          
          /* unlock mutex */
          mutexUnlock(&cback->mutex);

          /* Signal to cMsgRunCallbacks in case the cue is full and it's
           * blocked trying to put another message in. So now we tell it that
           * there are no messages in the cue and, in fact, no callback anymore.
           */
          status = pthread_cond_signal(&domain->subscribeCond);
          if (status != 0) {
            err_abort(status, "Failed callback condition signal");
          }
          
          goto end;
      }

      /* wait while there are no messages */
      while (cback->head == NULL) {
        /* wait until signaled or for .2 sec, before
         * waking thread up and checking for messages
         */
        getAbsoluteTime(&timeout, &wait);        
        status = pthread_cond_timedwait(&cback->cond, &cback->mutex, &wait);
        
        /* if the wait timed out ... */
        if (status == ETIMEDOUT) {
          /* if we wake up 10 times with no messages (2 sec), quit this thread */
          if (++empty%10 == 0) {
            cback->threads--;
            if (cMsgDebug >= CMSG_DEBUG_INFO) {
              fprintf(stderr, "thds = %d\n", cback->threads);
            }
            
            /* unlock mutex & kill this thread */
            mutexUnlock(&cback->mutex);
            
            sun_setconcurrency(con);

            pthread_exit(NULL);
            return NULL;
          }

        }
        else if (status != 0) {
          err_abort(status, "Failed callback cond wait");
        }
        
        /* quit if commanded to */
        if (cback->quit) {
          /* Set flag telling thread-that-adds-messages-to-cue that
           * its time to stop. The only bug-a-boo here is that we would
           * prefer to do the following with the domain->subscribeMutex
           * grabbed.
           */
          cback->active = 0;
          
          /* Now free all the messages cued up. */
          msg = cback->head; /* get first message in linked list */
          while (msg != NULL) {
            nextMsg = msg->next;
            cMsgFreeMessage(msg);
            msg = nextMsg;
          }
          
          cback->messages = 0;
          
          /* unlock mutex */
          mutexUnlock(&cback->mutex);

          /* Signal to cMsgRunCallbacks in case the cue is full and it's
           * blocked trying to put another message in. So now we tell it that
           * there are no messages in the cue and, in fact, no callback anymore.
           */
          status = pthread_cond_signal(&domain->subscribeCond);
          if (status != 0) {
            err_abort(status, "Failed callback condition signal");
          }
          
          goto end;
        }
      }
                  
      /* get first message in linked list */
      msg = cback->head;      

      /* if there are no more messages ... */
      if (msg->next == NULL) {
        cback->head = NULL;
        cback->tail = NULL;
      }
      /* else make the next message the head */
      else {
        cback->head = msg->next;
      }
      cback->messages--;
     
      /* unlock mutex */
      mutexUnlock(&cback->mutex);
      
      /* wakeup cMsgRunCallbacks thread if trying to add item to full cue */
      status = pthread_cond_signal(&domain->subscribeCond);
      if (status != 0) {
        err_abort(status, "Failed callback condition signal");
      }

      /* run callback */
#ifdef	__cplusplus
      cback->callback->callback(new cMsgMessageBase(msg), cback->userArg);
#else
      cback->callback(msg, cback->userArg);
#endif
      
    }
    
  end:
          
    sun_setconcurrency(con);
    
    pthread_exit(NULL);
    return NULL;
}


/*-------------------------------------------------------------------*/

/**
 * This routine runs all the appropriate subscribe and subscribeAndGet
 * callbacks when a message arrives from the server. 
 */
int cMsgRunCallbacks(cMsgDomainInfo *domain, cMsgMessage *msg) {

  int i, j, k, status, goToNextCallback;
  subscribeCbInfo *cback;
  getInfo *info;
  cMsgMessage *message, *oldHead;
  struct timespec wait, timeout;
    

  /* wait 60 sec between warning messages for a full cue */
  timeout.tv_sec  = 3;
  timeout.tv_nsec = 0;
    
  /* for each subscribeAndGet ... */
  for (j=0; j<MAX_SUBSCRIBE_AND_GET; j++) {
    
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
      message = (cMsgMessage *) cMsgCopyMessage((void *)msg);
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
    cMsgFreeMessage((void *)msg);
    return (CMSG_OK);
  }
   
  /* Don't want subscriptions added or removed while iterating through them. */
  subscribeMutexLock(domain);
  
  /* for each client subscription ... */
  for (i=0; i<MAX_SUBSCRIBE; i++) {

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
      for (j=0; j<MAX_CALLBACK; j++) {
        /* convenience variable */
        cback = &domain->subscribeInfo[i].cbInfo[j];

	/* if there is no existing callback, look at next item ... */
        if (cback->active != 1) {
          continue;
        }

        /* copy message so each callback has its own copy */
        message = (cMsgMessage *) cMsgCopyMessage((void *)msg);
        if (message == NULL) {
          subscribeMutexUnlock(domain);
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
              mutexLock(&cback->mutex);
/* fprintf(stderr, "cMsgRunCallbacks: grabbed mutex\n"); */

              for (k=0; k < cback->config.skipSize; k++) {
                oldHead = cback->head;
                cback->head = cback->head->next;
                cMsgFreeMessage(oldHead);
                cback->messages--;
                if (cback->head == NULL) break;
              }

              mutexUnlock(&cback->mutex);

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
                  getAbsoluteTime(&timeout, &wait);        
/* fprintf(stderr, "cMsgRunCallbacks: cue full, start waiting, will UNLOCK mutex\n"); */
                  status = pthread_cond_timedwait(&domain->subscribeCond, &domain->subscribeMutex, &wait);
/* fprintf(stderr, "cMsgRunCallbacks: out of wait, mutex is LOCKED\n"); */
                  
                  /* Check to see if server died and this thread is being killed. */
                  if (domain->killClientThread == 1) {
                    subscribeMutexUnlock(domain);
                    cMsgFreeMessage((void *)message);
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
                    cMsgFreeMessage((void *)message);
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

        mutexLock(&cback->mutex);

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
        mutexUnlock(&cback->mutex);
/* printf("cMsgRunCallbacks: mutex is UNLOCKED, msg taken off cue, broadcast to callback thd\n"); */

        /* wakeup callback thread */
        status = pthread_cond_broadcast(&cback->cond);
        if (status != 0) {
          err_abort(status, "Failed callback condition signal");
        }

      } /* search callback list */
    } /* if subscribe sub/type matches msg sub/type */
  } /* for each cback */

  subscribeMutexUnlock(domain);
  
  /* Need to free up msg allocated by client's listening thread */
  cMsgFreeMessage((void *)msg);
  
  return (CMSG_OK);
}



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
#include "cMsg.h"
#include "cMsgNetwork.h"
#include "rwlock.h"
#include "cMsgDomain.h"

#ifdef VXWORKS
#include <vxWorks.h>
#endif


/** Excluded characters from subject, type, and description strings. */
static const char *excludedChars = "`\'\"";

/* local prototypes */
static void  domainFree(cMsgDomainInfo *domain); 
static void  getInfoInit(getInfo *info, int reInit);
static void  subscribeInfoInit(subInfo *info, int reInit);
static void  getInfoFree(getInfo *info);
static void  subscribeInfoFree(subInfo *info);

/*-------------------------------------------------------------------*/


/** This routine locks the given pthread mutex. */
void cMsgMutexLock(pthread_mutex_t *mutex) {

  int status = pthread_mutex_lock(mutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/** This routine unlocks the given pthread mutex. */
void cMsgMutexUnlock(pthread_mutex_t *mutex) {

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
void cMsgConnectReadLock(cMsgDomainInfo *domain) {

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
void cMsgConnectReadUnlock(cMsgDomainInfo *domain) {

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
void cMsgConnectWriteLock(cMsgDomainInfo *domain) {

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
void cMsgConnectWriteUnlock(cMsgDomainInfo *domain) {

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
void cMsgSocketMutexLock(cMsgDomainInfo *domain) {

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
void cMsgSocketMutexUnlock(cMsgDomainInfo *domain) {

  int status = pthread_mutex_unlock(&domain->socketMutex);
  if (status != 0) {
    err_abort(status, "Failed socket mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/** This routine locks the pthread mutex used to serialize cmsgd_syncSend calls. */
void cMsgSyncSendMutexLock(cMsgDomainInfo *domain) {

  int status = pthread_mutex_lock(&domain->syncSendMutex);
  if (status != 0) {
    err_abort(status, "Failed syncSend mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/** This routine unlocks the pthread mutex used to serialize cmsgd_syncSend calls. */
void cMsgSyncSendMutexUnlock(cMsgDomainInfo *domain) {

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
void cMsgSubscribeMutexLock(cMsgDomainInfo *domain) {

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
void cMsgSubscribeMutexUnlock(cMsgDomainInfo *domain) {

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
int cMsgCheckString(const char *s) {

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
int cMsgGetAbsoluteTime(const struct timespec *deltaTime, struct timespec *absTime) {
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
static void getInfoInit(getInfo *info, int reInit) {
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
static void subscribeInfoInit(subInfo *info, int reInit) {
    int j, status;
    
    info->id            = 0;
    info->active        = 0;
    info->numCallbacks  = 0;
    info->type          = NULL;
    info->subject       = NULL;
    info->typeRegexp    = NULL;
    info->subjectRegexp = NULL;
    
    for (j=0; j<CMSG_MAX_CALLBACK; j++) {
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
void cMsgDomainInit(cMsgDomainInfo *domain, int reInit) {
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
        
  cMsgCountDownLatchInit(&domain->failoverLatch, 1, reInit);

  for (i=0; i<CMSG_MAX_SUBSCRIBE; i++) {
    subscribeInfoInit(&domain->subscribeInfo[i], reInit);
  }
  
  for (i=0; i<CMSG_MAX_SUBSCRIBE_AND_GET; i++) {
    getInfoInit(&domain->subscribeAndGetInfo[i], reInit);
  }
  
  for (i=0; i<CMSG_MAX_SEND_AND_GET; i++) {
    getInfoInit(&domain->sendAndGetInfo[i], reInit);
  }

#ifdef VXWORKS
  /* vxworks only lets us initialize mutexes and cond vars once */
  if (reInit) return;
#endif

  status = rwl_init(&domain->connectLock);
  if (status != 0) {
    err_abort(status, "cMsgDomainInit:initializing connect read/write lock");
  }
  
  status = pthread_mutex_init(&domain->socketMutex, NULL);
  if (status != 0) {
    err_abort(status, "cMsgDomainInit:initializing socket mutex");
  }
  
  status = pthread_mutex_init(&domain->syncSendMutex, NULL);
  if (status != 0) {
    err_abort(status, "cMsgDomainInit:initializing sync send mutex");
  }
  
  status = pthread_mutex_init(&domain->subscribeMutex, NULL);
  if (status != 0) {
    err_abort(status, "cMsgDomainInit:initializing subscribe mutex");
  }
  
  status = pthread_cond_init (&domain->subscribeCond,  NULL);
  if (status != 0) {
    err_abort(status, "cMsgDomainInit:initializing condition var");
  }
      
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees allocated memory in a structure used to hold
 * subscribe information.
 */
static void subscribeInfoFree(subInfo *info) {  
#ifdef sun    
    /* cannot destroy mutexes and cond vars in vxworks & apparently Linux */
    int j, status;

    for (j=0; j<CMSG_MAX_CALLBACK; j++) {
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
static void getInfoFree(getInfo *info) {  
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
static void domainFree(cMsgDomainInfo *domain) {  
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

  cMsgCountDownLatchFree(&domain->failoverLatch);
    
  for (i=0; i<CMSG_MAX_SUBSCRIBE; i++) {
    subscribeInfoFree(&domain->subscribeInfo[i]);
  }
  
  for (i=0; i<CMSG_MAX_SUBSCRIBE_AND_GET; i++) {
    getInfoFree(&domain->subscribeAndGetInfo[i]);
  }
  
  for (i=0; i<CMSG_MAX_SEND_AND_GET; i++) {
    getInfoFree(&domain->sendAndGetInfo[i]);
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine both frees and clears the structure used to hold
 * connection-to-a-domain information.
 */
void cMsgDomainClear(cMsgDomainInfo *domain) {
  domainFree(domain);
  cMsgDomainInit(domain, 1);
}

 
/*-------------------------------------------------------------------*/

/**
 * This routine initializes the structure used to implement a countdown latch.
 */
void cMsgCountDownLatchInit(countDownLatch *latch, int count, int reInit) {
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
void cMsgCountDownLatchFree(countDownLatch *latch) {  
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


/** This routine is run as a thread in which a single callback is executed. */
void *cMsgCallbackThread(void *arg)
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
    cMsgMessage_t *msg, *nextMsg;
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
            status = pthread_create(&thd, NULL, cMsgSupplementalThread, arg);
            if (status != 0) {
              err_abort(status, "Creating supplemental callback thread");
            }
          }
        }
      }
           
      /* lock mutex */
/* fprintf(stderr, "  CALLBACK THREAD: will grab mutex %p\n", &cback->mutex); */
      cMsgMutexLock(&cback->mutex);
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
          cMsgMutexUnlock(&cback->mutex);

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
          cMsgMutexUnlock(&cback->mutex);

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
      cMsgMutexUnlock(&cback->mutex);
      
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
      cback->callback(msg, cback->userArg);
      
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
 * cMsgSupplementalThread is a thread used to run a callback in parallel
 * with the cMsgCallbackThread. As many supplemental threads are created
 * as needed to keep the cue size manageable.
 *-------------------------------------------------------------------*/
/**
 * This routine is run as a thread in which a callback is executed in
 * parallel with other similar threads.
 */
void *cMsgSupplementalThread(void *arg)
{
    /* subscription information passed in thru arg */
    cbArg *cbarg = (cbArg *) arg;
    int domainId = cbarg->domainId;
    int subIndex = cbarg->subIndex;
    int cbIndex  = cbarg->cbIndex;
    cMsgDomainInfo *domain = cbarg->domain;
    subscribeCbInfo *cback  = &domain->subscribeInfo[subIndex].cbInfo[cbIndex];
    int status, empty;
    cMsgMessage_t *msg, *nextMsg;
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
      cMsgMutexLock(&cback->mutex);
      
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
          cMsgMutexUnlock(&cback->mutex);

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
        cMsgGetAbsoluteTime(&timeout, &wait);        
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
            cMsgMutexUnlock(&cback->mutex);
            
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
          cMsgMutexUnlock(&cback->mutex);

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
      cMsgMutexUnlock(&cback->mutex);
      
      /* wakeup cMsgRunCallbacks thread if trying to add item to full cue */
      status = pthread_cond_signal(&domain->subscribeCond);
      if (status != 0) {
        err_abort(status, "Failed callback condition signal");
      }

      /* run callback */
      cback->callback(msg, cback->userArg);
      
    }
    
  end:
          
    sun_setconcurrency(con);
    
    pthread_exit(NULL);
    return NULL;
}

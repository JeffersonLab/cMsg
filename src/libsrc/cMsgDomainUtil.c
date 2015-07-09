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
 
#ifdef VXWORKS
#include <vxWorks.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include <pthread.h>

#include "cMsgPrivate.h"
#include "cMsg.h"
#include "cMsgNetwork.h"
#include "rwlock.h"
#include "cMsgDomain.h"


/** Excluded characters from subject, type, and description strings. */
static const char *excludedChars = "`\'\"";

/* local prototypes */


/*-------------------------------------------------------------------*/


/** This routine locks the given pthread mutex. */
void cMsgMutexLock(pthread_mutex_t *mutex) {

  int status = pthread_mutex_lock(mutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/** This routine unlocks the given pthread mutex. */
void cMsgMutexUnlock(pthread_mutex_t *mutex) {

  int status = pthread_mutex_unlock(mutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the read lock used to allow simultaneous
 * writing of the update server thread, and distribution of messages
 * to callbacks, but to NOT allow simultaneous
 * addition or removal of subscriptions.
 */
void cMsgSubscribeReadLock(cMsgDomainInfo *domain) {

    int status = rwl_readlock(&domain->subscribeLock);
    if (status != 0) {
        cmsg_err_abort(status, "Failed subscribe read lock");
    }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the read lock used to allow simultaneous
 * writing of the update server thread, and distribution of messages
 * to callbacks, but to NOT allow simultaneous
 * addition or removal of subscriptions.
 */
void cMsgSubscribeReadUnlock(cMsgDomainInfo *domain) {

    int status = rwl_readunlock(&domain->subscribeLock);
    if (status != 0) {
        cmsg_err_abort(status, "Failed subscribe read unlock");
    }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the write lock used to allow simultaneous
 * writing of the update server thread, and distribution of messages
 * to callbacks, but to NOT allow simultaneous
 * addition or removal of subscriptions.
 */
void cMsgSubscribeWriteLock(cMsgDomainInfo *domain) {

    int status = rwl_writelock(&domain->subscribeLock);
    if (status != 0) {
        cmsg_err_abort(status, "Failed subscribe read lock");
    }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the write lock used to allow simultaneous
 * writing of the update server thread, and distribution of messages
 * to callbacks, but to NOT allow simultaneous
 * addition or removal of subscriptions.
 */
void cMsgSubscribeWriteUnlock(cMsgDomainInfo *domain) {

    int status = rwl_writeunlock(&domain->subscribeLock);
    if (status != 0) {
        cmsg_err_abort(status, "Failed subscribe read unlock");
    }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the read lock used to allow simultaneous
 * execution of send, syncSend, subscribe, unsubscribe,
 * sendAndGet, and subscribeAndGet, but NOT allow simultaneous
 * execution of those routines with disconnect.
 */
void cMsgConnectReadLock(cMsgDomainInfo *domain) {

    int status = rwl_readlock(&domain->connectLock);
    if (status != 0) {
        cmsg_err_abort(status, "Failed read lock");
    }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the read lock used to allow simultaneous
 * execution of send, syncSend, subscribe, unsubscribe,
 * sendAndGet, and subscribeAndGet, but NOT allow simultaneous
 * execution of those routines with disconnect.
 */
void cMsgConnectReadUnlock(cMsgDomainInfo *domain) {

    int status = rwl_readunlock(&domain->connectLock);
    if (status != 0) {
        cmsg_err_abort(status, "Failed read unlock");
    }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the write lock used to allow simultaneous
 * execution of send, syncSend, subscribe, unsubscribe,
 * sendAndGet, and subscribeAndGet, but NOT allow simultaneous
 * execution of those routines with disconnect.
 */
void cMsgConnectWriteLock(cMsgDomainInfo *domain) {

    int status = rwl_writelock(&domain->connectLock);
    if (status != 0) {
        cmsg_err_abort(status, "Failed read lock");
    }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the write lock used to allow simultaneous
 * execution of send, syncSend, subscribe, unsubscribe,
 * sendAndGet, and subscribeAndGet, but NOT allow simultaneous
 * execution of those routines with disconnect.
 */
void cMsgConnectWriteUnlock(cMsgDomainInfo *domain) {

    int status = rwl_writeunlock(&domain->connectLock);
    if (status != 0) {
        cmsg_err_abort(status, "Failed read unlock");
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
    cmsg_err_abort(status, "Failed socket mutex lock");
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
    cmsg_err_abort(status, "Failed socket mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the pthread mutex used to serialize
 * subscribeAndGet hash table access.
 */
void cMsgSubAndGetMutexLock(cMsgDomainInfo *domain) {

  int status = pthread_mutex_lock(&domain->subAndGetMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed subAndGet mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the pthread mutex used to serialize
 * subscribeAndGet hash table access.
 */
void cMsgSubAndGetMutexUnlock(cMsgDomainInfo *domain) {

  int status = pthread_mutex_unlock(&domain->subAndGetMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed subAndGet mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the pthread mutex used to serialize
 * sendAndGet hash table access.
 */
void cMsgSendAndGetMutexLock(cMsgDomainInfo *domain) {

  int status = pthread_mutex_lock(&domain->sendAndGetMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed sendAndGet mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the pthread mutex used to serialize
 * sendAndGet hash table access.
 */
void cMsgSendAndGetMutexUnlock(cMsgDomainInfo *domain) {

  int status = pthread_mutex_unlock(&domain->sendAndGetMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed sendAndGet mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the pthread mutex used to serialize
 * syncSend hash table access.
 */
void cMsgSyncSendMutexLock(cMsgDomainInfo *domain) {

  int status = pthread_mutex_lock(&domain->syncSendMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed syncSend mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the pthread mutex used to serialize
 * syncSend hash table access.
 */
void cMsgSyncSendMutexUnlock(cMsgDomainInfo *domain) {

  int status = pthread_mutex_unlock(&domain->syncSendMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed syncSend mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine returns the number of digits in an integer
 * including a minus sign.
 *
 * @param number integer
 * @param isUint64 is number an unsigned 64 bit integer (0=no)
 * @return number of digits in the integer argument including a minus sign
 */
int cMsgNumDigits(int64_t number, int isUint64) {
  int digits = 1;
  uint64_t step = 10;
  
  if (isUint64) {
    uint64_t num = (uint64_t) number;
    while (step <= num) {
      if (++digits >= 20) return 20; /* 20 digits at most in uint64_t */
      step *= 10;
    }
    return digits;
  }
  
  if (number < 0) {
    digits++;
    number *= -1;
  }
  
  while (step <= number) {
    digits++;
    step *= 10;
  }
  return digits;
}

/*-------------------------------------------------------------------*/


/**
 * This routine checks a string given as a function argument.
 * It returns an error if it is NULL, or contains an unprintable character
 * or any character from a list of excluded characters (`'").
 *
 * @param s string to check
 *
 * @returns CMSG_OK if string is OK
 * @returns CMSG_ERROR if string contains excluded or unprintable characters
 */   
int cMsgCheckString(const char *s) {

  int i, len;

  if (s == NULL) return(CMSG_ERROR);
  len = strlen(s);

  /* check for printable character */
  for (i=0; i<len; i++) {
    if (isprint((int)s[i]) == 0) return(CMSG_ERROR);
  }

  /* check for excluded chars */
  if (strpbrk(s, excludedChars) != NULL) return(CMSG_ERROR);
  
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
 * This routine blocks the signal SIGPIPE in addition to those already
 * being blocked and stores the old signal mask in the domain structure
 * so it can be restored upon disconnect. If this routine fails it's not
 * critical so ignore errors.
 * In Linux and Solaris 10, the SIGPIPE signal is delivered to the thread
 * that caused it. In Solaris 9 and earlier the signal is delivered to a
 * random thread and so may not be blocked in the rc and cMsg domains.
 *
 * @param domain pointer to struct of domain information
 */
void cMsgBlockSignals(cMsgDomainInfo *domain) {
    sigset_t signalSet;
    int status;
  
    /*
     * Block SIGPIPE signal from linux kernel to this and all derived threads
     * when read or write is interrupted in the middle by the server death.
     * This may interfere with the caller's handling of signals.
     */
    sigemptyset(&signalSet);
    /* set has only SIGPIPE in it */
    sigaddset(&signalSet, SIGPIPE);
    /* Add SIGPIPE to those signals already blocked by this thread,
     * AND store the old signal mask for later restoration. */
    if (!domain->maskStored) {
        status = pthread_sigmask(SIG_BLOCK, &signalSet, &domain->originalMask);
        if (status == 0) domain->maskStored = 1; 
    }
}


/*-------------------------------------------------------------------*/


/**
 * This routine restores the signal mask in effect before connect was called.
 * If this routine fails it's not critical so ignore errors.
 *
 * @param domain pointer to struct of domain information
 */
void cMsgRestoreSignals(cMsgDomainInfo *domain) {
    int status;
  
    if (domain->maskStored) {
        status = pthread_sigmask(SIG_SETMASK, &domain->originalMask, NULL);
        if (status == 0) domain->maskStored = 0;
    }
}


/*-------------------------------------------------------------------*/


/**
 * This routine initializes the structure used to store numberRange data
 * for subscriptions.
 * 
 * @param range pointer to range data structure
 */
 void cMsgNumberRangeInit(numberRange *range) {    
    range->numbers[0] = -1;
    range->numbers[1] = -1;
    range->numbers[2] = -1;
    range->numbers[3] = -1;
    range->next       = NULL;
    range->nextHead   = NULL;
}


/*-------------------------------------------------------------------*/


/**
 * This routine initializes the structure used to handle
 * either a sendAndGet, a subscribeAndGet, or a syncSend.
 *
 * @param info pointer to structure holding send&Get, sub&Get or syncSend info
 */
void cMsgGetInfoInit(getInfo *info) {
    int status;
    
    info->id       = 0;
    info->response = 0;
    info->error    = CMSG_OK;
    info->msgIn    = 0;
    info->quit     = 0;
    info->type     = NULL;
    info->subject  = NULL;    
    info->msg      = NULL;
    
    status = pthread_cond_init(&info->cond, NULL);
    if (status != 0) {
      cmsg_err_abort(status, "cMsgGetInfoInit:initializing condition var");
    }
    status = pthread_mutex_init(&info->mutex, NULL);
    if (status != 0) {
      cmsg_err_abort(status, "cMsgGetInfoInit:initializing mutex");
    }
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees allocated memory in a structure used to handle
 * subscribeAndGet/sendAndGet/syncSend information.
 *
 * @param info pointer to structure holding send&Get, sub&Get or syncSend info
 */
void cMsgGetInfoFree(getInfo *info) {
  void *p = (void *)info->msg;

#ifdef VXWORKS
    /* cannot destroy mutexes and cond vars in vxworks & Linux(?) */
    int status;
    
    status = pthread_cond_destroy (&info->cond);
    if (status != 0) {
      cmsg_err_abort(status, "cMsgGetInfoFree: destroying cond var");
    }
    
    status = pthread_mutex_destroy(&info->mutex);
    if (status != 0) {
      cmsg_err_abort(status, "cMsgGetInfoFree: destroying cond var");
    }
#endif
    
    if (info->type != NULL)    {free(info->type);    info->type    = NULL;}
    if (info->subject != NULL) {free(info->subject); info->subject = NULL;}
    if (info->msg != NULL)     {cMsgFreeMessage(&p); info->msg     = NULL;}
}


/*-------------------------------------------------------------------*/


/**
 * This routine initializes the structure used to handle a subscription's callback
 * with the exception of the mutex and condition variable.
 * 
 * @param info pointer to subscription callback structure
 */
void cMsgCallbackInfoInit(subscribeCbInfo *info) {
    int status;
    
    info->fullQ    = 0;
    info->threads  = 0;
    info->messages = 0;
    info->pause    = 0;
    info->quit     = 0;
    info->msgCount = 0;
    info->callback = NULL;
    info->cbarg    = NULL;
    info->userArg  = NULL;
    info->head     = NULL;
    info->tail     = NULL;
    info->next     = NULL;
    info->config.init          = 0;
    info->config.maySkip       = 0;
    info->config.mustSerialize = 1;
    info->config.maxCueSize    = 10000;
    info->config.skipSize      = 2000;
    info->config.maxThreads    = 20;
    info->config.msgsPerThread = 500;
    info->config.stackSize     = 0;

    cMsgCountDownLatchInit(&info->pauseLatch, 1);

    status = pthread_cond_init(&info->addToQ,  NULL);
    if (status != 0) {
        cmsg_err_abort(status, "cMsgCallbackInfoInit:initializing condition var");
    }

    status = pthread_cond_init(&info->checkQ,  NULL);
    if (status != 0) {
        cmsg_err_abort(status, "cMsgCallbackInfoInit:initializing condition var");
    }

    status = pthread_cond_init(&info->takeFromQ,  NULL);
    if (status != 0) {
        cmsg_err_abort(status, "cMsgCallbackInfoInit:initializing condition var");
    }

    status = pthread_mutex_init(&info->mutex, NULL);
    if (status != 0) {
      cmsg_err_abort(status, "cMsgCallbackInfoInit:initializing mutex");
    }

}


/*-------------------------------------------------------------------*/


/**
 * This routine free any resources taken by mutexes and condition
 * variables.
 *
 * @param info pointer to subscription callback structure
 */
void cMsgCallbackInfoFree(subscribeCbInfo *info) {
#ifndef VXWORKS
    /* cannot destroy mutexes and cond vars in vxworks & apparently Linux */
    int status;

      status = pthread_cond_destroy (&info->addToQ);
      if (status != 0) {
          cmsg_err_abort(status, "cMsgCallbackInfoFree:destroying cond var");
      }

      status = pthread_cond_destroy (&info->checkQ);
      if (status != 0) {
          cmsg_err_abort(status, "cMsgCallbackInfoFree:destroying cond var");
      }

      status = pthread_cond_destroy (&info->takeFromQ);
      if (status != 0) {
          cmsg_err_abort(status, "cMsgCallbackInfoFree:destroying cond var");
      }

      status = pthread_mutex_destroy(&info->mutex);
      if (status != 0) {
        cmsg_err_abort(status, "cMsgCallbackInfoFree:destroying mutex");
      }
  
#endif
    cMsgCountDownLatchFree(&info->pauseLatch);
}


/*-------------------------------------------------------------------*/


/** This routine initializes the structure used to handle a subscribe. */
void cMsgSubscribeInfoInit(subInfo *info) {
    info->id                = 0;
    info->numCallbacks      = 0;
    info->subWildCardCount  = 0;
    info->typeWildCardCount = 0;
    info->subRangeCount     = 0;
    info->typeRangeCount    = 0;
    info->type            = NULL;
    info->subject         = NULL;
    info->typeRegexp      = NULL;
    info->subjectRegexp   = NULL;
    info->subRange        = NULL;
    info->typeRange       = NULL;
    info->callbacks       = NULL;

    hashInit(&info->subjectTable,  256);
    hashInit(&info->typeTable,     256);
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees allocated memory in a structure used to hold
 * subscribe information.
 *
 * @param info pointer to subscription information structure
 */
void cMsgSubscribeInfoFree(subInfo *info) {
  if (info->type != NULL)           {free(info->type);          info->type          = NULL;}
  if (info->subject != NULL)        {free(info->subject);       info->subject       = NULL;}
  if (info->typeRegexp != NULL)     {free(info->typeRegexp);    info->typeRegexp    = NULL;}
  if (info->subjectRegexp != NULL)  {free(info->subjectRegexp); info->subjectRegexp = NULL;}
  cMsgRegfree(&info->compTypeRegexp);
  cMsgRegfree(&info->compSubRegexp);

  cMsgNumberRangeFree(info->subRange);
  cMsgNumberRangeFree(info->typeRange);
  info->subRange  = NULL;
  info->typeRange = NULL;
    
  hashDestroy(&info->subjectTable, NULL, NULL);
  hashDestroy(&info->typeTable,    NULL, NULL);

  /* the mutexes and condition variables are all freed by cMsgCallbackInfoFree
  * individually in the code. */
}


/*-------------------------------------------------------------------*/


/**
 * This routine initializes the structure used to hold connection-to-
 * a-domain information.
 *
 * @param domain pointer to cMsg domain information structure
 */
void cMsgDomainInit(cMsgDomainInfo *domain) {
  int status;
 
  domain->receiveState        = 0;
  domain->gotConnection       = 0;
  domain->disconnectCalled    = 0;
  domain->functionsRunning    = 0;
  domain->killKAthread        = 0;

  domain->sendSocket          = 0;
  domain->sendUdpSocket       = -1;
  domain->keepAliveSocket     = 0;
  domain->receiveSocket       = 0; /* rc domain only */
  domain->listenSocket        = 0; /* rc domain only */
  
  domain->sendPort            = 0;
  domain->sendUdpPort         = 0;
  domain->listenPort          = 0; /* rc domain only */
  domain->localPort           = 0;

  domain->hasSend             = 0;
  domain->hasSyncSend         = 0;
  domain->hasSubscribeAndGet  = 0;
  domain->hasSendAndGet       = 0;
  domain->hasSubscribe        = 0;
  domain->hasUnsubscribe      = 0;
  domain->hasShutdown         = 0;
  
  domain->rcConnectAbort      = 0;
  domain->rcConnectComplete   = 0;

  domain->myHost              = NULL;
  domain->sendHost            = NULL;
  domain->serverHost          = NULL;
  
  domain->name                = NULL;
  domain->udl                 = NULL;
  domain->description         = NULL;
  domain->password            = NULL;
  
  cMsgParsedUDLInit(&domain->currentUDL);

  domain->failovers           = NULL;
  domain->failoverSize        = 0;
  domain->failoverIndex       = 0;
  domain->implementFailovers  = 0;
  domain->haveLocalCloudServer= 0;
  domain->resubscribeComplete = 0;
  domain->killClientThread    = 0;
  
  domain->shutdownHandler     = NULL;
  domain->shutdownUserArg     = NULL;
  
  domain->msgBuffer           = NULL;
  domain->msgBufferSize       = 0;
  
  domain->userXML             = NULL;
  domain->monitorXML          = (char *)calloc(1,1);
  domain->monitorXMLSize      = 0;
  
  domain->maskStored          = 0;
  sigemptyset(&domain->originalMask);
  
  memset((void *) &domain->monData, 0, sizeof(monitorData));
  
  hashInit(&domain->rcIpAddrTable,    32);
  hashInit(&domain->syncSendTable,   128);
  hashInit(&domain->sendAndGetTable, 128);
  hashInit(&domain->subAndGetTable,  128);
  hashInit(&domain->subscribeTable,  128);
  hashInit(&domain->cloudServerTable, 32);

  cMsgCountDownLatchInit(&domain->syncLatch, 1);
    
  status = rwl_init(&domain->connectLock);
  if (status != 0) {
      cmsg_err_abort(status, "cMsgDomainInit:initializing connect read/write lock");
  }
  
  status = rwl_init(&domain->subscribeLock);
  if (status != 0) {
      cmsg_err_abort(status, "cMsgDomainInit:initializing subscribe read/write lock");
  }
  
  status = pthread_mutex_init(&domain->socketMutex, NULL);
  if (status != 0) {
    cmsg_err_abort(status, "cMsgDomainInit:initializing socket mutex");
  }
  
  status = pthread_mutex_init(&domain->sendAndGetMutex, NULL);
  if (status != 0) {
    cmsg_err_abort(status, "cMsgDomainInit:initializing sendAndGet mutex");
  }
  
  status = pthread_mutex_init(&domain->subAndGetMutex, NULL);
  if (status != 0) {
    cmsg_err_abort(status, "cMsgDomainInit:initializing subAndGet mutex");
  }
  
  status = pthread_mutex_init(&domain->syncSendMutex, NULL);
  if (status != 0) {
    cmsg_err_abort(status, "cMsgDomainInit:initializing syncSend mutex");
  }
  
  status = pthread_mutex_init(&domain->rcConnectMutex, NULL);
  if (status != 0) {
    cmsg_err_abort(status, "cMsgDomainInit:initializing rc connect mutex");
  }
  
  status = pthread_cond_init (&domain->rcConnectCond,  NULL);
  if (status != 0) {
    cmsg_err_abort(status, "cMsgDomainInit:initializing rc connect condition var");
  }
      
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees memory allocated for the structure used to hold
 * connection-to-a-domain information.
 */
void cMsgDomainFree(cMsgDomainInfo *domain) {  
  int i, size;
  hashNode *entries = NULL;
#ifndef VXWORKS
  int status;
#endif

  if (domain->myHost         != NULL) {free(domain->myHost);         domain->myHost         = NULL;}
  if (domain->sendHost       != NULL) {free(domain->sendHost);       domain->sendHost       = NULL;}
  if (domain->serverHost     != NULL) {free(domain->serverHost);     domain->serverHost     = NULL;}
  if (domain->name           != NULL) {free(domain->name);           domain->name           = NULL;}
  if (domain->udl            != NULL) {free(domain->udl);            domain->udl            = NULL;}
  if (domain->description    != NULL) {free(domain->description);    domain->description    = NULL;}
  if (domain->password       != NULL) {free(domain->password);       domain->password       = NULL;}
  if (domain->expid          != NULL) {free(domain->expid);          domain->expid          = NULL;}
  if (domain->msgBuffer      != NULL) {free(domain->msgBuffer);      domain->msgBuffer      = NULL;}
  if (domain->userXML        != NULL) {free(domain->userXML);        domain->userXML        = NULL;}
  if (domain->monitorXML     != NULL) {free(domain->monitorXML);     domain->monitorXML     = NULL;}

  cMsgParsedUDLFree(&domain->currentUDL);

  if (domain->failovers != NULL) {
    for (i=0; i<domain->failoverSize; i++) {
      cMsgParsedUDLFree(&domain->failovers[i]);
    }
    free(domain->failovers);
  }

  /* no values (all NULL) so only keys must be freed */
  hashDestroy(&domain->rcIpAddrTable, NULL, NULL);
  
  hashDestroy(&domain->syncSendTable, &entries, &size);
  /* If there are entries in the hashTable, free them.
   * Key is a string, value is getInfo pointer. */
  if (entries != NULL) {
    for (i=0; i<size; i++) {
      free(entries[i].key);
      cMsgGetInfoFree((getInfo *)entries[i].data);
      free(entries[i].data);
    }
    free(entries);
  }
  
  hashDestroy(&domain->sendAndGetTable, &entries, &size);
  if (entries != NULL) {
    for (i=0; i<size; i++) {
      free(entries[i].key);
      cMsgGetInfoFree((getInfo *)entries[i].data);
      free(entries[i].data);
    }
    free(entries);
  }
  
  hashDestroy(&domain->subAndGetTable, &entries, &size);
  if (entries != NULL) {
    for (i=0; i<size; i++) {
      free(entries[i].key);
      cMsgGetInfoFree((getInfo *)entries[i].data);
      free(entries[i].data);
    }
    free(entries);
  }

  /* new BUGBUG needs more ? */
  hashDestroy(&domain->subscribeTable, &entries, &size);
  if (entries != NULL) {
    for (i=0; i<size; i++) {
      free(entries[i].key);
      cMsgSubscribeInfoFree((subInfo *)entries[i].data); /* new less extreme form ? */
      free(entries[i].data);
    }
    free(entries);
  }
  
  hashDestroy(&domain->cloudServerTable, &entries, &size);
  if (entries != NULL) {
    for (i=0; i<size; i++) {
      free(entries[i].key);
      cMsgParsedUDLFree((parsedUDL *)entries[i].data);
      free(entries[i].data);
    }
    free(entries);
  }

#ifndef VXWORKS
  /* cannot destroy mutexes in vxworks & Linux(?) */

  status = rwl_destroy(&domain->connectLock);
  if (status != 0) {
      cmsg_err_abort(status, "cMsgDomainFree:destroying connect read/write lock");
  }
  
  status = rwl_destroy(&domain->subscribeLock);
  if (status != 0) {
      cmsg_err_abort(status, "cMsgDomainFree:destroying subscribe read/write lock");
  }
  
  status = pthread_mutex_destroy(&domain->socketMutex);
  if (status != 0) {
    cmsg_err_abort(status, "cMsgDomainFree:destroying socket mutex");
  }
  
  status = pthread_mutex_destroy(&domain->sendAndGetMutex);
  if (status != 0) {
    cmsg_err_abort(status, "cMsgDomainInit:destroying sendAndGet mutex");
  }
  
  status = pthread_mutex_destroy(&domain->subAndGetMutex);
  if (status != 0) {
    cmsg_err_abort(status, "cMsgDomainInit:destroying subAndGet mutex");
  }
  
  status = pthread_mutex_destroy(&domain->syncSendMutex);
  if (status != 0) {
    cmsg_err_abort(status, "cMsgDomainInit:destroying syncSend mutex");
  }
  
  status = pthread_mutex_destroy(&domain->rcConnectMutex);
  if (status != 0) {
    cmsg_err_abort(status, "cMsgDomainFree:destroying rc connect mutex");
  }
  
  status = pthread_cond_destroy (&domain->rcConnectCond);
  if (status != 0) {
    cmsg_err_abort(status, "cMsgDomainFree:destroying rc connect cond var");
  }
  /* this is freed in disconnectFromKeepAlive */
  /*  
  status = rwl_destroy (&domain->connectLock);
  if (status != 0) {
  cmsg_err_abort(status, "cMsgDomainFree:destroying connect read/write lock");
}
  */
#endif

  cMsgCountDownLatchFree(&domain->syncLatch);
    
}


/*-------------------------------------------------------------------*/


/**
 * This routine initializes memory in a structure used to hold
 * parsed UDL information.
 */
void cMsgParsedUDLInit(parsedUDL *p) {
  if (p == NULL) return;
  
  p->nameServerPort    = 0;
  p->domainServerPort  = 0;
  p->nameServerUdpPort = 0;
  p->mustMulticast     = 0;
  p->timeout           = 0;
  p->regime            = CMSG_REGIME_MEDIUM;
  p->failover          = CMSG_FAILOVER_ANY;
  p->cloud             = CMSG_CLOUD_ANY;
  p->isLocal           = 0;
  p->udl               = NULL;
  p->udlRemainder      = NULL;
  p->subdomain         = NULL;
  p->subRemainder      = NULL;
  p->password          = NULL;
  p->nameServerHost    = NULL;
  p->serverName        = NULL;
}


/*-------------------------------------------------------------------*/


/**
 * This routine copies memory in a structure used to hold
 * parsed UDL information to another structure.
 */
int cMsgParsedUDLCopy(parsedUDL *dest, parsedUDL *src) {
  if (dest == NULL || src == NULL) return CMSG_OK;
  
  dest->nameServerPort    = src->nameServerPort;
  dest->domainServerPort  = src->domainServerPort;
  dest->nameServerUdpPort = src->nameServerUdpPort;
  dest->mustMulticast     = src->mustMulticast;
  dest->timeout           = src->timeout;
  dest->regime            = src->regime;
  dest->failover          = src->failover;
  dest->cloud             = src->cloud;
  dest->isLocal           = src->isLocal;
  
  cMsgParsedUDLFree(dest);
  
  if (src->udl != NULL) {
    dest->udl = strdup(src->udl);
    if (dest->udl == NULL) return CMSG_OUT_OF_MEMORY;
  }
  
  if (src->udlRemainder != NULL) {
    dest->udlRemainder = strdup(src->udlRemainder);
    if (dest->udlRemainder == NULL) return CMSG_OUT_OF_MEMORY;
  }
  
  if (src->subdomain != NULL) {
    dest->subdomain = strdup(src->subdomain);
    if (dest->subdomain == NULL) return CMSG_OUT_OF_MEMORY;
  }
  
  if (src->subRemainder != NULL) {
    dest->subRemainder = strdup(src->subRemainder);
    if (dest->subRemainder == NULL) return CMSG_OUT_OF_MEMORY;
  }
  
  if (src->password != NULL) {
    dest->password = strdup(src->password);
    if (dest->password == NULL) return CMSG_OUT_OF_MEMORY;
  }
  
  if (src->nameServerHost != NULL) {
    dest->nameServerHost = strdup(src->nameServerHost);
    if (dest->nameServerHost == NULL) return CMSG_OUT_OF_MEMORY;
  }
  
  if (src->serverName != NULL) {
    dest->serverName = strdup(src->serverName);
    if (dest->serverName == NULL) return CMSG_OUT_OF_MEMORY;
  }
  
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees allocated memory in a structure used to hold
 * parsed UDL information.
 */
void cMsgParsedUDLFree(parsedUDL *p) {
  if (p == NULL) return;
  
  if (p->udl             != NULL) {free(p->udl);             p->udl             = NULL;}
  if (p->udlRemainder    != NULL) {free(p->udlRemainder);    p->udlRemainder    = NULL;}
  if (p->subdomain       != NULL) {free(p->subdomain);       p->subdomain       = NULL;}
  if (p->subRemainder    != NULL) {free(p->subRemainder);    p->subRemainder    = NULL;}
  if (p->password        != NULL) {free(p->password);        p->password        = NULL;}
  if (p->nameServerHost  != NULL) {free(p->nameServerHost);  p->nameServerHost  = NULL;}
  if (p->serverName      != NULL) {free(p->serverName);      p->serverName      = NULL;}
}

/*-------------------------------------------------------------------*/

/**
 * This routine frees allocated memory in a structure used to hold
 * parsed subscription's subject and type information.
 *
 * @param r the head of a linked list of linked lists containing
 *          parsed subscription information
 */
void cMsgNumberRangeFree(numberRange *r) {
  numberRange *head, *range, *nextHead, *next;
  if (r == NULL) return;
    
  /* go from linked list head to linked list head */
  head = r;
  while (head != NULL) {
    nextHead = head->nextHead;
    /* free a sinle linked list */
    range = head;
    while (range != NULL) {
      next = range->next;
      free(range);
      range = next;
    }
    head = nextHead;
  }
}

/*-------------------------------------------------------------------*/


/**
 * This routine both frees and clears the structure used to hold
 * connection-to-a-domain information.
 */
void cMsgDomainClear(cMsgDomainInfo *domain) {
  cMsgDomainFree(domain);
  cMsgDomainInit(domain);
}

 
/*-------------------------------------------------------------------*/

/**
 * This routine initializes the structure used to implement a countdown latch.
 */
void cMsgCountDownLatchInit(countDownLatch *latch, int count) {
    int status;
    
    latch->count   = count;
    latch->waiters = 0;
    
    status = pthread_mutex_init(&latch->mutex, NULL);
    if (status != 0) {
      cmsg_err_abort(status, "countDownLatchInit:initializing mutex");
    }
        
    status = pthread_cond_init(&latch->countCond, NULL);
    if (status != 0) {
      cmsg_err_abort(status, "countDownLatchInit:initializing condition var");
    } 
       
    status = pthread_cond_init(&latch->notifyCond, NULL);
    if (status != 0) {
      cmsg_err_abort(status, "countDownLatchInit:initializing condition var");
    }    
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees allocated memory in a structure used to implement
 * a countdown latch.
 */
void cMsgCountDownLatchFree(countDownLatch *latch) {  
#ifndef VXWORKS
    /* cannot destroy mutexes and cond vars in vxworks & Linux(?) */
    int status;
    
    status = pthread_mutex_destroy(&latch->mutex);
    if (status != 0) {
      cmsg_err_abort(status, "countDownLatchFree:destroying cond var");
    }
    
    status = pthread_cond_destroy (&latch->countCond);
    if (status != 0) {
      cmsg_err_abort(status, "countDownLatchFree:destroying cond var");
    }
    
    status = pthread_cond_destroy (&latch->notifyCond);
    if (status != 0) {
      cmsg_err_abort(status, "countDownLatchFree:destroying cond var");
    }
    
#endif   
}


/*-------------------------------------------------------------------*/


/**
 * This routine waits for the given count down latch to be counted down
 * to 0 before returning. Once the count down is at 0, it notifies the
 * "cMsgLatchCountDown" caller that it got the message and then returns.
 *
 * @param latch pointer to latch structure
 * @param timeout time to wait for the count down to reach 0 before returning
 *                with a timeout code (0)
 *
 * @returns -1 if the latch is being reset
 * @returns  0 if the count down has not reached 0 before timing out
 * @returns +1 if the count down has reached 0
 */
int cMsgLatchAwait(countDownLatch *latch, const struct timespec *timeout) {
  int status;
  struct timespec wait;
  
  /* Lock mutex */
  status = pthread_mutex_lock(&latch->mutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed mutex lock");
  }
  
  /* if latch is being reset, return -1 (error) */
  if (latch->count < 0) {
/* printf("    cMsgLatchAwait: resetting so return -1\n"); */
    status = pthread_mutex_unlock(&latch->mutex);
    if (status != 0) {
      cmsg_err_abort(status, "Failed mutex unlock");
    }

    return -1;  
  }
  /* if count = 0 already, return 1 (true) */
  else if (latch->count == 0) {
/* printf("    cMsgLatchAwait: count is already 0 so return 1\n"); */
    status = pthread_mutex_unlock(&latch->mutex);
    if (status != 0) {
      cmsg_err_abort(status, "Failed mutex unlock");
    }
    
    return 1;
  }
  
  /* We're a waiter */
  latch->waiters++;
/* printf("    cMsgLatchAwait: waiters set to %d\n",latch->waiters); */
  
  /* wait until count <= 0 */
  while (latch->count > 0) {
    /* wait until signaled */
    if (timeout == NULL) {
/* printf("    cMsgLatchAwait: wait forever\n"); */
      status = pthread_cond_wait(&latch->countCond, &latch->mutex);
    }
    /* wait until signaled or timeout */
    else {
      cMsgGetAbsoluteTime(timeout, &wait);
/* printf("    cMsgLatchAwait: timed wait\n"); */
      status = pthread_cond_timedwait(&latch->countCond, &latch->mutex, &wait);
    }
    
    /* if we've timed out, return 0 (false) */
    if (status == ETIMEDOUT) {
/* printf("    cMsgLatchAwait: timed out, return 0\n"); */
      status = pthread_mutex_unlock(&latch->mutex);
      if (status != 0) {
        cmsg_err_abort(status, "Failed mutex unlock");
      }

      return 0;
    }
    else if (status != 0) {
      cmsg_err_abort(status, "Failed cond wait");
    }
  }
  
  /* if latch is being reset, return -1 (error) */
  if (latch->count < 0) {
/* printf("    cMsgLatchAwait: resetting so return -1\n"); */
    status = pthread_mutex_unlock(&latch->mutex);
    if (status != 0) {
      cmsg_err_abort(status, "Failed mutex unlock");
    }

    return -1;  
  }
  
  /* if count down reached (count == 0) ... */
  latch->waiters--;  
/* printf("    cMsgLatchAwait: waiters set to %d\n",latch->waiters); */

  /* signal that we're done */
  status = pthread_cond_broadcast(&latch->notifyCond);
  if (status != 0) {
    cmsg_err_abort(status, "Failed condition broadcast");
  }
/* printf("    cMsgLatchAwait: broadcasted to (notified) cMsgLatchCountDowner\n"); */

  /* unlock mutex */
  status = pthread_mutex_unlock(&latch->mutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed mutex unlock");
  }
/* printf("    cMsgLatchAwait: done, return 1\n"); */
  
  return 1;
}

 
/*-------------------------------------------------------------------*/


/**
 * This routine reduces the count of a count down latch by 1.
 * Once the count down is at 0, it notifies the "cMsgLatchAwait" callers
 * of the fact and then waits for those callers to notify this routine
 * that they got the message. Once all the callers have done so, this
 * routine returns.
 *
 * @param latch pointer to latch structure
 * @param timeout time to wait for the "cMsgLatchAwait" callers to respond
 *                before returning with a timeout code (0), NULL means wait
 *                forever
 *
 * @returns -1 if the latch is being reset
 * @returns  0 if the "cMsgLatchAwait" callers have not responded before timing out
 * @returns +1 if the count down has reached 0 and all waiters have responded
 */
int cMsgLatchCountDown(countDownLatch *latch, const struct timespec *timeout) {
  int status;
  struct timespec wait;
  
  /* Lock mutex */
  status = pthread_mutex_lock(&latch->mutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed mutex lock");
  }
  
  /* if latch is being reset, return -1 (false) */
  if (latch->count < 0) {
/* printf("cMsgLatchCountDown: resetting so return -1\n"); */
    status = pthread_mutex_unlock(&latch->mutex);
    if (status != 0) {
      cmsg_err_abort(status, "Failed mutex unlock");
    }

    return -1;  
  }  
  /* if count = 0 already, return 1 (true) */
  else if (latch->count == 0) {
/* printf("cMsgLatchCountDown: count = 0 so return 1\n"); */
    status = pthread_mutex_unlock(&latch->mutex);
    if (status != 0) {
      cmsg_err_abort(status, "Failed mutex unlock");
    }
    
    return 1;
  }
  
  /* We're reducing the count */
  latch->count--;
/* printf("cMsgLatchCountDown: count is now %d\n", latch->count); */
  
  /* if we've reached 0, signal all waiters to wake up */
  if (latch->count == 0) {
/* printf("cMsgLatchCountDown: count = 0 so broadcast to waiters\n"); */
    status = pthread_cond_broadcast(&latch->countCond);
    if (status != 0) {
      cmsg_err_abort(status, "Failed condition broadcast");
    }    
  }
    
  /* wait until all waiters have reported back to us that they're awake */
  while (latch->waiters > 0) {
    /* wait until signaled */
    if (timeout == NULL) {
/* printf("cMsgLatchCountDown: wait for ever\n"); */
      status = pthread_cond_wait(&latch->notifyCond, &latch->mutex);
    }
    /* wait until signaled or timeout */
    else {
      cMsgGetAbsoluteTime(timeout, &wait);
/* printf("cMsgLatchCountDown: timed wait\n"); */
      status = pthread_cond_timedwait(&latch->notifyCond, &latch->mutex, &wait);
    }
    
    /* if we've timed out, return 0 (false) */
    if (status == ETIMEDOUT) {
/* printf("cMsgLatchCountDown: timed out\n"); */
      status = pthread_mutex_unlock(&latch->mutex);
      if (status != 0) {
        cmsg_err_abort(status, "Failed mutex unlock");
      }

      return 0;
    }
    else if (status != 0) {
      cmsg_err_abort(status, "Failed cond wait");
    }
    
    /* if latch is being reset, return -1 (error) */
    if (latch->count < 0) {
/* printf("cMsgLatchCountDown: resetting so return -1\n"); */
      status = pthread_mutex_unlock(&latch->mutex);
      if (status != 0) {
        cmsg_err_abort(status, "Failed mutex unlock");
      }

      return -1;  
    }  

  }
    
  /* unlock mutex */
  status = pthread_mutex_unlock(&latch->mutex);
  if (status != 0) {
    cmsg_err_abort(status, "await: Failed mutex unlock");
  }
/* printf("cMsgLatchCountDown:done, return 1\n"); */
  
  return 1;
}


/*-------------------------------------------------------------------*/


/**
 * This routine resets a count down latch to a given count.
 * The latch is disabled, the "cMsgLatchAwait" and "cMsgLatchCountDown"
 * callers are awakened, wait some time, and finally the count is reset.
 *
 * @param latch pointer to latch structure
 * @param count number to reset the initial count of the latch to
 * @param timeout time to wait for the "cMsgLatchAwait" and "cMsgLatchCountDown" callers
 *                to return errors before going ahead and resetting the count
 */
void cMsgLatchReset(countDownLatch *latch, int count, const struct timespec *timeout) {
  int status;

  /* Lock mutex */
  status = pthread_mutex_lock(&latch->mutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed mutex lock");
  }
    
  /* Disable the latch */
  latch->count = -1;
/* printf("  cMsgLatchReset: count set to -1\n"); */
  
  /* signal all waiters to wake up */
  status = pthread_cond_broadcast(&latch->countCond);
  if (status != 0) {
    cmsg_err_abort(status, "Failed condition broadcast");
  }
     
  /* signal all countDowners to wake up */
  status = pthread_cond_broadcast(&latch->notifyCond);
  if (status != 0) {
    cmsg_err_abort(status, "Failed condition broadcast");
  }      
/* printf("  cMsgLatchReset: broadcasted to count & notify cond vars\n"); */
        
  /* unlock mutex */
  status = pthread_mutex_unlock(&latch->mutex);
  if (status != 0) {
    cmsg_err_abort(status, "await: Failed mutex unlock");
  }
  
  /* wait the given amount for all parties to detect the reset */
  if (timeout != NULL) {
/* printf("  cMsgLatchReset: sleeping\n"); */
    nanosleep(timeout, NULL);
  }
  
  /* Lock mutex again */
  status = pthread_mutex_lock(&latch->mutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed mutex lock");
  }
    
  /* Reset the latch */
  latch->count = count;
/* printf("  cMsgLatchReset: count set to %d\n", count); */
  
  /* unlock mutex */
  status = pthread_mutex_unlock(&latch->mutex);
  if (status != 0) {
    cmsg_err_abort(status, "await: Failed mutex unlock");
  }
/* printf("  cMsgLatchReset: done\n"); */

}


/*-------------------------------------------------------------------*/


/** This structure (pointer) is passed as an arg to a callback worker thread. */
typedef struct cbWorkerArg_t {
    pthread_t       *threads;  /**< Array of thread ids of worker threads. */
    int             *used;     /**< Array of ints storing if threads element is used (1) or not (0). */
    int              index;    /**< Index into arrays - threads & used. */
    char            *subject;  /**< Subject of subscription. */
    char            *type;     /**< Type of subscription. */
    char            *udl;      /**< Udl of connection. */
    subscribeCbInfo *cb;       /**< Pointer to callback info structure. */
} cbWorkerArg;


/**
 * Routine to cancel all callback worker threads.
 * @param cb pointer to callback
 * @param threads array of callback worker pthread ids
 * @param used array of booleans stating whether element in threads array is used or not
 */
static void cancelCallbacks(subscribeCbInfo *cb, pthread_t *threads, int *used) {
    int i;
    
    /* cancel all worker threads */
    for (i=0; i<cb->config.maxThreads; i++) {
        if (!used[i]) continue;
        /*printf("cb thd: cancelling worker #%d\n",i);*/
        pthread_cancel(threads[i]);
    }
}


/**
 * Routine to join all callback worker threads (ensure they've ended).
 * @param cb pointer to callback
 * @param threads array of callback worker pthread ids
 * @param used array of booleans stating whether element in threads array is used or not
 */
static void joinCallbackWorkers(subscribeCbInfo *cb, pthread_t *threads, int *used) {
    void *p;
    int i, status;
    
    for (i=0; i<cb->config.maxThreads; i++) {
        if (!used[i]) continue;
        /*printf("cb thd: try joining worker #%d\n",i);*/
        status = pthread_join(threads[i], &p);
        /*
         * If inside of a callback, disconnect is called & the thread ends,
         * then p will != PTHREAD_CANCELED, but will = 0. Similarly, if inside
         * of a callback, disconnect is called followed by exit, then the
         * pthread_join will fail - returning an error. Thus we cannot, in
         * general, assume that an error here is a bad thing. So we ignore
         * them for now.
         */
        /*
        if (status != 0 || p != PTHREAD_CANCELED) {
            cmsg_err_abort(status, "Failed pend thread join");
        }
        */
        /*printf("cb thd: joined worker #%d, status = %d, exit val = %p\n",i,status,p);*/
    }
}


/** This routine is run as a thread which controls a single subscription's callback. */
void *cMsgCallbackThread(void *arg)
{
    /* subscription information passed in thru arg */
    cbArg *cbarg           = (cbArg *) arg;
    cMsgDomainInfo *domain = cbarg->domain;
    subInfo *sub           = cbarg->sub;
    subscribeCbInfo *cb    = cbarg->cb;
    int i, nextIndex, freeIndex, status, need, threadsAdded, maxToAdd, wantToAdd, state, con;
    int *used;
    cMsgMessage_t *msg;
    pthread_t *threads;
    cbWorkerArg *workerArg;
    char *subject, *type, *udl;

           
    /* release system resources when thread finishes */
    pthread_detach(pthread_self());
    
    /* increase concurrency for this thread for early Solaris */
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);
    
    /* keep local copies of some strings as they may be freed elsewhere */
    udl     = strdup(domain->udl);
    type    = strdup(sub->type);
    subject = strdup(sub->subject);

    /* allocate array to track threads */
    /*printf("cb thd: allocating space for %d worker thds\n",cb->config.maxThreads);*/
    used = (int *) calloc(cb->config.maxThreads,sizeof(int));
    threads = (pthread_t *) malloc(cb->config.maxThreads*sizeof(pthread_t));
    if (threads == NULL || used == NULL || udl == NULL || type == NULL || subject == NULL) {
        cmsg_err_exit(-1, "cMsgCallbackThreadNew: out of memory");
    }

    /* Tell subscribe that the callback thread has started.
     * Since we're past using sub & domain they may be freed by disconnect. */
    cb->started = 1;
    
    /*************************************/
    /* spawn one permanent worker thread */
    /*************************************/
    used[0] = 1;
    nextIndex = 1;
    
    /* arg to pass to new thread */
    workerArg = (cbWorkerArg *) malloc(sizeof(cbWorkerArg));
    if (workerArg == NULL) {
        cmsg_err_exit(-1, "cMsgCallbackThreadNew: out of memory");
    }
    workerArg->cb = cb;
    workerArg->used = used;
    workerArg->index = 0;
    workerArg->threads = threads;
    workerArg->subject = subject;
    workerArg->type = type;
    workerArg->udl = udl;

    status = pthread_create(&threads[0], NULL, cMsgCallbackWorkerThread, (void *)workerArg);
    if (status != 0) {
        cmsg_err_abort(status, "cMsgCallbackThread: creating worker callback thread");
    }
    
    cMsgMutexLock(&cb->mutex);
    cb->threads++;
    cMsgMutexUnlock(&cb->mutex);
  

    while(1) {

        cMsgMutexLock(&cb->mutex);

        if (cb->quit) {
            cancelCallbacks(cb, threads, used);
            cMsgMutexUnlock(&cb->mutex);
            
            /* wait until all worker threads finish before releasing memory */
            sched_yield();
            joinCallbackWorkers(cb, threads, used);
            
            /* free callback stuff */
            cMsgCallbackInfoFree(cb);
            free(cb);
            free(used); free(threads); free(subject); free(type); free(udl);
            
            /*printf("cb thd: done\n");*/
            pthread_exit(NULL);
        }

        threadsAdded = 0;

        /* Check to see if we need more threads to handle the load */
        if ( !cb->config.mustSerialize &&
             (cb->threads < cb->config.maxThreads) &&
             (cb->messages > cb->config.msgsPerThread)) {

            /* find number of threads needed */
            need = cb->messages/cb->config.msgsPerThread;

            /* add more threads if necessary */
            if (need > cb->threads) {
          
                /* maximum # of threads that can be added w/o exceeding config limit */
                maxToAdd  = cb->config.maxThreads - cb->threads;
                /* number of threads we want to add to handle the load */
                wantToAdd = need - cb->threads;
                /* number of threads that we will add */
                threadsAdded = maxToAdd > wantToAdd ? wantToAdd : maxToAdd;
                
                for (i=0; i < threadsAdded; i++) {
                    /* find free element in array */
                    freeIndex = -1;
                    if (nextIndex > cb->config.maxThreads - 1) nextIndex = 0;

                    while (freeIndex < 0) {
                        for (i=nextIndex; i<cb->config.maxThreads; i++) {
                            if (!used[i]) {
                                used[i] = 1;
                                freeIndex = i;
                                nextIndex++;
                                break;
                            }
                        }
                        if (freeIndex < 0 && nextIndex > 0) {
                            nextIndex = 0;
                            continue;
                        }
                        break;
                    }
                
                    if (freeIndex < 0) {
                        cmsg_err_exit(-1, "cMsgCallbackThreadNew: no array space for callback worker thread id");
                    }
                    
                    /* arg to pass to new thread */
                    workerArg = (cbWorkerArg *) malloc(sizeof(cbWorkerArg));
                    if (workerArg == NULL) {
                        cmsg_err_exit(-1, "cMsgCallbackThreadNew: out of memory");
                    }
                    workerArg->cb = cb;
                    workerArg->used = used;
                    workerArg->index = freeIndex;
                    workerArg->threads = threads;
                    workerArg->subject = subject;
                    workerArg->type = type;
                    workerArg->udl = udl;
#ifdef VXWORKS
                    /* zero some memory to avoid funny pthread stuff on vxworks */
                    memset(&threads[freeIndex], 0, sizeof(pthread_t));
#endif
                    status = pthread_create(&threads[freeIndex], NULL,
                                            cMsgCallbackWorkerThread, (void *)workerArg);
                    if (status != 0) {
                        cmsg_err_abort(status, "Creating worker callback thread");
                    }
                }
            }
        }
        
        cb->threads += threadsAdded;

        if (threadsAdded) {
            if (cMsgDebug >= CMSG_DEBUG_INFO) {
                fprintf(stderr, "cMsgCallbackThread:  # thds for cb = %d\n", cb->threads);
            }
        }
        
        /* Wait until message was added to Q or we need to quit. */
        status = pthread_cond_wait(&cb->checkQ, &cb->mutex);
        if (status != 0) {
           cmsg_err_abort(status, "Failed callback cond wait");
        }

        if (cb->quit) {
            cancelCallbacks(cb, threads, used);
            cMsgMutexUnlock(&cb->mutex);
            
            /* wait until all worker threads finish before releasing memory */
            sched_yield();
            joinCallbackWorkers(cb, threads, used);
            
            /* free callback stuff */
            cMsgCallbackInfoFree(cb);
            free(cb);
            free(used); free(threads); free(subject); free(type); free(udl);
            
            /*printf("cb thd: done\n");*/
            pthread_exit(NULL);
        }

        cMsgMutexUnlock(&cb->mutex);
      
    } /* while(1) */
      
    pthread_exit(NULL);
}

/*-------------------------------------------------------------------*
 * cMsgCallbackWorkerThread needs a pthread cancellation cleanup handler
 * because of a bug in either the Linux kernel or the pthread lib.
 * When the worker thread is cancelled while it is in a pthread_cond_wait,
 * it gets cancelled with the mutex still held (boo).
 * This handler will be called when the cMsgCallbackWorkerThread is
 * cancelled. It's task is to make sure the mutex is released.
 *-------------------------------------------------------------------*/
static void cleanUpHandler(void *arg) {
    int status;
    subscribeCbInfo *cb = (subscribeCbInfo *)arg;
    
    /* decrease concurrency as this thread disappears */
    sun_setconcurrency(sun_getconcurrency() - 1);
  
    /* release mutex */
    status = pthread_mutex_trylock(&cb->mutex);
    /*
    if (status == EBUSY) {
        printf("cleanUpHandler: callback mutex is STILL BUSY, so released it, status = %d\n", status);
    }
    else {
        printf("cleanUpHandler: callback mutex is NOT BUSY, status = %d\n", status);
    }
    */
    pthread_mutex_unlock(&cb->mutex);
}


/*-------------------------------------------------------------------*/
/**
 * This routine is run as a thread in which a callback is executed in
 * parallel with other similar threads. Worker threads may be created
 * as needed to keep the callback cue size manageable.
 */
void *cMsgCallbackWorkerThread(void *arg)
{
    /* subscription information passed in thru arg */
    cbWorkerArg *workerArg = (cbWorkerArg *)arg;
    subscribeCbInfo *cb = workerArg->cb;
    int permanentWorker = workerArg->index == 0 ? 1 : 0;
    int *used = workerArg->used, index = workerArg->index;
    pthread_t *threads = workerArg->threads;
    char *subject=workerArg->subject, *type=workerArg->type, *udl=workerArg->udl;
    int latchStatus, status, empty, state;
    cMsgMessage_t *msg;
    struct timespec timeout = {0,200000000}; /* wait .2 sec before waking up and checking for messages */
    struct timespec wait, waitOneTic = {0,1}, waitOneSec = {1,0}; /* 1 sec */

    /* increase concurrency for this thread for early Solaris */
    int  con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);

    /* release system resources when thread finishes */
    /*pthread_detach(pthread_self());*/

    free(arg);
    
    /*printf("Worker Callback Thd #%d started\n", index);*/
    
    pthread_cleanup_push(cleanUpHandler, (void *)cb);

    while(1) {
      
        empty = 0;
      
        /* lock mutex before messing with linked list */
        /*printf("worker: try grab 1, mutex = %p\n", &cb->mutex);*/
        cMsgMutexLock(&cb->mutex);
        /*printf("worker: grabbed 1, mutex = %p\n", &cb->mutex);*/
 
        /* quit if commanded to */
        if (cb->quit) {
            /*printf("Worker Callback Thd a: #%d quiting\n", index);*/
            cb->threads--;
            used[index] = 0;
/*printf("worker: try release 1, mutex = %p\n", &cb->mutex);*/
            cMsgMutexUnlock(&cb->mutex);
/*printf("worker: released 1, mutex = %p\n", &cb->mutex);*/
            goto end;
        }

        /* wait while there are no messages */
        while (cb->head == NULL) {
            /* if this is the one permanent worker thread ... */
            if (permanentWorker) {
                /* Wait until signaled when a message arrives.
                 * This is also a pthread cancellation point and will
                 * be woken up if disconnect/unsubscribe does a pthread_cancel. */
                /*printf("worker: try wait 1, mutex = %p\n", &cb->mutex);*/
                status = pthread_cond_wait(&cb->addToQ, &cb->mutex);
                if (status != 0) {
                    cmsg_err_abort(status, "Failed callback cond wait");
                }
                /*printf("worker: woken up 1, mutex = %p\n", &cb->mutex);*/
            }
            else {
                /* Wait until signaled when message arrives or for .2 sec, before
                 * waking thread up and checking status. */
                cMsgGetAbsoluteTime(&timeout, &wait);
/*printf("worker: try wait 2, mutex = %p\n", &cb->mutex);*/
                status = pthread_cond_timedwait(&cb->addToQ, &cb->mutex, &wait);
/*printf("worker: woken up 2, mutex = %p\n", &cb->mutex);*/

                /* if the wait timed out ... */
                if (status == ETIMEDOUT) {
                    /* if we wake up 10 times with no messages (2 sec), quit this thread */
                    if (++empty%10 == 0) {
                        cb->threads--;
                        used[index] = 0;
                        /* unlock mutex & kill this thread */
/*printf("worker: try release 2, mutex = %p\n", &cb->mutex);*/
                        cMsgMutexUnlock(&cb->mutex);
/*printf("worker: released 2, mutex = %p\n", &cb->mutex);*/
                        goto end;
                    }
                }
                else if (status != 0) {
                    cmsg_err_abort(status, "Failed callback cond wait");
                }
            }
      
            /* quit if commanded to */
            if (cb->quit) {
                /*printf("Worker Callback Thd b: #%d quiting\n", index);*/
                cb->threads--;
                used[index] = 0;
/*printf("worker: try release 3, mutex = %p\n", &cb->mutex);*/
                cMsgMutexUnlock(&cb->mutex);
/*printf("worker: released 3, mutex = %p\n", &cb->mutex);*/
                goto end;
            }
        }
                  
        /* get first message in linked list */
        msg = cb->head;

        /* if there are no more messages ... */
        if (msg->next == NULL) {
            cb->head = NULL;
            cb->tail = NULL;
        }
        /* else make the next message the head */
        else {
            cb->head = msg->next;
        }
        cb->messages--;
        cb->fullQ = 0;
        cb->msgCount++; /* # of msgs passed to callback */
   
        /* wakeup cMsgRunCallbacks thread if trying to add item to full cue */
        status = pthread_cond_signal(&cb->takeFromQ);
        if (status != 0) {
            cmsg_err_abort(status, "Failed callback condition signal");
        }

        /* unlock mutex */
/*printf("worker: try release 4, mutex = %p\n", &cb->mutex);*/
/*printf("worker: released 4, mutex = %p\n", &cb->mutex);*/

        /* Pause here if commanded to */
        /* latch returns -1 if the latch is being reset
                          0 if the count down has not reached 0 before timing out
                         +1 if the count down has reached 0  */
        latchStatus = 0;
        while (cb->pause && latchStatus == 0) {
/*printf("TRY PAUSING:\n");*/
            cMsgMutexUnlock(&cb->mutex);
            /* Wait for 1 second before waking up and checking if we gotta quit. */
/*printf("  pause, wait for latch\n");*/
            latchStatus = cMsgLatchAwait(&cb->pauseLatch, &waitOneSec);
            /*
            if (latchStatus == 0) {
                printf("  pause, latch timed out\n");
            }
            else if (latchStatus == 1) {
                printf("  pause, latch was unlatched by resume\n");
            }
            else {
                printf("  pause, latch was reset\n");
            }
            */
            cMsgMutexLock(&cb->mutex);
            cMsgLatchReset(&cb->pauseLatch, 1, &waitOneTic);

            /* quit if commanded to */
            if (cb->quit) {
                /*printf("Worker Callback Thd a: #%d quiting\n", index);*/
                cb->threads--;
                used[index] = 0;
                cMsgMutexUnlock(&cb->mutex);
                goto end;
            }
        }

        cMsgMutexUnlock(&cb->mutex);

        /* run callback */
        msg->context.domain  = (char *) strdup("cMsg");
        msg->context.subject = (char *) strdup(subject);
        msg->context.type    = (char *) strdup(type);
        msg->context.udl     = (char *) strdup(udl);
        msg->context.cueSize = &cb->messages; /* pointer to cueSize info allows it
        to always be up-to-date in callback */

        /* Disable pthread cancellation during running of callback.
         * Since we have no idea what is done in the callback, better
         * be safe than sorry. */
        status = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &state);
        if (status != 0) {
            cmsg_err_abort(status, "Disabling callback thread cancelability");
        }

        cb->callback(msg, cb->userArg);
      
        /* Renable pthread cancellation at cancellation points like pthread_testcancel */
        status = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &state);
        if (status != 0) {
            cmsg_err_abort(status, "Enabling callback thread cancelability");
        }

        /* If the callback took a long time, an unsubscribe or disconnect could have taken
         * place and allocated memory (like cb) could have been freed. Thus, before we do
         * anything to access that memory, allow us to be cancelled. Pthread_cancel will
         * have been called for this thread. The only problem here is if pthread_cancel is
         * called just after the following pthread_testcancel statement. In that case, we
         * have SLEEP_SEC amount of seconds before the memory we need to access (cb)
         * disappears. */
        pthread_testcancel();
    }
    
  end:
          
  sun_setconcurrency(con);
  
  /* calls cleanup handler */
  pthread_cleanup_pop(0);
  
  pthread_exit(NULL);
}

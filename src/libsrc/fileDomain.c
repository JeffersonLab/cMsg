/* to do:
 *   textOnly
 */



/*----------------------------------------------------------------------------*
 *
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    E.Wolin, 13-Jan-2006, Jefferson Lab                                     *
 *                                                                            *
 *    Authors: Elliott Wolin                                                  *
 *             wolin@jlab.org                    Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-7365             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 *
 * Description:
 *
 *  Implements built-in cMsg FILE domain
 *
 *  To enable a new built-in domain:
 *     edit cMsg.c, search for codaDomainTypeInfo, and do the same for the new domain
 *     add new file to C/Makefile.*, same as cMsgDomain.*
 *     add new file to CC/Makefile.*, same as cMsgDomain.*
 *
 *
 *----------------------------------------------------------------------------*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "cMsgPrivate.h"
#include "cMsg.h"
#include "cMsgNetwork.h"
#include "rwlock.h"



/* Prototypes of the 17 functions which implement the standard cMsg tasks in each domain. */
int   cmsg_file_connect(const char *myUDL, const char *myName, const char *myDescription,
                        const char *UDLremainder, void **domainId);
int   cmsg_file_reconnect(void *domainId);
int   cmsg_file_send(void *domainId, void *msg);
int   cmsg_file_syncSend(void *domainId, void *msg, const struct timespec *timeout, int *response);
int   cmsg_file_flush(void *domainId, const struct timespec *timeout);
int   cmsg_file_subscribe(void *domainId, const char *subject, const char *type,
                          cMsgCallbackFunc *callback, void *userArg,
                          cMsgSubscribeConfig *config, void **handle);
int   cmsg_file_unsubscribe(void *domainId, void *handle);
int   cmsg_file_subscriptionPause (void *domainId, void *handle);
int   cmsg_file_subscriptionResume(void *domainId, void *handle);
int   cmsg_file_subscriptionQueueClear(void *domainId, void *handle);
int   cmsg_file_subscriptionQueueCount(void *domainId, void *handle, int *count);
int   cmsg_file_subscriptionQueueIsFull(void *domainId, void *handle, int *full);
int   cmsg_file_subscriptionMessagesTotal(void *domainId, void *handle, int *total);
int   cmsg_file_subscribeAndGet(void *domainId, const char *subject, const char *type,
                                const struct timespec *timeout, void **replyMsg);
int   cmsg_file_sendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout, void **replyMsg);
int   cmsg_file_monitor(void *domainId, const char *command, void **replyMsg);
int   cmsg_file_start(void *domainId);
int   cmsg_file_stop(void *domainId);
int   cmsg_file_disconnect(void **domainId);
int   cmsg_file_shutdownClients(void *domainId, const char *client, int flag);
int   cmsg_file_shutdownServers(void *domainId, const char *server, int flag);
int   cmsg_file_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg);
int   cmsg_file_isConnected(void *domainId, int *connected);
int   cmsg_file_setUDL(void *domainId, const char *udl, const char *remainder);
int   cmsg_file_getCurrentUDL(void *domainId, const char **udl);
int   cmsg_file_getServerHost(void *domainId, const char **ipAddress);
int   cmsg_file_getServerPort(void *domainId, int *port);
int   cmsg_file_getInfo(void *domainId, const char *command, char **string);


/** List of the functions which implement the standard cMsg tasks in each domain. */
static domainFunctions functions = { cmsg_file_connect, cmsg_file_reconnect,
                                     cmsg_file_send, cmsg_file_syncSend, cmsg_file_flush,
                                     cmsg_file_subscribe, cmsg_file_unsubscribe,
                                     cmsg_file_subscriptionPause, cmsg_file_subscriptionResume,
                                     cmsg_file_subscriptionQueueClear, cmsg_file_subscriptionMessagesTotal,
                                     cmsg_file_subscriptionQueueCount, cmsg_file_subscriptionQueueIsFull,
                                     cmsg_file_subscribeAndGet, cmsg_file_sendAndGet,
                                     cmsg_file_monitor, cmsg_file_start,
                                     cmsg_file_stop, cmsg_file_disconnect,
                                     cmsg_file_shutdownClients, cmsg_file_shutdownServers,
                                     cmsg_file_setShutdownHandler, cmsg_file_isConnected,
                                     cmsg_file_setUDL, cmsg_file_getCurrentUDL,
                                     cmsg_file_getServerHost, cmsg_file_getServerPort,
                                     cmsg_file_getInfo};



/* for registering the domain */
domainTypeInfo fileDomainTypeInfo = {"file",&functions};


/* local domain info */
typedef struct {
  char *udl;
  char *domain;
  char *host;
  char *name;
  char *descr;
  FILE *file;
  int textOnly;
  pthread_mutex_t mutex; /* make this domain thread-safe */
} fileDomainInfo;


/*-------------------------------------------------------------------*/


/** This routine locks the given pthread mutex. */
static void mutexLock(pthread_mutex_t *mutex) {
  int status = pthread_mutex_lock(mutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/** This routine unlocks the given pthread mutex. */
static void mutexUnlock(pthread_mutex_t *mutex) {
  int status = pthread_mutex_unlock(mutex);
  if (status != 0) {
    cmsg_err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
* This routine does general I/O and returns a string for each string argument.
*
* @param domain id of the domain connection
* @param command command whose value determines what is returned in string arg
* @param string  pointer which gets filled in with a return string
*
* @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
*/
int cmsg_file_getInfo(void *domainId, const char *command, char **string) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
* This routine resets the server host anme, but is <b>NOT</b> implemented in this domain.
* @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
*/
int cmsg_file_getServerHost(void *domainId, const char **ipAddress) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
* This routine resets server socket port, but is <b>NOT</b> implemented in this domain.
* @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
*/
int cmsg_file_getServerPort(void *domainId, int *port) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * This routine resets the UDL, but is <b>NOT</b> implemented in this domain.
 *
 * @param domainId id of the domain connection
 * @param newUDL new UDL
 * @param newRemainder new UDL remainder
 *
 * @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
 */
int cmsg_file_setUDL(void *domainId, const char *newUDL, const char *newRemainder) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * This routine gets the UDL current used in the existing connection.
 *
 * @param domainId id of the domain connection
 * @param udl pointer filled in with current UDL (do not write to this
              pointer)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if domainId arg is NULL
 */
int cmsg_file_getCurrentUDL(void *domainId, const char **udl) {
    fileDomainInfo *fdi = (fileDomainInfo*)domainId;
    
    /* check args */
    if (fdi == NULL) {
        return(CMSG_BAD_ARGUMENT);
    }
    if (udl != NULL) *udl = fdi->udl;
    return(CMSG_OK);
}



/*-------------------------------------------------------------------*/


int cmsg_file_connect(const char *myUDL, const char *myName, const char *myDescription,
                      const char *UDLremainder, void **domainId) {


  char *fname;
  const char *c;
  int textOnly, status;
  fileDomainInfo *fdi;
  FILE *f;


  /* get file name and textOnly attribute from UDLremainder */
  if(UDLremainder==NULL)return(CMSG_ERROR);
  c = strchr(UDLremainder,'?');
  if(c==NULL) {
    fname=strdup(UDLremainder);
    textOnly=1;
  } else {
    size_t nc = c-UDLremainder+1;
    fname=(char*)malloc(nc+1);
    strncpy(fname,UDLremainder,nc);
    fname[nc]='\0';

    /* search for textOnly=, case insensitive, 0 is true ??? */
    textOnly=1;
  }


  /* open file */
  f = fopen(fname,"a");
  if(f==NULL) {
    if(fname!=NULL)free(fname);
    return(CMSG_ERROR);
  }
  if(fname!=NULL)free(fname);
  
  /* store file domain info */
  fdi = (fileDomainInfo*)malloc(sizeof(fileDomainInfo));
  if(fdi == NULL) {
    fclose(f);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  status = pthread_mutex_init(&fdi->mutex, NULL);
  if (status != 0) {
    cmsg_err_abort(status, "cmsg_file_connect: initializing mutex");
  }
  fdi->udl      = strdup("myUDL");
  fdi->domain   = strdup("file");
  fdi->host     = (char*)malloc(256);
  cMsgNetLocalHost(fdi->host,256);
  fdi->name     = strdup(myName);
  fdi->descr    = strdup(myDescription);
  fdi->file     = f;
  fdi->textOnly = textOnly;
  *domainId=(void*)fdi;


  /* done */
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * No server is involved so reconnect does nothing.
 */
int cmsg_file_reconnect(void *domainId) {
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine tells whether a file is open or not.
 *
 * @param domain id of the domain connection
 * @param connected pointer whose value is set to 1 if this file is open,
 *                  else it is set to 0
 *
 * @returns CMSG_OK
 */
int cmsg_file_isConnected(void *domainId, int *connected) {
  fileDomainInfo *fdi = (fileDomainInfo*)domainId;
  
  if (connected != NULL) {
    if (fdi == NULL) {
      *connected = 0;
    }
    else {
      *connected = 1;
    }
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_file_send(void *domainId, void *vmsg) {

  char *s;
  time_t now;
  char nowBuf[32];
  int stat;
  cMsgMessage_t  *msg = (cMsgMessage_t*)vmsg;
  fileDomainInfo *fdi = (fileDomainInfo*)domainId;
  
  if (fdi == NULL) return(CMSG_ERROR);

  /* keep sends and disconnect from interfering with eachother */
  mutexLock(&fdi->mutex);
  
  
  /* set domain */
  msg->domain=strdup(fdi->domain);


  /* set sender,senderTime,senderHost */
  msg->sender            = strdup(fdi->name);
  msg->senderHost        = strdup(fdi->host);
  msg->senderTime.tv_sec = time(NULL);
  

  /* write msg to file */
  if(fdi->textOnly!=0) {
    cMsgToString(vmsg,&s);
    stat = (int) fwrite(s,strlen(s),1,fdi->file);
    free(s);
  } else {
    now=time(NULL);
    ctime_r(&now,nowBuf);
    nowBuf[strlen(nowBuf)-1]='\0';
    s=(char*)malloc(strlen(nowBuf)+strlen(msg->text)+64);
    sprintf(s,"%s:    %s\n",nowBuf,msg->text);
    stat = (int) fwrite(s,strlen(s),1,fdi->file);
    free(s);
  }

  
  mutexUnlock(&fdi->mutex);

  
  /* done */
  return((stat!=0)?CMSG_OK:CMSG_ERROR);
}


/*-------------------------------------------------------------------*/


int cmsg_file_syncSend(void *domainId, void *vmsg, const struct timespec *timeout, int *response) {
  *response=0;
  return(cmsg_file_send(domainId, vmsg));
}


/*-------------------------------------------------------------------*/


int cmsg_file_subscribeAndGet(void *domainId, const char *subject, const char *type,
                              const struct timespec *timeout, void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_sendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout,
                      void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}

/*-------------------------------------------------------------------*/


/**
 * The monitor function is not implemented in the rc domain.
 */   
int cmsg_file_monitor(void *domainId, const char *command, void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_flush(void *domainId, const struct timespec *timeout) {
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_file_subscribe(void *domainId, const char *subject, const char *type,
                       cMsgCallbackFunc *callback, void *userArg,
                       cMsgSubscribeConfig *config, void **handle) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_unsubscribe(void *domainId, void *handle) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_subscriptionPause (void *domainId, void *handle) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_subscriptionResume(void *domainId, void *handle) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_subscriptionQueueClear(void *domainId, void *handle) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_subscriptionQueueCount(void *domainId, void *handle, int *count) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_subscriptionQueueIsFull(void *domainId, void *handle, int *full) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_subscriptionMessagesTotal(void *domainId, void *handle, int *total) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_start(void *domainId) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_stop(void *domainId) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_disconnect(void **domainId) {

  int stat, status;
  fileDomainInfo *fdi;

  if(domainId==NULL)return(CMSG_ERROR);
  if(*domainId==NULL)return(CMSG_ERROR);
  fdi = (fileDomainInfo*) (*domainId);

  /* keep sends and disconnect from interfering with eachother */
  mutexLock(&fdi->mutex);
  
  /* close file */
  stat = fclose(fdi->file);
  
  *domainId = NULL;

  mutexUnlock(&fdi->mutex);

  /* clean up */
  if(fdi->domain!=NULL) free(fdi->domain);
  if(fdi->host!=NULL)   free(fdi->host);
  if(fdi->name!=NULL)   free(fdi->name);
  if(fdi->descr!=NULL)  free(fdi->descr);
  status = pthread_mutex_destroy(&fdi->mutex);
  if (status != 0) {
    cmsg_err_abort(status, "cmsg_file_disconnect: destroying mutex");
  }  
  free(fdi);

  /* done */
  return((stat==0)?CMSG_OK:CMSG_ERROR);
}


/*-------------------------------------------------------------------*/
/*   shutdown handler functions                                      */
/*-------------------------------------------------------------------*/


int cmsg_file_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_shutdownClients(void *domainId, const char *client, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_shutdownServers(void *domainId, const char *server, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/

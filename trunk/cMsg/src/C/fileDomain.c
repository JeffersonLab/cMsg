// to do:
//   textOnly



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


#include "cMsgPrivate.h"
#include "cMsgBase.h"
#include "errors.h"
#include "cMsgNetwork.h"
#include "rwlock.h"


/* for c++ */
#ifdef __cplusplus
extern "C" {
#endif


/** Implementation of strdup() to cover vxWorks operating system. */
#ifdef VXWORKS
#include <vxWorks.h>
static char *strdup(const char *s1) {
    char *s;    
    if (s1 == NULL) return NULL;    
    if ((s = (char *) malloc(strlen(s1)+1)) == NULL) return NULL;    
    return strcpy(s, s1);
}
#endif



/* Prototypes of the 14 functions which implement the standard cMsg tasks in the cMsg domain. */
static int   fileConnect(const char *myUDL, const char *myName, const char *myDescription,
                         const char *UDLremainder, void **domainId);
static int   fileSend(void *domainId, void *msg);
static int   fileSyncSend(void *domainId, void *msg, int *response);
static int   fileFlush(void *domainId);
static int   fileSubscribe(void *domainId, const char *subject, const char *type, cMsgCallback *callback,
                           void *userArg, cMsgSubscribeConfig *config);
static int   fileUnsubscribe(void *domainId, const char *subject, const char *type, cMsgCallback *callback,
                              void *userArg);
static int   fileSubscribeAndGet(void *domainId, const char *subject, const char *type,
                                 const struct timespec *timeout, void **replyMsg);
static int   fileSendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout, void **replyMsg);
static int   fileStart(void *domainId);
static int   fileStop(void *domainId);
static int   fileDisconnect(void *domainId);
static int   fileShutdownClients(void *domainId, const char *client, int flag);
static int   fileShutdownServers(void *domainId, const char *server, int flag);
static int   fileSetShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg);


/** List of the functions which implement the standard cMsg tasks in the cMsg domain. */
static domainFunctions functions = { fileConnect, fileSend,
                                     fileSyncSend, fileFlush,
                                     fileSubscribe, fileUnsubscribe,
                                     fileSubscribeAndGet, fileSendAndGet,
                                     fileStart, fileStop, fileDisconnect,
                                     fileShutdownClients, fileShutdownServers,
                                     fileSetShutdownHandler
};


/* for registering the domain */
domainTypeInfo fileDomainTypeInfo = {"file",&functions};


/* local domain info */
typedef struct {
  char *domain;
  char *host;
  char *name;
  char *descr;
  FILE *file;
  int textOnly;
} fileDomainInfo;


/*-------------------------------------------------------------------*/


int fileConnect(const char *myUDL, const char *myName, const char *myDescription,
                       const char *UDLremainder, void **domainId) {


  char *fname;
  const char *c;
  int textOnly;
  fileDomainInfo *fdi;
  FILE *f;


  // get file name and textOnly attribute from UDLremainder
  if(UDLremainder==NULL)return(CMSG_ERROR);
  c = strchr(UDLremainder,'?');
  if(c==NULL) {
    fname=strdup(UDLremainder);
    textOnly=1;
  } else {
    int nc = c-UDLremainder+1;
    fname=(char*)malloc(nc+1);
    strncpy(fname,UDLremainder,nc);
    fname[nc]='\0';

    // search for textOnly=, case insensitive, 0 is true ???
    textOnly=1;
  }


  // open file
  f = fopen(fname,"a");
  if(f==NULL)return(CMSG_ERROR);

  
  // store file domain info
  fdi = (fileDomainInfo*)malloc(sizeof(fileDomainInfo));
  fdi->domain   = strdup("file");
  fdi->host     = (char*)malloc(256);
  cMsgLocalHost(fdi->host,256);
  fdi->name     = strdup(myName);
  fdi->descr    = strdup(myDescription);
  fdi->file     = f;
  fdi->textOnly = textOnly;
  *domainId=(void*)fdi;


  // done
  if(fname!=NULL)free(fname);
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int fileSend(void *domainId, void *vmsg) {

  char *s;
  time_t now;
  char nowBuf[32];
#ifdef VXWORKS
  size_t nowLen=sizeof(nowBuf);
#endif
  int stat;
  cMsgMessage *msg    = (cMsgMessage*)vmsg;
  fileDomainInfo *fdi = (fileDomainInfo*)domainId;
  

  // set domain
  msg->domain=strdup(fdi->domain);


  // check creator
  if(msg->creator==NULL)msg->creator=strdup(fdi->name);
    

  // set sender,senderTime,senderHost
  msg->sender            = strdup(fdi->name);
  msg->senderHost        = strdup(fdi->host);
  msg->senderTime.tv_sec = time(NULL);
  

  // write msg to file
  if(fdi->textOnly!=0) {
    cMsgToString(vmsg,&s);
    stat = fwrite(s,strlen(s),1,fdi->file);
    free(s);
  } else {
    now=time(NULL);
#ifdef VXWORKS
    ctime_r(&now,nowBuf,&nowLen);
#else
    ctime_r(&now,nowBuf);
#endif
    nowBuf[strlen(nowBuf)-1]='\0';
    s=(char*)malloc(strlen(nowBuf)+strlen(msg->text)+64);
    sprintf(s,"%s:    %s\n",nowBuf,msg->text);
    stat = fwrite(s,strlen(s),1,fdi->file);
    free(s);
  }


  // done
  return((stat!=0)?CMSG_OK:CMSG_ERROR);
}


/*-------------------------------------------------------------------*/


static int fileSyncSend(void *domainId, void *vmsg, int *response) {
  *response=0;
  return(fileSend(domainId,vmsg));
}


/*-------------------------------------------------------------------*/


static int fileSubscribeAndGet(void *domainId, const char *subject, const char *type,
                           const struct timespec *timeout, void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int fileSendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout,
                      void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int fileFlush(void *domainId) {  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int fileSubscribe(void *domainId, const char *subject, const char *type, cMsgCallback *callback,
                     void *userArg, cMsgSubscribeConfig *config) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int fileUnsubscribe(void *domainId, const char *subject, const char *type, cMsgCallback *callback,
                           void *userArg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int fileStart(void *domainId) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int fileStop(void *domainId) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int fileDisconnect(void *domainId) {

  int stat;
  fileDomainInfo *fdi;

  if(domainId==NULL)return(CMSG_ERROR);
  fdi = (fileDomainInfo*)domainId;

  // close file
  stat = fclose(fdi->file);
  
  // clean up
  if(fdi->domain!=NULL)free(fdi->domain);
  if(fdi->host!=NULL)free(fdi->host);
  if(fdi->name!=NULL)free(fdi->name);
  if(fdi->descr!=NULL)free(fdi->descr);
  free(fdi);

  // done
  return((stat==0)?CMSG_OK:CMSG_ERROR);
}


/*-------------------------------------------------------------------*/
/*   shutdown handler functions                                      */
/*-------------------------------------------------------------------*/


static int fileSetShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int fileShutdownClients(void *domainId, const char *client, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int fileShutdownServers(void *domainId, const char *server, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


#ifdef __cplusplus
}
#endif


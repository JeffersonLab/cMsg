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
#include "cMsg.h"
#include "errors.h"
#include "cMsgNetwork.h"
#include "rwlock.h"



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
int   cmsg_file_Connect(const char *myUDL, const char *myName, const char *myDescription,
                        const char *UDLremainder, void **domainId);
int   cmsg_file_Send(void *domainId, void *msg);
int   cmsg_file_SyncSend(void *domainId, void *msg, int *response);
int   cmsg_file_Flush(void *domainId);
int   cmsg_file_Subscribe(void *domainId, const char *subject, const char *type, cMsgCallbackFunc *callback,
                          void *userArg, cMsgSubscribeConfig *config, void **handle);
int   cmsg_file_Unsubscribe(void *domainId, void *handle);
int   cmsg_file_SubscribeAndGet(void *domainId, const char *subject, const char *type,
                                const struct timespec *timeout, void **replyMsg);
int   cmsg_file_SendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout, void **replyMsg);
int   cmsg_file_Start(void *domainId);
int   cmsg_file_Stop(void *domainId);
int   cmsg_file_Disconnect(void *domainId);
int   cmsg_file_ShutdownClients(void *domainId, const char *client, int flag);
int   cmsg_file_ShutdownServers(void *domainId, const char *server, int flag);
int   cmsg_file_SetShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg);


/** List of the functions which implement the standard cMsg tasks in the cMsg domain. */
static domainFunctions functions = { cmsg_file_Connect, cmsg_file_Send,
                                     cmsg_file_SyncSend, cmsg_file_Flush,
                                     cmsg_file_Subscribe, cmsg_file_Unsubscribe,
                                     cmsg_file_SubscribeAndGet, cmsg_file_SendAndGet,
                                     cmsg_file_Start, cmsg_file_Stop, cmsg_file_Disconnect,
                                     cmsg_file_ShutdownClients, cmsg_file_ShutdownServers,
                                     cmsg_file_SetShutdownHandler
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


int cmsg_file_Connect(const char *myUDL, const char *myName, const char *myDescription,
                      const char *UDLremainder, void **domainId) {


  char *fname;
  const char *c;
  int textOnly;
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
  if(f==NULL)return(CMSG_ERROR);

  
  /* store file domain info */
  fdi = (fileDomainInfo*)malloc(sizeof(fileDomainInfo));
  fdi->domain   = strdup("file");
  fdi->host     = (char*)malloc(256);
  cMsgLocalHost(fdi->host,256);
  fdi->name     = strdup(myName);
  fdi->descr    = strdup(myDescription);
  fdi->file     = f;
  fdi->textOnly = textOnly;
  *domainId=(void*)fdi;


  /* done */
  if(fname!=NULL)free(fname);
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_file_Send(void *domainId, void *vmsg) {

  char *s;
  time_t now;
  char nowBuf[32];
#ifdef VXWORKS
  size_t nowLen=sizeof(nowBuf);
#endif
  int stat;
  cMsgMessage_t  *msg = (cMsgMessage_t*)vmsg;
  fileDomainInfo *fdi = (fileDomainInfo*)domainId;
  

  /* set domain */
  msg->domain=strdup(fdi->domain);


  /* check creator */
  if(msg->creator==NULL)msg->creator=strdup(fdi->name);
    

  /* set sender,senderTime,senderHost */
  msg->sender            = strdup(fdi->name);
  msg->senderHost        = strdup(fdi->host);
  msg->senderTime.tv_sec = time(NULL);
  

  /* write msg to file */
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


  /* done */
  return((stat!=0)?CMSG_OK:CMSG_ERROR);
}


/*-------------------------------------------------------------------*/


int cmsg_file_SyncSend(void *domainId, void *vmsg, int *response) {
  *response=0;
  return(cmsg_file_Send(domainId,vmsg));
}


/*-------------------------------------------------------------------*/


int cmsg_file_SubscribeAndGet(void *domainId, const char *subject, const char *type,
                           const struct timespec *timeout, void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_SendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout,
                      void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_Flush(void *domainId) {  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_file_Subscribe(void *domainId, const char *subject, const char *type, cMsgCallbackFunc *callback,
                     void *userArg, cMsgSubscribeConfig *config, void **handle) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_Unsubscribe(void *domainId, void *handle) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_Start(void *domainId) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_Stop(void *domainId) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_Disconnect(void *domainId) {

  int stat;
  fileDomainInfo *fdi;

  if(domainId==NULL)return(CMSG_ERROR);
  fdi = (fileDomainInfo*)domainId;

  /* close file */
  stat = fclose(fdi->file);
  
  /* clean up */
  if(fdi->domain!=NULL) free(fdi->domain);
  if(fdi->host!=NULL)   free(fdi->host);
  if(fdi->name!=NULL)   free(fdi->name);
  if(fdi->descr!=NULL)  free(fdi->descr);
  free(fdi);

  /* done */
  return((stat==0)?CMSG_OK:CMSG_ERROR);
}


/*-------------------------------------------------------------------*/
/*   shutdown handler functions                                      */
/*-------------------------------------------------------------------*/


int cmsg_file_SetShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_ShutdownClients(void *domainId, const char *client, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_file_ShutdownServers(void *domainId, const char *server, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*
 *
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    E.Wolin, 15-Jul-2004, Jefferson Lab                                     *
 *                                                                            *
 *    Authors: Elliott Wolin                                                  *
 *             wolin@jlab.org                    Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-7365             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *             Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *
 *  Implements cMsg client api and dispatches to multiple domains
 *  Includes all message functions
 *
 *
 * still to do:
 *     move cMsgDomainClear() to this file but careful about locking (also change name and scope)
 *     careful of return(err) 
 *     perror must include all cMsg.h error codes
 *
 *
 *----------------------------------------------------------------------------*/


/* system includes */
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <pthread.h>


/* package includes */
#include "cMsgPrivate.h"
#include "cMsg.h"

/*
 * MAXHOSTNAMELEN is defined to be 256 on Solaris and 64 on Linux.
 * Make it to be uniform across all platforms.
 */
#define CMSG_MAXHOSTNAMELEN 256

/* set the "global" debug level */
int cMsgDebug = CMSG_DEBUG_ERROR;


/* local variables */
static int oneTimeInitialized = 0;
static pthread_mutex_t connectMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t generalMutex = PTHREAD_MUTEX_INITIALIZER;
static domainTypeInfo dTypeInfo[MAXDOMAINS];
static cMsgDomain domains[MAXDOMAINS];


/* excluded chars */
static char *excludedChars = "`\'\"";


/* for domain implementations */
extern domainTypeInfo codaDomainTypeInfo;


/* for c++ */
#ifdef __cplusplus
extern "C" {
#endif


/* local prototypes */
static int   checkString(char *s);
static void  registerDomainTypeInfo(void);
static void  domainInit(cMsgDomain *domain);
static void  domainFree(cMsgDomain *domain);
static int   parseUDL(const char *UDL, char **domainType);
static void  mutexLock(void);
static void  mutexUnlock(void);
static void  connectMutexLock(void);
static void  connectMutexUnlock(void);
static void  cMsgDomainClear(cMsgDomain *domain);



/*-------------------------------------------------------------------*/


int cMsgConnect(char *myUDL, char *myName, char *myDescription, int *domainId) {

  int i, id=-1, err, serverfd, status, realId;
  char *portEnvVariable=NULL, temp[CMSG_MAXHOSTNAMELEN];
  

  /* First, grab mutex for thread safety. This mutex must be held until
   * the initialization is completely finished. Otherwise, if we set
   * initComplete = 1 (so that we reserve space in the domains array)
   * before it's finished and then release the mutex, we may give an
   * "existing" connection to a user who does a second init
   * when in fact, an error may still occur in that "existing"
   * connection. Hope you caught that.
   */
  connectMutexLock();
  

  /* do one time initialization */
  if (!oneTimeInitialized) {

    /* clear arrays */
    for (i=0; i<MAXDOMAINTYPES; i++) dTypeInfo[i].type = NULL;
    for (i=0; i<MAXDOMAINS; i++) domainInit(&domains[i]);

    /* register domain types */
    registerDomainTypeInfo();

    oneTimeInitialized = 1;
  }

  
  /* find an existing connection to this domain if possible */
  for (i=0; i<MAXDOMAINS; i++) {
    if (domains[i].initComplete == 1   &&
        domains[i].name        != NULL &&
        domains[i].udl         != NULL &&
        domains[i].description != NULL)  {
        
      if ( (strcmp(domains[i].name,        myName       ) == 0)  &&
           (strcmp(domains[i].udl,         myUDL        ) == 0)  &&
           (strcmp(domains[i].description, myDescription) == 0) )  {
        /* got a match */
        id = i;
        break;
      }  
    }
  }
  

  /* found the id of a valid connection - return that */
  if (id > -1) {
    *domainId = id + DOMAIN_ID_OFFSET;
    connectMutexUnlock();
    return(CMSG_OK);
  }
  

  /* no existing connection, so find the first available place in the "domains" array */
  for (i=0; i<MAXDOMAINS; i++) {
    if (domains[i].initComplete > 0) {
      continue;
    }
    domainInit(&domains[i]);
    id = i;
  }
  

  /* exceeds number of domain connections allowed */
  if (id < 0) {
    connectMutexUnlock();
    return(CMSG_LIMIT_EXCEEDED);
  }


  /* reserve this element of the "domains" array */
  domains[id].initComplete = 1;
      
  /* check args */
  if ( (checkString(myName)        != CMSG_OK)  ||
       (checkString(myUDL)         != CMSG_OK)  ||
       (checkString(myDescription) != CMSG_OK) )  {
    domainInit(&domains[i]);
    connectMutexUnlock();
    return(CMSG_BAD_ARGUMENT);
  }
    

  /* store names, can be changed until server connection established */
  domains[id].name        = (char *) strdup(myName);
  domains[id].udl         = (char *) strdup(myUDL);
  domains[id].description = (char *) strdup(myDescription);
  
  /* parse the UDL - Uniform Domain Locator */
  if ( (err = parseUDL(myUDL, &domains[id].type)) != CMSG_OK ) {
    /* there's been a parsing error */
    cMsgDomainClear(&domains[id]);
    connectMutexUnlock();
    return(err);
  }
  

  /* if such a domain type store pointer to functions */
  domains[id].functions = NULL;
  for (i=0; i<MAXDOMAINTYPES; i++) {
    if (dTypeInfo[i].type != NULL) {
      if (strcasecmp(dTypeInfo[i].type, domains[id].type) == 0) {
	domains[id].functions = dTypeInfo[i].functions;
	break;
      }
    }
  }
  if(domains[id].functions == NULL) return(CMSG_BAD_DOMAIN_TYPE);
  

  /* dispatch to connect function registered for this domain type */
  err = domains[id].functions->connect(myUDL, myName, myDescription, &realId);
  domains[id].id = realId;
  
  connectMutexUnlock();
  
  return err;
}


/*-------------------------------------------------------------------*/


int cMsgSend(int domainId, void *msg) {
  
  int id = domainId - DOMAIN_ID_OFFSET;
  
  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[id].lostConnection == 1) return(CMSG_LOST_CONNECTION);
  if (msg==NULL)return(CMSG_BAD_ARGUMENT);


  /* dispatch to function registered for this domain type */
  return(domains[id].functions->send(domains[id].id, msg));
}


/*-------------------------------------------------------------------*/


int cMsgFlush(int domainId) {

  int id = domainId - DOMAIN_ID_OFFSET;

  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[id].lostConnection == 1) return(CMSG_LOST_CONNECTION);
  

  /* dispatch to function registered for this domain type */
  return(domains[id].functions->flush(domains[id].id));
}


/*-------------------------------------------------------------------*/


int cMsgSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback, void *userArg) {

  int id = domainId - DOMAIN_ID_OFFSET;

  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[id].lostConnection == 1) return(CMSG_LOST_CONNECTION);

  /* check args */
  if( (checkString(subject) !=0 )  ||
      (checkString(type)    !=0 )  ||
      (callback == NULL)          )  {
    return(CMSG_BAD_ARGUMENT);
  }
  

  /* dispatch to function registered for this domain type */
  return(domains[id].functions->subscribe(domains[id].id, subject, type, callback, userArg));
}


/*-------------------------------------------------------------------*/


int cMsgUnSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback) {

  int id = domainId - DOMAIN_ID_OFFSET;


  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[id].lostConnection == 1) return(CMSG_LOST_CONNECTION);


  /* check args */
  if( (checkString(subject) != 0)  ||
      (checkString(type)    != 0)  ||
      (callback == NULL)          )  {
    return(CMSG_BAD_ARGUMENT);
  }
  

  /* dispatch to function registered for this domain type */
  return(domains[id].functions->unsubscribe(domains[id].id, subject, type, callback));
} 


/*-------------------------------------------------------------------*/


int cMsgGet(int domainId, void *sendMsg, time_t timeout, void **replyMsg) {

  int id = domainId - DOMAIN_ID_OFFSET;

  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[id].lostConnection == 1) return(CMSG_LOST_CONNECTION);
  if (sendMsg==NULL)return(CMSG_BAD_ARGUMENT);

  return(domains[id].functions->get(domains[id].id, sendMsg, timeout, replyMsg));
}


/*-------------------------------------------------------------------*/


int cMsgReceiveStart(int domainId) {
  int id = domainId - DOMAIN_ID_OFFSET;

  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[id].lostConnection == 1) return(CMSG_LOST_CONNECTION);

  domains[id].receiveState = 1;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgReceiveStop(int domainId) {
  int id = domainId - DOMAIN_ID_OFFSET;

  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (domains[id].lostConnection == 1) return(CMSG_LOST_CONNECTION);

  domains[id].receiveState = 0;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgDisconnect(int domainId) {
  
  int id = domainId - DOMAIN_ID_OFFSET;

  if (domains[id].initComplete != 1) return(CMSG_NOT_INITIALIZED);
  

  /* dispatch to function registered for this domain type */
  return(domains[id].functions->disconnect(domains[id].id));
}


/*-------------------------------------------------------------------*/


char *cMsgPerror(int error) {

  static char temp[256];


  switch(error) {

  case CMSG_OK:
    sprintf(temp, "CMSG_OK:  action completed successfully\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_OK:  action completed successfully\n");
    break;

  case CMSG_ERROR:
    sprintf(temp, "CMSG_ERROR:  generic error return\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_ERROR:  generic error return\n");
    break;

  case CMSG_TIMEOUT:
    sprintf(temp, "CMSG_TIMEOUT:  no response from cMsg server within timeout period\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_TIMEOUT:  no response from cMsg server within timeout period\n");
    break;

  case CMSG_NOT_IMPLEMENTED:
    sprintf(temp, "CMSG_NOT_IMPLEMENTED:  function not implemented\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_NOT_IMPLEMENTED:  function not implemented\n");
    break;

  case CMSG_BAD_ARGUMENT:
    sprintf(temp, "CMSG_BAD_ARGUMENT:  one or more arguments bad\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_BAD_ARGUMENT:  one or more arguments bad\n");
    break;

  case CMSG_BAD_FORMAT:
    sprintf(temp, "CMSG_BAD_FORMAT:  one or more arguments in the wrong format\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_BAD_FORMAT:  one or more arguments in the wrong format\n");
    break;

  case CMSG_NAME_EXISTS:
    sprintf(temp, "CMSG_NAME_EXISTS: another process in this domain is using this name\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_NAME_EXISTS: another process in this domain is using this name\n");
    break;

  case CMSG_NOT_INITIALIZED:
    sprintf(temp, "CMSG_NOT_INITIALIZED:  cMsgConnect needs to be called\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_NOT_INITIALIZED:  cMsgConnect needs to be called\n");
    break;

  case CMSG_ALREADY_INIT:
    sprintf(temp, "CMSG_ALREADY_INIT:  cMsgConnect already called\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_ALREADY_INIT:  cMsgConnect already called\n");
    break;

  case CMSG_LOST_CONNECTION:
    sprintf(temp, "CMSG_LOST_CONNECTION:  connection to cMsg server lost\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_LOST_CONNECTION:  connection to cMsg server lost\n");
    break;

  case CMSG_NETWORK_ERROR:
    sprintf(temp, "CMSG_NETWORK_ERROR:  error talking to cMsg server\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_NETWORK_ERROR:  error talking to cMsg server\n");
    break;

  case CMSG_SOCKET_ERROR:
    sprintf(temp, "CMSG_NETWORK_ERROR:  error setting socket options\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_NETWORK_ERROR:  error setting socket options\n");
    break;

  case CMSG_PEND_ERROR:
    sprintf(temp, "CMSG_PEND_ERROR:  error waiting for messages to arrive\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_PEND_ERROR:  error waiting for messages to arrive\n");
    break;

  case CMSG_ILLEGAL_MSGTYPE:
    sprintf(temp, "CMSG_ILLEGAL_MSGTYPE:  pend received illegal message type\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_ILLEGAL_MSGTYPE:  pend received illegal message type\n");
    break;

  case CMSG_OUT_OF_MEMORY:
    sprintf(temp, "CMSG_OUT_OF_MEMORY:  ran out of memory\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_OUT_OF_MEMORY:  ran out of memory\n");
    break;

  case CMSG_OUT_OF_RANGE:
    sprintf(temp, "CMSG_OUT_OF_RANGE:  argument is out of range\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_OUT_OF_RANGE:  argument is out of range\n");
    break;

  case CMSG_LIMIT_EXCEEDED:
    sprintf(temp, "CMSG_LIMIT_EXCEEDED:  trying to create too many of something\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_LIMIT_EXCEEDED:  trying to create too many of something\n");
    break;

  case CMSG_WRONG_DOMAIN_TYPE:
    sprintf(temp, "CMSG_WRONG_DOMAIN_TYPE: when a UDL does not match the server type\n");
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("CMSG_WRONG_DOMAIN_TYPE: when a UDL does not match the server type\n");
    break;

  default:
    sprintf(temp, "?cMsgPerror...no such error: %d\n",error);
    if(cMsgDebug>CMSG_DEBUG_ERROR)printf("?cMsgPerror...no such error: %d\n",error);
    break;
  }

  return(temp);
}


/*-------------------------------------------------------------------*/


int cMsgSetDebugLevel(int level) {
  
  cMsgDebug = level;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static void cMsgDomainClear(cMsgDomain *domain) {
  domainFree(domain);
  domainInit(domain);
}


/*-------------------------------------------------------------------*
 *
 * Internal functions
 *
 *-------------------------------------------------------------------*/


static void registerDomainTypeInfo(void) {

  /* coda type */
  dTypeInfo[0].type = (char *)strdup(codaDomainTypeInfo.type); 
  dTypeInfo[0].functions = codaDomainTypeInfo.functions;

  return;
}


/*-------------------------------------------------------------------*/


static void domainInit(cMsgDomain *domain) {  
  domain->initComplete   = 0;
  
  domain->receiveState   = 0;
  domain->lostConnection = 0;
      
  domain->name           = NULL;
  domain->udl            = NULL;
  domain->description    = NULL;
  domain->functions      = NULL;  
}


/*-------------------------------------------------------------------*/


static void domainFree(cMsgDomain *domain) {  
  if (domain->name        != NULL) free(domain->name);
  if (domain->udl         != NULL) free(domain->udl);
  if (domain->description != NULL) free(domain->description);
}


/*-------------------------------------------------------------------*
 * Mutex functions
 *-------------------------------------------------------------------*/


static void connectMutexLock(void) {
  int status = pthread_mutex_lock(&connectMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static void connectMutexUnlock(void) {
  int status = pthread_mutex_unlock(&connectMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/


static void mutexLock(void) {
  int status = pthread_mutex_lock(&generalMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


static void mutexUnlock(void) {
  int status = pthread_mutex_unlock(&generalMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/
/*   miscellaneous local functions                                   */
/*-------------------------------------------------------------------*/


static int parseUDL(const char *UDL, char **domainType) {

/* note:  CODA domain UDL is of the form:   domainType://somethingDomainSpecific */

  int i;
  char *p, *portString, *udl;

  if (UDL  == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  /* strtok modifies the string it tokenizes, so make a copy */
  udl = (char *) strdup(UDL);
  
/*printf("UDL = %s\n", udl);*/
  /* get tokens separated by ":" or "/" */
  if ( (p = (char *) strtok(udl, ":/")) == NULL) {
    free(udl);
    return (CMSG_BAD_FORMAT);
  }
  if (domainType != NULL) *domainType = (char *) strdup(p);
/*printf("domainType = %s\n", *domainType);*/
    
  /* UDL parsed ok */
  free(udl);
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int checkString(char *s) {

  int i;

  if (s == NULL) return(CMSG_ERROR);

  /* check for printable character */
  for(i=0; i<strlen(s); i++) {
    if (isprint(s[i]) == 0) return(CMSG_ERROR);
  }

  /* check for excluded chars */
  if (strpbrk(s,excludedChars) != 0) return(CMSG_ERROR);
  
  /* string ok */
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/*   system accessor functions                                       */
/*-------------------------------------------------------------------*/


int cMsgGetUDL(int domainId, char *udl, size_t size) {

  int id  = domainId - DOMAIN_ID_OFFSET;
  int len = strlen(domains[id].udl);

  if (id < 0 || id >= MAXDOMAINS) return(CMSG_BAD_DOMAIN_ID);

  if (size>len) {
    strcpy(udl,domains[id].udl);
    return(CMSG_OK);
  } else {
    strncpy(udl,domains[id].udl,size-1);
    udl[size-1]='\0';
    return(CMSG_LIMIT_EXCEEDED);
  }
}
  
  
/*-------------------------------------------------------------------*/


int cMsgGetName(int domainId, char *name, size_t size) {

  int id  = domainId - DOMAIN_ID_OFFSET;
  int len = strlen(domains[id].name);

  if (id < 0 || id >= MAXDOMAINS) return(CMSG_BAD_DOMAIN_ID);

  if (size>len) {
    strcpy(name,domains[id].name);
    return(CMSG_OK);
  } else {
    strncpy(name,domains[id].name,size-1);
    name[size-1]='\0';
    return(CMSG_LIMIT_EXCEEDED);
  }
}

/*-------------------------------------------------------------------*/


int cMsgGetDescription(int domainId, char *description, size_t size) {

  int id  = domainId - DOMAIN_ID_OFFSET;
  int len = strlen(domains[id].description);

  if (id < 0 || id >= MAXDOMAINS) return(CMSG_BAD_DOMAIN_ID);

  if (size>len) {
    strcpy(description,domains[id].description);
    return(CMSG_OK);
  } else {
    strncpy(description,domains[id].description,size-1);
    description[size-1]='\0';
    return(CMSG_LIMIT_EXCEEDED);
  }
}


/*-------------------------------------------------------------------*/


int cMsgGetInitState(int domainId, int *initState) {

  int id = domainId - DOMAIN_ID_OFFSET;

  if (id < 0 || id >= MAXDOMAINS) return(CMSG_BAD_DOMAIN_ID);

  *initState=domains[id].initComplete;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgGetReceiveState(int domainId, int *receiveState) {

  int id = domainId - DOMAIN_ID_OFFSET;

  if (id < 0 || id >= MAXDOMAINS) return(CMSG_BAD_DOMAIN_ID);

  *receiveState=domains[id].receiveState;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/*   message accessor functions                                      */
/*-------------------------------------------------------------------*/


void *cMsgCreateMesssage(void) {

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgSetSubject(void *vmsg, char *subject) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->subject != NULL) free(msg->subject);
  msg->subject = (char *)strdup(subject);

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgSetType(void *vmsg, char *type) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->type != NULL) free(msg->type);
  msg->type = (char *)strdup(type);

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgSetText(void *vmsg, char *text) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->text != NULL) free(msg->text);
  msg->text = (char *)strdup(text);

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgSetSenderToken(void *vmsg, int senderToken) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  msg->senderToken = senderToken;

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgSetSender(void *vmsg, char *sender) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->sender != NULL) free(msg->sender);
  msg->sender = (char *)strdup(sender);

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgFreeMessage(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);

  free(msg->sender);
  free(msg->senderHost);
  free(msg->receiver);
  free(msg->receiverHost);
  free(msg->domain);
  free(msg->subject);
  free(msg->type);
  free(msg->text);
  free(msg);

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


  void *cMsgCopyMessage(void *vmsg) {
    cMsgMessage *newMsg, *msg = (cMsgMessage *)vmsg;
    
    if ((newMsg = (cMsgMessage *)malloc(sizeof(cMsgMessage))) == NULL) {
      return NULL;
    }
    
    newMsg->sysMsgId     = msg->sysMsgId;

    newMsg->sender       = (char *) strdup(msg->sender);
    newMsg->senderId     = msg->senderId;
    newMsg->senderHost   = (char *) strdup(msg->senderHost);
    newMsg->senderTime   = msg->senderTime;
    newMsg->senderMsgId  = msg->senderMsgId;
    newMsg->senderToken  = msg->senderToken;

    newMsg->receiver     = (char *) strdup(msg->receiver);
    newMsg->receiverHost = (char *) strdup(msg->receiverHost);
    newMsg->receiverTime = msg->receiverTime;
    newMsg->receiverSubscribeId = msg->receiverSubscribeId;

    newMsg->domain  = (char *) strdup(msg->domain);
    newMsg->subject = (char *) strdup(msg->subject);
    newMsg->type    = (char *) strdup(msg->type);
    newMsg->text    = (char *) strdup(msg->text);
    
    return (void *)newMsg;
  }


/*-------------------------------------------------------------------*/


int cMsgGetSysMsgId(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(-1);
  return(msg->sysMsgId);
}


/*-------------------------------------------------------------------*/


time_t cMsgGetReceiverTime(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(-1);
  return(msg->receiverTime);
}


/*-------------------------------------------------------------------*/


int cMsgGetReceiverSubscribeId(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;
 
  if (msg == NULL) return(-1);
  return(msg->receiverSubscribeId);
}


/*-------------------------------------------------------------------*/


char *cMsgGetSender(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(NULL);
  return(msg->sender);
}


/*-------------------------------------------------------------------*/


int cMsgGetSenderId(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(-1);
  return(msg->senderId);
}


/*-------------------------------------------------------------------*/


char *cMsgGetSenderHost(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(NULL);
  return(msg->senderHost);
}


/*-------------------------------------------------------------------*/


time_t cMsgGetSenderTime(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(-1);
  return(msg->senderTime);
}


/*-------------------------------------------------------------------*/


int cMsgGetSenderMsgId(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(-1);
  return(msg->senderMsgId);
}


/*-------------------------------------------------------------------*/


int cMsgGetSenderToken(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(-1);
  return(msg->senderToken);
}


/*-------------------------------------------------------------------*/


char *cMsgGetDomain(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(NULL);
  return(msg->domain);
}


/*-------------------------------------------------------------------*/


char *cMsgGetSubject(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(NULL);
  return(msg->subject);
}


/*-------------------------------------------------------------------*/


char *cMsgGetType(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(NULL);
  return(msg->type);
}


/*-------------------------------------------------------------------*/


char *cMsgGetText(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(NULL);
  return(msg->text);
}


/*-------------------------------------------------------------------*/


#ifdef __cplusplus
}
#endif


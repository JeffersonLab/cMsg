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
 *----------------------------------------------------------------------------*/


/* system includes */
#ifdef VXWORKS
#include <vxWorks.h>
#include <taskLib.h>
#else
#include <strings.h>
#endif

#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>

/* package includes */
#include "errors.h"
#include "cMsg.h"
#include "cMsgPrivate.h"

/*
 * MAXHOSTNAMELEN is defined to be 256 on Solaris and 64 on Linux.
 * Make it to be uniform across all platforms.
 */
#define CMSG_MAXHOSTNAMELEN 256

/* set the "global" debug level */
int cMsgDebug = CMSG_DEBUG_NONE;


/* local variables */
static int oneTimeInitialized = 0;
static pthread_mutex_t connectMutex = PTHREAD_MUTEX_INITIALIZER;
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
static int   parseUDL(const char *UDL, char **domainType, char **UDLremainder);
static void  connectMutexLock(void);
static void  connectMutexUnlock(void);
static void  domainClear(cMsgDomain *domain);
static void  initMessage(cMsgMessage *msg);

#ifdef VXWORKS

static char *strdup(const char *s1) {
    char *s;    
    if (s1 == NULL) return NULL;    
    if ((s = (char *) malloc(strlen(s1)+1)) == NULL) return NULL;    
    return strcpy(s, s1);
}

static int strcasecmp(const char *s1, const char *s2) {
  int i, len1, len2;
  
  /* handle NULL's */
  if (s1 == NULL && s2 == NULL) {
    return 0;
  }
  else if (s1 == NULL) {
    return -1;  
  }
  else if (s2 == NULL) {
    return 1;  
  }
  
  len1 = strlen(s1);
  len2 = strlen(s2);
  
  /* handle different lengths */
  if (len1 < len2) {
    for (i=0; i<len1; i++) {
      if (toupper((int) s1[i]) < toupper((int) s2[i])) {
        return -1;
      }
      else if (toupper((int) s1[i]) > toupper((int) s2[i])) {
         return 1;   
      }
    }
    return -1;
  }
  else if (len1 > len2) {
    for (i=0; i<len2; i++) {
      if (toupper((int) s1[i]) < toupper((int) s2[i])) {
        return -1;
      }
      else if (toupper((int) s1[i]) > toupper((int) s2[i])) {
         return 1;   
      }
    }
    return 1;  
  }
  
  /* handle same lengths */
  for (i=0; i<len1; i++) {
    if (toupper((int) s1[i]) < toupper((int) s2[i])) {
      return -1;
    }
    else if (toupper((int) s1[i]) > toupper((int) s2[i])) {
       return 1;   
    }
  }
  
  return 0;
}

#endif



/*-------------------------------------------------------------------*/


int cMsgConnect(char *myUDL, char *myName, char *myDescription, int *domainId) {

  int i, id=-1, err, implId;
  
  /* check args */
  if ( (checkString(myName)        != CMSG_OK) ||
       (checkString(myUDL)         != CMSG_OK) ||
       (checkString(myDescription) != CMSG_OK) ||
       (domainId                   == NULL   ))  {
    return(CMSG_BAD_ARGUMENT);
  }
      
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
        domains[i].udl         != NULL)  {
        
      if ( (strcmp(domains[i].name, myName) == 0)  &&
           (strcmp(domains[i].udl,  myUDL ) == 0) )  {
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
    domainClear(&domains[i]);
    id = i;
    break;
  }
  

  /* exceeds number of domain connections allowed */
  if (id < 0) {
    connectMutexUnlock();
    return(CMSG_LIMIT_EXCEEDED);
  }


  /* reserve this element of the "domains" array */
  domains[id].initComplete = 1;
      
  /* save ref to self */
  domains[id].id = id;
      
  /* store names, can be changed until server connection established */
  domains[id].name        = (char *) strdup(myName);
  domains[id].udl         = (char *) strdup(myUDL);
  domains[id].description = (char *) strdup(myDescription);
  
  /* parse the UDL - Uniform Domain Locator */
  if ( (err = parseUDL(myUDL, &domains[id].type, &domains[id].UDLremainder)) != CMSG_OK ) {
    /* there's been a parsing error */
    domainClear(&domains[id]);
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
  if (domains[id].functions == NULL) return(CMSG_BAD_DOMAIN_TYPE);
  

  /* dispatch to connect function registered for this domain type */
  err = domains[id].functions->connect(myUDL, myName, myDescription,
                                       domains[id].UDLremainder, &implId);

  if (err != CMSG_OK) {
    domainClear(&domains[id]);
    connectMutexUnlock();
    return err;
  }  
  
  domains[id].implId = implId;
  *domainId = id + DOMAIN_ID_OFFSET;
  
  connectMutexUnlock();
  
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/


int cMsgSend(int domainId, void *msg) {
  
  int id = domainId - DOMAIN_ID_OFFSET;
  
  if (domains[id].initComplete != 1) return(CMSG_NOT_INITIALIZED);
  if (msg == NULL)                   return(CMSG_BAD_ARGUMENT);

  /* dispatch to function registered for this domain type */
  return(domains[id].functions->send(domains[id].implId, msg));
}


/*-------------------------------------------------------------------*/


int cMsgSyncSend(int domainId, void *msg, int *response) {
  
  int id = domainId - DOMAIN_ID_OFFSET;
  
  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  if (msg == NULL || response == NULL) return(CMSG_BAD_ARGUMENT);

  /* dispatch to function registered for this domain type */
  return(domains[id].functions->syncSend(domains[id].implId, msg, response));
}


/*-------------------------------------------------------------------*/


int cMsgFlush(int domainId) {

  int id = domainId - DOMAIN_ID_OFFSET;

  if (domains[id].initComplete != 1) return(CMSG_NOT_INITIALIZED);
  

  /* dispatch to function registered for this domain type */
  return(domains[id].functions->flush(domains[id].implId));
}


/*-------------------------------------------------------------------*/


int cMsgSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback,
                  void *userArg, cMsgSubscribeConfig *config) {

  int id = domainId - DOMAIN_ID_OFFSET;

  if (domains[id].initComplete != 1) return(CMSG_NOT_INITIALIZED);

  /* check args */
  if ( (checkString(subject) !=0 )  ||
       (checkString(type)    !=0 )  ||
       (callback == NULL)          )  {
    return(CMSG_BAD_ARGUMENT);
  }
  
  /* dispatch to function registered for this domain type */
  return(domains[id].functions->subscribe(domains[id].implId, subject, type, callback,
                                          userArg, config));
}


/*-------------------------------------------------------------------*/


int cMsgUnSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback) {

  int id = domainId - DOMAIN_ID_OFFSET;


  if (domains[id].initComplete != 1) return(CMSG_NOT_INITIALIZED);


  /* check args */
  if ( (checkString(subject) !=0 )  ||
       (checkString(type)    !=0 )  ||
       (callback == NULL)          )  {
    return(CMSG_BAD_ARGUMENT);
  }
  

  /* dispatch to function registered for this domain type */
  return(domains[id].functions->unsubscribe(domains[id].implId, subject, type, callback));
} 


/*-------------------------------------------------------------------*/


int cMsgSendAndGet(int domainId, void *sendMsg, struct timespec *timeout, void **replyMsg) {

  int id = domainId - DOMAIN_ID_OFFSET;
  cMsgMessage *msg;

  if (domains[id].initComplete != 1)       return(CMSG_NOT_INITIALIZED);
  if (sendMsg == NULL || replyMsg == NULL) return(CMSG_BAD_ARGUMENT);
  
  msg = (cMsgMessage *)sendMsg;
  
  /* check msg fields */
  if ( (checkString(msg->subject) !=0 ) ||
       (checkString(msg->type)    !=0 ))  {
    return(CMSG_BAD_ARGUMENT);
  }

  return(domains[id].functions->sendAndGet(domains[id].implId, sendMsg, timeout, replyMsg));
}


/*-------------------------------------------------------------------*/


int cMsgSubscribeAndGet(int domainId, char *subject, char *type,
                        struct timespec *timeout, void **replyMsg) {

  int id = domainId - DOMAIN_ID_OFFSET;

  if (domains[id].initComplete != 1) return(CMSG_NOT_INITIALIZED);
  if (replyMsg == NULL)              return(CMSG_BAD_ARGUMENT);
  
  /* check args */
  if ( (checkString(subject) !=0 ) ||
       (checkString(type)    !=0 ))  {
    return(CMSG_BAD_ARGUMENT);
  }

  return(domains[id].functions->subscribeAndGet(domains[id].implId, subject, type,
                                                timeout, replyMsg));
}


/*-------------------------------------------------------------------*/


int cMsgReceiveStart(int domainId) {

  int id = domainId - DOMAIN_ID_OFFSET;
  int err;

  if (domains[id].initComplete != 1) return(CMSG_NOT_INITIALIZED);
  
  if ( (err = domains[id].functions->start(domains[id].implId)) != CMSG_OK) {
    return err;
  }
  
  domains[id].receiveState = 1;
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgReceiveStop(int domainId) {

  int id = domainId - DOMAIN_ID_OFFSET;
  int err;

  if (domains[id].initComplete != 1) return(CMSG_NOT_INITIALIZED);

  if ( (err = domains[id].functions->stop(domains[id].implId)) != CMSG_OK) {
    return err;
  }
  
  domains[id].receiveState = 0;
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgDisconnect(int domainId) {
  
  int id = domainId - DOMAIN_ID_OFFSET;
  int err;

  if (domains[id].initComplete != 1) return(CMSG_NOT_INITIALIZED);
  
  connectMutexLock();
  if ( (err = domains[id].functions->disconnect(domains[id].implId)) != CMSG_OK) {
    connectMutexUnlock();
    return err;
  }
  
  domains[id].initComplete = 0;
  
  connectMutexUnlock();
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


char *cMsgPerror(int error) {

  static char temp[256];

  switch(error) {

  case CMSG_OK:
    sprintf(temp, "CMSG_OK:  action completed successfully\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_OK:  action completed successfully\n");
    break;

  case CMSG_ERROR:
    sprintf(temp, "CMSG_ERROR:  generic error return\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_ERROR:  generic error return\n");
    break;

  case CMSG_TIMEOUT:
    sprintf(temp, "CMSG_TIMEOUT:  no response from cMsg server within timeout period\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_TIMEOUT:  no response from cMsg server within timeout period\n");
    break;

  case CMSG_NOT_IMPLEMENTED:
    sprintf(temp, "CMSG_NOT_IMPLEMENTED:  function not implemented\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_NOT_IMPLEMENTED:  function not implemented\n");
    break;

  case CMSG_BAD_ARGUMENT:
    sprintf(temp, "CMSG_BAD_ARGUMENT:  one or more arguments bad\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_BAD_ARGUMENT:  one or more arguments bad\n");
    break;

  case CMSG_BAD_FORMAT:
    sprintf(temp, "CMSG_BAD_FORMAT:  one or more arguments in the wrong format\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_BAD_FORMAT:  one or more arguments in the wrong format\n");
    break;

  case CMSG_BAD_DOMAIN_TYPE:
    sprintf(temp, "CMSG_BAD_DOMAIN_TYPE:  domain type not supported\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_BAD_DOMAIN_TYPE:  domain type not supported\n");
    break;

  case CMSG_NAME_EXISTS:
    sprintf(temp, "CMSG_NAME_EXISTS: another process in this domain is using this name\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_NAME_EXISTS:  another process in this domain is using this name\n");
    break;

  case CMSG_NOT_INITIALIZED:
    sprintf(temp, "CMSG_NOT_INITIALIZED:  cMsgConnect needs to be called\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_NOT_INITIALIZED:  cMsgConnect needs to be called\n");
    break;

  case CMSG_ALREADY_INIT:
    sprintf(temp, "CMSG_ALREADY_INIT:  cMsgConnect already called\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_ALREADY_INIT:  cMsgConnect already called\n");
    break;

  case CMSG_LOST_CONNECTION:
    sprintf(temp, "CMSG_LOST_CONNECTION:  connection to cMsg server lost\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_LOST_CONNECTION:  connection to cMsg server lost\n");
    break;

  case CMSG_NETWORK_ERROR:
    sprintf(temp, "CMSG_NETWORK_ERROR:  error talking to cMsg server\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_NETWORK_ERROR:  error talking to cMsg server\n");
    break;

  case CMSG_SOCKET_ERROR:
    sprintf(temp, "CMSG_NETWORK_ERROR:  error setting socket options\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_NETWORK_ERROR:  error setting socket options\n");
    break;

  case CMSG_PEND_ERROR:
    sprintf(temp, "CMSG_PEND_ERROR:  error waiting for messages to arrive\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_PEND_ERROR:  error waiting for messages to arrive\n");
    break;

  case CMSG_ILLEGAL_MSGTYPE:
    sprintf(temp, "CMSG_ILLEGAL_MSGTYPE:  pend received illegal message type\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_ILLEGAL_MSGTYPE:  pend received illegal message type\n");
    break;

  case CMSG_OUT_OF_MEMORY:
    sprintf(temp, "CMSG_OUT_OF_MEMORY:  ran out of memory\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_OUT_OF_MEMORY:  ran out of memory\n");
    break;

  case CMSG_OUT_OF_RANGE:
    sprintf(temp, "CMSG_OUT_OF_RANGE:  argument is out of range\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_OUT_OF_RANGE:  argument is out of range\n");
    break;

  case CMSG_LIMIT_EXCEEDED:
    sprintf(temp, "CMSG_LIMIT_EXCEEDED:  trying to create too many of something\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_LIMIT_EXCEEDED:  trying to create too many of something\n");
    break;

  case CMSG_BAD_DOMAIN_ID:
    sprintf(temp, "CMSG_BAD_DOMAIN_ID: id does not match any existing domain\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_BAD_DOMAIN_ID: id does not match any existing domain\n");
    break;

  case CMSG_BAD_MESSAGE:
    sprintf(temp, "CMSG_BAD_MESSAGE: message is not in the correct form\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_BAD_MESSAGE: message is not in the correct form\n");
    break;

  case CMSG_WRONG_DOMAIN_TYPE:
    sprintf(temp, "CMSG_WRONG_DOMAIN_TYPE: UDL does not match the server type\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_WRONG_DOMAIN_TYPE: UDL does not match the server type\n");
    break;
  case CMSG_NO_CLASS_FOUND:
    sprintf(temp, "CMSG_NO_CLASS_FOUND: class cannot be found to instantiate a subdomain client handler\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_NO_CLASS_FOUND: class cannot be found to instantiate a subdomain client handler\n");
    break;

  case CMSG_DIFFERENT_VERSION:
    sprintf(temp, "CMSG_DIFFERENT_VERSION: client and server are different versions\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_DIFFERENT_VERSION: client and server are different versions\n");
    break;

  default:
    sprintf(temp, "?cMsgPerror...no such error: %d\n",error);
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("?cMsgPerror...no such error: %d\n",error);
    break;
  }

  return(temp);
}


/*-------------------------------------------------------------------*/


int cMsgSetDebugLevel(int level) {
  
  if ((level != CMSG_DEBUG_NONE)  &&
      (level != CMSG_DEBUG_INFO)  &&
      (level != CMSG_DEBUG_WARN)  &&
      (level != CMSG_DEBUG_ERROR) &&
      (level != CMSG_DEBUG_SEVERE)) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  cMsgDebug = level;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static void domainClear(cMsgDomain *domain) {
  domainFree(domain);
  domainInit(domain);
}


/*-------------------------------------------------------------------*
 *
 * Internal functions
 *
 *-------------------------------------------------------------------*/


static void registerDomainTypeInfo(void) {

  /* cMsg type */
  dTypeInfo[0].type = (char *)strdup(codaDomainTypeInfo.type); 
  dTypeInfo[0].functions = codaDomainTypeInfo.functions;

  return;
}


/*-------------------------------------------------------------------*/


static void domainInit(cMsgDomain *domain) {  
  domain->id             = 0;
  domain->implId         = -1;
  domain->initComplete   = 0;
  domain->receiveState   = 0;
      
  domain->type           = NULL;
  domain->name           = NULL;
  domain->udl            = NULL;
  domain->description    = NULL;
  domain->UDLremainder   = NULL;
  domain->functions      = NULL;  
}


/*-------------------------------------------------------------------*/


static void domainFree(cMsgDomain *domain) {  
  if (domain->type         != NULL) free(domain->type);
  if (domain->name         != NULL) free(domain->name);
  if (domain->udl          != NULL) free(domain->udl);
  if (domain->description  != NULL) free(domain->description);
  if (domain->UDLremainder != NULL) free(domain->UDLremainder);
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
/*   miscellaneous local functions                                   */
/*-------------------------------------------------------------------*/


static int parseUDL(const char *UDL, char **domainType, char **UDLremainder) {

  /* note:  cMsg domain UDL is of the form:
   *                    cMsg:domainType://somethingDomainSpecific
   *
   * The initial cMsg is optional.
   */

  char *p, *udl, *pudl;

  if (UDL == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  /* strtok modifies the string it tokenizes, so make a copy */
  pudl = udl = (char *) strdup(UDL);
  
/*printf("UDL = %s\n", udl);*/

  /*
   * Check to see if optional "cMsg:" in front.
   * Start by looking for any occurance.
   */  
  p = strstr(udl, "cMsg:");
  
  /* if there a "cMsg:" in front ... */
  if (p == udl) {
    /* if there is still the domain before "://", strip off first "cMsg:" */
    pudl = udl+5;
    p = strstr(pudl, "//");
    if (p == pudl) {
      pudl = udl;
    }
  }
    
  /* get tokens separated by ":" or "/" */
  
  /* find domain */
  if ( (p = (char *) strtok(pudl, ":/")) == NULL) {
    free(udl);
    return (CMSG_BAD_FORMAT);
  }
  if (domainType != NULL) *domainType = (char *) strdup(p);
/*printf("domainType = %s\n", p);*/
   
  /* find UDL remainder */
  if ( (p = (char *) strtok(NULL, "")) != NULL) {
    /* skip over any beginning "/" chars */
    while (*p == '/') {
      p++;
    }
    
    if (UDLremainder != NULL) *UDLremainder = (char *) strdup(p);
/*printf("remainder = %s\n", p);*/
  }

  /* UDL parsed ok */
  free(udl);
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int checkString(char *s) {

  int i;

  if (s == NULL) return(CMSG_ERROR);

  /* check for printable character */
  for (i=0; i<strlen(s); i++) {
    if (isprint((int)s[i]) == 0) return(CMSG_ERROR);
  }

  /* check for excluded chars */
  if (strpbrk(s, excludedChars) != 0) return(CMSG_ERROR);
  
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


static void initMessage(cMsgMessage *msg) {
    
    if (msg == NULL) return;
    
    msg->version      = 0;
    msg->sysMsgId     = 0;
    msg->getRequest   = 0;
    msg->getResponse  = 0;
    
    msg->domain       = NULL;
    msg->subject      = NULL;
    msg->type         = NULL;
    msg->text         = NULL;
    
    msg->priority     = 0;
    msg->userInt      = 0;
    msg->userTime     = 0;

    msg->sender       = NULL;
    msg->senderHost   = NULL;
    msg->senderTime   = 0;
    msg->senderToken  = 0;

    msg->receiver     = NULL;
    msg->receiverHost = NULL;
    msg->receiverTime = 0;
    msg->receiverSubscribeId = 0;

    return;
  }


/*-------------------------------------------------------------------*/


int cMsgFreeMessage(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  
  if (msg->sender != NULL)       free(msg->sender);
  if (msg->senderHost != NULL)   free(msg->senderHost);
  if (msg->receiver != NULL)     free(msg->receiver);
  if (msg->receiverHost != NULL) free(msg->receiverHost);
  if (msg->domain != NULL)       free(msg->domain);
  if (msg->subject != NULL)      free(msg->subject);
  if (msg->type != NULL)         free(msg->type);
  if (msg->text != NULL)         free(msg->text);
  
  free(msg);

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


  void *cMsgCopyMessage(void *vmsg) {
    cMsgMessage *newMsg, *msg = (cMsgMessage *)vmsg;
    
    if ((newMsg = (cMsgMessage *)malloc(sizeof(cMsgMessage))) == NULL) {
      return NULL;
    }
    
    newMsg->version     = msg->version;
    newMsg->sysMsgId    = msg->sysMsgId;
    newMsg->getRequest  = msg->getRequest;
    newMsg->getResponse = msg->getResponse;
    
    if (msg->domain != NULL) newMsg->domain = (char *) strdup(msg->domain);
    else                     newMsg->domain = NULL;
        
    if (msg->subject != NULL) newMsg->subject = (char *) strdup(msg->subject);
    else                      newMsg->subject = NULL;
        
    if (msg->type != NULL) newMsg->type = (char *) strdup(msg->type);
    else                   newMsg->type = NULL;
        
    if (msg->text != NULL) newMsg->text = (char *) strdup(msg->text);
    else                   newMsg->text = NULL;

    newMsg->priority    = msg->priority;
    newMsg->userInt     = msg->userInt;
    newMsg->userTime    = msg->userTime;

    if (msg->sender != NULL) newMsg->sender = (char *) strdup(msg->sender);
    else                     newMsg->sender = NULL;
    
    if (msg->senderHost != NULL) newMsg->senderHost = (char *) strdup(msg->senderHost);
    else                         newMsg->senderHost = NULL;
    
    newMsg->senderTime  = msg->senderTime;
    newMsg->senderToken = msg->senderToken;
    
    if (msg->receiver != NULL) newMsg->receiver = (char *) strdup(msg->receiver);
    else                       newMsg->receiver = NULL;
        
    if (msg->receiverHost != NULL) newMsg->receiverHost = (char *) strdup(msg->receiverHost);
    else                           newMsg->receiverHost = NULL;    

    newMsg->receiverTime = msg->receiverTime;
    newMsg->receiverSubscribeId = msg->receiverSubscribeId;

    return (void *)newMsg;
  }


/*-------------------------------------------------------------------*/


  void cMsgInitMessage(void *vmsg) {
    cMsgMessage *msg = (cMsgMessage *)vmsg;
    
    if (msg == NULL) return;
    
    if (msg->domain != NULL)       free(msg->domain);
    if (msg->subject != NULL)      free(msg->subject);
    if (msg->type != NULL)         free(msg->type);
    if (msg->text != NULL)         free(msg->text);
    if (msg->sender != NULL)       free(msg->sender);
    if (msg->senderHost != NULL)   free(msg->senderHost);
    if (msg->receiver != NULL)     free(msg->receiver);
    if (msg->receiverHost != NULL) free(msg->receiverHost);
    
    initMessage(msg);
  }


/*-------------------------------------------------------------------*/


void *cMsgCreateMessage(void) {
  cMsgMessage *msg;
  
  msg = (cMsgMessage *) malloc(sizeof(cMsgMessage));
  if (msg == NULL) return NULL;
  /* initialize the memory */
  initMessage(msg);
  
  return((void *)msg);
}


/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

int cMsgGetVersion(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(-1);
  return(msg->version);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

int cMsgSetGetResponse(void *vmsg, int getReponse) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  msg->getResponse = getReponse;

  return(CMSG_OK);
}

int cMsgGetGetResponse(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(-1);
  return(msg->getResponse);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

int cMsgGetGetRequest(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(-1);
  return(msg->getRequest);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

char *cMsgGetDomain(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(NULL);
  if (msg->domain == NULL) return NULL;
  return( (char *) (strdup(msg->domain)) );
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

int cMsgSetSubject(void *vmsg, char *subject) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->subject != NULL) free(msg->subject);
  msg->subject = (char *)strdup(subject);

  return(CMSG_OK);
}

char *cMsgGetSubject(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(NULL);
  if (msg->subject == NULL) return NULL;
  return( (char *) (strdup(msg->subject)) );
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

int cMsgSetType(void *vmsg, char *type) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->type != NULL) free(msg->type);
  msg->type = (char *)strdup(type);

  return(CMSG_OK);
}

char *cMsgGetType(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(NULL);
  if (msg->type == NULL) return NULL;
  return( (char *) (strdup(msg->type)) );
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

int cMsgSetText(void *vmsg, char *text) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->text != NULL) free(msg->text);
  msg->text = (char *)strdup(text);

  return(CMSG_OK);
}

char *cMsgGetText(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(NULL);
  if (msg->text == NULL) return NULL;
  return( (char *) (strdup(msg->text)) );
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

int cMsgSetPriority(void *vmsg, int priority) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  msg->priority = priority;

  return(CMSG_OK);
}

int cMsgGetPriority(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(-1);
  return(msg->priority);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

int cMsgSetUserInt(void *vmsg, int userInt) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  msg->userInt = userInt;

  return(CMSG_OK);
}

int cMsgGetUserInt(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(-1);
  return(msg->userInt);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

int cMsgSetUserTime(void *vmsg, time_t userTime) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  msg->userTime = userTime;

  return(CMSG_OK);
}

time_t cMsgGetUserTime(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(-1);
  return(msg->userTime);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

char *cMsgGetSender(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(NULL);
  if (msg->sender == NULL) return NULL;
  return( (char *) (strdup(msg->sender)) );
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

char *cMsgGetSenderHost(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(NULL);
  if (msg->senderHost == NULL) return NULL;
  return( (char *) (strdup(msg->senderHost)) );
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

time_t cMsgGetSenderTime(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(-1);
  return(msg->senderTime);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

char *cMsgGetReceiver(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(NULL);
  if (msg->receiver == NULL) return NULL;
  return( (char *) (strdup(msg->receiver)) );
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

char *cMsgGetReceiverHost(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(NULL);
  if (msg->receiverHost == NULL) return NULL;
  return( (char *) (strdup(msg->receiverHost)) );
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

time_t cMsgGetReceiverTime(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(-1);
  return(msg->receiverTime);
}

/*-------------------------------------------------------------------*/
/*   subscribe config functions                                      */
/*-------------------------------------------------------------------*/


cMsgSubscribeConfig *cMsgSubscribeConfigCreate(void) {
  subscribeConfig *sc;
  
  sc = (subscribeConfig *) malloc(sizeof(subscribeConfig));
  if (sc == NULL) {
    return NULL;
  }
  
  /* default configuration for a subscription */
  sc->maxCueSize = 10000;  /* maximum number of messages to cue for callback */
  sc->skipSize   =  2000;  /* number of messages to skip over (delete) from the cue
                            * for a callback when the cue size has reached it limit */
  sc->maySkip        = 0;  /* may NOT skip messages if too many are piling up in cue */
  sc->mustSerialize  = 1;  /* messages must be processed in order */
  sc->init           = 1;  /* done intializing structure */
  sc->maxThreads   = 100;  /* max number of supplemental threads to run callback
                            * if mustSerialize = 0 */
  sc->msgsPerThread = 150; /* enough supplemental threads are started so that there are
                            * at most this many unprocessed messages for each thread */

  return (cMsgSubscribeConfig*) sc;

}


/*-------------------------------------------------------------------*/


int cMsgSubscribeConfigDestroy(cMsgSubscribeConfig *config) {
  if (config != NULL) {
    free((subscribeConfig *) config);
  }
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/

int cMsgSubscribeSetMaxCueSize(cMsgSubscribeConfig *config, int size) {
  subscribeConfig *sc = (subscribeConfig *) config;
  
  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }
   
  if (size < 1)  {
    return CMSG_BAD_ARGUMENT;
  }
  
  sc->maxCueSize = size;
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/


int cMsgSubscribeGetMaxCueSize(cMsgSubscribeConfig *config) {
  subscribeConfig *sc = (subscribeConfig *) config;   
  return sc->maxCueSize;
}


/*-------------------------------------------------------------------*/

int cMsgSubscribeSetSkipSize(cMsgSubscribeConfig *config, int size) {
  subscribeConfig *sc = (subscribeConfig *) config;
  
  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }
   
  if (size < 0)  {
    return CMSG_BAD_ARGUMENT;
  }
  
  sc->skipSize = size;
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/


int cMsgSubscribeGetSkipSize(cMsgSubscribeConfig *config) {
  subscribeConfig *sc = (subscribeConfig *) config;    
  return sc->skipSize;
}


/*-------------------------------------------------------------------*/


int cMsgSubscribeSetMaySkip(cMsgSubscribeConfig *config, int maySkip) {
  subscribeConfig *sc = (subscribeConfig *) config;

  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }   
    
  sc->maySkip = maySkip;
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/


int cMsgSubscribeGetMaySkip(cMsgSubscribeConfig *config) {
  subscribeConfig *sc = (subscribeConfig *) config;    
  return sc->maySkip;
}


/*-------------------------------------------------------------------*/


int cMsgSubscribeSetMustSerialize(cMsgSubscribeConfig *config, int serialize) {
  subscribeConfig *sc = (subscribeConfig *) config;

  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }   
    
  sc->mustSerialize = serialize;
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/


int cMsgSubscribeGetMustSerialize(cMsgSubscribeConfig *config) {
  subscribeConfig *sc = (subscribeConfig *) config;
  return sc->mustSerialize;
}


/*-------------------------------------------------------------------*/

int cMsgSubscribeSetMaxThreads(cMsgSubscribeConfig *config, int threads) {
  subscribeConfig *sc = (subscribeConfig *) config;
  
  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }
   
  if (threads < 0)  {
    return CMSG_BAD_ARGUMENT;
  }
  
  sc->maxThreads = threads;
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/


int cMsgSubscribeGetMaxThreads(cMsgSubscribeConfig *config) {
  subscribeConfig *sc = (subscribeConfig *) config;    
  return sc->maxThreads;
}


/*-------------------------------------------------------------------*/

int cMsgSubscribeSetMessagesPerThread(cMsgSubscribeConfig *config, int mpt) {
  subscribeConfig *sc = (subscribeConfig *) config;
  
  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }
   
  if (mpt < 1)  {
    return CMSG_BAD_ARGUMENT;
  }
  
  sc->msgsPerThread = mpt;
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/


int cMsgSubscribeGetMessagesPerThread(cMsgSubscribeConfig *config) {
  subscribeConfig *sc = (subscribeConfig *) config;    
  return sc->msgsPerThread;
}


/*-------------------------------------------------------------------*/


#ifdef __cplusplus
}
#endif


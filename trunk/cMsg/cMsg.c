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

/**
 * @file
 * This file contains the entire cMsg user API.
 *
 * <b>Introduction</b>
 *
 * The user API acts as a multiplexor. Depending on the particular UDL used to
 * connect to a specific cMsg server, the API will direct the user's library
 * calls to the appropriate cMsg implementation.
 */  
 
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

/**
 * Because MAXHOSTNAMELEN is defined to be 256 on Solaris and 64 on Linux,
 * use CMSG_MAXHOSTNAMELEN as a substitute that is uniform across all platforms.
 */
#define CMSG_MAXHOSTNAMELEN 256

/** Global debug level. */
int cMsgDebug = CMSG_DEBUG_ERROR;


/* local variables */
/** Is the one-time initialization done? */
static int oneTimeInitialized = 0;
/** Pthread mutex serializing calls to cMsgConnect() and cMsgDisconnect(). */
static pthread_mutex_t connectMutex = PTHREAD_MUTEX_INITIALIZER;
/** Store references to different domains and their cMsg implementations. */
static domainTypeInfo dTypeInfo[MAXDOMAINS];
/** Store information about each domain connected to. */
static cMsgDomain domains[MAXDOMAINS];


/** Excluded characters from subject, type, and description strings. */
static char *excludedChars = "`\'\"";


/** For domain implementations. */
extern domainTypeInfo codaDomainTypeInfo;


/* for c++ */
#ifdef __cplusplus
extern "C" {
#endif


/* local prototypes */
static int   checkString(char *s);
static int   checkTextString(char *s);
static void  registerDomainTypeInfo(void);
static void  domainInit(cMsgDomain *domain);
static void  domainFree(cMsgDomain *domain);
static int   parseUDL(const char *UDL, char **domainType, char **UDLremainder);
static void  connectMutexLock(void);
static void  connectMutexUnlock(void);
static void  domainClear(cMsgDomain *domain);
static void  initMessage(cMsgMessage *msg);

#ifdef VXWORKS

/** Implementation of strdup for vxWorks. */
static char *strdup(const char *s1) {
    char *s;    
    if (s1 == NULL) return NULL;    
    if ((s = (char *) malloc(strlen(s1)+1)) == NULL) return NULL;    
    return strcpy(s, s1);
}

/** Implementation of strcasecmp for vxWorks. */
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


/**
 * This routine is called once to connect to a domain.
 * The argument "myUDL" is the Universal Domain Locator used to uniquely
 * identify the cMsg server to connect to. It has the form:<p>
 *       <b><i>cMsg:domainType://domainInfo </i></b><p>
 * The argument "myName" is the client's name and may be required to be
 * unique within the domain depending on the domain.
 * The argument "myDescription" is an arbitrary string used to describe the
 * client.
 * If successful, this routine fills the argument "domainId", which identifies
 * the connection uniquely and is required as an argument by many other routines.
 * 
 * @param myUDL the Universal Domain Locator used to uniquely identify the cMsg
 *        server to connect to
 * @param myName name of this client
 * @param myDescription description of this client
 * @param domainId pointer to integer which gets filled with a unique id referring
 *        to this connection.
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if one of the arguments is bad
 * @returns CMSG_LIMIT_EXCEEDED if the maximum number of domain connections has
 *          been exceeded
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgConnect
 */   
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

  /* if such a domain type exists, store pointer to functions */
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


/**
 * This routine sends a msg to the specified domain server. It is completely
 * asynchronous and never blocks. The domain may require cMsgFlush() to be
 * called to force delivery.
 * The domainId argument is created by calling cMsgConnect()
 * and establishing a connection to a cMsg server. The message to be sent
 * may be created by calling cMsgCreateMessage(),
 * cMsgCreateNewMessage(), or cMsgCopyMessage().
 *
 * @param domainId id number of the domain connection
 * @param msg pointer to a message structure
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if one of the arguments is bad
 * @returns CMSG_NOT_INITIALIZED if the network connection to the server is closed
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSend
 */   
int cMsgSend(int domainId, void *msg) {
  
  int id = domainId - DOMAIN_ID_OFFSET;
  cMsgMessage *cmsg = (cMsgMessage *)msg;
  
  if (domains[id].initComplete != 1) return(CMSG_NOT_INITIALIZED);
  /* check args */
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if ( (checkString(cmsg->subject)  !=0 ) ||
       (checkString(cmsg->type)     !=0 ) ||
       (checkTextString(cmsg->text) !=0 )   ) {
    return(CMSG_BAD_ARGUMENT);
  }

  /* dispatch to function registered for this domain type */
  return(domains[id].functions->send(domains[id].implId, msg));
}


/*-------------------------------------------------------------------*/


/**
 * This routine sends a msg to the specified domain server and receives a response.
 * It is a synchronous routine and as a result blocks until it receives a status
 * integer from the cMsg server.
 * The domainId argument is created by calling cMsgConnect()
 * and establishing a connection to a cMsg server. The message to be sent
 * may be created by calling cMsgCreateMessage(),
 * cMsgCreateNewMessage(), or cMsgCopyMessage().
 *
 * @param domainId id number of the domain connection
 * @param msg pointer to a message structure
 * @param response integer pointer that gets filled with the server's response
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if one of the arguments is bad
 * @returns CMSG_NOT_INITIALIZED if the network connection to the server is closed
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSyncSend
 */   
int cMsgSyncSend(int domainId, void *msg, int *response) {
  
  int id = domainId - DOMAIN_ID_OFFSET;
  cMsgMessage *cmsg = (cMsgMessage *)msg;
  
  if (domains[id].initComplete != 1)   return(CMSG_NOT_INITIALIZED);
  /* check args */
  if (msg == NULL || response == NULL) return(CMSG_BAD_ARGUMENT);
  if ( (checkString(cmsg->subject)  !=0 ) ||
       (checkString(cmsg->type)     !=0 ) ||
       (checkTextString(cmsg->text) !=0 )   ) {
    return(CMSG_BAD_ARGUMENT);
  }
  

  /* dispatch to function registered for this domain type */
  return(domains[id].functions->syncSend(domains[id].implId, msg, response));
}


/*-------------------------------------------------------------------*/


/**
 * This routine sends any pending (queued up) communication with the server.
 * The implementation of this routine depends entirely on the domain in which 
 * it is being used. In the cMsg domain, this routine does nothing as all server
 * communications are sent immediately upon calling any function.
 *
 * @param domainId id number of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if the network connection to the server is closed
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgFlush
 */   
int cMsgFlush(int domainId) {

  int id = domainId - DOMAIN_ID_OFFSET;

  if (domains[id].initComplete != 1) return(CMSG_NOT_INITIALIZED);
  

  /* dispatch to function registered for this domain type */
  return(domains[id].functions->flush(domains[id].implId));
}


/*-------------------------------------------------------------------*/


/**
 * This routine subscribes to messages of the given subject and type.
 * When a message is received, the given callback is passed the message
 * pointer and the userArg pointer and then is executed. A configuration
 * structure is given to determine the behavior of the callback.
 *
 * @param domainId id number of the domain connection
 * @param subject subject of messages subscribed to
 * @param type type of messages subscribed to
 * @param callback pointer to callback to be executed on receipt of message
 * @param userArg user-specified pointer to be passed to the callback
 * @param config pointer to callback configuration structure
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if the network connection to the server is closed
 * @returns CMSG_BAD_ARGUMENT if one of the arguments is bad
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSubscribe
 */   
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


/**
 * This routine unsubscribes to messages of the given subject, type and
 * callback.
 *
 * @param domainId id number of the domain connection
 * @param subject subject of messages to unsubscribed from
 * @param type type of messages to unsubscribed from
 * @param callback pointer to callback to be removed
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if the network connection to the server is closed
 * @returns CMSG_BAD_ARGUMENT if one of the arguments is bad
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgUnSubscribe
 */   
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


/**
 * This routine gets one message from another cMsg client by sending out
 * an initial message to that responder. It is a synchronous routine that
 * fails when no reply is received with the given timeout. This function
 * can be thought of as a peer-to-peer exchange of messages.
 * One message is sent to all listeners. The first responder
 * to the initial message will have its single response message sent back
 * to the original sender.
 *
 * @param domainId id number of the domain connection
 * @param sendMsg messages to send to all listeners
 * @param timeout amount of time to wait for the response message
 * @param replyMsg message received from the responder
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if the network connection to the server is closed
 * @returns CMSG_BAD_ARGUMENT if one of the arguments is bad
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSendAndGet
 */   
int cMsgSendAndGet(int domainId, void *sendMsg, struct timespec *timeout, void **replyMsg) {

  int id = domainId - DOMAIN_ID_OFFSET;
  cMsgMessage *msg;

  if (domains[id].initComplete != 1)       return(CMSG_NOT_INITIALIZED);
  if (sendMsg == NULL || replyMsg == NULL) return(CMSG_BAD_ARGUMENT);
  
  msg = (cMsgMessage *)sendMsg;
  
  /* check msg fields */
  if ( (checkString(msg->subject)  !=0 ) ||
       (checkString(msg->type)     !=0 ) ||
       (checkTextString(msg->text) !=0 )   ) {
    return(CMSG_BAD_ARGUMENT);
  }

  return(domains[id].functions->sendAndGet(domains[id].implId, sendMsg, timeout, replyMsg));
}


/*-------------------------------------------------------------------*/


/**
 * This routine gets one message from a one-shot subscription to the given
 * subject and type.
 *
 * @param domainId id number of the domain connection
 * @param subject subject of message subscribed to
 * @param type type of message subscribed to
 * @param timeout amount of time to wait for the message
 * @param replyMsg message received
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if the network connection to the server is closed
 * @returns CMSG_BAD_ARGUMENT if one of the arguments is bad
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSendAndGet
 */   
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


/**
 * This routine enables the receiving of messages and delivery to callbacks.
 * The receiving of messages is disabled by default and must be explicitly enabled.
 *
 * @param domainId id number of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if the network connection to the server is closed
 */   
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


/**
 * This routine disables the receiving of messages and delivery to callbacks.
 * The receiving of messages is disabled by default. This routine only has an
 * effect when cMsgReceiveStart() was previously called.
 *
 * @param domainId id number of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if the network connection to the server is closed
 */   
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


/**
 * This routine disconnects the client from the cMsg server.
 *
 * @param domainId id number of the domain connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if the network connection to the server is closed
 */   
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


/**
 * This routine returns a string describing the given error condition.
 * It can also print out that same string with printf if the debug level
 * is set to CMSG_DEBUG_ERROR or CMSG_DEBUG_SEVERE by cMsgSetDebugLevel().
 * The returned string is a static char array. This means it is not 
 * thread-safe and will be overwritten on subsequent calls.
 *
 * @param error error condition
 *
 * @returns error string
 */   
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


/**
 * This routine sets the level of debug output. The argument should be
 * one of:<p>
 * - #CMSG_DEBUG_NONE
 * - #CMSG_DEBUG_INFO
 * - #CMSG_DEBUG_SEVERE
 * - #CMSG_DEBUG_ERROR
 * - #CMSG_DEBUG_WARN
 *
 * @param level debug level desired
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if debug level is bad
 */   
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


/*-------------------------------------------------------------------*
 *
 * Internal functions
 *
 *-------------------------------------------------------------------*/


/**
 * This routine registers the name of a domain implementation
 * along with the set of functions that implement all the basic domain
 * functionality (connect, disconnect, send, syncSend, flush, subscribe,
 * unsubscribe, sendAndGet, subscribeAndGet, start, and stop).
 * 
 * This routine must be updated to include each new domain accessible
 * from the "C" language.
 */   
static void registerDomainTypeInfo(void) {

  /* cMsg type */
  dTypeInfo[0].type = (char *)strdup(codaDomainTypeInfo.type); 
  dTypeInfo[0].functions = codaDomainTypeInfo.functions;

  return;
}


/*-------------------------------------------------------------------*/


/**
 * This routine initializes the given domain data structure. All strings
 * are set to null.
 *
 * @param domain pointer to structure holding domain info
 */   
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


/**
 * This routine frees all of the allocated memory of the given domain
 * data structure. 
 *
 * @param domain pointer to structure holding domain info
 */   
static void domainFree(cMsgDomain *domain) {  
  if (domain->type         != NULL) free(domain->type);
  if (domain->name         != NULL) free(domain->name);
  if (domain->udl          != NULL) free(domain->udl);
  if (domain->description  != NULL) free(domain->description);
  if (domain->UDLremainder != NULL) free(domain->UDLremainder);
}


/*-------------------------------------------------------------------*/


/**
 * This routine clears the given domain data structure. Any allocated
 * memory is freed, and the structure is initialized.
 *
 * @param domain pointer to structure holding domain info
 */   
static void domainClear(cMsgDomain *domain) {
  domainFree(domain);
  domainInit(domain);
}


/*-------------------------------------------------------------------*
 * Mutex functions
 *-------------------------------------------------------------------*/


/**
 * This routine locks the mutex used to prevent cMsgConnect() and
 * cMsgDisconnect() from being called concurrently.
 */   
static void connectMutexLock(void) {
  int status = pthread_mutex_lock(&connectMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the mutex used to prevent cMsgConnect() and
 * cMsgDisconnect() from being called concurrently.
 */   
static void connectMutexUnlock(void) {
  int status = pthread_mutex_unlock(&connectMutex);
  if (status != 0) {
    err_abort(status, "Failed mutex unlock");
  }
}


/*-------------------------------------------------------------------*/
/*   miscellaneous local functions                                   */
/*-------------------------------------------------------------------*/


/**
 * This routine parses the UDL given by the client in cMsgConnect().
 *
 * The UDL is the Universal Domain Locator used to uniquely
 * identify the cMsg server to connect to. It has the form:<p>
 *       <b><i>cMsg:domainType://domainInfo </i></b><p>
 * The domainType portion gets returned as the domainType.
 * The domainInfo portion gets returned the the UDLremainder.
 * Memory gets allocated for both the domainType and domainInfo which
 * must be freed by the caller.
 *
 * @param UDL UDL
 * @param domainType string which gets filled in with the domain type (eg. cMsg)
 * @param UDLremainder string which gets filled in with everything in the UDL
 *                     after the ://
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the UDL is null
 * @returns CMSG_BAD_FORMAT if the UDL is formatted incorrectly
 */   
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


/**
 * This routine checks a string given as message text.
 * It returns an error if it's NULL or contains an unprintable character
 *
 * @param s string to check
 *
 * @returns CMSG_OK if string is OK
 * @returns CMSG_ERROR if string is NULL or contains unprintable characters
 */   
static int checkTextString(char *s) {

  int i;

  if (s == NULL) return(CMSG_ERROR);

  /* check for printable character */
  for (i=0; i<strlen(s); i++) {
    if (isprint((int)s[i]) == 0) return(CMSG_ERROR);
  }
  
  /* string ok */
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/*   system accessor functions                                       */
/*-------------------------------------------------------------------*/

/**
 * This routine gets the UDL used to establish a cMsg connection.
 * If succesful, this routine will have memory allocated and assigned to
 * the dereferenced char ** argument. This memory must be freed eventually.
 *
 * @param domainId id number of the domain connection
 * @param domain pointer to pointer which gets filled with the UDL
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_DOMAIN_ID if an out-of-range domain id is used
 */   
int cMsgGetUDL(int domainId, char **udl) {

  int id  = domainId - DOMAIN_ID_OFFSET;
  int len = strlen(domains[id].udl);

  if (id < 0 || id >= MAXDOMAINS) return(CMSG_BAD_DOMAIN_ID);

  if (domains[id].udl == NULL) {
    *udl = NULL;
  }
  else {
    *udl = (char *) (strdup(domains[id].udl));
  }
  return(CMSG_OK);
}
  
  
/*-------------------------------------------------------------------*/

/**
 * This routine gets the client name used in a cMsg connection.
 * If succesful, this routine will have memory allocated and assigned to
 * the dereferenced char ** argument. This memory must be freed eventually.
 *
 * @param domainId id number of the domain connection
 * @param domain pointer to pointer which gets filled with the name
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_DOMAIN_ID if an out-of-range domain id is used
 */   
int cMsgGetName(int domainId, char **name) {

  int id  = domainId - DOMAIN_ID_OFFSET;
  int len = strlen(domains[id].name);

  if (id < 0 || id >= MAXDOMAINS) return(CMSG_BAD_DOMAIN_ID);

  if (domains[id].name == NULL) {
    *name = NULL;
  }
  else {
    *name = (char *) (strdup(domains[id].name));
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/


/**
 * This routine gets the client description used in a cMsg connection.
 * If succesful, this routine will have memory allocated and assigned to
 * the dereferenced char ** argument. This memory must be freed eventually.
 *
 * @param domainId id number of the domain connection
 * @param domain pointer to pointer which gets filled with the description
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_DOMAIN_ID if an out-of-range domain id is used
 */   
int cMsgGetDescription(int domainId, char **description) {

  int id  = domainId - DOMAIN_ID_OFFSET;
  int len = strlen(domains[id].description);

  if (id < 0 || id >= MAXDOMAINS) return(CMSG_BAD_DOMAIN_ID);

  if (domains[id].description == NULL) {
    *description = NULL;
  }
  else {
    *description = (char *) (strdup(domains[id].description));
  }
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine gets the state of a cMsg connection. If initState gets
 * filled with a one, there is a valid connection. Anything else (zero
 * in this case), indicates no connection to a cMsg server.
 *
 * @param domainId id number of the domain connection
 * @param initState integer pointer to be filled in with the connection state
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_DOMAIN_ID if an out-of-range domain id is used
 */   
int cMsgGetInitState(int domainId, int *initState) {

  int id = domainId - DOMAIN_ID_OFFSET;

  if (id < 0 || id >= MAXDOMAINS) return(CMSG_BAD_DOMAIN_ID);

  *initState=domains[id].initComplete;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine gets the message receiving state of a cMsg connection. If
 * receiveState gets filled with a one, all messages sent to the client
 * will be received and sent to appropriate callbacks . Anything else (zero
 * in this case), indicates no messages will be received or sent to callbacks.
 *
 * @param domainId id number of the domain connection
 * @param receiveState integer pointer to be filled in with the receive state
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_DOMAIN_ID if an out-of-range domain id is used
 */   
int cMsgGetReceiveState(int domainId, int *receiveState) {

  int id = domainId - DOMAIN_ID_OFFSET;

  if (id < 0 || id >= MAXDOMAINS) return(CMSG_BAD_DOMAIN_ID);

  *receiveState=domains[id].receiveState;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/*   message accessor functions                                      */
/*-------------------------------------------------------------------*/


/**
 * This routine initializes a given message structure.
 *
 * @param msg pointer to message structure being initialized
 */   
static void initMessage(cMsgMessage *msg) {
    
    if (msg == NULL) return;
    
    msg->version      = 0;
    msg->sysMsgId     = 0;
    msg->info         = 0;
    
    msg->domain       = NULL;
    msg->creator      = NULL;
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


/**
 * This routine frees the memory allocated in the creation of a message.
 * The cMsg client must call this routine on any messages created to avoid
 * memory leaks.
 *
 * @param msg pointer to message structure being freed
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if msg is NULL
 */   
int cMsgFreeMessage(void *vmsg) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  
  if (msg->sender != NULL)       free(msg->sender);
  if (msg->senderHost != NULL)   free(msg->senderHost);
  if (msg->receiver != NULL)     free(msg->receiver);
  if (msg->receiverHost != NULL) free(msg->receiverHost);
  if (msg->domain != NULL)       free(msg->domain);
  if (msg->creator != NULL)      free(msg->creator);
  if (msg->subject != NULL)      free(msg->subject);
  if (msg->type != NULL)         free(msg->type);
  if (msg->text != NULL)         free(msg->text);
  
  free(msg);

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine copies a message. Memory is allocated with this
 * function and can be freed by cMsgFreeMessage().
 *
 * @param msg pointer to message structure being copied
 *
 * @returns a pointer to the message copy
 * @returns NULL if argument is NULL or no memory available
 */   
  void *cMsgCopyMessage(void *vmsg) {
    cMsgMessage *newMsg, *msg = (cMsgMessage *)vmsg;
    
    if (vmsg == NULL) {
      return NULL;
    }
    
    if ((newMsg = (cMsgMessage *)malloc(sizeof(cMsgMessage))) == NULL) {
      return NULL;
    }
    
    newMsg->version     = msg->version;
    newMsg->sysMsgId    = msg->sysMsgId;
    newMsg->info        = msg->info;
    
    if (msg->domain != NULL) newMsg->domain = (char *) strdup(msg->domain);
    else                     newMsg->domain = NULL;
        
    if (msg->creator != NULL) newMsg->creator = (char *) strdup(msg->creator);
    else                      newMsg->creator = NULL;
        
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


/**
 * This routine initializes a message. It frees all allocated memory,
 * sets all strings to NULL, and sets all numeric values to their default
 * state.
 *
 * @param msg pointer to message structure being initialized
 */   
  void cMsgInitMessage(void *vmsg) {
    cMsgMessage *msg = (cMsgMessage *)vmsg;
    
    if (msg == NULL) return;
    
    if (msg->domain != NULL)       free(msg->domain);
    if (msg->creator != NULL)      free(msg->creator);
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


/**
 * This routine creates a new, initialized message. Memory is allocated with this
 * function and can be freed by cMsgFreeMessage().
 *
 * @returns a pointer to the new message
 * @returns NULL if no memory available
 */   
void *cMsgCreateMessage(void) {
  cMsgMessage *msg;
  
  msg = (cMsgMessage *) malloc(sizeof(cMsgMessage));
  if (msg == NULL) return NULL;
  /* initialize the memory */
  initMessage(msg);
  
  return((void *)msg);
}


/*-------------------------------------------------------------------*/


/**
 * This routine creates a new, initialized message with the creator
 * field taken from the given message. Memory is allocated with this
 * function and can be freed by cMsgFreeMessage().
 *
 * @param msg pointer to message from which creator field is taken
 *
 * @returns a pointer to the new message
 * @returns NULL if no memory available or message argument is NULL
 */   
void *cMsgCreateNewMessage(void *vmsg) {  
    cMsgMessage *newMsg;
    
    if (vmsg == NULL) return NULL;  
    
    if ((newMsg = (cMsgMessage *)cMsgCopyMessage(vmsg)) == NULL) {
      return NULL;
    }
    
    if (newMsg->creator != NULL) {
        free(newMsg->creator);
        newMsg->creator = NULL;
    }
    
    return (void *)newMsg;
}


/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the cMsg major version number of a message.
 *
 * @param msg pointer to message
 * @param version integer pointer to be filled in with cMsg major version
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetVersion(void *vmsg, int *version) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  *version = msg->version;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the "get response" field of a message. The "get
 * reponse" field indicates the message is a response to a message sent
 * by a sendAndGet call, if it has a value of 1. Any other value indicates
 * it is not a response to a sendAndGet.
 *
 * @param msg pointer to message
 * @param getResponse set to 1 if message is a response to a sendAndGet,
 *                    anything else otherwise
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message argument is NULL
 */   
int cMsgSetGetResponse(void *vmsg, int getResponse) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  msg->info = getResponse ? msg->info |  CMSG_IS_GET_RESPONSE :
                            msg->info & ~CMSG_IS_GET_RESPONSE;

  return(CMSG_OK);
}

/**
 * This routine gets the "get response" field of a message. The "get
 * reponse" field indicates the message is a response to a message sent
 * by a sendAndGet call, if it has a value of 1. A value of 0 indicates
 * it is not a response to a sendAndGet.
 *
 * @param msg pointer to message
 * @param getResponse integer pointer to be filled in 1 if message
 *                    is a response to a sendAndGet and 0 otherwise
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetGetResponse(void *vmsg, int *getResponse) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  *getResponse = (msg->info & CMSG_IS_GET_RESPONSE) == CMSG_IS_GET_RESPONSE ? 1 : 0;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the "null get response" field of a message. If it
 * has a value of 1, the "null get response" field indicates that if the
 * message is a response to a message sent by a sendAndGet call, when sent
 * it will be received as a NULL pointer - not a message. Any other value
 * indicates it is not a null get response to a sendAndGet.
 *
 * @param msg pointer to message
 * @param getResponse set to 1 if message is a null get response to a sendAndGet,
 *                    anything else otherwise
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message argument is NULL
 */   
int cMsgSetNullGetResponse(void *vmsg, int nullGetResponse) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  msg->info = nullGetResponse ? msg->info |  CMSG_IS_NULL_GET_RESPONSE :
                                msg->info & ~CMSG_IS_NULL_GET_RESPONSE;

  return(CMSG_OK);
}

/**
 * This routine gets the "NULL get response" field of a message. If it
 * has a value of 1, the "NULL get response" field indicates that if the
 * message is a response to a message sent by a sendAndGet call, when sent
 * it will be received as a NULL pointer - not a message. Any other value
 * indicates it is not a null get response to a sendAndGet.
 *
 * @param msg pointer to message
 * @param nullGetResponse integer pointer to be filled in with 1 if message
 *                        is a NULL response to a sendAndGet and 0 otherwise
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetNullGetResponse(void *vmsg, int *nullGetResponse) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  *nullGetResponse = (msg->info & CMSG_IS_NULL_GET_RESPONSE) == CMSG_IS_NULL_GET_RESPONSE ? 1 : 0;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the "get request" field of a message. The "get
 * request" field indicates the message was sent by a sendAndGet call,
 * if it has a value of 1. A value of 0 indicates it was not sent by
 * a sendAndGet.
 *
 * @param msg pointer to message
 * @param nullGetResponse integer pointer to be filled in with 1 if message
 *                        sent by a sendAndGet and 0 otherwise
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetGetRequest(void *vmsg, int *getRequest) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  *getRequest = (msg->info & CMSG_IS_GET_REQUEST) == CMSG_IS_GET_REQUEST ? 1 : 0;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the domain of a message. When a message is newly
 * created (eg. by cMsgCreateMessage()), the domain field of a message
 * is not set. In the cMsg domain, the cMsg server sets this field
 * when it receives a client's sent message.
 * Messages received from the server will have this field set.
 * If succesful, this routine will have memory allocated and assigned to
 * the dereferenced char ** argument. This memory must be freed eventually.
 *
 * @param msg pointer to message
 * @param domain pointer to pointer which gets filled with a message's 
 *               cMsg domain
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetDomain(void *vmsg, char **domain) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->domain == NULL) {
    *domain = NULL;
  }
  else {
    *domain = (char *) (strdup(msg->domain));
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the creator of a message. When a newly created
 * message is sent, on the server it's creator field is set to the sender.
 * Once set, this value never changes. On the client, this field never gets
 * set. Messages received from the server will have this field set.
 * If succesful, this routine will have memory allocated and assigned to
 * the dereferenced char ** argument. This memory must be freed eventually.
 *
 * @param msg pointer to message
 * @param domain pointer to pointer which gets filled with a message's 
 *               creator
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetCreator(void *vmsg, char **creator) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;
  
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->creator == NULL) {
    *creator = NULL;
  }
  else {
    *creator = (char *) (strdup(msg->creator));
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the subject of a message.
 *
 * @param msg pointer to message
 * @param subject message subject
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgSetSubject(void *vmsg, char *subject) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->subject != NULL) free(msg->subject);  
  if (subject == NULL) {
    msg->subject = NULL;    
  }
  else {
    msg->subject = (char *)strdup(subject);
  }

  return(CMSG_OK);
}

/**
 * This routine gets the subject of a message.
 * If succesful, this routine will have memory allocated and assigned to
 * the dereferenced char ** argument. This memory must be freed eventually.
 *
 * @param msg pointer to message
 * @param domain pointer to pointer which gets filled with a message's 
 *               subject
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetSubject(void *vmsg, char **subject) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;
  
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->subject == NULL) {
    *subject = NULL;
  }
  else {
    *subject = (char *) (strdup(msg->subject));
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the type of a message.
 *
 * @param msg pointer to message
 * @param type message type
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgSetType(void *vmsg, char *type) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->type != NULL) free(msg->type);
  if (type == NULL) {
    msg->type = NULL;    
  }
  else {
    msg->type = (char *)strdup(type);
  }

  return(CMSG_OK);
}

/**
 * This routine gets the type of a message.
 * If succesful, this routine will have memory allocated and assigned to
 * the dereferenced char ** argument. This memory must be freed eventually.
 *
 * @param msg pointer to message
 * @param domain pointer to pointer which gets filled with a message's 
 *               type
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetType(void *vmsg, char **type) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;
  
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->type == NULL) {
    *type = NULL;
  }
  else {
    *type = (char *) (strdup(msg->type));
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the text of a message.
 *
 * @param msg pointer to message
 * @param text message text
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgSetText(void *vmsg, char *text) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->text != NULL) free(msg->text);
  if (text == NULL) {
    msg->text = NULL;    
  }
  else {
    msg->text = (char *)strdup(text);
  }

  return(CMSG_OK);
}

/**
 * This routine gets the text of a message.
 * If succesful, this routine will have memory allocated and assigned to
 * the dereferenced char ** argument. This memory must be freed eventually.
 *
 * @param msg pointer to message
 * @param domain pointer to pointer which gets filled with a message's 
 *               text
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetText(void *vmsg, char **text) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;
  
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->text == NULL) {
    *text = NULL;
  }
  else {
    *text = (char *) (strdup(msg->text));
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the priority of a message.
 *
 * @param msg pointer to message
 * @param priority message priority
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgSetPriority(void *vmsg, int priority) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  msg->priority = priority;

  return(CMSG_OK);
}

/**
 * This routine gets the priority of a message.
 *
 * @param msg pointer to message
 * @param priority integer pointer to be filled in with message priority
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetPriority(void *vmsg, int *priority) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  *priority = msg->priority;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets a message's user-defined integer.
 *
 * @param msg pointer to message
 * @param userInt message's user-defined integer
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgSetUserInt(void *vmsg, int userInt) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  msg->userInt = userInt;

  return(CMSG_OK);
}

/**
 * This routine gets a message's user-defined integer.
 *
 * @param msg pointer to message
 * @param userInt integer pointer to be filled with message's user-defined integer
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetUserInt(void *vmsg, int *userInt) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  *userInt = msg->userInt;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets a message's user-defined time (in seconds since
 * midnight GMT, Jan 1st, 1970).
 *
 * @param msg pointer to message
 * @param userTime message's user-defined time
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgSetUserTime(void *vmsg, time_t userTime) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  msg->userTime = userTime;

  return(CMSG_OK);
}

/**
 * This routine gets a message's user-defined time (in seconds since
 * midnight GMT, Jan 1st, 1970).
 *
 * @param msg pointer to message
 * @param userInt time_t pointer to be filled with message's user-defined time
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetUserTime(void *vmsg, time_t *userTime) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  *userTime = msg->userTime;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the sender of a message.
 * If succesful, this routine will have memory allocated and assigned to
 * the dereferenced char ** argument. This memory must be freed eventually.
 *
 * @param msg pointer to message
 * @param domain pointer to pointer which gets filled with a message's 
 *               sender
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetSender(void *vmsg, char **sender) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;
  
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->sender == NULL) {
    *sender = NULL;
  }
  else {
    *sender = (char *) (strdup(msg->sender));
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the host of the sender of a message.
 * If succesful, this routine will have memory allocated and assigned to
 * the dereferenced char ** argument. This memory must be freed eventually.
 *
 * @param msg pointer to message
 * @param domain pointer to pointer which gets filled with the host of
 *               the sender of a message
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetSenderHost(void *vmsg, char **senderHost) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;
  
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->senderHost == NULL) {
    *senderHost = NULL;
  }
  else {
    *senderHost = (char *) (strdup(msg->senderHost));
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the time a message was last sent (in seconds since
 * midnight GMT, Jan 1st, 1970).
 *
 * @param msg pointer to message
 * @param userInt time_t pointer to be filled with time message was last sent
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetSenderTime(void *vmsg, time_t *senderTime) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  *senderTime = msg->senderTime;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the receiver of a message.
 * If succesful, this routine will have memory allocated and assigned to
 * the dereferenced char ** argument. This memory must be freed eventually.
 *
 * @param msg pointer to message
 * @param domain pointer to pointer which gets filled with a message's 
 *               receiver
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetReceiver(void *vmsg, char **receiver) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;
  
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->receiver == NULL) {
    *receiver = NULL;
  }
  else {
    *receiver = (char *) (strdup(msg->receiver));
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the host of the receiver of a message. This field
 * is NULL for a newly created message.
 * If succesful, this routine will have memory allocated and assigned to
 * the dereferenced char ** argument. This memory must be freed eventually.
 *
 * @param msg pointer to message
 * @param domain pointer to pointer which gets filled with the host of
 *               the receiver of a message
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetReceiverHost(void *vmsg, char **receiverHost) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;
  
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->receiverHost == NULL) {
    *receiverHost = NULL;
  }
  else {
    *receiverHost = (char *) (strdup(msg->receiverHost));
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the time a message was received (in seconds since
 * midnight GMT, Jan 1st, 1970).
 *
 * @param msg pointer to message
 * @param userInt time_t pointer to be filled with time message was received
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgGetReceiverTime(void *vmsg, time_t *receiverTime) {

  cMsgMessage *msg = (cMsgMessage *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  *receiverTime = msg->receiverTime;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*   subscribe config functions                                      */
/*-------------------------------------------------------------------*/


/**
 * This routine creates a structure of configuration information used
 * to determine the behavior of a cMsgSubscribe()'s callback. The
 * configuration is filled with default values. Each aspect of the
 * configuration may be modified by setter and getter functions. The
 * defaults are:
 * - maximum messages to cue for callback is 10000
 * - no messages may be skipped
 * - calls to the callback function must be serialized
 * - may skip up to 2000 messages at once if skipping is enabled
 * - maximum number of threads when parallelizing calls to the callback
 *   function is 100
 * - enough supplemental threads are started so that there are
 *   at most 150 unprocessed messages for each thread
 *
 * Note that this routine allocates memory and cMsgSubscribeConfigDestroy()
 * must be called to free it.
 *
 * @returns NULL if no memory available
 * @returns pointer to configuration if successful
 */   
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



/**
 * This routine frees the memory associated with a configuration
 * created by cMsgSubscribeConfigCreate();
 * 
 * @param config pointer to configuration
 *
 * @returns CMSG_OK
 */   
int cMsgSubscribeConfigDestroy(cMsgSubscribeConfig *config) {
  if (config != NULL) {
    free((subscribeConfig *) config);
  }
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets a subscribe configuration's maximum message cue
 * size. Messages are kept in the cue until they can be processed by
 * the callback function.
 *
 * @param config pointer to configuration
 * @param size maximum cue size
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if configuration was not initialized
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL or size < 1
 */   
int cMsgSubscribeSetMaxCueSize(cMsgSubscribeConfig *config, int size) {
  subscribeConfig *sc = (subscribeConfig *) config;
  
  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }
   
  if (config == NULL || size < 1)  {
    return CMSG_BAD_ARGUMENT;
  }
  
  sc->maxCueSize = size;
  return CMSG_OK;
}


/**
 * This routine gets a subscribe configuration's maximum message cue
 * size. Messages are kept in the cue until they can be processed by
 * the callback function.
 *
 * @param config pointer to configuration
 * @param size integer pointer to be filled with configuration's maximum
 *             cue size
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL
 */   
int cMsgSubscribeGetMaxCueSize(cMsgSubscribeConfig *config, int *size) {
  subscribeConfig *sc = (subscribeConfig *) config;   
  
  if (config == NULL) return(CMSG_BAD_ARGUMENT);
  *size = sc->maxCueSize;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the number of messages to skip over (delete) if too
 * many messages are piling up in the cue. Messages are only skipped if
 * cMsgSubscribeSetMaySkip() sets the configuration to do so.
 *
 * @param config pointer to configuration
 * @param size number of messages to skip (delete)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if configuration was not initialized
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL or size < 0
 */   
int cMsgSubscribeSetSkipSize(cMsgSubscribeConfig *config, int size) {
  subscribeConfig *sc = (subscribeConfig *) config;
  
  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }
   
  if (config == NULL || size < 0)  {
    return CMSG_BAD_ARGUMENT;
  }
  
  sc->skipSize = size;
  return CMSG_OK;
}

/**
 * This routine gets the number of messages to skip over (delete) if too
 * many messages are piling up in the cue. Messages are only skipped if
 * cMsgSubscribeSetMaySkip() sets the configuration to do so.
 *
 * @param config pointer to configuration
 * @param size integer pointer to be filled with the number of messages
 *             to skip (delete)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL
 */   
int cMsgSubscribeGetSkipSize(cMsgSubscribeConfig *config, int *size) {
  subscribeConfig *sc = (subscribeConfig *) config;    

  if (config == NULL) return(CMSG_BAD_ARGUMENT);
  *size = sc->skipSize;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets whether messages may be skipped over (deleted)
 * if too many messages are piling up in the cue. The maximum number
 * of messages skipped at once is determined by cMsgSubscribeSetSkipSize().
 *
 * @param config pointer to configuration
 * @param maySkip set to 0 if messages may NOT be skipped, set to anything
 *                else otherwise
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if configuration was not initialized
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL
 */   
int cMsgSubscribeSetMaySkip(cMsgSubscribeConfig *config, int maySkip) {
  subscribeConfig *sc = (subscribeConfig *) config;

  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  } 
    
  if (config == NULL)  {
    return CMSG_BAD_ARGUMENT;
  }
    
  sc->maySkip = maySkip;
  return CMSG_OK;
}

/**
 * This routine gets whether messages may be skipped over (deleted)
 * if too many messages are piling up in the cue. The maximum number
 * of messages skipped at once is determined by cMsgSubscribeSetSkipSize().
 *
 * @param config pointer to configuration
 * @param maySkip integer pointer to be filled with 0 if messages may NOT
 *                be skipped (deleted), or anything else otherwise
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL
 */   
int cMsgSubscribeGetMaySkip(cMsgSubscribeConfig *config, int *maySkip) {
  subscribeConfig *sc = (subscribeConfig *) config;    

  if (config == NULL) return(CMSG_BAD_ARGUMENT);
  *maySkip = sc->maySkip;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets whether a subscribe's callback must be run serially
 * (in one thread), or may be parallelized (run simultaneously in more
 * than one thread) if more than 1 message is waiting in the cue.
 *
 * @param config pointer to configuration
 * @param serialize set to 0 if callback may be parallelized, or set to
 *                  anything else if callback must be serialized
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if configuration was not initialized
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL
 */   
int cMsgSubscribeSetMustSerialize(cMsgSubscribeConfig *config, int serialize) {
  subscribeConfig *sc = (subscribeConfig *) config;

  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }   
    
  if (config == NULL)  {
    return CMSG_BAD_ARGUMENT;
  }
    
  sc->mustSerialize = serialize;
  return CMSG_OK;
}

/**
 * This routine gets whether a subscribe's callback must be run serially
 * (in one thread), or may be parallelized (run simultaneously in more
 * than one thread) if more than 1 message is waiting in the cue.
 *
 * @param config pointer to configuration
 * @param maySkip integer pointer to be filled with 0 if callback may be
 *                parallelized, or anything else if callback must be serialized
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL
 */   
int cMsgSubscribeGetMustSerialize(cMsgSubscribeConfig *config, int *serialize) {
  subscribeConfig *sc = (subscribeConfig *) config;

  if (config == NULL) return(CMSG_BAD_ARGUMENT);
  *serialize = sc->mustSerialize;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the maximum number of threads a parallelized
 * subscribe's callback can run at once. This setting is only used if
 * cMsgSubscribeSetMustSerialize() was called with an argument of 0.
 *
 * @param config pointer to configuration
 * @param threads the maximum number of threads a parallelized
 *                subscribe's callback can run at once
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if configuration was not initialized
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL or threads < 0
 */   
int cMsgSubscribeSetMaxThreads(cMsgSubscribeConfig *config, int threads) {
  subscribeConfig *sc = (subscribeConfig *) config;
  
  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }
   
  if (config == NULL || threads < 0)  {
    return CMSG_BAD_ARGUMENT;
  }
  
  sc->maxThreads = threads;
  return CMSG_OK;
}

/**
 * This routine gets the maximum number of threads a parallelized
 * subscribe's callback can run at once. This setting is only used if
 * cMsgSubscribeSetMustSerialize() was called with an argument of 0.
 *
 * @param config pointer to configuration
 * @param threads integer pointer to be filled with the maximum number
 *                of threads a parallelized subscribe's callback can run at once
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL
 */   
int cMsgSubscribeGetMaxThreads(cMsgSubscribeConfig *config, int *threads) {
  subscribeConfig *sc = (subscribeConfig *) config;    

  if (config == NULL) return(CMSG_BAD_ARGUMENT);
  *threads = sc->maxThreads;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the maximum number of unprocessed messages per thread
 * before a new thread is started, if a callback is parallelized 
 * (cMsgSubscribeSetMustSerialize() set to 0).
 *
 * @param config pointer to configuration
 * @param mpt set to maximum number of unprocessed messages per thread
 *            before starting another thread
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if configuration was not initialized
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL or mpt < 1
 */   
int cMsgSubscribeSetMessagesPerThread(cMsgSubscribeConfig *config, int mpt) {
  subscribeConfig *sc = (subscribeConfig *) config;
  
  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }
   
  if (config == NULL || mpt < 1)  {
    return CMSG_BAD_ARGUMENT;
  }
  
  sc->msgsPerThread = mpt;
  return CMSG_OK;
}

/**
 * This routine gets the maximum number of unprocessed messages per thread
 * before a new thread is started, if a callback is parallelized 
 * (cMsgSubscribeSetMustSerialize() set to 0).
 *
 * @param config pointer to configuration
 * @param mpt integer pointer to be filled with the maximum number of
 *            unprocessed messages per thread before starting another thread
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL
 */   
int cMsgSubscribeGetMessagesPerThread(cMsgSubscribeConfig *config, int *mpt) {
  subscribeConfig *sc = (subscribeConfig *) config;    

  if (config == NULL) return(CMSG_BAD_ARGUMENT);
  *mpt = sc->msgsPerThread;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/


#ifdef __cplusplus
}
#endif


/*----------------------------------------------------------------------------*
 *
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    E.Wolin, 14-Jul-2004, Jefferson Lab                                     *
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
 * Description:                                                               *
 *                                                                            *
 *  Defines cMsg (CODA Message) API and return codes                          *
 *                                                                            *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 *
 *
 *  Still to do:
 *    add Doxygen comments
 *    some error codes redundant, some missing
 *
 *
 *
 *
 * Introduction
 * ------------
 *
 * cMsg is a simple abstract API to an arbitrary underlying message service.  It is 
 * powerful enough to support synchronous and asynchronous point-to-point and 
 * publish/subscribe communication, and network-accessible message queues.  Note 
 * that a given underlying implementation may not necessarily implement all these 
 * features.  
 *
 *
 * Domains
 * -------
 *
 * The abstraction relies on the important concept of a "domain", specified via a 
 * "Universal Domain Locator" (UDL) of the form:
 * 
 *       domainType://domainName
 *
 * The domain type refers to an underlying messaging software implementation, 
 * and the domain name is interpreted by the implementation. Generally domains with
 * different UDL's are isolated from each other, but this is not necessarily the 
 * case.  For example, users can easily create gateways between different domains,
 * or different domain servers may serve into the same messaging namespace.
 * 
 * The full domain specifier for the CODA domain implementation looks like:
 *
 *      coda://node:port/namespace?param1=val1(&param2=val2)
 *
 * where node:port correspond to the node and port of a CODA message server, and 
 * namespace allows for multiple domains on the same server.  If the port is missing 
 * a default port is used.  Parameters are optional and not specified at this time.
 * Currently different CODA domains are completely isolated from each other.
 *
 * A process can connect to multiple domains if desired.  Note that the CODA domain
 * is implemented in a "heavyweight" manner, via separate threads, processes, etc.
 * The efficient, lightweight way to distribute messages within the CODA domain is
 * to use subjects (see below).
 *
 *
 * Messages
 * --------
 *
 * Messages are sent via cMsgSend() and cMsgGet().  Messages have a type and are 
 * sent to a subject, and both are arbitrary strings.  The payload consists of
 * a single text string.  Users must call cMsgFlush() to initiate delivery of messages 
 * in the outbound send queues, although the implementation may deliver messages 
 * before cMsgFlush() is called.  Additional message meta-data may be set by the user
 * (see below), although much of it is set by the system.
 *
 * Message consumers ask the system to deliver messages to them that match various 
 * subject/type combinations (each may be NULL).  The messages are delivered 
 * asynchronously to callbacks (via cMsgSubscribe()).  cMsgFreeMessage() must be 
 * called when the user is done processing the message.  Synchronous or RPC-like 
 * messaging is also possible via cMsgGet().
 *
 * cMsgReceiveStart() must be called to start delivery of messages to callbacks.  
 *
 * In the CODA domain perl-like subject wildcard characters are supported, multiple 
 * callbacks for the same subject/type are allowed, and each callback executes in 
 * its own thread.
 *
 *
 *
 *
 * Domain Implementations
 * ----------------------
 *
 * The CODA group is supplying two default domain implementations, but users can
 * add additional implementations if desired.  This is not particularly difficult,
 * but you should probably talk to the CODA group if you want to do this.  The 
 * default implementations have domainTypes "CODA" and "FILE".
 *
 * The CODA domain supports standard asynchronous publish/subscribe messaging via
 * a separate FIPA agent based server system, but no explicit synchronous messaging
 * (i.e. cMsgGet() is not implemented yet).  Users can effectively implement
 * RPC-like communications via careful use of asynchronous publish/subscribe 
 * messaging and the senderToken (see below).
 *
 * The FILE domain simply logs text to files, and the only functions implemented
 * are cMsgConnect (calls fopen), cMsgSend (calls fwrite), and cMsgDisconnect (calls 
 * fclose).
 *
 * Note that new domain types can be added explicitely at the API level (within cMsg.c),
 * or effectively via a CODA domain proxy server.  Contact the DAQ group for details.
 *
 *
 *
 * --------------------------------------------------------------------------------------
 * cMsg API Specification
 * --------------------------------------------------------------------------------------
 *
 *   Note:  most calls return an integer cMsg error code, defined below.
 *
 *
 *
 * int cMsgConnect(char *myUDL, char *myName, char *myDescription, int *domainId)
 *
 *   Called once to connect to a domain.  myName must be unique within the domain.  
 *   myDesctiption is an arbitrary string.  If successful, fills domainId, required
 *   by many calls below.  
 *  
 *
 *
 *  void *cMsgCreateMessage(void)
 *
 *   Create an empty message object (some meta-data is set by default).  Returns NULL
 *   on failure.  Must call cMsgFree() after cMsgSend() to avoid memory leaks.  Note
 *   that a message can be modified and sent multiple times before cMsgFree() is 
 *   called.
 *
 *
 *
 * int cMsgSetSubject(void *msg, char *subject)
 *
 *   Set the subject field of a message object
 *
 *
 *
 * int cMsgSetType(void *msg, char *type)
 *
 *   Set the type field of a message object
 *
 *
 *
 * int cMsgSetText(void *msg, char *text)
 *
 *   Set the text field of a message object
 *
 *
 *
 * int cMsgSetSenderToken(void *msg, int senderToken)
 *
 *   Set the senderToken field of a message object
 *
 *
 *
 * int cMsgSend(int domainId, void *msg)
 *
 *   Queue up a message for delivery.  Must call cMsgFlush() to force delivery,
 *   although the system may deliver messages before the flush call.
 *
 *
 *
 * int cMsgFlush(int domainId)
 *
 *   Force flush of all outgoing send message queues.  An implementation may flush queues
 *   on occasion by itself.
 *
 *
 *
 * int cMsgSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback, void *userArg)
 *
 *    Subscribe to subject/type and deliver message to callback(userarg).
 *
 *
 *
 * int cMsgUnSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback)
 *
 *    Unsubscribe from subject/type/callback combination.
 *
 *
 *
 *  int cMsgGet(int domainId, void *sendMsg, time_t timeout, void **replyMsg)
 *
 *   Synchronously send message and get reply.  Fail if no reply message
 *   received within timeout. Independent of receive start/stop state.
 *
 *
 *
 * int cMsgReceiveStart(int domainId)
 *
 *    Enable receipt of subscribed messages and delivery to callbacks.  
 *
 *
 *
 * int cMsgReceiveStop(int domainId)
 *
 *    Stop receipt of subscribed messages and delivery to callbacks.  
 *
 *
 *
 * int cMsgFreeMessage(void *msg)
 *
 *    Free message and deallocate memory.  Must be called to avoid memory leaks.
 *
 *
 *
 * int cMsgDisconnect(int domainId)
 *
 *    Disconnect from domain, unregister all callbacks, stop message receipt, etc.
 *
 *
 *
 * char *cMsgPerror(int errorCode)
 *
 *    Return information about a cMsg error return code, NULL if illegal errorCode.
 *
 *
 *
 *  Access functions
 *  ----------------
 *
 * See below for a long list of information access functions, all of which should
 * be self-explanatory (e.g. cMsgGetName(), etc.)
 *
 *
 *
 *  Debug
 *  -----
 *
 *  int cMsgSetDebugLevel(int level)
 *
 *    Controls debug printout.  Default level is CMSG_DEBUG_ERROR, see below for 
 *    other levels.
 *
 *
 *----------------------------------------------------------------------------*/


#ifndef _cMsg_h
#define _cMsg_h


/* debug levels */
#define CMSG_DEBUG_NONE    0
#define CMSG_DEBUG_SEVERE  1
#define CMSG_DEBUG_ERROR   2
#define CMSG_DEBUG_WARN    3
#define CMSG_DEBUG_INFO    4


/* sccs id */
/* char sccsid[] = "%Z% cMsg abstract API definition"; */


/* required includes */
#include <time.h>


/* message receive callback */
typedef void (cMsgCallback) (void *msg, void *userArg);

/* pointer type to use for setting subscribe config */
typedef void *cMsgSubscribeConfig;


/* function prototypes */
#ifdef __cplusplus
extern "C" {
#endif


  /* basic functions */
  int 	cMsgConnect(char *myUDL, char *myName, char *myDescription, int *domainId);
  int 	cMsgSend(int domainId, void *msg);
  int 	cMsgFlush(int domainId);
  int 	cMsgSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback,
                      void *userArg, cMsgSubscribeConfig *config);
  int 	cMsgUnSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback);
  int   cMsgGet(int domainId, void *sendMsg, time_t timeout, void **replyMsg);
  int 	cMsgReceiveStart(int domainId);
  int 	cMsgReceiveStop(int domainId);
  int 	cMsgDisconnect(int domainId);
  char *cMsgPerror(int errorCode);
  


  /* message access functions */
  int      cMsgSetSubject(void *msg, char *subject);
  int      cMsgSetType(void *msg, char *type);
  int      cMsgSetText(void *msg, char *text);
  int      cMsgSetSenderToken(void *msg, int senderToken);
  int      cMsgFreeMessage(void *msg);
  void    *cMsgCreateMessage(void);
  void    *cMsgCopyMessage(void *msg);
  void     cMsgInitMessage(void *msg);

  int      cMsgGetSysMsgId(void *msg);

  time_t   cMsgGetReceiverTime(void *msg);
  int      cMsgGetReceiverSubscribeId(void *msg);

  char*    cMsgGetSender(void *msg);
  int      cMsgGetSenderId(void *msg);
  char*    cMsgGetSenderHost(void *msg);
  time_t   cMsgGetSenderTime(void *msg);
  int      cMsgGetSenderMsgId(void *msg);
  int      cMsgGetSenderToken(void *msg);

  char*    cMsgGetDomain(void *msg);
  char*    cMsgGetSubject(void *msg);
  char*    cMsgGetType(void *msg);
  char*    cMsgGetText(void *msg);


  /* system and domain info access functions */
  int cMsgGetUDL(int domainId, char *udl, size_t size);
  int cMsgGetName(int domainId, char *name, size_t size);
  int cMsgGetDescription(int domainId, char *description, size_t size);
  int cMsgGetInitState(int domainId, int *initState);
  int cMsgGetReceiveState(int domainId, int *receiveState);
  
  /* subscribe configuration functions */
  cMsgSubscribeConfig *cMsgSubscribeConfigCreate(void);
  int cMsgSubscribeConfigDestroy(cMsgSubscribeConfig *config);
  int cMsgSubscribeSetMaxCueSize(cMsgSubscribeConfig *config, int size);
  int cMsgSubscribeGetMaxCueSize(cMsgSubscribeConfig *config, int *size);
  int cMsgSubscribeSetSkipSize(cMsgSubscribeConfig *config, int size);
  int cMsgSubscribeGetSkipSize(cMsgSubscribeConfig *config, int *size);
  int cMsgSubscribeSetMaySkip(cMsgSubscribeConfig *config, int maySkip);
  int cMsgSubscribeGetMaySkip(cMsgSubscribeConfig *config, int *maySkip);
  int cMsgSubscribeSetMustSerialize(cMsgSubscribeConfig *config, int serialize);
  int cMsgSubscribeGetMustSerialize(cMsgSubscribeConfig *config, int *serialize);

  /* for debugging */
  int  	cMsgSetDebugLevel(int level);


#ifdef __cplusplus
}
#endif


/* return codes */
enum {
  CMSG_OK               = 0,
  CMSG_ERROR,
  CMSG_TIMEOUT,
  CMSG_NOT_IMPLEMENTED,
  CMSG_BAD_ARGUMENT,
  CMSG_BAD_FORMAT,
  CMSG_BAD_DOMAIN_TYPE,
  CMSG_NAME_EXISTS,
  CMSG_NOT_INITIALIZED,
  CMSG_ALREADY_INIT,
  CMSG_LOST_CONNECTION,
  CMSG_NETWORK_ERROR,
  CMSG_SOCKET_ERROR,
  CMSG_PEND_ERROR,
  CMSG_ILLEGAL_MSGTYPE,
  CMSG_OUT_OF_MEMORY,
  CMSG_OUT_OF_RANGE,
  CMSG_LIMIT_EXCEEDED,
  CMSG_BAD_DOMAIN_ID,
  CMSG_BAD_MESSAGE,
  CMSG_WRONG_DOMAIN_TYPE,
  CMSG_NO_CLASS_FOUND
};


#endif /* _cMsg_h */

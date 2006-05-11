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
 *  Defines cMsg API and return codes                                         *
 *                                                                            *
 *                                                                            *
 *----------------------------------------------------------------------------*/
 
/**
 * @file
 * This is the one necessary header file for all cMsg C users.  C++ users must 
 *  include cMsg.hxx instead, which includes this file.
 *
 *
 *
 * <b>INTRODUCTION</b>
 *
 * cMsg is a simple, abstract API to an arbitrary underlying message service. It 
 * is powerful enough to support synchronous and asynchronous point-to-point and 
 * publish/subscribe communication, and network-accessible message queues.  Note 
 * that a given underlying implementation may not necessarily implement all these 
 * features.  
 *
 *
 * <b>DOMAINS</b>
 *
 * The abstraction relies on the important concept of a "domain", specified via a 
 * "Universal Domain Locator" (UDL) of the form:
 * 
 * <code><b>cMsg:domainType://domainInfo</b></code>
 *
 * The domain type refers to an underlying messaging software implementation, 
 * and the domain info is interpreted by the implementation. Generally domains with
 * different UDL's are isolated from each other, but this is not necessarily the 
 * case.  For example, users can easily create gateways between different domains,
 * or different domain servers may serve into the same messaging namespace.
 * 
 * The full domain specifier for the full cMsg domain looks like:
 *
 * <code><b>cMsg:cMsg://node:port/cMsg/namespace?param1=val1(&param2=val2)</b></code>
 *
 * where node:port correspond to the node and port of a cMsg nameserver, and 
 * namespace allows for multiple namespaces on the same server.  If the port is missing 
 * a default port is used.  Parameters are optional and not specified at this time.
 * Currently different cMsg domains are completely isolated from each other. A
 * process can connect to multiple domains if desired. 
 *
 *
 * <b>MESSAGES</b>
 *
 * Messages are sent via cMsgSend() and related functions.  Messages have a type and are 
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
 * messaging is also possible via cMsgSendAndGet().
 *
 * cMsgReceiveStart() must be called to start delivery of messages to callbacks.  
 *
 * In the cMsg domain perl-like subject wildcard characters are supported, multiple 
 * callbacks for the same subject/type are allowed, and each callback executes in 
 * its own thread.
 *
 *

 * <b>ADDITIONAL INFORMATION</b>
 *
 * See the cMsg User's Guide and the cMsg Developer's Guide for more information.
 * See the cMsg Doxygen and Java docs for the full API specification.
 *
 */


#ifndef _cMsg_h
#define _cMsg_h


/* required includes */
#include <time.h>
#include <cMsgConstants.h>


/** Subscribe configuration. */
typedef void *cMsgSubscribeConfig;

/** Shutdown handler function. */
typedef void (cMsgShutdownHandler) (void *userArg);

/** Callback function. */
typedef void (cMsgCallbackFunc) (void *msg, void *userArg);

/* function prototypes */
#ifdef __cplusplus
extern "C" {
#endif


  /* basic functions */
  int 	cMsgConnect           (const char *myUDL, const char *myName, const char *myDescription,
                               int *domainId);
  int 	cMsgSend              (int domainId, void *msg);
  int   cMsgSyncSend          (int domainId, void *msg, int *response);
  int 	cMsgFlush             (int domainId);
  int 	cMsgSubscribe         (int domainId, const char *subject, const char *type, cMsgCallbackFunc *callback,
                               void *userArg, cMsgSubscribeConfig *config, void **handle);
  int 	cMsgUnSubscribe       (int domainId, void *handle);
  int   cMsgSendAndGet        (int domainId, void *sendMsg, const struct timespec *timeout, void **replyMsg);
  int   cMsgSubscribeAndGet   (int domainId, const char *subject, const char *type,
                               const struct timespec *timeout, void **replyMsg);
  int 	cMsgReceiveStart      (int domainId);
  int 	cMsgReceiveStop       (int domainId);
  int 	cMsgDisconnect        (int domainId);
  int   cMsgSetShutdownHandler(int domainId, cMsgShutdownHandler *handler, void *userArg);
  int   cMsgShutdownClients   (int domainId, const char *client, int flag);
  int   cMsgShutdownServers   (int domainId, const char *server, int flag);
  char *cMsgPerror            (int errorCode);
  
  
  /* message access functions */
  int    cMsgFreeMessage          (void *msg);
  void  *cMsgCreateMessage        (void);
  void  *cMsgCreateNewMessage     (void *vmsg);
  void  *cMsgCopyMessage          (void *msg);
  void   cMsgInitMessage          (void *msg);
  void  *cMsgCreateResponseMessage(void *vmsg);
  void  *cMsgCreateNullResponseMessage(void *vmsg);
  
  int    cMsgGetVersion           (void *vmsg, int *version);
  int    cMsgGetGetRequest        (void *vmsg, int *getRequest);
  
  int    cMsgSetGetResponse       (void *vmsg, int  getReponse);
  int    cMsgGetGetResponse       (void *vmsg, int *getReponse);
  
  int    cMsgSetNullGetResponse   (void *vmsg, int  nullGetResponse);
  int    cMsgGetNullGetResponse   (void *vmsg, int *nullGetResponse);
    
  int    cMsgGetDomain            (void *vmsg, char **domain);
  int    cMsgGetCreator           (void *vmsg, char **creator);
  
  int    cMsgSetSubject           (void *vmsg, const char  *subject);
  int    cMsgGetSubject           (void *vmsg, char **subject);
  
  int    cMsgSetType              (void *vmsg, const char  *type);
  int    cMsgGetType              (void *vmsg, char **type);
  
  int    cMsgSetText              (void *vmsg, const char  *text);
  int    cMsgGetText              (void *vmsg, char **text);
  
  int    cMsgSetUserInt           (void *vmsg, int  userInt);
  int    cMsgGetUserInt           (void *vmsg, int *userInt);
  
  int    cMsgSetUserTime          (void *vmsg, const struct timespec *userTime);
  int    cMsgGetUserTime          (void *vmsg, struct timespec *userTime);
  
  int    cMsgGetSender            (void *vmsg, char  **sender);
  int    cMsgGetSenderHost        (void *vmsg, char  **senderHost);
  int    cMsgGetSenderTime        (void *vmsg, struct timespec *senderTime);
  
  int    cMsgGetReceiver          (void *vmsg, char  **receiver);
  int    cMsgGetReceiverHost      (void *vmsg, char  **receiverHost);
  int    cMsgGetReceiverTime      (void *vmsg, struct timespec *receiverTime);
  
  int    cMsgSetByteArrayLength   (void *vmsg, int  length);
  int    cMsgGetByteArrayLength   (void *vmsg, int *length);
  
  int    cMsgSetByteArrayOffset   (void *vmsg, int  offset);
  int    cMsgGetByteArrayOffset   (void *vmsg, int *offset);
  
  int    cMsgSetByteArrayEndian   (void *vmsg, int endian);
  int    cMsgGetByteArrayEndian   (void *vmsg, int *endian);
  int    cMsgNeedToSwap           (void *vmsg, int *swap);
  
  int    cMsgSetByteArray         (void *vmsg, char  *array);
  int    cMsgGetByteArray         (void *vmsg, char **array);
  
  int    cMsgSetByteArrayAndLimits(void *vmsg, char *array, int offset, int length);
  int    cMsgCopyByteArray        (void *vmsg, char *array, int offset, int length);

  int    cMsgToString             (void *vmsg, char **string);


  /* system and domain info access functions */
  int cMsgGetUDL         (int domainId, char **udl);
  int cMsgGetName        (int domainId, char **name);
  int cMsgGetDescription (int domainId, char **description);
  int cMsgGetConnectState   (int domainId, int *connected);
  int cMsgGetReceiveState(int domainId, int   *receiveState);

  
  /* subscribe configuration functions */
  cMsgSubscribeConfig *cMsgSubscribeConfigCreate(void);
  int cMsgSubscribeConfigDestroy       (cMsgSubscribeConfig *config);
  
  int cMsgSubscribeSetMaxCueSize       (cMsgSubscribeConfig *config, int  size);
  int cMsgSubscribeGetMaxCueSize       (cMsgSubscribeConfig *config, int *size);
  
  int cMsgSubscribeSetSkipSize         (cMsgSubscribeConfig *config, int  size);
  int cMsgSubscribeGetSkipSize         (cMsgSubscribeConfig *config, int *size);
  
  int cMsgSubscribeSetMaySkip          (cMsgSubscribeConfig *config, int  maySkip);
  int cMsgSubscribeGetMaySkip          (cMsgSubscribeConfig *config, int *maySkip);
  
  int cMsgSubscribeSetMustSerialize    (cMsgSubscribeConfig *config, int  serialize);
  int cMsgSubscribeGetMustSerialize    (cMsgSubscribeConfig *config, int *serialize);
  
  int cMsgSubscribeSetMaxThreads       (cMsgSubscribeConfig *config, int  threads);
  int cMsgSubscribeGetMaxThreads       (cMsgSubscribeConfig *config, int *threads);
  
  int cMsgSubscribeSetMessagesPerThread(cMsgSubscribeConfig *config, int  mpt);
  int cMsgSubscribeGetMessagesPerThread(cMsgSubscribeConfig *config, int *mpt);


  /* for debugging */
  int cMsgSetDebugLevel(int level);


#ifdef __cplusplus
}
#endif


#endif /* _cMsg_h */

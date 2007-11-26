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
#ifndef _cMsgConstants_h
#include "cMsgConstants.h"
#endif

#ifndef VXWORKS
#include <inttypes.h>
#endif

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

#ifdef Darwin
#define CLOCK_REALTIME 0
void clock_gettime(int dummy, struct timespec *t1);
#endif

#ifdef VXWORKS
  char *strdup(const char *s1);
  int   strcasecmp(const char *s1, const char *s2);
  int   strncasecmp(const char *s1, const char *s2, size_t n);
#endif


  /* basic functions */
  int 	cMsgConnect           (const char *myUDL, const char *myName, const char *myDescription,
                               void **domainId);
  int 	cMsgSend              (void *domainId, const void *msg);
  int   cMsgSyncSend          (void *domainId, const void *msg, const struct timespec *timeout, int *response);
  int 	cMsgFlush             (void *domainId, const struct timespec *timeout);
  int 	cMsgSubscribe         (void *domainId, const char *subject, const char *type, cMsgCallbackFunc *callback,
                               void *userArg, cMsgSubscribeConfig *config, void **handle);
  int 	cMsgUnSubscribe       (void *domainId, void *handle);
  int   cMsgSendAndGet        (void *domainId, const void *sendMsg, const struct timespec *timeout, void **replyMsg);
  int   cMsgSubscribeAndGet   (void *domainId, const char *subject, const char *type,
                               const struct timespec *timeout, void **replyMsg);
  int   cMsgMonitor           (void *domainId, const char *command, void **replyMsg);
  int 	cMsgReceiveStart      (void *domainId);
  int 	cMsgReceiveStop       (void *domainId);
  int 	cMsgDisconnect        (void **domainId);
  int   cMsgSetShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg);
  int   cMsgShutdownClients   (void *domainId, const char *client, int flag);
  int   cMsgShutdownServers   (void *domainId, const char *server, int flag);
  char *cMsgPerror            (int errorCode);
  
  
  /* message access functions */
  int    cMsgFreeMessage          (void **vmsg);
  void  *cMsgCreateMessage        (void);
  void  *cMsgCreateNewMessage     (const void *vmsg);
  void  *cMsgCopyMessage          (const void *vmsg);
  int    cMsgInitMessage          (void *vmsg);
  void  *cMsgCreateResponseMessage(const void *vmsg);
  void  *cMsgCreateNullResponseMessage(const void *vmsg);
  int    cMsgWasSent              (const void *vmsg, int *hasBeenSent);
  
  int    cMsgGetVersion           (const void *vmsg, int *version);
  int    cMsgGetGetRequest        (const void *vmsg, int *getRequest);
  
  int    cMsgSetGetResponse       (      void *vmsg, int  getReponse);
  int    cMsgGetGetResponse       (const void *vmsg, int *getReponse);
  
  int    cMsgSetNullGetResponse   (      void *vmsg, int  nullGetResponse);
  int    cMsgGetNullGetResponse   (const void *vmsg, int *nullGetResponse);
    
  int    cMsgGetDomain            (const void *vmsg, const char **domain);
  int    cMsgGetCreator           (const void *vmsg, const char **creator);
  
  int    cMsgSetSubject           (      void *vmsg, const char  *subject);
  int    cMsgGetSubject           (const void *vmsg, const char **subject);
  
  int    cMsgSetType              (      void *vmsg, const char  *type);
  int    cMsgGetType              (const void *vmsg, const char **type);
  
  int    cMsgSetText              (      void *vmsg, const char  *text);
  int    cMsgGetText              (const void *vmsg, const char **text);
  
  int    cMsgSetUserInt           (      void *vmsg, int  userInt);
  int    cMsgGetUserInt           (const void *vmsg, int *userInt);
  
  int    cMsgSetUserTime          (      void *vmsg, const struct timespec *userTime);
  int    cMsgGetUserTime          (const void *vmsg, struct timespec *userTime);
  
  int    cMsgGetSender            (const void *vmsg, const char  **sender);
  int    cMsgGetSenderHost        (const void *vmsg, const char  **senderHost);
  int    cMsgGetSenderTime        (const void *vmsg, struct timespec *senderTime);
  
  int    cMsgGetReceiver          (const void *vmsg, const char  **receiver);
  int    cMsgGetReceiverHost      (const void *vmsg, const char  **receiverHost);
  int    cMsgGetReceiverTime      (const void *vmsg, struct timespec *receiverTime);
  
  int    cMsgSetByteArrayLength   (      void *vmsg, int  length);
  int    cMsgGetByteArrayLength   (const void *vmsg, int *length);
  
  int    cMsgSetByteArrayOffset   (      void *vmsg, int  offset);
  int    cMsgGetByteArrayOffset   (const void *vmsg, int *offset);
  
  int    cMsgSetByteArrayEndian   (      void *vmsg, int endian);
  int    cMsgGetByteArrayEndian   (const void *vmsg, int *endian);
  int    cMsgNeedToSwap           (const void *vmsg, int *swap);
  
  int    cMsgSetByteArray         (      void *vmsg, char  *array);
  int    cMsgGetByteArray         (const void *vmsg, char **array);
  
  int    cMsgSetByteArrayAndLimits(      void *vmsg, char *array, int offset, int length);
  int    cMsgCopyByteArray        (      void *vmsg, char *array, int offset, int length);
  /* message context stuff */
  int    cMsgGetSubscriptionDomain (const void *vmsg, const char **domain);
  int    cMsgGetSubscriptionSubject(const void *vmsg, const char **subject);
  int    cMsgGetSubscriptionType   (const void *vmsg, const char **type);
  int    cMsgGetSubscriptionUDL    (const void *vmsg, const char **udl);
  int    cMsgGetSubscriptionCueSize(const void *vmsg, int   *size);
  int    cMsgSetReliableSend       (      void *vmsg, int boolean);
  int    cMsgGetReliableSend       (      void *vmsg, int *boolean);
  /*  misc. */
  int    cMsgToString              (      void *vmsg, char **string, int binary);
  
  /* compound payload stuff - 63 user routines */
  
  void   cMsgPayloadClear            (      void *vmsg);
  void   cMsgPayloadPrintout         (      void *vmsg);
  int    cMsgSetPayloadFromText      (      void *vmsg, const char *text);
  int    cMsgSetSystemFieldsFromText (      void *vmsg, const char *text);
  int    cMsgSetAllFieldsFromText    (      void *vmsg, const char *text);
 /* char  *cMsgGetPayloadText          (const void *vmsg);*/
  int    cMsgGetPayloadText          (const void *vmsg, char **buf, char *dst, size_t size, size_t *length);
  int    cMsgGetPayloadTextLength    (const void *vmsg);
  int    cMsgHasPayload              (const void *vmsg, int *hasPayload);
  int    cMsgCopyPayload             (const void *vmsgFrom, void *vmsgTo);
  const char *cMsgGetFieldDescription(const void *vmsg);
  
  int    cMsgGoToFieldName           (      void *vmsg, const char *name);
  int    cMsgGoToField               (      void *vmsg, int  position);
  int    cMsgGetFieldPosition        (const void *vmsg, int *position);
  int    cMsgNextField               (      void *vmsg);
  int    cMsgHasNextField            (const void *vmsg);
  int    cMsgFirstField              (      void *vmsg);
  int    cMsgRemoveFieldName         (      void *vmsg, const char *name);
  int    cMsgRemoveField             (      void *vmsg, int position);
  int    cMsgMoveFieldName           (      void *vmsg, const char *name, int placeTo);
  int    cMsgMoveField               (      void *vmsg, int placeFrom, int placeTo);

  char  *cMsgGetFieldName            (const void *vmsg);
  char  *cMsgGetFieldText            (const void *vmsg);
  int    cMsgGetFieldType            (const void *vmsg, int *type);
  int    cMsgGetFieldCount           (const void *vmsg, int *count);

  int    cMsgGetBinary               (const void *vmsg, char **val, size_t *len, int *endian);
  int    cMsgGetMessage              (const void *vmsg, void **val);
  
  int    cMsgGetString               (const void *vmsg, char **val);
  int    cMsgGetStringArray          (const void *vmsg, const char ***array, size_t *len);
  
  int    cMsgGetFloat                (const void *vmsg, float  *val);
  int    cMsgGetFloatArray           (const void *vmsg, const float  **vals, size_t *len);
  int    cMsgGetDouble               (const void *vmsg, double *val);
  int    cMsgGetDoubleArray          (const void *vmsg, const double **vals, size_t *len);
  
  int    cMsgGetInt8                 (const void *vmsg, int8_t   *val);
  int    cMsgGetInt16                (const void *vmsg, int16_t  *val);
  int    cMsgGetInt32                (const void *vmsg, int32_t  *val);
  int    cMsgGetInt64                (const void *vmsg, int64_t  *val);
  int    cMsgGetUint8                (const void *vmsg, uint8_t  *val);
  int    cMsgGetUint16               (const void *vmsg, uint16_t *val);
  int    cMsgGetUint32               (const void *vmsg, uint32_t *val);
  int    cMsgGetUint64               (const void *vmsg, uint64_t *val);
  
  int    cMsgGetInt8Array            (const void *vmsg, const int8_t   **vals, size_t *len);
  int    cMsgGetInt16Array           (const void *vmsg, const int16_t  **vals, size_t *len);
  int    cMsgGetInt32Array           (const void *vmsg, const int32_t  **vals, size_t *len);
  int    cMsgGetInt64Array           (const void *vmsg, const int64_t  **vals, size_t *len);
  int    cMsgGetUint8Array           (const void *vmsg, const uint8_t  **vals, size_t *len);
  int    cMsgGetUint16Array          (const void *vmsg, const uint16_t **vals, size_t *len);
  int    cMsgGetUint32Array          (const void *vmsg, const uint32_t **vals, size_t *len);
  int    cMsgGetUint64Array          (const void *vmsg, const uint64_t **vals, size_t *len);

  int    cMsgAddInt8                 (      void *vmsg, const char *name, int8_t   val, int place);
  int    cMsgAddInt16                (      void *vmsg, const char *name, int16_t  val, int place);
  int    cMsgAddInt32                (      void *vmsg, const char *name, int32_t  val, int place);
  int    cMsgAddInt64                (      void *vmsg, const char *name, int64_t  val, int place);
  int    cMsgAddUint8                (      void *vmsg, const char *name, uint8_t  val, int place);
  int    cMsgAddUint16               (      void *vmsg, const char *name, uint16_t val, int place);
  int    cMsgAddUint32               (      void *vmsg, const char *name, uint32_t val, int place);
  int    cMsgAddUint64               (      void *vmsg, const char *name, uint64_t val, int place);

  int    cMsgAddInt8Array            (      void *vmsg, const char *name, const int8_t   vals[], int len, int place);
  int    cMsgAddInt16Array           (      void *vmsg, const char *name, const int16_t  vals[], int len, int place);
  int    cMsgAddInt32Array           (      void *vmsg, const char *name, const int32_t  vals[], int len, int place);
  int    cMsgAddInt64Array           (      void *vmsg, const char *name, const int64_t  vals[], int len, int place);
  int    cMsgAddUint8Array           (      void *vmsg, const char *name, const uint8_t  vals[], int len, int place);
  int    cMsgAddUint16Array          (      void *vmsg, const char *name, const uint16_t vals[], int len, int place);
  int    cMsgAddUint32Array          (      void *vmsg, const char *name, const uint32_t vals[], int len, int place);
  int    cMsgAddUint64Array          (      void *vmsg, const char *name, const uint64_t vals[], int len, int place);

  int    cMsgAddString               (      void *vmsg, const char *name, const char *val, int place);
  int    cMsgAddStringArray          (      void *vmsg, const char *name, const char **vals, int len, int place);

  int    cMsgAddFloat                (      void *vmsg, const char *name, float  val, int place);
  int    cMsgAddDouble               (      void *vmsg, const char *name, double val, int place);
  int    cMsgAddFloatArray           (      void *vmsg, const char *name, const float vals[],  int len, int place);
  int    cMsgAddDoubleArray          (      void *vmsg, const char *name, const double vals[], int len, int place);

  int    cMsgAddBinary               (      void *vmsg, const char *name, const char *src, size_t size, int place, int endian);
  int    cMsgAddMessage              (      void *vmsg, char *name, const void *vmessage, int place);



  /* system and domain info access functions */
  int cMsgGetUDL         (void *domainId, char **udl);
  int cMsgGetName        (void *domainId, char **name);
  int cMsgGetDescription (void *domainId, char **description);
  int cMsgGetConnectState(void *domainId,  int *connectState);
  int cMsgGetReceiveState(void *domainId,  int *receiveState);

  
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

  int cMsgSubscribeSetStackSize        (cMsgSubscribeConfig *config, size_t size);
  int cMsgSubscribeGetStackSize        (cMsgSubscribeConfig *config, size_t *size);


  /* for debugging */
  int cMsgSetDebugLevel(int level);


#ifdef __cplusplus
}
#endif


#endif /* _cMsg_h */

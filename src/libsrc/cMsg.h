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
#include <stdlib.h>
#include <time.h>
#ifndef _cMsgConstants_h
#include "cMsgConstants.h"
#endif

#include <inttypes.h>

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
                               void **domainId);
  int   cMsgReconnect         (void *domainId);
  int 	cMsgSend              (void *domainId, void *msg);
  int   cMsgSyncSend          (void *domainId, void *msg, const struct timespec *timeout, int *response);
  int 	cMsgFlush             (void *domainId, const struct timespec *timeout);
  int 	cMsgSubscribe         (void *domainId, const char *subject, const char *type, cMsgCallbackFunc *callback,
                               void *userArg, cMsgSubscribeConfig *config, void **handle);
  int 	cMsgUnSubscribe       (void *domainId, void *handle);
  int   cMsgSubscriptionPause (void *domainId, void *handle);
  int   cMsgSubscriptionResume(void *domainId, void *handle);
  int   cMsgSubscriptionQueueClear(void *domainId, void *handle);
  int   cMsgSubscriptionQueueCount(void *domainId, void *handle, int *count);
  int   cMsgSubscriptionQueueIsFull(void *domainId, void *handle, int *full);
  int   cMsgSubscriptionMessagesTotal(void *domainId, void *handle, int *total);
  int   cMsgSendAndGet        (void *domainId, void *sendMsg, const struct timespec *timeout, void **replyMsg);
  int   cMsgSubscribeAndGet   (void *domainId, const char *subject, const char *type,
                               const struct timespec *timeout, void **replyMsg);
  int   cMsgMonitor           (void *domainId, const char *command, void **replyMsg);
  int 	cMsgReceiveStart      (void *domainId);
  int 	cMsgReceiveStop       (void *domainId);
  int 	cMsgDisconnect        (void **domainId);
  int   cMsgSetShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg);
  int   cMsgShutdownClients   (void *domainId, const char *client, int flag);
  int   cMsgShutdownServers   (void *domainId, const char *server, int flag);
  int   cMsgGetConnectState   (void *domainId,  int *connectState);
  int   cMsgSetUDL            (void *domainId, const char *udl);
  int   cMsgGetCurrentUDL     (void *domainId, const char **udl);
  int   cMsgGetServerHost     (void *domainId, const char **ipAddress);
  int   cMsgGetServerPort     (void *domainId, int *port);
  int   cMsgGetInfo           (void *domainId, const char *command, char **string);
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
  int    cMsgGetPayloadText       (const void *vmsg, const char **payloadText);
  
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
  
  int    cMsgSetByteArrayLength    (      void *vmsg, int  length);
  int    cMsgResetByteArrayLength  (      void *vmsg);
  int    cMsgGetByteArrayLength    (const void *vmsg, int *length);
  int    cMsgGetByteArrayLengthFull(const void *vmsg, int *length);
  
  int    cMsgSetByteArrayOffset   (      void *vmsg, int  offset);
  int    cMsgGetByteArrayOffset   (const void *vmsg, int *offset);
  
  int    cMsgSetByteArrayEndian   (      void *vmsg, int endian);
  int    cMsgGetByteArrayEndian   (const void *vmsg, int *endian);
  int    cMsgNeedToSwap           (const void *vmsg, int *swap);
  
  int    cMsgSetByteArray         (      void *vmsg, char *array, int length);
  int    cMsgSetByteArrayNoCopy   (      void *vmsg, char *array, int length);
  int    cMsgGetByteArray         (const void *vmsg, char **array);
  
  int    cMsgSetReliableSend      (      void *vmsg, int boolean);
  int    cMsgGetReliableSend      (      void *vmsg, int *boolean);
  
  /* message context stuff */
  int    cMsgGetSubscriptionDomain (const void *vmsg, const char **domain);
  int    cMsgGetSubscriptionSubject(const void *vmsg, const char **subject);
  int    cMsgGetSubscriptionType   (const void *vmsg, const char **type);
  int    cMsgGetSubscriptionUDL    (const void *vmsg, const char **udl);
  int    cMsgGetSubscriptionCueSize(const void *vmsg, int   *size);
  
  /*  misc. */
  int    cMsgToString              (const void *vmsg, char **string);
  int    cMsgToString2             (const void *vmsg, char **string, int binary,
                                    int compact, int noSystemFields);
  int    cMsgPayloadToString       (const void *vmsg, char **string, int binary,
                                    int compact, int noSystemFields);
  void   cMsgTrim                  (char *s);
  void   cMsgTrimChar              (char *s, char trimChar);
  void   cMsgTrimDoubleChars       (char *s, char trimChar);
 
  /* ***************************************** */
  /* compound payload stuff - 66 user routines */
  /* ***************************************** */
  int    cMsgAddHistoryToPayloadText (      void *vmsg, char *name, char *host, int64_t time, char **pTxt);
  int    cMsgSetHistoryLengthMax     (      void *vmsg, int len);
  int    cMsgGetHistoryLengthMax     (const void *vmsg, int *len);
  
  int    cMsgPayloadGet              (const void *vmsg, char **names,  int *types,  int len);
  int    cMsgPayloadGetInfo          (const void *vmsg, char ***names, int **types, int *len);
  int    cMsgPayloadGetCount         (const void *vmsg, int *count);
  int    cMsgPayloadContainsName     (const void *vmsg, const char *name);
  int    cMsgPayloadGetType          (const void *vmsg, const char *name, int *type);
  int    cMsgPayloadRemove           (      void *vmsg, const char *name);
  int    cMsgPayloadCopy             (const void *vmsgFrom, void *vmsgTo);

  int    cMsgPayloadUpdateText       (const void *vmsg);
  int    cMsgPayloadGetFieldText     (const void *vmsg, const char *name, const char **val);
  void   cMsgPayloadPrint            (const void *vmsg);
  
const char *cMsgPayloadFieldDescription(const void *vmsg, const char *name);
  
  /* users should NOT have access to these 3 routines */
  int    cMsgPayloadSetFromText             (      void *vmsg, const char *text);
  int    cMsgPayloadSetSystemFieldsFromText (      void *vmsg, const char *text);
  int    cMsgPayloadSetAllFieldsFromText    (void *vmsg, const char *text);

  void   cMsgPayloadReset            (      void *vmsg);
  void   cMsgPayloadClear            (      void *vmsg);
  int    cMsgHasPayload              (const void *vmsg, int *hasPayload);
 
  int    cMsgGetBinary               (const void *vmsg, const char *name, const char **val,
                                      int *len, int *endian);
  int    cMsgGetBinaryArray          (const void *vmsg, const char *name, const char ***vals,
                                      int **sizes, int **endians, int *count);

  int    cMsgGetMessage              (const void *vmsg, const char *name, const void **val);
  int    cMsgGetMessageArray         (const void *vmsg, const char *name, const void ***val, int *len);
  
  int    cMsgGetString               (const void *vmsg, const char *name, const char **val);
  int    cMsgGetStringArray          (const void *vmsg, const char *name, const char ***array, int *len);
  
  int    cMsgGetFloat                (const void *vmsg, const char *name, float  *val);
  int    cMsgGetFloatArray           (const void *vmsg, const char *name, const float  **vals, int *len);
  int    cMsgGetDouble               (const void *vmsg, const char *name, double *val);
  int    cMsgGetDoubleArray          (const void *vmsg, const char *name, const double **vals, int *len);
  
  int    cMsgGetInt8                 (const void *vmsg, const char *name, int8_t   *val);
  int    cMsgGetInt16                (const void *vmsg, const char *name, int16_t  *val);
  int    cMsgGetInt32                (const void *vmsg, const char *name, int32_t  *val);
  int    cMsgGetInt64                (const void *vmsg, const char *name, int64_t  *val);
  int    cMsgGetUint8                (const void *vmsg, const char *name, uint8_t  *val);
  int    cMsgGetUint16               (const void *vmsg, const char *name, uint16_t *val);
  int    cMsgGetUint32               (const void *vmsg, const char *name, uint32_t *val);
  int    cMsgGetUint64               (const void *vmsg, const char *name, uint64_t *val);
  
  int    cMsgGetInt8Array            (const void *vmsg, const char *name, const int8_t   **vals, int *len);
  int    cMsgGetInt16Array           (const void *vmsg, const char *name, const int16_t  **vals, int *len);
  int    cMsgGetInt32Array           (const void *vmsg, const char *name, const int32_t  **vals, int *len);
  int    cMsgGetInt64Array           (const void *vmsg, const char *name, const int64_t  **vals, int *len);
  int    cMsgGetUint8Array           (const void *vmsg, const char *name, const uint8_t  **vals, int *len);
  int    cMsgGetUint16Array          (const void *vmsg, const char *name, const uint16_t **vals, int *len);
  int    cMsgGetUint32Array          (const void *vmsg, const char *name, const uint32_t **vals, int *len);
  int    cMsgGetUint64Array          (const void *vmsg, const char *name, const uint64_t **vals, int *len);

  int    cMsgAddInt8                 (      void *vmsg, const char *name, int8_t   val);
  int    cMsgAddInt16                (      void *vmsg, const char *name, int16_t  val);
  int    cMsgAddInt32                (      void *vmsg, const char *name, int32_t  val);
  int    cMsgAddInt64                (      void *vmsg, const char *name, int64_t  val);
  int    cMsgAddUint8                (      void *vmsg, const char *name, uint8_t  val);
  int    cMsgAddUint16               (      void *vmsg, const char *name, uint16_t val);
  int    cMsgAddUint32               (      void *vmsg, const char *name, uint32_t val);
  int    cMsgAddUint64               (      void *vmsg, const char *name, uint64_t val);

  int    cMsgAddInt8Array            (      void *vmsg, const char *name, const int8_t   vals[], int len);
  int    cMsgAddInt16Array           (      void *vmsg, const char *name, const int16_t  vals[], int len);
  int    cMsgAddInt32Array           (      void *vmsg, const char *name, const int32_t  vals[], int len);
  int    cMsgAddInt64Array           (      void *vmsg, const char *name, const int64_t  vals[], int len);
  int    cMsgAddUint8Array           (      void *vmsg, const char *name, const uint8_t  vals[], int len);
  int    cMsgAddUint16Array          (      void *vmsg, const char *name, const uint16_t vals[], int len);
  int    cMsgAddUint32Array          (      void *vmsg, const char *name, const uint32_t vals[], int len);
  int    cMsgAddUint64Array          (      void *vmsg, const char *name, const uint64_t vals[], int len);

  int    cMsgAddString               (      void *vmsg, const char *name, const char *val);
  int    cMsgAddStringArray          (      void *vmsg, const char *name, const char **vals, int len);

  int    cMsgAddFloat                (      void *vmsg, const char *name, float  val);
  int    cMsgAddDouble               (      void *vmsg, const char *name, double val);
  int    cMsgAddFloatArray           (      void *vmsg, const char *name, const float vals[],  int len);
  int    cMsgAddDoubleArray          (      void *vmsg, const char *name, const double vals[], int len);

  int    cMsgAddBinary               (      void *vmsg, const char *name, const char *src, int size, int endian);
  int    cMsgAddBinaryArray          (      void *vmsg, const char *name, const char *src[], int number,
                                            const int size[], const int endian[]);
  int    cMsgAddMessage              (      void *vmsg, const char *name, const void *vmessage);
  int    cMsgAddMessageArray         (      void *vmsg, const char *name, const void *vmessage[], int len);

  char   *cMsgFloatChars(float f);
  char   *cMsgDoubleChars(double d);
  char   *cMsgIntChars(uint32_t i);


  /* system and domain info access functions */
  int cMsgGetUDL         (void *domainId, char **udl);
  int cMsgGetName        (void *domainId, char **name);
  int cMsgGetDescription (void *domainId, char **description);
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

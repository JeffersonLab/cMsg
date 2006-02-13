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
 
#ifndef _cMsgBase_h
#define _cMsgBase_h

/* required includes */
#include <time.h>

/* endian values */
/** Is big endian. */
#define CMSG_ENDIAN_BIG      0
/** Is little endian. */
#define CMSG_ENDIAN_LITTLE   1
/** Is same endian as local host. */
#define CMSG_ENDIAN_LOCAL    2
/** Is opposite endian as local host. */
#define CMSG_ENDIAN_NOTLOCAL 3
/** Switch recorded value of data's endian. */
#define CMSG_ENDIAN_SWITCH   4

/* debug levels */
/** No debugging output. */
#define CMSG_DEBUG_NONE    0
/** Output only severe (process-ending) errors for debugging. */
#define CMSG_DEBUG_SEVERE  1
/** Output only errors for debugging. */
#define CMSG_DEBUG_ERROR   2
/** Output warnings and errors for debugging. */
#define CMSG_DEBUG_WARN    3
/** Output everything for debugging. */
#define CMSG_DEBUG_INFO    4

/* shutdown flags */
/** When shutting down clients, include the calling client (me). */
#define CMSG_SHUTDOWN_INCLUDE_ME 1


/** Subscribe configuration. */
typedef void *cMsgSubscribeConfig;

/** Shutdown handler function. */
typedef void (cMsgShutdownHandler) (void *userArg);

/** Return codes. */
enum {
  CMSG_OK              = 0, /**< No error. */
  CMSG_ERROR,               /**< Generic error. */
  CMSG_TIMEOUT,             /**< Timeout. */
  CMSG_NOT_IMPLEMENTED,     /**< Feature not implemented. */
  CMSG_BAD_ARGUMENT,        /**< Function argument(s) have illegal value. */
  CMSG_BAD_FORMAT,          /**< Function argument(s) in wrong format. */
  CMSG_BAD_DOMAIN_TYPE,     /**< Domain type not supported. */
  CMSG_ALREADY_EXISTS,      /**< Unique item already exists. */
  CMSG_NOT_INITIALIZED,     /**< Connection not established - call cMsgConnect. */
  CMSG_ALREADY_INIT,        /**< Connection already established. */
  CMSG_LOST_CONNECTION,     /**< No network connection to cMsg server. */
  CMSG_NETWORK_ERROR,       /**< Communication error talking to server. */
  CMSG_SOCKET_ERROR,        /**< Error setting TCP socket option(s). */
  CMSG_PEND_ERROR,          /**< Error when waiting for messages to arrive. */
  CMSG_ILLEGAL_MSGTYPE,     /**< Received illegal message type. */
  CMSG_OUT_OF_MEMORY,       /**< No more memory available. */
  CMSG_OUT_OF_RANGE,        /**< Argument out of acceptable range. */
  CMSG_LIMIT_EXCEEDED,      /**< Trying to create too many of an item. */
  CMSG_BAD_DOMAIN_ID,       /**< Id does not match any existing domain. */
  CMSG_BAD_MESSAGE,         /**< Message is not in the correct form. */
  CMSG_WRONG_DOMAIN_TYPE,   /**< UDL does not match the server type. */
  CMSG_NO_CLASS_FOUND,      /**< Java class cannot be found to instantiate subdomain handler. */
  CMSG_DIFFERENT_VERSION,   /**< Client and server are different cMsg versions. */
  CMSG_WRONG_PASSWORD       /**< Wrong password given. */
};


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
  int 	cMsgSubscribe         (int domainId, const char *subject, const char *type, cMsgCallback *callback,
                               void *userArg, cMsgSubscribeConfig *config);
  int 	cMsgUnSubscribe       (int domainId, const char *subject, const char *type, cMsgCallback *callback,
                               void *userArg);
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
  
  int    cMsgSetByteArray         (void *vmsg, char  *array);
  int    cMsgGetByteArray         (void *vmsg, char **array);
  
  int    cMsgSetByteArrayAndLimits(void *vmsg, char *array, int offset, int length);
  int    cMsgCopyByteArray        (void *vmsg, char *array, int offset, int length);

  int    cMsgToString             (void *vmsg, char **string);


  /* system and domain info access functions */
  int cMsgGetUDL         (int domainId, char **udl);
  int cMsgGetName        (int domainId, char **name);
  int cMsgGetDescription (int domainId, char **description);
  int cMsgGetInitState   (int domainId, int   *initState);
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


#endif /* _cMsgBase_h */

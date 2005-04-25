/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Author:  Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *      Header for cMsg routines
 *
 *----------------------------------------------------------------------------*/


/**
 * @file
 * This is a necessary header file for all cMsg developers. It defines the
 * structures which are needed to implement the API. 
 */
 
#ifndef __cMsgPrivate_h
#define __cMsgPrivate_h


/* This file may be compiled with the C or C++ compiler.
 * cMsgCallback is defined as C version (function) or
 *   C++ version (class) via the following macro.
 *
 * EJW, 21-Mar-2005
 */
#ifdef cppversion
#include "cMsgBase.hxx"
# else
typedef void (cMsgCallback) (void *msg, void *userArg);
#endif

#include "cMsgBase.h"


#ifdef	__cplusplus
extern "C" {
#endif


/** Major version number. */
#define CMSG_VERSION_MAJOR 0
/** Minor version number. */
#define CMSG_VERSION_MINOR 9

/** The maximum number domain types that a client can connect to. */
#define MAX_DOMAIN_TYPES    10
/** The maximum number connections that a client can make. */
#define MAX_DOMAINS       100
/** The user's domain id is an index into the #domains array, offset by this amount. */
#define DOMAIN_ID_OFFSET 100

/** Is message a sendAndGet request? -- is stored in 1st bit. */
#define CMSG_IS_GET_REQUEST       0x1
/** Is message a response to a sendAndGet? -- is stored in 2nd bit. */
#define CMSG_IS_GET_RESPONSE      0x2
/** Is the response message null instead of a message? -- is stored in 3rd bit. */
#define CMSG_IS_NULL_GET_RESPONSE 0x4
/** Is the byte array in big endian form? -- is stored in 4th bit. */
#define CMSG_IS_BIG_ENDIAN 0x8


/** Debug level. */
extern int cMsgDebug;


/** This structure holds domain implementation function pointers. */
typedef struct domainFunctions_t {
  /** This function connects to a cMsg server. */
  int (*connect)         (const char *udl, const char *name, const char *description,
                          const char *UDLremainder, int *domainId); 
  
  /** This function sends a message to a cMsg server. */
  int (*send)            (int domainId, void *msg);
  
  /** This function sends a message to a cMsg server and receives a synchronous response. */
  int (*syncSend)        (int domainId, void *msg, int *response);
  
  /** This function sends any pending (queued up) communication with the server. */
  int (*flush)           (int domainId);
  
  /** This function subscribes to messages of the given subject and type. */
  int (*subscribe)       (int domainId, const char *subject, const char *type, cMsgCallback *callback,
                          void *userArg, cMsgSubscribeConfig *config);
  
  /** This functin unsubscribes to messages of the given subject, type and callback. */
  int (*unsubscribe)     (int domainId, const char *subject, const char *type, cMsgCallback *callback,
                          void *userArg);
  
  /**
   * This function gets one message from a one-time subscription to the given
   * subject and type.
   */
  int (*subscribeAndGet) (int domainId, const char *subject, const char *type,
                          const struct timespec *timeout, void **replyMsg);
  /**
   * This function gets one message from another cMsg client by sending out
   * an initial message to that responder.
   */
  int (*sendAndGet)      (int domainId, void *sendMsg, const struct timespec *timeout, void **replyMsg);
  
  /** This function enables the receiving of messages and delivery to callbacks. */
  int (*start)           (int domainId);
  
  /** This function disables the receiving of messages and delivery to callbacks. */
  int (*stop)            (int domainId);
  
  /** This function disconnects the client from its cMsg server. */
  int (*disconnect)      (int domainId);
  
  /** This function shuts down the given clients and/or servers. */
  int (*shutdown)        (int domainId, const char *client, const char *server, int flag);
  
  /** This function sets the shutdown handler. */
  int (*setShutdownHandler) (int domainId, cMsgShutdownHandler *handler, void *userArg);
  
} domainFunctions;


/** This structure holds function pointers by domain type. */
typedef struct domainTypeInfo_t {
  char *type;                 /**< Type of the domain. */
  domainFunctions *functions; /**< Pointer to structure of domain implementation functions. */
} domainTypeInfo;


/** This structure contains information about a domain connection. */
typedef struct cMsgDomain_t {
  int id;              /**< Index into an array of this domain structure. */
  int implId;          /**< Index into the array of the implementation domain structure. */

  /* other state variables */
  int initComplete;    /**< Is initialization of this structure complete? 0 = No, 1 = Yes */
  int receiveState;    /**< Is connection receiving callback messages? 0 = No, 1 = Yes */
  
  char *type;          /**< Domain type (eg cMsg, CA, SmartSockets, File, etc). */
  char *name;          /**< Name of user. */
  char *udl;           /**< UDL of cMsg name server. */
  char *description;   /**< User description. */
  char *UDLremainder;  /**< UDL with initial "cMsg:domain://" stripped off. */
    
  /** Pointer to a structure contatining pointers to domain implementation functions. */
  domainFunctions *functions;
  
} cMsgDomain;


/** This structure holds a message. */
typedef struct cMsg_t {
  /* general quantities */
  int     version;     /**< Major version of cMsg. */
  int     sysMsgId;    /**< Unique id set by system to track sendAndGet's. */
  int     info;        /**< Stores information in bit form (true = 1).
                        * - is message a sendAndGet request? 1st bit
                        * - is message a response to a sendAndGet request? 2nd bit
                        * - is response message NULL instead of a message? 3rd bit
                        * - is byte array data big endian? 4th bit
                        */
  char   *domain;      /**< Domain message is generated in. */
  char   *creator;     /**< Message was originally created by this user/sender. */
  int     reserved;    /**< Reserved for future use. */
  
  /* user-settable quantities */
  char   *subject;             /**< Subject of message. */
  char   *type;                /**< Type of message. */
  char   *text;                /**< Text of message. */
  int     userInt;             /**< User-defined integer. */
  struct timespec userTime;    /**< User-defined time. */
  char   *byteArray;           /**< Array of bytes. */
  int     byteArrayLength;     /**< Length (in bytes) of byte array data of interest. */
  int     byteArrayOffset;     /**< Index into byte array to data of interest. */
  

  /* sender quantities */
  char   *sender;              /**< Last sender of message. */
  char   *senderHost;          /**< Host of sender. */
  struct timespec senderTime;  /**< Time message was sent (sec since 12am, GMT, Jan 1st, 1970). */
  int     senderToken;         /**< Unique id generated by system to track sendAndGet's. */
  
  char   *receiver;            /**< Receiver of message. */
  char   *receiverHost;        /**< Host of receiver. */
  struct timespec receiverTime;/**< Time message was received (sec since 12am, GMT, Jan 1st, 1970). */
  int     receiverSubscribeId; /**< Unique id used by system in subscribes and subscribeAndGets. */  
  
  struct cMsg_t *next; /**< For using messages in a linked list. */
} cMsgMessage;


/** Commands/Requests sent from client to server. */
enum requestMsgId {
  CMSG_SERVER_CONNECT     = 0,       /**< Connect client to name server. */
  CMSG_SERVER_DISCONNECT,            /**< Disconnect client from name server. */
  CMSG_KEEP_ALIVE,                   /**< Tell me if you are alive. */
  CMSG_SHUTDOWN,                     /**< Shutdown the server/client. */
  CMSG_SEND_REQUEST,                 /**< Send request. */
  CMSG_SYNC_SEND_REQUEST,            /**< SyncSend request. */
  CMSG_SUBSCRIBE_REQUEST,            /**< Subscribe request. */
  CMSG_UNSUBSCRIBE_REQUEST,          /**< Unsubscribe request. */
  CMSG_SUBSCRIBE_AND_GET_REQUEST,    /**< SubscribeAndGet request. */
  CMSG_UNSUBSCRIBE_AND_GET_REQUEST,  /**< UnSubscribeAndGet request. */
  CMSG_SEND_AND_GET_REQUEST,         /**< SendAndGet request. */
  CMSG_UN_SEND_AND_GET_REQUEST       /**< UnSendAndGet request. */
};


/** Responses sent to client from server. */
enum responseMsgId {
  CMSG_GET_RESPONSE         = 20,    /**< SendAndGet response. */
  CMSG_GET_RESPONSE_IS_NULL,         /**< Get response is NULL. */
  CMSG_SUBSCRIBE_RESPONSE            /**< Subscribe response. */
};


/** This structure contains parameters used to control subscription callback behavior. */
typedef struct subscribeConfig_t {
  int  init;          /**< If structure was initialized, init = 1. */
  int  maySkip;       /**< May skip messages if too many are piling up in cue (if = 1). */
  int  mustSerialize; /**< Messages must be processed in order received (if = 1),
                           else messages may be processed by parallel threads. */
  int  maxCueSize;    /**< Maximum number of messages to cue for callback. */
  int  skipSize;      /**< Maximum number of messages to skip over (delete) from the 
                           cue for a callback when the cue size has reached it limit
                           (if maySkip = 1) . */
  int  maxThreads;    /**< Maximum number of supplemental threads to use for running
                           the callback if mustSerialize is 0 (off). */
  int  msgsPerThread; /**< Enough supplemental threads are started so that there are
                           at most this many unprocessed messages for each thread. */
} subscribeConfig;



#ifdef	__cplusplus
}
#endif

#endif

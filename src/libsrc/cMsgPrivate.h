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

#ifndef _cMsg_h
#include "cMsg.h"
#endif


#ifdef	__cplusplus
extern "C" {
#endif

#include <stdlib.h>


/** Macro taken from "Programming with POSIX threads' by Butenhof
 * that aborts a program while printing out the file, line, and error
 * message.
 * NOTE: the "do {" ... "} while (0);" bracketing around the macros
 * allows the err_abort and errno_abort macros to be used as if they
 * were function calls, even in contexts where a trailing ";" would
 * generate a null statement. For example,
 *
 *      if (status != 0)
 *          err_abort (status, "message");
 *      else
 *          return status;
 *
 * will not compile if err_abort is a macro ending with "}", because
 * C does not expect a ";" to follow the "}". Because C does expect
 * a ";" following the ")" in the do...while construct, err_abort and
 * errno_abort can be used as if they were function calls.
 */
#define cmsg_err_abort(code,text) do { \
    fprintf (stderr, "%s at \"%s\":%d: %s\n", \
        text, __FILE__, __LINE__, strerror (code)); \
    abort (); \
} while (0)

#define cmsg_err_exit(code,text) do { \
    fprintf (stderr, "%s at \"%s\":%d\n", \
        text, __FILE__, __LINE__); \
    exit (code); \
} while (0)

#define cmsg_err(text) do { \
    fprintf (stderr, "%s at \"%s\":%d\n", text, __FILE__, __LINE__); \
    } while (0)

/** Major version number. */
#define CMSG_VERSION_MAJOR 5
/** Minor version number. */
#define CMSG_VERSION_MINOR 2

/** The maximum number domain types that a client can connect to. */
#define CMSG_MAX_DOMAIN_TYPES   20

/* Access to information stored in the "info" member of the message structure */
/** Is message a sendAndGet request? -- is stored in 1st bit of info. */
#define CMSG_IS_GET_REQUEST       0x1
/** Is message a response to a sendAndGet? -- is stored in 2nd bit of info. */
#define CMSG_IS_GET_RESPONSE      0x2
/** Is the response message null instead of a message? -- is stored in 3rd bit of info. */
#define CMSG_IS_NULL_GET_RESPONSE 0x4
/** Is the byte array in big endian form? -- is stored in 4th bit of info. */
#define CMSG_IS_BIG_ENDIAN 0x8
/** Has the message been sent over the wire? -- is stored in 5th bit of info. */
#define CMSG_WAS_SENT 0x10
/** Does the message have a compound payload? -- is stored in 6th bit of info. */
#define CMSG_HAS_PAYLOAD 0x20
/**
 * If the message has a compound payload, is the payload only in text form
 * or has it been expanded into a list of real structures?
 * Stored in the 7th bit of info.
 */
#define CMSG_EXPANDED_PAYLOAD 0x40
/**
 * Does local server have no subscriber to send from sendAndGet?
 * Stored in 8rd bit of info.
 * This is only for internal use.
 */
#define CMSG_NULL_GET_SERVER_RESPONSE 0x80

/** Time format in XML. */
#define CMSG_TIME_FORMAT "%a %b %d %H:%M:%S %Z %Y"
    
/** The absolute maximum number of entries a message keeps
 * when recording the history of various parameters. */
#define CMSG_HISTORY_LENGTH_ABS_MAX 200
/** The maximum number of entries a message keeps
 * when recording the history of various parameters. */
#define CMSG_HISTORY_LENGTH_MAX 20
    
/** Is byte array copied in? -- is stored in 1st bit. */
#define CMSG_BYTE_ARRAY_IS_COPIED 0x1

/** Get high 32 bits of 64 bit int. */
#define CMSG_HIGHINT(i) ((int)(((i) >> 32) & 0x00000000FFFFFFFF))
/** Get low 32 bits of 64 bit int. */
#define CMSG_LOWINT(i)  ((int)((i) & 0x00000000FFFFFFFF))
/** Create unsigned 64 bit int out of 2, 32 bit ints. */
#define CMSG_64BIT_UINT(hi,lo) (((uint64_t)(hi) << 32) | ((uint64_t)(lo) & 0x00000000FFFFFFFF))
/** Create signed 64 bit int out of 2, 32 bit ints. */
#define CMSG_64BIT_INT(hi,lo)   (((int64_t)(hi) << 32) | ((int64_t)(lo)  & 0x00000000FFFFFFFF))

/** Debug level. */
extern int cMsgDebug;



/** Typedef for a domain's connection function */
typedef int (*CONNECT_PTR)     (const char *udl, const char *name, const char *description,
                                const char *UDLremainder, void **domainId); 
                                  
/** Typedef for a domain's send function */  
typedef int (*SEND_PTR)        (void *domainId, void *msg);

/** Typedef for a domain's syncSend function */  
typedef int (*SYNCSEND_PTR)    (void *domainId, void *msg, const struct timespec *timeout,
                                int *response);

/** Typedef for a domain's subscribe function */  
typedef int (*SUBSCRIBE_PTR)   (void *domainId, const char *subject, const char *type,
                                cMsgCallbackFunc *callback, void *userArg,
                                cMsgSubscribeConfig *config, void **handle);

/** Typedef for a domain's unsubscribe function */  
typedef int (*UNSUBSCRIBE_PTR) (void *domainId, void *handle);
  
/** Typedef for a domain's subscription-related function */
typedef int (*SUBSCRIPTION_PTR) (void *domainId, void *handle, int *val);
  
/** Typedef for a domain's subscribeAndGet function */
typedef int (*SUBSCRIBE_AND_GET_PTR) (void *domainId, const char *subject, const char *type,
                                      const struct timespec *timeout, void **replyMsg);

/** Typedef for a domain's sendAndGet function */  
typedef int (*SEND_AND_GET_PTR)         (void *domainId, void *sendMsg,
                                         const struct timespec *timeout, void **replyMsg);

/** Typedef for a domain's monitor function */  
typedef int (*MONITOR_PTR)              (void *domainId, const char *command, void **replyMsg);

/** Typedef for a domain's flush function */  
typedef int (*FLUSH_PTR)                (void *domainId, const struct timespec *timeout);

/** Typedef for a domain's start & stop functions */  
typedef int (*START_STOP_PTR)           (void *domainId);

/** Typedef for a domain's disconnect function */
typedef int (*DISCONNECT_PTR)           (void **domainId);

/** Typedef for a domain's shutdownClients and shutdownServers functions */  
typedef int (*SHUTDOWN_PTR)             (void *domainId, const char *client, int flag);

/** Typedef for a domain's shutdownClients and shutdownServers functions */  
typedef int (*SET_SHUTDOWN_HANDLER_PTR) (void *domainId, cMsgShutdownHandler *handler,
                                         void *userArg);
                                         
/** Typedef for a domain's isConnected function */
typedef int (*ISCONNECTED_PTR)          (void *domainId, int *connected);

/** Typedef for a domain's setUDL function */
typedef int (*SETUDL_PTR)               (void *domainId, const char *udl, const char *udlRemainder);

/** Typedef for a domain's getCurrentUDL function */
typedef int (*GETUDL_PTR)               (void *domainId, const char **udl);

/** Typedef for a domain's getString function */
typedef int (*GETINFO_PTR)              (void *domainId, const char *command, char **string);




/** This structure holds domain implementation function pointers. */
typedef struct domainFunctions_t {

  /** This function connects to a cMsg server. */
  CONNECT_PTR connect; 
  
  /** This function enables reconnection to a cMsg server. */
  START_STOP_PTR reconnect;
  
  /** This function sends a message to a cMsg server. */
  SEND_PTR send;
  
  /** This function sends a message to a cMsg server and receives a synchronous response. */
  SYNCSEND_PTR syncSend;
  
  /** This function sends any pending (queued up) communication with the server. */
  FLUSH_PTR flush;
  
  /** This function subscribes to messages of the given subject and type. */
  SUBSCRIBE_PTR subscribe;
  
  /** This function unsubscribes to messages of the given subject, type and callback. */
  UNSUBSCRIBE_PTR unsubscribe;
  
  /** This function pauses the delivery of messages to a given subscription. */
  UNSUBSCRIBE_PTR subscriptionPause;
  
  /** This function resumes the delivery of messages to a paused subscription. */
  UNSUBSCRIBE_PTR subscriptionResume;
  
  /** This function clears a subscription of all queued messages. */
  UNSUBSCRIBE_PTR subscriptionQueueClear;
  
  /** This function returns the total number of messages sent to a subscription. */
  SUBSCRIPTION_PTR subscriptionMessagesTotal;
  
  /** This function returns the number of messages currently in a subscription's queue. */
  SUBSCRIPTION_PTR subscriptionQueueCount;
  
  /** This function returns true(1) if a subscription's queue is full, else false(0). */
  SUBSCRIPTION_PTR subscriptionQueueIsFull;
  
  /**
   * This function gets one message from a one-time subscription to the given
   * subject and type.
   */
  SUBSCRIBE_AND_GET_PTR subscribeAndGet;
  
  /**
   * This function gets one message from another cMsg client by sending out
   * an initial message to that responder.
   */
  SEND_AND_GET_PTR sendAndGet;
  
  /**
   * This function synchronously gets one message with domain specific monitoring
   * data.
   */
  MONITOR_PTR monitor;
  
  /** This function enables the receiving of messages and delivery to callbacks. */
  START_STOP_PTR start;
  
  /** This function disables the receiving of messages and delivery to callbacks. */
  START_STOP_PTR stop;
  
  /** This function disconnects the client from its cMsg server. */
  DISCONNECT_PTR disconnect;
  
  /** This function shuts down the given clients. */
  SHUTDOWN_PTR shutdownClients;
  
  /** This function shuts down the given servers. */
  SHUTDOWN_PTR shutdownServers;
  
  /** This function sets the shutdown handler. */
  SET_SHUTDOWN_HANDLER_PTR setShutdownHandler;

  /** This function tells whether a client is connected or not. */
  ISCONNECTED_PTR isConnected;
  
  /** This function resets the UDL. */
  SETUDL_PTR setUDL;
  
  /** This function gets the UDL currently used. */
  GETUDL_PTR getCurrentUDL;
  
  /** This function gets the server host currently used. */
  GETUDL_PTR getServerHost;
  
  /** This function gets the server port currently used. */
  ISCONNECTED_PTR getServerPort;
  
  /** This function gets a string given a string (general I/O function). */
  GETINFO_PTR getInfo;
  
} domainFunctions;


/** This structure holds function pointers by domain type. */
typedef struct domainTypeInfo_t {
  const char *type;                 /**< Type of the domain. */
  domainFunctions *functions; /**< Pointer to structure of domain implementation functions. */
} domainTypeInfo;


/** This structure contains information about a domain connection. */
typedef struct cMsgDomain_t {
  void *implId;         /**< Pointer set by implementation to identify particular domain connection. */

  /* other state variables */
  int disconnectCalled; /**< Has disconnect been called? 0 = No, 1 = Yes. */
  int functionsRunning; /**< How many functions (needing access to this mem) currently running? */
  int receiveState;     /**< Is connection receiving callback messages? 0 = No, 1 = Yes. */

  char *type;           /**< Domain type (eg cMsg, CA, SmartSockets, File, etc). */
  char *name;           /**< Name of user. */
  char *udl;            /**< UDL of cMsg name server. */
  char *description;    /**< User description. */
  char *UDLremainder;   /**< UDL with initial "cMsg:domain://" stripped off. */
    
  /** Pointer to a structure containing pointers to domain implementation functions. */
  domainFunctions *functions;
  
} cMsgDomain;


/**
 * This structure contains information about a message's context
 * including the callback context and the sending context.
 */
typedef struct cMsgMessageContext_t {
  /* callback context stuff */
  char *domain;   /**< Subscription's domain (eg cMsg, CA, rc, File, etc). */
  char *subject;  /**< Subscription's subject. */
  char *type;     /**< Subscription's type. */
  char *udl;      /**< UDL of connection. */
  int  *cueSize;  /**< Callback's cue size. */
} cMsgMessageContext;


/** 
 * This structure holds a single string, its length,
 * and a pointer to the next struct. Used in compound payloads.
 */
typedef struct singleStringItem_t {
    int    length;                   /**< Length in bytes of ASCII string contained (not including null terminator). */
    char  *string;                   /**< ASCII string. */
    struct singleStringItem_t *next; /**< Pointer to next item in linked list. */
} singleStringItem;


/** 
 * This structure is used to form a compound payload.
 * It is an item that represents a number, a string, binary data
 * a cMsg message, or an array of any of those.
 * It is designed to allowing the marshalling of its string(s) and/or number(s)
 * to a simple format text string. This format allows easy unmarshalling of the
 * text back into its constituent parts.
 */
typedef struct payloadItem_t {
    int    type;     /**< Type of item (number, bin, string, msg) stored in this item. */
    int    count;    /**< Number of items in array if array, else 1. */
    int    length;   /**< Length of string representation in chars. */
    int    noHeaderLen; /**< Length of text in chars without header (first) line. */
    int    endian;   /**< Endian value (CMSG_ENDIAN_BIG/LITTLE) if item is binary. */
    int   *endians;  /**< Endian values (CMSG_ENDIAN_BIG/LITTLE) if item is binary array. */
    int    size;     /**< Size (in bytes) value if item is binary. */
    int   *sizes;    /**< Size (in bytes) values if item is binary array. */

    char  *text;     /**< String representation for this item, containing name,
                      * - type, count, length, values, etc for wire protocol. */
    char  *name;     /**< Name of this item. */
    void  *next;     /**< Next item in linked list. */

    /* Pointer for general storage */
    void  *pointer;  /**< General use pointer (used for C++ vector). */
    /* Pointer to array payload item */
    void  *array;    /**< Array of any item, single string, message, or binary data. */
    /* Value of single numeric payload item */
    int64_t  val;    /**< An integer value of any type stored here since 64 bits in length. */
    double  dval;    /**< A double or float stored here since 64 bits in length. */
} payloadItem;


/** This structure holds a message. */
struct cMsg_t {
  /* general quantities */
  int     version;       /**< Major version of cMsg. */
  int     sysMsgId;      /**< Unique id set by system to track sendAndGet's. */
  int     info;          /**< Stores information in bit form (true = 1).
                          * - is message a sendAndGet request? 1st bit
                          * - is message a response to a sendAndGet request? 2nd bit
                          * - is response message NULL instead of a message? 3rd bit
                          * - is byte array data big endian? 4th bit
                          * - has message been sent over the wire? 5th bit
                          * - message has compound payload? 6th bit
                          * - payload is expanded (from only text into structures)? 7th bit
                          * - local server has no subscriber for send part of sendAndGet? 8th bit
                          */
  int     reserved;      /**< Reserved for future use. */
  int     bits;          /**< Stores info in bit form about internal state (true = 1).
                          * - is byte array copied in? 1st bit
                          */
  int     historyLengthMax; /**< The maximum number of entries a message keeps (in payload)
                             *   when recording the history of various parameters. */
  int     payloadCount;  /**< Number of items in compound payload. */
  char   *payloadText;   /**< Entire compound payload in text form. */
  payloadItem  *payload; /**< First compound payload item. */
  char   *domain;        /**< Domain message is generated in. */
  
  /* user-settable quantities */
  char   *subject;             /**< Subject of message. */
  char   *type;                /**< Type of message. */
  char   *text;                /**< Text of message. */
  char   *byteArray;           /**< Array of bytes. */
  int     byteArrayLength;     /**< Length (bytes) of byte array data of interest. */
  int     byteArrayOffset;     /**< Index into byte array to data of interest. */
  int     userInt;             /**< User-defined integer. */
  struct timespec userTime;    /**< User-defined time. */

  /* byte array unsettable quantity */
  int     byteArrayLengthFull; /**< Length (bytes) of complete byte array
                                *   (not just region of interest). */

  /* sender quantities */
  char   *sender;              /**< Last sender of message. */
  char   *senderHost;          /**< Host of sender. */
  struct timespec senderTime;  /**< Time message was sent (sec since 12am, GMT, Jan 1st, 1970). */
  int     senderToken;         /**< Unique id generated by system to track sendAndGet's. */
  
  char   *receiver;            /**< Receiver of message. */
  char   *receiverHost;        /**< Host of receiver. */
  struct timespec receiverTime;/**< Time message was received (sec since 12am, GMT, Jan 1st, 1970). */
  int     receiverSubscribeId; /**< Unique id used by system in subscribes and subscribeAndGets. */  

  /* context */
  int udpSend;                /**< if true (non-zero) use UDP to send, else use TCP */
  cMsgMessageContext context; /**< struct with info about context of this message. */
  
  struct cMsg_t *next; /**< For using messages in a linked list. */
};

/** Typedef for cMsg_t structure. Split statement for the sake of doxygen. */
typedef struct cMsg_t cMsgMessage_t;

/** Commands/Requests sent from client to server. */
enum requestMsgId {
  CMSG_SERVER_CONNECT     = 0,       /**< Connect client to name server. */
  CMSG_SERVER_DISCONNECT,            /**< Disconnect client from name server. */
  CMSG_KEEP_ALIVE,                   /**< Tell me if you are alive. */
  CMSG_SHUTDOWN_CLIENTS,             /**< Shutdown clients. */
  CMSG_SHUTDOWN_SERVERS,             /**< Shutdown servers. */
  CMSG_SEND_REQUEST,                 /**< Send request. */
  CMSG_SYNC_SEND_REQUEST,            /**< SyncSend request. */
  CMSG_SUBSCRIBE_REQUEST,            /**< Subscribe request. */
  CMSG_UNSUBSCRIBE_REQUEST,          /**< Unsubscribe request. */
  CMSG_SUBSCRIBE_AND_GET_REQUEST,    /**< SubscribeAndGet request. */
  CMSG_UNSUBSCRIBE_AND_GET_REQUEST,  /**< UnSubscribeAndGet request. */
  CMSG_SEND_AND_GET_REQUEST,         /**< SendAndGet request. */
  CMSG_UN_SEND_AND_GET_REQUEST,      /**< UnSendAndGet request. */
  CMSG_MONITOR_REQUEST               /**< Monitor request. */
};


/** Responses sent to client from server. */
enum responseMsgId {
  CMSG_GET_RESPONSE         = 20, /**< SendAndGet response. */
  CMSG_SUBSCRIBE_RESPONSE,        /**< Subscribe response. */
  CMSG_SERVER_GET_RESPONSE,       /**< Respose to server's "sendAndGet" request. */
  CMSG_RC_CONNECT,                /**< Respond to RC server's "connect" request. */
  CMSG_RC_CONNECT_ABORT,          /**< Response by RC Broadcast server to abort an RC client's "connect" request. */
  CMSG_SYNC_SEND_RESPONSE         /**< SyncSend response. */
};


/** This structure contains parameters used to control subscription callback behavior. */
typedef struct subscribeConfig_t {
  int    init;          /**< If structure was initialized, init = 1. */
  int    maySkip;       /**< May skip messages if too many are piling up in cue (if = 1). */
  int    mustSerialize; /**< Messages must be processed in order received (if = 1),
                             else messages may be processed by parallel threads. */
  int    maxCueSize;    /**< Maximum number of messages to cue for callback. */
  int    skipSize;      /**< Maximum number of messages to skip over (delete) from the 
                             cue for a callback when the cue size has reached it limit
                             (if maySkip = 1) . */
  int    maxThreads;    /**< Maximum number of worker threads to use for running
                             the callback if mustSerialize is 0 (off). */
  int    msgsPerThread; /**< Enough worker threads are started so that there are
                             at most this many unprocessed messages for each thread. */
  size_t stackSize;     /**< Stack size in bytes of subscription thread. By default 
                             this is left unspecified (0). */
} subscribeConfig;

/* private prototype used in multiple files */
         int  cMsgPayloadSetFieldPointer(const void *vmsg, const char *name, void *p);
         int  cMsgPayloadGetFieldPointer(const void *vmsg, const char *name, void **p);
unsigned int  cMsg_b64_encode        (const char *src, unsigned int len, char *dst, int lineBreaks);
         int  cMsg_b64_decode        (const char *src, unsigned int len, char *dst);
unsigned int  cMsg_b64_encode_len    (const char *src, unsigned int srclen, int lineBreaks);
unsigned int  cMsg_b64_decode_len    (const char *src, unsigned int srclen);
unsigned int  cMsg_b64_encode_len_est(const char *src, unsigned int srclen);
unsigned int  cMsg_b64_decode_len_est(const char *src, unsigned int srclen);
         int  cMsgNumDigits          (int64_t number, int isUint64);
         void cMsgPayloadReset_r     (void *vmsg);
         int  cMsgFreeMessage_r      (void **vmsg);


#ifdef	__cplusplus
}
#endif

#endif

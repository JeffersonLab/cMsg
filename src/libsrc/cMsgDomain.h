/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Author:  Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *      Header for cMsg routines
 *
 *----------------------------------------------------------------------------*/
 
/**
 * @file
 * This is the header file for the cMsg domain implementation of cMsg.
 */
 
#ifndef __cMsgDomain_h
#define __cMsgDomain_h

#ifndef VXWORKS
#include <inttypes.h>
#endif
#include <signal.h>

#include "cMsgPrivate.h"
#include "rwlock.h"
#include "cMsgRegex.h"
#include "hash.h"

#ifdef	__cplusplus
extern "C" {
#endif


/** Number of array elements in connectPointers array. */
#define CMSG_CONNECT_PTRS_ARRAY_SIZE 200


/**
 * This structure is used to synchronize threads waiting to failover (are
 * calling send or subscribe or something) and the thread which detects
 * the need to failover (keepalive thread).
 */
typedef struct countDownLatch_t {
  int count;   /**< Number of calls to "countDown" before releasing callers of "await". */
  int waiters; /**< Number of current waiters (callers of "await"). */
  pthread_mutex_t mutex;  /**< Mutex used to change count. */
  pthread_cond_t  countCond;   /**< Condition variable used for callers of "await" to wait. */
  pthread_cond_t  notifyCond;  /**< Condition variable used for caller of "countDown" to wait. */
} countDownLatch;

/**
 * This structure is used to store monitoring data for a single connection to server.
 */
typedef struct monitorData_t {
  int subAndGets;           /**< Number of subscribeAndGets currently active. */
  int sendAndGets;          /**< Number of sendAndGets currently active. */
  int syncSends;            /**< Number of syncSends currently active. */
  uint64_t numTcpSends;     /**< Number of tcp sends done. */
  uint64_t numUdpSends;     /**< Number of udp sends done. */
  uint64_t numSyncSends;    /**< Number of syncSends done. */
  uint64_t numSubAndGets;   /**< Number of subscribeAndGets done. */
  uint64_t numSendAndGets;  /**< Number of sendAndGets done. */
  uint64_t numSubscribes;   /**< Number of subscribes done. */
  uint64_t numUnsubscribes; /**< Number of unsubscribes done. */
} monitorData;


/** This structure represents a single subscription's callback. */
typedef struct subscribeCbInfo_t {
  int               fullQ;    /**< Boolean telling if this callback msg queue is full. */
  int               messages; /**< Number of messages in list. */
  int               threads;  /**< Number of worker threads to run callback if
                               *   config allows parallelizing (mustSerialize = 0). */
  int               started;  /**< Boolean telling if thread started or not. */
  int               pause;    /**< Boolean telling thread to pause. */
  int               quit;     /**< Boolean telling thread to end. */
  uint64_t          msgCount; /**< Number of messages passed to callback. */
  void             *cbarg;    /**< Pointer to mem that must be freed on disconnect if unsubscribe not done. */
  void             *userArg;  /**< User argument to be passed to the callback. */
  cMsgCallbackFunc *callback; /**< Callback function (or C++ callback class instance) to be called. */
  cMsgMessage_t    *head;     /**< Head of linked list of messages given to callback. */
  cMsgMessage_t    *tail;     /**< Tail of linked list of messages given to callback. */
  subscribeConfig   config;   /**< Subscription configuration info. */
  pthread_t         thread;   /**< Thread running callback. */
  pthread_cond_t    addToQ;   /**< Condition variable for adding msg to callback queue. */
  pthread_cond_t    checkQ;   /**< Condition variable for callback thread to add worker threads or quit as needed. */
  pthread_cond_t    takeFromQ;/**< Condition variable for removing msg from callback queue. */
  pthread_mutex_t   mutex;    /**< Mutex callback thread is waiting on. */
  countDownLatch    pauseLatch;  /**< Latch used to pause the callback. */
  struct subscribeCbInfo_t *next; /**< Pointer allows struct to be part of linked list. */
} subscribeCbInfo;


/**
 * This structure represents the results of a parsed subject or type from a subscription
 * with a pseudo wildcard number range(s) specified (ie. format like {i>5&10>i|i=66}).
 * This structure can be made into a linked list of linked lists of numberRanges.
 */
typedef struct numberRange_t {
  int    numbers[4];              /**< Array containing number, operator, number, and conjunction. */
  struct numberRange_t *next;     /**< Next member in linked list of numberRange structs. */
  struct numberRange_t *nextHead; /**< Pointer to head of next linked list of numberRange structs. */
} numberRange;


/**
 * This structure represents a subscription of a certain subject and type.
 */
typedef struct subscribeInfo_t {
  int  id;                 /**< Unique id # corresponding to a unique subject/type pair. */
  int  numCallbacks;       /**< Current number of active callbacks. */
  int  subWildCardCount;   /**< Number of pseudo wildcards in the subject */
  int  typeWildCardCount;  /**< Number of pseudo wildcards in the type */
  int  subRangeCount;      /**< Number of pseudo wildcard number ranges in the subject */
  int  typeRangeCount;     /**< Number of pseudo wildcard number ranges in the type */
  char *subject;           /**< Subject of subscription. */
  char *type;              /**< Type of subscription. */
  char *subjectRegexp;     /**< Subject of subscription made into regular expression. */
  char *typeRegexp;        /**< Type of subscription made into regular expression. */
  numberRange *subRange;   /**< Linked list of linked lists containing results of parsed
                                subject from subscription with pseudo wildcard number ranges. */
  numberRange *typeRange;  /**< Linked list of linked lists containing results of parsed
                                type from subscription with pseudo wildcard number ranges. */
  regex_t compSubRegexp;   /**< Subject of subscription made into compiled regular expression. */
  regex_t compTypeRegexp;  /**< Type of subscription made into compiled regular expression. */
  hashTable subjectTable;  /**< Hash table of strings matching subject. */
  hashTable typeTable;     /**< Hash table of strings matching type. */
  subscribeCbInfo *callbacks; /**< Linked list of callbacks. */
} subInfo;


/** This structure represents a sendAndGet, subscribeAndGet, or syncSend. */
typedef struct getInfo_t {
  int  id;       /**< Unique id # corresponding to a unique subject/type pair. */
  int  response; /**< SyncSend reponse value. */
  int  error;    /**< Error code when client woken up with error condition. */
  int  msgIn;    /**< Boolean telling if a message has arrived. (1-y, 0-n) */
  int  quit;     /**< Boolean commanding sendAndGet to end. */
  char *subject; /**< Subject of sendAndGet. */
  char *type;    /**< Type of sendAndGet. */
  cMsgMessage_t *msg;    /**< Message to be passed to the caller. */
  pthread_cond_t  cond;  /**< Condition variable sendAndGet thread is waiting on. */
  pthread_mutex_t mutex; /**< Mutex sendAndGet thread is waiting on. */
} getInfo;


/**
 * This structure contains the components of a given UDL broken down
 * into its constituent parts.
 */
typedef struct parsedUDL_t {
  int   nameServerPort;    /**< TCP port of name server. */
  int   domainServerPort;  /**< TCP port of domain server. */
  int   nameServerUdpPort; /**< UDP port of name server. */
  int   mustMulticast;     /**< 1 if UDL specifies multicasting to find server, else 0. */
  int   timeout;           /**< time in seconds to wait for a multicast response. */
  int   regime;            /**< CMSG_REGIME_LOW if low data rate regime, similarly can be
                                can be CMSG_REGIME_MEDIUM, or CMSG_REGIME_HIGH. */
  int   failover;          /**< Failover to any server = CMSG_FAILOVER_ANY,
                                to cloud server first and any server after = CMSG_FAILOVER_CLOUD,
                                to cloud server only = CMSG_FAILOVER_CLOUD_ONLY. */
  int   cloud;             /**< Failover to any cloud server = CMSG_CLOUD_ANY,
                                to local cloud server first = CMSG_CLOUD_LOCAL. */
  int   isLocal;           /**< Is the server we're connected to local? (1-y, 0-n). */
  char *udl;               /**< whole UDL for name server */
  char *udlRemainder;      /**< domain specific part of the UDL. */
  char *subdomain;         /**< subdomain name. */
  char *subRemainder;      /**< subdomain specific part of the UDL. */
  char *password;          /**< password of name server. */
  char *nameServerHost;    /**< host of name server. */
  char *serverName;        /**< name of server (nameServerHost:nameServerPort). */
  char *subnet;            /**< name of preferred subnet over which to connect to server. */
} parsedUDL;


/**
 * This structure contains all information concerning a single client
 * connection to this domain.
 */
typedef struct cMsgDomainInfo_t {  
  
  int receiveState;     /**< Boolean telling if messages are being delivered to
                             callbacks (1) or if they are being ignored (0). */
  int gotConnection;    /**< Boolean telling if connection to cMsg server is good. */
  int disconnectCalled; /**< Boolean telling if user called disconnect function. */
  int functionsRunning; /**< How many functions using this struct are currently running? */
  int killKAthread;     /**< Boolean telling keep alive thread to die. */

  int sendSocket;       /**< File descriptor for TCP socket to send/receive messages/requests on. */
  int sendUdpSocket;    /**< File descriptor for UDP socket to send messages on. */
  int keepAliveSocket;  /**< File descriptor for socket to tell if server is still alive or not. */
  int receiveSocket;    /**< File descriptor for TCP socket to receive request responses on (rcDomain). */
  int listenSocket;     /**< File descriptor for socket this program listens on for TCP connections (rc Domain). */

  int sendPort;         /**< Port to send messages to. */
  int sendUdpPort;      /**< Port to send messages to with UDP protocol. */
  int listenPort;       /**< Port this program listens on for this domain's TCP connections (rcDomain). */
  int localPort;        /**< Local (client side) port of the sendSocket. */

  /* subdomain handler attributes */
  int hasSend;            /**< Does this subdomain implement a send function? (1-y, 0-n) */
  int hasSyncSend;        /**< Does this subdomain implement a syncSend function? (1-y, 0-n) */
  int hasSubscribeAndGet; /**< Does this subdomain implement a subscribeAndGet function? (1-y, 0-n) */
  int hasSendAndGet;      /**< Does this subdomain implement a sendAndGet function? (1-y, 0-n) */
  int hasSubscribe;       /**< Does this subdomain implement a subscribe function? (1-y, 0-n) */
  int hasUnsubscribe;     /**< Does this subdomain implement a unsubscribe function? (1-y, 0-n) */
  int hasShutdown;        /**< Does this subdomain implement a shutdowm function? (1-y, 0-n) */

  char *myHost;       /**< This hostname. */
  char *sendHost;     /**< Host to send messages to. */
  char *serverHost;   /**< Host cMsg name server lives on. */

  char *name;         /**< Name of this user. */
  char *udl;          /**< semicolon separated list of UDLs of cMsg name servers. */
  char *description;  /**< User description. */
  char *password;     /**< User password. */
  char *expid;        /**< RC domain experiment id. */

  parsedUDL currentUDL;      /**< Store info about current connection to server. */

  /* failover stuff */
  /** Array of parsedUDL structures for failover purposes obtained from parsing udl. */
  parsedUDL *failovers;
  int failoverSize;          /**< Size of the failover array. */
  int failoverIndex;         /**< Index into the failover array for the UDL currently being used. */
  int implementFailovers;    /**< Boolean telling if failovers are being used. */
  int haveLocalCloudServer;  /**< Boolean telling if any cloud failover server is local. */
  int resubscribeComplete;   /**< Boolean telling if resubscribe is complete in failover process. */
  int killClientThread;      /**< Boolean telling if client thread receiving messages should be killed. */
  countDownLatch syncLatch;  /**< Latch used to synchronize the failover. */
  
  char *msgBuffer;           /**< Buffer used in socket communication to server. */
  int   msgBufferSize;       /**< Size of buffer (in bytes) used in socket communication to server. */

  pthread_t pendThread;         /**< Msg receiving thread for cmsg domain, listening thread for rc domain. */
  pthread_t keepAliveThread;    /**< Thread reading keep alives (monitor data) from server. */
  pthread_t updateServerThread; /**< Thread sending keep alives (monitor data) to server. */
  pthread_t clientThread;       /**< Thread handling rc server connection to rc client (created by pendThread). */
  

  rwLock_t connectLock;        /**< Read/write lock to prevent connect or disconnect from being
                                    run simultaneously with any other function. */
  rwLock_t subscribeLock;      /**< Read/write lock to ensure thread-safety of (un)subscribes. */
  pthread_mutex_t socketMutex;     /**< Mutex to ensure thread-safety of socket use. */
  pthread_mutex_t subAndGetMutex;  /**< Mutex to ensure thread-safety of subAndGet hash table. */
  pthread_mutex_t sendAndGetMutex; /**< Mutex to ensure thread-safety of sendAndGet hash table. */
  pthread_mutex_t syncSendMutex;   /**< Mutex to ensure thread-safety of syncSend hash table. */

  /*  rc domain stuff  */
  int rcConnectAbort;    /**< Flag used to abort rc client connection to RC Broadcast server. */
  int rcConnectComplete; /**< Has a special TCP message been sent from RC server to
                              indicate that connection is complete? (1-y, 0-n) */
  pthread_mutex_t rcConnectMutex;    /**< Mutex used for rc domain connect. */
  pthread_cond_t  rcConnectCond;     /**< Condition variable used for rc domain connect. */
  /** Hashtable of rcServer ip addresses used by emu client to make TCP connection (no longer used in rc client). */
  hashTable rcIpAddrTable;
  /* ***************** */
  
  /** Size in bytes of cMsg system data in XML form. */
  int  monitorXMLSize;
  /** cMsg system data in XML form from keepalive communications. */
  char *monitorXML;
  /** User-supplied XML fragment to send to server
   *  in client data for keepalive communications. */
  char *userXML;
  
  /** Data from monitoring client connection. */
  monitorData monData;
  
  /** Hashtable of syncSends. */
  hashTable syncSendTable;  
  /** Hashtable of sendAndGets. */
  hashTable sendAndGetTable;  
  /** Hashtable of subscribeAndGets. */
  hashTable subAndGetTable;  
  /** Hashtable of subscriptions. */
  hashTable subscribeTable;
  
  /** Hashtable of cloud servers. */
  hashTable cloudServerTable;
  
  /** Shutdown handler function. */
  cMsgShutdownHandler *shutdownHandler;
  
  /** Shutdown handler user argument. */
  void *shutdownUserArg;
  
  /** Store signal mask for restoration after disconnect. */
  sigset_t originalMask;
  
  /** Boolean telling if original mask is being stored. */
  int maskStored;
 
} cMsgDomainInfo;


/** This structure (pointer) is passed as an argument to a callback thread
 *  and also used to unsubscribe. */
typedef struct cbArg_t {
    intptr_t domainId;     /**< Domain identifier. */
    char *key;              /**< Key into hashtable, value = subscription give by sub. */
    subInfo *sub;           /**< Pointer to subscription info structure. */
    subscribeCbInfo *cb;    /**< Pointer to callback info structure. */
    cMsgDomainInfo *domain; /**< Pointer to element of domain structure array. */
} cbArg;


/**
 * This structure passes relevant info to each thread spawned by 
 * another thread. Not all the structure's elements are used in
 * each circumstance, but all are combined into 1 struct for
 * convenience.
 */
typedef struct cMsgThreadInfo_t {
  int isRunning;  /**< Boolean to indicate client listening thread is running. (1-y, 0-n) */
  int connfd;     /**< Socket connection's file descriptor. */
  int listenFd;   /**< Listening socket file descriptor. */
  int thdstarted; /**< Boolean to indicate client msg receiving thread is running. (1-y, 0-n) */
  int blocking;   /**< Block in accept (CMSG_BLOCKING) or
                      not (CMSG_NONBLOCKING)? */
  void *domainId; /**<  Domain id (index into array). */
  cMsgDomainInfo *domain;  /**< Pointer to element of domain structure array. */
  struct cMsgThreadInfo_t *arg;  /**< Pointer to same structure. */
} cMsgThreadInfo;


/* prototypes */

/* string matching */
int   cMsgSubscriptionSetRegexpStuff(subInfo *sub);
void  cMsgNumberRangeInit(numberRange *range);
void  cMsgNumberRangeFree(numberRange *r);
int   cMsgStringMatches(char *regexp, const char *s);
char *cMsgStringToRegexp(const char *s, int *wildCardCount);
int   cMsgSubAndGetMatches(getInfo *info, char *msgSubject, char *msgType);
int   cMsgSubscriptionMatches(subInfo *sub, char *msgSubject, char *msgType);


/* mutexes and read/write locks */
void  cMsgMutexLock(pthread_mutex_t *mutex);
void  cMsgMutexUnlock(pthread_mutex_t *mutex);

void  cMsgConnectReadLock(cMsgDomainInfo *domain);
void  cMsgConnectReadUnlock(cMsgDomainInfo *domain);
void  cMsgConnectWriteLock(cMsgDomainInfo *domain);
void  cMsgConnectWriteUnlock(cMsgDomainInfo *domain);

void  cMsgSocketMutexLock(cMsgDomainInfo *domain);
void  cMsgSocketMutexUnlock(cMsgDomainInfo *domain);

void  cMsgSubAndGetMutexLock(cMsgDomainInfo *domain);
void  cMsgSubAndGetMutexUnlock(cMsgDomainInfo *domain);

void  cMsgSendAndGetMutexLock(cMsgDomainInfo *domain);
void  cMsgSendAndGetMutexUnlock(cMsgDomainInfo *domain);

void  cMsgSyncSendMutexLock(cMsgDomainInfo *domain);
void  cMsgSyncSendMutexUnlock(cMsgDomainInfo *domain);

void  cMsgSubscribeReadLock(cMsgDomainInfo *domain);
void  cMsgSubscribeReadUnlock(cMsgDomainInfo *domain);
void  cMsgSubscribeWriteLock(cMsgDomainInfo *domain);
void  cMsgSubscribeWriteUnlock(cMsgDomainInfo *domain);

void  cMsgCountDownLatchFree(countDownLatch *latch);
void  cMsgCountDownLatchInit(countDownLatch *latch, int count);

void  cMsgLatchReset(countDownLatch *latch, int count, const struct timespec *timeout);
int   cMsgLatchCountDown(countDownLatch *latch, const struct timespec *timeout);
int   cMsgLatchAwait(countDownLatch *latch, const struct timespec *timeout);

void  cMsgMemoryMutexLock(void);
void  cMsgMemoryMutexUnlock(void);

/* threads */
void *cMsgClientListeningThread(void *arg);
void *cMsgCallbackThread(void *arg);
void *cMsgSupplementalThread(void *arg);
void *cMsgCallbackWorkerThread(void *arg);

/* initialization and freeing */
void  cMsgSubscribeInfoInit(subInfo *info);
void  cMsgSubscribeInfoFree(subInfo *info);
void  cMsgCallbackInfoInit(subscribeCbInfo *info);
void  cMsgCallbackInfoFree(subscribeCbInfo *info);
void  cMsgDomainInit(cMsgDomainInfo *domain);
void  cMsgDomainClear(cMsgDomainInfo *domain);
void  cMsgDomainFree(cMsgDomainInfo *domain);
void  cMsgGetInfoInit(getInfo *info);
void  cMsgGetInfoFree(getInfo *info);
void  cMsgParsedUDLInit(parsedUDL *p);
void  cMsgParsedUDLFree(parsedUDL *p);
int   cMsgParsedUDLCopy(parsedUDL *dest, parsedUDL *src);

  /* misc */
int   cMsgReadMessage(int connfd, char *buffer, cMsgMessage_t *msg);
int   cMsgRunCallbacks(cMsgDomainInfo *domain, void *msg);
int   cMsgCheckString(const char *s);
int   cMsgGetAbsoluteTime(const struct timespec *deltaTime, struct timespec *absTime);
int   sun_setconcurrency(int newLevel);
int   sun_getconcurrency(void);

/* signals */
void  cMsgBlockSignals(cMsgDomainInfo *domain);
void  cMsgRestoreSignals(cMsgDomainInfo *domain);


#ifdef	__cplusplus
}
#endif

#endif

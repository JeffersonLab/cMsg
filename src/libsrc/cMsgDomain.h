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
#include "regex.h"
#include "hash.h"

#ifdef	__cplusplus
extern "C" {
#endif


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
  int               done;     /**< Ending callback, ok to free this struct. */
  int               fullQ;    /**< Boolean telling if this callback msg queue is full. */
  int               messages; /**< Number of messages in list. */
  int               threads;  /**< Number of supplemental threads to run callback if
                               *   config allows parallelizing (mustSerialize = 0). */
  int               quit;     /**< Boolean telling thread to end. */
  uint64_t          msgCount; /**< Number of messages passed to callback. */
  void             *userArg;  /**< User argument to be passed to the callback. */
  cMsgCallbackFunc *callback; /**< Callback function (or C++ callback class instance) to be called. */
  cMsgMessage_t    *head;     /**< Head of linked list of messages given to callback. */
  cMsgMessage_t    *tail;     /**< Tail of linked list of messages given to callback. */
  subscribeConfig   config;   /**< Subscription configuration info. */
  pthread_t         thread;   /**< Thread running callback. */
  pthread_cond_t    cond;     /**< Condition variable callback thread is waiting on. */
  pthread_mutex_t   mutex;    /**< Mutex callback thread is waiting on. */
  struct subscribeCbInfo_t * next; /**< Pointer allows struct to be part of linked list. */
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
 * into its consituent parts.
 */
typedef struct parsedUDL_t {
  int   nameServerPort; /**< port of name server. */
  int   valid;          /**< 1 if valid UDL for the cMsg domain, else 0. */
  int   mustBroadcast;  /**< 1 if UDL specifies broadcasting to find server, else 0. */
  int   timeout;        /**< time in seconds to wait for a broadcast response. */
  int   regimeLow;      /**< 1 if regime is low (low data rate). */
  char *udl;            /**< whole UDL for name server */
  char *udlRemainder;   /**< domain specific part of the UDL. */
  char *subdomain;      /**< subdomain name. */
  char *subRemainder;   /**< subdomain specific part of the UDL. */
  char *password;       /**< password of name server. */
  char *nameServerHost; /**< host of name server. */
} parsedUDL;


/**
 * This structure contains all information concerning a single client
 * connection to this domain.
 */
typedef struct cMsgDomainInfo_t {  
  
  int receiveState;    /**< Boolean telling if messages are being delivered to
                            callbacks (1) or if they are being igmored (0). */
  int gotConnection;   /**< Boolean telling if connection to cMsg server is good. */
  
  int sendSocket;      /**< File descriptor for TCP socket to send/receive messages/requests on. */
  int sendUdpSocket;   /**< File descriptor for UDP socket to send messages on. */
  int keepAliveSocket; /**< File descriptor for socket to tell if server is still alive or not. */
  int receiveSocket;   /**< File descriptor for TCP socket to receive request responses on (rcDomain). */
  int listenSocket;    /**< File descriptor for socket this program listens on for TCP connections (rc Domain). */

  int sendPort;        /**< Port to send messages to. */
  int sendUdpPort;     /**< Port to send messages to with UDP protocol. */
  int listenPort;      /**< Port this program listens on for this domain's TCP connections (rcDomain). */
   
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
  
  /** Array of parsedUDL structures for failover purposes obtained from parsing udl. */  
  parsedUDL *failovers;
  int failoverSize;          /**< Size of the failover array. */
  int failoverIndex;         /**< Index into the failover array for the UDL currently being used. */
  int implementFailovers;    /**< Boolean telling if failovers are being used. */
  int resubscribeComplete;   /**< Boolean telling if resubscribe is complete in failover process. */
  int killClientThread;      /**< Boolean telling if client thread receiving messages should be killed. */
  countDownLatch syncLatch;  /**< Latch used to synchronize the failover. */
  
  char *msgBuffer;           /**< Buffer used in socket communication to server. */
  int   msgBufferSize;       /**< Size of buffer (in bytes) used in socket communication to server. */

  pthread_t pendThread;         /**< Msg receiving thread for cmsg domain, listening thread for rc domain. */
  pthread_t keepAliveThread;    /**< Thread reading keep alives (monitor data) from server. */
  pthread_t updateServerThread; /**< Thread sending keep alives (monitor data) to server. */
  pthread_t clientThread;       /**< Thread handling rc server connection to rc client (created by pendThread). */
  
  /**
   * Read/write lock to prevent connect or disconnect from being
   * run simultaneously with any other function.
   */
  rwLock_t connectLock;
  pthread_mutex_t socketMutex;    /**< Mutex to ensure thread-safety of socket use. */
  pthread_mutex_t subscribeMutex; /**< Mutex to ensure thread-safety of (un)subscribes. */
  pthread_cond_t  subscribeCond;  /**< Condition variable used for waiting on clogged callback cue. */
  
  pthread_mutex_t subAndGetMutex; /**< Mutex to ensure thread-safety of subAndGet hash table. */
  pthread_mutex_t sendAndGetMutex; /**< Mutex to ensure thread-safety of sendAndGet hash table. */
  pthread_mutex_t syncSendMutex;   /**< Mutex to ensure thread-safety of syncSend hash table. */

  /*  rc domain stuff  */
  int rcConnectAbort;    /**< Flag used to abort rc client connection to RC Broadcast server. */
  int rcConnectComplete; /**< Has a special TCP message been sent from RC server to
                              indicate that connection is conplete? (1-y, 0-n) */
  pthread_mutex_t rcConnectMutex;    /**< Mutex used for rc domain connect. */
  pthread_cond_t  rcConnectCond;     /**< Condition variable used for rc domain connect. */
  /* ***************** */
  
  /** Size in bytes of cMsg system data in XML form. */
  int  monitorXMLSize;
  /** cMsg system data in XML form from keepalive communications. */
  char *monitorXML;
  
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
  uintptr_t domainId;     /**< Domain identifier. */
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
void  cMsgSubscribeMutexLock(cMsgDomainInfo *domain);
void  cMsgSubscribeMutexUnlock(cMsgDomainInfo *domain);
void  cMsgCountDownLatchFree(countDownLatch *latch);
void  cMsgCountDownLatchInit(countDownLatch *latch, int count);
void  cMsgLatchReset(countDownLatch *latch, int count, const struct timespec *timeout);
int   cMsgLatchCountDown(countDownLatch *latch, const struct timespec *timeout);
int   cMsgLatchAwait(countDownLatch *latch, const struct timespec *timeout);

/* threads */
void *cMsgClientListeningThread(void *arg);
void *cMsgCallbackThread(void *arg);
void *cMsgSupplementalThread(void *arg);

/* initialization and freeing */
void  cMsgSubscribeInfoInit(subInfo *info);
void  cMsgSubscribeInfoFree(subInfo *info);
void  cMsgSubscribeInfoFreeNoMutex(subInfo *info);
void  cMsgCallbackInfoInit(subscribeCbInfo *info);
void  cMsgCallbackInfoFree(subscribeCbInfo *info);
void  cMsgDomainInit(cMsgDomainInfo *domain);
void  cMsgDomainClear(cMsgDomainInfo *domain);
void  cMsgDomainFree(cMsgDomainInfo *domain);
void  cMsgGetInfoInit(getInfo *info);
void  cMsgGetInfoFree(getInfo *info);

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

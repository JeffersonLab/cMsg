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
 * This is the header file for the cMsg domain implementation of cMsg.
 */
 
#ifndef __cMsgDomain_h
#define __cMsgDomain_h

#include "cMsgPrivate.h"
#include "rwlock.h"

#ifdef	__cplusplus
extern "C" {
#endif

/** Maximum number of subscriptions per client connection. */
#define MAX_SUBSCRIBE 100
/** Maximum number of simultaneous subscribeAndGets per client connection. */
#define MAX_SUBSCRIBE_AND_GET 20
/** Maximum number of simultaneous sendAndGets per client connection. */
#define MAX_SEND_AND_GET 20
/** Maximum number of callbacks per subscription. */
#define MAX_CALLBACK 20


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


/** This structure represents a single subscription's callback. */
typedef struct subscribeCbInfo_t {
  int             active;   /**< Boolean telling if this callback is active. */
  cMsgCallback   *callback; /**< Callback function (or C++ callback class instance) to be called. */
  void           *userArg;  /**< User argument to be passed to the callback. */
  cMsgMessage    *head;     /**< Head of linked list of messages given to callback. */
  cMsgMessage    *tail;     /**< Tail of linked list of messages given to callback. */
  int             messages; /**< Number of messages in list. */
  int             threads;  /**< Number of supplemental threads to run callback if
                             *   config allows parallelizing (mustSerialize = 0). */
  subscribeConfig config;   /**< Subscription configuration info. */
  char            quit;     /**< Boolean telling thread to end. */
  pthread_t       thread;   /**< Thread running callback. */
  pthread_cond_t  cond;     /**< Condition variable callback thread is waiting on. */
  pthread_mutex_t mutex;    /**< Mutex callback thread is waiting on. */
} subscribeCbInfo;


/**
 * This structure represents a subscription of a certain subject and type.
 */
typedef struct subscribeInfo_t {
  int  id;             /**< Unique id # corresponding to a unique subject/type pair. */
  int  active;         /**< Boolean telling if this subject/type has an active callback. */
  int  numCallbacks;   /**< Current number of active callbacks. */
  char *subject;       /**< Subject of subscription. */
  char *type;          /**< Type of subscription. */
  char *subjectRegexp; /**< Subject of subscription made into regular expression. */
  char *typeRegexp;    /**< Type of subscription made into regular expression. */
  struct subscribeCbInfo_t cbInfo[MAX_CALLBACK]; /**< Array of callbacks. */
} subInfo;


/**
 * This structure represents a sendAndGet or subscribeAndGet
 * of a certain subject and type.
 */
typedef struct getInfo_t {
  int  id;       /**< Unique id # corresponding to a unique subject/type pair. */
  int  active;   /**< Boolean telling if this subject/type has an active callback. */
  int  error;    /**< Error code when client woken up with error condition. */
  char msgIn;    /**< Boolean telling if a message has arrived. (1-y, 0-n) */
  char quit;     /**< Boolean commanding sendAndGet to end. */
  char *subject; /**< Subject of sendAndGet. */
  char *type;    /**< Type of sendAndGet. */
  cMsgMessage *msg;      /**< Message to be passed to the caller. */
  pthread_cond_t  cond;  /**< Condition variable sendAndGet thread is waiting on. */
  pthread_mutex_t mutex; /**< Mutex sendAndGet thread is waiting on. */
} getInfo;


/**
 * This structure contains the components of a given UDL broken down
 * into its consituent parts.
 */
typedef struct parsedUDL_t {
  unsigned short nameServerPort; /**< port of name server. */
  int   valid;          /**< 1 if valid UDL for the cMsg domain, else 0. */
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
  
  int id;                     /**< Unique id of connection. */ 
  
  volatile int initComplete;  /**< Boolean telling if imitialization of this structure
                                    is complete and it is being used. 0 = No, 1 = Yes */
  volatile int receiveState;  /**< Boolean telling if messages are being delivered to
                                    callbacks (1) or if they are being igmored (0). */
  volatile int gotConnection; /**< Boolean telling if connection to cMsg server is good. */
  
  int sendSocket;      /**< File descriptor for TCP socket to send messages/requests on. */
  int receiveSocket;   /**< File descriptor for TCP socket to receive request responses on. */
  int listenSocket;    /**< File descriptor for socket this program listens on for TCP connections. */
  int keepAliveSocket; /**< File descriptor for socket to tell if server is still alive or not. */

  unsigned short sendPort;   /**< Port to send messages to. */
  unsigned short serverPort; /**< Port cMsg name server listens on. */
  unsigned short listenPort; /**< Port this program listens on for this domain's TCP connections. */
  
  /* subdomain handler attributes */
  char hasSend;            /**< Does this subdomain implement a send function? (1-y, 0-n) */
  char hasSyncSend;        /**< Does this subdomain implement a syncSend function? (1-y, 0-n) */
  char hasSubscribeAndGet; /**< Does this subdomain implement a subscribeAndGet function? (1-y, 0-n) */
  char hasSendAndGet;      /**< Does this subdomain implement a sendAndGet function? (1-y, 0-n) */
  char hasSubscribe;       /**< Does this subdomain implement a subscribe function? (1-y, 0-n) */
  char hasUnsubscribe;     /**< Does this subdomain implement a unsubscribe function? (1-y, 0-n) */
  char hasShutdown;        /**< Does this subdomain implement a shutdowm function? (1-y, 0-n) */

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
  countDownLatch failoverLatch; /**< Latch used to synchronize the failover. */
  
  char *msgBuffer;           /**< Buffer used in socket communication to server. */
  int   msgBufferSize;       /**< Size of buffer (in bytes) used in socket communication to server. */

  char *msgInBuffer[2];      /**< Buffers used in socket communication from server. */

  pthread_t pendThread;      /**< Listening thread. */
  pthread_t keepAliveThread; /**< Thread sending keep alives to server. */
  pthread_t clientThread[2]; /**< Threads from server connecting to client (created by pendThread). */
  
  /**
   * Read/write lock to prevent connect or disconnect from being
   * run simultaneously with any other function.
   */
  rwLock_t connectLock;
  pthread_mutex_t socketMutex;    /**< Mutex to ensure thread-safety of socket use. */
  pthread_mutex_t syncSendMutex;  /**< Mutex to ensure thread-safety of syncSends. */
  pthread_mutex_t subscribeMutex; /**< Mutex to ensure thread-safety of (un)subscribes. */
  pthread_cond_t  subscribeCond;  /**< Condition variable used for waiting on clogged callback cue. */
    
  /** Array of structures - each of which contain a subscription. */
  subInfo subscribeInfo[MAX_SUBSCRIBE]; 
  /** Array of structures - each of which contain a subscribeAndGet. */
  getInfo subscribeAndGetInfo[MAX_SUBSCRIBE_AND_GET];
  /** Array of structures - each of which contain a sendAndGet. */
  getInfo sendAndGetInfo[MAX_SEND_AND_GET];
  
  /** Shutdown handler function. */
  cMsgShutdownHandler *shutdownHandler;
  
  /** Shutdown handler user argument. */
  void *shutdownUserArg;  
 
} cMsgDomainInfo;


/** This structure (pointer) is passed as an argument to a callback. */
typedef struct cbArg_t {
  int domainId;  /**< Domain identifier. */
  int subIndex;  /**< Index into domain structure's subscription array. */
  int cbIndex;   /**< Index into subscription structure's callback array. */
  cMsgDomainInfo *domain;  /**< Pointer to element of domain structure array. */
} cbArg;


/** This structure is used for passing data from main to network threads. */
typedef struct mainThreadInfo_t {
  int isRunning; /**< Boolean to indicate thread is running. (1-y, 0-n) */
  int domainId;  /**< Domain identifier. */
  int listenFd;  /**< Listening socket file descriptor. */
  int blocking;  /**< Block in accept (CMSG_BLOCKING) or
                      not (CMSG_NONBLOCKING)? */
} mainThreadInfo;

/**
 * This structure passes relevant info to each thread spawned by the 
 * cMsgClientListeningThread or the rcClientListeningThread serving
 * a cMsg connection.
 */
typedef struct cMsgThreadInfo_t {
  int connfd;   /**< Socket connection's file descriptor. */
  int domainId; /**< Index into the cMsgDomains or rcDomains array. */
  int connectionNumber; /**< Number of connection to this listening port (starting at 0). */
} cMsgThreadInfo;

/* prototypes */
int   cMsgRunCallbacks(cMsgDomainInfo *domain, cMsgMessage *msg);
int   cMsgWakeGet(int domainId, cMsgMessage *msg);
int   cMsgReadMessage(int connfd, char *buffer, cMsgMessage *msg, int *acknowledge);

/* string matching */
char *cMsgStringEscape(const char *s);
int   cMsgStringMatches(char *regexp, const char *s);
int   cMsgRegexpMatches(char *regexp, const char *s);

/* mutexes and read/write locks */
void  mutexLock(pthread_mutex_t *mutex);
void  mutexUnlock(pthread_mutex_t *mutex);
void  connectReadLock(cMsgDomainInfo *domain);
void  connectReadUnlock(cMsgDomainInfo *domain);
void  connectWriteLock(cMsgDomainInfo *domain);
void  connectWriteUnlock(cMsgDomainInfo *domain);
void  socketMutexLock(cMsgDomainInfo *domain);
void  socketMutexUnlock(cMsgDomainInfo *domain);
void  syncSendMutexLock(cMsgDomainInfo *domain);
void  syncSendMutexUnlock(cMsgDomainInfo *domain);
void  subscribeMutexLock(cMsgDomainInfo *domain);
void  subscribeMutexUnlock(cMsgDomainInfo *domain);
void  countDownLatchFree(countDownLatch *latch); 
void  countDownLatchInit(countDownLatch *latch, int count, int reInit);

/* threads */
void *callbackThread(void *arg);
void *supplementalThread(void *arg);

/* initialization and freeing */
void  domainInit(cMsgDomainInfo *domain, int reInit);
void  domainFree(cMsgDomainInfo *domain);  
void  domainClear(cMsgDomainInfo *domain);
void  getInfoInit(getInfo *info, int reInit);
void  subscribeInfoInit(subInfo *info, int reInit);
void  getInfoFree(getInfo *info);
void  subscribeInfoFree(subInfo *info);

/* misc */
int   checkString(const char *s);
int   getAbsoluteTime(const struct timespec *deltaTime, struct timespec *absTime);
int   sun_setconcurrency(int newLevel);
int   sun_getconcurrency(void);


#ifdef	__cplusplus
}
#endif

#endif

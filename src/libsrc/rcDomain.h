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
 *      Header for rc domain routines
 *
 *----------------------------------------------------------------------------*/
 
/**
 * @file
 * This is the header file for the rc domain implementation of cMsg.
 */
 
#ifndef __rcDomain_h
#define __rcDomain_h

#include "cMsgPrivate.h"
#include "cMsgDomain.h"

#ifdef	__cplusplus
extern "C" {
#endif


/**
 * This structure contains all information concerning a single client
 * connection to this domain.
 */
typedef struct rcDomain_t {  
  
  int receiveState;    /**< Boolean telling if messages are being delivered to
                            callbacks (1) or if they are being igmored (0). */
  int gotConnection;   /**< Boolean telling if connection to rc server is good. */
  
  int sendSocket;      /**< File descriptor for TCP socket to send messages on. */
  int sendUdpSocket;   /**< File descriptor for UDP socket to send messages on. */
  int receiveSocket;   /**< File descriptor for TCP socket to receive responses on. */
  int listenSocket;    /**< File descriptor for socket this program listens on for TCP connections. */

  unsigned short sendPort;   /**< Port to send messages to. */
  unsigned short serverPort; /**< Port rc server listens on. */
  unsigned short listenPort; /**< Port this program listens on for this domain's TCP connections. */
  
  char *myHost;       /**< This hostname. */
  char *sendHost;     /**< Host to send messages to. */
  char *serverHost;   /**< Host rc server lives on. */

  char *name;         /**< Name of this user. */
  char *udl;          /**< UDL of rc server. */
  char *description;  /**< User description. */
  char *password;     /**< User password. */
  
  int killClientThread;      /**< Boolean telling if client thread receiving messages should be killed. */
  
  char *msgBuffer;           /**< Buffer used in socket communication to server. */
  int   msgBufferSize;       /**< Size of buffer (in bytes) used in socket communication to server. */
  char *msgInBuffer;         /**< Buffers used in socket communication from server. */

  pthread_t pendThread;      /**< Listening thread. */
  pthread_t clientThread;    /**< Thread from server connecting to client (created by pendThread). */

  /**
   * Read/write lock to prevent connect or disconnect from being
   * run simultaneously with any other function.
   */
  rwLock_t connectLock;
  pthread_mutex_t socketMutex;    /**< Mutex to ensure thread-safety of socket use. */
  pthread_mutex_t subscribeMutex; /**< Mutex to ensure thread-safety of (un)subscribes. */
  pthread_cond_t  subscribeCond;  /**< Condition variable used for waiting on clogged callback cue. */    
  pthread_mutex_t sendAndGetMutex; /**< Mutex to ensure thread-safety of sendAndGet hash table. */

  int rcConnectAbort;    /**< Flag used to abort rc client connection to RC Broadcast server. */
  int rcConnectComplete; /**< Has a special TCP message been sent from RC server to
  indicate that connection is conplete? (1-y, 0-n) */
  
  pthread_mutex_t rcConnectMutex;    /**< Mutex used for rc domain connect. */
  pthread_cond_t  rcConnectCond;     /**< Condition variable used for rc domain connect. */
  
  /** Hashtable of sendAndGets. */
  hashTable sendAndGetTable;
  /** Hashtable of subscriptions. */
  hashTable subscribeTable;
  
  /** Shutdown handler function. */
  cMsgShutdownHandler *shutdownHandler;
  
  /** Store signal mask for restoration after disconnect. */
  sigset_t originalMask;
  
  /** Boolean telling if original mask is being stored. */
  int maskStored;
 
  /** Shutdown handler user argument. */
  void *shutdownUserArg;  
 
} rcDomainInfo;


/* prototypes */

#ifdef	__cplusplus
}
#endif

#endif

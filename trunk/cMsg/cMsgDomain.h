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
 
#ifndef __cMsgDomain_h
#define __cMsgDomain_h

#include "cMsgPrivate.h"

#ifdef	__cplusplus
extern "C" {
#endif

/* built-in limits */
#define MAX_SUBSCRIBE 100
#define MAX_GENERAL_GET 10
#define MAX_SPECIFIC_GET 10
#define MAXCALLBACK   20

/* for dispatching callbacks in their own threads */
typedef struct dispatchCbInfo_t {
  cMsgCallback *callback;
  void *userArg;
  cMsgMessage *msg;
} dispatchCbInfo;


/* for a single subscription's callback */
typedef struct subscribeCbInfo_t {
  cMsgCallback   *callback; /* function to be called */
  void           *userArg;  /* user argument given to callback */
  cMsgMessage    *head;     /* head of linked list of messages given to callback */
  cMsgMessage    *tail;     /* tail of linked list of messages given to callback */
  int             messages; /* number of messages in list */
  int             threads;  /* number of supplemental threads to run callback if
                             * config allows parallelizing (mustSerialize = 0) */
  subscribeConfig config;   /* subscription configuration info */
  char            quit;     /* boolean telling thread to end */
  pthread_t       thread;   /* thread running callback */
  pthread_cond_t  cond;     /* condition variable thread is waiting on */
  pthread_mutex_t mutex;    /* mutex thread is waiting on */
} subscribeCbInfo;

/* For both regular subscriptions and 1-shot-subscriptions/general-gets */
/* of a certain subject and type. */
typedef struct subscribeInfo_t {
  int  id;       /* unique id # corresponding to a unique subject/type pair */
  int  active;   /* does this subject/type have active callbacks? */
  char *type;
  char *subject;
  struct subscribeCbInfo_t cbInfo[MAXCALLBACK];
} subscribeInfo;


/* for a "get" of a certain subject and type */
typedef struct getInfo_t {
  int  id;       /* unique id # corresponding to a unique subject/type pair */
  int  active;   /* does this subject/type have an active callback? */
  char msgIn;    /* has message arrived? (1-y, 0-n) */
  char quit;     /* boolean telling "get" to end */
  char *type;
  char *subject;
  cMsgMessage *msg;
  pthread_cond_t  cond;     /* condition variable get thread is waiting on */
  pthread_mutex_t mutex;    /* mutex get thread is waiting on */
} getInfo;

/* structure containing all domain info */
typedef struct cMsgDomain_CODA_t {  
  
  int id;
  
  /* state variables */
  volatile int initComplete;  /* 0 = No, 1 = Yes */
  volatile int receiveState;
  volatile int lostConnection;
  
  int sendSocket;      /* file descriptor for TCP socket to send messages on */
  int listenSocket;    /* file descriptor for socket this program listens on for TCP connections */
  int keepAliveSocket; /* file descriptor for socket to tell if server is still alive or not */

  unsigned short sendPort;   /* port to send messages to */
  unsigned short serverPort; /* port cMsg name server listens on */
  unsigned short listenPort; /* port this program listens on for this domain's TCP connections */
  
  /* subdomain handler attributes */
  char hasSend;            /* subdomain implements meaningful send function */
  char hasSyncSend;        /* subdomain implements meaningful syncSend function */
  char hasSubscribeAndGet; /* subdomain implements meaningful subscribeAndGet function */
  char hasSendAndGet;      /* subdomain implements meaningful sendAndGet function */
  char hasSubscribe;       /* subdomain implements meaningful subscribe function */
  char hasUnsubscribe;     /* subdomain implements meaningful unsubscribe function */

  char *myHost;       /* this hostname */
  char *sendHost;     /* host to send messages to */
  char *serverHost;   /* host cMsg name server lives on */

  char *name;         /* name of user (this program) */
  char *udl;          /* UDL of cMsg name server */
  char *description;  /* user description */
  
  pthread_t pendThread;      /* listening thread */
  pthread_t keepAliveThread; /* thread sending keep alives to server */
  
  pthread_mutex_t socketMutex;    /* mutex to ensure thread-safety of socket use */
  pthread_mutex_t syncSendMutex;  /* mutex to ensure thread-safety of syncSends */
  pthread_mutex_t subscribeMutex; /* mutex to ensure thread-safety of (un)subscribes */
  
  /* array of structures - each of which contain a subscription */
  subscribeInfo subscribeInfo[MAX_SUBSCRIBE]; 
  /* array of structures - each of which contain a 1-shot subscription or general get */
  getInfo generalGetInfo[MAX_GENERAL_GET];
  /* array of structures - each of which contain a specific get */
  getInfo specificGetInfo[MAX_SPECIFIC_GET];
  
} cMsgDomain_CODA;



/* struct for passing data from main to network threads */
typedef struct mainThreadInfo_t {
  int isRunning; /* flag to indicate thread is running */
  int domainId;  /* domain identifier */
  int listenFd;  /* listening socket file descriptor */
  int blocking;  /* block in accept (CMSG_BLOCKING) or
                     not (CMSG_NONBLOCKING)? */
} mainThreadInfo;

/* prototypes */
int cMsgReadMessage(int fd, cMsgMessage *msg);
int cMsgRunCallbacks(int domainId, cMsgMessage *msg);
int cMsgWakeGet(int domainId, cMsgMessage *msg);
int cMsgWakeGetWithNull(int domainId, int senderToken);

#ifdef	__cplusplus
}
#endif

#endif

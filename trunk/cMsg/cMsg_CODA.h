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
 
#ifndef __cMsg_CODA_h
#define __cMsg_CODA_h

#include "cMsg.h"

#ifdef	__cplusplus
extern "C" {
#endif

/* built-in limits */
#define MAXSUBSCRIBE 100
#define MAXCALLBACK   10

/* for dispatching callbacks in their own threads */
typedef struct dispatchCbInfo_t {
  cMsgCallback *callback;
  void *userArg;
  cMsgMessage *msg;
} dispatchCbInfo;


/* for subscribe lists */
struct subscribeCbInfo_t {
  cMsgCallback *callback;
  void *userArg;
};


struct subscribeInfo_t {
  int  id;       /* unique id # corresponding to a unique subject/type pair */
  int  active;   /* does this subject/type have valid callbacks? */
  char *type;
  char *subject;
  struct subscribeCbInfo_t cbInfo[MAXCALLBACK];
};


/* structure containing all domain info */
typedef struct cMsgDomain_CODA_t {  
  int initComplete;  /* 0 = No, 1 = Yes */
  int id;
  
  /* other state variables */
  int receiveState;
  int lostConnection;
  
  int sendSocket;      /* file descriptor for TCP socket to send messages on */
  int listenSocket;    /* file descriptor for socket this program listens on for TCP connections */
  int keepAliveSocket; /* file descriptor for socket to tell if server is still alive or not */

  char *myHost;       /* this hostname */
  char *sendHost;     /* host to send messages to */
  char *serverHost;   /* host cMsg name server lives on */

  char *name;         /* name of user (this program) */
  char *udl;          /* UDL of cMsg name server */
  char *description;  /* user description */
  
  unsigned short sendPort;   /* port to send messages to */
  unsigned short serverPort; /* port cMsg name server listens on */
  unsigned short listenPort; /* port this program listens on for this domain's TCP connections */
  
  pthread_t pendThread;      /* listening thread */
  pthread_t keepAliveThread; /* thread sending keep alives to server */
  
  pthread_mutex_t sendMutex;      /* mutex to ensure thread-safety of send socket */
  pthread_mutex_t subscribeMutex; /* mutex to ensure thread-safety of (un)subscribes */
  
  struct subscribeInfo_t subscribeInfo[MAXSUBSCRIBE];
  
} cMsgDomain_CODA;



/* struct for passing data from main to network threads */
typedef struct mainThreadInfo_t {
  int isRunning; /* flag to indicate thread is running */
  int domainId;  /* domain identifier */
  int listenFd;  /* listening socket file descriptor */
  int blocking;  /* block in accept (CMSG_BLOCKING) or
                     not (CMSG_NONBLOCKING)? */
  cMsgDomain_CODA *domain; /* pointer to domain structure corresponding to domainId */
} mainThreadInfo;

/* prototypes */
int cMsgReadMessage(int fd, cMsgMessage *msg);
int cMsgRunCallbacks(cMsgDomain_CODA *domain, int command, cMsgMessage *msg);

#ifdef	__cplusplus
}
#endif

#endif

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
 
#ifndef __cMsgPrivate_h
#define __cMsgPrivate_h

#include "cMsg.h"

#ifdef	__cplusplus
extern "C" {
#endif


/* version numbers */
#define CMSG_VERSION_MAJOR 1
#define CMSG_VERSION_MINOR 0


/* user's domain id is index into "domains" array, offset by this amount + */
#define MAXDOMAINTYPES    10
#define MAXDOMAINS       100
#define DOMAIN_ID_OFFSET 100


/* set the debug level */
int debug = CMSG_DEBUG_ERROR;


/* structure containing all domain info */
typedef struct cMsgDomain_t {
  /* init state */
  int initComplete; /* 0 = No, 1 = Yes */

  /* other state variables */
  int receiveState;
  int lostConnection;
  
  int sendSocket;            /* file descriptor for TCP socket to send messages on */
  int listenSocket;          /* file descriptor for socket this program listens on for TCP connections */

  unsigned short sendPort;   /* port to send messages to */
  unsigned short serverPort; /* port cMsg name server listens on */
  unsigned short listenPort; /* port this program listens on for this domain's TCP connections */
  
  pthread_t pendThread; /* listening thread */

  char *myHost;      /* this hostname */
  char *sendHost;    /* host to send messages to */
  char *serverHost;  /* host of cMsg name server */
  
  char *type;        /* domain type (coda, JMS, SmartSockets, etc.) */
  char *name;        /* name of user (this program) */
  char *udl;         /* UDL of cMsg name server */
  char *description; /* user description */
  
  pthread_mutex_t sendMutex;      /* mutex to ensure thread-safety of send socket */
  pthread_mutex_t subscribeMutex; /* mutex to ensure thread-safety of (un)subscribes */
  
  struct subscribeInfo_t subscribeInfo[MAXSUBSCRIBE];

  /* pointer to a struct contatining pointers to domain implementation functions */
  domainFunctions *functions;

} cMsgDomain;


/* see "Programming with POSIX threads' by Butenhof */
#define err_abort(code,text) do { \
    fprintf (stderr, "%s at \"%s\":%d: %s\n", \
        text, __FILE__, __LINE__, strerror (code)); \
    exit (-1); \
    } while (0)


/* message structure */
typedef struct cMsg_t {
  int     sysMsgId;         /* set by system */
  
  char   *sender;
  int     senderId;         /* in case fred dies and resurrects */
  char   *senderHost;
  time_t  senderTime;
  int     senderMsgId;      /* set by client system */
  int     senderToken;      /* set by sender user code */
  
  char   *receiver;
  char   *receiverHost;
  time_t  receiverTime;
  int     receiverSubscribeId;
  
  char   *domain;
  char   *subject;
  char   *type;
  char   *text;
} cMsgMessage;



/* holds domain implementation function pointers */
typedef struct domainFunctions_t {
  int (*connect)(cMsgDomain *domain, char *udl, char *name, char *description);
  int (*send)(void *msg);
  int (*flush)(void);
  int (*subscribe)(char *subject, char *type, cMsgCallback *callback, void *userArg);
  int (*unsubscribe)(char *subject, char *type, cMsgCallback *callback);
  int (*get)(void *sendMsg, time_t timeout, void **replyMsg);
  int (*disconnect)(void);
} domainFunctions;


/* holds function pointers by domain type */
typedef struct domainTypeInfo_t {
  char *type;
  domainFunctions *functions;
} domainTypeInfo;



/* system msg id types */
enum msgId {
  CMSG_SERVER_CONNECT     = 0,
  CMSG_SERVER_RESPONSE,
  CMSG_KEEP_ALIVE,
  CMSG_HEARTBEAT,
  CMSG_SHUTDOWN,
  CMSG_GET_REQUEST,
  CMSG_GET_RESPONSE,
  CMSG_SEND_REQUEST,
  CMSG_SEND_RESPONSE,
  CMSG_SUBSCRIBE_REQUEST,
  CMSG_UNSUBSCRIBE_REQUEST,
  CMSG_SUBSCRIBE_RESPONSE
};


/* struct for passing data from main to network threads */
typedef struct mainThreadInfo_t {
  int listenFd;  /* listening socket file descriptor */
  int blocking;  /* block in accept (CMSG_BLOCKING) or
                     not (CMSG_NONBLOCKING)? */
} mainThreadInfo;



/* prototypes (move to cMsg.c eventually) */
void  cMsgDomainClear(cMsgDomain *domain);


#ifdef	__cplusplus
}
#endif

#endif

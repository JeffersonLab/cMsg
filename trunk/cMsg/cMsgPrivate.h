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
extern int cMsgDebug;


/* holds domain implementation function pointers */
typedef struct domainFunctions_t {
  int (*connect)    (char *udl, char *name, char *description, int *domainId); 
  int (*send)       (int domainId, void *msg);
  int (*syncSend)   (int domainId, void *msg, int *response);
  int (*flush)      (int domainId);
  int (*subscribe)  (int domainId, char *subject, char *type, cMsgCallback *callback,
                     void *userArg, cMsgSubscribeConfig *config);
  int (*unsubscribe)(int domainId, char *subject, char *type, cMsgCallback *callback);
  int (*get)        (int domainId, void *sendMsg, time_t timeout, void **replyMsg);
  int (*start)      (int domainId);
  int (*stop)       (int domainId);
  int (*disconnect) (int domainId);
} domainFunctions;


/* holds function pointers by domain type */
typedef struct domainTypeInfo_t {
  char *type;
  domainFunctions *functions;
} domainTypeInfo;


/* structure containing all domain info */
typedef struct cMsgDomain_t {
  /* init state */
  int initComplete; /* 0 = No, 1 = Yes */
  int id;           /* index into an array of this domain structure */
  int implId;       /* index into the array of the implementation domain struct */

  /* other state variables */
  int receiveState;
  int lostConnection;
  
  char *type;        /* domain type (coda, JMS, SmartSockets, etc.) */
  char *name;        /* name of user (this program) */
  char *udl;         /* UDL of cMsg name server */
  char *description; /* user description */
    
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
  int     getRequest;       /* is message a get request? 0=n,else=y */
  int     getResponse;      /* is message a response to a get request? 0=n,else=y */
  
  char   *sender;
  int     senderId;         /* in case fred dies and resurrects - not necessary! */
  char   *senderHost;
  time_t  senderTime;       /* time in seconds since Jan 1, 1970 */
  int     senderMsgId;      /* set by client system */
  int     senderToken;      /* set by sender user code */
  
  char   *receiver;
  char   *receiverHost;
  time_t  receiverTime;     /* time in seconds since Jan 1, 1970 */
  int     receiverSubscribeId;
  
  char   *domain;
  char   *subject;
  char   *type;
  char   *text;
  
  struct cMsg_t *next; /* for using messages in a linked list */
} cMsgMessage;


/* system msg id types */
enum msgId {
  CMSG_SERVER_CONNECT     = 0,
  CMSG_SERVER_DISCONNECT,
  CMSG_SERVER_RESPONSE,
  CMSG_KEEP_ALIVE,
  CMSG_SHUTDOWN,
  CMSG_GET_REQUEST,
  CMSG_GET_RESPONSE,
  CMSG_SEND_REQUEST,
  CMSG_SEND_RESPONSE,
  CMSG_SUBSCRIBE_REQUEST,
  CMSG_UNSUBSCRIBE_REQUEST,
  CMSG_SUBSCRIBE_RESPONSE,
  CMSG_SYNC_SEND_REQUEST,
  CMSG_UNGET_REQUEST
};

/* parameters used to subscribe to messages */
typedef struct subscribeConfig_t {
  int  init;          /* flag = 1 if structure was initialized */
  int  maySkip;       /* may skip messages if too many are piling up in cue */
  int  mustSerialize; /* if == 1, messages must be processed in order
                         received, else the messages may be processed
                         by parallel threads */
  int  maxCueSize;    /* maximum number of messages to cue for callback */
  int  skipSize;      /* maximum number of messages to skip over (delete) from the 
                       * cue for a callback when the cue size has reached it limit */
  int  maxThreads;    /* maximum number of supplemental threads to use for running
                       * the callback if mustSerialize is 0 (off) */
  int  msgsPerThread; /* enough supplemental threads are started so that there are
                       * at most this many unprocessed messages for each thread */
} subscribeConfig;



#ifdef	__cplusplus
}
#endif

#endif

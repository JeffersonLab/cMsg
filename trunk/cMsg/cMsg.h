/*
 *  cMsg.h
 *
 *  Defines cMsg (CODA Message) API, message structure, and return codes
 *
 *  E.Wolin, 24-Jun-2004, Jefferson Lab
 *
 *
 *  Notes: (need to be in implementation file, not here!)
 *    can have multiple domain/name connections
 *    user must always free messages
 *    can have many callbacks for the same subject and type
 *    each callback executes in its own thread
 *    flush may happen before cMsgFlush call
 */

#ifndef _cMsg_h
#define _cMsg_h


/* sccs id */
char sccsid[] = "%Z% Implementation of cMsg publish/subscribe API using FIPA agents";


/* required includes */
#include <time.h>


/* message structure */
typedef struct cMsg {
  int domainId;
  int sysMsgId;
  char *sender;
  int senderId;
  char *senderHost;
  time_t senderTime;
  int senderMsgId;
  char *receiver;
  char *receiverHost;
  time_t receiverTime;
  char *domain;
  char *subject;
  char *type;
  char *text;
};


/* message receive callback */
typedef void cMsgCallback(cMsg *msg, void *userArg);


/* function prototypes */
#ifdef __cplusplus
extern "C" {
#endif

  int cMsgInit(char *myDomain, char *myName, char *myDescription, int *domainId);
  int cMsgSend(int domainId, char *subject, char *type, char *text);
  int cMsgFlush(int domainId);
  int cMsgGet(int domainId, char *subject, char *type, time_t timeout, cMsg **msg);
  int cMsgSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback, void *userArg);
  int cMsgUnSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback);
  int cMsgReceiveStart(int domainId);
  int cMsgReceiveStop(int domainId);
  int cMsgFree(cMsg *msg);
  int cMsgDone(int domainId);
  int cMsgPerror(int error);
  

  char*     cMsgGetDomain(int domainId);
  char*     cMsgGetName(int domainId);
  char*     cMsgGetDescription(int domainId);
  char*     cMsgGetHost(int domainId);
  int 	    cMsgGetSendSocket(int domainId);
  int 	    cMsgGetReceiveSocket(int domainId);
  pthread_t cMsgGetPendThread(int domainId);
  int 	    cMsgGetInitState(int domainId);
  int 	    cMsgGetReceiveState(int domainId);


#ifdef __cplusplus
}
#endif


/* return codes */
enum {
  CMSG_OK              	  = 0,
  CMSG_ERROR,
  CMSG_NOT_IMPLEMENTED,
  CMSG_BAD_ARGUMENT,
  CMSG_NAME_EXISTS,
  CMSG_NOT_INITIALIZED,
  CMSG_ALREADY_INIT,
  CMSG_LOST_CONNECTION,
  CMSG_TIMEOUT,
  CMSG_NETWORK_ERROR,
  CMSG_PEND_ERROR,
  CMSG_ILLEGAL_MSGTYPE,
  CMSG_OUT_OF_MEMORY,
};


#endif /* _cMsg_h */

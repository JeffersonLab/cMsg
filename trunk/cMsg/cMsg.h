/*
 *  cMsg.h
 *
 *  Defines cMsg API, data structures, and return codes
 *
 *  E.Wolin, 14-Jun-2004, Jefferson Lab
 *
 *
 *  Notes:
 *    user must always free the message
 *    can have many callbacks for the same subject and type
 *    each callback executes in its own thread
 *    flush may happen before cMsgFlush call
 *
 */

#ifndef _cMsg_h
#define _cMsg_h


/* sccs id */
char sccsid[] = "%Z% Implementation of cMsg publish/subscribe API using FIPA agents";


/* required includes */
#include <time.h>


/* cMsg message structure */
typedef struct cMsg {
  int sysMsgId;
  char *sender;
  int senderId;
  char *senderHost;
  time_t senderTime;
  int senderMsgId;
  char *receiver;
  char *receiverHost;
  time_t receiverTime;
  char *project;
  char *subject;
  char *type;
  char *text;
};


/* cMsg message callback */
typedef void cMsgCallback(cMsg *msg, void *userArg);


/* cMsg function prototypes */
#ifdef __cplusplus
extern "C" {
#endif

  int cMsgInit(char *myProject, char *myName, char *myDescription);
  int cMsgSend(char *subject, char *type, char *text);
  int cMsgFlush(void);
  int cMsgGet(char *subject, char *type, time_t timeout, cMsg **msg);
  int cMsgSubscribe(char *subject, char *type, cMsgCallback *callback, void *userArg);
  int cMsgUnSubscribe(char *subject, char *type, cMsgCallback *callback);
  int cMsgReceiveStart(void);
  int cMsgReceiveStop(void);
  int cMsgFree(cMsg *msg);
  int cMsgDone(void);
  int cMsgPerror(int error);
  

  char*     cMsgGetProject();
  char*     cMsgGetName();
  char*     cMsgGetDescription();
  char*     cMsgGetHost();
  int 	    cMsgGetSendSocket();
  int 	    cMsgGetReceiveSocket();
  pthread_t cMsgGetPendThread();
  int 	    cMsgGetInitState();
  int 	    cMsgGetReceiveState();


#ifdef __cplusplus
}
#endif


/* cMsg return codes */
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

/*
 *  cMsg.c
 *
 *  Implements cMsg client api using CODA FIPA agent system
 *
 *  E.Wolin, C.Timmer, 17-Jun-2004, Jefferson Lab
 *
 *
 * still to do:
 *    integrate with Carl's stuff
 *    make everything thread-safe, get static assignements correct (dispatchCallback?)
 *    need to lock various parts of code, e.g. cMsgInit, ...
 *    is atexit handler needed?
 *
 */


/* built-in limits */
#define MAXSUBSCRIBE 100
#define MAXCALLBACK   10



/* system includes */
#include <stdio.h>
#include <strings.h>
#include <socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <pthread.h>


/* package includes */
#include cMsg.h


/* init info */
static char *project;
static char *name;
static char *description;
static char host[128];
static int sendSocket;
static int receiveSocket;
static pthread_t pendThread;


/* init state */
static int initSockets    = 0;
static int initPend       = 0;
static int initServer     = 0;
static int initComplete   = 0;


/* other state variables */
static int receiveState   = 0;
static int lostConnection = 0;


/* for dispatching callbacks in their own threads */
typedef struct dispatchCbInfo {
  cMsgCallback *callback;
  void *userArg;
  cMsg *msg;
}


/* for subscribe lists */
typedef struct subscribeCbInfo {
  cMsgCallback *callback;
  void *userArg;
}
struct {
  int active;
  char *subject;
  char *type;
  subscribeCbInfo cbInfo[MAXCALLBACK];
} subscribeInfo[MAXSUBSCRIBE];


/* misc variables */
static char *excludedChars = "\`\'\"";


/* system msg id types */
enum msgId {
  CMSG_SERVER_CONNECT     = 0,
  CMSG_SERVER_RESPONSE,
  CMSG_KEEP_ALIVE,
  CMSG_HEARTBEAT,
  CMSG_SHUTDOWN,
  CMSG_GET_REQUEST,
  CMSG_GET_RESPONSE,
  CMSG_SUBSCRIBE_REQUEST,
  CMSG_SUBSCRIBE_RESPONSE,
}


/* for c++ */
#ifdef __cplusplus
extern "C" {
#endif


/* local prototypes */
static int checkString(char *s);
static cMsg *extractMsg(int ???);
static cMsg *copyMsg(cMsg *msgIn);
static void *dispatchCallback(void *param);
static void *pend(void *param);



/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/


int cMsgInit(char *myProject, char *myName, char *myDescription) {

  int i,j;


  /* reinit on init after lost connection */
  if(lostConnection==1)cMsgDone();


  /* init called twice */
  if(initComplete==1)return(CMSG_ALREADY_INIT);
  
  
  /* check args */
  if( (checkString(myProject)!=0) ||
      (checkString(myname)!=0)    )
    return(CMSG_BAD_ARGUMENT);
  
  
  /* init variables */
  for(i=0; i<MAXSUBSCRIBE; i++) {
    subscribeInfo[i].active=0;
    for(j=0; j<MAXCALLBACK; j++)subscribeInfo[i].cbInfo[j]=NULL;
  }


  /* store names, can be changed until server connection established */
  project=strdup(myProject);
  name=strdup(myName);
  description=strdup(myDescription);
  gethostname(host,sizeof(host));
  
  
  /* get send and receive sockets */
  if(initSockets==0) {
    sendSocket=xxx();
    receiveSocket=xxx();
    if()return(CMSG_NETWORK_ERROR);
    initSocket=1;
  }

  
  /* launch pend thread and start listening on receive socket */
  if(initPend==0) {
    if(!pthread_create(&pendThread,NULL,pend,NULL))return(CMSG_PEND_ERROR);
    initPend=1;
  }
     
     
  /* connect to server and check if name is unique */
  if(initServer==0) {
    if()return(CMSG_NAME_EXISTS);
    if()return(CMSG_TIMEOUT);
    initServer=1;
  }
     

  /* two-way communication with agent now established */
  initComplete=1;
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cMsgSend(char *subject, char *type, char *text) {


  if(initComplete!=1)return(CMSG_NOT_INITIALIZED);
  if(lostConnection==1)return(CMSG_LOST_CONNECTION);


  /* check args */
  if( (checkString(subject)!=0) ||
      (checkString(type)!=0)    ||
      (text==NULL)              )
    return(CMSG_BAD_ARGUMENT);
  

  /* fill outgoing buffer */


  /* queue message for sending */


  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cMsgFlush(void) {


  if(initComplete!=1)return(CMSG_NOT_INITIALIZED);
  if(lostConnection==1)return(CMSG_LOST_CONNECTION);


  /* flush outgoing buffers */
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cMsgGet(char *subject, char *type, time_t timeout, cMsg **msg) {


  if(initComplete!=1)return(CMSG_NOT_INITIALIZED);
  if(lostConnection==1)return(CMSG_LOST_CONNECTION);


  /* check args */
  if( (checkString(subject)!=0) ||
      (checkString(type)!=0)    ||
      (msg==NULL)               )
    return(CMSG_BAD_ARGUMENT);
  

  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cMsgSubscribe(char *subject, char *type, cMsgCallback *callback, void *userArg) {

  int i,j,iok,jok;


  if(initComplete!=1)return(CMSG_NOT_INITIALIZED);
  if(lostConnection==1)return(CMSG_LOST_CONNECTION);


  /* check args */
  if( (checkString(subject)!=0) ||
      (checkString(type)!=0)    !!
      (callback==NULL)          )
    return(CMSG_BAD_ARGUMENT);
  
  
  /* add to callback list if subscribe to same subject/type exists */
  iok=0;
  for(i=0; i<MAXSUBSCRIBE; i++) {
    if((subscribeInfo[i].active==1) && 
       (strcmp(subscribeInfo[i].subject,subject)==0) && 
       (strcmp(subscribeInfo[i].type,type)==0) ) {
      iok=1;

      jok=0;
      for(j=0; j<MAXCALLBACK; j++) {
	if(subscribeInfo[i].cbInfo[j].callback==NULL) {
	  subscribeInfo[i].cbInfo[j]callback=callback;
	  subscribeInfo[i].cbInfo[j]userArg=userArg;
	  jok=1;
	}
      }
      break;

    }
  }
  if((ok==1)&&(jok==0))return(CMSG_OUT_OF_MEMORY);
  if((ok==1)&&(jok==1))return(CMSG_OK);


  /* no match, make new entry and notify server */
  iok=0;
  for(i=0; i<MAXSUBSCRIBE; i++) {
    if(subscribeInfo[i].active==0) {
      subscribeInfo[i].active=1;
      subscribeInfo[i].subject=strdup(subject);
      subscribeInfo[i].type=strdup(type);
      subscribeInfo[i].cbInfo[j].callback=callback;
      subscribeInfo[i].cbInfo[j].userArg=userArg;
      iok=1;
      /* notify server */
    }
  }
  if(iok==0)return(CMSG_OUT_OF_MEMORY);
  else return(CMSG_NOT_IMPLEMENTED);

}


/*-------------------------------------------------------------------*/


int cMsgUnSubscribe(char *subject, char *type, cMsgCallback *callback) {

  int i,j,count;


  if(initComplete!=1)return(CMSG_NOT_INITIALIZED);
  if(lostConnection==1)return(CMSG_LOST_CONNECTION);


  /* check args */
  if( (checkString(subject)!=0) ||
      (checkString(type)!=0)    )
    return(CMSG_BAD_ARGUMENT);


  /* search entry list */
  for(i=0; i<MAXSUBSCRIBE; i++) {
    if((subscribeInfo[i].active==1) && 
       (strcmp(subscribeInfo[i].subject,subject)==0) && 
       (strcmp(subscribeInfo[i].type,type)==0) ) {


      /* search callback list */
      count=0;
      for(j=0; j<MAXCALLBACK; j++) {
	if(subscribeInfo[i].cbInfo[j].callback!=NULL) {
	  count++;
	  if(subscribeInfo[i].cbInfo[j].callback==callback)subscribeInfo[i].cbInfo[j].callback==NULL;
	}
      }


      /* delete entry and notify server if no more callbacks for this subject/type */
      if(count<=1) {
	subscribeInfo[i].active=0;
	free(subscribeInfo[i].subject);
	free(subscribeInfo[i].type);

	/* notify server */

      }
      break;
    }
  }

  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cMsgReceiveStart(void) {

  if(initComplete!=1)return(CMSG_NOT_INITIALIZED);
  if(lostConnection==1)return(CMSG_LOST_CONNECTION);

  receiveState=1;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgReceiveStop(void) {

  if(initComplete!=1)return(CMSG_NOT_INITIALIZED);
  if(lostConnection==1)return(CMSG_LOST_CONNECTION);

  receiveState=0;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgFree(cMsg *msg) {


  if(msg==NULL)return(CMSG_BAD_ARGUMENT);

  free(msg->project);
  free(msg->sender);
  free(msg->senderHost);
  free(msg->receiver);
  free(msg->receiverHost);
  free(msg->subject);
  free(msg->type);
  free(msg->text);
  free(msg);

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgDone(void) {


  /* disconnect from server */
  if(initServer==1){
    if(lostConnection) {
    } else {
    }
  }


  /* close sockets */
  if(initSockets==1){
  }


  /* stop pend thread */
  if(initPend==1)pthread_destroy(pendThread);


  /* reset vars, free memory */
  receiveState=0;
  initSockets=0;
  initPend=0;
  initServer=0;
  initComplete=0;
  lostConnection=0;
  free(project);
  free(name);
  free(description);


  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cMsgPerror(int error) {

  switch(error) {

  case CMSG_OK:
    printf("CMSG_OK:  action completed successfully\n");
    break;

  case CMSG_ERROR:
    printf("CMSG_ERROR:  generic error return\n");
    break;

  case CMSG_NOT_IMPLEMENTED:
    printf("CMSG_NOT_IMPLEMENTED:  function not implemented\n");
    break;

  case CMSG_BAD_ARGUMENT:
    printf("CMSG_BAD_ARGUMENT:  one or more arguments bad\n");
    break;

  case CMSG_NAME_EXISTS:
    printf("CMSG_NAME_EXISTS: another process in this project is using this name\n");
    break;

  case CMSG_NOT_INITIALIZED:
    printf("CMSG_NOT_INITIALIZED:  cMsgInit needs to be called\n");
    break;

  case CMSG_ALREADY_INIT:
    printf("CMSG_ALREADY_INIT:  cMsgInit already called\n");
    break;

  case CMSG_LOST_CONNECTION:
    printf("CMSG_LOST_CONNECTION:  connection to cMsg server lost\n");
    break;

  case CMSG_TIMEOUT:
    printf("CMSG_TIMEOUT:  no response from cMsg server within timeout period\n");
    break;

  case CMSG_NETWORK_ERROR:
    printf("CMSG_NETWORK_ERROR:  error talking to cMsg server\n");
    break;

  case CMSG_PEND_ERROR:
    printf("CMSG_PEND_ERROR:  error waiting for messages to arrive\n");
    break;

  case CMSG_ILLEGAL_MSGTYPE:
    printf("CMSG_ILLEGAL_MSGTYPE:  pend received illegal message type\n");
    break;

  case CMSG_OUT_OF_MEMORY:
    printf("CMSG_OUT_OF_MEMORY:  ran out of memory\n");
    break;

  default:
    printf("?cMsgPerror...no such error: %d\n",error);
    break;
  }

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/* internal functions */

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/


static int checkString(char *s) {

  int i;

  if(s==NULL)return(1);

  /* check for printable character */
  for(i=0; i<strlen(s); i++) {
    if(isgraph(s[i])!=0)return(1);
  }

  /* check for excluded chars */
  if(strpbrk(s,excludedChars)!=0)return(1);

  /* string ok */
  return(0);
}



/*-------------------------------------------------------------------*/


static cMsg *extractMsg(some tcp buffer pointer) {

  struct cMsg *msg;
  
  /* allocate memory for message */
  msg = (struct cMsg*)malloc(sizeof(struct cMsg));


  /* fill message fields */
  msg->sysMsgId	     =	;
  msg->sender  	     =	strdup();
  msg->senderId      =	;
  msg->senderHost    =	strdup();
  msg->senderTime    =	;
  msg->senderMsgId   =	;
  msg->receiver      =	strdup(name);
  msg->receiverHost  =	strdup(host);
  msg->receiverTime  =	time(NULL);
  msg->project	     =	strdup();
  msg->subject	     =	strdup();
  msg->type   	     =	strdup();
  msg->text   	     =	strdup();
  
  return(msg);
}



/*-------------------------------------------------------------------*/


static cMsg *copyMsg(cMsg *msgIn) {

  struct cMsg *msgCopy;
  
  /* allocate memory for copy of message */
  msgCopy = (struct cMsg*)malloc(sizeof(struct cMsg));


  /* fill message fields */
  msgCopy->sysMsgId    	 =  msgIn->sysMsgId;
  msgCopy->sender      	 =  strdup(msgIn->sender);
  msgCopy->senderId    	 =  msgIn->senderId;
  msgCopy->senderHost  	 =  strdup(msgIn->senderHost);
  msgCopy->senderTime  	 =  msgIn->senderTime;
  msgCopy->senderMsgId 	 =  msgIn->senderMsgId;
  msgCopy->receiver    	 =  strdup(msgIn->receiver);
  msgCopy->receiverHost	 =  strdup(msgIn->receiverHost);
  msgCopy->receiverTime	 =  time(msgIn->receiverTime);
  msgCopy->project     	 =  strdup(msgIn->project);
  msgCopy->subject     	 =  strdup(msgIn->subject);
  msgCopy->type	       	 =  strdup(msgIn->type);
  msgCopy->text	       	 =  strdup(msgIn->text);
  
  return(msgCopy);
}



/*-------------------------------------------------------------------*/


static void *pend(void *param) {


  dispatchCbInfo *dcbi;
  pthread_t newThread;


  /* wait for messages */


  /* got one, parse msgSysId and decide what to do */
  /* if user message then copy and launch callback in new thread */
  sysMsgId=xxx;
  switch(sysMsgId) {

  case CMSG_SERVER_RESPONSE:
    break;

  case CMSG_KEEP_ALIVE:
    break;

  case CMSG_SHUTDOWN:
    break;

  case CMSG_GET_RESPONSE:
    break;

  case CMSG_SUBSCRIBE_RESPONSE:
    for(;;) {
      if() { 
	dcbi=(dispatchCbInfo*)malloc(sizeof(dispatchCbInfo));
	dcbi->callback=xxx;
	dcbi->userArg =xxx;
	dcbi->msg=extractMsg(???);
	pthread_create(&newThread,NULL,dispatchCallback,(void*)dcbi);
      }
    }
    break;

  default:
    cMsgPerror(CMSG_ILLEGAL_MSGTYPE);
    break;
  }

}


/*-------------------------------------------------------------------*/


static void *dispatchCallback(void *param) {
    
  dispatchCbInfo *dcbi;

  dcbi=(dispatchCbInfo*)param;
  dcbi->callback(dcbi->msg,dcbi->userArg);
  free(*dcbi);

}

  
/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/* access functions */

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/


char* cMsgGetProject(void) {
  return(project);
}
  
  
/*-------------------------------------------------------------------*/


char* cMsgGetName(void) {
  return(name);


/*-------------------------------------------------------------------*/


char* cMsgGetDescription(void) {
  return(description);
}


/*-------------------------------------------------------------------*/


char* cMsgGetHost(void) {
  return(host);
}


/*-------------------------------------------------------------------*/


int cMsgGetSendSocket(void) {
  return(sendSocket);
}


/*-------------------------------------------------------------------*/


int cMsgGetReceiveSocket(void) {
  return(receiveSocket);
}


/*-------------------------------------------------------------------*/


int cMsgGetPendThread(void) {
  return(pendThread);
}


/*-------------------------------------------------------------------*/


int cMsgGetInitState(void) {
  return(initComplete);
}


/*-------------------------------------------------------------------*/


int cMsgGetReceiveState(void) {
  return(receiveState);
}


/*-------------------------------------------------------------------*/


#ifdef __cplusplus
}
#endif


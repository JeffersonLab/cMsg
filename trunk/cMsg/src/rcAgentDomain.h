/*---------------------------------------------------------------------------*
 *  Copyright (c) 2005        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Author:  Vardan Gyurjyan                                                *
 *             gurjyan@jlab.org                  Jefferson Lab, MS-6A         *
 *             Phone: (757) 269-5879             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5519             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

/** Define the sleep time in msec.*/
#define SLEEP_TIME 1

/** Socket buffer size*/
#define SOCKET_BUF_SIZE 49640

/** aget message size*/
#define AGENT_MESSAGE_SIZE  2048

/** cMsg message size*/
#define CMSG_MAX_SIZE  1500

/** max number of messages in the output queue to Agent*/
#define MQ_OUT_MAXMSG  16

/** max output message queue size*/
#define MQ_OUT_MSGSIZE 2048

/** max number of messages in the queu of the message handler*/
#define MQ_MAXMSG  8

/** max message queue size for the handler*/
#define MQ_MSGSIZE 2048

/** max host name*/
#define MAX_HOSTNAME_LENGTH        256

/** Max number of subscriptions client allowed to have*/
#define MAX_SUBSCRIPTIONS 111

/** Max number of callbacks user can have for the same subject-type*/
#define MAX_CALLBACKS 111

/** Error calling listen on the socket. */
#define ERROR_LISTENING 1
/** Problem with the accept call.*/
#define ERROR_NETWORK_ACCEPT 2
/** Error at getsocketname call.  */
#define ERROR_RADING_SOCKET_INFO 3
/** Unable to read the header of the message.*/
#define ERROR_READING_MESSAGE 4
/** Unable to start the private agent listening thread.*/
#define ERROR_STARTING_SERVER 5
/** Unable to start the message sending thread tothe agent.*/
#define ERROR_STARTING_CLIENT 6
/** Error reading the env variables*/
#define ERROR_READING_ENV 7
/** Error listening, when we put the socket into the pasive mode*/
#define ERROR_NETWORK_LISTENING 8
/** Error starting message handler queue*/
#define ERROR_STARTING_QUEUE 9
/** Error starting message handler thread*/
#define ERROR_STARTING_HANDLER 10
/** Error removing message queue*/
#define ERROR_REMOVING_MQ 11  
/** Error during unsubscribe*/
#define ERROR_UNSUBSCRIBE 12
/** Error sending the message to the agent*/
#define ERROR_TRANSMITING_TO_AGENT 13
/** Request for the agent on the platform failed*/
#define ERROR_GETTING_AGENT 14
/** Eroor  reading socket nfo by executing getsockname*/
#define ERROR_READING_SOCKET_INFO 15
/** Error puting msg into the msg queue*/
#define ERROR_WRITING_Q 16
/** Error exceeded the number of allowed callbacks for the same subject-type message*/
#define ERROR_NO_MORE_CALLBACKS 17
/** Error exceeded the number of allowed subscriptions*/
#define ERROR_NO_MORE_SUBSCRIPTIONS 18
/** Error indicating that there is no active subscription*/
#define ERROR_NOT_SUBSCRIBED 19

/** User integer of cMsg defines the subscribe action*/
#define SUBSCRIBE 777

/** User integer of cMsg defines the unsubscribe action*/
#define UNSUBSCRIBE 888 

/* commands, which is the same as msg id comming from the agentserver*/
#define INIT        789

/** Successfull execution of the initialization process.     */
#define INIT_COMPLETED 5

#include "cMsg.h"
#include "cMsgPrivate.h"
#include "errors.h"


/** Message structure*/
typedef struct message_st {
  int    version;                 /* version of this protocol */
  int    priority;                /* defines the priority of the message */
  int    usrInt;                  /* user defined integer */
  int senderTime;                 /* time */
  int    msgId;                   /* id of the message necessary for callbacks */
  int    senderPort;              /* Port of the sender agent */
  int    senderHostLength;        /* agent host name lenght */
  int    senderLength;            /* length of the senders name */
  int    subjectLength;           /* length of the subject */
  int    typeLength;              /* length of the type */
  int    contentLength;           /* length of the message content */
  char  *senderHost;              /* host of the sender agent */
  char  *sender;                  /* sender name */
  char  *subject;                 /* subject of the message */
  char  *type;                    /* type of the message */
  char  *content;                 /* content of the message */
} msg;


/** Synchronized structure indicating the existance and state of the handling threads*/
typedef struct State {
  int listen_thr;
  int send_thr;
  pthread_mutex_t mutex;
} state_t;

/** Struncture of network information for the physical client */
typedef struct NetInfo {
  int port;
  char *host;
  char *name;
  char*type;
} netinfo_t;

/** Struncture for single user callback function*/
typedef struct Callback {
  cMsgCallback *callback;
  void * userArg;
}callback_t;


/** Structure for single subscription */
typedef struct Sub {
  char *subject;
  char *type ;
  int thread_stat;
  int callbackCount;
  mqd_t messageQ;
  char *mqd_name;
  callback_t *callback_list[MAX_CALLBACKS];
}sub_t;


/** Synchronized structure of all subscriptions */
typedef struct Subscriptions {
  int queueCount;
  sub_t *subs_list[MAX_SUBSCRIPTIONS];
  pthread_mutex_t mutex;
}subscriptions_t;

/** Synchronized structure of the message output queue, 
 *  where send messages will be queueud before sending to the agent.*/
typedef struct msgoutq {
  mqd_t mqd;
  pthread_mutex_t mutex;
} msgoutq_t;

/** Structure which is passed as an argument to the user callback handling thread*/
typedef struct Usera {
  callback_t *usrcallb;
  cMsgMessage *usrmsg;
} usera_t;

/* Global variables */
/** the structure keeps the integers to control listening and sending threads*/
state_t *myState;                

/** Structure keeps the list of all subscriptions*/
subscriptions_t *mySubscriptions; 

/** holds the queue descriptor for the message output queue*/
msgoutq_t *myMsgOutQ;             

/* function prototypes. Wrapper functions for Socket system calls, etc.*/

int np_socket( int domain, int type, int protocol) ;
int np_bind(int s, const struct sockaddr *addr, int addrlen);
int np_connect(int s, struct sockaddr *addr, int addrlen);
int np_read(int sd, void *ptr, int size);
int np_readString(int sd, char *ptr, int size);

int connect2Platform(int sending2port, char *sending2Host);

int fillMsgStructure(msg *Msg, int priority, int uint, int senderTime, int id, int port, char *senderhost, char *sender, char *subject, char *type, char *data);
int transmit2Agent( int sk, msg *Msg);
int state_init(state_t *myState);
int subscriptions_init(subscriptions_t *subs);
int outQ_init(msgoutq_t *q);
int requestAgent(char *name, char *type, int port, char *host);

int cMsg2Wire(cMsgMessage *cmsg, msg *wire);
int Wire2cMsg( msg *wire, cMsgMessage *cmsg);

void *listen2Agent(void *arg);
void *send2Agent(void *arg);
void *subsHandler(void *arg);
int   sun_setconcurrency(int newLevel);
int   sun_getconcurrency(void);


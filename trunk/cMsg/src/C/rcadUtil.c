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
/* Helper functions, including wrapper functions for some socket calls.
 * These functions assume any error is a fatal error! */


#ifdef VXWORKS

#include <vxWorks.h>
#include <taskLib.h>
#include <sockLib.h>
#include <inetLib.h>
#include <hostLib.h>
#include <ioLib.h>
#include <time.h>
#include <net/uio.h>
#include <net/if_dl.h>

#else

#include <sys/uio.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/utsname.h>

#endif

#include <stdio.h>	/* standard C i/o facilities */
#include <stdlib.h>	/* needed for atoi() */
#include <unistd.h>	/* Unix System Calls */
#include <sys/types.h>	/* system data type definitions */
#include <sys/socket.h> /* socket specific definitions */
#include <netinet/in.h> /* INET constants and stuff */
#include <arpa/inet.h>	/* IP address conversion stuff */
#include <netdb.h>	/* Name lookups (gethostbyname) */
#include <pthread.h>
#include <errno.h>
#include <netinet/tcp.h>
#include <mqueue.h>
#include <strings.h>

#ifdef sun
#include <sys/filio.h>
#endif

#include "cMsgPrivate.h"
#include "cMsg.h"
#include "rcAgentDomain.h"


/**
 * wrapper for socket system call, assumes it is fatal if a socket
 * cannot be created.
 *
 * @param domain inet domain, for eg. PF_INET
 * @param type socket type, for eg. SOCK_STREAM
 * @protocol defins the protocol
 * @returns socket descriptor
 *
 ****************************************************/
int np_socket( int domain, int type, int protocol) {
  int sk;
  if ((sk = socket( domain, type, 0 )) < 0) {
    perror("Problem creating socket\n");
    exit(1);
  }
  return(sk);
}

/**
 * wrapper for socket binding.
 *
 * @param sd socket descriptor
 * @param addr socket address structure
 * @param addrlen socket address structure length
 * @returns execution status
 *
 ***************************************************/
int np_bind(int sd, const struct sockaddr *addr, int addrlen) {
  int retval;
  
  if ((retval = bind(sd, addr, addrlen))<0) {
    perror("Problem binding\n");
    exit(1);
  }
  return(retval);
}

/**
 * wrapper for connect, any error is fatal.
 *
 * @param sd socket descriptor
 * @param addr socket address structure
 * @param addrlen socket address structure length
 * @returns execution status
 *
 *****************************************************/
int np_connect(int sd, struct sockaddr *addr, int addrlen) {
  int retval;
  
  if ((retval = connect(sd, addr, addrlen))<0) {
    perror("Problem calling connect\n");
    exit(1);
  }
  return(retval);
}

/**
 * Reads the header of the message sent by the client agent
 *
 * @param sd socket descriptor
 * @param ptr pointer to the memory location, where the data will be stored
 * @param size memory size
 * @returns execution status 
 *
 ****************************************************/
int np_read(int sd, void *ptr, int size) {
  int bytecount = 0;
  int n;
  
  /* Read the integers of the protocol*/
  while( (n =read(sd, ptr, size)) >0 ){
    bytecount++;
  }
  
  /* check if the reading of the integers were ok */
  if(bytecount != size ){
    perror("Problem reading the entire protocol from my agent\n");
    return(-1);
  }
  return(0);
}

/**
 * Reads the string from the protocol
 *
 * @param sd socket descriptor
 * @param ptr pointer to the char
 * @param size of the char
 * 
 ****************************************************/
int np_readString(int sd, char *ptr, int size) {
  
  int memSize;
  int bytecount = 0;
  int n;
  char *string;
  
  /* allocate memory a bit more */
  memSize = size + 1;
  string  = (char *) malloc((size_t) memSize);
  if (string == NULL) {
    fprintf(stderr, "Read-Protocol-String: cannot allocate memory\n");
    exit(1);
  }
  
  if (np_read(sd,ptr,size) < 0) {
    fprintf(stderr, "Read-Protocol-String: cannot read the string\n");
  }
  
  if ( memSize > AGENT_MESSAGE_SIZE) {
    free((void *) string);
    return(-1);
  }
  
  /* add null terminator to the C string */
  string[size] = 0;
  /* copy to the pointer to string */
  ptr = (char *) strdup(string);
  return(0);
}

/**
 * Gets the port and hostname of the server. 
 * makes the connection and returns the socket descriptor.
 *
 * @param _sending2Port port number of the server
 * @param _sending2Host host name of the server
 * @returns socket descritpor
 *
 ******************************************************/
int connect2Platform(int sending2Port, char *sending2Host){
  
  int err = 0;                      /* Indicates successfull completion of server communication*/
  const int optval = 1;             /* needed for socket option setting. */
  const int size = SOCKET_BUF_SIZE; /* Defined in the .h file*/
  int sk               ;            /* socket descriptor*/
  struct sockaddr_in skaddr;
  struct hostent *hp;		    /* used for name lookup */
  
  
  /* create a socket IP protocol family (PF_INET), TCP protocol (SOCK_STREAM) */
  sk = np_socket( PF_INET, SOCK_STREAM, 0 );
  
  /* Set the socket options. No message queueing*/
  if(setsockopt(sk, IPPROTO_TCP, TCP_NODELAY, (void *) &optval, sizeof(optval)) < 0){
    close(sk);
    printf("Error during setting the socket options.");
  }
  
  /* Set send buffer size*/
  if(setsockopt(sk, SOL_SOCKET, SO_SNDBUF, (void *) &size, sizeof(size)) < 0){
    close(sk);
    printf("Error during setting the socket options.");
  }
  
  /* Set receive buffer size*/
  if(setsockopt(sk, SOL_SOCKET, SO_RCVBUF, (void *) &size, sizeof(size)) < 0){
    close(sk);
    printf("Error during setting the socket options.");
  }
  
  /* Fill in an address structure that will be used to specify
     the address of the server we want to connect to.
     Address family is IP  (AF_INET)
     Server IP address is found by calling gethostbyname with the
     name of the server (we get from the env. variable).
     Server port number is also from the env. variable)
  */
  
  bzero((void *)&skaddr, sizeof(skaddr));
  skaddr.sin_family = AF_INET;
  
  /* convert server agent hostname to a network byte order binary IP address */
  /* First try to convert using gethostbyname */
  
#ifdef VXWORKS
  
  servaddr.sin_addr.s_addr = hostGetByName((char *) sending2Host);
  if (servaddr.sin_addr.s_addr == -1) {
    close(sk);
    fprintf(stderr, "rcdomain: unknown server address for host %s\n",ip_address);
  }
#else
  if ((hp = gethostbyname(sending2Host))!=0) {
    
    /* Name lookup was successful - copy the IP address */
    memcpy( &skaddr.sin_addr.s_addr, hp->h_addr, hp->h_length);
  } else {
    
    /* Name lookup didn't work, try converting from dotted decimal */
#ifndef SUN
    if (inet_aton(sending2Host,&skaddr.sin_addr)==0) {
      printf("Invalid IP address: %s\n",sending2Host);
      exit(1);
    }
#else
    /*inet_aton is missing on Solaris - you need to use inet_addr! 
      inet_addr is not as nice, the return value is -1 if it fails
      (so we need to assume that is not the right address !) */
    skaddr.sin_addr.s_addr = inet_addr(sending2Host);
    if (skaddr.sin_addr.s_addr ==-1) {
      printf("Invalid IP address: %s\n",argv[1]);
      exit(1);
    }
#endif
#endif
  }
  skaddr.sin_port = htons(sending2Port);
  
  /* attempt to establish a connection with the server */
  np_connect(sk,(struct sockaddr *) &skaddr,sizeof(skaddr));
  
  return(sk);
}

/**
 * Fills the wire protocole mesage structure.
 *
 * @param Msg pointer to the internal wire protocole
 * @param priority of the message
 * @param uint user integer
 * @param senderTime time defineb by the sender
 * @param id of the message
 * @param port the port of the sender
 * @param senderhost host of the message generator
 * @param sender the name of the sender
 * @param subject the subject of the message
 * @param type the type of the message
 * @param data content of the message
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 ********************************************************************/
int fillMsgStructure(msg *Msg, int priority, int uint, int senderTime, int id, int port,
                     char *senderhost, char *sender, char *subject,
                     char *type, char *data){
  
  if (Msg == NULL) return(CMSG_BAD_ARGUMENT);
  Msg->version = 1;
  Msg->priority = priority;
  Msg->usrInt = uint;
  Msg->senderTime = senderTime;
  Msg->msgId = id;
  Msg->senderPort = port;
  
  gethostname(senderhost,MAX_HOSTNAME_LENGTH);
  
  Msg->senderHostLength = strlen(senderhost);
  Msg->subjectLength = strlen(subject);
  Msg->typeLength = strlen(type);
  Msg->contentLength = strlen(data);
  Msg->senderHost = senderhost;
  Msg->sender = sender;
  Msg->subject = subject;
  Msg->content = data;
  return (CMSG_OK);
}

/**
 * Sends the actual message to the agent.
 *
 * @param sk socket descriptor
 * @param Msg wire protocol message structure
 * @returns execution status: CMSG_OK - Ok, or ERROR_TRANSMITING_TO_AGENT
 *
 *************************************************************/
int transmit2Agent( int sk, msg *Msg) {
  
  int err = CMSG_OK;
  
  err = write(sk,(void *)Msg->version, sizeof(Msg->version));
  err = write(sk,(void *)Msg->priority, sizeof(Msg->priority));
  err = write(sk,(void *)Msg->usrInt, sizeof(Msg->usrInt));
  err = write(sk,(void *)Msg->senderTime, sizeof(Msg->senderTime));
  err = write(sk,(void *)Msg->msgId, sizeof(Msg->msgId));
  err = write(sk,(void *)Msg->senderPort, sizeof(Msg->senderPort));
  err = write(sk,(void *)Msg->senderHostLength, sizeof(Msg->senderHostLength));
  err = write(sk,(void *)Msg->senderLength, sizeof(Msg->senderLength));
  err = write(sk,(void *)Msg->subjectLength, sizeof(Msg->subjectLength));
  err = write(sk,(void *)Msg->typeLength, sizeof(Msg->typeLength));
  err = write(sk,(void *)Msg->contentLength, sizeof(Msg->contentLength));
  err = write(sk,(void *)Msg->senderHost,strlen(Msg->senderHost));
  err = write(sk,(void *)Msg->sender,strlen(Msg->sender));
  err = write(sk,(void *)Msg->subject,strlen(Msg->subject));
  err = write(sk,(void *)Msg->type,strlen(Msg->type));
  err = write(sk,(void *)Msg->content,strlen(Msg->content));
  
  if(err<0) return (ERROR_TRANSMITING_TO_AGENT);
  return(err);
}


/**
 * Initialization of the state synchronized structure
 *
 * @param myState pointer to stat_t structure
 *
 **************************************************/
int state_init(state_t *myState){
  myState->listen_thr = 0;
  myState->send_thr = 0;
  pthread_mutex_init(&(myState->mutex), NULL);
  return 1;
}


/**
 * Initialization of the state subscriptions structure
 *
 * @param subs pointer to subscriptions_t structure
 *
 **************************************************/
int subscriptions_init(subscriptions_t *subs){
  pthread_mutex_init(&(subs->mutex), NULL);
  return 1;
}

/**
 * Initialize the mutex for the message output queue structure
 *
 * @param q pointer to the msgoutq_t structure
 *
 ********************************************************/
int outQ_init(msgoutq_t *q){
  pthread_mutex_init(&(q->mutex), NULL);
  return 1;
}



/**
 * Opens the connection to the server agent and sends the request to start and agent.
 * For that it sends its name, type, i.e. roc, eb, er, etc. its listening port number
 * and its hostname.Server agent port and host name we get from env variables.
 *
 * @param name of the physical client
 * @param type of the component
 * @param port number of the cMsgAgentServer
 * @param host host name of the container where the server agent is running
 * @returns ERROR_READING_ENV in case we have a problem reading env variables
 * @retunrs CMSG_OK in case of successul communication
 ********************************************************************************/
int requestAgent(char *name, char *type, int port, char *host){
  
  int err;                         /* Indicates successfull completion of server communication*/
  int sk, sending2Port;            /* socket descriptor, server port and its listening port*/
  char *sending2Host;              /* serveragenthost*/
  msg *myMsg;
  char *sap;                       /* Server port number*/
  
  pthread_t pt;
  
  /* Getting port and host of the server */
  
  /* Get port number of the cMsgAgentServer from the env. variables.*/
  if( (sap = getenv("RC_SERVER_AGENT_PORT")) == NULL) {
    printf("Error reading RC_SERVER_AGENT_PORT env. variable");
    return(ERROR_READING_ENV);
  } else  sending2Port = atoi(sap);
  
  /* Get the host name of the server agent*/
  if( (sending2Host = getenv("RC_SERVER_AGENT_HOST")) == NULL) {
    printf("Error reading RC_SERVER_AGENT_HOST env. variable");
    return(ERROR_READING_ENV);
  }
  sk = connect2Platform(sending2Port, sending2Host);
  err = fillMsgStructure(myMsg, 0, 0, 0, 0, port, host, name, "register",type,"");
  
  /* Send a protocol*/
  err = transmit2Agent(sk, myMsg);
  close(sk);
  return(err);
}


/**
 * Function takes as a parameter the cMsgMessage and creates a message based on the wire protocole
 * for sending to the agent.
 *
 * @param cmsg pointer to the cMsgMEssage structure
 * @param wire pointer to the physical client/agent communication internal protocole structure
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 *
 *************************************************************************************************/

int cMsg2Wire(cMsgMessage_t *cmsg, msg *wire){

  int status;
  int priority;
  int userint = 0;
  struct timespec *usertime;
  char *creator;
  char *subject;
  char *type;
  char *text;
  
/* N.B. user integer of the cMsgMessage is used to set the priority of the rc wire protocole*/
  status = cMsgGetUserInt(cmsg,  &priority);
  status = cMsgGetUserTime(cmsg, usertime);
  status = cMsgGetCreator(cmsg, &creator);
  status = cMsgGetSubject(cmsg, &subject);
  status = cMsgGetType(cmsg, &type);
  status = cMsgGetText(cmsg, &text);
  
  if(status == CMSG_OK)
    /* Fill the wire protocole structure*/
    fillMsgStructure(wire, priority, userint, usertime->tv_sec, 0, 0, "", creator, subject, type, text);
  
  /* Free the cMsgMessage */
  cMsgFreeMessage(cmsg);
  return (status);
}


/**
 * Function takes as a parameter the msg ( Agent communication protocol) and creates a cMsg message, 
 * which is passed to the user handler function.
 *
 * @param wire pointer to the physical client/agent communication internal protocole structure
 * @param cmsg pointer to the cMsgMessage
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 *
 *************************************************************************************************/

int Wire2cMsg( msg *wire, cMsgMessage_t *cmsg){
  
  int status;
    struct timespec *usertime;

  
  if(wire == NULL) return(CMSG_BAD_ARGUMENT);

    /* fill the timespec structure*/
    usertime->tv_sec = wire->senderTime;
  
/* N.B. for rc wire protocole we have priority, but for cmsg it is userInt */
  status = cMsgSetUserInt(cmsg,  wire->priority);
  status = cMsgSetUserTime(cmsg, usertime);
  status = cMsgSetSubject(cmsg, wire->subject);
  status = cMsgSetType(cmsg, wire->type);
  status = cMsgSetText(cmsg, wire->content);
  
  /* Free the cMsgMessage */
  cMsgFreeMessage(wire);
  return (status);
}

/*---------------------------------------------------------------------------*
 *  Copyright (c) 2005        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Author:  Vardan  Gyurjyan                                                *
 *             gurjyan@jlab.org                  Jefferson Lab, MS-6A         *
 *             Phone: (757) 269-5879             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5519             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/
#include <stdio.h>	    /* standard C i/o facilities */
#include <stdlib.h>	    /* needed for atoi() */
#include <unistd.h>	    /* Unix System Calls */
#include <sys/types.h>	    /* system data type definitions */
#include <sys/socket.h>     /* socket specific definitions */
#include <netinet/in.h>     /* INET constants and stuff */
#include <arpa/inet.h>	    /* IP address conversion stuff */
#include <netdb.h>	    /* Name lookups (gethostbyname) */
#include <pthread.h>
#include <errno.h>

#ifdef VXWORKS
#include <vxWorks.h>
#include <taskLib.h>
#include <hostLib.h>
#include <timers.h>
#include <sysLib.h>
#endif

#include <mqueue.h>

#include "rcAgentDomain.h"


  /* Prototypes of the functions which implement the standard cMsg tasks in the cMsg domain. */
  static int   rcConnect(const char *myUDL, const char *myName, const char *myDescription,
			 const char *UDLremainder,int *domainId);
  static int   rcSend(int domainId, void *msg);
  static int   rcSyncSend(int domainId, void *msg, int *response);
  static int   rcFlush(int domainId);
  static int   rcSubscribe(int domainId, const char *subject, const char *type, cMsgCallbackFunc *callback,
                           void *userArg, cMsgSubscribeConfig *config);
  static int   rcUnsubscribe(int domainId, const char *subject, const char *type, cMsgCallbackFunc *callback,
                             void *userArg);
  static int   rcSubscribeAndGet(int domainId, const char *subject, const char *type,
                                 struct timespec *timeout, void **replyMsg);
  static int   rcSendAndGet(int domainId, void *sendMsg, struct timespec *timeout,
                            void **replyMsg);
  static int   rcStart(int domainId);
  static int   rcStop(int domainId);
  static int   rcDisconnect(int domainId);
  static int   rcSetShutdownHandler(int domainId, cMsgShutdownHandler *handler, void *userArg);
  static int   rcShutdown(int domainId, const char *client, const char *server, int flag);
  static void sMutexUnlock(pthread_mutex_t mx);
  static void sMutexLock(pthread_mutex_t mx);
  void *usrHandlThread(void * arg);  
  
  /** List of the functions which implement the standard cMsg tasks in the cMsg domain. */
  static domainFunctions functions = {rcConnect, rcSend,
				      rcSyncSend, rcFlush,
				      rcSubscribe, rcUnsubscribe,
				      rcSubscribeAndGet, rcSendAndGet,
				      rcStart, rcStop, rcDisconnect,
				      rcShutdown, rcSetShutdownHandler};
  
  /** cMsg domain type */
  domainTypeInfo rcDomainTypeInfo = {
    "rc",
    &functions
  };
  
  
  /**
   * Sends the request to the agent server to get a representative agent on
   * the agent platform. Accepts the messages from the server agent and starts 
   * agent listening thread.
   *
   * @param myUDL the Universal Domain Locator used to uniquely identify the cMsg
   *        server to connect to
   * @param myName name of this client
   * @param myDescription description of this client
   * @param UDLremainder partially parsed (initial cMsg:domainType:// stripped off)
   *                     UDL which gets passed down from the API level (cMsgConnect())
   * @param domainId pointer to integer which gets filled with a unique id referring
   *        to this connection.
   *
   * @returns ERROR_LISTENING Error calling listen on the socket. 
   * @returns ERROR_NETWORK_ACCEPT Problem with the accept call.
   * @returns ERROR_RADING_SOCKET_INFO Error at getsocketname call.  
   * @returns ERROR_READING__MESSAGE Unable to read the header of the message.
   * @returns ERROR_STARTING_SERVER Unable to start the private agent listening thread.
   * @returns ERROR_STARTING_CLIENT Unable to start the message sending thread tothe agent.
   * @returns INIT_COMPLETED Successfull execution of the initialization process.     
   *   
   ***********************************************************************************/   
  static int rcConnect(const char *myUDL, const char *myName, const char *myDescription,
		       const char *UDLremainder, int *domainId) {
    
    pthread_t pt;
    int status;                      
    /** ld listening socket descriptor*/
    int ld,sd;                       
    struct sockaddr_in skaddr;
    struct sockaddr_in from;
    int addrlen,length;
    /** listening port number */
    int lport;                       
    /** hostname */
    char lhost[MAX_HOSTNAME_LENGTH]; 
    int n;
    /** Number of the integer fields of the wire protocole mesage */
    int InComing[11];                
    /** pointer to the rcAgentDomain protocole message object*/  
    msg *myMsg;                      
    /** Network info of the physical client, i.e. listening port and host name*/
    netinfo_t *myNetworkInfo;        
    /**Pointer to the synchronized  state_t structure*/
    state_t *myState;
    /** Pointer to the synchronized subscriptions_t structure*/
    subscriptions_t *mySubscriptions;
    /** Pointer to the synchronized message output queue structure*/
    msgoutq_t * myMsgOutQ;
    
    /* Initialize mutexes */
    state_init(myState);
    subscriptions_init(mySubscriptions);
    outQ_init(myMsgOutQ);
    
    /* create a listening socket */
    ld = np_socket( PF_INET, SOCK_STREAM, 0 );
    
    /* establish our address, address family is AF_INET,
       our IP address is INADDR_ANY (any of our IP addresses)
       the port number is assigned by the kernel.
    */
    skaddr.sin_family = AF_INET;
    skaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    skaddr.sin_port = htons(0);
    
    np_bind(ld, (struct sockaddr *) &skaddr, sizeof(skaddr));
    
    /* find out what port we were assigned by the kernel. */
    length = sizeof( skaddr );
    if (getsockname(ld, (struct sockaddr *) &skaddr, &length)<0) {
      perror("Error getsockname\n");
      return(ERROR_READING_SOCKET_INFO);
    }
    lport = ntohs(skaddr.sin_port);
    printf("The %s server listening socket port number is %d\n", myName, lport);
    
    /* put the socket into passive mode (waiting for connections) */
    if (listen(ld,5) < 0 ) {
      perror("Error calling listen\n");
      return(ERROR_LISTENING);
    }
    
    /* Send the registration request to the agent platform. It gets the host name 
       and sends the registration request, including also his name and type to the 
       RC agent platfrom cMsgServerAgent. N.B. myDescription variable will define 
       the type of the component.*/
    gethostname(lhost,MAX_HOSTNAME_LENGTH);
    
/*Vardan: pass in copy of myName & myDescription*/
    if( (status = requestAgent(myName, myDescription, lport, lhost)) != CMSG_OK) return(status);
    
    /* fill the network information structure for this physical client*/
    myNetworkInfo->port = lport;
    myNetworkInfo->host = lhost;
/*Vardan: store copy of myName*/
    myNetworkInfo->name = myName;
/*Vardan: store copy of myDescription*/
    myNetworkInfo->type = myDescription;
    
    /* now process incoming connections from the cMsgAgentServer */
    while (1) {
      printf("Ready for a connection...\n");
      addrlen=sizeof(skaddr);
      if ( (sd = accept( ld, (struct sockaddr*) &from, &addrlen)) < 0) {
	perror("Problem with accept call\n");
	return(ERROR_NETWORK_ACCEPT);
      }
      printf("Got a connection - processing...\n");
      
      /*< This section is for debugging purposes */
      
      /* Determine and print out the address of the new server socket */
      length = sizeof( skaddr );
      if (getsockname(sd, (struct sockaddr *) &skaddr, &length)<0) {
	perror("Error getsockname\n");
	return(ERROR_READING_SOCKET_INFO);
      }
      printf("My server (listening) port number is %d\n",ntohs(skaddr.sin_port));
      printf("My server (listening)IP ADDRESS is %s\n",inet_ntoa(skaddr.sin_addr));
      /* print out the address of the cMsgServerAgent. */
      printf("cMsgServerAgent port number is %d\n",ntohs(from.sin_port));
      printf("cmSgServerAgent IP ADDRESS is %s\n",inet_ntoa(from.sin_addr));
      
      /*< End of the debugging section */
      
      /* Reading the header integers of the message from the cMsgServerAgent.
	 N.B msgId of the protocole defines the type of the request (in this case INFO)*/
      if( n=np_read(sd, (void*)InComing, sizeof(InComing)) <0) {
	perror("Problem reading the header of the incoming message.");
	
	/* Close the connection*/
	close(sd);
	return(ERROR_READING_MESSAGE);
      }
      
      /* we need to swap back to the local endian from the network */
      myMsg->version               = ntohl(InComing[0]);
      myMsg->priority              = ntohl(InComing[1]);
      myMsg->usrInt                = ntohl(InComing[2]);
      myMsg->senderTime            = ntohl(InComing[3]);
      myMsg->msgId                 = ntohl(InComing[4]);
      myMsg->senderPort            = ntohl(InComing[5]);
      myMsg->senderHostLength      = ntohl(InComing[6]);
      myMsg->senderLength          = ntohl(InComing[7]);
      myMsg->subjectLength         = ntohl(InComing[8]);
      myMsg->typeLength            = ntohl(InComing[9]);
      myMsg->contentLength         = ntohl(InComing[10]);
      
      /* The version comming back from the cMsgAgentServer will indicate if there is a name conflict in the platform,
       * or it is the wrong domain.*/
      if(myMsg->version == 9) { /*number 9 is defined in the cMsgConstants.java as errorAlreadyInitialized */
	perror("ERROR: name conflict in the agent platform \n");
	exit(-1);
      } else if(myMsg->version == 22) {/* number 22 indicates wrong domain. 
					  never will happen, since domain is hard codded =0*/
	perror("ERROR: wrong domain \n");
	exit(-1);
      }

      /* Check to see if we have an initialization request*/
      if ((int)INIT == myMsg->msgId){
	/* Check to see if we are already initialized. For that we check the 
	   global synchronized myState structure.*/
	/* lock the structure */
	sMutexLock(myState->mutex);
	/* listening thread is not yet started */
	if(myState->listen_thr = 0){
	  /* Start the thread which openins the socket to listen the agent*/
	  myState->listen_thr = 1;
	  status = pthread_create(&pt,NULL,listen2Agent,(void *)myNetworkInfo);
	  if (status!=0) {
	    perror("ERROR creating a listening (server) thread to the agent ");
	    myState->listen_thr = 0;
	    return(ERROR_STARTING_SERVER);
	  }
	} else {
	  printf("Already initialized and ready to accept messages from the agent.");
	}
	
	/* sending thread is not yet started */
	if(myState->send_thr = 0){
	  /* Start the thread sending messages to the agent*/
	  myState->send_thr = 1;
	  /* This thread will open connection to the private agent and will create a 
	   *  output message queue. It will then wait the messages at the queue and 
	   *  transport them to the agent */
	  status = pthread_create(&pt,NULL,send2Agent,(void *)myMsg);
	  if (status!=0) {
	    perror("ERROR creating a sending (client) thread ");
	    myState->send_thr = 0;
	    return(ERROR_STARTING_CLIENT);
	  }
	} else {
	  printf("Already initialized and ready to send messages to the agent.");
	}
	if((myState->listen_thr == 1) && (myState->send_thr == 1)){
	  /* Unlock the structure*/
	  sMutexUnlock(myState->mutex);
	  printf("Done with connection to the cMsgServerAgent- closing\n");
	  close(sd);
	  return(INIT_COMPLETED);
	} else {
	  /* Unlock the structure*/
	  sMutexUnlock(myState->mutex);
	}
      }
      printf("Done with connection to the cMsgServerAgent- closing\n");
      close(sd);
    }
  }
  
  /**
   * This routine sends a msg to the specified cMsgAgent domain server.  It is called
   * by the user through cMsgSend(). 
   *
   * @param domainId id number of the domain connection. For Agent domain it is = 0;
   * @param vmsg pointer to a message structure
   *
   * @returns CMSG_OK if successful
   * @returns ERROR_WRITING_Q
   **********************************************************************************/   
  static int rcSend(int domainId, void *cmsg) {
    
    int ret;  
    /** wire protocole message object*/
    msg *output;
    
    /* Cast the cmsg to cMsgMessage*/
    cMsgMessage_t *mycmsg = (cMsgMessage_t *)cmsg;
    
    /* Translate to the wire protocole*/
    if ((ret = cMsg2Wire(mycmsg, output)) != CMSG_OK) return(ret);
    
    /* put the message into the output queue*/
    sMutexLock((myMsgOutQ->mutex));
    ret = mq_send(myMsgOutQ->mqd,(void *)output, CMSG_MAX_SIZE, output->priority);
    sMutexUnlock((myMsgOutQ->mutex));      
    if(ret != EAGAIN) {          /* No problem wrting into the queue*/ 
      return(CMSG_OK);
    } else 
      return(ERROR_WRITING_Q);
  }
  
  
  /**
   * This routine is not implementd
   *
   * @returnes CMSG_NOT_IMPLEMENTED
   ************************************/
  static int rcSyncSend(int domainId, void *vmsg, int *response) {
    return(CMSG_NOT_IMPLEMENTED);
  }
  
  /**
   * This routine is not implementd
   *
   * @returnes CMSG_NOT_IMPLEMENTED
   ************************************/
  static int rcFlush(int domainId){
    return(CMSG_NOT_IMPLEMENTED);
  }
  
  /**
   * This routine is not implementd
   *
   * @returnes CMSG_NOT_IMPLEMENTED
   ************************************/
  static int rcSubscribeAndGet(int domainId, const char *subject, const char *type,
			       struct timespec *timeout, void **replyMsg){
    return(CMSG_NOT_IMPLEMENTED);
  }
  
  /**
   * This routine is not implementd
   *
   * @returnes CMSG_NOT_IMPLEMENTED
   ************************************/
  static int rcSendAndGet(int domainId, void *sendMsg, struct timespec *timeout,
			  void **replyMsg){
    return(CMSG_NOT_IMPLEMENTED);
  }
  
  /**
   * This routine is not implementd
   *
   * @returnes CMSG_NOT_IMPLEMENTED
   ************************************/
  static int rcStart(int domainId){
    return(CMSG_NOT_IMPLEMENTED);
  }
  
  /**
   * This routine is not implementd
   *
   * @returnes CMSG_NOT_IMPLEMENTED
   ************************************/
  static int rcStop(int domainId){
    return(CMSG_NOT_IMPLEMENTED);
  }
  
  /**
   * This routine is not implementd
   *
   * @returnes CMSG_NOT_IMPLEMENTED
   ************************************/
  static int rcDisconnect(int domainId){
    return(CMSG_NOT_IMPLEMENTED);
  }
  
  /**
   * This routine is not implementd
   *
   * @returnes CMSG_NOT_IMPLEMENTED
   ************************************/
  static int rcSetShutdownHandler(int domainId, cMsgShutdownHandler *handler, void *userArg){
    return(CMSG_NOT_IMPLEMENTED);
  }
  
  /**
   * This routine is not implementd
   *
   * @returnes CMSG_NOT_IMPLEMENTED
   ************************************/
  static int rcShutdown(int domainId, const char *client, const char *server, int flag){
    return(CMSG_NOT_IMPLEMENTED);
  }
  
  /**
   * This routine subscribes to messages of the given subject and type.
   * When a message is received, the given callback is passed the message
   * pointer and the userArg pointer and then is executed. A configuration
   * structure is given to determine the behavior of the callback.
   * This routine is called by the user through cMsgSubscribe().
   * Only 1 subscription for a specific combination of subject, type, callback
   * and userArg is allowed.
   *
   * @param domainId id number of the domain connection. For rc domain it is 0.
   * @param subject subject of messages subscribed to
   * @param type type of messages subscribed to
   * @param callback pointer to callback to be executed on receipt of message
   * @param userArg user-specified pointer to be passed to the callback
   * @param config pointer to callback configuration structure
   *
   * @returns CMSG_OK if successful
   * @returns CMSG_ALREADY_EXISTS if an identical subscription already exists
   ****************************************************************************/   
  static int rcSubscribe(int domainId, const char *subject, const char *type,cMsgCallbackFunc *callback,
			 void *userArg, cMsgSubscribeConfig *config) {
    
    int i,j;
    int status;
    pthread_t pt;  
    /** single subscription structure*/
    sub_t *subscription, *new_sub;  
    /** message queue attribute information*/
    struct mq_attr obuf;
    /** Output queue flags. O_NONBLOCK flag will 
     *  force error=EAGAIN in case the queue is empty. */
    int oflags = O_RDWR|O_CREAT|O_NONBLOCK;
    /** Message queue descriptor.*/
    mqd_t mymqd;
    /** Output message queue permissions*/
    int perms = 0777;
    /** Name of the queue*/
    char *Qname;
    /** cMsg structure*/
    cMsgMessage_t *message;
    /** callback_t structure for the user new callbacks*/
    callback_t *new_callb;    

    /* increase concurrency for this thread for early Solaris */
	int  con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);

    /* release system resouces when thread finishes */
    pthread_detach(pthread_self());
    /* See if the subscription already exists*/
    sMutexLock((mySubscriptions->mutex)); /* mutex lock */    

    for (i = 0; i<=mySubscriptions->queueCount; i++) { /*Loop over all registered subscriptions*/ 
      
      /* See if the queue for this subject and type exists*/
      if((strcmp(mySubscriptions->subs_list[i]->subject,subject)==0) && 
	 (strcmp(mySubscriptions->subs_list[i]->type, type)==0)){
	
	/* subject-type queue already exists, see if we have callback registered*/
	if(mySubscriptions->subs_list[i]->callbackCount <= MAX_CALLBACKS) {
	  for (j = 0; j<=MAX_CALLBACKS; j++) { /*Loop over all registered subscriptions*/ 
	    if ((mySubscriptions->subs_list[j]->callback_list[j]->callback == callback) && 
		(mySubscriptions->subs_list[j]->callback_list[j]->userArg == userArg)){ /* Yes, it is registered*/
	      sMutexUnlock((mySubscriptions->mutex));
	      return(CMSG_ALREADY_EXISTS);
	    }
	  }
	  /* No, it is not registered.Start registration*/
Vardan: store copy of subject
	  new_sub->subject = subject; /* add to the sub_t structure */
Vardan: store copy of type
	  new_sub->type = type;
	  
	  /* Create a new callback_t structure */
	  new_callb->callback = callback;
	  new_callb->userArg = userArg;
	  
	  /* Add this new callback structure to the callback_list*/
	  new_sub->callback_list[new_sub->callbackCount] = new_callb;
	  
	  /* Increment the callback count of the same subject-time message callbacks*/
	  new_sub->callbackCount += 1;

	  /* Add this new subscription to the subscriptions synchronized global structure*/
	  mySubscriptions->subs_list[mySubscriptions->queueCount] = new_sub;

	  sMutexUnlock((mySubscriptions->mutex));
	  return(CMSG_OK);           /* queue exists and we added new callback for handling*/
	} else {
	  printf("ERROR: exceeded the number of callbacks for %s and %s \n",subject, type);
	  sMutexUnlock((mySubscriptions->mutex));
	  return(ERROR_NO_MORE_CALLBACKS);
	}
      } /* queue exists*/
    }
    /* We went through all allowed number of subscriptions and there no 
     * message queue for this subject and type. Create one.*/	  
Vardan: store copy of subject
    new_sub->subject = subject; /* add to the sub_t structure */
Vardan: store copy of type
    new_sub->type = type;
    /* Create a new callback_t structure */
    new_callb->callback = callback;
    new_callb->userArg = userArg;
    
    /* Add this new callback structure to the callback_list*/
    new_sub->callback_list[new_sub->callbackCount] = new_callb;
    
    /* Increment the callback count of the same subject-time message callbacks*/
    new_sub->callbackCount += 1;
    
    /* start the message queue*/
Vardan: may not modify subject or type
    Qname =(char *) strcat(subject,type);
    /* Create the new message queue for this subscription*/
    mymqd = mq_open(Qname,oflags,perms,&obuf);
    new_sub->messageQ = mymqd;
    new_sub->mqd_name = Qname;
    new_sub->thread_stat = 1;
    if(-1 != (int)mymqd){
      /* set output queue message max size and max number of the messages.*/
      obuf.mq_msgsize = MQ_MSGSIZE;
      obuf.mq_maxmsg = MQ_MAXMSG;
      
      /* Start a thread to run the subscription handler function which will run as long as the subject and type 
       * queue is active. It will get the messages from the queue and dispatch to the proper callback user function.*/
      status = pthread_create(&pt,NULL,subsHandler,(void *)new_sub);
      if(status != 0) {
	new_sub->thread_stat = 0;
	free(new_sub);
	free(&obuf);
	sMutexUnlock((mySubscriptions->mutex));
	return(ERROR_STARTING_HANDLER);
      }
      else { /* Thread started ok.*/
	/* add the new queue to the list. */
	if(mySubscriptions->queueCount <= MAX_SUBSCRIPTIONS) {

	  mySubscriptions->subs_list[mySubscriptions->queueCount] = new_sub;

	  /* Increment the queue count*/
	  mySubscriptions->queueCount += 1;
	  
	  /* Send the subscription message to the agent. We send userint, subject and type.
	   * userint will define the action at the agent side.*/
	  /* Create cMsgMessage */
	  message = cMsgCreateMessage();
	  cMsgSetUserInt(message,SUBSCRIBE);
	  cMsgSetSubject(message,subject);
	  cMsgSetType(message,type);
	  rcSend(0,message); /* put into output message queue for sending to the agent*/
	} else {           /* Here we exceeded the number of the allowed subscriptions*/
	  new_sub->thread_stat = 0;
	  free(new_sub);
	  free(&obuf);
	  sMutexUnlock((mySubscriptions->mutex));
	  return(ERROR_NO_MORE_SUBSCRIPTIONS);
	}
      }  
      
    } else {/* Message queue is not started ok*/
      sMutexUnlock((mySubscriptions->mutex));
      new_sub->thread_stat = 0;
      free(new_sub);
      return(ERROR_STARTING_QUEUE);
    }
    sMutexUnlock((mySubscriptions->mutex));
    return(CMSG_OK);
  } 

  /** This is a function running in the separate thread, where single user callback is executed.
   *
   * @param arg is the pointer to the the usera_t structure including the 
   *            cMsgtMessage and callback_t structures. 
   *
   ********************************************************************************/
  void *usrHandlThread(void *arg){
    
    cMsgMessage_t *cmsg;
    callback_t *clb;
    usera_t *myargs;
    
    pthread_t thd;
    
    /* increase concurrency for this thread for early Solaris */
    int  con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);
    
    /* release system resources when thread finishes */
    pthread_detach(pthread_self());
    
    /* cast the arg to the usera_t structure*/
    myargs = (usera_t *)arg;
    
    /* get the cMsg message*/
    cmsg = myargs->usrmsg;
    
    /* get the user registered callback parameters*/
    clb = myargs->usrcallb;
    
    /* start the user callback function */
    clb->callback(cmsg,clb->userArg);
    
  } 
  
  /** 
   * This function will execute the user callback function in the separate thread,
   * (executing the userHandlerThread function), 
   * after it will get messages from the subscriptions message queue.
   *
   * @param sub_t subscription structure
   ***********************************************************/
  void *subsHandler(void *arg){
    
    pthread_t pt; 
    int i, status;
    sub_t *mySubscription;
    int ret;
    /** pointer to the agent message structure*/
    void *msgptr;
    /** Message queue priority.*/
    unsigned int msg_prio;
    /** pointer to the cMsg structure*/
    cMsgMessage_t *outcMsg;
    /**Structure which is passed as an argument to the user callback handling thread */  
    usera_t *userArguments;

    /* increase concurrency for this thread for early Solaris */
    int  con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);
    
    /* release system resources when thread finishes */
    pthread_detach(pthread_self());

    /* cast the arg to the sub_t subscription structure*/
    mySubscription = (sub_t *)arg;
    
    /* alocate the memory for the message. We assume that all the messages 
     * are set to have AGENT_MESSAGE_SIZE*/
    msgptr = calloc(1,AGENT_MESSAGE_SIZE);
    
    while(1) {
      /* See if we need to exit this thread ( user unsubscribed )*/
      if(mySubscription->thread_stat == 0) break;
      /* See  if there is a message in the queue*/   
      /* Get the message from the queue*/
      ret = mq_receive(mySubscription->messageQ, msgptr, AGENT_MESSAGE_SIZE, &msg_prio);
      
      if(ret != EAGAIN) {                                           /* In case there is a message in the queue*/ 
	Wire2cMsg((msg *)msgptr, outcMsg);                          /* Translate between wire and cMsg protocole*/
        /* Check all registered callbacks for this subject and type message queue*/
	for (i = 0; i<=MAX_CALLBACKS; i++) { /*Loop over all registered subscriptions*/ 
	  
	  if( mySubscription->callback_list[i]->callback != NULL){     /* in case callback is not NULL */
	    
	    /* start the callback in the sequential mode*/
	    //mySubscription->callback_list[i]->callback(outcMsg,mySubscription->callback_list[i]->userArg); 
	    
	    /* fill  the usera_t structure*/
	    userArguments->usrmsg = outcMsg;
	    userArguments->usrcallb = mySubscription->callback_list[i];
	    /* start user callback handling thread*/
	    status = pthread_create(&pt, NULL, usrHandlThread, (void *)userArguments);
	    if (status != 0) {
	      err_abort(status, "Creating user callback handling thread");
	    }
	  }
	}
      }
      else {                                                        /* The queue is empty. */ 
	free(msgptr);
	sleep(SLEEP_TIME);	                                    /* Sleep for a while*/
      }
    }
    free(mySubscription);
  }
  
  /**
   * This routine unsubscribes to messages of the given subject, type,
   * callback, and user argument. This routine is called by the user through
   * cMsgUnSubscribe().
   *
   * @param domainId id number = 0 for RC domain
   * @param subject subject of messages to unsubscribed from
   * @param type type of messages to unsubscribed from
   * @param callback pointer to callback to be removed
   * @param userArg user-specified pointer to be passed to the callback
   *
   * @returns CMSG_OK if successful
   *
   **********************************************************************************/   
  static int rcUnsubscribe(int domainId, const char *subject, const char *type, cMsgCallbackFunc *callback,
                           void *userArg) {
    int i,j;
    /** single subscription structure*/
    sub_t *subscription, *new_sub;  
    /** message queue attribute information*/
    struct mq_attr buf;
    /** Output queue flags. O_NONBLOCK flag will 
     *  force error=EAGAIN in case the queue is empty. */
    int oflags = O_RDWR|O_CREAT|O_NONBLOCK;
    /** Message queue descriptor.*/
    mqd_t mymqd;
    /** Output message queue permissions*/
    int perms = 0777;
    /** Name of the queue*/
    char *Qname;
    /** cMsg structure*/
    cMsgMessage_t *message;
    
    
    /* increase concurrency for this thread for early Solaris */
    int  con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);

    /* release system resouces when thread finishes */
    pthread_detach(pthread_self());
    /* See if the subscription already exists*/
    sMutexLock((mySubscriptions->mutex)); /* mutex lock */    
    for (i = 0; i<=mySubscriptions->queueCount; i++) { /*Loop over all registered subscriptions*/
      /* See if the queue for this subject and type exists*/
      if((strcmp(mySubscriptions->subs_list[i]->subject,subject)==0) && 
	 (strcmp(mySubscriptions->subs_list[i]->type, type)==0)){
	
	/* subject-type exists, see if we have callback registered*/
	for (j = 0; j<=mySubscriptions->subs_list[i]->callbackCount; j++) { /*Loop over all registered subscriptions*/ 
	  if ((mySubscriptions->subs_list[j]->callback_list[j]->callback == callback) && 
	      (mySubscriptions->subs_list[j]->callback_list[j]->userArg == userArg)){ /* Yes, it is registered*/
	    
	    /* free the subs_t*/
	    free(mySubscriptions->subs_list[j]);
	    mySubscriptions->subs_list[j]->callbackCount -= 1;
	    
	    /* If callback count is -1 , no more callbacks registered, remove the queue*/
	    if( mySubscriptions->subs_list[j]->callbackCount <0 ) {
	      if( mq_unlink(mySubscriptions->subs_list[j]->mqd_name) == 0){ /* remove the mesage queu */
		mySubscriptions->queueCount -= 1;
	      } else { /* error removing the queue*/
		sMutexUnlock((mySubscriptions->mutex));
		return(ERROR_REMOVING_MQ);
	      }		   
	    }
	    goto msgsend;
	  }
	}
      }
    }
    sMutexUnlock((mySubscriptions->mutex));
    return(ERROR_NOT_SUBSCRIBED);
    
  msgsend:
    sMutexUnlock((mySubscriptions->mutex)); /* mutex unlock */    
    /* Send a unsubscribe message to the agent */
    /* Create cMsgMessage */
    message = (cMsgMessage_t *) cMsgCreateMessage();
    cMsgSetUserInt(message, UNSUBSCRIBE);
    cMsgSetSubject(message, subject);
    cMsgSetType(message, type);
    /* put into output message queue for sending to the agent*/
    if(rcSend(0,message) !=CMSG_OK) return(ERROR_UNSUBSCRIBE); 
    else return(CMSG_OK);
  }
  
  
  /** Function listening the messages comming from this physical component private agent.
   * It decodes the subject type and puts the message into the appropriate message queue.
   * 
   * @param arg netinfo structure, including port number and host name of this client. 
   *           We need to open the same port which was already reported to the agent platfo rm.
   ******************************************************************************************/
  void *listen2Agent(void *arg){
    
    int i;
    /** ld listening socket descriptor*/    
    int ld,sd;                       
    struct sockaddr_in skaddr;
    struct sockaddr_in from;
    int addrlen,length;
    /** listening port number */
    int lport;                       
    /** hostname */
    char lhost[MAX_HOSTNAME_LENGTH]; 
    int n;
    /** Number of the integer fields of the wire protocole mesage */
    int InComing[11];                
    /** pointer to the rcAgentDomain protocole message object*/  
    msg *myMsg;                      
    /** Network info of the physical client, i.e. listening port and host name*/
    netinfo_t *myNetInfo;        
    /**Pointer to the synchronized  state_t structure*/
    state_t *myState;
    /** Pointer to the synchronized subscriptions_t structure*/
    subscriptions_t *mySubscriptions;
    /** Pointer to one subscription object from the subscriptions list array */
    sub_t *subscription;          
    
    /* increase concurrency for this thread for early Solaris */
    int  con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);

    /* release system resouces when thread finishes */
    pthread_detach(pthread_self());
    /* cast the arg to netinfo structure*/
    myNetInfo = (netinfo_t *)arg;
    /* create a listening socket */
    ld = np_socket( PF_INET, SOCK_STREAM, 0 );
    /* establish our address, address family is AF_INET,
       our IP address is INADDR_ANY (any of our IP addresses)
       the port number is assigned by the kernel.
    */
    skaddr.sin_family = AF_INET;
    skaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    skaddr.sin_port = htons(myNetInfo->port);
    
    np_bind(ld, (struct sockaddr *) &skaddr, sizeof(skaddr));
    
    /* put the socket into passive mode (waiting for connections) */
    if (listen(ld,5) < 0 ) {
      perror("Error calling listen\n");
      exit(ERROR_NETWORK_LISTENING);
    }
    
    /* now process incoming connections from the private agent for ever.. */
    while (1) {
      /* first we check the synchronized state_t structure and see if this thread will stop or not.
	 for that lock the structure */
      sMutexLock((myState->mutex));      
      if(myState->listen_thr = 0)exit(-1); /* We exit now*/
      sMutexUnlock((myState->mutex));      
      printf("Ready for a connection...\n");
      addrlen=sizeof(skaddr);
      if ( (sd = accept( ld, (struct sockaddr*) &from, &addrlen)) < 0) {
	perror("Problem with accept call\n");
	exit(ERROR_NETWORK_ACCEPT);
      }
      printf("Got a connection - processing...\n");
      
      /*< This section is for debugging purposes */
      
      /* Determine and print out the address of the new server socket */
      length = sizeof( skaddr );
      if (getsockname(sd, (struct sockaddr *) &skaddr, &length)<0) {
	perror("Error getsockname\n");
	exit(ERROR_READING_SOCKET_INFO);
      }
      printf("My server (listening) port number is %d\n",ntohs(skaddr.sin_port));
      printf("My server (listening)IP ADDRESS is %s\n",inet_ntoa(skaddr.sin_addr));
      /* print out the address of the cMsgServerAgent. */
      printf("My cMsgClientAgent port number is %d\n",ntohs(from.sin_port));
      printf("My cMsgClientAgent IP ADDRESS is %s\n",inet_ntoa(from.sin_addr));
      
      /*< End of the debugging section */
      
      /* Reading the header integers of the message from the cMsgClientAgent.*/
      if( n=np_read(sd, (void*)InComing, sizeof(InComing)) <0) {
	perror("Problem reading the header of the incoming message.");
	
	/* Close the connection*/
	close(sd);
	exit(ERROR_READING_MESSAGE);
      }
      
      /* we need to swap back to the local endian from the network */
      myMsg->version               = ntohl(InComing[0]);
      myMsg->priority              = ntohl(InComing[1]);
      myMsg->usrInt                = ntohl(InComing[2]);
      myMsg->senderTime            = ntohl(InComing[3]);
      myMsg->msgId                 = ntohl(InComing[4]);
      myMsg->senderPort            = ntohl(InComing[5]);
      myMsg->senderHostLength      = ntohl(InComing[6]);
      myMsg->senderLength          = ntohl(InComing[7]);
      myMsg->subjectLength         = ntohl(InComing[8]);
      myMsg->typeLength            = ntohl(InComing[9]);
      myMsg->contentLength         = ntohl(InComing[10]);
      
      /* Now read the sender host name */
      if( n=np_readString(sd, (char *) myMsg->senderHost, myMsg->senderHostLength) < 0) {
	perror("Problem reading the sender of the incoming message.");
	//exit(ERROR_READING_MESSAGE);
      }
      
      /* Now read the sender of the message*/
      if( n=np_readString(sd, (char *) myMsg->sender, myMsg->senderLength) < 0) {
	free((void *)myMsg->sender);
	perror("Problem reading the sender of the incoming message.");
	//exit(ERROR_READING_MESSAGE);
      }
      
      /* Now read the subject of the message*/
      if( n=np_readString(sd, (char *) myMsg->subject, myMsg->subjectLength) < 0) {
	free((void *)myMsg->sender);
	free((void *)myMsg->subject);
	perror("Problem reading the subject of the incoming message.");
	//exit(ERROR_READING_MESSAGE);
      }
      
      /* Now read the type of the message*/
      if( n=np_readString(sd, (char *) myMsg->type, myMsg->typeLength) < 0) {
	free((void *)myMsg->sender);
	free((void *)myMsg->subject);
	free((void *)myMsg->type);
	perror("Problem reading the type of the incoming message.");
	//return(ERROR_READING_MESSAGE);
      }
      
      sMutexLock((mySubscriptions->mutex));      
      /* Loop over all registered subscriptions*/ 
      for (i = 0; i<=MAX_SUBSCRIPTIONS; i++) {
	subscription = mySubscriptions->subs_list[i];
	/* See if user unsubscribed this message, i.e. handling thread is stoped.*/
	if(subscription->thread_stat !=0 ) {
	  if((strcmp(subscription->subject,myMsg->subject)==0) && (strcmp(subscription->type, myMsg->type)==0)){
	    
	    /* put the message into the queue*/
	    if(mq_send(subscription->messageQ,(void *)myMsg,sizeof(myMsg),0) >= 0)
	      printf("Message added to the queue successfully\n");
	    else
	      perror("Error: can not add the message to the queue\n");
	    sMutexUnlock((mySubscriptions->mutex));      
	  }
	}
      }
      sMutexUnlock((mySubscriptions->mutex));      
      printf("Done with connection to the cMsgServerAgent- closing\n");
      close(sd);
    }
  }
  
  
  /** Function creates the output queue and starts accepting messages from the queue 
   *  and sending them to the private agent. 
   * 
   * @param arg pointer to the wire message protocole.
   *           
   *********************************************************************************/
  void *send2Agent(void *arg){
    
    /** socket file discriptor*/
    int sk;       
    /** returned error from the output queueu receive operation*/
    int ret;
    /** Output message (wire protocol) structure*/
    msg *outMsg;
    /** pointer to the cMsg message */
    void *msgptr;
    /** Message queue priority.*/
    unsigned int msg_prio;
    /** message queue attribute information*/
    struct mq_attr obuf;
    /** Output queue flags. The combination of O_CREAT_O_EXCL taks 
     *  care of the queue name conflict in case there is 
     *  already open queue with the same name. O_NONBLOCK flag will 
     *  force error=EAGAIN in case the queue is empty. */
    int oflags = O_RDWR|O_CREAT|O_EXCL|O_NONBLOCK;
    /** Message queue descriptor.*/
    mqd_t outQ;
    /** Output message queue permissions*/
    int perms = 0777;
    /** Name of the queue*/
    char *Qname;
    
    /* increase concurrency for this thread for early Solaris */
    int  con;
    con = sun_getconcurrency();
    sun_setconcurrency(con + 1);


    /* Set the name of the queue.*/
    Qname = "OutputQ";
    
    /* release system resouces when thread finishes */
    pthread_detach(pthread_self());
    
    /* cast the arg to msg structure*/
    outMsg = (msg *)arg;
    
    /* Open the socket to the Agent*/
    sk = connect2Platform(outMsg->senderPort, outMsg->senderHost);
    
    /* Create  outQ output queue. Here we set following flags O_RDWR|O_CREAT|0_EXCL|O_NONBLOCK */
    outQ = mq_open(Qname,oflags,perms,&obuf);
    if(-1 != (int)outQ){
      /* set output queue message max size and max number of the messages.*/
      obuf.mq_msgsize = MQ_OUT_MSGSIZE;
      obuf.mq_maxmsg = MQ_OUT_MAXMSG;
      /* We successfully created output queue set the descriptor inside the global structure.*/
      sMutexLock((myMsgOutQ->mutex));      
      myMsgOutQ->mqd = outQ;
      sMutexUnlock((myMsgOutQ->mutex));      
      
      /* Error opening the ouptut message queue*/
    } else perror("Error opening the ouptut message queue.");
    
    /* loop for ever */
    while(1) {
      
      /* alocate the memory for the message. We assume that all the messages 
       * are set to have CMAG_MAX_SIZE*/
      msgptr = calloc(1,CMSG_MAX_SIZE);
      
      /* Get the message from the queue*/
      sMutexLock((myMsgOutQ->mutex));      
      ret = mq_receive(myMsgOutQ->mqd, msgptr, CMSG_MAX_SIZE, &msg_prio);
      sMutexUnlock((myMsgOutQ->mutex));      
      if(ret != EAGAIN) {           /* In case there is a message in the queue*/ 
	cMsg2Wire((cMsgMessage_t *)msgptr, outMsg);  /* Translate between cMsg to wire protocole*/
	/* Send the message*/
	if (transmit2Agent(sk, outMsg) == ERROR_TRANSMITING_TO_AGENT) exit(-1); 
	free(msgptr);               /* free the memory*/
      }
      else {                        /* The queue is empty. */ 
	free(msgptr);
	sleep(SLEEP_TIME);	    /* Sleep for a while*/
      }
    }
  } 
  
  
  /**
   * This routine locks the pthread mutex used to serialize
   * codaSubscribe and codaUnsubscribe calls.
   ****************************************************/
  static void sMutexLock(pthread_mutex_t mx) {
    
    int status;
    status = pthread_mutex_lock(&(mx));
    if (status != 0) {
      err_abort(status, "Failed mutex lock");
    }
  }
  
  
  /**
   * This routine unlocks the pthread mutex used to serialize
   * codaSubscribe and codaUnsubscribe calls.
   *********************************************************/
  static void sMutexUnlock(pthread_mutex_t mx) {
    
    int status;
    
    status = pthread_mutex_unlock(&(mx));
    if (status != 0) {
      err_abort(status, "Failed mutex unlock");
    }
  }

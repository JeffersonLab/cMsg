/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 8-Jul-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#include "cMsg.h"
#include "cMsgDomain.h"

int count = 0, oldInt=-1;
void *domainId;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


/******************************************************************/
static void callback(void *msg, void *arg) {
    const char *sub, *type;
    int userInt;
    
    cMsgGetSubject(msg, &sub);
    cMsgGetType(msg, &type);
    cMsgGetUserInt(msg, &userInt);
    printf("Got msg with sub = %s, typ = %s, msg # = %d\n", sub, type, userInt);
    cMsgPayloadPrint(msg);

    cMsgFreeMessage(&msg);
}



/******************************************************************/
static void sAndgCallback(void *msg, void *arg) {
    const char *sub, *type;
    int getRequest=0;
    void *rmsg;

    cMsgGetGetRequest(msg, &getRequest);
    if (!getRequest) {
        printf("Callback received non-sendAndGet msg - ignoring\n");
        return;
    }
    
    cMsgGetSubject(msg, &sub);
    cMsgGetType(msg, &type);
    printf("Callback received sendAndGet msg (%s, %s) - responding\n", sub, type);
    rmsg = cMsgCreateResponseMessage(msg);
    cMsgAddString(rmsg, "payloadItem", "any string you want");
    cMsgSetSubject(rmsg, "RESPONDING");
    cMsgSetType(rmsg, "TO MESSAGE");
    cMsgSetText(rmsg, "responder's text");
    cMsgSend(domainId, rmsg);

    cMsgFreeMessage(&rmsg);
    cMsgFreeMessage(&msg);
}



/******************************************************************/
int main(int argc,char **argv) {

  char *myName   = "C rc client";
  char *myDescription = "rc trial";
  
 /* RC domain UDL is of the form:
  *        cMsg:rc://<host>:<port>/<expid>?multicastTO=<timeout>&connectTO=<timeout>
  *
  *
  * Remember that for this domain:
  *<ul>
  *<li>1) host is required and may also be "multicast", "localhost", or in dotted decimal form<p>
  *<li>2) port is optional with a default of {@link RC_MULTICAST_PORT}<p>
  *<li>3) the experiment id or expid is required, it is NOT taken from the environmental variable EXPID<p>
  *<li>4) multicastTO is the time to wait in seconds before connect returns a
  *       timeout when a rc multicast server does not answer<p>
  *<li>5) connectTO is the time to wait in seconds before connect returns a
  *       timeout while waiting for the rc server to send a special (tcp)
  *       concluding connect message<p>
  *</ul><p>
  */
  char *UDL = "cMsg:rc://multicast/emutest&multicastTO=5&connectTO=5";

  int   err, debug = 1;
  cMsgSubscribeConfig *config;
  void *subHandle1, *subHandle2, *msg;
  int  loops = 5;
  

  if (argc > 1) {
    myName = argv[1];
  }
  
  if (argc > 2) {
    UDL = argv[2];
  }
  
  if (debug) {
    printf("Running the cMsg client, \"%s\"\n", myName);
    printf("  connecting to, %s\n", UDL);
  }

  /* connect to cMsg server */
  err = cMsgConnect(UDL, myName, myDescription, &domainId);
  if (err != CMSG_OK) {
      if (debug) {
          printf("cMsgConnect: %s\n",cMsgPerror(err));
      }
      exit(1);
  }
  
  /* start receiving messages */
  cMsgReceiveStart(domainId);

  /* set debug level */
  cMsgSetDebugLevel(CMSG_DEBUG_NONE);

  /* set the subscribe configuration */
  config = cMsgSubscribeConfigCreate();

  /* subscribe to subject/type to receive from RC Server send */
  err = cMsgSubscribe(domainId, "rcSubject", "rcType", callback, NULL, config, &subHandle1);
  if (err != CMSG_OK) {
      if (debug) {
          printf("cMsgSubscribe: %s\n",cMsgPerror(err));
      }
      exit(1);
  }
  
  /* subscribe to subject/type to receive from RC Server sendAndGet */
  err = cMsgSubscribe(domainId, "sAndGSubject", "sAndGType", sAndgCallback, NULL, config,
                      &subHandle2);
  if (err != CMSG_OK) {
      if (debug) {
          printf("cMsgSubscribe: %s\n",cMsgPerror(err));
      }
      exit(1);
  }
  
  cMsgSubscribeConfigDestroy(config);

  sleep(1);
  
  /* send stuff to RC Server */
  msg = cMsgCreateMessage();
  cMsgSetSubject(msg, "subby");
  cMsgSetType(msg, "typey");
  cMsgSetText(msg, "Send with TCP");
  cMsgSetReliableSend(msg, 1);
  /* Create Compound Payload */
  cMsgAddString(msg,"severity","really severe");
   
  printf("Send subby, typey with TCP\n");
  err = cMsgSend(domainId, msg);
  if (err != CMSG_OK) {
      printf("ERROR in sending message!!\n");
      exit(-1);
  }

  cMsgSetText(msg, "Send with UDP");
  cMsgSetReliableSend(msg, 0);
  printf("Send subby, typey with UDP\n");
  err = cMsgSend(domainId, msg);
  if (err != CMSG_OK) {
      printf("ERROR in sending message!!\n");
      exit(-1);
  }

  printf("Sleep for 4 sec\n");
  sleep(4);
     
  cMsgReceiveStop(domainId);
  cMsgUnSubscribe(domainId, subHandle1);
  cMsgUnSubscribe(domainId, subHandle2);
  cMsgDisconnect(&domainId);
  
  cMsgFreeMessage(&msg);

  return(0);
}

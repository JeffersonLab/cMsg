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
#include "errors.h"

int count = 0, oldInt=-1;
void *domainId;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


/******************************************************************/
static void callback(void *msg, void *arg) {
  int status, userInt;
  struct timespec sleeep;
  
  sleeep.tv_sec  = 0;
  sleeep.tv_nsec = 10000000; /* 10 millisec */
  sleeep.tv_sec  = 1;
  sleeep.tv_nsec = 0;
  
  
  status = pthread_mutex_lock(&mutex);
  if (status != 0) {
    err_abort(status, "error grabbing mutex");
  }
  
  count++;
  /*printf("Running callback, count = %d\n", count);*/
  
  /*nanosleep(&sleeep, NULL);*/
  
  /*
  cMsgGetUserInt(msg, &userInt);
  if (userInt != oldInt+1)
    printf("%d -> %d; ", oldInt, userInt);
  
  oldInt = userInt;
  */  
  status = pthread_mutex_unlock(&mutex);
  if (status != 0) {
    err_abort(status, "error releasing mutex");
  }
  
  /* user MUST free messages passed to the callback */
  cMsgFreeMessage(&msg);
}



/******************************************************************/
int main(int argc,char **argv) {

  char *myName   = "Coda component name";
  char *myDescription = "RC test";
  char *subject = "rcSubject";
  char *type    = "rcType";
  
    /* RC domain UDL is of the form:
     *        cMsg:rc://<host>:<port>/?expid=<expid>&broadcastTO=<timeout>&connectTO=<timeout>
     *
     * Remember that for this domain:
     * 1) port is optional with a default of RC_BROADCAST_PORT (6543)
     * 2) host is optional with a default of 255.255.255.255 (broadcast)
     *    and may be "localhost" or in dotted decimal form
     * 3) the experiment id or expid is optional, it is taken from the
     *    environmental variable EXPID
     * 4) broadcastTO is the time to wait in seconds before connect returns a
     *    timeout when a rc broadcast server does not answer 
     * 5) connectTO is the time to wait in seconds before connect returns a
     *    timeout while waiting for the rc server to send a special (tcp)
     *    concluding connect message
     */
  char *UDL     = "cMsg:rc://?expid=carlExp";
  
  int   err, debug = 1;
  cMsgSubscribeConfig *config;
  void *unSubHandle, *msg;
  int toggle = 2, loops = 10;
  
  /* msg rate measuring variables */
  int             period = 2, ignore=0;
  double          freq, freqAvg=0., totalT=0.;
  long long       totalC=0;

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
  
  /* set the subscribe configuration */
  config = cMsgSubscribeConfigCreate();
  cMsgSubscribeSetMaxCueSize(config, 100);
  cMsgSubscribeSetSkipSize(config, 20);
  cMsgSubscribeSetMaySkip(config,0);
  cMsgSubscribeSetMustSerialize(config, 1);
  cMsgSubscribeSetMaxThreads(config, 10);
  cMsgSubscribeSetMessagesPerThread(config, 150);
  cMsgSetDebugLevel(CMSG_DEBUG_ERROR);
  
  /* subscribe */
  err = cMsgSubscribe(domainId, subject, type, callback, NULL, config, &unSubHandle);
  if (err != CMSG_OK) {
      if (debug) {
          printf("cMsgSubscribe: %s\n",cMsgPerror(err));
      }
      exit(1);
  }
  
  msg = cMsgCreateMessage();
  cMsgSetSubject(msg, "subby");
  cMsgSetType(msg, "typey");
    
  while (loops-- > 0) {
      count = 0;
      
      /* send udp msgs to rc server */
      err = cMsgSend(domainId, msg);
      if (err != CMSG_OK) {
          printf("ERROR in sending message!!\n");
          sleep(1);
          continue;
      }
      
      /* wait for messages */
      sleep(period);
      
      /* calculate rate */
      if (!ignore) {
          totalT += period;
          totalC += count;
          freq    = (double)count/period;
          freqAvg = totalC/totalT;
          printf("count = %d, %9.1f Hz, %9.1f Hz Avg.\n", count, freq, freqAvg);
      }
      else {
          ignore--;
      }
      
      /* test unsubscribe */
      /*
      if (toggle++%2 == 0) {
          cMsgUnSubscribe(domainId, unSubHandle);
      }
      else {
          cMsgSubscribe(domainId, subject, type, callback, NULL, config, &unSubHandle);
      }
      */
  }
/*printf("rcClient try disconnect\n");*/
  cMsgDisconnect(&domainId);
/*printf("rcClient done disconnect\n");*/

  return(0);
}

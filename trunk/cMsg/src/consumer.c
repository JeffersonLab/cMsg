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

int count = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static void callback(void *msg, void *arg);


int main(int argc,char **argv) {  

  char *myName   = "C Consumer";
  char *myDescription = "C consumer";
  char *subject = "SUBJECT";
  char *type    = "TYPE";
  char *UDL     = "cMsg:cMsg://aslan:3456/cMsg/test";
  int   err, debug = 1, domainId = -1;
  cMsgSubscribeConfig *config;
  
  /* msg rate measuring variables */
  int             loops=20000;
  int             period = 5; /* sec */
  struct timespec t1, t2;
  double          freq, freqAvg=0., deltaT, totalT=0.;
  long long       totalC=0;
  
  if (argc > 1) {
    myName = argv[1];
  }
  
  if (debug) {
    printf("Running the cMsg consumer, \"%s\"\n", myName);
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
  cMsgSubscribeSetMaxCueSize(config, 10000);
  cMsgSubscribeSetSkipSize(config, 2000);
  cMsgSubscribeSetMaySkip(config, 0);
  cMsgSubscribeSetMustSerialize(config, 1);
  cMsgSubscribeSetMaxThreads(config, 290);
  cMsgSubscribeSetMessagesPerThread(config, 150);
  cMsgSetDebugLevel(CMSG_DEBUG_ERROR);
  
  /* subscribe */
  err = cMsgSubscribe(domainId, subject, type, callback, NULL, config);
  if (err != CMSG_OK) {
      if (debug) {
          printf("cMsgSubscribe: %s\n",cMsgPerror(err));
      }
      exit(1);
  }

  while (1) {
      count = 0;
      
      /* wait for messages */
      sleep(period);
      
      /* calculate rate */
      totalT += period;
      totalC += count;
      freq    = (double)count/period;
      freqAvg = totalC/totalT;
      printf("count = %d, %9.0f Hz, %9.0f Hz Avg.\n", count, freq, freqAvg);
  }

  return(0);
}

static void callback(void *msg, void *arg) {
  
  pthread_mutex_lock(&mutex);
  count++;
  pthread_mutex_unlock(&mutex);
  
  /* user MUST free messages passed to the callback */
  cMsgFreeMessage(msg);
}

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
#ifdef sun
#include <thread.h>
#endif

#include "cMsg.h"

/* recent versions of linux put float.h (and DBL_MAX) in a strange place */
#define DOUBLE_MAX   1.7976931348623157E+308

int count = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void callback(void *msg, void *arg);

int main(int argc,char **argv) {  
  char *myName   = "C-consumer";
  char *myDescription = "trial run";
  int err, domainId = -1;
  cMsgSubscribeConfig *config;
  
  double freq=0., freqAvg=0., freqTotal=0.;
  long   iterations=1;
  
  if (argc > 1) {
    myName = argv[1];
  }
  printf("My name is %s\n", myName);
  
  printf("cMsgConnect ...\n");
  err = cMsgConnect("cMsg:cMsg://aslan:3456/cMsg/vx/", myName, myDescription, &domainId);
  printf("cMsgConnect: %s\n", cMsgPerror(err));
 
  cMsgReceiveStart(domainId);
  
  printf("cMsgSubscribe ...\n");
  config = cMsgSubscribeConfigCreate();
  cMsgSubscribeSetMaxCueSize(config, 10000);
  cMsgSubscribeSetSkipSize(config, 2000);
  cMsgSubscribeSetMaySkip(config, 0);
  cMsgSubscribeSetMustSerialize(config, 1);
  cMsgSubscribeSetMaxThreads(config, 290);
  cMsgSubscribeSetMessagesPerThread(config, 150);
  cMsgSetDebugLevel(CMSG_DEBUG_ERROR);
  err = cMsgSubscribe(domainId, "SUBJECT", "TYPE", callback, NULL, config);
  printf("cMsgSubscribe: %s\n", cMsgPerror(err));

  while (1) {
      count = 0;
      sleep(5);

      freq = (double)count/10.;
      if (DOUBLE_MAX - freqTotal < freq) {
          freqTotal  = 0.;
          iterations = 1;
      }
      freqTotal += freq;
      freqAvg = freqTotal/iterations;
      iterations++;
      printf("count = %d, %9.0f Hz, %9.0f Hz Avg.\n", count, freq, freqAvg);
      fflush(stdout);
  }

  return(0);
}

static void callback(void *msg, void *arg) {
pthread_mutex_lock(&mutex);
  count++;
pthread_mutex_unlock(&mutex);
  cMsgFreeMessage(msg);
}

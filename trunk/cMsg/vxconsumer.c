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
#define NUMGETS 1000

int count = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void callback(void *msg, void *arg);

int cMsgConsumer(void) {  
  char *myName   = "VX-consumer";
  char *myDescription = "trial run";
  int err, domainId = -1;
  cMsgSubscribeConfig *config;
  
  double freq=0., freqAvg=0., freqTotal=0.;
  long   iterations=1;
  
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


int cMsgGetConsumer(void) {
    char *subject = "responder";
    char *type    = "TYPE";
    char *UDL     = "cMsg:cMsg://aslan:3456/cMsg/vx";
    char *myName  = "VX-consumer";
    char *myDescription = "trial run";
    int   i, err, domainId = -1;
    
    double freq=0., freqAvg=0., countTotal=0., timeTotal=0.;
    long   count=0;
    void  *msg, *getMsg;
    struct timespec t1, t2, timeout;
    double time;

    printf("Running Message GET Consumer\n");

    printf("cMsgConnect ...\n");
    err = cMsgConnect(UDL, myName, myDescription, &domainId);
    printf("cMsgConnect: %s\n", cMsgPerror(err));
 
    cMsgReceiveStart(domainId);
    
    msg = cMsgCreateMessage();
    cMsgSetSubject(msg, subject);
    cMsgSetType(msg, type);
    cMsgSetText(msg, "Message 1");
    
    timeout.tv_sec  = 1;
    timeout.tv_nsec = 0;
  
    while (1) {
        count = 0;
        clock_gettime(CLOCK_REALTIME, &t1);

        /* do a bunch of gets */
        for (i=0; i < NUMGETS; i++) {
            err = cMsgSendAndGet(domainId, msg, &timeout, &getMsg);

            if (err == CMSG_TIMEOUT) {
                printf("TIMEOUT in sendAndGet\n");
            }
            else {
                count++;
                if (getMsg != NULL) {
                  cMsgFreeMessage(getMsg);
                }
            }
        }
        
        clock_gettime(CLOCK_REALTIME, &t2);
        time = (double)(t2.tv_sec - t1.tv_sec) + 1.e-9*(t2.tv_nsec - t1.tv_nsec);
        
        freq = count/time;
        if ( ((DOUBLE_MAX - countTotal) < count)  ||
             ((DOUBLE_MAX - timeTotal)  < time ) )  {
          countTotal = 0.;
          timeTotal  = 0.;
        }
        countTotal += count;
        timeTotal  += time;
        freqAvg = countTotal/timeTotal;
        printf("%9.0f Hz,  %9.0f Hz Avg.\n", freq, freqAvg);                    
        t1 = t2;        
    }
}

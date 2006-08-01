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

int count = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void callback(void *msg, void *arg);

int cMsgConsumer(void) {  
  char *myName   = "VX-consumer";
  char *myDescription = "trial run";
  char *subject = "SUBJECT";
  char *type    = "TYPE";
  int   err, loops=5;
  void *domainId;
  cMsgSubscribeConfig *config;
  void *unsubHandle;
  
  /* msg rate measuring variables */
  int             period = 5, ignore=4;
  double          freq, freqAvg=0., totalT=0.;
  long long       totalC=0;
  
  printf("Running cMsg consumer %s\n", myName);
    
  err = cMsgConnect("cMsg:cMsg://aslan:3456/cMsg/test", myName, myDescription, &domainId);
   
  cMsgReceiveStart(domainId);
  
  config = cMsgSubscribeConfigCreate();
  cMsgSubscribeSetMaxCueSize(config, 1000);
  cMsgSubscribeSetSkipSize(config, 200);
  cMsgSubscribeSetMaySkip(config, 0);
  cMsgSubscribeSetMustSerialize(config, 1);
  cMsgSubscribeSetMaxThreads(config, 10);
  cMsgSubscribeSetMessagesPerThread(config, 150);
  cMsgSetDebugLevel(CMSG_DEBUG_ERROR);

  /* subscribe */
  err = cMsgSubscribe(domainId, subject, type, callback, NULL, config, &unsubHandle);
  if (err != CMSG_OK) {
      printf("cMsgSubscribe: %s\n",cMsgPerror(err));
      exit(1);
  }
       
  while (loops-- > 0) {
      count = 0;
      
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
  }

  return(0);
}

static void callback(void *msg, void *arg) {
pthread_mutex_lock(&mutex);
  count++;
pthread_mutex_unlock(&mutex);
  cMsgFreeMessage(&msg);
}


int cMsgGetConsumer(void) {
    /*char *subject = "responder";*/
    char *subject = "SUBJECT";
    char *type    = "TYPE";
    char *UDL     = "cMsg:cMsg://aslan:3456/cMsg/vx";
    char *myName  = "VX-getconsumer";
    char *myDescription = "trial run";
    int   i, err;
    void *domainId, *msg, *getMsg;
    
    /* msg rate measuring variables */
    int             count, loops=1000, ignore=5;
    struct timespec t1, t2, timeout;
    double          freq, freqAvg=0., deltaT, totalT=0;
    long long       totalC=0;

    printf("Running cMsg GET consumer %s\n", myName);

    err = cMsgConnect(UDL, myName, myDescription, &domainId);
 
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
        for (i=0; i < loops; i++) {
            /*err = cMsgSendAndGet(domainId, msg, &timeout, &getMsg);*/
            err = cMsgSubscribeAndGet(domainId, subject, type, &timeout, &getMsg);

            if (err == CMSG_TIMEOUT) {
                printf("TIMEOUT in sendAndGet\n");
            }
            else {
                count++;
                if (getMsg != NULL) {
                  cMsgFreeMessage(&getMsg);
                }
            }
        }
        
        if (!ignore) {
          clock_gettime(CLOCK_REALTIME, &t2);
          deltaT  = (double)(t2.tv_sec - t1.tv_sec) + 1.e-9*(t2.tv_nsec - t1.tv_nsec);
          totalT += deltaT;
          totalC += count;
          freq    = count/deltaT;
          freqAvg = (double)totalC/totalT;

          printf("count = %d, %9.1f Hz, %9.1f Hz Avg.\n", count, freq, freqAvg);
        }
        else {
          ignore--;
        } 
   }
}

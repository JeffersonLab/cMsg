/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 3-Mar-2005, Jefferson Lab                                    *
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
#include <math.h>

#include "cMsg.h"

int count = 0, domainId=-1;
void mycallback(void *msg, void *arg);

/************************************/
/*           Globals.               */
/************************************/
double secondsPerLoop = 5.e-7;

/******************************************************************/
void timeDelay(int usec) {
  int i, loops;
  double u;
  
  loops = usec*1.e-6/secondsPerLoop;
  /* printf("timeDelay: %d loops for %d usec\n",loops, usec); */
    
  for (i=0; i < loops; i++) {
    u = sqrt(pow((double)i, 2.));
  }
  return;
}


/******************************************************************/
void calibrateDelay() {
  int i, loops = 5000000;
  double u, time;
  struct timespec t1, t2;
  
  clock_gettime(CLOCK_REALTIME, &t1);
  for (i=0; i < loops; i++) {
    u = sqrt(pow((double)i, 2.));
  }
  clock_gettime(CLOCK_REALTIME, &t2);
  time = (double)(t2.tv_sec - t1.tv_sec) + 1.e-9*(t2.tv_nsec - t1.tv_nsec);
  secondsPerLoop = time/loops;
  printf("calibrateDelay: %e seconds/loop, time of measurement = %4.2f sec\n",
		secondsPerLoop, time);
  return;
}


/******************************************************************/
int main(int argc,char **argv) {  

  char *myName   = "C getResponder";
  char *myDescription = "C getresponder";
  char *subject = "SUBJECT";
  char *type    = "TYPE";
  char *UDL     = "cMsg:cMsg://aslan:3456/cMsg/test";
  int   err, debug = 1, loops=0;
  
  /* msg rate measuring variables */
  int             period = 5; /* sec */
  double          freq, freqAvg=0., totalT=0.;
  long long       totalC=0;
  
  if (argc > 1) {
    myName = argv[1];
  }
  
  if (debug) {
    printf("Running the cMsg C getResponsder, \"%s\"\n", myName);
  }
  
  /*calibrateDelay();*/
  
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
  cMsgSetDebugLevel(CMSG_DEBUG_ERROR);
    
  /* subscribe with default configuration */
  err = cMsgSubscribe(domainId, subject, type, mycallback, NULL, NULL);
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
      if (++loops > 99) break;
  }

  return(0);
}


 
void mycallback(void *msg, void *arg) {
    struct timespec wait;
    /* Create a response to the incoming message */
    void *sendMsg = cMsgCreateResponseMessage(msg);
    /*
     * If we've run out of memory, msg is NULL, or
     * msg was not sent from a sendAndGet() call,
     * sendMsg will be NULL.
     */
    if (sendMsg == NULL) {
        /* user MUST free messages passed to the callback */
        cMsgFreeMessage(msg);
        return;
    }
    
    cMsgSetSubject(sendMsg, "RESPONDING");
    cMsgSetType(sendMsg,"TO MESSAGE");
    cMsgSetText(sendMsg,"responder's text");
    
    /*timeDelay(10);*/
    /*
    wait.tv_sec  = 0;
    wait.tv_nsec = 1000000;
    nanosleep(&wait, NULL);
    */
/*printf("SEND A MESSAGE: subject = RESPONDNG, type = TO MESSAGE\n");*/
    cMsgSend(domainId, sendMsg);
    cMsgFlush(domainId);
    
    /*
     * By default, subscriptions run callbacks serially. 
     * If that is not the case, global data (like "count")
     * must be mutex protected.
     */
    count++;
    
    /* user MUST free messages passed to the callback */
    cMsgFreeMessage(msg);
    /* user MUST free messages created in this callback */
    cMsgFreeMessage(sendMsg);
}

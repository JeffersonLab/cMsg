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

#define NUMEVENTS 10000
/* recent versions of linux put float.h (and DBL_MAX) in a strange place */
#define DOUBLE_MAX   1.7976931348623157E+308

int count = 0;


int main(int argc,char **argv) {  
  char *myName   = "C-getConsumer";
  char *myDescription = "trial run";
  int   i, err, domainId = -1;
  void *msg, *replyMsg;
  
  double freq=0., freqAvg=0., freqTotal=0.;
  long   iterations=1;
  
  double time;
  struct timespec timeout, t1, t2;
  
  timeout.tv_sec  = 1;
  timeout.tv_nsec = 0;
    
  if (argc > 1) {
    myName = argv[1];
  }
  printf("My name is %s\n", myName);
  
  printf("cMsgConnect ...\n");
  err = cMsgConnect("cMsg:cMsg://aslan:3456/cMsg/l45!@#$%/88//", myName, myDescription, &domainId);
  printf("cMsgConnect: %s\n", cMsgPerror(err));
  
  cMsgReceiveStart(domainId);
    
  printf("cMsgCreateMessage ...\n");
  msg = cMsgCreateMessage();
  cMsgSetSubject(msg, "responder");
  /*cMsgSetSubject(msg, "SUBJECT");*/
  cMsgSetType(msg, "TYPE");
  cMsgSetText(msg, "TEXT");
  
  while (1) {
      clock_gettime(CLOCK_REALTIME, &t1);

      /* do a bunch of gets */
      for (i=0; i < NUMEVENTS; i++) {
          /*cMsgSubscribeAndGet(domainId, "SUBJECT", "TYPE", &timeout, &replyMsg);*/
          cMsgSendAndGet(domainId, msg, &timeout, &replyMsg);
          if (replyMsg == NULL) {
              printf("TIMEOUT in GET\n");
          }
          else {
              /*
              printf("GOT A MESSAGE: subject = %s, type = %s\n",
                      cMsgGetSubject(replyMsg), cMsgGetType(replyMsg));
              */
              cMsgFreeMessage(replyMsg);
              count++;
          }
      }


      if (count >= NUMEVENTS) {
        clock_gettime(CLOCK_REALTIME, &t2);
        time = (double)(t2.tv_sec - t1.tv_sec) + 1.e-9*(t2.tv_nsec - t1.tv_nsec);
        freq = (double)count/time;
        if (DOUBLE_MAX - freqTotal < freq) {
            freqTotal  = 0.;
            iterations = 1;
        }
        freqTotal += freq;
        freqAvg = freqTotal/iterations;
        iterations++;
        printf("count = %d, %9.0f Hz, %9.0f Hz Avg.\n", count, freq, freqAvg);
        count = 0;
        t1 = t2;
      }
  }

  return(0);
}

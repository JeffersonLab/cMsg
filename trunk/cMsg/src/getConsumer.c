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
#include <sys/time.h>
#include <sys/resource.h>
#include <pthread.h>

#include "cMsg.h"


int main(int argc,char **argv) {  

  char *myName = "C getConsumer";
  char *myDescription = "C getConsumer";
  char *subject = "SUBJECT";
  char *type    = "TYPE";
  char *text    = "TEXT";
  char *UDL     = "cMsg:cMsg://aslan:3456/cMsg/test";
  int   err, debug=1, domainId = -1, textSize;
  void *msg, *replyMsg;
  
  /* msg rate measuring variables */
  int             count, i, loops=5000;
  struct timespec timeout, t1, t2;
  double          freq, freqAvg=0., deltaT, totalT=0.;
  long long       totalC=0;

/* 
#if defined (_POSIX_THREAD_PRIORITY_SCHEDULING)    
    pthread_attr_t thread_attr;
        
    printf("Hey this operating system support POSIX priority scheduling\n");
    err = pthread_attr_init(&thread_attr);
    if (err != 0) printf("Cannot init thread attribute\n");
    err = pthread_attr_setschedpolicy(&thread_attr, SCHED_FIFO);
    if (err != 0) printf("Cannot set to round-robin scheduling\n");
    setpriority(PRIO_PROCESS, getpid(), 0);
    
    printf("pid = %d\n", getpid());
#endif
*/

  /* maximum time to wait for sendAndGet to return */
  timeout.tv_sec  = 3;
  timeout.tv_nsec = 0;    
  
  
  /*
  if (argc > 1) {
    myName = argv[1];
  }
  */
  if (argc > 1) {
    char *p;
    textSize = atoi(argv[1]);
    text = p = (char *) malloc((size_t) (textSize + 1));
    if (p == NULL) exit(1);
    printf("using text size %d\n", textSize);
    for (i=0; i < textSize; i++) {
      *p = 'A';
      p++;
    }
    *p = '\0'; /* string's ending null */
  }
  
  /*printf("Text = %s\n", text);*/
  
  if (debug) {
    printf("Running the cMsg C getConsumer, \"%s\"\n", myName);
    cMsgSetDebugLevel(CMSG_DEBUG_ERROR);
  }

  /* connect to cMsg server */
  err = cMsgConnect(UDL, myName, myDescription, &domainId);
  if (err != CMSG_OK) {
      if (debug) {
          printf("cMsgConnect: %s\n",cMsgPerror(err));
      }
      exit(1);
  }
  
  /* create message to be sent */
  msg = cMsgCreateMessage();
  cMsgSetSubject(msg, subject);
  cMsgSetType(msg, type);
  cMsgSetText(msg, text);
    
  /* start receiving response messages */
  cMsgReceiveStart(domainId);

  while (1) {
      count = 0;
      
      /* read time for rate calculations */
      clock_gettime(CLOCK_REALTIME, &t1);

      /* do a bunch of sendAndGets */
      for (i=0; i < loops; i++) {
          
          /* send msg and wait up to timeout for reply */
          err = cMsgSendAndGet(domainId, msg, &timeout, &replyMsg);
          /*err = cMsgSubscribeAndGet(domainId, subject, type, &timeout, &replyMsg);*/
          if (err == CMSG_TIMEOUT) {
              printf("TIMEOUT in GET\n");
          }
          
          else if (err != CMSG_OK) {
              printf("cMsgSendAndGet: %s\n",cMsgPerror(err));
              goto end;
          }
          
          else {
              if (debug-1) {
                  char *subject, *type;
                  cMsgGetSubject(replyMsg, &subject);
                  cMsgGetType(replyMsg, &type);
                  printf(" GOT A MESSAGE: subject = %s, type = %s\n", subject, type);
                  /* be sure to free strings */
                  free(subject);
                  free(type);
              }
              cMsgFreeMessage(replyMsg);
              count++;
          }
      }

      /* rate */
      clock_gettime(CLOCK_REALTIME, &t2);
      deltaT  = (double)(t2.tv_sec - t1.tv_sec) + 1.e-9*(t2.tv_nsec - t1.tv_nsec);
      totalT += deltaT;
      totalC += count;
      freq    = count/deltaT;
      freqAvg = (double)totalC/totalT;
      
      printf("count = %d, %9.0f Hz, %9.0f Hz Avg.\n", count, freq, freqAvg);
  } 
  
  end:
  
  err = cMsgDisconnect(domainId);
  if (err != CMSG_OK) {
      if (debug) {
          printf("%s\n",cMsgPerror(err));
      }
  }
    
  return(0);
  
}

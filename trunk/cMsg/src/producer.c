/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 7-Sep-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/
 
#ifdef VXWORKS
#include <vxWorks.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#include "cMsg.h"


int main(int argc,char **argv) {  

  char *myName = "C Producer";
  char *myDescription = "C producer";
  char *subject = "SUBJECT";
  char *type    = "TYPE";
  char *text    = "TEXT";
  char *UDL     = "cMsg:cMsg://aslan:3456/cMsg/test";
  int   err, debug=1, domainId = -1, textSize, response;
  void *msg;
  
  /* msg rate measuring variables */
  int             count, i, delay=0, loops=60000;
  struct timespec t1, t2;
  double          freq, freqAvg=0., deltaT, totalT=0.;
  long long       totalC=0;
  
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
    printf("Running the cMsg producer, \"%s\"\n", myName);
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
  
  if (delay != 0) {
      loops = loops/(2000*delay);      
  }
  
  while (1) {
      count = 0;
      
      /* read time for rate calculations */
      clock_gettime(CLOCK_REALTIME, &t1);

      for (i=0; i < loops; i++) {
          /* send msg */
          /*if (cMsgSyncSend(domainId, msg, &response) != CMSG_OK) {*/
          if (cMsgSend(domainId, msg) != CMSG_OK) {
            printf("cMsgSend: %s\n",cMsgPerror(err));
            fflush(stdout);
            goto end;
          }
          cMsgFlush(domainId);
          count++;
          
          if (delay != 0) {
              sleep(delay);
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

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
#else
#include <strings.h>
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
  char *text    = NULL;
  char *bytes   = NULL;
  char *UDL     = "cMsg:cMsg://phecda:3456/cMsg/test";
  int   err, debug=1, domainId=-1, msgSize=0, mainloops=200;
  void *msg;
  
  /* msg rate measuring variables */
  int             dostring=0, count, i, delay=0, loops=100000, ignore=0;
  struct timespec t1, t2, sleeep;
  double          freq, freqAvg=0., deltaT, totalT=0.;
  long long       totalC=0;
  
  sleeep.tv_sec  = 0;
  sleeep.tv_nsec = 10000000; /* 10 millisec */
  
  
  if (argc > 1) {
     if (strcmp("-s", argv[1]) == 0) {
       dostring = 1;
     }
     else if (strcmp("-b", argv[1]) == 0) {
       dostring = 0;
     }
     else {
       printf("specify -s or -b flag for string or bytearray data\n");
       exit(1);
     }
  }
  
  if (argc > 2) {
    if (dostring) {
      char *p;
      msgSize = atoi(argv[2]);
      text = p = (char *) malloc((size_t) (msgSize + 1));
      if (p == NULL) exit(1);
      printf("using text msg size %d\n", msgSize);
      for (i=0; i < msgSize; i++) {
        *p = 'A';
        p++;
      }
      *p = '\0';
      /*printf("Text = %s\n", text);*/
    }
    else {
      char *p;
      msgSize = atoi(argv[2]);
      bytes = p = (char *) malloc((size_t) msgSize); /* allocating mem here */
      if (p == NULL) exit(1);
      printf("using array msg size %d\n", msgSize);
      for (i=0; i < msgSize; i++) {
        *p = i%255;
        p++;
      }
    }
  }
  else {
      printf("using no text or byte array\n");
      dostring = 1;
  }
  
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
  msg = cMsgCreateMessage();    /* allocating mem here */
  cMsgSetSubject(msg, subject); /* allocating mem here */
  cMsgSetType(msg, type);       /* allocating mem here */
  
  if (dostring) {
    printf("setting text\n");
    cMsgSetText(msg, text);
  }
  else {
    printf("setting byte array\n");
    cMsgSetByteArrayAndLimits(msg, bytes, 0, msgSize);
  }
  
  /*
  if (delay != 0) {
      loops = loops/(2000*delay);      
  }
  */
  
  while (mainloops-- > 0) {
  /*while (1) {*/
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
              nanosleep(&sleeep, NULL);
          }          
      }

      /* rate */
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
  
  end:
  
  cMsgFreeMessage(msg);
  err = cMsgDisconnect(domainId);
  if (err != CMSG_OK) {
      if (debug) {
          printf("%s\n",cMsgPerror(err));
      }
  }
    
  return(0);
}

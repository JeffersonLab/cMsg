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

#define NUMLOOPS 10000
#define DOUBLE_MAX   1.7976931348623157E+308


int cMsgProducer(void) {
  char *myName = "VX-producer";
  char *myDescription = "produce messages as fast as possible";
  int err, domainId = -1;
  void *msg;
  
  /* freq measuring variables */
  int             iterations=1, count=1, i;
  double          freq=0.0, freq_tot=0.0, freq_avg=0.0;
  struct timespec t1, t2;
  double          time;
  
  
  printf("My name is %s\n", myName);
  
  printf("cMsgConnect ...\n");
  err = cMsgConnect("cMsg:cMsg://aslan:3456/cMsg/vx", myName, myDescription, &domainId);
  if (err != CMSG_OK) {
    /* printf("cMsgConnect: %s\n",cMsgPerror(err)); */
    fflush(stdout);
    exit(1);
  }
  /* printf("\n"); */
  
  msg = cMsgCreateMessage();
  cMsgSetSubject(msg, "SUBJECT");
  cMsgSetType(msg, "TYPE");
  cMsgSetText(msg, "Message 1");
  /* printf("\n"); */
  
  while (1) {
      /* read time for future statistics calculations */
      clock_gettime(CLOCK_REALTIME, &t1);

      for (i=0; i < NUMLOOPS; i++) {
          err = cMsgSend(domainId, msg);
          if (err != CMSG_OK) {
            printf("cMsgSend: %s\n",cMsgPerror(err));
            fflush(stdout);
            goto end;
          }
      }

      /* statistics */
      clock_gettime(CLOCK_REALTIME, &t2);
      time = (double)(t2.tv_sec - t1.tv_sec) + 1.e-9*(t2.tv_nsec - t1.tv_nsec);
      freq = (count*NUMLOOPS)/time;
      if ((DOUBLE_MAX - freq_tot) < freq) {
        freq_tot   = 0.0;
	iterations = 1;
      }
      freq_tot += freq;
      freq_avg = freq_tot/(double)iterations;
      iterations++;
      
      printf("%s: %9.0f Hz,  %9.0f Hz Avg.\n", myName, freq, freq_avg);
  } 
  
  end:
  
  printf("cMsgDisconnect ...\n\n\n");
  err = cMsgDisconnect(domainId);
  if (err != CMSG_OK) {
    printf("%s\n",cMsgPerror(err));
    fflush(stdout);
  }
    
  return(0);
}


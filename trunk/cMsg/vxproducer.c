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

#define NUMLOOPS 20000


int cMsgProducer(void) {
  char *myName = "VX-producer";
  char *myDescription = "produce messages as fast as possible";
  int   i, err, domainId = -1;
  void *msg;
    
  err = cMsgConnect("cMsg:cMsg://aslan.jlab.org:3456/cMsg/vx", myName, myDescription, &domainId);
  if (err != CMSG_OK) {
    exit(1);
  }
  
  msg = cMsgCreateMessage();
  cMsgSetSubject(msg, "SUBJECT");
  cMsgSetType(msg, "TYPE");
  cMsgSetText(msg, "Message 1");
  
  while (1) {
      for (i=0; i < NUMLOOPS; i++) {
          err = cMsgSend(domainId, msg);
          if (err != CMSG_OK) {
            goto end;
          }
      }
  } 
  
  end:
 
  cMsgDisconnect(domainId);    
  return(0);
}

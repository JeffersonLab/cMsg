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
#include <string.h>
#include <strings.h>
#include <time.h>
#include <pthread.h>

#include "cMsg.h"
#include "cMsgDomain.h"

int count = 0;
void *domainId;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


/******************************************************************/
static void callback(void *msg, void *arg) {  
  int i, sent, endian, version;
  size_t size;
  int32_t *vals;
  char *p;
  
  count++;
  printf("Print out payload:\n");
  cMsgPayloadPrint(msg);
  
  /*
  cMsgToString(msg, &p, 1);
  printf("XML message:\n%s", p);
  free(p);
  */
/*
printf("\nLook for and print array values:\n");
      if (cMsgGoToFieldName(msg, "intarray")) {
        if (cMsgGetInt32Array(msg, &vals, &size) == CMSG_OK) {
          for (i=0; i < size; i++) {
            printf("%d ", vals[i]);
          }
        }      
      }
*/ 
/* 
  if (cMsgGoToFieldName(msg, "message")) {
   void *msg2;
    
    if (cMsgGetMessage(msg, &msg2) == CMSG_OK) {
      char *buf;
      int i;
      size_t size;
      
      if (msg2 == NULL) printf("MSG2 == NULL !!!\n");
      
      printf("\n2nd level msg binary:\n");     
      
      if (cMsgGoToFieldName(msg2, "my_bin")) {
        if (cMsgGetBinary(msg2, &buf, &size, &endian) == CMSG_OK) {
          for (i=0; i < size; i++) {
            printf("%x ", buf[i] & 0xff);
          }
        }
      }
      else {
        printf("Cannot find field \"my_bin\" of field \"message\"\n");
      }
    }
  }
  else {
      printf("Cannot find field \"message\"\n");
  }
  
  cMsgWasSent(msg, &sent);
  if (sent) printf("I WAS SENT\n\n\n");
  else printf("I WAS NOT SENT\n\n\n");
*/  

  /* user MUST free messages passed to the callback */
  cMsgFreeMessage(&msg);
}



/******************************************************************/
static void usage() {
  printf("Usage:  consumer <name> <UDL>\n");
}


/******************************************************************/
int main(int argc,char **argv) {

  char *myName        = "consumer";
  char *myDescription = "C consumer";
  char *subject       = "SUBJECT";
  char *type          = "TYPE";
  char *UDL           = "cMsg:cMsg://localhost/cMsg/test";
  /* char *UDL           = "cMsg:cMsg://broadcast/cMsg/test"; */
  int   err, debug = 1, loops=10000;
  cMsgSubscribeConfig *config;
  void *unSubHandle;
  
  /* msg rate measuring variables */
  int             period = 5, ignore=0;
  double          freq, freqAvg=0., totalT=0.;
  long long       totalC=0;
  

  
  if (argc > 1) {
    myName = argv[1];
    if (strcmp(myName, "-h") == 0) {
      usage();
      return(-1);
    }
  }
  
  if (argc > 2) {
    UDL = argv[2];
    if (strcmp(UDL, "-h") == 0) {
      usage();
      return(-1);
    }
  }
  
  if (argc > 3) {
    usage();
    return(-1);
  }
  
  if (debug) {
    printf("Running the cMsg consumer, \"%s\"\n", myName);
    printf("  connecting to, %s\n", UDL);
  }

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
  
  /* set the subscribe configuration */
  config = cMsgSubscribeConfigCreate();
  cMsgSubscribeSetMaxCueSize(config, 10000);
  cMsgSubscribeSetSkipSize(config, 20);
  cMsgSubscribeSetMaySkip(config,0);
  cMsgSubscribeSetMustSerialize(config, 1);
  cMsgSubscribeSetMaxThreads(config, 290);
  cMsgSubscribeSetMessagesPerThread(config, 150);
  cMsgSetDebugLevel(CMSG_DEBUG_ERROR);
  
  /* subscribe */
  err = cMsgSubscribe(domainId, subject, type, callback, NULL, config, &unSubHandle);
  if (err != CMSG_OK) {
      if (debug) {
          printf("cMsgSubscribe: %s\n",cMsgPerror(err));
      }
      exit(1);
  }
   
  cMsgSubscribeConfigDestroy(config);
    
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
  
  cMsgUnSubscribe(domainId, unSubHandle);
  cMsgDisconnect(&domainId);

  return(0);
}

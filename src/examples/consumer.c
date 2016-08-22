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
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
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
void *unSubHandle;


/******************************************************************/
static void callback(void *msg, void *arg) {  
  int i, sent, endian, version, val;
  size_t size;
  int32_t *vals;
  char *p;
  
  count++;
  //printf("Got message #%d\n", count);

//  sleep(2);
  
  cMsgToString(msg, &p);
  printf("XML message:\n%s", p);
  free(p);
  
  /*
  cMsgGetPayloadText(msg, &p);
  printf("\n\n\npayload text:\n%s", p);
  */  

  /* user MUST free messages passed to the callback */
  cMsgFreeMessage(&msg);
}

/******************************************************************/
static void *thread2(void *arg) {
    int val, iter=0;
    while (1) {
        if (iter++%2 == 0) {
            printf("PAUSE for 3 sec\n");
            cMsgSubscriptionPause(domainId, unSubHandle);
        }
        else {
            printf("RESUME for 3 sec\n");
            cMsgSubscriptionResume(domainId, unSubHandle);
        }
        sleep(3);
        cMsgSubscriptionMessagesTotal(domainId, unSubHandle, &val);
        printf("  %d messages received in callback so far\n", val);
        cMsgSubscriptionQueueCount(domainId, unSubHandle, &val);
        printf("  %d messages in queue right now\n", val);
        cMsgSubscriptionQueueIsFull(domainId, unSubHandle, &val);
        if (val) {
            printf("  message queue is full, so clear it\n");
            cMsgSubscriptionQueueClear(domainId, unSubHandle);
        }
        else
            printf("  message queue is NOT full\n");

    }

    pthread_exit(NULL);
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
  char *UDL           = "cMsg://localhost/cMsg/myNameSpace";
  /* char *UDL           = "cMsg:cMsg://broadcast/cMsg/myNameSpace"; */
  int   i, err, debug = 1, loops=30, connected, status;
  cMsgSubscribeConfig *config;
  pthread_t tid;

  /* msg rate measuring variables */
  int             period = 3, ignore=0;
  double          freq, freq1, freq2, freqAvg=0., totalT=0., totalT1=0., totalT2=0., deltaT;
  long long       totalC=0;
  struct timespec t1, t2, T1, T2, sleeep;

  sleeep.tv_sec  = 0; /* .02 sec */
  sleeep.tv_nsec = 40000000;

  
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
    cMsgSetDebugLevel(CMSG_DEBUG_INFO);

    /* start receiving messages */
  cMsgReceiveStart(domainId);
  
  /* set the subscribe configuration */
  config = cMsgSubscribeConfigCreate();
  cMsgSubscribeSetMaxCueSize(config, 10);
  /*
  cMsgSubscribeSetSkipSize(config, 20);
  cMsgSubscribeSetMaySkip(config,0);
  cMsgSubscribeSetMustSerialize(config, 0);
  cMsgSubscribeSetMaxThreads(config, 10);
  cMsgSubscribeSetMessagesPerThread(config, 10);
  */

  /* subscribe */
  cMsgSubscribe(domainId, subject, type, callback, NULL, config, &unSubHandle);
   
  cMsgSubscribeConfigDestroy(config);

  /* Start up new thread. */
  
  status = pthread_create(&tid, NULL, thread2, NULL);
  if (status != 0) {
    printf("Error creating update server thread");
    exit(-1);
  }
  

  
  /* NEW PART */
  //cMsgSetDebugLevel(CMSG_DEBUG_INFO);

  /* read time for rate calculations */

  clock_gettime(CLOCK_REALTIME, &T1);
  clock_gettime(CLOCK_REALTIME, &t1);

  for (i=0; i< 1000; i++) {
      printf("D%d\n",i+1);
      clock_gettime(CLOCK_REALTIME, &t1);
      err = cMsgDisconnect(&domainId);
      if (err != CMSG_OK) {
          printf("cMsgDisconnect: %s\n",cMsgPerror(err));
          //exit(1);
      }
      clock_gettime(CLOCK_REALTIME, &t2);
      totalT1 += (double)(t2.tv_sec - t1.tv_sec) + 1.e-9*(t2.tv_nsec - t1.tv_nsec);

      //nanosleep(&sleeep, NULL);

      printf("C%d\n", i+2);
      clock_gettime(CLOCK_REALTIME, &t1);
      err = cMsgConnect(UDL, myName, myDescription, &domainId);
      if (err != CMSG_OK) {
          printf("cMsgConnect: %s\n",cMsgPerror(err));
      }
      clock_gettime(CLOCK_REALTIME, &t2);
      totalT2 += (double)(t2.tv_sec - t1.tv_sec) + 1.e-9*(t2.tv_nsec - t1.tv_nsec);
      
      //nanosleep(&sleeep, NULL);
  }
  clock_gettime(CLOCK_REALTIME, &T2);

  freq1 = 100/totalT1;
  freq2 = 100/totalT2;
  totalT1 += (double)(T2.tv_sec - T1.tv_sec) + 1.e-9*(T2.tv_nsec - T1.tv_nsec);
  freq  = 200/totalT1;

  printf("count = %d, disconnect freq = %9.1f Hz, connect freq = %9.1f Hz, total freq = %9.1f Hz\n",
         100, freq1, freq2, freq);
  */
   
/*  printf("Connected!, please kill server:\n");
  cMsgGetConnectState(domainId, &connected);
  i=0;
  while (connected) {
      sleep(1);
      printf("%d, ", ++i);
      fflush(stdout);
      cMsgGetConnectState(domainId, &connected);
  }
  printf("\nDisconnected from server, try reconnect()\n");

  while (!connected) {
      err = cMsgReconnect(domainId);
      if (err != CMSG_OK) {
          printf("Failed reconnecting to server, again in 2 secs\n");
          sleep(2);
     }
     cMsgGetConnectState(domainId, &connected);
  }

  if (connected) {
      printf("Now we are connected to server\n");
  }
  else {
      printf("Now we are disconnected from server\n");
  }
*/
  
//     
//   printf("Connected!, please kill server AGAIN!:\n");
//   cMsgGetConnectState(domainId, &connected);
//   i=0;
//   while (connected) {
//       sleep(1);
//       printf("%d, ", ++i);
//       fflush(stdout);
//       cMsgGetConnectState(domainId, &connected);
//   }
//   printf("\nDisconnected from server, try another connect()\n");
// 
//   while (!connected) {
//       err = cMsgReconnect(domainId);
//       if (err != CMSG_OK) {
//           printf("Failed reconnecting to server, again in 2 secs\n");
//           sleep(2);
//      }
//      cMsgGetConnectState(domainId, &connected);
//   }
// 
//   if (connected) {
//       printf("Now we are connected to server\n");
//   }
//   else {
//       printf("Now we are disconnected from server\n");
//   }
//     
//   sleep(2);
//   printf("Now disconnect from server\n");
//   err = cMsgDisconnect(&domainId);
//   if (err != CMSG_OK) {
//       printf("Failed disconnecting from server\n");
//   }
//   
//   sleep(2);
//   printf("Now reconnect to server\n");
//   err = cMsgReconnect(domainId);
//   if (err != CMSG_OK) {
//       printf("Failed reconnecting to server, err = %d\n", err);
//       exit(-1);
//   }
//


      
/*      while (1) {
          char *udl;
          cMsgSetUDL(domainId, "cMsg://localhost/cMsg/myNameSpace");
          err = cMsgReconnect(domainId);
          if (err != CMSG_OK) {
              printf("Failed reconnecting to server, again in 2 secs\n");
          }
          else {
              printf("reconnect succeeded, wait 2 sec\n");
          }
          cMsgGetConnectState(domainId, &connected);
          cMsgGetCurrentUDL(domainId, &udl);
          printf("Current UDL = %s\n",udl);
          sleep(2);
          
          cMsgSetUDL(domainId, "cMsg://localhost:45005/cMsg/myNameSpace");
          err = cMsgReconnect(domainId);
          if (err != CMSG_OK) {
              printf("Failed reconnecting to server, again in 2 secs\n");
          }
          else {
              printf("reconnect succeeded, wait 2 sec\n");
          }
          cMsgGetConnectState(domainId, &connected);
          cMsgGetCurrentUDL(domainId, &udl);
          printf("Current UDL = %s\n",udl);
          sleep(2);
      }
*/
  
  /* END NEW PART */

  while (loops-- > 0) {
  //    count = 0;

      /* wait for messages */
      sleep(period);

      /* calculate rate */
      if (!ignore) {
          totalT += period;
          totalC += count;
          freq    = (double)count/period;
          freqAvg = totalC/totalT;
        //  printf("count = %d, %9.1f Hz, %9.1f Hz Avg.\n", count, freq, freqAvg);
      }
      else {
          ignore--;
      }
  }
  cMsgUnSubscribe(domainId, unSubHandle);
  sleep(4);
  cMsgDisconnect(&domainId);
  pthread_exit(NULL);
  return(0);
}

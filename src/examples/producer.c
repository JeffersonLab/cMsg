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
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <limits.h>

#include "cMsg.h"


/******************************************************************/
static void usage() {
    printf("Usage:  producer [-a <size> | -b <size>] -u <UDL> -s <subject> -t <type>\n");
    printf("                  -a sets the byte size for ascii data, or\n");
    printf("                  -b sets the byte size for binary data\n");
    printf("                  -s sets the subscription subject\n");
    printf("                  -t sets the subscription type\n");
    printf("                  -u sets the connection UDL\n");
}

/******************************************************************/
int main(int argc,char **argv) {  

  char *myName        = "producer";
  char *myDescription = "C producer";
  char *subject       = "SUBJECT";
  char *type          = "TYPE";
  char *text          = "JUNK";
  char *bytes         = NULL;
  char *UDL           = "cMsg:cmsg://localhost:3456/cMsg/test";
  char *p;
  int   i, j, err, debug=1, msgSize=0, mainloops=200;
//  int8_t    i1vals[3];
//  int16_t   i2vals[3];
//  int32_t   i3vals[3];
//  int64_t   i4vals[3];
//  uint8_t   i5vals[3];
//  uint16_t  i6vals[3];
//  uint32_t  i7vals[3];
//  uint64_t  i8vals[3];
//  char       *vals[3];
//  float      fvals[3];
//  double     dvals[3];
  void *msg, *domainId;
  
  /* msg rate measuring variables */
  int             dostring=1, count, delay=0, loops=20000, ignore=0;
  struct timespec t1, t2, sleeep;
  double          freq, freqAvg=0., deltaT, totalT=0.;
  long long       totalC=0;
  
  sleeep.tv_sec  = 3; /* 3 sec */
  sleeep.tv_nsec = 0;
  
  if (argc > 1) {
  
    for (i=1; i<argc; i++) {

       if (strcmp("-a", argv[i]) == 0) {
         if (argc < i+2) {
           usage();
           return(-1);
         }
         
         dostring = 1;
         
         msgSize = atoi(argv[++i]);
         text = p = (char *) malloc((size_t) (msgSize + 1));
         if (p == NULL) exit(1);
printf("using text msg size %d\n", msgSize);
         for (j=0; j < msgSize; j++) {
           *p = 'A';
           p++;
         }
         *p = '\0';
         /*printf("Text = %s\n", text);*/
         
       }
       else if (strcmp("-b", argv[i]) == 0) {
         if (argc < i+2) {
           usage();
           return(-1);
         }
         
         dostring = 0;
         
         msgSize = atoi(argv[++i]);
         if (msgSize < 1) {
           usage();
           return(-1);
         }
printf("using array msg size %d\n", msgSize);
         bytes = p = (char *) malloc((size_t) msgSize);
         if (p == NULL) exit(1);
         for (j=0; j < msgSize; j++) {
           *p = j%255;
           p++;
         }

       }
       else if (strcmp(argv[i], "-s") == 0) {
         if (argc < i+2) {
           usage();
           return(-1);
         }
         subject = argv[++i];
       }
       else if (strcmp(argv[i], "-t") == 0) {
         if (argc < i+2) {
           usage();
           return(-1);
         }
         type = argv[++i];
       }
       else if (strcmp(argv[i], "-u") == 0) {
         if (argc < i+2) {
           usage();
           return(-1);
         }
         UDL = argv[++i];
printf("Setting UDP = %s\n", UDL);
       }
       else if (strcmp(argv[i], "-h") == 0) {
         usage();
         return(-1);
       }
       else {
         usage();
         return(-1);
       }
    }  
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
/*  cMsgSetReliableSend(msg, 0);*/
  
  /* set compound payload fields */

//  cMsgAddFloat (msg, "flt", -1.23456e-05);
//  cMsgAddDouble(msg, "dbl", -1.23456789101012e-195);
//  
//  cMsgAddString (msg, "str", "string");
//  
//  vals[0] = "zero";
//  vals[1] = "one";
//  vals[2] = "two";
//  cMsgAddStringArray(msg, "strA", (const char **) vals, 3);
//
//  fvals[0] = -1234567;
//  fvals[1] = +234.;
//  fvals[2] = -12.3456e-35;
//  cMsgAddFloatArray(msg,  "fltA", fvals, 3);
//  
//  dvals[0] = -1234567;
//  dvals[1] = +234.678910;
//  dvals[2] = -12.3456789101012e-301;
//  cMsgAddDoubleArray(msg,  "dblA", dvals, 3);
//
//  cMsgAddInt8   (msg, "int8",  SCHAR_MIN);
//  cMsgAddInt16  (msg, "int16", SHRT_MIN);
//  cMsgAddInt32  (msg, "int32", INT_MIN);
//  cMsgAddInt64  (msg, "int64", -9223372036854775807LL);
//  
//  cMsgAddUint8   (msg, "uint8",  UCHAR_MAX);
//  cMsgAddUint16  (msg, "uint16", USHRT_MAX);
//  cMsgAddUint32  (msg, "uint32", UINT_MAX);
//  cMsgAddUint64  (msg, "uint64", 18446744073709551615ULL);
//    
//  i1vals[0] = -128;
//  i1vals[1] = 127;
//  i1vals[2] = 255;
//  cMsgAddInt8Array(msg, "int8A", i1vals, 3);
//  
//  i2vals[0] = -32768;
//  i2vals[1] = 32767;
//  i2vals[2] = 65535;
//  cMsgAddInt16Array(msg, "int16A", i2vals, 3);
//  
//  i3vals[0] = -2147483648;
//  i3vals[1] = 2147483647;
//  i3vals[2] = 4294967295;
//  cMsgAddInt32Array(msg, "int32A", i3vals, 3);
//  
//  i4vals[0] = -9223372036854775807LL;
//  i4vals[1] = 9223372036854775807LL;
//  i4vals[2] = 18446744073709551615ULL;
//  cMsgAddInt64Array(msg, "int64A", i4vals, 3);
//  
//  i5vals[0] = 0;
//  i5vals[1] = 255;
//  i5vals[2] = -1;
//  cMsgAddUint8Array(msg, "uint8A", i5vals, 3);
//
//  i6vals[0] = 0;
//  i6vals[1] = 65535;
//  i6vals[2] = -1;
//  cMsgAddUint16Array(msg, "uint16A", i6vals, 3);
//  
//  i7vals[0] = 0U;
//  i7vals[1] = 4294967295U;
//  i7vals[2] = -1;
//  cMsgAddUint32Array(msg, "uint32A", i7vals, 3);
//  
//  i8vals[0] = 0ULL;
//  i8vals[1] = 18446744073709551615ULL;
//  i8vals[2] = -1;
//  cMsgAddUint64Array(msg, "uint64A", i8vals, 3);
//  
//  cMsgToString(msg, &p, 1);
//  printf("XML message:\n%s", p);
//  free(p);
//    
  if (dostring) {
    printf("  try setting text to %s\n", text);
    cMsgSetText(msg, text);
  }
  else {
    printf("  setting byte array\n");
    cMsgSetByteArrayAndLimits(msg, bytes, 0, msgSize);
  }
   
  while (mainloops-- > 0) {
      count = 0;
      
      /* read time for rate calculations */
      clock_gettime(CLOCK_REALTIME, &t1);

      for (i=0; i < loops; i++) {
          /* send msg */
          /* err = cMsgSyncSend(domainId, msg, NULL, &response); */
          err = cMsgSend(domainId, msg);
          if (err != CMSG_OK) {
            printf("cMsgSend: err = %d, %s\n",err, cMsgPerror(err));
            fflush(stdout);
            goto end;
          }
          cMsgFlush(domainId, NULL);
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
printf("producer: will free msg\n");  
  cMsgFreeMessage(&msg);
printf("producer: will disconnect\n");  
  err = cMsgDisconnect(&domainId);
  if (err != CMSG_OK) {
      if (debug) {
          printf("err = %d, %s\n",err, cMsgPerror(err));
      }
  }
    
  return(0);
}

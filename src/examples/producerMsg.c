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
  printf("Usage:  producer [-s <size> | -b <size>] -u <UDL>\n");
  printf("                  -s sets the byte size for text data, or\n");
  printf("                  -b sets the byte size for binary data\n");
  printf("                  -u sets the connection UDL\n");
}

/******************************************************************/
int main(int argc,char **argv) {  

  char *myName        = "producerMsg";
  char *myDescription = "C payload producer";
  char *subject       = "SUBJECT";
  char *type          = "TYPE";
  char *text          = "JUNK";
  char *bytes         = NULL;
  char *UDL           = "cMsg:cmsg://localhost:3456/cMsg/test";
  char *p;
  char  bin[256], *strs[3];
  int   i, j, test[10], err, debug=1, msgSize=0, mainloops=200;
  int32_t test32[1000];
  int64_t test64[4];
  double dbl, testd[7], dzero[10000];
  float  flt, testf[10000];
  void *msg1, *msg2, *msg3, *domainId, *msgs[2];
  
  /* msg rate measuring variables */
  int             dostring=1, count, delay=1, loops=10000, ignore=0;
  struct timespec t1, t2, sleeep;
  double          freq, freqAvg=0., deltaT, totalT=0.;
  long long       totalC=0;
  
  sleeep.tv_sec  = 3; /* 3 sec */
  sleeep.tv_nsec = 0;
  
  if (argc > 1) {
  
    for (i=1; i<argc; i++) {

       if (strcmp("-s", argv[i]) == 0) {
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
         for (i=0; i < msgSize; i++) {
           *p = i%255;
           p++;
         }

       }
       else if (strcmp(argv[i], "-u") == 0) {
         if (argc < i+2) {
           usage();
           return(-1);
         }
         UDL = argv[++i];
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
  
 flt = 0.;
 printf("String rep of  float (0) = %s\n", cMsgFloatChars(flt));
 dbl = 0.;
 printf("String rep of double (0) = %s\n", cMsgDoubleChars(dbl));
 
  
  if (debug) {
    printf("Running the cMsg producerMsg, \"%s\"\n", myName);
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
  
  for (i=0; i< 256; i++) {
    bin[i] = i;
  }

  /* create message to be sent */
  msg1 = cMsgCreateMessage();    /* allocating mem here */
  cMsgSetSubject(msg1, subject); /* allocating mem here */
  cMsgSetType(msg1, type);       /* allocating mem here */
  
  test[0] = 0;
  test[1] = 0;
  test[2] = 0;
  test[3] = 0;
  test[4] = 0;
  test[5] = 1;
  test[6] = 0;
  test[7] = 0;
  test[8] = 0;
  test[9] = 0;
  
  test64[0] = 1000000000000LL;
  test64[1] = -1000000000000LL;
  test64[2] = 256L;
  test64[3] = 65535L;
  
  testd[0] = 0;
  testd[1] = 1.;
  testd[2] = 2.;
  testd[3] = 3.;
  testd[4] = 1.23e-123;
  testd[5] = 123456789.987654321;
  testd[6] = 3.14159e+111;
  
  cMsgAddInt32Array(msg1, "int32array", test, 10);
  cMsgAddInt64Array(msg1, "int64array", test64, 4);
  cMsgAddDoubleArray(msg1, "Dblarray", testd, 7);
  
  flt =  1.23456;
  cMsgAddFloat(msg1, "floater", flt);
  cMsgAddDouble(msg1, "doubler",testd[4]);
  
  memset(testf, 0, 10000*sizeof(float));
  memset(dzero, 0, 10000*sizeof(double));
  dzero[0] = 1.;
  dzero[500] = 1.;
  dzero[999] = 1.;
  
  err = cMsgAddFloatArray(msg1, "Fltarray", testf, 10);
  if (err != CMSG_OK) printf("Err = %s\n",cMsgPerror(err)); 
  err = cMsgAddDoubleArray(msg1, "DblZero", dzero, 10);
  
  cMsgAddInt32(msg1, "msg1_int2", 7899);
  cMsgAddDouble(msg1, "msg1_dbl", 1.23);
  cMsgAddString(msg1, "msg1_str", "blah blah");
  strs[0] = "a";
  strs[1] = "b";
  strs[2] = "c";
  cMsgAddStringArray(msg1, "msg1_strA", strs, 3);
  
  /* message contained by msg1 */
  
  msg2 = cMsgCreateMessage();
  cMsgSetSubject(msg2, "Subject 2");
  cMsgSetType(msg2, "Type 2");
  cMsgAddInt16(msg2, "msg2_int16", 16);
  
  err = cMsgAddBinary(msg2, "my_bin", bin, 8, CMSG_ENDIAN_LOCAL);
  if (err != CMSG_OK) {
      if (debug) {
          printf("cMsgAddBinary: %s\n",cMsgPerror(err));
      }
      exit(1);
  }
  
  /* message contained by msg1 */
  msg3 = cMsgCreateMessage();
  cMsgSetSubject(msg3, "Subject_3");
  cMsgSetType(msg3, "Type_3");
  cMsgAddDouble(msg3, "doubler", 1.23456789123);
   
  /* add msg 3 to msg 2 */
  cMsgAddMessage(msg2, "messagE", msg3);
  /* add msg 2 to msg 1 */
  msgs[0] = msg2;
  msgs[1] = msg3;
  cMsgAddMessageArray(msg1, "message", msgs, 2);
  cMsgAddInt64(msg1, "msg1_64_int1", 6789LL);
    
  cMsgToString(msg1, &p, 1);
  printf("XML message:\n%s", p);
  free(p);
    
  if (dostring) {
    printf("  try setting text to %s\n", text);
    cMsgSetText(msg1, text);
  }
  else {
    printf("  setting byte array\n");
    cMsgSetByteArrayAndLimits(msg1, bytes, 0, msgSize);
  }
   
  while (mainloops-- > 0) {
      count = 0;
      
      /* read time for rate calculations */
      clock_gettime(CLOCK_REALTIME, &t1);

      for (i=0; i < loops; i++) {
          /* send msg1 */
          /* err = cMsgSyncSend(domainId, msg1, NULL, &response); */
          err = cMsgSend(domainId, msg1);
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
printf("producer: will free msg1\n");  
  cMsgFreeMessage(&msg1);
printf("producer: will disconnect\n");  
  err = cMsgDisconnect(&domainId);
  if (err != CMSG_OK) {
      if (debug) {
          printf("err = %d, %s\n",err, cMsgPerror(err));
      }
  }
    
  return(0);
}

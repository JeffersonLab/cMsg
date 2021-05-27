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

  void *msg,*msg2, *domainId;
  const void *msgs[2];
  int delay=0, count=0;

/******************************************************************/
static void usage() {
    printf("\nUsage:  producer [-d | -p | -r | -a <size> | -b <size>] | -n <name> |\n");
    printf("                  -u <UDL> | -s <subject> | -t <type> | -udp | -delay <sec>]\n\n");
    printf("                  -d turns on debug output\n");
    printf("                  -p gives message a payload\n");
    printf("                  -r receives own messages\n");
    printf("                  -a sets the byte size for ascii data, or\n");
    printf("                  -b sets the byte size for binary data\n");
    printf("                  -n sets the client name\n");
    printf("                  -u sets the connection UDL\n");
    printf("                  -s sets the subscription subject\n");
    printf("                  -t sets the subscription type\n");
    printf("                  -udp sends messages with UDP instead of TCP\n");
    printf("                  -delay sets the delay between sends in millisec\n");
}

/******************************************************************/
static void callback(void *msg, void *arg) {  
  int err;
  char *p;

  count++;
  printf("X\n");
   cMsgToString2(msg, &p, 0, 0, 1);
   printf("XML message:\n%s", p);
   free(p);

  /*
  printf("\nCallback msg payload:\n");
  cMsgPayloadPrint(msg);
  */
  
  /*
  if (delay != 0) {
  nanosleep(&sleeep, NULL);
  }

  err = cMsgSend(domainId, msg);
  if (err != CMSG_OK) {
     printf("cMsgSend: err = %d, %s\n",err, cMsgPerror(err));
     return;
  }
  */
  /* user MUST free messages passed to the callback */
  cMsgFreeMessage(&msg);
}

/******************************************************************/
int main(int argc,char **argv) {  

  char *myName        = "producer";
  char *myDescription = "C producer";
  char *subject       = "SUBJECT";
  char *type          = "TYPE";
  char *text          = "JUNK";
  char *bytes         = NULL;
  char *UDL           = "cMsg://localhost/cMsg/myNameSpace";
  char *p;
  char *jj[] = {"k","j","l"};
  int   i, j, err, udp=0, payload=0, receives=0, debug=0, msgSize=0, mainloops=2;
  int8_t    i1vals[3];
  int16_t   i2vals[3];
  int32_t   i3vals[3];
  int64_t   i4vals[3];
  uint8_t   i5vals[3];
  uint16_t  i6vals[3];
  uint32_t  i7vals[3];
  uint64_t  i8vals[3];
  char       *vals[3];
  float      fvals[3];
  double     dvals[3];
  char       binArray[256], b1[3], b2[3], b3[3];
  const char *bins[3];
  int        switcher=0, sizes[3], endians[3];
  cMsgSubscribeConfig *config;
  void *unSubHandle;

  /* msg rate measuring variables */
  int             dostring=1, loops=2, ignore=0;
  struct timespec t1, t2, sleeep = {0,0};
  double          freq, freqAvg=0., deltaT, totalT=0.;
  long long       totalC=0;
    
  if (argc > 1) {
  
    for (i=1; i<argc; i++) {

        if (strcmp("-delay", argv[i]) == 0) {
            long nsec, sec;
            
            if (argc < i+2) {
                usage();
                return(-1);
            }
         
            delay = atoi(argv[++i]);
            if (delay < 0) {
                printf("Delay must be >= 0\n\n");
                usage();
                return(-1);
            }
            
            sec  =  delay/1000L;
            nsec = (delay - sec*1000L)*1000000L;
            
            sleeep.tv_sec  = sec;
            sleeep.tv_nsec = nsec;
            printf("Delay sec = %ld, nsec = %ld\n", sec, nsec);
        }
        else if (strcmp("-a", argv[i]) == 0) {
            if (argc < i+2) {
                usage();
                return(-1);
            }
         
            dostring = 1;
         
            msgSize = atoi(argv[++i]);
            text = p = (char *) malloc((size_t) (msgSize + 1));
            if (p == NULL) exit(1);
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
         bytes = p = (char *) malloc((size_t) msgSize);
         if (p == NULL) exit(1);
         for (j=0; j < msgSize; j++) {
           *p = j%255;
           p++;
         }
       }
       else if (strcmp(argv[i], "-n") == 0) {
         if (argc < i+2) {
           usage();
           return(-1);
         }
         myName = argv[++i];
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
       }
       else if (strcmp(argv[i], "-d") == 0) {
           debug = 1;
       }
       else if (strcmp(argv[i], "-p") == 0) {
           payload = 1;
       }
       else if (strcmp(argv[i], "-r") == 0) {
           receives = 1;
       }
       else if (strcmp(argv[i], "-udp") == 0) {
           udp = 1;
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
      cMsgSetDebugLevel(CMSG_DEBUG_INFO);
  }
  else {
      cMsgSetDebugLevel(CMSG_DEBUG_NONE);
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
  
  if (receives) {
      /* set the subscribe configuration */
      config = cMsgSubscribeConfigCreate();

      /* subscribe - receive own messages */
      err = cMsgSubscribe(domainId, subject, type, callback, NULL, config, &unSubHandle);
      if (err != CMSG_OK) {
          if (debug) {
              printf("cMsgSubscribe: %s\n",cMsgPerror(err));
          }
          exit(1);
      }
      cMsgSubscribeConfigDestroy(config);
  }
  
  /* create message to be sent */
  msg = cMsgCreateMessage();    /* allocating mem here */
  cMsgSetSubject(msg, subject); /* allocating mem here */
  cMsgSetType(msg, type);       /* allocating mem here */
  cMsgSetText(msg, "~!@#$%^&*()_-+=,.<>?/|\\;:[]{}`");
  /*cMsgSetText(msg, "A<![CDATA[B]]><![CDATA[C]]>]]>D");*/
  cMsgSetHistoryLengthMax(msg, 3); /* no history by default */

  if (udp) {
    cMsgSetReliableSend(msg, 0);
  }

  /* define binary arrays */
  for (j=0; j < 256; j++) {
      binArray[j] = (char)j%255;
  }

  //bins[0]    = bins[1]    = bins[2]    = binArray;
  //sizes[0]   = sizes[1]   = sizes[2]   = 256;

  b1[0] = (char)-1;  b1[1] = (char)-2;  b1[2] = (char)-3;
  b2[0] = (char)10;  b2[1] = (char)20;  b2[2] = (char)30;
  b3[0] = (char)-7;  b3[1] = (char)-8;  b3[2] = (char)-9;
  
  bins[0] = b1; bins[1] = b2; bins[2] = b3;
  sizes[0] = sizes[1] = sizes[2] = 3;
  endians[0] = endians[1] = endians[2] = CMSG_ENDIAN_LITTLE;

  /* set the byte array */
  cMsgSetByteArrayNoCopy(msg, binArray, 58);

  if (payload) {
      /* keep only the 4 most recent entries of history */
     cMsgSetHistoryLengthMax(msg, 4);

     cMsgAddBinary(msg, "binaryArray", binArray, 58, CMSG_ENDIAN_LOCAL);

     cMsgAddBinaryArray(msg, "array_of_bin_arrays", bins, 3, sizes, endians);

     cMsgAddFloat (msg, "flt", -1.23456e-05F);
     cMsgAddDouble(msg, "dbl", -1.23456789101012e-195);

     cMsgAddString (msg, "str", "string");

     vals[0] = "zero\nzero";
     vals[1] = "one\none";
     vals[2] = "two\ntwo";
     cMsgAddStringArray(msg, "strA", (const char **) vals, 3);

     fvals[0] = -1234567;
     fvals[1] = +234.F;
     fvals[2] = -12.3456e-35F;
     cMsgAddFloatArray(msg,  "fltA", fvals, 3);

     dvals[0] = -1234567;
     dvals[1] = +234.678910;
     dvals[2] = -12.3456789101012e-301;
     cMsgAddDoubleArray(msg,  "dblA", dvals, 3);

     cMsgAddInt8   (msg, "int8",  SCHAR_MIN);
     cMsgAddInt16  (msg, "int16", SHRT_MIN);
     cMsgAddInt32  (msg, "int32", INT_MIN);
     cMsgAddInt64  (msg, "int64", -9223372036854775807LL);

     cMsgAddUint8   (msg, "uint8",  UCHAR_MAX);
     cMsgAddUint16  (msg, "uint16", USHRT_MAX);
     cMsgAddUint32  (msg, "uint32", UINT_MAX);
     cMsgAddUint64  (msg, "uint64", 18446744073709551615ULL);

     i1vals[0] = -128;
     i1vals[1] = 127;
     i1vals[2] = (int8_t)255;
     cMsgAddInt8Array(msg, "int8A", i1vals, 3);

     i2vals[0] = -32768;
     i2vals[1] = 32767;
     i2vals[2] = (int16_t)65535;
     cMsgAddInt16Array(msg, "int16A", i2vals, 3);

     i3vals[0] = -2147483648;
     i3vals[1] = 2147483647;
     i3vals[2] = (int32_t)4294967295;
     cMsgAddInt32Array(msg, "int32A", i3vals, 3);

     i4vals[0] = -9223372036854775807LL;
     i4vals[1] = 9223372036854775807LL;
     i4vals[2] = 18446744073709551615ULL;
     cMsgAddInt64Array(msg, "int64A", i4vals, 3);

     i5vals[0] = 0;
     i5vals[1] = 255;
     i5vals[2] = -1;
     cMsgAddUint8Array(msg, "uint8A", i5vals, 3);

     i6vals[0] = 0;
     i6vals[1] = 65535;
     i6vals[2] = -1;
     cMsgAddUint16Array(msg, "uint16A", i6vals, 3);

     i7vals[0] = 0U;
     i7vals[1] = 4294967295U;
     i7vals[2] = -1;
     cMsgAddUint32Array(msg, "uint32A", i7vals, 3);

     i8vals[0] = 0ULL;
     i8vals[1] = 18446744073709551615ULL;
     i8vals[2] = -1;
     cMsgAddUint64Array(msg, "uint64A", i8vals, 3);

     /* create message for payload */
     msg2 = cMsgCreateMessage();    /* allocating mem here */
     cMsgSetSubject(msg2, subject); /* allocating mem here */
     cMsgSetType(msg2, type);       /* allocating mem here */
     cMsgSetText(msg2, "TEXT FIELD IN MSG");       /* allocating mem here */
     cMsgAddFloat (msg2, "flt", -1.23456e-05);
     cMsgAddDouble(msg2, "dbl", -1.23456789101012e-195);
     cMsgAddString (msg2, "str", "HEY, HEY, YOU THERE!!!");

     /* create array of msgs for payload */
     cMsgAddMessage(msg, "myMessage", msg2);
     msgs[0] = msg2;
     msgs[1] = msg2;
     cMsgAddMessageArray(msg, "msgArray", msgs, 2);
  }
  
  /*
  cMsgToString(msg, &p);
  printf("XML message:\n%s", p);
  free(p);
  cMsgGetPayloadText(msg, &p);
  printf("\n\n\npayload text:\n%s", p);
  */

  while (mainloops-- > 0) {
      count = 0;
      
      /* read time for rate calculations */
      clock_gettime(CLOCK_REALTIME, &t1);

      for (i=0; i < loops; i++) {
          /* send msg */
          /* err = cMsgSyncSend(domainId, msg, NULL, &response); */
/*          if (udp && switcher%2 == 0) {
              cMsgSetReliableSend(msg, 0);
          }
          else {
              cMsgSetReliableSend(msg, 1);
          }
          switcher++;*/
          err = cMsgSend(domainId, msg);
          if (err != CMSG_OK) {
              printf("cMsgSend: err = %d, %s\n",err, cMsgPerror(err));
              fflush(stdout);
              goto end;
          }
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
          
  /*printf("producer: will free msg, msg = %p, &msg = %p\n", msg, &msg);*/
  cMsgFreeMessage(&msg);

    printf("SLEEPING before disconnect\n");
    sleep(40);

    /*printf("producer: will disconnect\n");*/
  err = cMsgDisconnect(&domainId);
  if (err != CMSG_OK) {
      if (debug) {
          printf("err = %d, %s\n",err, cMsgPerror(err));
      }
  }
    printf("SLEEPING after disconnect\n");
    sleep(40);

  return(0);
}

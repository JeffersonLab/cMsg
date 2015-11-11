/*----------------------------------------------------------------------------*
 *  Copyright (c) 2014        Jefferson Science Associates,                   *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 7-Aug-2014, Jefferson Lab                                    *
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
#include <time.h>

#include "cMsg.h"
#include "cMsgNetwork.h"


void *msg, *domainId;
int delay=0, count=0;

/******************************************************************/
static void usage() {
    printf("\nUsage:  emuProducer  -n <name> -x <expid> -p <port> [-d | -u <UDL> | -delay <msec>]\n\n");
    printf("                  -d turns on debug output\n");
    printf("                  -n sets the destination CODA component name\n");
    printf("                  -x sets the expid\n");
    printf("                  -u sets the connection UDL\n");
    printf("                  -p sets the multicast port\n");
    printf("                  -delay sets the delay between sends in millisec\n");
}


/******************************************************************/
int main(int argc,char **argv) {

    char *myName        = "emuProducer";
    char *myDescription = "coda event generator in emu domain";
    //  char *UDL           = "emu://23945/emutest?myCodaId=0&timeout=4";
    char UDL[256];
    char *componentName = NULL, *expid = NULL, *udl = NULL;

    int     i, j, err, debug=0, mainloops=2000, port=46100;
    int32_t evioData[103];

    /* msg rate measuring variables */
    int             dostring=1, loops=1000000, ignore=0;
    struct timespec t1, t2, sleeep = {0,0};
    double          freq, freqAvg=0., deltaT, totalT=0.;
    long long       totalC=0;


    memset(UDL, 0, 256);

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
            else if (strcmp(argv[i], "-p") == 0) {
                int newPort = atoi(argv[++i]);
                if (newPort < 1024 || newPort > 65535) {
                    printf("Port must be > 1023 and < 65536\n\n");
                    usage();
                    return(-1);
                }

                port = newPort;
            }
            else if (strcmp(argv[i], "-n") == 0) {
                if (argc < i+2) {
                    usage();
                    return(-1);
                }
                componentName = argv[++i];
            }
            else if (strcmp(argv[i], "-x") == 0) {
                if (argc < i+2) {
                    usage();
                    return(-1);
                }
                expid = argv[++i];
            }
            else if (strcmp(argv[i], "-u") == 0) {
                if (argc < i+2) {
                    usage();
                    return(-1);
                }
                udl = argv[++i];
            }
            else if (strcmp(argv[i], "-d") == 0) {
                debug = 1;
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

    /* If a UDL was not supplied, we need to construct one. */
    if (udl == NULL) {
        if (componentName == NULL) {
            printf("Component name must be specified\n\n");
            usage();
            return(-1);
        }
        else if (expid == NULL) {
            printf("Expid must be specified\n\n");
            usage();
            return(-1);
        }

        sprintf(UDL, "emu://%d/%s/%s?codaId=0&timeout=4", port, expid, componentName);
    }
    else {
        sprintf(UDL, "%s", udl);
    }
    printf("Using UDL = %s\n", UDL);


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

    /* create message to be sent (allocates mem) */
    msg = cMsgCreateMessage();
    cMsgSetUserInt(msg, EMU_EVIO_FILE_FORMAT);
    cMsgSetHistoryLengthMax(msg, 0); /* no history by default */


    /* fake evio event data to send to EB */
    memset((void *)evioData, 0, 103*sizeof(int32_t));

    /* block header */
    evioData[0]  = 0x67; // 103 decimal
    evioData[1]  = 0x1;
    evioData[2]  = 0x8;
    evioData[3]  = 0x1;
    evioData[4]  = 0x0;
    evioData[5]  = 0x204;
    evioData[6]  = 0x0;
    evioData[7]  = 0xc0da0100;

    /* data */
    evioData[8]  = 0x5e; // 94 + 1
    evioData[9]  = 0xe02;
    evioData[10] = 0x9;  // 9 + 1
    evioData[11] = 0xff110d02;
    evioData[12] = 0xf010003;
    evioData[13] = 0x1;
    evioData[14] = 0x0;
    evioData[15] = 0x0;
    evioData[16] = 0xf010003;
    evioData[17] = 0x2;
    evioData[18] = 0x4;
    evioData[19] = 0x0;
    evioData[20] = 0x52;  // 82 + 1
    evioData[21] = 0x6f0102;
    evioData[22] = 0x1;


    /* set the byte array */
    cMsgSetByteArrayNoCopy(msg, (char *)evioData, 103*sizeof(int32_t));

    while (mainloops-- > 0) {
        count = 0;

        /* read time for rate calculations */
        clock_gettime(CLOCK_REALTIME, &t1);

        for (i=0; i < loops; i++) {
            /* send msg */
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

            /*goto end;*/
        }
        else {
            ignore--;
        }
    }

    end:

    /*printf("producer: will free msg, msg = %p, &msg = %p\n", msg, &msg);*/
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

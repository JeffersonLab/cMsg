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
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#include "cMsg.h"
#include "cMsgDomain.h"
#include "cMsgCommonNetwork.h"
#include "emuDomain.h"


void *domainId;


/**
 * Run the following to receive what this program is sending:<p>
 * java org.jlab.coda.emu.test.EmuDomainReceiver -h<p>
 * This will show all options. Run with a port and component name that
 * matches the UDL in the code below.
 */
int main(int argc,char **argv) {

    char *myName   = "C_emu_client";
    char *myDescription = "emu trial";

    codaIpList *listHead=NULL, *listEnd=NULL, *listItem;

    /*
     * Emu domain UDL is of the form:<p>
     *   <b>cMsg:emu://&lt;port&gt;/&lt;expid&gt;/&lt;compName&gt;?codaId=&lt;id&gt;&timeout=&lt;sec&gt;&bufSize=&lt;size&gt;&tcpSend=&lt;size&gt;&subnet=&lt;subnet&gt;&noDelay</b><p>
     *
     * Remember that for this domain:
     *<ol>
     *<li>multicast address is always 239.230.0.0<p>
     *<li>port (of emu domain server) is required<p>
     *<li>expid is required<p>
     *<li>compName is required - destination CODA component name<p>
     *<li>codaId (coda id of data sender) is required<p>
     *<li>optional timeout (sec) to connect to emu server, default = 0 (wait forever)<p>
     *<li>optional bufSize (max size in bytes of a single send), min = 1KB, default = 2.1MB<p>
     *<li>optional tcpSend is the TCP send buffer size in bytes, min = 1KB<p>
     *<li>optional subnet is the preferred subnet used to connect to server<p>
     *<li>optional noDelay is the TCP no-delay parameter turned on<p>
     *</ol><p>
     */

    //char *UDL = "cMsg:emu://46100/emutest/Eb1?codaId=0&timeout=10&sockets=1";
    //char *UDL = "cMsg:emu://46100/emutest/Eb1?codaId=0&timeout=10&sockets=2&subnet=172.19.10.255";
    //char *UDL = "cMsg:emu://46100/emutest/Eb1?codaId=0&timeout=10&sockets=2&subnet=undefined";
    char *UDL = "cMsg:emu://direct:46100/emutest/Eb1?codaId=0&timeout=10&sockets=1&subnet=undefined";
    //char *UDL = "cMsg:emu://46100/emutest/Eb1?codaId=0&timeout=10&sockets=2&subnet=undefined";
    //char *UDL = "cMsg:emu://46100/emutest/Eb1?codaId=0&timeout=10&subnet=172.19.10.255";
    //char *UDL = "cMsg:emu://46100/emutest/Eb1?codaId=0&timeout=10&subnet=129.57.29.255";

    int   err, debug = 1, direct = 1;
    void *msg;
    int32_t data[4*11];



    if (argc > 1) {
        myName = argv[1];
    }

    if (argc > 2) {
        UDL = argv[2];
    }

    if (debug) {
        printf("Running the cMsg client, \"%s\"\n", myName);
        printf("  connecting to, %s\n", UDL);
    }

    /* If connecting directly, provide the IP information of destination. */
    if (direct) {
        int len;
        const char **ipListArray;
        const char **baListArray;

        /* Create address lists for testing */
        const char *ipList[3] = {"132.8.9.10", "172.19.5.100", "129.57.29.64"};
        const char *baList[3] = {"132.8.9.255", "172.19.5.255", "129.57.29.255"};

        /* Create a cmsg message */
        void *msg2 = cMsgCreateMessage();

        /* Add 2 payloads of a string array each */
        cMsgAddStringArray(msg2, "ipList_Roc1", ipList, 3);
        cMsgAddStringArray(msg2, "baList_Roc1", baList, 3);

        /* Extract the 2 string arrays from the msg payloads */
        cMsgGetStringArray(msg2, "ipList_Roc1", &ipListArray, &len);
        cMsgGetStringArray(msg2, "baList_Roc1", &baListArray, &len);

        /* Put string arrays into a static location in lib so cMsgConnect can get to it. */
        setDirectConnectDestination(ipListArray, baListArray, len);
    };

    /* connect to cMsg server */
    err = cMsgConnect(UDL, myName, myDescription, &domainId);
    if (err != CMSG_OK) {
        if (debug) {
            printf("cMsgConnect: %s\n",cMsgPerror(err));
        }
        exit(1);
    }

    /* set debug level */
    cMsgSetDebugLevel(CMSG_DEBUG_INFO);

    sleep(1);

    /* send stuff to emu Server */
    msg = cMsgCreateMessage();
    cMsgSetUserInt(msg, 1);

    /* Create data */
    data[0]  = 0xb;
    data[1]  = 1;
    data[2]  = 8;
    data[3]  = 1;
    data[4]  = 0;
    data[5]  = 0x5204;
    data[6]  = 0;
    data[7]  = 0xc0da0100;
    data[8]  = 2;
    data[9]  = 0x10102;
    data[10] = 7;

    cMsgSetByteArrayNoCopy(msg, (char *)data, 4*11);

    printf("Send messages to emu server\n");

    while (1) {
        err = cMsgSend(domainId, msg);
        if (err != CMSG_OK) {
            printf("ERROR in sending message!!\n");
            exit(-1);
        }
    }

    printf("Sleep for 3 sec\n");
    sleep(3);

    cMsgDisconnect(&domainId);

    cMsgFreeMessage(&msg);

    return(0);
}

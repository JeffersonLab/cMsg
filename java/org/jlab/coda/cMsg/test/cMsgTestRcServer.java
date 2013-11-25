/*----------------------------------------------------------------------------*
 *  Copyright (c) 2007        Jefferson Science Associates,                   *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 1-Feb-2007, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.*;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeoutException;

/**
 * This class implements an RC Multicast server and an RC Server server together
 * and works with cMsgTestRcClient as a client.
 */
public class cMsgTestRcServer {

    private String rcClientHost;
    private int rcClientTcpPort;
    private CountDownLatch latch;
    private int count;
    private boolean debug;
    private String UDL;
    private String rcsUDL;


    class MulticastCallback extends cMsgCallbackAdapter {
         public void callback(cMsgMessage msg, Object userObject) {
             rcClientHost = msg.getSenderHost();
             rcClientTcpPort = msg.getUserInt();

             System.out.println("Running RC Multicast domain callback, host = " + rcClientHost +
                                ", port = " + rcClientTcpPort +
                                ", name = " + msg.getSender());

             // Use the ip addresses and port that the client sends over to
             // create the UDL we need for the rc server.
             cMsgPayloadItem item = msg.getPayloadItem("IpAddresses");
             if (item != null) {
                 try {
                     String[] addrs = item.getStringArray();
                     StringBuilder builder = new StringBuilder(512);
                     for (int i=0; i < addrs.length; i++) {
                         builder.append("rcs://");
                         builder.append(addrs[i]);
                         builder.append(":");
                         builder.append(rcClientTcpPort);
                         if (i < addrs.length - 1) builder.append(";");
                     }

                     rcsUDL = builder.toString();
                 }
                 catch (cMsgException e) {
                 }
             }
             else {
                 rcsUDL = "rcs://" + rcClientHost + ":" + rcClientTcpPort;
             }

             // finish connection
             latch.countDown();
        }
    }


    class rcCallback extends cMsgCallbackAdapter {
        public void callback(cMsgMessage msg, Object userObject) {
            count++;
            System.out.println("Running RC Server domain callback");
            System.out.println("              msg text = " + msg.getText());
            System.out.println("              msg  sub = " + msg.getSubject());
            System.out.println("              msg type = " + msg.getType());
            msg.payloadPrintout(0);
        }
    }


    /** Constructor. */
    cMsgTestRcServer(String[] args) {
        decodeCommandLine(args);
        latch = new CountDownLatch(1);
    }


    /**
     * Method to decode the command line used to start this application.
     * @param args command line arguments
     */
    private void decodeCommandLine(String[] args) {
        // loop over all args
        for (int i = 0; i < args.length; i++) {

            if (args[i].equalsIgnoreCase("-h")) {
                usage();
                System.exit(-1);
            }
            else if (args[i].equalsIgnoreCase("-u")) {
                UDL = args[i + 1];
                i++;
            }
            else if (args[i].equalsIgnoreCase("-debug")) {
                debug = true;
            }
            else {
                usage();
                System.exit(-1);
            }
        }

        return;
    }


    /** Method to print out correct program command line usage. */
    private static void usage() {
        System.out.println("\nUsage:\n\n" +
                "   java cMsgTestRcServer\n" +
                "        [-u <UDL>]           set UDL to start multicast domain server\n" +
                "        [-debug]             turn on printout\n" +
                "        [-h]                 print this help\n");
    }


    public static void main(String[] args) throws cMsgException {
        cMsgTestRcServer server = new cMsgTestRcServer(args);
        server.run();
    }


    public void run() throws cMsgException {

        System.out.println("Starting RC Multicast Domain test server");

        //---------------------------------------------------------------------------------------
        // RC Multicast domain UDL is of the form:
        //       cMsg:rcm://<udpPort>/<expid>?multicastTO=<timeout>
        //
        // The intial cMsg:rcm:// is stripped off by the top layer API
        //
        // Remember that for this domain:<p>
        //
        // 1) udp listening port is optional and defaults to MsgNetworkConstants.rcMulticastPort
        // 2) the experiment id is required If none is given, an exception is thrown
        // 3) the multicast timeout is in seconds and sets the time of sending out multicasts
        //     trying to locate other rc multicast servers already running on its port. Default
        //     is 2 seconds
        //---------------------------------------------------------------------------------------

        if (UDL == null)  UDL = "cMsg:rcm:///testExpid?multicastTO=1";

        // start up rc multicast server
        cMsg cmsg = new cMsg(UDL, "multicast listener", "udp trial");
        try {
            cmsg.connect();}
        catch (cMsgException e) {
            System.out.println(e.getMessage());
            return;
        }

        // create a callback for when a client connects to the rc multicast server
        MulticastCallback cb = new MulticastCallback();
        // subject and type are ignored in this domain
        cmsg.subscribe("sub", "type", cb, null);
        
        // enable message reception
        cmsg.start();


        // wait for first incoming message from rc client
        try { latch.await(); }
        catch (InterruptedException e) {}

        // That was the RC multicast domain part,
        // now we go on the RC server domain part.

        //---------------------------------------------------------------------------------------
        // RC Server domain UDL is of the form:
        //       cMsg:rcs://<host>:<tcpPort>?port=<udpPort>
        //
        // The intial cMsg:rcs:// is stripped off by the top layer API
        //
        // Remember that for this domain:
        // 1) host is NOT optional (must start with an alphabetic character according to "man hosts" or
        //    may be in dotted form (129.57.35.21)
        // 2) host can be "localhost"
        // 3) tcp port is optional and defaults to cMsgNetworkConstants.rcClientPort
        // 4) the udp port to listen on may be given by the optional port parameter.
        //    if it's not given, the system assigns one
        //---------------------------------------------------------------------------------------

        // Now that we have a message, we know what TCP host and port
        // to connect to in the RC Server domain.
        System.out.println("Starting RC Server Domain test server with UDL: ");
        System.out.println("    " + rcsUDL);

        //try {Thread.sleep(4000);}
        //catch (InterruptedException e) {}

        // Connect to the RC client that just connected with our RC multicast server,
        // and thereby conclude the client's complicated connection process.
        cMsg server = new cMsg(rcsUDL, "rc server", "udp trial");
        server.connect();
        server.start();

        // register callback to get messages from rc client
        rcCallback cb2 = new rcCallback();
        cMsgSubscriptionHandle unsub = server.subscribe("subby", "typey", cb2, null);

        System.out.println("wait 2 sec");
        try {Thread.sleep(2000);}
        catch (InterruptedException e) {}
        System.out.println("done waiting");

        // send stuff to rc client
        cMsgMessage msg = new cMsgMessage();
        msg.setSubject("rcSubject");
        msg.setType("rcType");

        server.send(msg);
        System.out.println("Sent msg (sub = rcSubject, typ = rcType) to rc client, wait 2 sec ...\n");
        try {Thread.sleep(2000);}
        catch (InterruptedException e) {}

        // Test the sendAndGet
        try {
            msg.setSubject("sAndGSubject");
            msg.setType("sAndGType");
            System.out.println("Call sendAndGet msg (sub = sAndGSubject, typ = sAndGType) to rc client");
            cMsgMessage response = server.sendAndGet(msg,4000);
            System.out.println("Response msg sub = " + response.getSubject() +
                                    ", type = " + response.getType() +
                                    ", text = " + response.getText());
            response.payloadPrintout(0);
        }
        catch (TimeoutException e) {
            System.out.println("TIMEOUT");
        }

        try {Thread.sleep(2000);}
        catch (InterruptedException e) {}

        server.unsubscribe(unsub);
        server.disconnect();
    }

}

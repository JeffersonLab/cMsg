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
import org.jlab.coda.cMsg.cMsgCallbackAdapter;
import org.jlab.coda.cMsg.cMsgCallbackInterface;

/**
 * This class implements a run control client that works with cMsgTextRcServer.
 */
public class cMsgTestRcClient {

    private cMsg cmsg;
    private boolean debug;
    private String UDL;


    /** Constructor. */
    cMsgTestRcClient(String[] args) {
        decodeCommandLine(args);
    }

    public static void main(String[] args) throws cMsgException {
        cMsgTestRcClient client = new cMsgTestRcClient(args);
        client.run();
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
                UDL= args[i + 1];
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
                "   java cMsgTestRcClient\n" +
                "        [-u <UDL>]           set UDL to connect to cMsg\n" +
                "        [-debug]             turn on printout\n" +
                "        [-h]                 print this help\n");
    }


    /**
     * This class defines the callback to be run when a message matching
     * our subscription arrives.
     */
    class myCallback extends cMsgCallbackAdapter {
        public void callback(cMsgMessage msg, Object userObject) {
            System.out.println("Got msg with sub = " + msg.getSubject() + ", typ = " + msg.getType() +
                    ", msg # = " + msg.getUserInt());
            msg.payloadPrintout(0);
        }
     }


    /** This class defines our callback object. */
    class sAndGCallback extends cMsgCallbackAdapter {
        public void callback(cMsgMessage msg, Object userObject) {

            if (!msg.isGetRequest()) {
                System.out.println("Callback received non-sendAndGet msg - ignoring");
                return;
            }

            try {
                cMsgMessage sendMsg;
                try {
                    System.out.println("Callback received sendAndGet msg (" + msg.getSubject() +
                            ", " + msg.getType() + ") - responding");
                    sendMsg = msg.response();
                    // Create Compound Payload
                    cMsgPayloadItem item = new cMsgPayloadItem("payloadItem", "any string you want");
                    sendMsg.addPayloadItem(item);
                }
                catch (cMsgException e) {
                    e.printStackTrace();
                    return;
                }
                sendMsg.setSubject("RESPONDING");
                sendMsg.setType("TO MESSAGE");
                sendMsg.setText("responder's text");
                // to send with UDP, uncomment following line
                // sendMsg.getContext().setReliableSend(false);
                cmsg.send(sendMsg);
            }
            catch (cMsgException e) {
                e.printStackTrace();
            }
        }
    }


    public void run() throws cMsgException {

        System.out.println("Starting RC domain test client");

       /* Runcontrol domain UDL is of the form:
        *        cMsg:rc://<host>:<port>/<expid>?multicastTO=<timeout>&connectTO=<timeout>
        *
        * Remember that for this domain:
        * 1) host is required and may also be "multicast", "localhost", or in dotted decimal form
        * 2) port is optional with a default of {@link cMsgNetworkConstants#rcMulticastPort}
        * 3) the experiment id or expid is required, it is NOT taken from the environmental variable EXPID
        * 4) multicastTO is the time to wait in seconds before connect returns a
        *       timeout when a rc multicast server does not answer
        * 5) connectTO is the time to wait in seconds before connect returns a
        *       timeout while waiting for the rc server to send a special (tcp)
        *       concluding connect message
        */
        if (UDL == null) UDL = "cMsg:rc://multicast/testExpid?connectTO=0";

        cmsg = new cMsg(UDL, "java rc client", "rc trial");

        // can find host of rc multicast server without connecting ...
        // cMsgMessage rcmMsg = cmsg.monitor(null);
        // System.out.println("Got msg with senderHost = " + rcmMsg.getSenderHost());

        cmsg.connect();
System.out.println("CONNECTED");

        // enable message reception
        cmsg.start();

        // subscribe to subject/type to receive from RC Server send
        cMsgCallbackInterface cb = new myCallback();
        cMsgSubscriptionHandle unsub = cmsg.subscribe("rcSubject", "rcType", cb, null);

        // subscribe to subject/type to receive from RC Server sendAndGet
        cMsgCallbackInterface cb2 = new sAndGCallback();
        cMsgSubscriptionHandle unsub2 = cmsg.subscribe("sAndGSubject", "sAndGType", cb2, null);

        try {
            Thread.sleep(1000);
        }
        catch (InterruptedException e) {
        }

        // send stuff to RC Server
        cMsgMessage msg = new cMsgMessage();
        msg.setSubject("subby");
        msg.setType("typey");
        msg.setText("Send with TCP");
        cMsgPayloadItem item = new cMsgPayloadItem("severity", "really severe");
        msg.addPayloadItem(item);

        System.out.println("Send subby, typey with TCP");
        cmsg.send(msg);

        msg.setText("Send with UDP");
        msg.setReliableSend(false);
        System.out.println("Send subby, typey with UDP");
        cmsg.send(msg);

        System.out.println("Sleep for 4 sec");
        try {
            Thread.sleep(4000);
        }
        catch (InterruptedException e) {
        }

        cmsg.stop();
        cmsg.unsubscribe(unsub);
        cmsg.unsubscribe(unsub2);
        cmsg.disconnect();

    }
}

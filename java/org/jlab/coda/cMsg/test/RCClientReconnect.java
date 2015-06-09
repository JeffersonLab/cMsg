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
 * This class implements a run control client that reconnects with a server that
 * comes and goes - RCMulticastServer.
 */
public class RCClientReconnect {

    int count;

    public static void main(String[] args) throws cMsgException {
         RCClientReconnect client = new RCClientReconnect();
         client.run();
     }

    /**
     * This class defines the callback to be run when a message matching
     * our subscription arrives.
     */
    class myCallback extends cMsgCallbackAdapter {
        public void callback(cMsgMessage msg, Object userObject) {
            System.out.println("Got msg " + msg.getUserInt() + " from server");
        }
     }


     public void run() throws cMsgException {

         System.out.println("Starting RC Client");

        /* Runcontrol domain UDL is of the form:<p>
         *   rc://host:port/expid?connectTO=<timeout>&ip=<address>
         *
         * Remember that for this domain:
         *
         *host is required and may also be "multicast", "localhost", or in dotted decimal form
         *port is optional with a default of 45200
         *the experiment id or expid is required, it is NOT taken from the environmental variable EXPID<p>
         *connectTO is the time to wait in seconds before connect returns a
         *       timeout while waiting for the rc server to send a special (tcp)
         *       concluding connect message. Defaults to 5 seconds
         *ip is the optional dot-decimal IP address which the rc server must use
         *       to connect to this rc client.
         */
         String UDL = "rc://multicast:45333/emutest?connectTO=5";

         cMsg cmsg = new cMsg(UDL, "java rc client", "rc trial");
         cmsg.connect();

         // enable message reception
         cmsg.start();

         // subscribe to subject/type to receive from RC Server
         cMsgCallbackInterface cb = new myCallback();
         cMsgSubscriptionHandle unsub = cmsg.subscribe("rcSubject", "rcType", cb, null);

         // send stuff to RC Server
         cMsgMessage msg = new cMsgMessage();
         msg.setSubject("subby");
         msg.setType("typey");
         msg.setText("Send with TCP");

         try {Thread.sleep(1000); }
         catch (InterruptedException e) {}

         int loops=5;
         while (loops-->0) {
             cmsg.send(msg);
         }

         msg.setText("Send with UDP");
         msg.setSubject("junk");
         msg.setType("junk");
         msg.setReliableSend(false);
         loops=5;
         while (loops-->0) {
             cmsg.send(msg);
         }

         try {Thread.sleep(7000); }
         catch (InterruptedException e) {}

         msg.setSubject("blah");
         msg.setType("yech");

         loops=5;
         while (loops-->0) {
             cmsg.send(msg);
         }

         msg.setText("Send with TCP");
         msg.setSubject("subby");
         msg.setType("typey");
         msg.setReliableSend(true);
         loops=5;
         while (loops-->0) {
             cmsg.send(msg);
         }

         try {Thread.sleep(10000); }
         catch (InterruptedException e) {}

         cmsg.stop();
         cmsg.unsubscribe(unsub);
         cmsg.disconnect();

     }
}

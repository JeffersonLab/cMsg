package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;

/**
 * This class implements a run control client.
 */
public class rcClient {

    int count;

    public static void main(String[] args) throws cMsgException {
         rcClient client = new rcClient();
         client.run();
     }

    /**
     * This class defines the callback to be run when a message matching
     * our subscription arrives.
     */
    class myCallback extends cMsgCallbackAdapter {
        /**
         * Callback method definition.
         *
         * @param msg message received from domain server
         * @param userObject object passed as an argument which was set when the
         *                   client orginally subscribed to a subject and type of
         *                   message.
         */
        public void callback(cMsgMessage msg, Object userObject) {
            System.out.println("Count = " + count++);
        }
     }


     public void run() throws cMsgException {

         System.out.println("Starting RC Client");

         /* Runcontrol domain UDL is of the form:
          *        cMsg:rc://<host>:<port>/?expid=<expid>&broadcastTO=<timeout>&connectTO=<timeout>
          *
          * Remember that for this domain:
          * 1) port is optional with a default of 6543 (cMsgNetworkConstants.rcBroadcastPort)
          * 2) host is optional with a default of 255.255.255.255 (broadcast)
          *    and may be "localhost" or in dotted decimal form
          * 3) the experiment id or expid is optional, it is taken from the
          *    environmental variable EXPID
          * 4) broadcastTO is the time to wait in seconds before connect returns a
          *    timeout when a rc broadcast server does not answer
          * 5) connectTO is the time to wait in seconds before connect returns a
          *    timeout while waiting for the rc server to send a special (tcp)
          *    concluding connect message
          */
         String UDL = "cMsg:rc://?expid=carlExp&broadcastTO=5&connectTO=5";

         cMsg cmsg = new cMsg(UDL, "java rc client", "rc trial");
         cmsg.connect();

         // enable message reception
         cmsg.start();

         // subscribe to subject/type to receive from RC Server
         cMsgCallbackInterface cb = new myCallback();
         Object unsub = cmsg.subscribe("rcSubject", "rcType", cb, null);

         // send stuff to RC Server
         cMsgMessage msg = new cMsgMessage();
         msg.setSubject("subby");
         msg.setType("typey");
         msg.setText("Send with TCP");
         int loops=5;
         while (loops-->0) {
             cmsg.send(msg);
         }

         msg.setText("Send with UDP");
         msg.getContext().setReliableSend(false);
         loops=5;
         while (loops-->0) {
             cmsg.send(msg);
         }

         try {Thread.sleep(5000); }
         catch (InterruptedException e) {}

         loops=5;
         while (loops-->0) {
             cmsg.send(msg);
         }

         msg.setText("Send with TCP");
         msg.getContext().setReliableSend(true);
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

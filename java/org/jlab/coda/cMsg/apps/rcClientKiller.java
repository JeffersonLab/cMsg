package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;

/**
 * Created by timmer on 6/5/15.
 */
public class rcClientKiller {

    /** RC multicast domain object. */
    private cMsg cmsg;

    class MulticastCallback extends cMsgCallbackAdapter {

        /**
         * This callback is called when a client is trying to connect to
         * this multicast server.
         *
         * @param msg {@inheritDoc}
         * @param userObject {@inheritDoc}
         */
        public void callback(cMsgMessage msg, Object userObject) {

            // In this (rcm) domain, the cMsg send() method sends an abort
            // message to the client associated with the msg of this method's
            // arg.
            try {
                cmsg.send(msg);
            }
            catch (cMsgException e) {
                e.printStackTrace();
            }

            System.out.println("Just sent abort to " + msg.getSender());
        }
    }


    public rcClientKiller() {
    }


    public static void main(String[] args) throws cMsgException {
        rcClientKiller server = new rcClientKiller();
        server.run();
    }


    public void run() throws cMsgException {

        System.out.println("Starting RC Client Killer");

        // RC Multicast domain UDL is of the form:
        //       [cMsg:]rcm://<udpPort>/<expid>?multicastTO=<timeout>
        //
        // Remember that for this domain:
        // 1) udp listening port is optional and defaults to MsgNetworkConstants.rcMulticastPort
        // 2) the experiment id is required If none is given, an exception is thrown
        // 3) the multicast timeout is in seconds and sets the time of sending out multicasts
        //    trying to locate other rc multicast servers already running on its port. Default
        //    is 2 seconds.

        String UDL = "rcm:///emutest";

        cmsg = new cMsg(UDL, "multicast listener", "udp trial");
        try {cmsg.connect();}
        catch (cMsgException e) {
            System.out.println(e.getMessage());
            return;
        }
        System.out.println("connected");

        // Install callback to kill all clients asking to connect.
        // Subject and type are ignored in this domain.
        MulticastCallback cb = new MulticastCallback();
        cmsg.subscribe("sub", "type", cb, null);
        System.out.println("subscribed");

        // Enable message reception
        cmsg.start();
        System.out.println("started");
        try {
            System.out.println("SLEEPING");
            Thread.sleep(100000);
        }
        catch (InterruptedException e) {
        }
    }

}

/*---------------------------------------------------------------------------*
*  Copyright (c) 2015        Jefferson Science Associates,                   *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    C.Timmer, Jun-2015, Jefferson Lab                                       *
*                                                                            *
*    Authors: Carl Timmer                                                    *
*             timmer@jlab.org                   Jefferson Lab, #10           *
*             Phone: (757) 269-5130             12000 Jefferson Ave.         *
*             Fax:   (757) 269-6248             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;


/**
 * This class simulates an rc multicast receiver, but then it commands all rc
 * clients that attempt to connect to abort/die. The reason this is necessary
 * is that occasionally there are "orphan" rc clients multicasting to find a
 * non-existent platform. Their constant multicasting uses resources of
 * existing multicast servers to filter out their useless packets. This
 * application can be used to get rid of such clients once and for all.<p>
 *
 * Once started, this app only hangs around for 100 seconds.
 */
public class rcClientKiller {

    /** RC multicast domain object. */
    private cMsg cmsg;

    /** Expid of this multicast server. */
    private String expid;

    /** Time in seconds to look for other multicast servers at this expid and port. */
    private int timeout;

    /** UDP port of multicast server. */
    private int udpPort = cMsgNetworkConstants.rcMulticastPort;



    /** Callback for handling RC client trying to connect. */
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



    /**
     * Constructor.
     * @param args arguments.
     */
    rcClientKiller(String[] args) {
        decodeCommandLine(args);
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
           else if (args[i].equalsIgnoreCase("-e")) {
                expid = args[i + 1];
                i++;
            }
            else if (args[i].equalsIgnoreCase("-p")) {
                udpPort = Integer.parseInt(args[i + 1]);
                if (udpPort < 1024 || udpPort > 65535) {
                    System.out.println("port must be > 1023 and < 65536");
                    continue;
                }
                i++;
            }
            else if (args[i].equalsIgnoreCase("-t")) {
                timeout = Integer.parseInt(args[i + 1]);
                i++;
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
            "   java rcClientKiller\n" +
            "        [-e <expid>]  set expid of this multicast server\n"+
            "        [-p <port>]   set UDP port of this multicast server\n" +
            "        [-t <time>]   set time in seconds to look for interfering multicast servers\n");
    }


    public static void main(String[] args) {
        try {
            rcClientKiller server = new rcClientKiller(args);
            server.run();
        }
        catch (cMsgException e) {
            System.out.println(e.toString());
            System.exit(-1);
        }
    }


    public void run() throws cMsgException {

        System.out.println("Starting RC Client Killer");

        // RC Multicast domain UDL is of the form:
        //       [cMsg:]rcm://<udpPort>/<expid>?multicastTO=<timeout>
        //
        // Remember that for this domain:
        // 1) udp listening port is optional and defaults to cMsgNetworkConstants.rcMulticastPort
        // 2) the experiment id is required If none is given, an exception is thrown
        // 3) the multicast timeout is in seconds and sets the time of sending out multicasts
        //    trying to locate other rc multicast servers already running on its port. Default
        //    is 2 seconds.

        String UDL = "rcm://" + udpPort + "/" + expid;
        if (timeout > 0) {
            UDL += "?multicastTO=" + timeout;
        }

        cmsg = new cMsg(UDL, "multicast listener", "udp trial");
        try {cmsg.connect();}
        catch (cMsgException e) {
            System.out.println(e.getMessage());
            return;
        }

        // Install callback to kill all clients asking to connect.
        // Subject and type are ignored in this domain.
        MulticastCallback cb = new MulticastCallback();
        cmsg.subscribe("sub", "type", cb, null);

        // Enable message reception
        cmsg.start();

        try {
            // Hang around for 100 seconds before quitting
            Thread.sleep(100000);
        }
        catch (InterruptedException e) {
        }
    }

}

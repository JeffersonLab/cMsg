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

/**
 * This class implements an RC Multicast server and RC Server server together.
 * The RC Server part dies and then comes back in order to test its ability
 * to reconnect to clients.
 */
public class RCMulticastServer {

    class MulticastCallback extends cMsgCallbackAdapter {
         public void callback(cMsgMessage msg, Object userObject) {
             String host = msg.getSenderHost();
             int    port = msg.getUserInt();
             String name = msg.getSender();
             String broadcast = "255.255.255.255";

             cMsgPayloadItem item = msg.getPayloadItem("IpAddresses");
             if (item != null) {
                 try {
                     host = (item.getStringArray())[0];
                 }
                 catch (cMsgException e) { }
             }

             item = msg.getPayloadItem("BroadcastAddresses");
             if (item != null) {
                 try {
                     broadcast = (item.getStringArray())[0];
                 }
                 catch (cMsgException e) { }
             }

             System.out.println("Running RC Multicast domain callback, host = " + host +
                                        ", bcast = " + broadcast +
                                        ", port = " + port +
                                        ", name = " + name);

             // Now that we have the message, we know what TCP host and port
             // to connect to in the RC Server domain.
             System.out.println("Starting RC Server domain server");

             RcServerReconnectThread rcserver  = new RcServerReconnectThread(host, broadcast, port);
             //ConnectDisconnectThread rcserver = new ConnectDisconnectThread(host, broadcast, port);
             rcserver.start();
        }
    }


    class rcCallback extends cMsgCallbackAdapter {
        public void callback(cMsgMessage msg, Object userObject) {
            String s = (String) userObject;
            System.out.println("Running regular cb, msg sub = " + msg.getSubject() +
                    ", type " + msg.getType() + ", text = " + msg.getText() + ", for " + s);
        }
    }

    class starCallback extends cMsgCallbackAdapter {
        public void callback(cMsgMessage msg, Object userObject) {
            String s = (String) userObject;
            System.out.println("Running star cb, msg sub = " + msg.getSubject() +
                    ", type " + msg.getType() + ", text = " + msg.getText() + ", for " + s);
        }
    }


    public RCMulticastServer() {
    }


    public static void main(String[] args) throws cMsgException {
        RCMulticastServer server = new RCMulticastServer();
        server.run();
    }


    public void run() throws cMsgException {

        System.out.println("Starting RC Multicast domain server");

        // RC Multicast domain UDL is of the form:
        //       [cMsg:]rcm://<udpPort>/<expid>?multicastTO=<timeout>
        //
        // The initial cMsg:rcm:// is stripped off by the top layer API
        //
        // Remember that for this domain:
        // 1) udp listening port is optional and defaults to MsgNetworkConstants.rcMulticastPort
        // 2) the experiment id is required If none is given, an exception is thrown
        // 3) the multicast timeout is in seconds and sets the time of sending out multicasts
        //    trying to locate other rc multicast servers already running on its port. Default
        //    is 2 seconds.

        String UDL = "cMsg:rcm://45333/emutest";

        cMsg cmsg = new cMsg(UDL, "multicast listener", "udp trial");
        try {cmsg.connect();}
        catch (cMsgException e) {
            System.out.println(e.getMessage());
            return;
        }

        // enable message reception
        cmsg.start();

        MulticastCallback cb = new MulticastCallback();
        // subject and type are ignored in this domain
        cmsg.subscribe("sub", "type", cb, null);


        try {
            Thread.sleep(120000);
        }
        catch (InterruptedException e) {
        }

    }


    /**
     * Class for handling client which does endless connects and disconnects.
     */
    class ConnectDisconnectThread extends Thread {

        String rcClientHost;
        String rcClientBroadcast;
        int rcClientTcpPort;

        ConnectDisconnectThread(String ip, String bcast, int port) {
            rcClientHost = ip;
            rcClientBroadcast = bcast;
            rcClientTcpPort = port;
        }

        public void run() {
            try {
                String rcsUDL = "cMsg:rcs://" + rcClientHost + ":" + rcClientTcpPort +
                                "/" + rcClientBroadcast;

                cMsg server = new cMsg(rcsUDL, "rc server", "connect/disconnect trial");
//System.out.println("connect");
                server.connect();
                try {Thread.sleep(200);}
                catch (InterruptedException e) {}
                server.disconnect();
            }
            catch (cMsgException e) {
                e.printStackTrace();
            }
        }

    }

    /**
     * Class for client testing message sending after server disconnects then reconnects.
     */
    class RcServerReconnectThread extends Thread {

        String rcClientHost;
        String rcClientBroadcast;
        int rcClientTcpPort;

        RcServerReconnectThread(String host, String bcast, int port) {
            rcClientHost = host;
            rcClientBroadcast = bcast;
            rcClientTcpPort = port;
        }

        public void run() {
            try {

                // RC Server domain UDL is of the form:
                //       cMsg:rcs://<host>:<tcpPort>?port=<udpPort>
                //
                // The intial cMsg:rcs:// is stripped off by the top layer API
                //
                // Remember that for this domain:
                // 1) host is NOT optional (must start with an alphabetic character according to "man hosts" or
                //    may be in dotted form (129.57.35.21)
                // 2) host can be "localhost"
                // 3) tcp port is optional and defaults to 7654 (cMsgNetworkConstants.rcClientPort)
                // 4) the udp port to listen on may be given by the optional port parameter.
                //    if it's not given, the system assigns one

                String rcsUDL = "cMsg:rcs://" + rcClientHost + ":" + rcClientTcpPort +
                                 "/" + rcClientBroadcast;

                cMsg server = new cMsg(rcsUDL, "rc server", "udp trial");
                server.connect();
                server.start();

                rcCallback cb2 = new rcCallback();
                cMsgSubscriptionHandle unsub = server.subscribe("subby", "typey", cb2, "1st sub");

                starCallback starCb = new starCallback();
                cMsgSubscriptionHandle unsub2 = server.subscribe("*", "*", starCb, "2nd sub");

                // send stuff to rc client
                cMsgMessage msg = new cMsgMessage();
                msg.setSubject("rcSubject");
                msg.setType("rcType");

                int loops = 5;
                while(loops-- > 0) {
                    System.out.println("Send msg " +  (loops+1) + " to rc client");
                    msg.setUserInt(loops+1);
                    try {Thread.sleep(500);}
                    catch (InterruptedException e) {}
                    server.send(msg);
                }
                server.unsubscribe(unsub);
                server.unsubscribe(unsub2);
                server.disconnect();

                // test reconnect feature
                System.out.println("\n\n\nNow wait 2 seconds and connect again\n\n\n");
                try {Thread.sleep(2000);}
                catch (InterruptedException e) {}

                cMsg server2 = new cMsg(rcsUDL, "rc server", "udp trial");
                server2.connect();
                server2.start();

                server2.subscribe("subby", "typey", cb2, "3rd sub");
                server2.subscribe("*", "*", starCb, "4th sub");

                loops = 5;
                while(loops-- > 0) {
                    System.out.println("Send msg " +  (loops+1) + " to rc client");
                    msg.setUserInt(loops+1);
                    server2.send(msg);
                    try {Thread.sleep(500);}
                    catch (InterruptedException e) {}
                }
            }
            catch (cMsgException e) {
                e.printStackTrace();
            }
        }

    }


}

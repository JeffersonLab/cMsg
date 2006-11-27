/*----------------------------------------------------------------------------*
 *  Copyright (c) 2006        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 9-May-2006, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.RCBroadcastDomain;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.cMsgDomain.client.cMsgGetHelper;
import org.jlab.coda.cMsg.cMsgDomain.client.cMsgCallbackThread;

import java.net.DatagramSocket;
import java.net.DatagramPacket;
import java.net.InetAddress;
import java.net.SocketException;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.util.Date;
import java.util.Set;

/**
 * This class implements a thread to listen to runcontrol clients in the
 * runcontrol broadcast domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class rcListeningThread extends Thread {

    /** This domain's name. */
    private String domainType = "rcb";

    /** RC broadcast server that created this object. */
    private RCBroadcast server;

    /** UDP port on which to listen for rc client broad/unicasts. */
    int broadcastPort;

    /** UDP socket on which to read packets sent from rc clients. */
     DatagramSocket broadcastSocket;

    /** Level of debug output for this class. */
    private int debug;

    /** Setting this to true will kill this thread. */
    private boolean killThread;

    /** Kills this thread. */
    void killThread() {
        killThread = true;
        this.interrupt();
        broadcastSocket.close();
    }


    /**
     * Converts 4 bytes of a byte array into an integer.
     *
     * @param b   byte array
     * @param off offset into the byte array (0 = start at first element)
     * @return integer value
     */
    private static final int bytesToInt(byte[] b, int off) {
        int result = ((b[off] & 0xff) << 24)     |
                     ((b[off + 1] & 0xff) << 16) |
                     ((b[off + 2] & 0xff) << 8)  |
                      (b[off + 3] & 0xff);
        return result;
    }


    /**
     * Constructor.
     * @param server rc server that created this object
     * @param port udp port on which to receive transmissions from rc clients
     */
    public rcListeningThread(RCBroadcast server, int port) throws cMsgException {

        // Create a UDP socket for accepting broad/unicasts from the RC client.
        broadcastPort = port;
        try {
//System.out.println("Creating UDP socket at port " + broadcastPort);
            broadcastSocket = new DatagramSocket(broadcastPort);
        }
        catch (SocketException e) {
            throw new cMsgException(e.getMessage());
        }
        this.server = server;
        debug = server.debug;
        // die if no more non-daemon thds running
        setDaemon(true);
    }


    /** This method is executed as a thread. */
    public void run() {

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("Running RC Broadcast Listening Thread");
        }

        // create a packet to be written into
        byte[] buf = new byte[2048];
        DatagramPacket packet = new DatagramPacket(buf, 2048);

        // listen for broadcasts and interpret packets
        try {
            while (true) {
                if (killThread) { return; }
                packet.setLength(2048);
                broadcastSocket.receive(packet);
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("RECEIVED BROADCAST PACKET !!!");
                }

                if (killThread) { return; }

                // pick apart byte array received
                InetAddress clientAddress = packet.getAddress();
                String rcClientHost = clientAddress.getCanonicalHostName();
                int rcClientUdpPort = packet.getPort();   // port to send response packet to
                int msgType         = bytesToInt(buf, 0); // what type of broadcast is this ?

                // ignore broadcasts from unknown sources
                if (msgType != cMsgNetworkConstants.rcDomainBroadcast) {
                    continue;
                }

                int rcClientTcpPort = bytesToInt(buf, 4); // tcp listening port
                int nameLen         = bytesToInt(buf, 8); // length of client name (# chars)
                int expidLen        = bytesToInt(buf, 12); // length of expid (# chars)

                // rc client's name
                String clientName = null;
                try {
                    clientName = new String(buf, 16, nameLen, "US-ASCII");
                }
                catch (UnsupportedEncodingException e) {}

                // rc client's EXPID
                String clientExpid = null;
                try {
                    clientExpid = new String(buf, 16+nameLen, expidLen, "US-ASCII");
                }
                catch (UnsupportedEncodingException e) {}

                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("clientHost = " + rcClientHost + ", UDP port = " + rcClientUdpPort +
                        ", TCP port = " + rcClientTcpPort + ", name = " + clientName +
                        ", expid = " + clientExpid);
                }

                // Check for conflicting expid's
                if (!server.expid.equalsIgnoreCase(clientExpid)) {
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("Conflicting EXPID's, ignoring");
                    }
                    continue;
                }

                // If expid's match, pass on messgage to subscribes and/or subscribeAndGets
                cMsgMessageFull msg = new cMsgMessageFull();
                msg.setSenderHost(rcClientHost);
                msg.setUserInt(rcClientTcpPort);
                msg.setSender(clientName);
                msg.setDomain(domainType);
                msg.setReceiver(server.getName());
                msg.setReceiverHost(server.getHost());
                msg.setReceiverTime(new Date()); // current time

                // Send a reply to broad/unicast. This can be a blank packet since
                // all we want to communicate is that the client was heard and can
                // now stop broadcasting
                try {
                    // create packet to respond to broadcast
                    DatagramPacket pkt = new DatagramPacket(buf, 0, clientAddress, rcClientUdpPort);
//System.out.println("Send reponse packet");
                    broadcastSocket.send(pkt);
                }
                catch (IOException e) {
//System.out.println("I/O Error: " + e);
                }

                // run callbacks for this message
                runCallbacks(msg);
            }

        }
        catch (IOException e) {
            if (debug >= cMsgConstants.debugError) {
                System.out.println("rcBroadcastListenThread: I/O ERROR in rc broadcast server");
                System.out.println("rcBroadcastListenThread: close broadcast socket, port = " + broadcastSocket.getLocalPort());
            }

            // We're here if there is an IO error.
            // Disconnect the server (kill this thread).
            broadcastSocket.close();
        }

        return;
    }


    /**
     * This method runs all callbacks - each in their own thread -
     * for server subscribe and subscribeAndGet calls. In this domain
     * there is no matching of subject and type, all messages are sent
     * to all callbacks.
     *
     * @param msg incoming message
     */
    private void runCallbacks(cMsgMessageFull msg) {

        // handle subscribeAndGets
        Set<cMsgGetHelper> set1 = server.subscribeAndGets;

        synchronized (set1) {
            if (set1.size() > 0) {
                // for each subscribeAndGet called by this server ...
                for (cMsgGetHelper helper : set1) {
                    helper.setTimedOut(false);
                    helper.setMessage(msg.copy());
                    // Tell the subscribeAndGet-calling thread to wakeup
                    // and retrieve the held msg
                    synchronized (helper) {
                        helper.notify();
                    }
                }
                server.subscribeAndGets.clear();
            }
        }

        // handle subscriptions
        Set<cMsgSubscription> set2 = server.subscriptions;

        synchronized (set2) {
            if (set2.size() > 0) {
                // if callbacks have been stopped, return
                if (!server.isReceiving()) {
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("runCallbacks: all subscription callbacks have been stopped");
                    }
                    return;
                }

                // set is NOT modified here
                // for each subscription of this server ...
                for (cMsgSubscription sub : set2) {
                    // run through all callbacks
                    for (cMsgCallbackThread cbThread : sub.getCallbacks()) {
                        // The callback thread copies the message given
                        // to it before it runs the callback method on it.
                        cbThread.sendMessage(msg);
                    }
                }
            }
        }

    }


}

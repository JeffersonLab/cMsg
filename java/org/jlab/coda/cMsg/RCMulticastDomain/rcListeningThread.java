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

package org.jlab.coda.cMsg.RCMulticastDomain;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgCallbackThread;
import org.jlab.coda.cMsg.common.cMsgSubscription;
import org.jlab.coda.cMsg.common.cMsgMessageFull;

import java.net.*;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.util.Date;
import java.util.Set;
import java.util.Enumeration;

/**
 * This class implements a thread to listen to runcontrol clients in the
 * runcontrol multicast domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
class rcListeningThread extends Thread {

    /** This domain's name. */
    private String domainType = "rcb";

    /** RC multicast server that created this object. */
    private RCMulticast server;

    /** UDP port on which to listen for rc client multi/unicasts. */
    private int multicastPort;

    /** UDP socket on which to read packets sent from rc clients. */
    private MulticastSocket multicastSocket;

    /** Level of debug output for this class. */
    private int debug;

    /** Setting this to true will kill this thread. */
    private volatile boolean killThread;

    /** Kills this thread. */
    void killThread() {
        killThread = true;
        this.interrupt();
    }



    /**
     * Constructor.
     * @param server rc server that created this object
     * @param port udp port on which to receive transmissions from rc clients
     */
    public rcListeningThread(RCMulticast server, int port) throws cMsgException {

        try {
            // Create a UDP socket for accepting multi/unicasts from the RC client.
            multicastPort = port;
            multicastSocket = new MulticastSocket(multicastPort);
            // Be sure to join the multicast addr group on each interface
            // (something not mentioned in any javadocs or books!).
            SocketAddress sa;
            Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();
            while (enumer.hasMoreElements()) {
                sa = new InetSocketAddress(InetAddress.getByName(cMsgNetworkConstants.rcMulticast), multicastPort);
                multicastSocket.joinGroup(sa, enumer.nextElement());
            }
            multicastSocket.setReceiveBufferSize(65535);
            multicastSocket.setReuseAddress(true);
            multicastSocket.setTimeToLive(32);
        }
        catch (IOException e) {
            throw new cMsgException("Port " + multicastPort + " is taken", e);
        }
        this.server = server;
        debug = server.getDebug();
        // die if no more non-daemon thds running
        setDaemon(true);
    }


    /** This method is executed as a thread. */
    public void run() {

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("Running RC Multicast Listening Thread");
        }

        // create a packet to be written into from client
        byte[] buf = new byte[2048];
        DatagramPacket packet = new DatagramPacket(buf, 2048);

        // prepare to create a packet to be send back to the client
        byte[] outBuf = null;
        DatagramPacket sendPacket  = null;
        ByteArrayOutputStream baos = new ByteArrayOutputStream(1024);
        DataOutputStream out       = new DataOutputStream(baos);

        try {
            // Put our special #s, UDP listening port, host, & expid into byte array
            out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
            out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
            out.writeInt(cMsgNetworkConstants.magicNumbers[2]);
            out.writeInt(multicastPort);
            out.writeInt(server.getHost().length());
            out.writeInt(server.expid.length());
            try {
                out.write(server.getHost().getBytes("US-ASCII"));
                out.write(server.expid.getBytes("US-ASCII"));
            }
            catch (UnsupportedEncodingException e) { }
            out.flush();
            out.close();

            // create buffer to multicast from the byte array
            outBuf = baos.toByteArray();
            baos.close();
        }
        catch (IOException e) {
            if (debug >= cMsgConstants.debugError) {
                System.out.println("I/O Error: " + e);
            }
        }

        // server object is waiting for this thread to start in connect method,
        // so tell it we've started.
        synchronized (this) {
            notifyAll();
        }

        // listen for multicasts and interpret packets
        try {
            while (true) {
                if (killThread) { return; }
                packet.setLength(2048);
                multicastSocket.receive(packet);
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("     ***** RECEIVED RC DOMAIN MULTICAST PACKET !!!");
                }

                if (killThread) { return; }

                // pick apart byte array received
                InetAddress multicasterAddress = packet.getAddress();
                String multicasterHost = multicasterAddress.getCanonicalHostName();
                int multicasterUdpPort = packet.getPort();   // port to send response packet to

                if (packet.getLength() < 4*4) {
                    if (debug >= cMsgConstants.debugWarn) {
                        System.out.println("got multicast packet that's too small");
                    }
                    continue;
                }

                int magic1  = cMsgUtilities.bytesToInt(buf, 0);
                int magic2  = cMsgUtilities.bytesToInt(buf, 4);
                int magic3  = cMsgUtilities.bytesToInt(buf, 8);
                if (magic1 != cMsgNetworkConstants.magicNumbers[0] ||
                    magic2 != cMsgNetworkConstants.magicNumbers[1] ||
                    magic3 != cMsgNetworkConstants.magicNumbers[2])  {
                    if (debug >= cMsgConstants.debugWarn) {
                        System.out.println("got multicast packet with bad magic #s");
                    }
                    continue;
                }

                int msgType = cMsgUtilities.bytesToInt(buf, 12); // what type of message is this ?

                switch (msgType) {
                    // multicasts from rc clients
                    case cMsgNetworkConstants.rcDomainMulticastClient:
//System.out.println("Client wants to connect");
                        break;
                    // multicasts from rc servers
                    case cMsgNetworkConstants.rcDomainMulticastServer:
//System.out.println("Server wants to connect");
                        break;
                    // kill this server since one already exists on this port/expid
                    case cMsgNetworkConstants.rcDomainMulticastKillSelf:
System.out.println("RC multicast server : I was told to kill myself by another multicast server");
                        server.respondingHost = multicasterHost;
                        server.multicastResponse.countDown();
                        return;
                    // Packet from client just trying to locate rc multicast servers.
                    // Send back a normal response but don't do anything else.
                    case cMsgNetworkConstants.rcDomainMulticastProbe:
//System.out.println("I was probed");
                        break;
                    // ignore packets from unknown sources
                    default:
//System.out.println("Unknown command");
                        continue;
                }

                int multicasterTcpPort = cMsgUtilities.bytesToInt(buf, 16); // tcp listening port
                int nameLen            = cMsgUtilities.bytesToInt(buf, 20); // length of sender's name (# chars)
                int expidLen           = cMsgUtilities.bytesToInt(buf, 24); // length of expid (# chars)

                // sender's name
                String multicasterName = null;
                try {
                    multicasterName = new String(buf, 28, nameLen, "US-ASCII");
                }
                catch (UnsupportedEncodingException e) {}

                // sender's EXPID
                String multicasterExpid = null;
                try {
                    multicasterExpid = new String(buf, 28+nameLen, expidLen, "US-ASCII");
                }
                catch (UnsupportedEncodingException e) {}

                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("multicaster's host = " + multicasterHost + ", UDP port = " + multicasterUdpPort +
                        ", TCP port = " + multicasterTcpPort + ", name = " + multicasterName +
                        ", expid = " + multicasterExpid);
                }

                // Check for conflicting expid's
                if (!server.expid.equalsIgnoreCase(multicasterExpid)) {
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("Conflicting EXPID's, ignoring");
                    }
                    continue;
                }

                // Before sending a reply, check to see if we simply got a packet
                // from ourself when first connecting. Just ignore our own probing
                // multicast.

//                System.out.println("accepting Clients = " + server.acceptingClients);
//                System.out.println("our host = " + InetAddress.getLocalHost().getCanonicalHostName());
//                System.out.println("multicaster's host = " + multicasterHost);
//                System.out.println("our port = " + server.localTempPort);
//                System.out.println("multicaster's port = " + multicasterUdpPort);

                if (/*!server.acceptingClients &&*/ // doesn't matter if we're accepting clients or not ...
                        InetAddress.getLocalHost().getCanonicalHostName().equals(multicasterHost) &&
                        multicasterUdpPort == server.localTempPort) {
System.out.println("RC multicast server : ignore my own udp messages");
                    continue;
                }

                // if multicast from client ...
                if (msgType == cMsgNetworkConstants.rcDomainMulticastProbe) {
                    try {
                        sendPacket = new DatagramPacket(outBuf, outBuf.length, multicasterAddress, multicasterUdpPort);
//System.out.println("Send response-to-probe packet to client");
                        multicastSocket.send(sendPacket);
                        continue;
                    }
                    catch (IOException e) {
                        System.out.println("I/O Error: " + e);
                    }
                }
                // if multicast from client ...
                else if (msgType == cMsgNetworkConstants.rcDomainMulticastClient) {
                    // Send a reply - some integers, our multicast port, host,
                    // and expid so the client can filter out any rogue responses.
                    // All we want to communicate is that the client was heard and
                    // can now stop multicasting.
                    // HOWEVER, we cannot send a reply (and have the clients stop muliticasting
                    // and looking for the server) and NOT have an active subscription waiting on
                    // this end to process the client's request. So before we accept a client, make
                    // sure we are able to process the connection.
                    if (!server.acceptingClients || !server.hasSubscription || !server.isReceiving()) {
//System.out.println("Server is not accepting clients right now, ignore multicast");
                        continue;
                    }

                    try {
                        sendPacket = new DatagramPacket(outBuf, outBuf.length, multicasterAddress, multicasterUdpPort);
//System.out.println("Send response packet to client");
                        multicastSocket.send(sendPacket);
                    }
                    catch (IOException e) {
                        System.out.println("I/O Error: " + e);
                    }
                }
                // else if multicast from server ...
                else {
                    // Other RCMulticast servers send "feelers" just trying see if another
                    // RCMulticast server is on the same port with the same EXPID. Don't
                    // send this on as a message to subscriptions.
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("Another RCMulticast server probing this one");
                    }

                    // if this server was properly started, tell the one probing us to kill itself
                    if (server.acceptingClients) {
                        // create packet to respond to multicast
                        cMsgUtilities.intToBytes(cMsgNetworkConstants.magicNumbers[0], buf, 0);
                        cMsgUtilities.intToBytes(cMsgNetworkConstants.magicNumbers[1], buf, 4);
                        cMsgUtilities.intToBytes(cMsgNetworkConstants.magicNumbers[2], buf, 8);
                        cMsgUtilities.intToBytes(cMsgNetworkConstants.rcDomainMulticastKillSelf, buf, 12);
                        DatagramPacket pkt = new DatagramPacket(buf, 16, multicasterAddress, server.udpPort);
//System.out.println("Send reponse packet (kill yourself) to server");
                        multicastSocket.send(pkt);
                    }
                    else {
//System.out.println("Still starting up but have been probed by starting server. So quit");
                        server.respondingHost = multicasterHost;
                        server.multicastResponse.countDown();
                    }
                    continue;
                }
//System.out.println("Pass msg on to subscriptions");
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("Client " + multicasterName + " is now connected");
                }

                // If expid's match, pass on messgage to subscribes and/or subscribeAndGets
                cMsgMessageFull msg = new cMsgMessageFull();
                msg.setSenderHost(multicasterHost);
                msg.setUserInt(multicasterTcpPort);
                msg.setSender(multicasterName);
                msg.setDomain(domainType);
                msg.setReceiver(server.getName());
                msg.setReceiverHost(server.getHost());
                msg.setReceiverTime(new Date()); // current time

                // run callbacks for this message
                runCallbacks(msg);
            }

        }
        catch (IOException e) {
            if (debug >= cMsgConstants.debugError) {
                System.out.println("rcMulticastListenThread: I/O ERROR in rc multicast server");
                System.out.println("rcMulticastListenThread: close multicast socket, port = " + multicastSocket.getLocalPort());
            }
        }
        finally {
            if (!multicastSocket.isClosed())  multicastSocket.close();
        }

        return;
    }


    /**
     * This method runs all callbacks - each in their own thread - for server subscribe calls.
     * In this domain there is no matching of subject and type, all messages are sent to all callbacks.
     *
     * @param msg incoming message
     */
    private void runCallbacks(cMsgMessageFull msg) {

        // handle subscriptions
        Set<cMsgSubscription> set = server.subscriptions;

        synchronized (set) {
            if (set.size() > 0) {
                // if callbacks have been stopped, return
                if (!server.isReceiving()) {
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("runCallbacks: all subscription callbacks have been stopped");
                    }
                    return;
                }

                // set is NOT modified here
                // for each subscription of this server ...
                for (cMsgSubscription sub : set) {
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

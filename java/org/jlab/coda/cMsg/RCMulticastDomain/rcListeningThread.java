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
import java.util.*;

/**
 * This class implements a thread to listen to runcontrol clients in the
 * runcontrol multicast domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
class rcListeningThread extends Thread {

    /** This domain's name. */
    private String domainType = "rcm";

    /** RC multicast server that created this object. */
    private RCMulticast server;

    /** UDP port on which to listen for rc client multi/unicasts. */
    private int multicastPort;

    /** UDP socket on which to read packets sent from rc clients. */
    private MulticastSocket multicastSocket;

    /** Network interface on which this thread is listening. */
    private NetworkInterface networkInterface;

    /** Level of debug output for this class. */
    private int debug;

    /** Setting this to true will kill this thread. */
    private volatile boolean killThread;

    /** Kills this thread. */
    void killThread() {
        killThread = true;
        this.interrupt();
    }

    private void printNI(NetworkInterface ni) {
        System.out.println("\n\nInterface name = " + ni.getName());
        System.out.println("Interface display name = " + ni.getDisplayName());

        int counter = 1;

        List<InterfaceAddress> inAddrs  = ni.getInterfaceAddresses();
        for (InterfaceAddress ifAddr : inAddrs) {
            System.out.println("interface address #" + counter++ + ":");
            InetAddress addr = ifAddr.getAddress();
            System.out.println("    host address = " + addr.getHostAddress());
            System.out.println("    canonical host name = " + addr.getCanonicalHostName());
            System.out.println("    host name = " + addr.getHostName());
            System.out.println("    toString() = " + addr.toString());
            byte b[] = addr.getAddress();
            for (int i=0; i<b.length; i++) {
                int bb = b[i] & 0xff;
                System.out.println("addr byte = " + bb + ", 0x" + Integer.toHexString(bb));
            }

            InetAddress baddr = ifAddr.getBroadcast();
            if (baddr != null)
                System.out.println("broadcast addr = " + baddr.getHostAddress());

        }
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
            SocketAddress sa =
                new InetSocketAddress(InetAddress.getByName(cMsgNetworkConstants.rcMulticast), multicastPort);
            // Be sure to join the multicast address group of all network interfaces
            // (something not mentioned in any javadocs or books!).
            Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();
//System.out.println("Join multicast address group of these interfaces:");
            while (enumer.hasMoreElements()) {
                NetworkInterface ni = enumer.nextElement();
                if (ni.isUp()) {
                    try {
                        multicastSocket.joinGroup(sa, ni);
//printNI(ni);
                    }
                    catch (IOException e) {
                        System.out.println("Error joining multicast group over " + ni.getName());
                    }
                }
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
            out.writeInt(cMsgConstants.version);
            out.writeInt(multicastPort);
            out.writeInt(server.getHost().length());
            out.writeInt(server.expid.length());
            try {
                out.write(server.getHost().getBytes("US-ASCII"));
                out.write(server.expid.getBytes("US-ASCII"));
            }
            catch (UnsupportedEncodingException e) {/*never happen */}

            List<InterfaceAddress> ipInfo = cMsgUtilities.getAllIpInfo();
            out.writeInt(ipInfo.size());

            try {
                for (InterfaceAddress ia : ipInfo) {
                    String ip = ia.getAddress().getHostAddress();
                    out.writeInt(ip.length());
                    out.write(ip.getBytes("US-ASCII"));
                    ip = ia.getBroadcast().getHostAddress();
                    out.writeInt(ip.length());
                    out.write(ip.getBytes("US-ASCII"));
                }
            }
            catch (UnsupportedEncodingException e) {/*never happen */}

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
                multicastSocket.receive(packet);   // blocks
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("     ***** RECEIVED RC DOMAIN MULTICAST PACKET from " + packet.getAddress().getHostName());
                }

                if (killThread) { return; }

                // pick apart byte array received
                InetAddress multicasterAddress = packet.getAddress();
                String multicasterHost = multicasterAddress.getHostName();
                int multicasterUdpPort = packet.getPort();   // port to send response packet to

                if (packet.getLength() < 4*4) {
                    if (debug >= cMsgConstants.debugWarn) {
                        System.out.println("RC multicast listener: got multicast packet that's too small");
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
                        System.out.println("RC multicast listener: got multicast packet with bad magic #s");
                    }
                    continue;
                }

                // what version of cMsg?
                int version = cMsgUtilities.bytesToInt(buf, 12);
                if (version != cMsgConstants.version) {
                    if (debug >= cMsgConstants.debugWarn) {
                        System.out.println("RC multicast listener: got cMsg packet version " + version +
                        " which does NOT match version " + cMsgConstants.version + ", so ignore");
                    }
                    continue;
                }

                // what type of message is this ?
                int msgType = cMsgUtilities.bytesToInt(buf, 16);

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
//System.out.println("RC multicast listener: told to kill myself by another multicast server, ignore for now");
//                        server.respondingHost = multicasterHost;
//                        server.multicastResponse.countDown();
//                        return;
                        continue;
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

                int multicasterTcpPort = cMsgUtilities.bytesToInt(buf, 20); // tcp listening port
                int senderId           = cMsgUtilities.bytesToInt(buf, 24); // unique id #
                int nameLen            = cMsgUtilities.bytesToInt(buf, 28); // length of sender's name (# chars)
                int expidLen           = cMsgUtilities.bytesToInt(buf, 32); // length of expid (# chars)
                int pos = 36;

                // sender's name
                String multicasterName = null;
                try {
                    multicasterName = new String(buf, pos, nameLen, "US-ASCII");
                    pos += nameLen;
                }
                catch (UnsupportedEncodingException e) {}

                // sender's EXPID
                String multicasterExpid = null;
                try {
                    multicasterExpid = new String(buf, pos, expidLen, "US-ASCII");
                    pos += expidLen;
                }
                catch (UnsupportedEncodingException e) {}

                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("RC multicast listener: multicaster's host = " +
                        multicasterHost + ", UDP port = " + multicasterUdpPort +
                        ", TCP port = " + multicasterTcpPort + ", name = " + multicasterName +
                        ", expid = " + multicasterExpid + ", this server udp = " + server.udpPort +
                        ", is local host = " + cMsgUtilities.isHostLocal(multicasterHost) +
                        ", version = " + version + ", id # = " + senderId);
                }

                // If not a probe, ignore packets with conflicting expids
                if (!server.expid.equalsIgnoreCase(multicasterExpid) &&
                    msgType != cMsgNetworkConstants.rcDomainMulticastProbe) {
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("RC multicast listener: conflicting EXPID's, ignoring");
                    }
                    continue;
                }

                // Probes from cMsgServerFinder   have port = 1
                // Probes from rcClient.monitor() have port = 0

                // Before sending a reply, check to see if we simply got a packet
                // from our self when first connecting. Just ignore our own probing
                // multicast.
                // If the ephemeral port this server uses to send multicasts to other servers
                // when starting up == the packet's udp port, then we can be fairly sure that
                // this packet comes from this very server.
                if (msgType == cMsgNetworkConstants.rcDomainMulticastServer &&
                    multicasterUdpPort == server.localTempPort) {
//System.out.println("RC multicast listener : ignore own start-up udp messages");
                    continue;
                }

                ArrayList<String> ipList, broadList;
                int packetCounter=1;
//                int fixedIp;

                // if probe from client ...
                if (msgType == cMsgNetworkConstants.rcDomainMulticastProbe) {
                    try {
//if (multicasterTcpPort == 1) {
//    System.out.println("RC multicast listener: got probe from cMsgFindServers");
//}
//else if (multicasterTcpPort == 0) {
//    System.out.println("RC multicast listener: got probe from client monitor()");
//}
                        sendPacket = new DatagramPacket(outBuf, outBuf.length, multicasterAddress, multicasterUdpPort);
//System.out.println("RC multicast listener: send probe response to " + multicasterName + " on " + multicasterHost +
//", expid = " + server.expid);
                        multicastSocket.send(sendPacket);
                    }
                    catch (IOException e) {
                        System.out.println("I/O Error: " + e);
                    }
                    continue;
                }
                // if connect request from client ...
                else if (msgType == cMsgNetworkConstants.rcDomainMulticastClient) {
                    // Read additional data now sent - list of all IP & broadcast addrs

                    // # of address pairs to follow
                    int listLen = cMsgUtilities.bytesToInt(buf, pos);
                    pos += 4;
//                    System.out.println("   list len = " + listLen);
//                    System.out.println("   list len = 0x" + Integer.toHexString(listLen));
//                    System.out.println("   list len swap = 0x" +
//                            Integer.toHexString(Integer.reverseBytes(listLen)));
                    String ss;
                    int stringLen;
                    ipList    = new ArrayList<String>(listLen);
                    broadList = new ArrayList<String>(listLen);

                    for (int i=0; i < listLen; i++) {
                        try {
                            stringLen = cMsgUtilities.bytesToInt(buf, pos); pos += 4;
//System.out.println("     ip len = " + listLen);
                            ss = new String(buf, pos, stringLen, "US-ASCII");
//System.out.println("     ip = " + ss);
                            ipList.add(ss);
                            pos += stringLen;

                            stringLen = cMsgUtilities.bytesToInt(buf, pos); pos += 4;
//System.out.println("     broad len = " + listLen);
                            ss = new String(buf, pos, stringLen, "US-ASCII");
//System.out.println("     broad = " + ss);
                            broadList.add(ss);
                            pos += stringLen;
                        }
                        catch (UnsupportedEncodingException e) {/*never happen */}
                    }

                    packetCounter = cMsgUtilities.bytesToInt(buf, pos);
System.out.println("RC multicast listener: got client packet #" + packetCounter + " from " + multicasterName);

                    // We must have an active subscription waiting on
                    // this end to process the client's request.
                    // So before we accept a client, make
                    // sure we are able to process the connection.
                    if (!server.acceptingClients || !server.hasSubscription || !server.isReceiving()) {
//System.out.println("RC multicast listener: server not accepting clients, ignore multicast");
                        continue;
                    }
                }
                // else if from server ...
                else {
                    // Other RCMulticast servers send "feelers" just trying see if another
                    // RCMulticast server is on the same port with the same EXPID. Don't
                    // send this on as a message to subscriptions.
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("RC multicast listener: another RC multicast server probing this one");
                    }

                    // if this server was properly started, tell the one probing us to kill itself
                    if (server.acceptingClients) {
                        // create packet to respond to multicast
                        cMsgUtilities.intToBytes(cMsgNetworkConstants.magicNumbers[0], buf, 0);
                        cMsgUtilities.intToBytes(cMsgNetworkConstants.magicNumbers[1], buf, 4);
                        cMsgUtilities.intToBytes(cMsgNetworkConstants.magicNumbers[2], buf, 8);
                        cMsgUtilities.intToBytes(cMsgConstants.version, buf, 12);
                        cMsgUtilities.intToBytes(cMsgNetworkConstants.rcDomainMulticastKillSelf, buf, 16);
                        DatagramPacket pkt = new DatagramPacket(buf, 16, multicasterAddress, server.udpPort);
//System.out.println("RC multicast listener: send response packet (kill yourself) to server");
                        multicastSocket.send(pkt);
                    }
                    else {
//System.out.println("RC multicast listener: starting up but was probed by another starting server. So quit");
                        server.respondingHost = multicasterHost;
                        server.multicastResponse.countDown();
                        return;
                    }
                    continue;
                }

//System.out.println("RC multicast listener: pass msg on to subscriptions");
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("RC multicast listener: client " + multicasterName + " is now connected");
                }

                // If expid's match, pass on message to subscribes and/or subscribeAndGets
                cMsgMessageFull msg = new cMsgMessageFull();
                msg.setSenderHost(multicasterHost);
                msg.setUserInt(multicasterTcpPort);
                msg.setSender(multicasterName);
                msg.setDomain(domainType);
                msg.setReceiver(server.getName());
                msg.setReceiverHost(server.getHost());
                msg.setReceiverTime(new Date()); // current time

                // Add list of IP addrs for client connections, if any
                if (ipList.size() > 0) {
                    try {
                        String[] ips = new String[ipList.size()];
                        ipList.toArray(ips);
                        cMsgPayloadItem pItem = new cMsgPayloadItem("IpAddresses", ips);
                        msg.addPayloadItem(pItem);
                    }
                    catch (cMsgException e) {/* never happen */}
                }

                // Add list of broadcast addrs for client connections, if any
                if (broadList.size() > 0) {
                    try {
                        String[] ips = new String[broadList.size()];
                        broadList.toArray(ips);
                        cMsgPayloadItem pItem = new cMsgPayloadItem("BroadcastAddresses", ips);
                        msg.addPayloadItem(pItem);
                    }
                    catch (cMsgException e) {/* never happen */}
                }

                try {
                    cMsgPayloadItem pItem = new cMsgPayloadItem("SenderId", senderId);
                    msg.addPayloadItem(pItem);
                }
                catch (cMsgException e) {/* never happen */}

                try {
                    cMsgPayloadItem pItem = new cMsgPayloadItem("packetCount", packetCounter);
                    msg.addPayloadItem(pItem);
                }
                catch (cMsgException e) {/* never happen */}

                //if (packetCounter < 6) continue;
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

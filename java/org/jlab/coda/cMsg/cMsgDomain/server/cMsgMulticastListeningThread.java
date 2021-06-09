/*----------------------------------------------------------------------------*
 *  Copyright (c) 2006        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 9-Nov-2006, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.server;

import org.jlab.coda.cMsg.*;

import java.net.*;
import java.io.IOException;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.UnsupportedEncodingException;
import java.util.List;

/**
 * This class implements a thread to listen to cMsg clients multicasting
 * in order to find and then fully connect to a cMsg server.
 *
 * @author Carl Timmer
 * @version 1.0
 */
class cMsgMulticastListeningThread extends Thread {

    /** Name server object. */
    cMsgNameServer server;

    /** cMsg name server's main TCP listening port. */
     private int serverTcpPort;

    /** cMsg name server's main UDP listening port. */
     private int serverUdpPort;

    /** cMsg name server's client password. */
     private String serverPassword;

     /** UDP socket on which to read packets sent from cMsg clients. */
    private MulticastSocket multicastSocket;

    /** Level of debug output for this class. */
    private int debug;

    /** Setting this to true will kill this thread. */
    private boolean killThread;

    /** Kills this thread. */
    void killThread() {
        killThread = true;
        this.interrupt();
        multicastSocket.close();
    }



    /**
     * Constructor.
     *
     * @param nameServer the cMsg name server that started this thread
     * @param port cMsg name server's main tcp listening port
     * @param multicastPort cMsg name server's multicast listening port
     * @param socket udp socket on which to receive multicasts from cMsg clients
     * @param password cMsg server's client password
     * @param debug cMsg server's debug level
     */
    public cMsgMulticastListeningThread(cMsgNameServer nameServer, int port, int multicastPort,
                                        MulticastSocket socket, String password, int debug) {
        server          = nameServer;
        multicastSocket = socket;
        serverTcpPort   = port;
        serverUdpPort   = multicastPort;
        serverPassword  = password;
        this.debug      = debug;
        // die if no more non-daemon thds running
        setDaemon(true);
    }


    /** This method is executed as a thread. */
    public void run() {

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println(">>     MC: Running cMsgNameserver Multicast Listening Thread");
        }

        // create a packet to be written into
        byte[] buf = new byte[1024];
        DatagramPacket packet = new DatagramPacket(buf, 1024);

        // create a packet to be send back to the client
        DatagramPacket sendPacket  = null;
        ByteArrayOutputStream baos = new ByteArrayOutputStream(1024);
        DataOutputStream out       = new DataOutputStream(baos);

        // Send dot-decimal from of all local IP & broadcast addresses.
        // The canonical name may be associated with an address that
        // is inaccessible from the client.
        // We can still make things work on an isolated machine as
        // the address will default to 127.0.0.1 if /etc/hosts
        // is setup properly.

        try {
            // Put our magic ints, TCP listening port, and our host into byte array
            out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
            out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
            out.writeInt(cMsgNetworkConstants.magicNumbers[2]);
            out.writeInt(serverTcpPort);
            out.writeInt(serverUdpPort);

            //-------------------------------------
            // Now send IP and broadcast addresses
            //-------------------------------------
            int i=0, addrCount;
            String[] ipAddrs;
            String[] broadcastAddrs;

            // List of all IP data (no IPv6, no loopback, no down interfaces)
            List<InterfaceAddress> ifAddrs = cMsgUtilities.getAllIpInfo();
            addrCount      = ifAddrs.size();
            ipAddrs        = new String[addrCount];
            broadcastAddrs = new String[addrCount];

            for (InterfaceAddress ifAddr : ifAddrs) {
                Inet4Address bAddr;
                try { bAddr = (Inet4Address)ifAddr.getBroadcast(); }
                catch (ClassCastException e) {
                    // should never happen since IPv6 already removed
                    continue;
                }
                broadcastAddrs[i] = bAddr.getHostAddress();
                ipAddrs[i++] = ifAddr.getAddress().getHostAddress();
            }

            // Let folks know how many address pairs are coming
            out.writeInt(addrCount);

//System.out.println("RC connect: ip list items = " + addrCount);
            for (int j=0; j < addrCount; j++) {
                try {
                    out.writeInt(ipAddrs[j].length());
//System.out.println("RC connect: ip size = " + ipAddrs[j].length());
                    out.write(ipAddrs[j].getBytes("US-ASCII"));
//System.out.println("RC connect: ip = " + ipAddrs[j]);
                    out.writeInt(broadcastAddrs[j].length());
//System.out.println("RC connect: broad size = " + broadcastAddrs[j].length());
                    out.write(broadcastAddrs[j].getBytes("US-ASCII"));
//System.out.println("RC connect: broad = " + broadcastAddrs[j]);
                }
                catch (UnsupportedEncodingException e) {/* never happen*/}
            }

            out.flush();
            out.close();

            // create packet to multicast from the byte array
            byte[] outBuf = baos.toByteArray();
            sendPacket = new DatagramPacket(outBuf, outBuf.length);
        }
        catch (IOException e) {
            if (debug >= cMsgConstants.debugError) {
                System.out.println("I/O Error: " + e);
            }
        }
        
        // Tell whoever is waiting for this thread to start, that
        // it has now started.
        server.listeningThreadsStartedSignal.countDown();


        // listen for multicasts and interpret packets
        try {
            while (true) {
                if (killThread) { return; }

                packet.setLength(1024);
                multicastSocket.receive(packet);
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("RECEIVED CMSG DOMAIN MULTICAST PACKET !!!");
                }

                if (killThread) { return; }

                // pick apart byte array received
                InetAddress clientAddress = packet.getAddress();
                int clientUdpPort = packet.getPort();   // port to send response packet to

                // Because there are so many problems with underlying operating systems,
                // our use of multicasting allows other multicasts, broadcasts or unicasts to
                // send to this UDP socket. We'll have to implement our own filter.

                // if packet is smaller than 5 ints ...
                if (packet.getLength() < 20) {
                    continue;
                }

                // pick apart byte array received
                int magicInt1  = cMsgUtilities.bytesToInt(buf, 0); // magic password
                int magicInt2  = cMsgUtilities.bytesToInt(buf, 4); // magic password
                int magicInt3  = cMsgUtilities.bytesToInt(buf, 8); // magic password

                if ( (magicInt1 != cMsgNetworkConstants.magicNumbers[0]) ||
                     (magicInt2 != cMsgNetworkConstants.magicNumbers[1]) ||
                     (magicInt3 != cMsgNetworkConstants.magicNumbers[2]))  {
//System.out.println("  Bad magic numbers for multicast response packet");
                     continue;
                }

                // Check cMsg version
                int version = cMsgUtilities.bytesToInt(buf, 12); // what cMsg version is this ?
                if (version != cMsgConstants.version) {
                    // ignore multicasts from different version
//System.out.println("multicast packet: bad version (" + version + ") != expected (" + cMsgConstants.version + ")");
                    continue;
                }
//System.out.println("multicast packet: version = " + version);

                int msgType     = cMsgUtilities.bytesToInt(buf, 16); // what type of multicast is this ?
                int passwordLen = cMsgUtilities.bytesToInt(buf, 20); // password length

                // Check to distinguish between this case and sending messages
                // to the rc broadcast domain.
                if (msgType != cMsgNetworkConstants.cMsgDomainMulticast) {
                    // ignore multicasts from unknown sources
//System.out.println("bad msgtype");
                    continue;
                }
//System.out.println("multicast packet: msg type = " + msgType);

                // if packet is too small ...
                if (packet.getLength() < 20 + passwordLen) {
                    continue;
                }

                // password
                String pswd = null;
                if (passwordLen > 0) {
                    try { pswd = new String(buf, 24, passwordLen, "US-ASCII"); }
                    catch (UnsupportedEncodingException e) {}
                }
//System.out.println("multicast packet: password = " + pswd);

                // Compare sent password with name server's password (not the cloud password).
                // Reject mismatches.
                if (serverPassword != null) {
                    if (pswd == null || !serverPassword.equals(pswd)) {
                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("REJECTING PASSWORD: server's does not match client's ("
                                    + pswd + ")");
                        }
                        continue;
                    }
                }
                else if (pswd != null) {
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("Client password (" + pswd + ") does not match server's (null), reject packet");
                    }
                    continue;
                }

                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("packet passes all tests, send response");
                }

                // Send a reply to multicast. This must contain this name server's
                // host and tcp port so a regular connect can be done by the client.
                try {
                    // set address and port for responding packet
                    sendPacket.setAddress(clientAddress);
                    sendPacket.setPort(clientUdpPort);
                    multicastSocket.send(sendPacket);
                }
                catch (IOException e) {
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("I/O Error: " + e);
                    }
                }
            }
        }
        catch (IOException e) {
            if (debug >= cMsgConstants.debugError) {
                System.out.println("cMsgBroadcastListenThread: I/O ERROR in cMsg multicast server");
                System.out.println("                         : close multicast socket, port = " +
                                    multicastSocket.getLocalPort());
            }
        }
        finally {
            // We're here if there is an IO error. Close socket and kill this thread.
            multicastSocket.close();
        }

        return;
    }

}

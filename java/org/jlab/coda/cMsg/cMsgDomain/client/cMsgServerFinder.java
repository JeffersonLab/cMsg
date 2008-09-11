/*----------------------------------------------------------------------------*
 *  Copyright (c) 2008        Southeastern Universities Research Association, *
 *                            Jefferson Science Associates                    *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C.Timmer, 10-Sep-2008, Jefferson Lab                                    *
 *                                                                            *
 *    Authors: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.client;

import org.jlab.coda.cMsg.*;

import java.net.*;
import java.io.*;
import java.util.HashSet;

/**
 * This class implements a program to find cMsg domain servers and cMsg broadcast
 * domain servers which are listening on UDP sockets on the local subnet.
 * By convention, all cMsg Domain servers should have a broadcast port starting at
 * 45000 and not exceeding 45099. All these ports will be probed for servers.
 */
public class cMsgServerFinder {

    /** Port numbers to probe. */
    int[] broadcastPorts;
    int[] defaultPorts;

    /** Optional password included in UDL for connection to server requiring one. */
    private String password = "";

    byte[] outBuffer;

    InetAddress broadcastAddr;

    /** Socket over which to send UDP broadcast and receive response packets from server. */
    DatagramSocket udpSocket;

    HashSet<String> responders;

    /** Time in milliseconds between sending monitor data / keep alives. */
    final int sleepTime = 5000;

    /** Level of debug output for this class. */
    int debug = cMsgConstants.debugError;




    /** Constructor. */
    cMsgServerFinder(String[] args) {
        decodeCommandLine(args);
        responders = new HashSet<String>(100);
        broadcastPorts = new int[0];
        defaultPorts = new int[100];
        // set default ports to scan
        for (int i=0; i<100; i++) {
            defaultPorts[i] = 45000 + i;
        }
    }


    /**
     * Method to decode the command line used to start this application.
     * @param args command line arguments
     */
    public void decodeCommandLine(String[] args) {

        // loop over all args
        for (int i = 0; i < args.length; i++) {

            if (args[i].equalsIgnoreCase("-h")) {
                usage();
                System.exit(-1);
            }
            else if (args[i].equalsIgnoreCase("-pswd")) {
                password= args[i + 1];
                i++;
            }
            else if (args[i].equalsIgnoreCase("-p")) {
                String[] strs = (args[i + 1]).split("\\p{Punct}");
                broadcastPorts = new int[strs.length];
                for (int j=0; j<strs.length; j++) {
                    broadcastPorts[j] = Integer.parseInt(strs[j]);
                    if (broadcastPorts[j] < 1024 || broadcastPorts[j] > 65535) {
                        System.out.println("broadcast port " + broadcastPorts[j] + " must be > 1023 and < 65536");
                        System.exit(-1);
                    }
                }
                i++;
            }
            else if (args[i].equalsIgnoreCase("-debug")) {
                debug = cMsgConstants.debugInfo;
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
            "   java cMsgServerFinder [-p colon-separated list of ports]\n" +
            "                         [-pswd password]\n" +
            "                         [-h print this usage text]\n" +
            "                         [-debug]\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            cMsgServerFinder finder = new cMsgServerFinder(args);
            finder.run();
        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }
    }



    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {

        //-------------------------------------------------------
        // broadcast on local subnet to find cMsg server
        //-------------------------------------------------------
        try {
            broadcastAddr = InetAddress.getByName("255.255.255.255");
        }
        catch (UnknownHostException e) {
            e.printStackTrace();
        }

        // create byte array for broadcast
        ByteArrayOutputStream baos = new ByteArrayOutputStream(1024);
        DataOutputStream out = new DataOutputStream(baos);

        try {
            // send our magic ints
            out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
            out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
            out.writeInt(cMsgNetworkConstants.magicNumbers[2]);
            // int describing our message type: broadcast is from cMsg domain client
            out.writeInt(cMsgNetworkConstants.cMsgDomainBroadcast);
            out.writeInt(password.length());
            try {out.write(password.getBytes("US-ASCII"));}
            catch (UnsupportedEncodingException e) { }
            out.flush();
            out.close();

            // create socket to receive at anonymous port & all interfaces
            udpSocket = new DatagramSocket();
            udpSocket.setReceiveBufferSize(1024);

            // create broadcast packet from the byte array
            outBuffer = baos.toByteArray();
        }
        catch (IOException e) {
            try { out.close();} catch (IOException e1) {}
            try {baos.close();} catch (IOException e1) {}
            if (udpSocket != null) udpSocket.close();
            throw new cMsgException("Cannot create broadcast packet", e);
        }

        // create a thread which will receive any responses to our broadcast
        BroadcastReceiver receiver = new BroadcastReceiver();
        receiver.start();

        // give receiver time to get started before we starting sending out packets
        try { Thread.sleep(200); }
        catch (InterruptedException e) { }

        // create a thread which will send our broadcast
        Broadcaster sender = new Broadcaster();
        sender.start();

        // wait for responses
        try { Thread.sleep(sleepTime); }
        catch (InterruptedException e) { }

        sender.interrupt();

        // receiving thread must not be writing into this when printing out, so wait a bit
        try { Thread.sleep(200); }
        catch (InterruptedException e) { }

        String[] bits;
        for (String s : responders) {
            bits = s.split(":");
            System.out.println("Host = " + bits[0] + ",  Port = " + bits[1]);
        }

        return;
    }

//-----------------------------------------------------------------------------

    /**
     * This class gets any response to our UDP broadcast.
     */
    class BroadcastReceiver extends Thread {

        public void run() {
            String nameServerHost;
            int nameServerPort;
            StringBuffer id = new StringBuffer(1024);
            byte[] buf = new byte[1024];
            DatagramPacket packet = new DatagramPacket(buf, 1024);

            while (true) {
                try {
                    nameServerHost = "";
                    packet.setLength(1024);
                    udpSocket.receive(packet);

                    // if packet is smaller than 5 ints plus 1 really short string ...
                    if (packet.getLength() < 21) {
                        continue;
                    }

System.out.println("RECEIVED BROADCAST RESPONSE PACKET !!!");
                    // pick apart byte array received
                    int magicInt1  = cMsgUtilities.bytesToInt(buf, 0); // magic password
                    int magicInt2  = cMsgUtilities.bytesToInt(buf, 4); // magic password
                    int magicInt3  = cMsgUtilities.bytesToInt(buf, 8); // magic password

                    if ( (magicInt1 != cMsgNetworkConstants.magicNumbers[0]) ||
                         (magicInt2 != cMsgNetworkConstants.magicNumbers[1]) ||
                         (magicInt3 != cMsgNetworkConstants.magicNumbers[2]))  {
System.out.println("  Bad magic numbers for broadcast response packet");
                        continue;
                    }

                    // cMsg name server port
                    nameServerPort = cMsgUtilities.bytesToInt(buf, 12); // port to do a direct connection to
                    int hostLength = cMsgUtilities.bytesToInt(buf, 16); // host to do a direct connection to

                    if ((nameServerPort < 1024 || nameServerPort > 65535) ||
                            (hostLength < 0 || hostLength > 1024 - 20)) {
System.out.println("  Wrong port # or host length for broadcast response packet");
                        continue;
                    }

                    if (packet.getLength() != 4*5 + hostLength) {
System.out.println("  Wrong length for broadcast response packet");
                        continue;
                    }

                    // cMsg name server host
                    try { nameServerHost = new String(buf, 20, hostLength, "US-ASCII"); }
                    catch (UnsupportedEncodingException e) {}
System.out.println("  Got port = " + nameServerPort + ", host = " + nameServerHost);

                    // put in a unique item: "host:port"
                    if (nameServerHost.length() > 0) {
                        id.delete(0,1023);
                        id.append(nameServerHost);
                        id.append(":");
                        id.append(nameServerPort);
                        responders.add(id.toString());
                    }
                }
                catch (InterruptedIOException e) {
System.out.println("  Interrupted receiving thread so return");
                    return;
                }
                catch (IOException e) {
System.out.println("  IO exception in receiving thread so return");
                    return;
                }
            }
        }
    }

//-----------------------------------------------------------------------------

    /**
     * This class defines a thread to broadcast a single UDP packet to
     * cMsg name servers.
     */
    class Broadcaster extends Thread {

        public void run() {
            DatagramPacket packet;

            try {
                for (int broadcastPort : broadcastPorts) {
System.out.println("Send broadcast packets on port " + broadcastPort);
                    packet = new DatagramPacket(outBuffer, outBuffer.length,
                                                broadcastAddr, broadcastPort);
                    udpSocket.send(packet);
                }
                for (int broadcastPort : defaultPorts) {
System.out.println("Send broadcast packets on port " + broadcastPort);
                    packet = new DatagramPacket(outBuffer, outBuffer.length,
                                                broadcastAddr, broadcastPort);
                    udpSocket.send(packet);
                }
            }
            catch (IOException e) {
                e.printStackTrace();
            }
        }
    }



}

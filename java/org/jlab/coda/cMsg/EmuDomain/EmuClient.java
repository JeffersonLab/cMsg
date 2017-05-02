/*---------------------------------------------------------------------------*
*  Copyright (c) 2014        Jefferson Science Associates,                   *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    C.Timmer, Apr-2014, Jefferson Lab                                       *
*                                                                            *
*    Authors: Carl Timmer                                                    *
*             timmer@jlab.org                   Jefferson Lab, #10           *
*             Phone: (757) 269-5130             12000 Jefferson Ave.         *
*             Fax:   (757) 269-6248             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.EmuDomain;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgDomainAdapter;

import java.io.*;
import java.net.*;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * This class implements a cMsg client in the Emu domain.
 *
 * @author Carl Timmer
 * @version 3.5
 */
public final class EmuClient extends cMsgDomainAdapter {

    /** Timeout in milliseconds to wait for emu server to respond to multicasts. */
    private int multicastTimeout = 3000;

    /** All of server's IP addresses obtained from multicast response. */
    private final ArrayList<String> ipAddresses = new ArrayList<String>(10);

    /** All of server's broadcast addresses obtained from multicast response. */
    private final ArrayList<String> broadcastAddresses = new ArrayList<String>(10);

    /** Server's IP address used to connect. */
    private String serverIp;

    /** Server's TCP listening port used to connect. */
    private volatile int tcpServerPort;

    /** Server's multicast listening port obtained from UDL. */
    private int multicastServerPort;

    /** Socket over which to UDP multicast to and receive UDP packets from the server. */
    private MulticastSocket multicastUdpSocket;

    /** Number of sockets over which to send messages to the server over TCP. */
    private int socketCount = 1;

    /** Sockets over which to send messages to the server over TCP. */
    private Socket[] tcpSocket;

    /** Output TCP data streams from this client to the server. */
    private DataOutputStream[] domainOut;

    /** Signal to coordinate the multicasting and waiting for responses. */
    private CountDownLatch multicastResponse;

    /** From UDL, max size, in bytes, of data chunk to be sent in one msg. */
    private int maxSize = 4010000;

    /** TCP send buffer size in bytes. */
    private int tcpSendBufferSize = maxSize + 1024;

    /** From UDL, no delay setting for TCP socket. */
    private boolean tcpNoDelay = false;

    /** From UDL, our coda id. */
    private int codaID;

    /** From UDL, name of destination CODA component. */
    private String destComponentName;

    /** From UDL, our experiment id. */
    private String expid;

    /** Preferred subnet over which to connect to the server. */
    private String preferredSubnet;



    /** Constructor. */
    public EmuClient() throws cMsgException {
        domain = "emu";
    }



    /**
     * Get the host of the emu server that this client is connected to.
     * @return emu server's host; null if unknown
     */
    public String getServerHost() {
        return serverIp;
    }


    /**
     * Get the TCP port of the emu server that this client is connected to.
     * @return emu server's port; 0 if unknown
     */
    public int getServerPort() {
        return tcpServerPort;
    }


    /**
     * Method to connect to the server from this client.
     *
     * @throws org.jlab.coda.cMsg.cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server(s)
     */
    public void connect() throws cMsgException {

        parseUDL(UDLremainder);

        if (connected) return;

        // set the latches
        multicastResponse = new CountDownLatch(1);

        //--------------------------------------------------------------
        // multicast on local subnets to find EmuClient Multicast server
        //--------------------------------------------------------------
        DatagramPacket udpPacket;

        // create byte array for multicast
        ByteArrayOutputStream baos = new ByteArrayOutputStream(1024);
        DataOutputStream out = new DataOutputStream(baos);

        try {
            // Send our magic #s, cMsg version, name and the expid.
            out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
            out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
            out.writeInt(cMsgNetworkConstants.magicNumbers[2]);
            out.writeInt(cMsgNetworkConstants.emuDomainMulticastClient);
            out.writeInt(cMsgConstants.version);
            out.writeInt(destComponentName.length());
            out.writeInt(expid.length());

            try {
                out.write(destComponentName.getBytes("US-ASCII"));
                out.write(expid.getBytes("US-ASCII"));
            }
            catch (UnsupportedEncodingException e) {/* never happen*/}

            out.flush();
            out.close();

            // create socket to receive at anonymous port & all interfaces
            multicastUdpSocket = new MulticastSocket();

            // Avoid local port for socket to which others may be multicasting to
            int tries = 20;
            while (multicastUdpSocket.getLocalPort() > cMsgNetworkConstants.UdpClientPortMin &&
                   multicastUdpSocket.getLocalPort() < cMsgNetworkConstants.UdpClientPortMax) {
                multicastUdpSocket = new MulticastSocket();
                if (--tries < 0) break;
            }

            multicastUdpSocket.setTimeToLive(32);  // Make it through routers
            InetAddress multicastServerAddress = null;
            try {multicastServerAddress = InetAddress.getByName(cMsgNetworkConstants.emuMulticast); }
            catch (UnknownHostException e) {}

            // create packet to multicast from the byte array
            byte[] buf = baos.toByteArray();
            System.out.println("");
            udpPacket = new DatagramPacket(buf, buf.length,
                                           multicastServerAddress,
                                           multicastServerPort);
            baos.close();
        }
        catch (IOException e) {
            try { out.close();} catch (IOException e1) {}
            try {baos.close();} catch (IOException e1) {}
            if (multicastUdpSocket != null) multicastUdpSocket.close();

            if (debug >= cMsgConstants.debugError) {
                System.out.println("I/O Error: " + e);
            }
            throw new cMsgException(e.getMessage(), e);
        }

        // create a thread which will receive any responses to our multicast
        MulticastReceiver receiver = new MulticastReceiver();
        receiver.start();

        // create a thread which will send our multicast
        Multicaster sender = new Multicaster(udpPacket);
        sender.start();

        // wait up to multicast timeout seconds
        boolean response = false;
        if (multicastTimeout > 0) {
            try {
                if (multicastResponse.await(multicastTimeout, TimeUnit.MILLISECONDS)) {
                    response = true;
                }
            }
            catch (InterruptedException e) {}
        }
        // wait forever
        else {
            try {
                multicastResponse.await();
                response = true;
            }
            catch (InterruptedException e) {}
        }

        multicastUdpSocket.close();
        sender.interrupt();

        if (!response) {
            throw new cMsgException("No response to UDP multicast received");
        }
//        else {
//System.out.println("Emu connect: got a response to multicast!");
//        }

        // Now that we got a response from the Emu server,
        // we have the info to connect to its TCP listening thread.

        // First sort the response into a list of IP addresses in which
        // the IPs on the preferred subnet are listed first, the IPs on
        // common subnets are listed next, and all others last.
        List<String> orderedIps = cMsgUtilities.orderIPAddresses(ipAddresses,
                                                                 broadcastAddresses,
                                                                 preferredSubnet);

        // Find an IP address on this host that matches the preferred subnet,
        // else return null.
        String outgoingIp = cMsgUtilities.getMatchingLocalIpAddress(preferredSubnet);

        // Create TCP connection(s) to the Emu Server
        IOException ioex = null;

        tcpSocket = new Socket[socketCount];
        domainOut = new DataOutputStream[socketCount];
        boolean gotAllConnections = true;
        boolean[] gotTcpConnection = new boolean[socketCount];

        if (orderedIps != null && orderedIps.size() > 0) {
            search:
            for (String ip : orderedIps) {
                for (int i=0; i < socketCount; i++) {
                    try {
                        tcpSocket[i] = new Socket();
                        tcpSocket[i].setTcpNoDelay(tcpNoDelay);
                        tcpSocket[i].setSendBufferSize(tcpSendBufferSize);
                        tcpSocket[i].setPerformancePreferences(0,0,1);
                        // Bind this end of the socket to the preferred subnet
                        if (outgoingIp != null) {
                            try {
                                tcpSocket[i].bind(new InetSocketAddress(outgoingIp, 0));
System.out.println("      Emu connect: bound outgoing data to " + outgoingIp);
                            }
                            catch (IOException e) {
                                // If we cannot bind to this IP address, forget about it
System.out.println("      Emu connect: tried but FAILED to bind outgoing data to " + outgoingIp);
                            }
                        }
                        // Don't waste time if a connection can't be made, timeout = 0.2 sec
                        tcpSocket[i].connect(new InetSocketAddress(ip, tcpServerPort), 200);

                        domainOut[i] = new DataOutputStream(new BufferedOutputStream(tcpSocket[i].getOutputStream()));
System.out.println("      Emu connect: Made TCP connection to host = " +
                   ip + "; port = " + tcpServerPort);
                        serverIp = ip;
                        gotTcpConnection[i] = true;

                        // If last socket, we're done
                        if (i == socketCount - 1) {
                            break search;
                        }
                    }
                    catch (SocketTimeoutException e) {
                        System.out.println("      Emu connect: connection TIMEOUT");
                        ioex = e;
                    }
                    catch (IOException e) {
                        System.out.println("      Emu connect: connection failed");
                        ioex = e;
                    }
                }
            }
        }

        for (int i=0; i < socketCount; i++) {
            gotAllConnections = gotAllConnections && gotTcpConnection[i];
        }

        if (!gotAllConnections) {
            if (domainOut != null) {
                for (int i=0; i < socketCount; i++) {
                    try {domainOut[i].close();}
                    catch (IOException e) {}
                }
            }

            if (tcpSocket != null) {
                for (int i=0; i < socketCount; i++) {
                    try {tcpSocket[i].close();}
                    catch (IOException e) {}
                }
            }
            
            throw new cMsgException("Cannot make all TCP connections to Emu server", ioex);
        }

        try {
            talkToServer();
        }
        catch (IOException e) {
            throw new cMsgException("Communication error with Emu server", e);
        }

        // create request sending (to domain) channel (This takes longest so do last)
        connected = true;

        return;
    }


    /** Talk to emu server over TCP connection. */
    private void talkToServer() throws IOException {
        try {
            for (int i=0; i < socketCount; i++) {
                // Send emu server some info
                domainOut[i].writeInt(cMsgNetworkConstants.magicNumbers[0]);
                domainOut[i].writeInt(cMsgNetworkConstants.magicNumbers[1]);
                domainOut[i].writeInt(cMsgNetworkConstants.magicNumbers[2]);

                // Version, coda id, buffer size
                domainOut[i].writeInt(cMsgConstants.version);
                domainOut[i].writeInt(codaID);
                domainOut[i].writeInt(maxSize);

                // How many sockets, relative position of socket
                domainOut[i].writeInt(socketCount);
                domainOut[i].writeInt(i+1);
                
                domainOut[i].flush();
            }
        }
        catch (IOException e) {
            e.printStackTrace();
        }
    }



    /**
     * Method to parse the Universal Domain Locator (UDL) into its various components.
     *
     * Emu domain UDL is of the form:<p>
     *   <b>cMsg:emu://&lt;port&gt;/&lt;expid&gt;/&lt;compName&gt;?codaId=&lt;id&gt;&timeout=&lt;sec&gt;&bufSize=&lt;size&gt;&tcpSend=&lt;size&gt;&subnet=&lt;subnet&gt;&sockets=&lt;count&gt;&noDelay</b><p>
     *
     * Remember that for this domain:
     *<ol>
     *<li>multicast address is always 239.230.0.0<p>
     *<li>port is required - UDP multicast port<p>
     *<li>expid is required <p>
     *<li>compName is required - destination CODA component name<p>
     *<li>codaId is required<p>
     *<li>optional timeout for connecting to emu server, defaults to 3 seconds<p>
     *<li>optional bufSize (max size in bytes of a single send) defaults to 2.1MB<p>
     *<li>optional tcpSend is the TCP send buffer size in bytes<p>
     *<li>optional subnet is the preferred subnet used to connect to server<p>
     *<li>optional sockets is the number of TCP sockets to use when connecting to server<p>
     *<li>optional noDelay is the TCP no-delay parameter turned on<p>
     *</ol><p>
     *
     * @param udlRemainder partial UDL to parse
     * @throws cMsgException if udlRemainder is null
     */
    void parseUDL(String udlRemainder) throws cMsgException {

        if (udlRemainder == null) {
            throw new cMsgException("invalid UDL");
        }

        Pattern pattern = Pattern.compile("(\\d+)/([^/]+)/([^?&]+)(.*)");
        Matcher matcher = pattern.matcher(udlRemainder);

        String udlPort, udlExpid, udlDestName, remainder;

        if (matcher.find()) {
            // port
            udlPort = matcher.group(1);
            // expid
            udlExpid = matcher.group(2);
            // destination component name
            udlDestName = matcher.group(3);
            // remainder
            remainder = matcher.group(4);

            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("\nparseUDL: " +
                                   "\n  port      = " + udlPort +
                                   "\n  expid     = " + udlExpid +
                                   "\n  component = " + udlDestName +
                                   "\n  remainder = " + remainder);
            }
        }
        else {
            throw new cMsgException("invalid UDL");
        }

        // Get multicast server port
        try {
            multicastServerPort = Integer.parseInt(udlPort);
        }
        catch (NumberFormatException e) {
            throw new cMsgException("parseUDL: bad port number");
        }

        if (multicastServerPort < 1024 || multicastServerPort > 65535) {
            throw new cMsgException("parseUDL: illegal port number");
        }
//System.out.println("Port = " + multicastServerPort);

        // Get expid
        if (udlExpid == null) {
            throw new cMsgException("parseUDL: must specify the EXPID");
        }
        expid = udlExpid;
//System.out.println("expid = " + udlExpid);

        // Get destination CODA component name
        if (udlDestName == null) {
            throw new cMsgException("parseUDL: must specify the destination CODA component name");
        }
        destComponentName = udlDestName;
//System.out.println("component = " + udlDestName);

        // If no remaining UDL to parse, return
        if (remainder == null) {
            throw new cMsgException("parseUDL: must specify the CODA id");
        }

        // Look for ?codaId=value
        pattern = Pattern.compile("[\\?]codaId=([0-9]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                codaID = Integer.parseInt(matcher.group(1));
            }
            catch (NumberFormatException e) {
                throw new cMsgException("parseUDL: improper CODA id", e);
            }
        }
        else {
            throw new cMsgException("parseUDL: must specify the CODA id");
        }
//System.out.println("CODA id = " + codaID);

        // Look for ?timeout=value or &timeout=value
        pattern = Pattern.compile("[\\?&]timeout=([0-9]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                multicastTimeout = 1000 * Integer.parseInt(matcher.group(1));
//System.out.println("timeout = " + multicastTimeout + " seconds");
            }
            catch (NumberFormatException e) {
                // ignore error and keep default value of 2.1MB
            }
        }

        // Look for ?bufSize=value or &bufSize=value
        pattern = Pattern.compile("[\\?&]bufSize=([0-9]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                int mSize = Integer.parseInt(matcher.group(1));
                if (mSize > 0) {
                    maxSize = mSize;
                }
//System.out.println("max data buffer size = " + maxSize);
            }
            catch (NumberFormatException e) {
                // ignore error and keep default value of 2.1MB
            }
        }

        // now look for ?tcpSend=value or &tcpSend=value
        pattern = Pattern.compile("[\\?&]tcpSend=([0-9]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                tcpSendBufferSize = Integer.parseInt(matcher.group(1));
                if (tcpSendBufferSize == 0) {
                    tcpSendBufferSize = maxSize + 1024;
                }
//System.out.println("tcp send buffer size = " + tcpSendBufferSize);
            }
            catch (NumberFormatException e) {
                // ignore error and keep value of 0
            }
        }

        // now look for ?subnet=value or &subnet=value
        pattern = Pattern.compile("[\\?&]subnet=((?:[0-9]{1,3}\\.){3}[0-9]{1,3})", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                preferredSubnet = matcher.group(1);
                // make sure it's in the proper format
                if (cMsgUtilities.isDottedDecimal(preferredSubnet) == null) {
                    preferredSubnet = null;
                }
//System.out.println("Emu client: preferred subnet = " + preferredSubnet);
            }
            catch (NumberFormatException e) {
                // ignore error and keep value of 0
            }
        }

        // now look for ?tcpSend=value or &tcpSend=value
        pattern = Pattern.compile("[\\?&]sockets=([0-9]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                socketCount = Integer.parseInt(matcher.group(1));
                if (socketCount < 1) {
                    socketCount = 1;
                }
//System.out.println("socket count = " + socketCount);
            }
            catch (NumberFormatException e) {
                // ignore error and keep value of 1
            }
        }

        // now look for ?noDelay or &noDelay
        pattern = Pattern.compile("[\\?&]noDelay", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                tcpNoDelay = true;
//System.out.println("tcp noDelay = " + tcpNoDelay);
            }
            catch (NumberFormatException e) {
                // ignore error and keep value of false
            }
        }

    }


    /**
     * {@inheritDoc}
     *
     * @param timeout {@inheritDoc}
     * @throws cMsgException if I/O exception on the output stream flush.
     */
    public void flush(int timeout) throws cMsgException {
        try {
            for (int i=0; i < socketCount; i++) {
                domainOut[i].flush();
            }
        }
        catch (IOException e) {
            throw new cMsgException(e);
        }
    }


    /**
     * Method to close the connection to the domain server. This method results in this object
     * becoming functionally useless.
     */
    public void disconnect() {
        if (!connected) return;
        connected = false;
        for (int i=0; i < socketCount; i++) {
            try {domainOut[i].flush();}  catch (IOException e) {}
            try {tcpSocket[i].close();}  catch (IOException e) {}
            try {domainOut[i].close();}  catch (IOException e) {}
        }
        multicastUdpSocket.close();
    }


    /**
     * Method to send a message to the Emu domain server.
     * If more than one socket is being used, alternate between them.
     *
     * @param message {@inheritDoc}
     * @throws cMsgException if there are communication problems with the server;
     *                       subject and/or type is null
     */
    public void send(final cMsgMessage message) throws cMsgException {

        int sendIndex = message.getSysMsgId();
        int binaryLength = message.getByteArrayLength();

        try {
            if (!connected) {
                throw new IOException("not connected to server");
            }

            // Type of message is in 1st int.
            // Total length of binary (not including this int) is in 2nd int
            domainOut[sendIndex].writeLong((long)message.getUserInt() << 32L | (binaryLength & 0xffffffffL));

            // Write byte array
            try {
                if (binaryLength > 0) {
                    domainOut[sendIndex].write(message.getByteArray(),
                                               message.getByteArrayOffset(),
                                               binaryLength);
                }
            }
            catch (UnsupportedEncodingException e) {
            }

            //domainOut[sendIndex].flush();
        }
        catch (IOException e) {
            e.printStackTrace();
            if (debug >= cMsgConstants.debugError) {
                System.out.println("send: " + e.getMessage());
            }
            throw new cMsgException(e.getMessage());
        }
    }

//-----------------------------------------------------------------------------


    /**
     * This class gets any response to our UDP multicast. A response will
     * stop the multicast and complete the  connect call by making a TCP
     * connection to that server.
     */
    class MulticastReceiver extends Thread {

        public void run() {

            /* A slight delay here will help the main thread (calling connect)
             * to be already waiting for a response from the server when we
             * multicast to the server here (prompting that response). This
             * will help insure no responses will be lost.
             */
            try { Thread.sleep(200); }
            catch (InterruptedException e) {}

            byte[] buf = new byte[1024];
            DatagramPacket packet = new DatagramPacket(buf, 1024);
            int index, len, totalLen, lengthOfInts = 4*5;
            String ip;

            newPacket:
            while (true) {
                // reset for each round
                packet.setLength(1024);
                index = 0;

                try {
                    multicastUdpSocket.receive(packet);
                    // if we get too small of a packet, reject it
                    if (packet.getLength() < lengthOfInts) {
                        if (debug >= cMsgConstants.debugWarn) {
                                     System.out.println("Multicast receiver: got packet that's too small");
                        }
                        continue;
                    }
//System.out.println("Emu client: multicast receiver, received multicast packet");
                    int magic1 = cMsgUtilities.bytesToInt(buf, index); index += 4;
                    int magic2 = cMsgUtilities.bytesToInt(buf, index); index += 4;
                    int magic3 = cMsgUtilities.bytesToInt(buf, index); index += 4;
                    
                    if (magic1 != cMsgNetworkConstants.magicNumbers[0] ||
                        magic2 != cMsgNetworkConstants.magicNumbers[1] ||
                        magic3 != cMsgNetworkConstants.magicNumbers[2])  {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("Multicast receiver: got bad magic # response to multicast");
                        }
                        continue;
                    }

                    tcpServerPort = cMsgUtilities.bytesToInt(buf, index); index += 4;

                    if (tcpServerPort < 1024 || tcpServerPort > 65535) {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("Multicast receiver: got bad tcp port value ("
                                               + tcpServerPort + ")");
                        }
                        continue;
                    }

                    // How many addresses are being sent?
                    int addressCount = cMsgUtilities.bytesToInt(buf, index); index += 4;
//System.out.println("Emu client: multicast receiver, addressCount = " + addressCount);

                    if (addressCount < 1) {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("Multicast receiver: got bad # of addresses ("
                                               + addressCount + ")");
                        }
                        continue;
                    }

                    // Bytes of all ints just read in
                    totalLen = lengthOfInts;

                    // Read all server's IP addresses (dot-decimal format)
                    for (int i=0; i < addressCount; i++) {

                        // Packet has enough data?
                        totalLen += 4;
                        if (packet.getLength() < totalLen) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("Multicast receiver: got packet that's too small");
                            }
                            continue;
                        }

                        len = cMsgUtilities.bytesToInt(buf, index); index += 4;
                        if (len < 7) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("Multicast receiver: got length that's too small");
                            }
                            continue newPacket;
                        }

                        totalLen += len;
                        if (packet.getLength() < totalLen) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("Multicast receiver: got packet that's too small");
                            }
                            continue newPacket;
                        }

                        // Get & store dotted-decimal IP address
                        ip = new String(buf, index, len, "US-ASCII"); index += len;
                        ipAddresses.add(ip);
//System.out.println("Emu client: multicast receiver, server IP = " + ip);

                        totalLen += 4;
                        if (packet.getLength() < totalLen) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("Multicast receiver: got packet that's too small");
                            }
                            continue;
                        }

                        len = cMsgUtilities.bytesToInt(buf, index); index += 4;
                        if (len < 7) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("Multicast receiver: got length that's too small");
                            }
                            continue newPacket;
                        }

                        totalLen += len;
                        if (packet.getLength() < totalLen) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("Multicast receiver: got packet that's too small");
                            }
                            continue newPacket;
                        }

                        // Get & store dotted-decimal broadcast addresses
                        ip = new String(buf, index, len, "US-ASCII"); index += len;
                        broadcastAddresses.add(ip);
//System.out.println("Emu client: multicast receiver, server broadcast = " + ip);
                    }

                }
                catch (UnsupportedEncodingException e) {continue;}
                catch (IOException e) {
System.out.println("Got IOException in multicast receive, exiting");
                    return;
                }
                // Weed out unknown format packets
                catch (Exception e) {
                    continue;
                }
                break;
            }

            multicastResponse.countDown();
        }
    }


    /**
     * This class defines a thread to multicast a UDP packet to the
     * Emu server every second.
     */
    class Multicaster extends Thread {

        DatagramPacket packet;

        Multicaster(DatagramPacket udpPacket) {
            packet = udpPacket;
        }


        public void run() {

//System.out.println("RC connect: client " + name + ": STARTED multicast sending thread");
            try {
                // A slight delay here will help the main thread (calling connect)
                // to be already waiting for a response from the server when we
                // multicast to the server here (prompting that response). This
                // will help insure no responses will be lost.
                Thread.sleep(100);

                while (true) {

                    int sleepCount = 0;

                    try {
                        // Send a packet over each network interface.
                        // Place a 1/2 second delay between each.
                        Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();

                        while (enumer.hasMoreElements()) {
                            NetworkInterface ni = enumer.nextElement();
//System.out.println("RC client: found interface " + ni +
//                   ", up = " + ni.isUp() +
//                   ", loopback = " + ni.isLoopback() +
//                   ", has multicast = " + ni.supportsMulticast());
                            if (ni.isUp()) {
System.out.println("Emu client: sending mcast packet over " + ni.getName());
                                multicastUdpSocket.setNetworkInterface(ni);
                                multicastUdpSocket.send(packet);
                                Thread.sleep(200);
                                sleepCount++;
                            }
                        }
                    }
                    catch (IOException e) {
                        e.printStackTrace();
                    }

                    if (sleepCount < 1) Thread.sleep(1000);
                }
            }
            catch (InterruptedException e) {
                // time to quit
//System.out.println("Interrupted sender");
            }
        }
    }


}


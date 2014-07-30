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
import java.util.*;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * This class implements a cMsg client in the Emu domain.
 *
 * @author Carl Timmer
 * @version 3.5
 */
public class EmuClient extends cMsgDomainAdapter {

    static final int version = 1;


    /** Timeout in milliseconds to wait for server to respond to multicasts. */
    int multicastTimeout;

    /**
     * Timeout in seconds to wait for RC server to finish connection
     * once RC multicast server responds. Defaults to 5 seconds.
     */
    int connectTimeout = 5000;

    /** Server's net addresses obtained from multicast response. */
    volatile InetAddress serverAddress;

    /** Server's net address obtained from multicast response. */
    volatile ArrayList<InetAddress> serverAddresses = new ArrayList<InetAddress>(10);

    /** Server's UDP listening port obtained from {@link #connect}. */
    volatile int udpServerPort;

    /** Server's TCP listening port obtained from {@link #connect}. */
    volatile int tcpServerPort;

    String tcpServerHost;

    /** Server's net address obtained from UDL. */
    InetAddress multicastServerAddress;

    /** Server's multicast listening port obtained from UDL. */
    int multicastServerPort;

    /**
     * Packet to send over UDP to server to implement
     * {@link #send(org.jlab.coda.cMsg.cMsgMessage)}.
     */
    DatagramPacket sendUdpPacket;

    /** Socket over which to UDP multicast to and receive UDP packets from the server. */
    MulticastSocket multicastUdpSocket;

    /** Socket over which to send messages to the server over TCP. */
    Socket tcpSocket;

    /** Output TCP data stream from this client to the server. */
    DataOutputStream domainOut;

    /**
     * This lock is for controlling access to the methods of this class.
     * It is inherently more flexible than synchronizing code. The {@link #connect}
     * and {@link #disconnect} methods of this object cannot be called simultaneously
     * with each other or any other method. However, the {@link #send(org.jlab.coda.cMsg.cMsgMessage)}
     * method is thread-safe and may be called simultaneously from multiple threads.
     */
    private final ReentrantReadWriteLock methodLock = new ReentrantReadWriteLock();

    /** Lock for calling {@link #connect} or {@link #disconnect}. */
    Lock connectLock = methodLock.writeLock();

    /** Lock for calling methods other than {@link #connect} or {@link #disconnect}. */
    Lock notConnectLock = methodLock.readLock();

    /** Lock to ensure that methods using the socket, write in sequence. */
    Lock socketLock = new ReentrantLock();

    /** Signal to coordinate the multicasting and waiting for responses. */
    CountDownLatch multicastResponse;


    /////////// New Stuff

    /** Maximum size, in bytes, of data chunk to be sent. */
    private int maxSize = 2100000;

    private int tcpSendBufferSize = 0;

    private boolean tcpNoDelay = false;

    private int myCodaID = 22;

    private String expid;



    /** Constructor. */
    public EmuClient() throws cMsgException {
        domain = "emu";
    }



    /**
     * Method to connect to the server from this client.
     *
     * @throws cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server(s)
     */
    synchronized public void connect() throws cMsgException {

        parseUDL(UDLremainder);

        if (connected) return;
System.out.println("Emu connect: connecting");

        // set the latches
        multicastResponse = new CountDownLatch(1);

System.out.println("Emu connect: create multicast packet");
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
            out.writeInt(name.length());
            out.writeInt(expid.length());

            try {
                out.write( name.getBytes("US-ASCII"));
                out.write(expid.getBytes("US-ASCII"));
            }
            catch (UnsupportedEncodingException e) {/* never happen*/}

            out.flush();
            out.close();

            // create socket to receive at anonymous port & all interfaces
            multicastUdpSocket = new MulticastSocket();
            multicastUdpSocket.setTimeToLive(32);  // Make it through routers

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
System.out.println("Emu connect: start receiver & sender threads");

        // create a thread which will receive any responses to our multicast
System.out.println("Emu connect: client " + name + ": will start multicast receiver thread");
        MulticastReceiver receiver = new MulticastReceiver();
        receiver.start();

        // create a thread which will send our multicast
System.out.println("Emu connect: client " + name + ": will start multicast sender thread");
        Multicaster sender = new Multicaster(udpPacket);
        sender.start();

        // TODO: make sure sender & receiver are started before waiting for a response?

        // wait up to multicast timeout seconds
        boolean response = false;
        if (multicastTimeout > 0) {
            try {
System.out.println("Emu connect: timed wait4  response");
                if (multicastResponse.await(multicastTimeout, TimeUnit.MILLISECONDS)) {
                    response = true;
                }
            }
            catch (InterruptedException e) {}
        }
        // wait forever
        else {
            try {
System.out.println("Emu connect: infinite wait 4 response");
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
        else {
System.out.println("Emu connect: got a response to multicast!");
        }

        // Now that we got a response from the Emu server,
        // we have the info to connect to its TCP listening thread.

        // Create a TCP connection to the Emu Server
        boolean gotTcpConnection = false;
        IOException ioex = null;

        if (serverAddresses.size() > 0) {
            for (InetAddress serverAddr : serverAddresses) {
                try {
System.out.println("Emu connect: Try making tcp connection to server (host = " +
                    serverAddr.getHostName() + ", " +
                    serverAddr.getHostAddress() + "; port = " + tcpServerPort + ")");

                    tcpSocket = new Socket();
                    // Don't waste time if a connection can't be made, timeout = 0.2 sec
                    tcpSocket.connect(new InetSocketAddress(serverAddr, tcpServerPort), 200);
                    tcpSocket.setTcpNoDelay(true);
                    tcpSocket.setSendBufferSize(cMsgNetworkConstants.bigBufferSize);
                    domainOut = new DataOutputStream(new BufferedOutputStream(tcpSocket.getOutputStream(),
                                                                              cMsgNetworkConstants.bigBufferSize));
System.out.println("Emu connect: Made tcp connection to Emu server");
                    serverAddress = serverAddr;
                    gotTcpConnection = true;
                    break;
                }
                catch (SocketTimeoutException e) {
                    System.out.println("Emu connect: connection TIMEOUT");
                    ioex = e;
                }
                catch (IOException e) {
                    System.out.println("Emu connect: connection failed");
                    ioex = e;
                }
            }
        }


        if (!gotTcpConnection) {
            if (domainOut != null) try {domainOut.close();}  catch (IOException e1) {}
            if (tcpSocket != null) try {tcpSocket.close();}  catch (IOException e1) {}
            throw new cMsgException("Cannot make TCP connection to Emu server", ioex);
        }


        try {
            talkToServer();
        }
        catch (IOException e) {
            throw new cMsgException("Communication error with Emu server", e);
        }


        // create request sending (to domain) channel (This takes longest so do last)
        connected = true;

System.out.println("Emu connect: SUCCESSFUL");

        return;
    }



    private void talkToServer() throws IOException {

        try {
            System.out.println("talk to emu domain tcp server");
            // Send emu server some info
            domainOut.writeInt(cMsgNetworkConstants.magicNumbers[0]);
            domainOut.writeInt(cMsgNetworkConstants.magicNumbers[1]);
            domainOut.writeInt(cMsgNetworkConstants.magicNumbers[2]);
            System.out.println("wrote magic ints");

            // version, coda id, buffer size, isBigEndian
            domainOut.writeInt(cMsgConstants.version);
            domainOut.writeInt(myCodaID);
            domainOut.writeInt(maxSize);
            domainOut.writeInt(1);  // any single event is sent in big endian
            System.out.println("done writing");
            domainOut.flush();
            System.out.println("flushed");
        }
        catch (IOException e) {
            e.printStackTrace();
        }
    }



    /**
     * Method to parse the Universal Domain Locator (UDL) into its various components.
     *
     * Emu domain UDL is of the form:<p>
     *   <b>cMsg:emu://&lt;port&gt;/&lt;expid&gt;?myCodaId=&lt;id&gt;&bufSize=&lt;size&gt;&tcpSend=&lt;size&gt;&noDelay     </b><p>
     *
     * Remember that for this domain:
     *<ul>
     *<li>1) multicast address is always 239.230.0.0<p>
     *<li>2) port is required<p>
     *<li>3) expid is required<p>
     *<li>4) myCodaId is required<p>
     *<li>5) optional bufSize (max size in bytes of a single send) defaults to 2.1MB<p>
     *<li>6) optional tcpSend is the TCP send buffer size in bytes<p>
     *<li>7) optional noDelay is the TCP no-delay parameter turned on<p>
     *</ul><p>
     *
     * @param udlRemainder partial UDL to parse
     * @throws cMsgException if udlRemainder is null
     */
    void parseUDL(String udlRemainder) throws cMsgException {

        if (udlRemainder == null) {
            throw new cMsgException("invalid UDL");
        }

        Pattern pattern = Pattern.compile("(\\d+)/([^?&]+)(.*)");
        Matcher matcher = pattern.matcher(udlRemainder);

        String udlPort, udlExpid, remainder;

        if (matcher.find()) {
            // port
            udlPort = matcher.group(1);
            // expid
            udlExpid = matcher.group(2);
            // remainder
            remainder = matcher.group(3);

            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("\nparseUDL: " +
                                   "\n  port      = " + udlPort +
                                   "\n  expid     = " + udlExpid +
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
System.out.println("Port = " + multicastServerPort);

        // Get expid
        if (udlExpid == null) {
            throw new cMsgException("parseUDL: must specify the EXPID");
        }
        expid = udlExpid;
System.out.println("expid = " + udlExpid);

        // If no remaining UDL to parse, return
        if (remainder == null) {
            throw new cMsgException("parseUDL: must specify the CODA id");
        }

        // Look for ?myCodaId=value&
        pattern = Pattern.compile("[\\?]myCodaId=([0-9]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                myCodaID = Integer.parseInt(matcher.group(1));
            }
            catch (NumberFormatException e) {
                throw new cMsgException("parseUDL: improper CODA id", e);
            }
        }
System.out.println("CODA id = " + myCodaID);

        // Look for ?bufSize=value& or &bufSize=value&
        pattern = Pattern.compile("[\\?&]bufSize=([0-9]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                maxSize = Integer.parseInt(matcher.group(1));
System.out.println("max buffer size = " + maxSize);
            }
            catch (NumberFormatException e) {
                // ignore error and keep default value of 2.1MB
            }
        }

        // now look for ?tcpBuf=value& or &tcpBuf=value&
        pattern = Pattern.compile("[\\?&]tcpBuf=([0-9]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                tcpSendBufferSize = Integer.parseInt(matcher.group(1));
System.out.println("tcp send buffer size = " + tcpSendBufferSize);
            }
            catch (NumberFormatException e) {
                // ignore error and keep value of 0
            }
        }

        // now look for ?noDelay& or &noDelay&
        pattern = Pattern.compile("[\\?&]noDelay", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                tcpNoDelay = true;
System.out.println("tcp no-delay = " + tcpNoDelay);
            }
            catch (NumberFormatException e) {
                // ignore error and keep value of false
            }
        }

    }


    /**
     * Method to close the connection to the domain server. This method results in this object
     * becoming functionally useless.
     */
    synchronized public void disconnect() {
        if (!connected) return;

        connected = false;
        multicastUdpSocket.close();
        try {tcpSocket.close();} catch (IOException e) {}
        try {domainOut.close();} catch (IOException e) {}
    }


    /**
     * Method to send a message to the Emu domain server.
     *
     * @param message {@inheritDoc}
     * @throws cMsgException if there are communication problems with the server;
     *                       subject and/or type is null
     */
    synchronized public void send(final cMsgMessage message) throws cMsgException {

        int binaryLength = message.getByteArrayLength();

        try {
            if (!connected) {
                throw new IOException("not connected to server");
            }

            // Type of message is in 1st (lowest) byte.
            // Source (Emu's EventType) of message is in 2nd byte.
            domainOut.writeInt(message.getUserInt());

            // Total length of binary (not including this int)
            domainOut.writeInt(binaryLength);

            // Write byte array
            try {
                if (binaryLength > 0) {
                    domainOut.write(message.getByteArray(),
                                    message.getByteArrayOffset(),
                                    binaryLength);
                }
            }
            catch (UnsupportedEncodingException e) {
            }

            domainOut.flush();
        }
        catch (IOException e) {
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

System.out.println("Emu client " + name + ": STARTED multicast receiving thread");
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
System.out.println("received multicast packet");
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

                    if (addressCount < 1) {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("Multicast receiver: got bad # of addresses ("
                                               + addressCount + ")");
                        }
                        continue;
                    }

                    // Bytes of all ints
                    totalLen = lengthOfInts + 4*addressCount;

                    // Packet have enough data?
                    if (packet.getLength() < totalLen) {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("Multicast receiver: got packet that's too small");
                        }
                        continue;
                    }

                    // Read all server's IP addresses
                    for (int i=0; i < addressCount; i++) {
                        len = cMsgUtilities.bytesToInt(buf, index); index += 4;

                        totalLen += len;
                        if (packet.getLength() < totalLen) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("Multicast receiver: got packet that's too small");
                            }
                            continue newPacket;
                        }

                        // Get & store dotted-decimal IP addresses
                        if (len > 0) {
                            ip = new String(buf, index, len, "US-ASCII");
                            serverAddresses.add(InetAddress.getByName(ip));
System.out.println("host = " + ip);
                        }
                    }

                }
                catch (UnsupportedEncodingException e) {continue;}
                catch (IOException e) {
System.out.println("Got IOException in multicast receive, exiting");
                    return;
                }
                break;
            }

            multicastResponse.countDown();
        }
    }


    /**
     * This class defines a thread to multicast a UDP packet to the
     * RC Multicast server every second.
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
                            if (ni.isUp() && ni.supportsMulticast() && !ni.isLoopback()) {
System.out.println("RC client: sending mcast packet over " + ni.getName());
                                multicastUdpSocket.setNetworkInterface(ni);
                                multicastUdpSocket.send(packet);
                                Thread.sleep(500);
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


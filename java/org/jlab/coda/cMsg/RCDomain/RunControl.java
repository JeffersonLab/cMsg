/*----------------------------------------------------------------------------*
 *  Copyright (c) 2006        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 31-Mar-2006, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.RCDomain;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgCallbackThread;
import org.jlab.coda.cMsg.common.*;

import java.io.*;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.util.*;
import java.net.*;
import java.nio.channels.ServerSocketChannel;

/**
 * This class implements a cMsg client in the RunControl (or RC) domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class RunControl extends cMsgDomainAdapter {

    /** Thread listening for TCP connections and responding to RC domain server commands. */
    private rcListeningThread listeningThread;

    /** Coda experiment id under which this is running. */
    private String expid;

    /**
     * Timeout in seconds to wait for RC server to finish connection
     * once RC multicast server responds. Defaults to 30 seconds.
     */
    private int connectTimeout = 30000;

    /** Local IP address for rc server to connect to. */
    private String specifiedLocalIp;

    /** Subnet corresponding to local IP address for rc server to connect to. */
    private String specifiedLocalSubnet;

    /** Quit a connection in progress if true. */
    volatile boolean abandonConnection;

    /** RunControl server's net addresses obtained from multicast response. */
    volatile InetAddress rcServerAddress;

    /** RunControl server's net address obtained from multicast response. */
    volatile LinkedList<InetAddress> rcServerAddresses = new LinkedList<InetAddress>();

    /** RunControl server's UDP listening port obtained from {@link #connect}. */
    volatile int rcUdpServerPort;

    /** RunControl server's TCP listening port obtained from {@link #connect}. */
    volatile int rcTcpServerPort;

    /** RunControl multicast server's net address obtained from UDL. */
    private InetAddress rcMulticastServerAddress;

    /** RunControl multicast server's multicast listening port obtained from UDL. */
    private int rcMulticastServerPort;

    /**
     * Packet to send over UDP to RC server to implement
     * {@link #send(org.jlab.coda.cMsg.cMsgMessage)}.
     */
    private DatagramPacket sendUdpPacket;

    /** Socket over which to UDP multicast to and receive UDP packets from the RCMulticast server. */
    private MulticastSocket multicastUdpSocket;

    /** Socket over which to end messages to the RC server over UDP. */
    private DatagramSocket udpSocket;

    /** Socket over which to send messages to the RC server over TCP. */
    Socket tcpSocket;

    /** Output TCP data stream from this client to the RC server. */
    DataOutputStream domainOut;

    /**
     * Collection of all of this client's message subscriptions which are
     * {@link cMsgSubscription} objects. This set is synchronized.
     */
    public Set<cMsgSubscription> subscriptions;

    /**
     * HashMap of all of this client's callback threads (keys) and their associated
     * subscriptions (values). The cMsgCallbackThread object of a new subscription
     * is returned (as an Object) as the unsubscribe handle. When this object is
     * passed as the single argument of an unsubscribe, a quick lookup of the
     * subscription is done using this hashmap.
     */
    private Map<Object, cMsgSubscription> unsubscriptions;

    /**
     * This lock is for controlling access to the methods of this class.
     * It is inherently more flexible than synchronizing code. The {@link #connect}
     * and {@link #disconnect} methods of this object cannot be called simultaneously
     * with each other or any other method. However, the {@link #send(org.jlab.coda.cMsg.cMsgMessage)}
     * method is thread-safe and may be called simultaneously from multiple threads.
     */
    private final ReentrantReadWriteLock methodLock = new ReentrantReadWriteLock();

    /** Lock for calling {@link #connect} or {@link #disconnect}. */
    private Lock connectLock = methodLock.writeLock();

    /** Lock for calling methods other than {@link #connect} or {@link #disconnect}. */
    private Lock notConnectLock = methodLock.readLock();

    /**
     * Lock to ensure
     * {@link #subscribe(String, String, org.jlab.coda.cMsg.cMsgCallbackInterface, Object)}
     * and {@link #unsubscribe(org.jlab.coda.cMsg.cMsgSubscriptionHandle)}
     * calls are sequential.
     */
    private Lock subscribeLock = new ReentrantLock();

    /** Lock to ensure that methods using the socket, write in sequence. */
    private Lock socketLock = new ReentrantLock();

    /** Used to create unique id numbers associated with a specific message subject/type pair. */
    private AtomicInteger uniqueId;

    /** Signal to coordinate the multicasting and waiting for responses. */
    private CountDownLatch multicastResponse;

    /** Signal to coordinate the finishing of the 3-leg connect method. */
    CountDownLatch connectCompletion;



    /** Constructor. */
    public RunControl() {
        domain = "rc";
        subscriptions    = new HashSet<cMsgSubscription>(20);
        uniqueId         = new AtomicInteger();
        unsubscriptions  = Collections.synchronizedMap(new HashMap<Object, cMsgSubscription>(20));
    }




    /**
     * Get the host of the RC server that this client is connected to.
     * @return server's host; null if unknown
     */
    public String getServerHost() {return rcServerAddress.getHostAddress();}


    /**
     * Get the TCP port of the RC server that this client is connected to.
     * @return server's port; 0 if unknown
     */
    public int getServerPort() {return rcTcpServerPort;}


    /**
     * Does nothing in this domain.
     *
     * @param timeout {@inheritDoc}
     * @throws cMsgException never thrown
     */
    public void flush(int timeout) throws cMsgException {return;}


    /**
     * Method to connect to the codaComponent server from this client.
     *
     * @throws cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server(s)
     */
    public void connect() throws cMsgException {

        // cannot run this simultaneously with any other public method
        connectLock.lock();

        parseUDL(UDLremainder);

        try {
            if (connected) return;
//System.out.println("RC connect: connecting");

            // set the latches
            multicastResponse = new CountDownLatch(1);
            connectCompletion = new CountDownLatch(1);

            // read env variable for starting port number
            int startingPort=0;
            try {
                String env = System.getenv("CMSG_RC_CLIENT_PORT");
                if (env != null) {
                    startingPort = Integer.parseInt(env);
                }
            }
            catch (NumberFormatException ex) {
            }

            // port #'s < 1024 are reserved
            if (startingPort < 1024) {
                startingPort = cMsgNetworkConstants.rcTcpClientPort;
            }

            // At this point, find a port to bind to. If that isn't possible, throw
            // an exception.
            // Server channel (contains socket)
            ServerSocketChannel serverChannel;
            try {
                serverChannel = ServerSocketChannel.open();
            }
            catch (IOException ex) {
                ex.printStackTrace();
                throw new cMsgException("connect: cannot open a listening socket", ex);
            }

            int port = startingPort;
            ServerSocket listeningSocket = serverChannel.socket();

            while (true) {
                try {
                    listeningSocket.bind(new InetSocketAddress(port));
                    break;
                }
                catch (IOException ex) {
                    // try another port by adding one
                    if (port < 65535) {
                        port++;
                        try { Thread.sleep(100);  }
                        catch (InterruptedException e) {}
                    }
                    else {
                        // close channel
                        try { serverChannel.close(); }
                        catch (IOException e) { }

                        ex.printStackTrace();
                        throw new cMsgException("connect: cannot find port to listen on", ex);
                    }
                }
            }

//System.out.println("RC connect: start listening thread on port " + port);
            // launch thread and start listening on receive socket
            listeningThread = new rcListeningThread(this, serverChannel);
            listeningThread.start();

            // Wait for indication thread is actually running before
            // continuing on. This thread must be running before we talk to
            // the name server since the server tries to communicate with
            // the listening thread.

            synchronized (listeningThread) {
                if (!listeningThread.isAlive()) {
                    try {
                        listeningThread.wait();
                    }
                    catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }
            }

//System.out.println("RC connect: create multicast packet");
            //--------------------------------------------------------------
            // multicast on local subnets to find RunControl Multicast server
            //--------------------------------------------------------------
            DatagramPacket udpPacket;

            // create byte array for multicast
            ByteArrayOutputStream baos = new ByteArrayOutputStream(1024);
            DataOutputStream out = new DataOutputStream(baos);

            try {
                // Put our magic #s, TCP listening port, name, and
                // the EXPID (experiment id string) into byte array.
                out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
                out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
                out.writeInt(cMsgNetworkConstants.magicNumbers[2]);
                // Add this in version 4.0 for future protocol checking
                out.writeInt(cMsgConstants.version);
                out.writeInt(cMsgNetworkConstants.rcDomainMulticastClient); // multicast is from rc domain client
                out.writeInt(port);
                // Add this in version 4.0 for uniquely identifying id number
                // (can't reliably get pid so use time).
                out.writeInt((int)System.currentTimeMillis());
                out.writeInt(name.length());
                out.writeInt(expid.length());
                try {
                    out.write(name.getBytes("US-ASCII"));
                    out.write(expid.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {/* never happen*/}

                //-------------------------------------
                // Now send IP and broadcast addresses
                //-------------------------------------
                int i=0, addrCount = 1;
                String[] ipAddrs;
                String[] broadcastAddrs;

                // If we have a single, specified, local IP for rc server to connect to,
                // use that to the exclusion of all else.
                if (specifiedLocalIp != null && specifiedLocalSubnet != null) {
                    ipAddrs        = new String[] {specifiedLocalIp};
                    broadcastAddrs = new String[] {specifiedLocalSubnet};
                }
                else {
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

                // create socket to receive at anonymous port & all interfaces
                multicastUdpSocket = new MulticastSocket();

                // Pick local port for socket to avoid being assigned a port
                // to which cMsgServerFinder is multicasting.
                int localPort = cMsgNetworkConstants.rcUdpClientPort;
                while (true) {
                    try {
                        multicastUdpSocket.bind(new InetSocketAddress(localPort));
                        break;
                    }
                    catch (IOException ex) {
                        // try another port by adding one
                        if (localPort < 65535) {
                            localPort++;
                            try { Thread.sleep(100);  }
                            catch (InterruptedException e) {}
                        }
                        else {
                            // close socket
                            multicastUdpSocket.close();
                            throw new cMsgException("connect: cannot find local UDP port", ex);
                        }
                    }
                }

                multicastUdpSocket.setTimeToLive(32);  // Make it through routers

                // create packet to multicast from the byte array
                byte[] buf = baos.toByteArray();
                udpPacket = new DatagramPacket(buf, buf.length,
                                               rcMulticastServerAddress,
                                               rcMulticastServerPort);
                baos.close();
            }
            catch (IOException e) {
                listeningThread.killThread();
                try { out.close();} catch (IOException e1) {}
                try {baos.close();} catch (IOException e1) {}
                if (multicastUdpSocket != null) multicastUdpSocket.close();

                if (debug >= cMsgConstants.debugError) {
                    System.out.println("I/O Error: " + e);
                }
                throw new cMsgException(e.getMessage(), e);
            }
//System.out.println("RC connect: start sender thread");

            // create a thread which will send our multicast
//System.out.println("RC connect: RC client " + name + ": will start multicast sender thread");
            Multicaster sender = new Multicaster(udpPacket);
            sender.start();

            // Now that we got a response from the RC Multicast server,
            // wait for that server to pass its info on to the RC server
            // which should complete this connect by sending a "connect"
            // message to our listening thread.

            // wait up to connect timeout seconds
            boolean completed = false;
            if (connectTimeout > 0) {
                try {
//System.out.println("RC connect: waiting for a response to final connection (with timeout)");
                    if (connectCompletion.await(connectTimeout, TimeUnit.MILLISECONDS)) {
                        completed = true;
                    }
                }
                catch (InterruptedException e) {}
            }
            // wait forever
            else {
//System.out.println("RC connect: waiting for a response to final connection (no timeout)");
                try { connectCompletion.await(); completed = true;}
                catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }

            // RC Multicast server told me to abandon the connection attempt
            if (abandonConnection) {
                sender.interrupt();
                listeningThread.killThread();
                throw new cMsgException("RC Multicast server says to abort the connect attempt");
            }

            if (!completed) {
//System.out.println("RC connect: Did NOT complete the connection");
                sender.interrupt();
                listeningThread.killThread();
                throw new cMsgException("No connect from the RC server received");
            }
            else {
//System.out.println("RC connect: Completed the connection from RC server");
            }

            // Stop sending multicast packets now
            multicastUdpSocket.close();
            sender.interrupt();

            // create a TCP connection to the RC Server
            boolean gotTcpConnection = false;
            IOException ioex = null;

            if (rcServerAddresses.size() > 0) {
                for (InetAddress rcServerAddr : rcServerAddresses) {
                    try {
//System.out.println("RC connect: Try making tcp connection to RC server (host = " + rcServerAddr.getHostName() + ", " +
//                   rcServerAddr.getHostAddress() + "; port = " + rcTcpServerPort + ")");

                        tcpSocket = new Socket();
                        // don't waste time if a connection cannot be made, timeout = 2 seconds
                        tcpSocket.connect(new InetSocketAddress(rcServerAddr, rcTcpServerPort), 2000);
                        tcpSocket.setTcpNoDelay(true);
                        tcpSocket.setSendBufferSize(cMsgNetworkConstants.bigBufferSize);
                        domainOut = new DataOutputStream(new BufferedOutputStream(tcpSocket.getOutputStream(),
                                                                                  cMsgNetworkConstants.bigBufferSize));
//System.out.println("RC connect: Made tcp connection to RC server");
System.out.println("RC connect: made tcp connection to host " + rcServerAddr + ", port " + rcTcpServerPort);
                        rcServerAddress = rcServerAddr;
                        gotTcpConnection = true;
                        break;
                    }
                    catch (SocketTimeoutException e) {
//System.out.println("RC connect: connection TIMEOUT");
                        ioex = e;
                    }
                    catch (IOException e) {
//System.out.println("RC connect: connection failed");
                        ioex = e;
                    }
                }
            }


            if (!gotTcpConnection) {
                listeningThread.killThread();
                if (domainOut != null) try {domainOut.close();}  catch (IOException e1) {}
                if (tcpSocket != null) try {tcpSocket.close();}  catch (IOException e1) {}
                throw new cMsgException("Cannot make TCP connection to RC server", ioex);
            }


            // Create a UDP "connection". This means security check is done only once
            // and communication with any other host/port is not allowed.
            // create socket to receive at anonymous port & all interfaces
            try {
                udpSocket = new DatagramSocket();
                udpSocket.setReceiveBufferSize(cMsgNetworkConstants.bigBufferSize);
            }
            catch (SocketException e) {
                listeningThread.killThread();
                if (udpSocket != null) udpSocket.close();
                e.printStackTrace();
            }
//System.out.println("RC connect: Make udp connection to RC server");
            udpSocket.connect(rcServerAddress, rcUdpServerPort);
            sendUdpPacket = new DatagramPacket(new byte[0], 0, rcServerAddress, rcUdpServerPort);

            // create request sending (to domain) channel (This takes longest so do last)
            connected = true;
        }
        finally {
            connectLock.unlock();
        }
System.out.println("RC connect: SUCCESSFUL");

        return;
    }


    /**
     * This method is a synchronous call to receive a message containing monitoring data
     * from the rc multicast server specified in the UDL.
     * In this case, the "monitoring" data is just the host on which the rc multicast
     * server is running. It can be found by calling the
     * {@link cMsgMessage#getSenderHost()} method of the returned message.
     * Get the whole list of server IP addresses in the
     * String array payload item called "IpAddresses". This is useful when trying to find
     * the location of a particular AFECS (runcontrol) platform.
     *
     * @param  command time in milliseconds to wait for a response to multicasts (1000 default)
     * @return response message containing the host running the rc multicast server contacted;
     *         null if no response is found
     * @throws cMsgException
     */
    public cMsgMessage monitor(String command) throws cMsgException {

        /** Tell us receiver thread has started. */
        final CountDownLatch startLatch = new CountDownLatch(1);

        /** This class gets the response to our UDP multicast to an rc multicast server. */
        class rcMulticastReceiver extends Thread {

            String host;
            DatagramSocket socket;
            volatile cMsgMessageFull msg;

            rcMulticastReceiver(DatagramSocket socket) {this.socket = socket;}

            public cMsgMessage getMsg() {return msg;}

            public String getMsgHost() {
                String name = null;
                try {
                    name = Inet4Address.getByName(host).getCanonicalHostName();
                }
                catch (UnknownHostException e) {}
                return name;
            }


            public void run() {

                int index;
                byte[] buf = new byte[1024];
                DatagramPacket packet = new DatagramPacket(buf, 1024);

                // Thread has started ...
                startLatch.countDown();

                while (true) {
                    // Reset for each round
                    packet.setLength(1024);

                    try {
                        socket.receive(packet);
                        //System.out.println("received UDP packet");
                        // if we get too small of a packet, reject it
                        if (packet.getLength() < 6*4) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("monitor: got packet that's too small");
                            }
                            continue;
                        }
                        int magic1 = cMsgUtilities.bytesToInt(buf, 0);
                        int magic2 = cMsgUtilities.bytesToInt(buf, 4);
                        int magic3 = cMsgUtilities.bytesToInt(buf, 8);
                        if (magic1 != cMsgNetworkConstants.magicNumbers[0] ||
                                magic2 != cMsgNetworkConstants.magicNumbers[1] ||
                                magic3 != cMsgNetworkConstants.magicNumbers[2])  {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("monitor: got bad magic # response to multicast");
                            }
                            continue;
                        }

                        int version  = cMsgUtilities.bytesToInt(buf, 12);
                        int port     = cMsgUtilities.bytesToInt(buf, 16);
                        int hostLen  = cMsgUtilities.bytesToInt(buf, 20);
                        int expidLen = cMsgUtilities.bytesToInt(buf, 24);

                        if (packet.getLength() < 4*6 + hostLen + expidLen) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("monitor: got packet that's too small");
                            }
                            continue;
                        }

                        // get host
                        index = 28;
                        host = "";
                        if (hostLen > 0) {
                            host = new String(buf, index, hostLen, "US-ASCII");
                            //System.out.println("host = " + host);
                            index += hostLen;
                        }

                        // get expid
                        String serverExpid;
                        if (expidLen > 0) {
                            serverExpid = new String(buf, index, expidLen, "US-ASCII");
                            index += expidLen;
                            //System.out.println("monitor: got return expid = " + serverExpid);
                            if (!expid.equals(serverExpid)) {
                                if (debug >= cMsgConstants.debugWarn) {
                                    System.out.println("monitor: ignore response from expid = " + serverExpid);
                                }
                                continue;
                            }
                        }

                        int ipCount = cMsgUtilities.bytesToInt(buf, index); index += 4;
//System.out.println("monitor: got ipCount = " + ipCount);

                        if (ipCount < 0 || ipCount > 50) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("monitor: bad ip address count (" + ipCount + ")");
                            }
                            continue;
                        }

                        int ipLen;
                        String[] ipAddrs = new String[ipCount];
                        for (int i=0; i < ipCount; i++) {
                            // Read in IP address
                            ipLen = cMsgUtilities.bytesToInt(buf, index); index += 4;
                            if (ipLen > 0) {
                                ipAddrs[i] = new String(buf, index, ipLen, "US-ASCII");
//System.out.println("monitor: ip = " + ipAddrs[i]);
                                index += ipLen;
                            }

                            // Skip over broadcast address
                            ipLen = cMsgUtilities.bytesToInt(buf, index);
                            index += 4 + ipLen;
                        }

                        // store in message
                        msg = new cMsgMessageFull();
                        msg.setSenderHost(host);
                        if (ipCount > 0) {
                            try {
                                msg.addPayloadItem(new cMsgPayloadItem("IpAddresses", ipAddrs));
                            }
                            catch (cMsgException e) {/* never happen */}
                        }
                    }
                    catch (InterruptedIOException e) {
                        //System.out.println("  Interrupted receiving thread so return");
                        return;
                    }
                    catch (IOException e) {
                        //System.out.println("  IO exception in receiving thread so return");
                        return;
                    }
                    break;
                }
            }
        }
        // cannot run this simultaneously with any other public method
        connectLock.lock();

        // parse only if necessary
        if (expid == null) {
            parseUDL(UDLremainder);
        }

        // Time in milliseconds waiting for a response to the multicasts.
        // 1 second default.
        int sleepTime = 1000;
        if (command != null) {
            try {
                int t = Integer.parseInt(command);
                if (t > -1) {
                    sleepTime = t;
                }
            }
            catch (NumberFormatException e) {
            }
        }

        try {
            //--------------------------------------------------------------
            // multicast on local subnet to find RunControl Multicast server
            //--------------------------------------------------------------
            byte[] buffer;
            MulticastSocket socket = null;

            // create byte array for multicast
            ByteArrayOutputStream baos = new ByteArrayOutputStream(1024);
            DataOutputStream out = new DataOutputStream(baos);

            try {
                // Put our magic #s, TCP listening port, name, and
                // the EXPID (experiment id string) into byte array.
                out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
                out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
                out.writeInt(cMsgNetworkConstants.magicNumbers[2]);
                out.writeInt(cMsgConstants.version);
                // multicast is from rc domain prober
                out.writeInt(cMsgNetworkConstants.rcDomainMulticastProbe);
                // use port number = 0 to indicate monitor probe
                out.writeInt(0);
                // id not used in probe, set to 0
                out.writeInt(0);
                out.writeInt(name.length());
                out.writeInt(expid.length());
                try {
                    out.write(name.getBytes("US-ASCII"));
                    out.write(expid.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {
                }
                out.flush();
                out.close();

                // create socket to receive at anonymous port & all interfaces
                socket = new MulticastSocket();
                socket.setReceiveBufferSize(1024);
                socket.setSoTimeout(sleepTime);
                socket.setTimeToLive(32);

                // create multicast packet from the byte array
                baos.close();
                buffer = baos.toByteArray();
            }
            catch (IOException e) {
                try { out.close();} catch (IOException e1) {}
                try {baos.close();} catch (IOException e1) {}
                if (socket != null) socket.close();
                System.out.println("Cannot create rc multicast packet");
                return null;
            }

            // create a thread which will receive any responses to our multicast
            rcMulticastReceiver receiver = new rcMulticastReceiver(socket);
            receiver.start();

            // make sure it's started before we start sending stuff
            try {startLatch.await();}
            catch (InterruptedException e) {}

            // send our multicast packet
            DatagramPacket packet = null;
            try {
                InetAddress addr = InetAddress.getByName(cMsgNetworkConstants.rcMulticast);
//System.out.println("monitor: send probe packet on port " + rcMulticastServerPort);
                packet = new DatagramPacket(buffer, buffer.length, addr, rcMulticastServerPort);
            }
            catch (UnknownHostException e) { /* never thrown */ }


            // wait up to multicast timeout seconds for a response
            int waitTime = 0;

            // send a packet over each network interface.
            try {
                Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();

                while (enumer.hasMoreElements()) {
                    NetworkInterface ni = enumer.nextElement();
                    if (ni.isUp()) {
                        socket.setNetworkInterface(ni);
                        socket.send(packet);

                        // place a delay between each
                        try { Thread.sleep(250); }
                        catch (InterruptedException e) {}
                    }
                }
            }
            catch (IOException e) {}

            while (receiver.getMsg() == null && waitTime < sleepTime) {
                // wait 1/2 second & check for response
                try { Thread.sleep(500); }
                catch (InterruptedException e) { }
                waitTime += 500;
            }

            // return the first legitimate response or null if none
            return receiver.getMsg();
        }
        finally {
            connectLock.unlock();
        }
    }



    /**
     * Method to parse the Universal Domain Locator (UDL) into its various components.
     *
     * Runcontrol domain UDL is of the form:<p>
     *   <b>cMsg:rc://&lt;host&gt;:&lt;port&gt;/&lt;expid&gt;?connectTO=&lt;timeout&gt;&ip=&lt;address&gt;</b><p>
     *
     * Remember that for this domain:
     *<ul>
     *<li>host is required and may also be "multicast", "localhost", or in dotted decimal form<p>
     *<li>port is optional with a default of {@link cMsgNetworkConstants#rcMulticastPort}<p>
     *<li>the experiment id or expid is required, it is NOT taken from the environmental variable EXPID<p>
     *<li>connectTO (optional) is the time to wait in seconds before connect returns a
     *    timeout while waiting for the rc server to send a special (tcp)
     *    concluding connect message. Defaults to 30 seconds.<p>
     *<li>ip (optional) is ip address in dot-decimal format which the rc server
     *    or agent must use to connect to this rc client.<p>
     *</ul><p>
     *
     * @param udlRemainder partial UDL to parse
     * @throws cMsgException if udlRemainder is null
     */
    void parseUDL(String udlRemainder) throws cMsgException {

        if (udlRemainder == null) {
            throw new cMsgException("invalid UDL");
        }

        Pattern pattern = Pattern.compile("([^:/?]+):?(\\d+)?/([^?&]+)(.*)");
        Matcher matcher = pattern.matcher(udlRemainder);

        String udlHost, udlPort, udlExpid, remainder;

        if (matcher.find()) {
            // host
            udlHost = matcher.group(1);
            // port
            udlPort = matcher.group(2);
            // expid
            udlExpid = matcher.group(3);
            // remainder
            remainder = matcher.group(4);

            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("\nparseUDL: " +
                                   "\n  host      = " + udlHost +
                                   "\n  port      = " + udlPort +
                                   "\n  expid     = " + udlExpid +
                                   "\n  remainder = " + remainder);
            }
        }
        else {
            throw new cMsgException("invalid UDL");
        }

        // if host given ...
        if (udlHost == null) {
            throw new cMsgException("parseUDL: must specify a host (or multicast, localhost)");
        }

        // if the host is "localhost", find the actual, fully qualified  host name
        if (udlHost.equalsIgnoreCase("multicast")) {
            udlHost = cMsgNetworkConstants.rcMulticast;
//System.out.println("Will multicast to " + cMsgNetworkConstants.rcMulticast);
        }
        else {
            if (udlHost.equalsIgnoreCase("localhost")) {
                try {
                    udlHost = InetAddress.getLocalHost().getCanonicalHostName();
//System.out.println("Will unicast to host " + udlHost);
                    if (debug >= cMsgConstants.debugWarn) {
                        System.out.println("parseUDL: codaComponent host given as \"localhost\", substituting " +
                                udlHost);
                    }
                }
                catch (UnknownHostException e) {
                    udlHost = cMsgNetworkConstants.rcMulticast;
//System.out.println("Will multicast to " + cMsgNetworkConstants.rcMulticast);
                }
            }
            else {
                try {
                    if (InetAddress.getByName(udlHost).isMulticastAddress()) {
//System.out.println("Will multicast to " + udlHost);
                    }
                    else {
                        udlHost = InetAddress.getByName(udlHost).getCanonicalHostName();
//System.out.println("Will unicast to host " + udlHost);
                    }
                }
                catch (UnknownHostException e) {
                    udlHost = cMsgNetworkConstants.rcMulticast;
//System.out.println("Will multicast to " + cMsgNetworkConstants.rcMulticast);
                }
            }
        }

        try {
            rcMulticastServerAddress = InetAddress.getByName(udlHost);
        }
        catch (UnknownHostException e) {
            throw new cMsgException("parseUDL: cannot find host", e);
        }


        // get multicast server port or guess if it's not given
        if (udlPort != null && udlPort.length() > 0) {
            try { rcMulticastServerPort = Integer.parseInt(udlPort); }
            catch (NumberFormatException e) {
                rcMulticastServerPort = cMsgNetworkConstants.rcMulticastPort;
                if (debug >= cMsgConstants.debugWarn) {
                    System.out.println("parseUDL: non-integer port, guessing codaComponent port is " + rcMulticastServerPort);
                }
            }
        }
        else {
            rcMulticastServerPort = cMsgNetworkConstants.rcMulticastPort;
            if (debug >= cMsgConstants.debugWarn) {
                System.out.println("parseUDL: guessing codaComponent port is " + rcMulticastServerPort);
            }
        }

        if (rcMulticastServerPort < 1024 || rcMulticastServerPort > 65535) {
            throw new cMsgException("parseUDL: illegal port number");
        }
//System.out.println("Port = " + rcMulticastServerPort);

        // if no expid to parse, return
        if (udlExpid == null) {
            throw new cMsgException("parseUDL: must specify the EXPID");
        }
        expid = udlExpid;
//System.out.println("expid = " + expid);

        // if no remaining UDL to parse, return
        if (remainder == null) {
            return;
        }

        // now look for ?connectTO=value or &connectTO=value
        pattern = Pattern.compile("[\\?&]connectTO=([0-9]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                connectTimeout = 1000 * Integer.parseInt(matcher.group(1));
//System.out.println("connect TO = " + connectTimeout);
            }
            catch (NumberFormatException e) {
                // ignore error and keep value of 0
            }
        }

        // now look for ?ip=value or &ip=value
        pattern = Pattern.compile("[\\?&]ip=((?:[0-9]{1,3}\\.){3}[0-9]{1,3})", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            specifiedLocalIp = matcher.group(1);
            try {
                // Get related broadcast addr. Will also check if in dot-decimal format
                specifiedLocalSubnet = cMsgUtilities.getBroadcastAddress(specifiedLocalIp);
                // If not local, forget it ...
                if (specifiedLocalSubnet == null) {
                    specifiedLocalIp = null;
                }
            }
            catch (cMsgException e) {
                // Not dot-decimal format
                specifiedLocalIp = specifiedLocalSubnet = null;
            }
//System.out.println("IP = " + specifiedLocalIp + ", subnet IP = " + specifiedLocalSubnet);
        }

    }
    

    /**
     * Method to close the connection to the domain server. This method results in this object
     * becoming functionally useless.
     */
    public void disconnect() {
        // cannot run this simultaneously with any other public method
        connectLock.lock();

        try {

            if (!connected) return;

            connected = false;
            multicastUdpSocket.close();
            udpSocket.close();
            try {tcpSocket.close();} catch (IOException e) {}
            try {domainOut.close();} catch (IOException e) {}

            // stop listening and client communication thread & close channel
            listeningThread.killThread();

            // stop all callback threads
            synchronized (subscriptions) {
                for (cMsgSubscription sub : subscriptions) {
                    // run through all callbacks
                    for (cMsgCallbackThread cbThread : sub.getCallbacks()) {
                        // Tell the callback thread(s) to wakeup and die
                        if (Thread.currentThread() == cbThread) {
                            //System.out.println("Don't interrupt my own thread!!!");
                            cbThread.dieNow(false);
                        }
                        else {
                            cbThread.dieNow(true);
                        }
                    }
                }
                // empty all hash tables
                subscriptions.clear();
                unsubscriptions.clear();
            }
        }
        finally {
            connectLock.unlock();
        }
    }


    /**
     * Method to send a message to the domain server for further distribution.
     *
     * @param message {@inheritDoc}
     * @throws cMsgException if there are communication problems with the server;
     *                       subject and/or type is null
     */
    public void send(final cMsgMessage message) throws cMsgException {

        if (!message.getReliableSend()) {
            udpSend(message);
            return;
        }

        String subject = message.getSubject();
        String type    = message.getType();

        // check message fields first
        if (subject == null || type == null) {
            throw new cMsgException("message subject and/or type is null");
        }

        // check for null text
        String text = message.getText();
        int textLen = 0;
        if (text != null) {
            textLen = text.length();
        }

        // Payload stuff. Do NOT keep track of sender history.
        String payloadTxt = message.getPayloadText();
        int payloadLen = 0;
        if (payloadTxt != null) {
            payloadLen = payloadTxt.length();
        }

        int msgType = cMsgConstants.msgSubscribeResponse;
        if (message.isGetResponse()) {
            msgType = cMsgConstants.msgGetResponse;
        }

        int binaryLength = message.getByteArrayLength();

        // cannot run this simultaneously with connect, reconnect, or disconnect
        notConnectLock.lock();
        // protect communications over socket
        socketLock.lock();
        try {
            if (!connected) {
                throw new IOException("not connected to server");
            }

            // length not including first int
            int totalLength = (4 * 15) + name.length() + subject.length() +
                    type.length() + payloadLen + textLen + binaryLength;

            // total length of msg (not including this int) is 1st item
            domainOut.writeInt(totalLength);
            domainOut.writeInt(msgType);
            domainOut.writeInt(cMsgConstants.version);
            domainOut.writeInt(message.getUserInt());
            domainOut.writeInt(message.getInfo());
            domainOut.writeInt(message.getSenderToken());

            long now = new Date().getTime();
            // send the time in milliseconds as 2, 32 bit integers
            domainOut.writeInt((int) (now >>> 32)); // higher 32 bits
            domainOut.writeInt((int) (now & 0x00000000FFFFFFFFL)); // lower 32 bits
            domainOut.writeInt((int) (message.getUserTime().getTime() >>> 32));
            domainOut.writeInt((int) (message.getUserTime().getTime() & 0x00000000FFFFFFFFL));

            domainOut.writeInt(name.length());
            domainOut.writeInt(subject.length());
            domainOut.writeInt(type.length());
            domainOut.writeInt(payloadLen);
            domainOut.writeInt(textLen);
            domainOut.writeInt(binaryLength);

            // write strings & byte array
            try {
                domainOut.write(name.getBytes("US-ASCII"));
                domainOut.write(subject.getBytes("US-ASCII"));
                domainOut.write(type.getBytes("US-ASCII"));
                if (payloadLen > 0) {
                    domainOut.write(payloadTxt.getBytes("US-ASCII"));
                }
                if (textLen > 0) {
                    domainOut.write(text.getBytes("US-ASCII"));
                }
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
        finally {
            socketLock.unlock();
            notConnectLock.unlock();
        }
    }

//-----------------------------------------------------------------------------


    /**
     * Method to send a message to the domain server over UDP for further distribution.
     *
     * @param message message to send
     * @throws cMsgException if there are communication problems with the server;
     *                       subject and/or type is null; message is too big for
     *                       UDP packet size if doing UDP send
     */
    private void udpSend(cMsgMessage message) throws cMsgException {

        String subject = message.getSubject();
        String type    = message.getType();

        // check message fields first
        if (subject == null || type == null) {
            throw new cMsgException("message subject and/or type is null");
        }

        // check for null text
        String text = message.getText();
        int textLen = 0;
        if (text != null) {
            textLen = text.length();
        }

        // Payload stuff. Do NOT keep track of sender history.
        String payloadTxt = message.getPayloadText();
        int payloadLen = 0;
        if (payloadTxt != null) {
            payloadLen = payloadTxt.length();
        }

        int msgType = cMsgConstants.msgSubscribeResponse;
        if (message.isGetResponse()) {
//System.out.println("sending get-response with UDP");
            msgType = cMsgConstants.msgGetResponse;
        }

        int binaryLength = message.getByteArrayLength();

        // total length of msg (not including first int which is this size)
        int totalLength = (4 * 15) + name.length() + subject.length() +
                type.length() + payloadLen + textLen + binaryLength;

        if (totalLength > 8192) {
            throw new cMsgException("Too big a message for UDP to send");
        }

        // create byte array for multicast
        ByteArrayOutputStream baos = new ByteArrayOutputStream(8192);
        DataOutputStream out = new DataOutputStream(baos);

        // cannot run this simultaneously with connect, reconnect, or disconnect
        notConnectLock.lock();

        try {
            if (!connected) {
                throw new IOException("not connected to server");
            }

            out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
            out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
            out.writeInt(cMsgNetworkConstants.magicNumbers[2]);

            out.writeInt(totalLength); // total length of msg (not including this int)
            out.writeInt(msgType);
            out.writeInt(cMsgConstants.version);
            out.writeInt(message.getUserInt());
            out.writeInt(message.getInfo());
            out.writeInt(message.getSenderToken());

            long now = new Date().getTime();
            // send the time in milliseconds as 2, 32 bit integers
            out.writeInt((int) (now >>> 32)); // higher 32 bits
            out.writeInt((int) (now & 0x00000000FFFFFFFFL)); // lower 32 bits
            out.writeInt((int) (message.getUserTime().getTime() >>> 32));
            out.writeInt((int) (message.getUserTime().getTime() & 0x00000000FFFFFFFFL));

            out.writeInt(name.length());
            out.writeInt(subject.length());
            out.writeInt(type.length());
            out.writeInt(payloadLen);
            out.writeInt(textLen);
            out.writeInt(binaryLength);

            // write strings & byte array
            try {
                out.write(name.getBytes("US-ASCII"));
                out.write(subject.getBytes("US-ASCII"));
                out.write(type.getBytes("US-ASCII"));
                if (payloadLen > 0) {
                    out.write(payloadTxt.getBytes("US-ASCII"));
                }
                if (textLen > 0) {
                    out.write(text.getBytes("US-ASCII"));
                }
                if (binaryLength > 0) {
                    out.write(message.getByteArray(),
                              message.getByteArrayOffset(),
                              binaryLength);
                }
            }
            catch (UnsupportedEncodingException e) {
            }
            out.flush();
            out.close();

            // send message packet from the byte array
            byte[] buf = baos.toByteArray();

            synchronized (sendUdpPacket) {
                // setData is synchronized on the packet.
                sendUdpPacket.setData(buf, 0, buf.length);
                // send in synchronized internally on the packet object.
                // Because we only use one packet object for this client,
                // all udp sends are synchronized.
                udpSocket.send(sendUdpPacket);
            }
        }
        catch (IOException e) {
            throw new cMsgException("Cannot create or send message packet", e);
        }
        finally {
            notConnectLock.unlock();
        }

    }


//-----------------------------------------------------------------------------


    /**
     * Method to subscribe to receive messages of a subject and type from the domain server.
     *
     * Note about the server failing and an IOException being thrown. All existing
     * subscriptions are resubscribed on the new failover server by the keepAlive thread.
     * However, this routine will recover from an IO error during the subscribe itself
     * if the failover is successful.
     *
     * @param subject {@inheritDoc}
     * @param type    {@inheritDoc}
     * @param cb      {@inheritDoc}
     * @param userObj {@inheritDoc}
     * @return {@inheritDoc}
     * @throws cMsgException if the callback, subject and/or type is null or blank;
     *                       an identical subscription already exists; there are
     *                       communication problems with the server
     */
    public cMsgSubscriptionHandle subscribe(String subject, String type, cMsgCallbackInterface cb, Object userObj)
            throws cMsgException {

        // check args first
        if (subject == null || type == null || cb == null) {
            throw new cMsgException("subject, type or callback argument is null");
        }
        else if (subject.length() < 1 || type.length() < 1) {
            throw new cMsgException("subject or type is blank string");
        }

        cMsgCallbackThread cbThread = null;

        cMsgSubscription newSub;

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();
        // cannot run this simultaneously with unsubscribe
        // or itself (iterate over same hashtable)
        subscribeLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            // add to callback list if subscription to same subject/type exists

            int id;

            // client listening thread may be iterating thru subscriptions concurrently
            // and we may change set structure
            synchronized (subscriptions) {

                // for each subscription ...
                for (cMsgSubscription sub : subscriptions) {
                    // If subscription to subject & type exist already, keep track of it locally
                    // and don't bother the server since any matching message will be delivered
                    // to this client anyway.
                    if (sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                        // add to existing set of callbacks
                        cbThread = new cMsgCallbackThread(cb, userObj, domain, subject, type);
                        sub.addCallback(cbThread);
                        unsubscriptions.put(cbThread, sub);
                        return cbThread;
                    }
                }

                // If we're here, the subscription to that subject & type does not exist yet.
                // We need to create it.

                // First generate a unique id for the receiveSubscribeId field. This info
                // allows us to unsubscribe.
                id = uniqueId.getAndIncrement();

                // add a new subscription & callback
                cbThread = new cMsgCallbackThread(cb, userObj, domain, subject, type);
                newSub = new cMsgSubscription(subject, type, id, cbThread);
                unsubscriptions.put(cbThread, newSub);

                // client listening thread may be iterating thru subscriptions concurrently
                // and we're changing the set structure
                subscriptions.add(newSub);
            }
        }
        finally {
            subscribeLock.unlock();
            notConnectLock.unlock();
        }

        return cbThread;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to unsubscribe a previous subscription to receive messages of a subject and type
     * from the domain server.
     *
     * Note about the server failing and an IOException being thrown. To have "unsubscribe" make
     * sense on the failover server, we must wait until all existing subscriptions have been
     * successfully resubscribed on the new server.
     *
     * @param obj {@inheritDoc}
     * @throws cMsgException if there are communication problems with the server; object arg is null
     */
    public void unsubscribe(cMsgSubscriptionHandle obj)
            throws cMsgException {

        // check arg first
        if (obj == null) {
            throw new cMsgException("argument is null");
        }

        // unsubscriptions is concurrent hashmap so this is OK
        cMsgSubscription sub = unsubscriptions.remove(obj);
        // already unsubscribed
        if (sub == null) {
            return;
        }
        cMsgCallbackThread cbThread = (cMsgCallbackThread) obj;

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();
        // cannot run this simultaneously with subscribe
        // or itself (iterate over same hashtable)
        subscribeLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            // If there are still callbacks left,
            // don't unsubscribe for this subject/type
            synchronized (subscriptions) {
                if (sub.numberOfCallbacks() > 1) {
                    // kill callback thread
                    cbThread.dieNow(false);
                    // remove this callback from the set
                    sub.getCallbacks().remove(cbThread);
                    return;
                }

                // Delete stuff from hashes & kill threads
                cbThread.dieNow(false);
                sub.getCallbacks().remove(cbThread);
                subscriptions.remove(sub);
            }
        }
        finally {
            subscribeLock.unlock();
            notConnectLock.unlock();
        }
    }


    /**
     * This class defines a thread to multicast a UDP packet to the
     * RC Multicast server every second.
     */
    class Multicaster extends Thread {

        DatagramPacket packet;

        Multicaster(DatagramPacket udpPacket) { packet = udpPacket; }


        public void run() {

//System.out.println("RC connect: client " + name + ": STARTED multicast sending thread");
            try {
                // A slight delay here will help the main thread (calling connect)
                // to be already waiting for a response from the server when we
                // multicast to the server here (prompting that response). This
                // will help insure no responses will be lost.
                Thread.sleep(100);

                while (true) {
                    try {
                        // Send a packet over each network interface.
                        // Place a delay between each.
                        Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();

                        while (enumer.hasMoreElements()) {
                            NetworkInterface ni = enumer.nextElement();
//System.out.println("RC client: found interface " + ni +
//                   ", up = " + ni.isUp() +
//                   ", loopback = " + ni.isLoopback() +
//                   ", has multicast = " + ni.supportsMulticast());
                            if (ni.isUp()) {
//System.out.println("RC client: sending mcast packet over " + ni.getName());
                                multicastUdpSocket.setNetworkInterface(ni);
                                multicastUdpSocket.send(packet);

                                Thread.sleep(200);
                            }
                        }
                    }
                    catch (IOException e) {
                        e.printStackTrace();
                    }

                    // One second between rounds
                    Thread.sleep(1000);
                }
            }
            catch (InterruptedException e) {
                // time to quit
//System.out.println("Interrupted sender");
            }
        }
    }



}


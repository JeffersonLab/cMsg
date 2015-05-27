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

    /** Timeout in milliseconds to wait for server to respond to multicasts. */
    private int multicastTimeout;

    /**
     * Timeout in seconds to wait for RC server to finish connection
     * once RC multicast server responds. Defaults to 30 seconds.
     */
    private int connectTimeout = 30000;

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
     * Does nothing in this domain.
     *
     * @param timeout {@inheritDoc}
     * @throws cMsgException never thrown
     */
    public void flush(int timeout) throws cMsgException {
        return;
    }



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
                startingPort = cMsgNetworkConstants.rcClientPort;
            }

            // At this point, find a port to bind to. If that isn't possible, throw
            // an exception.
            /** Server channel (contains socket). */
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
                out.writeInt(cMsgNetworkConstants.rcDomainMulticastClient); // multicast is from rc domain client
                out.writeInt(port);
                out.writeInt(name.length());
                out.writeInt(expid.length());
                try {
                    out.write(name.getBytes("US-ASCII"));
                    out.write(expid.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {/* never happen*/}

                // List of our IP addresses (starting w/ canonical)
                Collection<String> ipAddrs = cMsgUtilities.getAllIpAddresses();
                out.writeInt(ipAddrs.size());
//System.out.println("RC connect to rcm server: ip list size = " + ipAddrs.size());
                for (String s : ipAddrs) {
                    try {
                        out.writeInt(s.length());
//System.out.println("RC connect to rcm server: ip size = " + s.length());
                        out.write(s.getBytes("US-ASCII"));
//System.out.println("RC connect to rcm server: ip = " + s);
                    }
                    catch (UnsupportedEncodingException e) {/* never happen*/}
                }

                out.flush();
                out.close();

                // create socket to receive at anonymous port & all interfaces
                multicastUdpSocket = new MulticastSocket();
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
//System.out.println("RC connect: start receiver & sender threads");

            // create a thread which will receive any responses to our multicast
//System.out.println("RC connect: RC client " + name + ": will start multicast receiver thread");
            MulticastReceiver receiver = new MulticastReceiver();
            receiver.start();

            // create a thread which will send our multicast
//System.out.println("RC connect: RC client " + name + ": will start multicast sender thread");
            Multicaster sender = new Multicaster(udpPacket);
            sender.start();

            // Wait up to multicast timeout seconds for return UDP packet from
            // rc multicast server.
            //
            // There is a potential problem if UDP packets coming back from the rc multicast
            // server are all dropped (we've seen it happen). In that case, we are stuck in
            // the wait just below, even though the rc server has finished the last of
            // the 3-part connection. In that case, the rc server cannot finish its
            // connection because this method cannot progress further and make a TCP
            // connection back to the rc server.
            //
            // To avoid this scenario, break up the wait just below into 1/2 second
            // increments in order to check and see if rc server has already made its
            // connection.

            boolean response = false;
            if (multicastTimeout > 0) {
                int halfSecChunks = multicastTimeout*2/1000;
                try {
//System.out.println("RC connect: wait response");
                    for (int i=0; i < halfSecChunks; i++) {
                        if (!multicastResponse.await(500, TimeUnit.MILLISECONDS)) {
                            // Check after timeout to see if connection was completed by rc server
//System.out.println("CONNECT TIMEOUT, TRY AGAIN");
                            if (connectCompletion.getCount() < 1) {
                                break;
                            }
                        }
                        else {
                            response = true;
                            break;
                        }
                    }
                }
                catch (InterruptedException e) {}
            }
            // Wait forever
            else {
                try {
                    // Returning false after 1/2 sec means timeout
                    while (!multicastResponse.await(500, TimeUnit.MILLISECONDS)) {
                        // Check after timeout to see if connection was completed by rc server
//System.out.println("CONNECT TIMEOUT, TRY AGAIN");
                        if (connectCompletion.getCount() < 1) {
                            break;
                        }
                    }
                    response = true;
                }
                catch (InterruptedException e) {}

//                try { multicastResponse.await(); response = true;}
//                catch (InterruptedException e) {}
            }

            multicastUdpSocket.close();
            sender.interrupt();

            if (!response) {
                throw new cMsgException("No response to UDP multicast received");
            }
            else {
//System.out.println("RC connect: got a response to multicast!");
            }

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
                throw new cMsgException("RC Multicast server says to quit the connect attempt");
            }

            if (!completed) {
//System.out.println("RC connect: Did NOT complete the connection");
                throw new cMsgException("No connect from the RC server received");
            }
            else {
//System.out.println("RC connect: Completed the connection from RC server");
            }

            // create a TCP connection to the RC Server
            boolean gotTcpConnection = false;
            IOException ioex = null;

            if (!gotTcpConnection && rcServerAddresses.size() > 0) {
                for (InetAddress rcServerAddr : rcServerAddresses) {
                    try {
//System.out.println("RC connect: Try making tcp connection to RC server (host = " + rcServerAddr.getHostName() + ", " +
//                   rcServerAddr.getHostAddress() + "; port = " + rcTcpServerPort + ")");

                        tcpSocket = new Socket();
                        // don't waste time if a connection cannot be made, timeout = 0.2 seconds
                        tcpSocket.connect(new InetSocketAddress(rcServerAddr,rcTcpServerPort), 200);
                        tcpSocket.setTcpNoDelay(true);
                        tcpSocket.setSendBufferSize(cMsgNetworkConstants.bigBufferSize);
                        domainOut = new DataOutputStream(new BufferedOutputStream(tcpSocket.getOutputStream(),
                                                                                  cMsgNetworkConstants.bigBufferSize));
//System.out.println("RC connect: Made tcp connection to RC server");
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
     * {@link org.jlab.coda.cMsg.cMsgMessage#getSenderHost()}
     * method of the returned message. This is useful when trying to find the location
     * of a particular AFECS (runcontrol) platform.
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

                        int port     = cMsgUtilities.bytesToInt(buf, 12);
                        int hostLen  = cMsgUtilities.bytesToInt(buf, 16);
                        int expidLen = cMsgUtilities.bytesToInt(buf, 20);

                        if (packet.getLength() < 4*6 + hostLen + expidLen) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("monitor: got packet that's too small");
                            }
                            continue;
                        }

                        // get host
                        index = 24;
                        host = "";
                        if (hostLen > 0) {
                            host = new String(buf, index, hostLen, "US-ASCII");
                            //System.out.println("host = " + host);
                            index += hostLen;
                        }

//                        if (cMsgUtilities.isHostLocal(host)) {
//                            System.out.println("monitor: probe response from same host, ignoring");
//                            continue;
//                        }

                        // get expid
                        String serverExpid;
                        if (expidLen > 0) {
                            serverExpid = new String(buf, index, expidLen, "US-ASCII");
                            //System.out.println("monitor: got return expid = " + serverExpid);
                            if (!expid.equals(serverExpid)) {
                                if (debug >= cMsgConstants.debugWarn) {
                                    System.out.println("monitor: ignore response from expid = " + serverExpid);
                                }
                                continue;
                            }
                        }

                        // store in message
                        msg = new cMsgMessageFull();
                        msg.setSenderHost(host);
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
                // multicast is from rc domain prober
                out.writeInt(cMsgNetworkConstants.rcDomainMulticastProbe);
                // use port number = 0 to indicate monitor probe
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

            // send packet
            try { socket.send(packet); } catch (IOException e) {}

            while (receiver.getMsg() == null && waitTime < sleepTime) {
                // wait 1/2 second & check for response
                try { Thread.sleep(500); }
                catch (InterruptedException e) { }
                waitTime += 500;
            }

            // return the first legitimate response or null if none
//            cMsgMessage msg = receiver.getMsg();
//if (msg != null) System.out.println("monitor: received msg from " + receiver.getMsgHost() + "\n");
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
     *   <b>cMsg:rc://&lt;host&gt;:&lt;port&gt;/&lt;expid&gt;?multicastTO=&lt;timeout&gt;&connectTO=&lt;timeout&gt;</b><p>
     *
     * For the cMsg domain the UDL has the more specific form:<p>
     *   <b>cMsg:cMsg://&lt;host&gt;:&lt;port&gt;/&lt;subdomainType&gt;/&lt;subdomain remainder&gt;?tag=value&tag2=value2 ...</b><p>
     *
     * Remember that for this domain:
     *<ul>
     *<li>1) host is required and may also be "multicast", "localhost", or in dotted decimal form<p>
     *<li>2) port is optional with a default of {@link cMsgNetworkConstants#rcMulticastPort}<p>
     *<li>3) the experiment id or expid is required, it is NOT taken from the environmental variable EXPID<p>
     *<li>4) multicastTO is the time to wait in seconds before connect returns a
     *       timeout when a rc multicast server does not answer. Defaults to no timeout.<p>
     *<li>5) connectTO is the time to wait in seconds before connect returns a
     *       timeout while waiting for the rc server to send a special (tcp)
     *       concluding connect message. Defaults to 5 seconds.<p>
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

        // now look for ?multicastTO=value& or &multicastTO=value&
        pattern = Pattern.compile("[\\?&]multicastTO=([0-9]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                multicastTimeout = 1000 * Integer.parseInt(matcher.group(1));
//System.out.println("multicast TO = " + multicastTimeout);
            }
            catch (NumberFormatException e) {
                // ignore error and keep value of 0
            }
        }

        // now look for ?connectTO=value& or &connectTO=value&
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
     * This class gets any response to our UDP multicast. A response will
     * stop the multicast and tell us to wait for the completion of the
     * connect call by the RC server (not RC Multicast server).
     */
    class MulticastReceiver extends Thread {

        public void run() {

//System.out.println("RC client " + name + ": STARTED multicast receiving thread");
            /* A slight delay here will help the main thread (calling connect)
             * to be already waiting for a response from the server when we
             * multicast to the server here (prompting that response). This
             * will help insure no responses will be lost.
             */
            try { Thread.sleep(200); }
            catch (InterruptedException e) {}

            byte[] buf = new byte[1024];
            DatagramPacket packet = new DatagramPacket(buf, 1024);
            int index;

            while (true) {
                // reset for each round
                packet.setLength(1024);
                index = 0;

                try {
                    multicastUdpSocket.receive(packet);
                    // if we get too small of a packet, reject it
                    if (packet.getLength() < 6*4) {
                        if (debug >= cMsgConstants.debugWarn) {
                                     System.out.println("Multicast receiver: got packet that's too small");
                        }
                        continue;
                    }
//System.out.println("received multicast packet");
                    int magic1 = cMsgUtilities.bytesToInt(buf, index);
                    index += 4;
                    int magic2 = cMsgUtilities.bytesToInt(buf, index);
                    index += 4;
                    int magic3 = cMsgUtilities.bytesToInt(buf, index);
                    index += 4;
                    
                    if (magic1 != cMsgNetworkConstants.magicNumbers[0] ||
                        magic2 != cMsgNetworkConstants.magicNumbers[1] ||
                        magic3 != cMsgNetworkConstants.magicNumbers[2])  {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("Multicast receiver: got bad magic # response to multicast");
                        }
                        continue;
                    }

                    int port = cMsgUtilities.bytesToInt(buf, index);
                    index += 4;

                    if (port != rcMulticastServerPort) {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("Multicast receiver: got bad port response to multicast (" + port + ")");
                        }
                        continue;
                    }

                    int hostLen = cMsgUtilities.bytesToInt(buf, index);
                    index += 4;
                    int expidLen = cMsgUtilities.bytesToInt(buf, index);
                    index += 4;

                    if (packet.getLength() < 4*6 + hostLen + expidLen) {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("Multicast receiver: got packet that's too small");
                        }
                        continue;
                    }

                    // get host
                    if (hostLen > 0) {
//                        String host = new String(buf, index, hostLen, "US-ASCII");
//System.out.println("host = " + host);
                        index += hostLen;
                    }

                    // get expid
                    if (expidLen > 0) {
                        String serverExpid = new String(buf, index, expidLen, "US-ASCII");
//System.out.println("expid = " + serverExpid);
                        if (!expid.equals(serverExpid)) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("Multicast receiver: got bad expid response to multicast (" + serverExpid + ")");
                            }
                            continue;
                        }
                    }
                }
                catch (UnsupportedEncodingException e) {continue;}
                catch (IOException e) {
//System.out.println("Got IOException in multicast receive, exiting");
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
                                try {
                                    multicastUdpSocket.setNetworkInterface(ni);
                                    multicastUdpSocket.send(packet);
                                }
                                catch (IOException e) {
                                    System.out.println("Error sending packet over " + ni.getName());
                                }
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


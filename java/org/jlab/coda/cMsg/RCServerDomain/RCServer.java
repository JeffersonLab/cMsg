/*----------------------------------------------------------------------------*
 *  Copyright (c) 2006        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 26-Apr-2006, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.RCServerDomain;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgGetHelper;
import org.jlab.coda.cMsg.common.*;

import java.util.concurrent.TimeUnit;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.*;
import java.net.*;
import java.io.*;

/**
 * This class implements the runcontrol server (rcs) domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class RCServer extends cMsgDomainAdapter {

    /** Runcontrol client's TCP listening port obtained from UDL. */
    private int rcClientPort;

    /** Runcontrol client's host obtained from UDL. */
    private String rcClientHost;

    /** UDP port on which to receive messages from the rc client. */
    int localUdpPort;

    /** TCP port on which to receive messages from the rc client. */
    private int localTcpPort;

    /** Set of client IP addresses sorted so that first addr
     *  is on same subnet as rc server. */
    private ArrayList<String> clientIpOrderedSet;

    /** Set of server IP addresses sorted so that first addr
     *  is on same subnet as rc client. */
    private ArrayList<String> serverIpOrderedSet;

    /** Thread that listens for TCP packets sent to this server. */
    private rcListeningThread listenerThread;

    /** TCP socket over which to send rc commands to runcontrol client. */
    private Socket socket;

    /** Buffered data output stream associated with {@link #socket}. */
    private DataOutputStream out;

    /** Buffered data input stream associated with {@link #socket}. */
    private DataInputStream in;

    /**
     * This lock is for controlling access to the methods of this class.
     * The {@link #connect} and {@link #disconnect} methods of this object
     * cannot be called simultaneously with each other or any other method.
     */
    private final ReentrantReadWriteLock methodLock = new ReentrantReadWriteLock();

    /** Lock for calling {@link #connect} or {@link #disconnect}. */
    private Lock connectLock = methodLock.writeLock();

    /** Lock for calling methods other than {@link #connect} or {@link #disconnect}. */
    private Lock notConnectLock = methodLock.readLock();

    /** Lock to ensure
     * {@link #subscribe(String, String, cMsgCallbackInterface, Object)}
     * and {@link #unsubscribe(cMsgSubscriptionHandle)}
     * calls are sequential. */
    private Lock subscribeLock = new ReentrantLock();

    /** Used to create unique id numbers associated with a specific message subject/type pair. */
    private AtomicInteger uniqueId;

    /**
     * Collection of all of this client's message subscriptions which are
     * {@link cMsgSubscription} objects. This set is synchronized.
     */
    Set<cMsgSubscription> subscriptions;

    /**
     * HashMap of all of this client's callback threads (keys) and their associated
     * subscriptions (values). The cMsgCallbackThread object of a new subscription
     * is returned (as an Object) as the unsubscribe handle. When this object is
     * passed as the single argument of an unsubscribe, a quick lookup of the
     * subscription is done using this hashmap.
     */
    private Map<Object, cMsgSubscription> unsubscriptions;

    /**
     * Collection of all of this client's {@link #subscribeAndGet(String, String, int)}
     * calls currently in execution.
     * SubscribeAndGets are very similar to subscriptions and can be thought of as
     * one-shot subscriptions.
     * Key is receiverSubscribeId object, value is {@link cMsgSubscription}
     * object.
     */
    ConcurrentHashMap<Integer,cMsgSubscription> subscribeAndGets;

    /**
     * Collection of all of this client's {@link #sendAndGet(cMsgMessage, int)}
     * calls currently in execution.
     * Key is senderToken object, value is {@link cMsgGetHelper} object.
     */
    ConcurrentHashMap<Integer,cMsgGetHelper> sendAndGets;



    /**
     * Returns a string back to the top level API user indicating the host
     * and port of the client that this server is communicating with.
     * @return host:port of client
     */
    public String getString() {
        return rcClientHost+":"+rcClientPort;
    }


    /**
     * Constructor.
     * @throws cMsgException if cannot find host name.
     */
    public RCServer() throws cMsgException {
        domain = "rcs";
        subscriptions    = Collections.synchronizedSet(new HashSet<cMsgSubscription>(20));
        subscribeAndGets = new ConcurrentHashMap<Integer,cMsgSubscription>(20);
        sendAndGets      = new ConcurrentHashMap<Integer, cMsgGetHelper>(20);
        unsubscriptions  = Collections.synchronizedMap(new HashMap<Object, cMsgSubscription>(20));
        uniqueId         = new AtomicInteger();

        // store our host's name
        try {
            host = InetAddress.getLocalHost().getCanonicalHostName();
        }
        catch (UnknownHostException e) {
            throw new cMsgException("cMsg: cannot find host name");
        }

        class myShutdownHandler implements cMsgShutdownHandlerInterface {
            cMsgDomainInterface cMsgObject;

            myShutdownHandler(cMsgDomainInterface cMsgObject) {
                this.cMsgObject = cMsgObject;
            }

            public void handleShutdown() {
                try {cMsgObject.disconnect();}
                catch (cMsgException e) {}
            }
        }

        // Now make an instance of the shutdown handler
        setShutdownHandler(new myShutdownHandler(this));
    }


    /**
     * Set the UDL of the client which may be a semicolon separated
     * list of (sub)UDLs in this domain. Bad sub UDLs are ignored, but
     * at least one must be valid.
     *
     * @param UDL UDL of client
     * @throws cMsgException if UDL is null or no valid UDL exists
     */
    public void setUDL(String UDL) throws cMsgException {
        if (UDL == null) {
            throw new cMsgException("UDL argument is null");
        }

        this.UDL = UDL;

        // The UDL is a semicolon separated list of UDLs. Separate them.
        String[] UDLstrings = UDL.split(";");

        if (debug >= cMsgConstants.debugInfo) {
            for (int i=0; i<UDLstrings.length; i++) {
                System.out.println("UDL #" + i + " = " + UDLstrings[i]);
            }
        }

        // Parse the list of UDLs and store them. Ignore any bad UDLs in the list.
        ArrayList<String> clientIpList = new ArrayList<String>(UDLstrings.length);
        ArrayList<String> clientBroadcastList = new ArrayList<String>(UDLstrings.length);
//        boolean fixedIp = false;

        for (String udl : UDLstrings) {
            // Strip off the beginning domain stuff
            Pattern pattern = Pattern.compile("(cMsg)?:?([\\w\\-]+)://(.*)", Pattern.CASE_INSENSITIVE);
            Matcher matcher = pattern.matcher(udl);

            String udlRemainder;

            if (matcher.matches()) {
                // udl remainder
                udlRemainder = matcher.group(3);
            }
            else {
                // Ignore bad udl
                continue;
            }

            // Any remaining UDL is analyzed
            if (udlRemainder == null) {
                // Ignore bad udl
                continue;
            }

            try {
                // Parse udl remainder to get host, TCP port, UDP port
                Object[] retObjs = parseUDL(udlRemainder);

                // Set the client TCP port. Pick the first legitimate value.
                // Should all be the same!
                int tcpPort = (Integer) retObjs[1];
                if ((rcClientPort == 0) && (tcpPort > 1023 && tcpPort < 65536)) {
                     rcClientPort = tcpPort;
                }

                // Store stuff
                clientIpList.add((String)retObjs[0]);
                clientBroadcastList.add((String)retObjs[2]);
                // If this is true, there will only be 1 UDL in the UDL argument
//                fixedIp = (Boolean)retObjs[3];
//System.out.println("RCS setUDL(): storing addr = " + retObjs[0] + ", port = " +
//                   retObjs[1]);
            }
            catch (UnknownHostException e) { /* ignore bad udl */ }
        }

        // Do we have anything useful?
        if (clientIpList.size() < 1) {
            throw new cMsgException("no valid UDL given");
        }

        // Create list of addresses with those on the same subnets as this server, first
        orderIpAddresses(clientIpList, clientBroadcastList);
    }

// BUGGY method that adds double to lists
//    /**
//     * Order both the rc server and rc client ip address lists (actually
//     * create new lists) so that the first addresses on each list are on the
//     * same subnet. This should facilitate communication between the 2
//     * with a minimum of waiting.
//     *
//     * @param clientIps         list of client IP addresses
//     * @param clientBroadcasts  list of client broadcast/subnet addresses
//     */
//    private void orderIpAddressesOrig(ArrayList<String> clientIps,
//                                      ArrayList<String> clientBroadcasts) {
//
//        // Get all info about the network interfaces on this machine
//        List<InterfaceAddress> ipInfoList = cMsgUtilities.getAllIpInfo();
//        InterfaceAddress iAddr;
//        ListIterator<InterfaceAddress> lit = ipInfoList.listIterator();
//
//        clientIpOrderedSet = new ArrayList<String>();
//        serverIpOrderedSet = new ArrayList<String>();
//
//        // Order both client and server IP lists so
//        // that those on the same subnet come first.
//        String localBroad;
//        while (lit.hasNext()) {
//            iAddr = lit.next();
//            // Get the local broadcast address
//            localBroad = iAddr.getBroadcast().getHostAddress();
//
//            // For each client broadcast address ...
//            for (int i=0; i < clientIps.size(); i++) {
//                // Compare local and client broadcast addresses
//                if (localBroad.equals(clientBroadcasts.get(i))) {
//                    // On same subnet, so add to head of lists
//                    clientIpOrderedSet.add(0, clientIps.get(i));
//                    serverIpOrderedSet.add(0, iAddr.getAddress().getHostAddress());
//                }
//                else {
//                    // Add to end of lists
//                    clientIpOrderedSet.add(clientIps.get(i));
//                    serverIpOrderedSet.add(iAddr.getAddress().getHostAddress());
//                }
//            }
//        }
//    }


    /**
     * Order both the rc server and rc client ip address lists (actually
     * create new lists) so that the first addresses on each list are on the
     * same subnet. This should facilitate communication between the 2
     * with a minimum of waiting.
     *
     * @param clientIps         list of client IP addresses
     * @param clientBroadcasts  list of client broadcast/subnet addresses
     */
    private void orderIpAddresses(ArrayList<String> clientIps,
                                  ArrayList<String> clientBroadcasts) {

        // Get all info about the network interfaces on this machine
        List<InterfaceAddress> ipInfoList = cMsgUtilities.getAllIpInfo();

        // Is the client on the local host?
        boolean clientIsLocal = false;
        for (String hostname : clientIps) {
            if (cMsgUtilities.isHostLocal(hostname)) {
                clientIsLocal = true;
                break;
            }
        }

        clientIpOrderedSet = new ArrayList<String>();
        serverIpOrderedSet = new ArrayList<String>();

        // Order both client and server IP lists so
        // that those on the same subnet come first.

        // For each client (broadcast) address:
        // See if it shares its subnet with this host.
        // If so, put at the top, else put at the bottom.
        for (int i=0; i < clientIps.size(); i++) {

            boolean onDifferentSubnet = true;

            for (InterfaceAddress iAddr : ipInfoList) {
                String localBroad = iAddr.getBroadcast().getHostAddress();
                // Compare local and client broadcast addresses
                if (localBroad.equals(clientBroadcasts.get(i))) {
                    // On same subnet, so add to head of list
                    clientIpOrderedSet.add(0, clientIps.get(i));
                    onDifferentSubnet = false;
                    break;
                }
            }

            if (onDifferentSubnet) {
                // On different subnet, so add to end of list
                clientIpOrderedSet.add(clientIps.get(i));
            }
        }

        // Now order the server's IP addresses in the same way
        for (InterfaceAddress iAddr : ipInfoList) {
            // Get the local broadcast address
            String localBroad = iAddr.getBroadcast().getHostAddress();
            boolean onDifferentSubnet = true;

            // For each client broadcast address ...
            for (int i=0; i < clientIps.size(); i++) {
                // Compare local and client broadcast addresses
                if (localBroad.equals(clientBroadcasts.get(i))) {
                    // On same subnet, so add to head of lists
                    serverIpOrderedSet.add(0, iAddr.getAddress().getHostAddress());
                    onDifferentSubnet = false;
                    break;
                }
            }

            if (onDifferentSubnet) {
                // On different subnet, so add to end of list
                serverIpOrderedSet.add(iAddr.getAddress().getHostAddress());
            }
        }

        // If on local host, put the loopback address first
        if (clientIsLocal) {
            clientIpOrderedSet.add(0, "127.0.0.1");
        }

    }



    /**
     * Method to connect to the rc client from this server.
     *
     * @throws cMsgException if there are problems parsing the UDL,
     *                       communication problems with the client,
     *                       or cannot start up a TCP listening thread
     */
    public void connect() throws cMsgException {

        // cannot run this simultaneously with any other public method
        connectLock.lock();

        try {
            if (connected) {
                return;
            }

            try {
                // Iterate through client ip addresses
                boolean failed = true;
System.out.println("RC server: ordered RC client IP address list:");
                for (String clientHost : clientIpOrderedSet) {
System.out.println("     ip = " + clientHost +
                   ", port = " + rcClientPort);
                }

                for (String clientHost : clientIpOrderedSet) {
                    try {

//System.out.println("RC server: try connection with RC client (host = " + clientHost +
//                   ", port = " + rcClientPort);
                        // Create an object to deliver messages to the RC client.
                        createTCPClientConnection(clientHost, rcClientPort);
                        rcClientHost = clientHost;
                        failed = false;
                        break;
                    }
                    catch (IOException e) {
                        // failure to communicate
                        System.out.println("RC server: failed to connect to RC client (host = " + clientHost +
                                                   ", port = " + rcClientPort);
                    }
                }

                if (failed) {
                    throw new cMsgException("Failed to create socket to RC client");
                }
//System.out.println("RC server: created socket to RC client");

                // Start listening for tcp connections if not already
                if (listenerThread == null) {
                    listenerThread = new rcListeningThread(this);
                    listenerThread.start();

                    // Wait for indication listener thread is actually running before
                    // continuing on. This thread must be running before we talk to
                    // the client since the client tries to communicate with it.
                    synchronized (listenerThread) {
                        if (!listenerThread.isAlive()) {
                            try {
                                listenerThread.wait();
                            }
                            catch (InterruptedException e) {
                                e.printStackTrace();
                            }
                        }
                    }
                }

                // Get the port selected for listening on
                localTcpPort = listenerThread.getTcpPort();

                // Get the port selected for communicating on
                localUdpPort = listenerThread.getUdpPort();
System.out.println("RC server: listening on TCP port = " + localTcpPort + " and UDP port = " +
localUdpPort);

                // Send a special message giving our host & udp port.
                cMsgMessageFull msg = new cMsgMessageFull();
                //msg.setSenderHost(InetAddress.getLocalHost().getCanonicalHostName());
                msg.setSenderHost(InetAddress.getByName(InetAddress.getLocalHost().
                                                        getCanonicalHostName()).getHostAddress());

//                // Send list of our IP addresses
//                String[] ips = new String[serverIpOrderedSet.size()];
//                serverIpOrderedSet.toArray(ips);

                // Have client use the same IP addresses that the server just
                // used to connect to the client
                cMsgPayloadItem pItem = new cMsgPayloadItem("serverIp", socket.getLocalAddress().getHostAddress());
                msg.addPayloadItem(pItem);

System.out.println("RC server: tell RC client to connect back using IP = " +
                   socket.getLocalAddress().getHostAddress());

                pItem = new cMsgPayloadItem("clientIp", rcClientHost);
                msg.addPayloadItem(pItem);

//                pItem = new cMsgPayloadItem("IpAddresses", ips);
//                msg.addPayloadItem(pItem);
//                msg.setText(localUdpPort+":"+localTcpPort);
                msg.setText(""+localUdpPort);
                msg.setUserInt(localTcpPort);

                deliverMessage(msg, cMsgConstants.msgRcConnect);

                // Wait until the client establishes a TCP connection back to this
                // object's TCP listening thread.
                try {
System.out.println("RC server connect: wait up to 15 sec for RC client TCP return connection");
                    boolean connectionMade = listenerThread.startLatch.await(15L, TimeUnit.SECONDS);
                    if (!connectionMade) {
                        throw new cMsgException("15 sec timeout waiting for RC client to connect back");
                    }
                }
                catch (InterruptedException e) {
                    throw new cMsgException("connect() interrupted", e);
                }
System.out.println("RC server connect: complete");

                connected = true;
            }
            catch (IOException e) {
                if (listenerThread != null) listenerThread.killThread();
                throw new cMsgException("cannot connect, IO error", e);
            }
        }
        finally {
            connectLock.unlock();
        }

        return;
    }

    //----------------------------------------------------------
    //  Methods for testing RC client's communication & protocol
    //----------------------------------------------------------

    /**
     * Method for testing rc client communications with this test server.
     * Use this in conjunction with org.jlab.coda.emu.support.test.RcProtocolTest .
     * First run this, then run that program with the UDP and TCP ports and IP
     * addresses printed out in the program below.
     */
    public void testConnect() {

        try {
            // Start listening for tcp connections
            listenerThread = new rcListeningThread(this);
            listenerThread.start();

            // Wait for indication listener thread is actually running before
            // continuing on. This thread must be running before we talk to
            // the client since the client tries to communicate with it.
            synchronized (listenerThread) {
                if (!listenerThread.isAlive()) {
                    try {
                        listenerThread.wait();
                    }
                    catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }
            }

            // Get the port selected for listening on
            localTcpPort = listenerThread.getTcpPort();

            // Get the port selected for communicating on
            localUdpPort = listenerThread.getUdpPort();
System.out.println("RC test server: listening on TCP port = " + localTcpPort +
                   " and UDP port = " + localUdpPort);

            // Print list of our IP addresses
            System.out.println("Rc test server IP addresses:");
            Collection<String> c = cMsgUtilities.getAllIpAddresses();
            for (String ip : c) {
                System.out.println("    " + ip);
            }

            // Create a little subscription to print incoming messages
            class rcCallback extends cMsgCallbackAdapter {
                public void callback(cMsgMessage msg, Object userObject) {
                    //System.out.println("Got RC test message:" + msg);
                    System.out.print(".");
                }
            }

            rcCallback cb = new rcCallback();
            cMsgSubscriptionHandle handle = testSubscribe("*", "*", cb, null);
            start();


            Thread.sleep(200000);

        }
        catch (Exception e) {
            e.printStackTrace();
        }
    }


    /**
     * Run as a stand-alone RC server to test incoming messages.
     * @param args arguments.
     */
    public static void main(String[] args) {
        try {
            RCServer server = new RCServer();
            server.testConnect();
        }
        catch (cMsgException e) {
            System.out.println(e.toString());
            System.exit(-1);
        }
    }

    /**
     * This is a method to subscribe to receive messages
     * of a subject and type from the test rc client.
     *
     * @param subject {@inheritDoc}
     * @param type    {@inheritDoc}
     * @param cb      {@inheritDoc}
     * @param userObj {@inheritDoc}
     * @return {@inheritDoc}
     */
    public cMsgSubscriptionHandle testSubscribe(String subject, String type,
                                                cMsgCallbackInterface cb, Object userObj) {

        // First generate a unique id for the receiveSubscribeId field.
        // (Left over from cMsg domain).
        int id = uniqueId.getAndIncrement();

        // Add a new subscription & callback
        cMsgCallbackThread cbThread = new cMsgCallbackThread(cb, userObj, domain, subject, type);
        cMsgSubscription newSub = new cMsgSubscription(subject, type, id, cbThread);
        unsubscriptions.put(cbThread, newSub);
        subscriptions.add(newSub);

        return cbThread;
    }


    //----------------------------------------------------------
    //----------------------------------------------------------


    /**
     * This method results in this object
     * becoming functionally useless.
     */
    public void close() {
System.out.println("RC Server: CLOSE() called");
        disconnect();
        if (listenerThread != null) {
            listenerThread.killThread();
            listenerThread = null;
        }
    }


    /**
     * Method to close the connection to the rc client. Keep the listening
     * threads running in case another call to connect() is made.
     */
    public void disconnect() {
System.out.println("RC Server: DISCONNECT() called");
        // cannot run this simultaneously with connect or send
        connectLock.lock();

        try {
            if (!connected) return;
            connected = false;

            if (in != null)     try {in.close();}     catch (IOException e) {}
            if (out != null)    try {out.close();}    catch (IOException e) {}
            if (socket != null) try {socket.close();} catch (IOException e) {}
        }
        finally {
            connectLock.unlock();
        }
    }


    /**
     * Creates a TCP socket to a runcontrol client.
     *
     * @param  clientHost host client is running on
     * @param  clientPort tcp port client is listening on
     * @throws IOException if socket cannot be created
     */
    private void createTCPClientConnection(String clientHost, int clientPort) throws IOException {
        try {
//System.out.println("RC Server: make tcp socket to " + clientHost + " on port " + clientPort);
            socket = new Socket(clientHost, clientPort);
            // Set tcpNoDelay so no packets are delayed
            socket.setTcpNoDelay(true);
            // set buffer size
            socket.setSendBufferSize(65535);

            // create buffered communication stream for efficiency
            in  = new DataInputStream(new BufferedInputStream(socket.getInputStream()));
            out = new DataOutputStream(new BufferedOutputStream(socket.getOutputStream(), 65536));

            // send some ints identifying us as a valid rc Domain server ("cMsg is cool" in ascii)
            out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
            out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
            out.writeInt(cMsgNetworkConstants.magicNumbers[2]);
            out.flush();
        }
        catch (IOException e) {
            if (in != null) try {in.close();} catch (IOException e1) {}
            if (out != null) try {out.close();} catch (IOException e1) {}
            if (socket != null) try {socket.close();} catch (IOException e1) {}
            throw e;
        }

System.out.println("RC Server: made tcp socket to rc client " + clientHost + " on port " + clientPort);
    }


    /**
     * Method to parse the Universal Domain Locator (UDL) into its various components.
     * RC Server domain UDL is of the form:<p>
     *       cMsg:rcs://&lt;host&gt;:&lt;tcpPort&gt;/&lt;bcastAddr&gt;
     *
     * The initial cMsg:rcs:// is stripped off by the top layer API
     *
     * Remember that for this domain:<p>
     * <ul>
     * <li>host is NOT optional, must be in dot-decimal form<p>
     * <li>tcp port is optional and defaults to cMsgNetworkConstants.rcClientPort<p>
     * <li>bcastAddr is NOT optional, must be the broadcast address in dot-decimal form <p>
     * </ul>
     *
     * @param udlRemainder partial UDL to parse
     * @return array of Objects: 1) host (String), 2) tcp port (Integer)
     * @throws cMsgException if udlRemainder is null
     */
    private Object[] parseUDL(String udlRemainder) throws cMsgException, UnknownHostException {

        if (udlRemainder == null) {
            throw new cMsgException("invalid UDL");
        }

        Pattern pattern = Pattern.compile("([^:/]+):?(\\d+)?/([^/?&]+)(.*)");
        Matcher matcher = pattern.matcher(udlRemainder);

        String udlHost, udlPort, bcastAddr, remainder;
        int clientPort=0;

        if (matcher.find()) {
            // host
            udlHost = matcher.group(1);
            // port
            udlPort = matcher.group(2);
            // broadcast address in dot-decimal
            bcastAddr = matcher.group(3);
            // fixedIP if it exists
            remainder = matcher.group(4);

           if (debug >= cMsgConstants.debugInfo) {
                System.out.println("\nparseUDL: " +
                                   "\n  host = " + udlHost +
                                   "\n  port = " + udlPort +
                                   "\n  broadcast = " + bcastAddr +
                                   "\n  remainder = " + remainder);
           }
        }
        else {
            throw new cMsgException("invalid UDL");
        }

        // If the host is "localhost", find the actual, fully qualified  host name
        byte[] b =cMsgUtilities.isDottedDecimal(udlHost);
        if (b == null) {
            throw new cMsgException("host not in dot-decimal form");
        }

        // Get runcontrol client port or guess if it's not given
        if (udlPort != null && udlPort.length() > 0) {
            try {
                clientPort = Integer.parseInt(udlPort);
            }
            catch (NumberFormatException e) {
                clientPort = cMsgNetworkConstants.rcTcpClientPort;
                if (debug >= cMsgConstants.debugWarn) {
                    System.out.println("parseUDL: non-integer port, guessing codaComponent port is " + rcClientPort);
                }
            }
        }
        else {
            clientPort = cMsgNetworkConstants.rcTcpClientPort;
            if (debug >= cMsgConstants.debugWarn) {
                System.out.println("parseUDL: guessing codaComponent port is " + rcClientPort);
            }
        }

        if (clientPort < 1024 || clientPort > 65535) {
            throw new cMsgException("parseUDL: illegal port number");
        }

        return new Object[] {udlHost, clientPort, bcastAddr};
    }


    /**
     * Method to send a message/command to the rc client. The command is sent as a
     * string in the message's text field.
     *
     * @param message {@inheritDoc}
     * @throws cMsgException if there are communication problems with the server;
     *                       text is null or blank
     */
    public void send(cMsgMessage message) throws cMsgException {

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }
            deliverMessage(message, cMsgConstants.msgSubscribeResponse);
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage(),e);
        }
        finally {
            notConnectLock.unlock();
        }

    }


    /**
     * Method to send a "ping" command to the rc client. It writes a message
     * and returns a zero if no connection to client exists, any other
     * value indicates a valid connection.
     *
     * @param message {@inheritDoc}
     * @param timeout ignored
     * @return 0 if no valid TCP connection to client, 1 if there is a connection.
     * @throws cMsgException if there is a timeout
     */
    synchronized public int syncSend(cMsgMessage message, int timeout)
            throws cMsgException {

        int origTimeout=0, val=0;

        notConnectLock.lock();

        try {
            if (!connected) {
                return val;
            }
            // Set timeout on socket read
            origTimeout = socket.getSoTimeout();
            socket.setSoTimeout(timeout);
            out.writeInt(4);
            out.writeInt(cMsgConstants.msgSyncSendRequest);
            out.flush();
            val = in.readInt();
        }
        catch (SocketTimeoutException e) {
            throw new cMsgException("timeout", e);
        }
        catch (IOException e) {
            return 0;
        }
        finally {
            // Reset socket read timeout
            try { socket.setSoTimeout(origTimeout); }
            catch (SocketException e) {}

            notConnectLock.unlock();
        }

        return val;
    }


    /**
     * Method to deliver a message to a client. This is a somewhat modified "copy"
     * of {@link org.jlab.coda.cMsg.cMsgDomain.subdomains.cMsgMessageDeliverer#deliverMessageReal(cMsgMessage, int)}.
     * It leaves open the possibility that many fields may be null and still will not
     * barf. It's always possible that a knowledgeable user could create an object of
     * type cMsgMessageFull and pass that in. That way the user gets to set all fields.
     *
     * @param msg message to be sent
     * @param msgType type of message to be sent
     * @throws IOException if the message cannot be sent over the channel
     */
    synchronized private void deliverMessage(cMsgMessage msg, int msgType) throws IOException {

        int[] len = new int[6]; // int arrays are initialized to 0
        int binLength = 0;

        if (msg.getSender()      != null) len[0] = msg.getSender().length();
        if (msg.getSenderHost()  != null) len[1] = msg.getSenderHost().length();
        if (msg.getSubject()     != null) len[2] = msg.getSubject().length();
        if (msg.getType()        != null) len[3] = msg.getType().length();
        if (msg.getPayloadText() != null) len[4] = msg.getPayloadText().length();
        if (msg.getText()        != null) len[5] = msg.getText().length();
        if (msg.getByteArray()   != null) binLength = msg.getByteArrayLength();

        // size of everything sent (except "size" itself which is first integer)
        int size = len[0] + len[1] + len[2] + len[3] + len[4] + len[5] + binLength + 4 * 18;

        out.writeInt(size);
        out.writeInt(msgType);
        out.writeInt(msg.getVersion());
        out.writeInt(0); // reserved for future use
        out.writeInt(msg.getUserInt());
        out.writeInt(msg.getInfo());

        // send the time in milliseconds as 2, 32 bit integers
        long now = new Date().getTime();
        out.writeInt((int) (now >>> 32)); // higher 32 bits
        out.writeInt((int) (now & 0x00000000FFFFFFFFL)); // lower 32 bits
        out.writeInt((int) (msg.getUserTime().getTime() >>> 32));
        out.writeInt((int) (msg.getUserTime().getTime() & 0x00000000FFFFFFFFL));

        out.writeInt(msg.getSysMsgId());
        out.writeInt(msg.getSenderToken());
        out.writeInt(len[0]);
        out.writeInt(len[1]);
        out.writeInt(len[2]);
        out.writeInt(len[3]);
        out.writeInt(len[4]);
        out.writeInt(len[5]);
        out.writeInt(binLength);

        // write strings
        try {
            if (msg.getSender()      != null) out.write(msg.getSender().getBytes("US-ASCII"));
            if (msg.getSenderHost()  != null) out.write(msg.getSenderHost().getBytes("US-ASCII"));
            if (msg.getSubject()     != null) out.write(msg.getSubject().getBytes("US-ASCII"));
            if (msg.getType()        != null) out.write(msg.getType().getBytes("US-ASCII"));
            if (msg.getPayloadText() != null) out.write(msg.getPayloadText().getBytes("US-ASCII"));
            if (msg.getText()        != null) out.write(msg.getText().getBytes("US-ASCII"));

            if (binLength > 0) {
                out.write(msg.getByteArray(),
                          msg.getByteArrayOffset(),
                          binLength);
            }
        }
        catch (UnsupportedEncodingException e) {
            e.printStackTrace();
        }
        out.flush();

        return;
    }


//-----------------------------------------------------------------------------


    /**
     * This is a method to subscribe to receive messages of a subject and type from the rc client.
     *
     * @param subject {@inheritDoc}
     * @param type    {@inheritDoc}
     * @param cb      {@inheritDoc}
     * @param userObj {@inheritDoc}
     * @return {@inheritDoc}
     * @throws cMsgException if the subject, type, or callback is null;
     *                       an identical subscription already exists;
     *                       if not connected to an rc client
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

        try {
            // cannot run this simultaneously with connect or disconnect
            notConnectLock.lock();
            // cannot run this simultaneously with unsubscribe or itself
            subscribeLock.lock();

            if (!connected) {
                throw new cMsgException("not connected to rc client");
            }

            // add to callback list if subscription to same subject/type exists

            int id;

            // client listening thread may be interating thru subscriptions concurrently
            // and we may change set structure
            synchronized (subscriptions) {

                // for each subscription ...
                for (cMsgSubscription sub : subscriptions) {
                    // If subscription to subject & type exist already...
                    if (sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                        // add to existing set of callbacks
                        cbThread = new cMsgCallbackThread(cb, userObj, domain, subject, type);
                        sub.addCallback(cbThread);
                        unsubscriptions.put(cbThread, sub);
                        return cbThread;
                    }
                }

                // If we're here, the subscription to that subject & type does not exist yet.
                // We need to create and register it.

                // First generate a unique id for the receiveSubscribeId field.
                // (Left over from cMsg domain).
                id = uniqueId.getAndIncrement();

                // add a new subscription & callback
                cbThread = new cMsgCallbackThread(cb, userObj, domain, subject, type);
                newSub = new cMsgSubscription(subject, type, id, cbThread);
                unsubscriptions.put(cbThread, newSub);

                // client listening thread may be interating thru subscriptions concurrently
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
     * Method to unsubscribe a previous subscription.
     *
     * @param obj {@inheritDoc}
     * @throws cMsgException if there is no connection with the rc client; obj is null
     */
    public void unsubscribe(cMsgSubscriptionHandle obj)
            throws cMsgException {

        // check arg first
        if (obj == null) {
            throw new cMsgException("argument is null");
        }

        cMsgSubscription sub = unsubscriptions.remove(obj);
        // already unsubscribed
        if (sub == null) {
            return;
        }
        cMsgCallbackThread cbThread = (cMsgCallbackThread) obj;

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();
        // cannot run this simultaneously with subscribe or itself
        subscribeLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to rc client");
            }

            // Delete stuff from hashes & kill threads.
            // If there are still callbacks left,
            // don't unsubscribe for this subject/type.
            synchronized (subscriptions) {
                cbThread.dieNow(false);
                sub.getCallbacks().remove(cbThread);
                if (sub.numberOfCallbacks() < 1) {
                    subscriptions.remove(sub);
                }
            }
        }
        finally {
            subscribeLock.unlock();
            notConnectLock.unlock();
        }

    }


//-----------------------------------------------------------------------------



    /**
     * This method is like a one-time subscribe. The rc server grabs the first incoming
     * message of the requested subject and type and sends that to the caller.
     *
     * NOTE: Disconnecting when one thread is in the waiting part of a subscribeAndGet may cause that
     * thread to block forever. It is best to always use a timeout with subscribeAndGet so the thread
     * is assured of eventually resuming execution.
     *
     * @param subject {@inheritDoc}
     * @param type    {@inheritDoc}
     * @param timeout {@inheritDoc}
     * @return {@inheritDoc}
     * @throws cMsgException if there are communication problems with rc client;
     *                       subject and/or type is null or blank
     * @throws TimeoutException if timeout occurs
     */
    public cMsgMessage subscribeAndGet(String subject, String type, int timeout)
            throws cMsgException, TimeoutException {

        // check args first
        if (subject == null || type == null) {
            throw new cMsgException("message subject or type is null");
        }
        else if (subject.length() < 1 || type.length() < 1) {
            throw new cMsgException("message subject or type is blank string");
        }

        int id = 0;
        cMsgSubscription helper = null;

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to rc client");
            }

            // First generate a unique id for the receiveSubscribeId and senderToken field.
            // (artifact of cMsg domain).
            id = uniqueId.getAndIncrement();

            // create cMsgGetHelper object (not callback thread object)
            helper = new cMsgSubscription(subject, type);

            // keep track of get calls
            subscribeAndGets.put(id, helper);
        }
        // release lock 'cause we can't block connect/disconnect forever
        finally {
            notConnectLock.unlock();
        }

        // WAIT for the msg-receiving thread to wake us up
        try {
            synchronized (helper) {
                if (timeout > 0) {
                    helper.wait(timeout);
                }
                else {
                    helper.wait();
                }
            }
        }
        catch (InterruptedException e) {
        }

        // Check the message stored for us in helper.
        if (helper.isTimedOut()) {
            // remove the get
            subscribeAndGets.remove(id);
            throw new TimeoutException();
        }

        return helper.getMessage();
    }



    /**
     * The message is sent as it would be in the
     * {@link #send(org.jlab.coda.cMsg.cMsgMessage)} method except that the
     * senderToken and creator are set. A marked response can be received from a client
     * regardless of its subject or type.
     *
     * NOTE: Disconnecting when one thread is in the waiting part of a sendAndGet may cause that
     * thread to block forever. It is best to always use a timeout with sendAndGet so the thread
     * is assured of eventually resuming execution.
     *
     * @param message message sent to client
     * @param timeout {@inheritDoc}
     * @return {@inheritDoc}
     * @throws cMsgException if there are communication problems with the client;
     *                       subject and/or type is null or blank
     * @throws TimeoutException if timeout occurs
     */
    public cMsgMessage sendAndGet(cMsgMessage message, int timeout)
            throws cMsgException, TimeoutException {

        String subject = message.getSubject();
        String type    = message.getType();

        // check args first
        if (subject == null || type == null) {
            throw new cMsgException("message subject and/or type is null");
        }
        else if (subject.length() < 1 || type.length() < 1) {
            throw new cMsgException("message subject or type is blank string");
        }

        int id = 0;
        cMsgGetHelper helper = null;

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to rc client");
            }

            // We're expecting a specific response, so the senderToken is sent back
            // in the response message, allowing us to run the correct callback.
            id = uniqueId.getAndIncrement();

            // for get, create cMsgHolder object (not callback thread object)
            helper = new cMsgGetHelper();

            // track specific get requests
            sendAndGets.put(id, helper);

            cMsgMessageFull fullMsg = new cMsgMessageFull(message);
            fullMsg.setSenderToken(id);
            fullMsg.setGetRequest(true);
            deliverMessage(fullMsg, cMsgConstants.msgSubscribeResponse);
        }
        catch (IOException e) {
System.out.println("IOException in send&Get, msg = " + e.getMessage());
            throw new cMsgException(e.getMessage(),e);
        }
        // release lock 'cause we can't block connect/disconnect forever
        finally {
            notConnectLock.unlock();
        }

        // WAIT for the msg-receiving thread to wake us up
        try {
            synchronized (helper) {
                if (timeout > 0) {
                    helper.wait(timeout);
                }
                else {
                    helper.wait();
                }
            }
        }
        catch (InterruptedException e) {
        }

        // Check the message stored for us in helper.
        if (helper.isTimedOut()) {
            // remove the get
            sendAndGets.remove(id);
            throw new TimeoutException();
        }

        return helper.getMessage();
    }


}

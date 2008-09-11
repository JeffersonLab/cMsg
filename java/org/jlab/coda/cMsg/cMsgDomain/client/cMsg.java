/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 14-Jul-2004, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.client;

import org.jlab.coda.cMsg.*;

import java.io.*;
import java.net.*;
import java.nio.channels.UnresolvedAddressException;
import java.util.*;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.CountDownLatch;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * This class implements a cMsg client in the cMsg domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsg extends cMsgDomainAdapter {

    /** Port number to listen on. */
    int port;


    /** Subdomain being used. */
    private String subdomain;

    /** Subdomain remainder part of the UDL. */
    private String subRemainder;

    /** Optional password included in UDL for connection to server requiring one. */
    private String password;

    //-- FAILOVER STUFF ---------------------------------------------------------------

    /** List of parsed UDL objects - one for each failover UDL. */
    private ArrayList<ParsedUDL> failovers;

    /** Number of failures to connect to the given array of UDLs. */
    private int connectFailures;

    /**
     * Index into the {@link #failovers} list corressponding to the UDL
     * currently being used.
     */
    private byte failoverIndex;

    /**
     * If more than one viable failover UDL is given, then this is true meaning
     * if any request to the server is interrupted, that method will wait a short
     * while for the failover to complete before throwing an exception or continuing
     * on.
     */
    private boolean useFailoverWaiting;

    /**
     * Have all the existing subscriptions been successfully resubscribed on the
     * failover server? This member is used as a flag between different threads.
     */
    private volatile boolean resubscriptionsComplete;

    //----------------------------------------------------------------------------------

    /** Port number from which to start looking for a suitable listening port. */
    int startingPort;

    /** Socket over which to send UDP broadcast and receive response packets from server. */
    DatagramSocket udpSocket;

    /** Socket over which to send messges with UDP. */
    DatagramSocket sendUdpSocket;

    /** Packet in which to send messages with UDP. */
    DatagramPacket sendUdpPacket;

    /**
     * True if the host given by the UDL is \"broadcast\" or \"255.255.255.255\".
     * In this case we must broadcast to find the server. Else false.
     */
    boolean mustBroadcast;

    /**
     * What throughput do we expect from this client? Values may be one of:
     * {@link cMsgConstants#regimeHigh}, {@link cMsgConstants#regimeMedium},
     * {@link cMsgConstants#regimeLow}.
     */
    int regime;

    /** Signal to coordinate the broadcasting and waiting for responses. */
    CountDownLatch broadcastResponse;

    /** Timeout in milliseconds to wait for server to respond to broadcasts. */
    int broadcastTimeout;

    /** Name server's host. */
    String nameServerHost;

    /** Name server's port. */
    int nameServerPort;

    /** Domain server's host. */
    String domainServerHost;

    /** Domain server's TCP port. */
    int domainServerPort;

    /** Domain server's UDP port for udp client sends. */
    int domainServerUdpPort;

    /** Channel for talking to domain server. */
    Socket domainOutSocket;
    /** Socket input stream associated with domainInChannel - gets info from server. */
    DataInputStream  domainIn;
    /** Socket output stream associated with domainOutChannel - sends info to server. */
    DataOutputStream domainOut;

    /** Socket for checking to see that the domain server is still alive. */
    Socket keepAliveSocket;

    /** String containing monitor data received from a cMsg server as a keep alive response. */
    public String monitorXML;

    /** Time in milliseconds between sending monitor data / keep alives. */
    final int sleepTime = 2000;

    /** Thread listening for TCP connections and responding to domain server commands. */
    cMsgClientListeningThread listeningThread;

    /** Thread for sending keep alive commands to domain server to check its health. */
    KeepAlive keepAliveThread;

    /** Thread for sending periodic monitoring info to domain server to check this client's health. */
    UpdateServer updateServerThread;

    /**
     * Collection of all of this client's message subscriptions which are
     * {@link cMsgSubscription} objects. This set is synchronized. A client is either
     * a regular client or a bridge but not both. That means it does not matter that
     * a bridge client will add namespace data to the stored subscription but a regular
     * client will not.
     */
    public Set<cMsgSubscription> subscriptions;

    /**
     * HashMap of all of this client's callback threads (keys) and their associated
     * subscriptions (values). The cMsgCallbackThread object of a new subscription
     * is returned (as an Object) as the unsubscribe handle. When this object is
     * passed as the single argument of an unsubscribe, a quick lookup of the
     * subscription is done using this hashmap.
     */
    private ConcurrentHashMap<Object, cMsgSubscription> unsubscriptions;

    /**
     * Collection of all of this client's {@link #subscribeAndGet} calls currently in execution.
     * SubscribeAndGets are very similar to subscriptions and can be thought of as
     * one-shot subscriptions.
     * Key is receiverSubscribeId object, value is {@link cMsgSubscription} object.
     */
    ConcurrentHashMap<Integer,cMsgSubscription> subscribeAndGets;

    /**
     * Collection of all of this client's {@link #sendAndGet} calls currently in execution.
     * Key is senderToken object, value is {@link cMsgGetHelper} object.
     */
    ConcurrentHashMap<Integer,cMsgGetHelper> sendAndGets;

    /**
     * Collection of all of this client's {@link #syncSend} calls currently in execution.
     * Key is senderToken object, value is {@link cMsgGetHelper} object.
     */
    ConcurrentHashMap<Integer,cMsgGetHelper> syncSends;


    /**
     * This lock is for controlling access to the methods of this class.
     * It is inherently more flexible than synchronizing code. The {@link #connect}
     * and {@link #disconnect} methods of this object cannot be called simultaneously
     * with each other or any other method. However, the {@link #send} method is
     * thread-safe and may be called simultaneously from multiple threads. The
     * {@link #syncSend} method is thread-safe with other methods but not itself
     * (since it requires a response from the server) and requires an additional lock.
     * The {@link #subscribeAndGet}, {@link #sendAndGet}, {@link #subscribe}, and
     * {@link #unsubscribe} methods are also thread-safe but require some locking
     * for bookkeeping purposes by means of other locks.
     */
    private final ReentrantReadWriteLock methodLock = new ReentrantReadWriteLock();

    /** Lock for calling {@link #connect} or {@link #disconnect}. */
    Lock connectLock = methodLock.writeLock();

    /** Lock for calling methods other than {@link #connect} or {@link #disconnect}. */
    Lock notConnectLock = methodLock.readLock();

    /** Lock to ensure {@link #subscribe} and {@link #unsubscribe} calls are sequential. */
    Lock subscribeLock = new ReentrantLock();

    /** Lock to ensure that methods using the socket, write in sequence. */
    Lock socketLock = new ReentrantLock();

    /** Used to create unique id numbers associated with a specific message subject/type pair. */
    AtomicInteger uniqueId;

    /** The subdomain server object or client handler implements {@link #send}. */
    boolean hasSend;

    /** The subdomain server object or client handler implements {@link #syncSend}. */
    boolean hasSyncSend;

    /** The subdomain server object or client handler implements {@link #subscribeAndGet}. */
    boolean hasSubscribeAndGet;

    /** The subdomain server object or client handler implements {@link #sendAndGet}. */
    boolean hasSendAndGet;

    /** The subdomain server object or client handler implements {@link #subscribe}. */
    boolean hasSubscribe;

    /** The subdomain server object or client handler implements {@link #unsubscribe}. */
    boolean hasUnsubscribe;

    /** The subdomain server object or client handler implements {@link #shutdownClients}. */
    boolean hasShutdown;

    /** Level of debug output for this class. */
    int debug = cMsgConstants.debugError;

    // For statistics/monitoring

    /** Numbers of commands sent from client to server. */
    long numTcpSends, numUdpSends, numSyncSends, numSendAndGets,
            numSubscribeAndGets, numSubscribes, numUnsubscribes;

//-----------------------------------------------------------------------------

    /**
     * Constructor which does NOT automatically try to connect to the name server specified.
     *
     * @throws cMsgException if local host name cannot be found
     */
    public cMsg() throws cMsgException {
        domain = "cMsg";

        subscriptions    = Collections.synchronizedSet(new HashSet<cMsgSubscription>(20));
        subscribeAndGets = new ConcurrentHashMap<Integer,cMsgSubscription>(20);
        sendAndGets      = new ConcurrentHashMap<Integer,cMsgGetHelper>(20);
        syncSends        = new ConcurrentHashMap<Integer,cMsgGetHelper>(10);
        uniqueId         = new AtomicInteger();
        unsubscriptions  = new ConcurrentHashMap<Object, cMsgSubscription>(20);
        failovers        = new ArrayList<ParsedUDL>(10);

        // store our host's name
        try {
            host = InetAddress.getLocalHost().getCanonicalHostName();
        }
        catch (UnknownHostException e) {
            throw new cMsgException("cMsg: cannot find host name");
        }

        // create a shutdown handler class which does a disconnect
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


//-----------------------------------------------------------------------------


    /**
     * Wait a while for a possible failover to a new cMsg server before
     * attempting to complete an interrupted command to the server or
     * before throwing an exception.
     */
    private boolean failoverSuccessful(boolean waitForResubscribes) {

        // If only 1 valid UDL is given by client, forget about
        // waiting for failovers to complete before throwing
        // an exception.
        if (!useFailoverWaiting) return false;

        // Check every .1 seconds for 3 seconds for a new connection
        // before giving up and throwing an exception.
        for (int i=0; i < 30; i++) {
            try { Thread.sleep(100); }
            catch (InterruptedException e) {}

            if (waitForResubscribes) {
                if (connected && resubscriptionsComplete) return true;
            }
            else {
                if (connected) return true;
            }
        }

        return false;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to connect to the domain server from this client.
     * This method handles multiple UDLs,
     * but passes off the real work to {@link #connectDirect}.
     *
     * @throws cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server(s)
     */
    public void connect() throws cMsgException {

        // If the UDL is a semicolon separated list of UDLs, separate them and
        // store them for future use in failovers.
        /** An array of UDLs given by user to failover to if connection to server fails. */
        String[] failoverUDLs = UDL.split(";");

        // If there's only 1 UDL ...
        if (failoverUDLs.length < 2) {
            // parse UDL
            ParsedUDL p = parseUDL(failoverUDLs[0]);
            // store locally
            p.copyToLocal();
            // connect using that UDL
            if (mustBroadcast) {
                connectWithBroadcast();
            }
            connectDirect();
            return;
        }

        // else if there's a bunch of UDL's
        if (debug >= cMsgConstants.debugInfo) {
            int i=0;
            for (String s : failoverUDLs) {
                System.out.println("UDL" + (i++) + " = " + s);
            }
        }

        // parse the list of UDLs and store them
        if (failoverUDLs.length > 10)
            failovers = new ArrayList<ParsedUDL>(failoverUDLs.length);
        ParsedUDL p;
        int viableUDLs = 0;
        for (String udl : failoverUDLs) {
            try {
                p = parseUDL(udl);
            }
            catch (cMsgException e) {
                // invalid UDL
                p = new ParsedUDL(udl, false);
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("UDL \"" + udl + "\" marked as invalid");
                }
            }
            failovers.add(p);
            if (p.valid) viableUDLs++;
        }

        // If we have more than one valid UDL, we can implement waiting
        // for a successful failover before aborting commands to the server
        // that were interrupted due to server failure.
        if (viableUDLs > 1) {
            useFailoverWaiting = true;
        }

        // Go through the UDL's until one works
        failoverIndex = -1;
        do {
            // get parsed & stored UDL info
            p = failovers.get(++failoverIndex);
            if (!p.valid) {
                connectFailures++;
                continue;
            }
            // copy info locally
            p.copyToLocal();
            // connect using that UDL info
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("Trying to connect with UDL = " + p.UDL);
            }

            try {
                if (mustBroadcast) {
                    connectWithBroadcast();
                }
                connectDirect();
                return;
            }
            catch (cMsgException e) {
                // clear effects of p.copyToLocal()
                p.clearLocal();
                connectFailures++;
            }
        } while (connectFailures < failovers.size());

        throw new cMsgException("connect: all UDLs failed");
    }

//-----------------------------------------------------------------------------


    /**
     * Method to broadcast in order to find the domain server from this client.
     * Once the server is found and returns its host and port, a direct connection
     * can be made.
     *
     * @throws cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server
     */
    private void connectWithBroadcast() throws cMsgException {
        // Need a new latch for each go round - one shot deal
        broadcastResponse = new CountDownLatch(1);

        // cannot run this simultaneously with any other public method
        connectLock.lock();
        try {
            if (connected) return;

            //-------------------------------------------------------
            // broadcast on local subnet to find cMsg server
            //-------------------------------------------------------
            DatagramPacket udpPacket;
            InetAddress broadcastAddr = null;
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
                byte[] buf = baos.toByteArray();
                udpPacket = new DatagramPacket(buf, buf.length, broadcastAddr, nameServerPort);
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

            // create a thread which will send our broadcast
            Broadcaster sender = new Broadcaster(udpPacket);
            sender.start();

            // wait up to broadcast timeout
            boolean response = false;
            if (broadcastTimeout > 0) {
                try {
                    if (broadcastResponse.await(broadcastTimeout, TimeUnit.MILLISECONDS)) {
                        response = true;
                    }
                }
                catch (InterruptedException e) {
                    System.out.println("INTERRUPTING WAIT FOR BROADCAST RESPONSE, (timeout specified)");
                }
            }
            // wait forever
            else {
                try { broadcastResponse.await(); response = true;}
                catch (InterruptedException e) {
                    System.out.println("INTERRUPTING WAIT FOR BROADCAST RESPONSE, (timeout NOT specified)");
                }
            }

            sender.interrupt();

            if (!response) {
                throw new cMsgException("No response to UDP broadcast received");
            }

//System.out.println("Got a response!, broadcast part finished ...");
        }
        finally {
            connectLock.unlock();
        }

        return;
    }

//-----------------------------------------------------------------------------

    /**
     * This class gets any response to our UDP broadcast.
     */
    class BroadcastReceiver extends Thread {

        public void run() {

            /* A slight delay here will help the main thread (calling connect)
             * to be already waiting for a response from the server when we
             * broadcast to the server here (prompting that response). This
             * will help insure no responses will be lost.
             */
            try { Thread.sleep(200); }
            catch (InterruptedException e) {}

            byte[] buf = new byte[1024];
            DatagramPacket packet = new DatagramPacket(buf, 1024);

            while (true) {
                try {
                    packet.setLength(1024);
                    udpSocket.receive(packet);

                    // if packet is smaller than 5 ints  ...
                    if (packet.getLength() < 20) {
                        continue;
                    }

//System.out.println("RECEIVED BROADCAST RESPONSE PACKET !!!");
                    // pick apart byte array received
                    int magicInt1  = cMsgUtilities.bytesToInt(buf, 0); // magic password
                    int magicInt2  = cMsgUtilities.bytesToInt(buf, 4); // magic password
                    int magicInt3  = cMsgUtilities.bytesToInt(buf, 8); // magic password

                    if ( (magicInt1 != cMsgNetworkConstants.magicNumbers[0]) ||
                         (magicInt2 != cMsgNetworkConstants.magicNumbers[1]) ||
                         (magicInt3 != cMsgNetworkConstants.magicNumbers[2]))  {
//System.out.println("  Bad magic numbers for broadcast response packet");
                         continue;
                     }

                    nameServerPort = cMsgUtilities.bytesToInt(buf, 12); // port to do a direct connection to
                    int hostLength = cMsgUtilities.bytesToInt(buf, 16); // host to do a direct connection to

                    if ((nameServerPort < 1024 || nameServerPort > 65535) ||
                        (hostLength < 0 || hostLength > 1024 - 20)) {
//System.out.println("  Wrong format for broadcast response packet");
                        continue;
                    }

                    if (packet.getLength() != 4*5 + hostLength) {
                        continue;
                    }

                    // cMsg server host
                    try { nameServerHost = new String(buf, 20, hostLength, "US-ASCII"); }
                    catch (UnsupportedEncodingException e) {}
//System.out.println("  Got port = " + nameServerPort + ", host = " + nameServerHost);
                    break;
                }
                catch (IOException e) {
                }
            }

            broadcastResponse.countDown();
        }
    }

//-----------------------------------------------------------------------------

    /**
     * This class defines a thread to broadcast a UDP packet to the
     * cMsg name server every second.
     */
    class Broadcaster extends Thread {

        DatagramPacket packet;

        Broadcaster(DatagramPacket udpPacket) {
            packet = udpPacket;
        }


        public void run() {

            try {
                /* A slight delay here will help the main thread (calling connect)
                * to be already waiting for a response from the server when we
                * broadcast to the server here (prompting that response). This
                * will help insure no responses will be lost.
                */
                Thread.sleep(100);

                while (true) {

                    try {
//System.out.println("Send broadcast packet to RC Broadcast server");
                        udpSocket.send(packet);
                    }
                    catch (IOException e) {
                        e.printStackTrace();
                    }

                    Thread.sleep(1000);
                }
            }
            catch (InterruptedException e) {
                // time to quit
 //System.out.println("Interrupted sender");
            }
        }
    }

//-----------------------------------------------------------------------------


    /**
     * Method to make the actual connection to the domain server from this client.
     *
     * @throws cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server
     */
    private void connectDirect() throws cMsgException {
        // cannot run this simultaneously with any other public method
        connectLock.lock();
        try {
            if (connected) return;

            // connect & talk to cMsg name server to check if name is unique
            Socket nsSocket = null;
            try {
                nsSocket = new Socket(nameServerHost, nameServerPort);
                // Set tcpNoDelay so no packets are delayed
                nsSocket.setTcpNoDelay(true);
                // no need to set buffer sizes
            }
            catch (IOException e) {
                try {if (nsSocket != null) nsSocket.close();} catch (IOException e1) {}
                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot create socket to name server", e);
            }


            // get host & port to send messages & other info from name server
            try {
                talkToNameServerFromClient(nsSocket);
            }
            catch (IOException e) {
                // undo everything we've just done
                //listeningThread.killThread();
                try {nsSocket.close();} catch (IOException e1) {}

                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot talk to name server", e);
            }

            // done talking to server
            try {
                nsSocket.close();
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("connect: cannot close channel to name server, continue on");
                    e.printStackTrace();
                }
            }


            // create request sending/receiving (to domain) socket
            try {
                // Do NOT use SocketChannel objects to establish communications. The socket obtained
                // from a SocketChannel object has its input and outputstreams synchronized - making 
                // simultaneous reads and writes impossible!!
                // SocketChannel.open(new InetSocketAddress(domainServerHost, domainServerPort));
//System.out.println("connect: try creating channel to connection handler");
                domainOutSocket = new Socket(domainServerHost, domainServerPort);
//System.out.println("connect: created channel to connection handler");
                domainOutSocket.setTcpNoDelay(true);
                domainOutSocket.setSendBufferSize(cMsgNetworkConstants.bigBufferSize);
                domainOut = new DataOutputStream(new BufferedOutputStream(domainOutSocket.getOutputStream(),
                                                                          cMsgNetworkConstants.bigBufferSize));
                // send magic #s to foil port-scanning
                domainOut.writeInt(cMsgNetworkConstants.magicNumbers[0]);
                domainOut.writeInt(cMsgNetworkConstants.magicNumbers[1]);
                domainOut.writeInt(cMsgNetworkConstants.magicNumbers[2]);
                domainOut.flush();

                // launch thread to start listening on receive end of "sending" socket
                listeningThread = new cMsgClientListeningThread(this, domainOutSocket);
                listeningThread.start();
            }
            catch (IOException e) {
                // undo everything we've just done so far
                try {if (domainOutSocket != null) domainOutSocket.close();} catch (IOException e1) {}
                if (listeningThread != null) listeningThread.killThread();

                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot create channel to domain server", e);
            }


            // create keepAlive socket
            try {
//System.out.println("connect: try creating channel to connection handler");
                keepAliveSocket = new Socket(domainServerHost, domainServerPort);
//System.out.println("connect: created KA channel to connection handler");
                keepAliveSocket.setTcpNoDelay(true);
                keepAliveSocket.setSendBufferSize(cMsgNetworkConstants.bigBufferSize);
                // send magic #s to foil port-scanning
                DataOutputStream kaOut = new DataOutputStream(new BufferedOutputStream(
                                                              keepAliveSocket.getOutputStream()));
                kaOut.writeInt(cMsgNetworkConstants.magicNumbers[0]);
                kaOut.writeInt(cMsgNetworkConstants.magicNumbers[1]);
                kaOut.writeInt(cMsgNetworkConstants.magicNumbers[2]);
                kaOut.flush();

                // Create thread to handle dead server with failover capability.
                keepAliveThread = new KeepAlive(keepAliveSocket, useFailoverWaiting, failovers);
                keepAliveThread.start();
                // Create thread to send periodic monitor data / keep alives
                updateServerThread = new UpdateServer(keepAliveSocket);
                updateServerThread.start();
            }
            catch (IOException e) {
                // undo everything we've just done so far
                listeningThread.killThread();
                try { domainOutSocket.close(); } catch (IOException e1) {}
                try { if (keepAliveSocket != null) keepAliveSocket.close(); } catch (IOException e1) {}
                if (keepAliveThread != null)    keepAliveThread.killThread();
                if (updateServerThread != null) updateServerThread.killThread();

                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot create keepAlive channel to domain server", e);
            }
//System.out.println("connect: created channel to keep alive handler");


            // create udp socket to send messages on
            try {
                sendUdpSocket = new DatagramSocket();
                InetAddress addr = InetAddress.getByName(domainServerHost);
                // connect for speed and to keep out unwanted packets
                sendUdpSocket.connect(addr, domainServerUdpPort);
                sendUdpSocket.setSendBufferSize(cMsgNetworkConstants.biggestUdpBufferSize);
                sendUdpPacket = new DatagramPacket(new byte[0], 0, addr, domainServerUdpPort);
                // System.out.println("udp socket connected to host = " + domainServerHost +
                // " and port = " + domainServerUdpPort);
            }
            catch (UnknownHostException e) {
                listeningThread.killThread();
                keepAliveThread.killThread();
                updateServerThread.killThread();
                try {
                    keepAliveSocket.close();} catch (IOException e1) {}
                try {domainOutSocket.close();} catch (IOException e1) {}
                if (sendUdpSocket != null) sendUdpSocket.close();

                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot create udp socket to domain server", e);
            }
            catch (SocketException e) {
                listeningThread.killThread();
                keepAliveThread.killThread();
                updateServerThread.killThread();
                try {
                    keepAliveSocket.close();} catch (IOException e1) {}
                try {domainOutSocket.close();} catch (IOException e1) {}
                if (sendUdpSocket != null) sendUdpSocket.close();

                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot create udp socket to domain server", e);
            }

            connected = true;
        }
        finally {
            connectLock.unlock();
        }

        return;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to force cMsg client to send pending communications with domain server.
     * In the cMsg domain implementation, this method does nothing.
     * @param timeout ignored in this domain
     */
    public void flush(int timeout) {
        return;
    }


//-----------------------------------------------------------------------------


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

            // Stop keep alive threads & close channel so when domain server
            // shuts down, we don't detect it's dead and make a fuss.
            //
            // NOTE: It may be the keepAlive thread that is calling this method.
            // In this case, it exits after running this method.
            keepAliveThread.killThread();
            keepAliveThread.interrupt();
            updateServerThread.killThread();
            updateServerThread.interrupt();

            // give thread a chance to shutdown
            try { Thread.sleep(100); }
            catch (InterruptedException e) {}

            // tell server we're disconnecting
            socketLock.lock();
            try {
                domainOut.writeInt(4);
                domainOut.writeInt(cMsgConstants.msgDisconnectRequest);
                domainOut.flush();
            }
            catch (IOException e) {
            }
            finally {
                socketLock.unlock();
            }

            // close all our streams and sockets
            try {domainOut.close();}        catch (IOException e) {}
            try {domainOutSocket.close();} catch (IOException e) {}
            try {
                keepAliveSocket.close();} catch (IOException e) {}
            sendUdpSocket.close();

            // give server a chance to shutdown
            try { Thread.sleep(100); }
            catch (InterruptedException e) {}

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
            }

            // wakeup all subscribeAndGets
            for (cMsgGetHelper helper : subscribeAndGets.values()) {
                helper.setMessage(null);
                synchronized (helper) {
                    helper.notify();
                }
            }

            // wakeup all sendAndGets
            for (cMsgGetHelper helper : sendAndGets.values()) {
                helper.setMessage(null);
                synchronized (helper) {
                    helper.notify();
                }
            }

            // wakeup all syncSends
            for (cMsgGetHelper helper : syncSends.values()) {
                helper.setErrorCode(cMsgConstants.errorAbort);
                synchronized (helper) {
                    helper.notify();
                }
            }

            // empty all hash tables
            subscriptions.clear();
            sendAndGets.clear();
            subscribeAndGets.clear();
            unsubscriptions.clear();
            failovers.clear();
        }
        finally {
            connectLock.unlock();
        }
//System.out.println("\nReached end of disconnect method");
    }


//-----------------------------------------------------------------------------


    /**
     * Method to reconnect to another server if the existing connection dies.
     */
    private void reconnect() throws cMsgException {

        // cannot run this simultaneously with any other public method
        connectLock.lock();

        try {
            // KeepAlive thread needs to keep running.
            // Keep all existing callback threads for the subscribes.

            // wakeup all syncSends - they can't be saved
            for (cMsgGetHelper helper : syncSends.values()) {
                helper.setMessage(null);
                helper.setErrorCode(cMsgConstants.errorServerDied);
                synchronized (helper) {
                    helper.notify();
                }
            }

            // wakeup all subscribeAndGets - they can't be saved
            for (cMsgGetHelper helper : subscribeAndGets.values()) {
                helper.setMessage(null);
                helper.setErrorCode(cMsgConstants.errorServerDied);
                synchronized (helper) {
                    helper.notify();
                }
            }

            // wakeup all existing sendAndGets - they can't be saved
            for (cMsgGetHelper helper : sendAndGets.values()) {
                helper.setMessage(null);
                helper.setErrorCode(cMsgConstants.errorServerDied);
                synchronized (helper) {
                    helper.notify();
                }
            }

            // shutdown listening thread (since socket needs to change)
            listeningThread.killThread();

            // connect & talk to cMsg name server to check if name is unique
            Socket nsSocket = null;
            try {
                nsSocket = new Socket(nameServerHost, nameServerPort);
                // Set tcpNoDelay so no packets are delayed
                nsSocket.setTcpNoDelay(true);
                // no need to set buffer sizes
            }
            catch (UnresolvedAddressException e) {
                try {if (nsSocket != null) nsSocket.close();} catch (IOException e1) {}
                throw new cMsgException("reconnect: cannot create socket to name server", e);
            }
            catch (IOException e) {
                try {if (nsSocket != null) nsSocket.close();} catch (IOException e1) {}
                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("reconnect: cannot create socket to name server", e);
            }

            // get host & port to send messages & other info from name server
            try {
                talkToNameServerFromClient(nsSocket);
            }
            catch (IOException e) {
                try {
                    nsSocket.close();} catch (IOException e1) {}
                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("reconnect: cannot talk to name server", e);
            }

            // done talking to server
            try {
                nsSocket.close();
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("reconnect: cannot close socket to name server, continue on");
                    e.printStackTrace();
                }
            }

            // create request sending (to domain) socket
            try {
                // Do NOT use SocketChannel objects to establish communications. The socket obtained
                // from a SocketChannel object has its input and outputstreams synchronized - making
                // simultaneous reads and writes impossible!!
                // SocketChannel.open(new InetSocketAddress(domainServerHost, domainServerPort));
                domainOutSocket = new Socket(domainServerHost, domainServerPort);
                domainOutSocket.setTcpNoDelay(true);
                domainOutSocket.setSendBufferSize(cMsgNetworkConstants.bigBufferSize);
                domainOut = new DataOutputStream(new BufferedOutputStream(domainOutSocket.getOutputStream(),
                                                                          cMsgNetworkConstants.bigBufferSize));
                // send magic #s to foil port-scanning
                domainOut.writeInt(cMsgNetworkConstants.magicNumbers[0]);
                domainOut.writeInt(cMsgNetworkConstants.magicNumbers[1]);
                domainOut.writeInt(cMsgNetworkConstants.magicNumbers[2]);
                domainOut.flush();

                // launch thread to start listening on receive end of "sending" socket
                listeningThread = new cMsgClientListeningThread(this, domainOutSocket);
                listeningThread.start();
            }
            catch (IOException e) {
                // undo everything we've just done so far
                try {if (domainOutSocket != null) domainOutSocket.close();} catch (IOException e1) {}
                if (listeningThread != null) listeningThread.killThread();

                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot create socket to domain server", e);
            }


            // create keepAlive socket
            try {
                keepAliveSocket = new Socket(domainServerHost, domainServerPort);
                keepAliveSocket.setTcpNoDelay(true);

                // send magic #s to foil port-scanning
                DataOutputStream kaOut = new DataOutputStream(new BufferedOutputStream(
                                                              keepAliveSocket.getOutputStream()));
                kaOut.writeInt(cMsgNetworkConstants.magicNumbers[0]);
                kaOut.writeInt(cMsgNetworkConstants.magicNumbers[1]);
                kaOut.writeInt(cMsgNetworkConstants.magicNumbers[2]);
                kaOut.flush();

                // Create thread to handle dead server with failover capability.
                keepAliveThread = new KeepAlive(keepAliveSocket, useFailoverWaiting, failovers);
                keepAliveThread.start();
                // Create thread to send periodic monitor data / keep alives
                updateServerThread = new UpdateServer(keepAliveSocket);
                updateServerThread.start();
            }
            catch (IOException e) {
                // undo everything we've just done so far
                listeningThread.killThread();
                try { domainOutSocket.close(); } catch (IOException e1) {}
                try { if (keepAliveSocket != null) keepAliveSocket.close(); } catch (IOException e1) {}
                if (keepAliveThread != null)    keepAliveThread.killThread();
                if (updateServerThread != null) updateServerThread.killThread();

                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot create keepAlive socket to domain server", e);
            }


            // create udp socket to send messages on
            try {
                sendUdpSocket = new DatagramSocket();
                InetAddress addr = InetAddress.getByName(domainServerHost);
                // connect for speed and to keep out unwanted packets
                sendUdpSocket.connect(addr, domainServerUdpPort);
                sendUdpSocket.setSendBufferSize(cMsgNetworkConstants.biggestUdpBufferSize);
                sendUdpPacket = new DatagramPacket(new byte[0], 0, addr, domainServerUdpPort);
                // System.out.println("udp socket connected to host = " + domainServerHost +
                // " and port = " + domainServerUdpPort);
            }
            catch (UnknownHostException e) {
                listeningThread.killThread();
                keepAliveThread.killThread();
                updateServerThread.killThread();
                try {
                    keepAliveSocket.close();} catch (IOException e1) {}
                try {domainOutSocket.close();} catch (IOException e1) {}
                if (sendUdpSocket != null) sendUdpSocket.close();

                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("reconnect: cannot create udp socket to domain server", e);
            }
            catch (SocketException e) {
                listeningThread.killThread();
                keepAliveThread.killThread();
                updateServerThread.killThread();
                try {
                    keepAliveSocket.close();} catch (IOException e1) {}
                try {domainOutSocket.close();} catch (IOException e1) {}
                if (sendUdpSocket != null) sendUdpSocket.close();

                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("reconnect: cannot create udp socket to domain server", e);
            }

            connected = true;
        }
        finally {
            connectLock.unlock();
        }

        return;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to send a message to the domain server for further distribution.
     *
     * @param message message to send
     * @throws cMsgException if there are communication problems with the server;
     *                       subject and/or type is null; message is too big for
     *                       UDP packet size if doing UDP send
     */
    public void send(cMsgMessage message) throws cMsgException {

        if (!hasSend) {
            throw new cMsgException("send is not implemented by this subdomain");
        }

        if (!message.getContext().getReliableSend()) {
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

        // Payload stuff. Include the name of this sender as part of a history
        // of senders in the cMsgSenderHistory payload field. Note that msg may
        // be set not to record any history.
        message.addSenderToHistory(name);
        String payloadTxt = message.getPayloadText();
        int payloadLen = 0;
        if (payloadTxt != null) {
            payloadLen = payloadTxt.length();
        }

        int binaryLength = message.getByteArrayLength();

        // go here to try the send again
        while (true) {

            // cannot run this simultaneously with connect, reconnect, or disconnect
            notConnectLock.lock();
            // protect communicatons over socket
            socketLock.lock();

            try {
                if (!connected) {
                    throw new IOException("not connected to server");
                }
                int size = 4 * 15 + subject.length() + type.length() + payloadLen +
                                  textLen + binaryLength;
//System.out.println("send: writing " + size + " bytes in send msg");
                // total length of msg (not including this int) is 1st item
                domainOut.writeInt(size);
                domainOut.writeInt(cMsgConstants.msgSendRequest);
                domainOut.writeInt(0); // reserved for future use
                domainOut.writeInt(message.getUserInt());
                domainOut.writeInt(message.getSysMsgId());
                domainOut.writeInt(message.getSenderToken());
                domainOut.writeInt(message.getInfo());

                long now = new Date().getTime();
                // send the time in milliseconds as 2, 32 bit integers
                domainOut.writeInt((int) (now >>> 32)); // higher 32 bits
                domainOut.writeInt((int) (now & 0x00000000FFFFFFFFL)); // lower 32 bits
                domainOut.writeInt((int) (message.getUserTime().getTime() >>> 32));
                domainOut.writeInt((int) (message.getUserTime().getTime() & 0x00000000FFFFFFFFL));

                domainOut.writeInt(subject.length());
                domainOut.writeInt(type.length());
                domainOut.writeInt(payloadLen);
                domainOut.writeInt(textLen);
                domainOut.writeInt(binaryLength);

                // write strings & byte array
                try {
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

//System.out.println("send: flush");
                domainOut.flush();
//System.out.println("send: flush done");
                numTcpSends++;
            }
            catch (IOException e) {
//System.out.println("send: error");
                // wait awhile for possible failover
                if (failoverSuccessful(false)) {
                    continue;
                }
                throw new cMsgException(e.getMessage());

            }
            finally {
//System.out.println("send: try unlocking mutexes");
                socketLock.unlock();
                notConnectLock.unlock();
            }

            break;
        }
//System.out.println("send: DONE");

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

        // Payload stuff. Include the name of this sender as part of a history
        // of senders in the cMsgSenderHistory payload field. Note that msg may
        // be set not to record any history.
        message.addSenderToHistory(name);
        String payloadTxt = message.getPayloadText();
        int payloadLen = 0;
        if (payloadTxt != null) {
            payloadLen = payloadTxt.length();
        }

        int binaryLength = message.getByteArrayLength();

        // total length of msg (not including first int which is this size & the magic #s)
        int totalLength = 4*15 + subject.length() + type.length() + payloadLen +
                                 textLen + binaryLength;
        if (totalLength > cMsgNetworkConstants.biggestUdpBufferSize) {
            throw new cMsgException("Too big a message for UDP to send");
        }

        // create byte array for broadcast
        ByteArrayOutputStream baos = new ByteArrayOutputStream(totalLength);
        DataOutputStream out = new DataOutputStream(baos);

        // go here to try the send again
        while (true) {
            // cannot run this simultaneously with connect, reconnect, or disconnect
            notConnectLock.lock();

            try {
                out.writeInt(cMsgNetworkConstants.magicNumbers[0]); // cMsg
                out.writeInt(cMsgNetworkConstants.magicNumbers[1]); // is
                out.writeInt(cMsgNetworkConstants.magicNumbers[2]); // cool
                
                out.writeInt(totalLength); // total length of msg (not including this int)
                out.writeInt(cMsgConstants.msgSendRequest);
                out.writeInt(0); // reserved for future use
                out.writeInt(message.getUserInt());
                out.writeInt(message.getSysMsgId());
                out.writeInt(message.getSenderToken());
                out.writeInt(message.getInfo());

                long now = new Date().getTime();
                // send the time in milliseconds as 2, 32 bit integers
                out.writeInt((int) (now >>> 32)); // higher 32 bits
                out.writeInt((int) (now & 0x00000000FFFFFFFFL)); // lower 32 bits
                out.writeInt((int) (message.getUserTime().getTime() >>> 32));
                out.writeInt((int) (message.getUserTime().getTime() & 0x00000000FFFFFFFFL));

                out.writeInt(subject.length());
                out.writeInt(type.length());
                out.writeInt(payloadLen);
                out.writeInt(textLen);
                out.writeInt(binaryLength);

                // write strings & byte array
                try {
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

                // send message packet from the byte array
                byte[] buf = baos.toByteArray();

                synchronized (sendUdpPacket) {
                    // setData is synchronized on the packet.
                    sendUdpPacket.setData(buf, 0, buf.length);
                    // send in synchronized internally on the packet object.
                    // Because we only use one packet object for this client,
                    // all udp sends are synchronized.
                    sendUdpSocket.send(sendUdpPacket);
                }
                numUdpSends++;
            }
            catch (IOException e) {
                // wait awhile for possible failover
                if (failoverSuccessful(false)) {
                    continue;
                }
                throw new cMsgException("Cannot create or send message packet", e);
            }
            finally {
                notConnectLock.unlock();
            }

            break;
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to send a message to the domain server for further distribution
     * and wait for a response from the subdomain handler that got it.
     *
     * @param message message
     * @param timeout ignored in this domain
     * @return response from subdomain handler (0 in this subdomain)
     * @throws cMsgException if there are communication problems with the server;
     *                       subject and/or type is null
     */
    public int syncSend(cMsgMessage message, int timeout) throws cMsgException {

        if (!hasSyncSend) {
            throw new cMsgException("sync send is not implemented by this subdomain");
        }

        String subject = message.getSubject();
        String type = message.getType();

        // check args first
        if (subject == null || type == null) {
            throw new cMsgException("message subject and/or type is null");
        }

        // check for null text
        String text = message.getText();
        int textLen = 0;
        if (text != null) {
            textLen = text.length();
        }

        // Payload stuff. Include the name of this sender as part of a history
        // of senders in the cMsgSenderHistory payload field. Note that msg may
        // be set not to record any history.
        message.addSenderToHistory(name);
        String payloadTxt = message.getPayloadText();
        int payloadLen = 0;
        if (payloadTxt != null) {
            payloadLen = payloadTxt.length();
        }

        int binaryLength = message.getByteArrayLength();

        // go here to try the syncSend again
        while (true) {

            // cannot run this simultaneously with connect, reconenct, or disconnect
            notConnectLock.lock();

            // We're expecting a response, so the id is sent back
            // in the response message, allowing us to wake this call.
            int id = uniqueId.getAndIncrement();

            // track specific syncSend request
            cMsgGetHelper helper = new cMsgGetHelper();
            syncSends.put(id, helper);

            try {

                if (!connected) {
                    throw new IOException("not connected to server");
                }

                socketLock.lock();
                try {
                    // total length of msg (not including this int) is 1st item
                    domainOut.writeInt(4 * 15 + subject.length() + type.length() + payloadLen +
                                       textLen + binaryLength);
                    domainOut.writeInt(cMsgConstants.msgSyncSendRequest);
                    domainOut.writeInt(id);
                    domainOut.writeInt(message.getUserInt());
                    domainOut.writeInt(message.getSysMsgId());
                    domainOut.writeInt(message.getSenderToken());
                    domainOut.writeInt(message.getInfo());

                    long now = new Date().getTime();
                    // send the time in milliseconds as 2, 32 bit integers
                    domainOut.writeInt((int) (now >>> 32)); // higher 32 bits
                    domainOut.writeInt((int) (now & 0x00000000FFFFFFFFL)); // lower 32 bits
                    domainOut.writeInt((int) (message.getUserTime().getTime() >>> 32));
                    domainOut.writeInt((int) (message.getUserTime().getTime() & 0x00000000FFFFFFFFL));

                    domainOut.writeInt(subject.length());
                    domainOut.writeInt(type.length());
                    domainOut.writeInt(payloadLen);
                    domainOut.writeInt(textLen);
                    domainOut.writeInt(binaryLength);

                    // write strings & byte array
                    try {
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
                }
                finally {
                    socketLock.unlock();
                }

                domainOut.flush(); // no need to be protected by socketLock
                numSyncSends++;
            }
            catch (IOException e) {
                // wait awhile for possible failover
                if (failoverSuccessful(false)) {
                    continue;
                }
                throw new cMsgException(e.getMessage());
            }
            finally {
                notConnectLock.unlock();
            }

            // WAIT for the msg-receiving thread to wake us up
            try {
                synchronized (helper) {
                    // syncSend responses can be so fast that the helper's
                    // notify method has already been called before we have
                    // had time to wait in the code below. Thus, first check
                    // the timedOut variable. If that is false, the response
                    // has already come.
                    if (helper.needToWait()) {
                        helper.wait();
                    }
                }
            }
            catch (InterruptedException e) {
            }

            if (helper.getErrorCode() != cMsgConstants.ok) {
                throw new cMsgException("syncSend abort", helper.getErrorCode());
            }

            // If response arrived, client listening thread has also removed subscription from client's
            // records (syncSends HashSet).
            return helper.getIntVal();
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
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @param userObj any user-supplied object to be given to the callback method as an argument
     * @return handle object to be used for unsubscribing
     * @throws cMsgException if the callback, subject and/or type is null or blank;
     *                       an identical subscription already exists; there are
     *                       communication problems with the server
     */
    public Object subscribe(String subject, String type, cMsgCallbackInterface cb, Object userObj)
            throws cMsgException {

        if (!hasSubscribe) {
            throw new cMsgException("subscribe is not implemented by this subdomain");
        }

        // check args first
        if (subject == null || type == null || cb == null) {
            throw new cMsgException("subject, type or callback argument is null");
        }
        else if (subject.length() < 1 || type.length() < 1) {
            throw new cMsgException("subject or type is blank string");
        }

        cMsgCallbackThread cbThread = null;

        // go here to try the subscribe again
        while (true) {

            boolean addedHashEntry = false;
            cMsgSubscription newSub = null;

            // cannot run this simultaneously with connect or disconnect
            notConnectLock.lock();
            // cannot run this simultaneously with unsubscribe (get wrong order at server)
            // or itself (iterate over same hashtable)
            subscribeLock.lock();

            try {
                if (!connected) {
                    throw new cMsgException("not connected to server");
                }

                // add to callback list if subscription to same subject/type exists

                int id;

                // client listening thread may be interating thru subscriptions concurrently
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
                            numSubscribes++;
                            return cbThread;
                        }
                    }

                    // If we're here, the subscription to that subject & type does not exist yet.
                    // We need to create it and register it with the domain server.

                    // First generate a unique id for the receiveSubscribeId field. This info
                    // allows us to unsubscribe.
                    id = uniqueId.getAndIncrement();

                    // add a new subscription & callback
                    cbThread = new cMsgCallbackThread(cb, userObj, domain, subject, type);
                    newSub = new cMsgSubscription(subject, type, id, cbThread);
                    unsubscriptions.put(cbThread, newSub);

                    // client listening thread may be interating thru subscriptions concurrently
                    // and we're changing the set structure
                    subscriptions.add(newSub);
                    addedHashEntry = true;
                }

                socketLock.lock();
                try {
//System.out.println("subscribe: writing into domainOut");
                    // total length of msg (not including this int) is 1st item
                    domainOut.writeInt(5 * 4 + subject.length() + type.length());
                    domainOut.writeInt(cMsgConstants.msgSubscribeRequest);
                    domainOut.writeInt(id); // reserved for future use
                    domainOut.writeInt(subject.length());
                    domainOut.writeInt(type.length());
                    domainOut.writeInt(0); // namespace length (we don't send this from
                    // regular client only from "server" client)

                    // write strings & byte array
                    try {
                        domainOut.write(subject.getBytes("US-ASCII"));
                        domainOut.write(type.getBytes("US-ASCII"));
                    }
                    catch (UnsupportedEncodingException e) {
                    }

//System.out.println("subscribe: flush");
                    domainOut.flush();
//System.out.println("subscribe: flush done");
                    numSubscribes++;
                }
                finally {
                    socketLock.unlock();
                }
            }
            catch (IOException e) {
//System.out.println("subscribe: error");
                // undo the modification of the hashtable we made & stop the created thread
                if (addedHashEntry) {
                    // "subscriptions" is synchronized so it's mutex protected
                    subscriptions.remove(newSub);
                    unsubscriptions.remove(cbThread);
                    cbThread.dieNow(true);
                }

                // wait awhile for possible failover
                if (failoverSuccessful(false)) {
                    continue;
                }

                throw new cMsgException(e.getMessage());
            }
            finally {
                subscribeLock.unlock();
                notConnectLock.unlock();
            }

            break;
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
     * @param obj the object "handle" returned from a subscribe call
     * @throws cMsgException if there are communication problems with the server; object arg is null
     */
    public void unsubscribe(Object obj)
            throws cMsgException {

        if (!hasUnsubscribe) {
            throw new cMsgException("unsubscribe is not implemented by this subdomain");
        }

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

        // go here to try the unsubscribe again
        while (true) {

            // cannot run this simultaneously with connect or disconnect
            notConnectLock.lock();
            // cannot run this simultaneously with subscribe (get wrong order at server)
            // or itself (iterate over same hashtable)
            subscribeLock.lock();

            try {
                if (!connected) {
                    throw new cMsgException("not connected to server");
                }

                // If there are still callbacks left,
                // don't unsubscribe for this subject/type
                if (sub.numberOfCallbacks() > 1) {
                    // kill callback thread
                    cbThread.dieNow(false);
                    // remove this callback from the set
                    synchronized (subscriptions) {
                        sub.getCallbacks().remove(cbThread);
                    }
                    numUnsubscribes++;
                    return;
                }

                // Get rid of the whole subscription - notify the domain server
                String subject = sub.getSubject();
                String type = sub.getType();

                socketLock.lock();
                try {
                    // total length of msg (not including this int) is 1st item
                    domainOut.writeInt(5 * 4 + subject.length() + type.length());
                    domainOut.writeInt(cMsgConstants.msgUnsubscribeRequest);
                    domainOut.writeInt(sub.getIntVal()); // reserved for future use
                    domainOut.writeInt(subject.length());
                    domainOut.writeInt(type.length());
                    domainOut.writeInt(0); // no namespace being sent

                    // write strings & byte array
                    try {
                        domainOut.write(subject.getBytes("US-ASCII"));
                        domainOut.write(type.getBytes("US-ASCII"));
                    }
                    catch (UnsupportedEncodingException e) {
                    }
                    domainOut.flush();
                    numUnsubscribes++;
                }
                finally {
                    socketLock.unlock();
                }

                // Now that we've communicated with the server,
                // delete stuff from hashes & kill threads -
                // basically, do the unsubscribe now.
                cbThread.dieNow(false);
                synchronized (subscriptions) {
                    sub.getCallbacks().remove(cbThread);
                    subscriptions.remove(sub);
                }
            }
            catch (IOException e) {
                // wait awhile for possible failover && resubscribe is complete
                if (failoverSuccessful(true)) {
                    continue;
                }

                throw new cMsgException(e.getMessage());
            }
            finally {
                subscribeLock.unlock();
                notConnectLock.unlock();
            }

            break;
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to resubscribe to receive messages of a subject and type from the domain server.
     * This method is run when failing over to a new server and subscriptions must be
     * reestablished on the new server.
     *
     * If any resubscribe fails, then an attempt is made to connect to another failover server.
     *
     * @param subject message subject
     * @param type    message type
     * @throws cMsgException if the callback, subject and/or type is null or blank;
     *                       an identical subscription already exists; there are
     *                       communication problems with the server
     */
    private void resubscribe(String subject, String type)
            throws cMsgException {

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();
        // cannot run this simultaneously with unsubscribe (get wrong order at server)
        // or itself (iterate over same hashtable)
        subscribeLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            // add to callback list if subscription to same subject/type exists

            int id;
            boolean gotSub = false;

            // if an unsubscribe has been done, forget about resubscribing
            synchronized (subscriptions) {
                // for each subscription ...
                cMsgSubscription mySub = null;
                for (cMsgSubscription sub : subscriptions) {
                    // If subscription to subject & type exist we're OK
                    if (sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                        mySub  = sub;
                        gotSub = true;
                        break;
                    }
                }

                if (!gotSub) return;

                // First generate a unique id for the receiveSubscribeId field. This info
                // allows us to unsubscribe.
                id = uniqueId.getAndIncrement();
                mySub.setIntVal(id);
            }

            socketLock.lock();
            try {
                // total length of msg (not including this int) is 1st item
                domainOut.writeInt(5*4 + subject.length() + type.length());
                domainOut.writeInt(cMsgConstants.msgSubscribeRequest);
                domainOut.writeInt(id); // reserved for future use
                domainOut.writeInt(subject.length());
                domainOut.writeInt(type.length());
                domainOut.writeInt(0); // namespace length (we don't send this from
                                       // regular client only from "server" client)

                // write strings & byte array
                try {
                    domainOut.write(subject.getBytes("US-ASCII"));
                    domainOut.write(type.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {}

                domainOut.flush();
            }
            finally {
                socketLock.unlock();
            }
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
        finally {
            subscribeLock.unlock();
            notConnectLock.unlock();
        }
    }


//-----------------------------------------------------------------------------


    /**
     * This method is like a one-time subscribe. The server grabs the first incoming
     * message of the requested subject and type and sends that to the caller.
     *
     * NOTE: Disconnecting when one thread is in the waiting part of a subscribeAndGet may cause that
     * thread to block forever. It is best to always use a timeout with subscribeAndGet so the thread
     * is assured of eventually resuming execution.
     *
     * @param subject subject of message desired from server
     * @param type type of message desired from server
     * @param timeout time in milliseconds to wait for a message
     * @return response message
     * @throws cMsgException if there are communication problems with the server;
     *                       server dies; subject and/or type is null or blank
     * @throws TimeoutException if timeout occurs
     */
    public cMsgMessage subscribeAndGet(String subject, String type, int timeout)
            throws cMsgException, TimeoutException {

        if (!hasSubscribeAndGet) {
            throw new cMsgException("subscribeAndGet is not implemented by this subdomain");
        }

        // check args first
        if (subject == null || type == null) {
            throw new cMsgException("message subject or type is null");
        }
        else if (subject.length() < 1 || type.length() < 1) {
            throw new cMsgException("message subject or type is blank string");
        }

        int id = 0;
        cMsgSubscription sub = null;
        boolean addedHashEntry  = false;

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            // First generate a unique id for the receiveSubscribeId and senderToken field.
            //
            // The receiverSubscribeId is sent back by the domain server in the future when
            // messages of this subject and type are sent to this cMsg client. This helps
            // eliminate the need to parse subject and type each time a message arrives.

            id = uniqueId.getAndIncrement();

            // create cMsgSubscription object (not callback thread object)
            sub = new cMsgSubscription(subject, type);

            // keep track of get calls
            subscribeAndGets.put(id, sub);
            addedHashEntry = true;

            socketLock.lock();
            try {
                // total length of msg (not including this int) is 1st item
                domainOut.writeInt(5*4 + subject.length() + type.length());
                domainOut.writeInt(cMsgConstants.msgSubscribeAndGetRequest);
                domainOut.writeInt(id); // reserved for future use
                domainOut.writeInt(subject.length());
                domainOut.writeInt(type.length());
                domainOut.writeInt(0); // namespace length (we don't send this from
                                       // regular client only from "server" client)

                // write strings & byte array
                try {
                    domainOut.write(subject.getBytes("US-ASCII"));
                    domainOut.write(type.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {}
                numSubscribeAndGets++;
            }
            finally {
                socketLock.unlock();
            }
            domainOut.flush();
        }
        catch (IOException e) {
            e.printStackTrace();

            if (addedHashEntry) {
                subscribeAndGets.remove(id);
            }
            throw new cMsgException(e.getMessage());
        }
        // release lock 'cause we can't block connect/disconnect forever
        finally {
            notConnectLock.unlock();
        }

        // WAIT for the msg-receiving thread to wake us up
        try {
            synchronized (sub) {
                if (timeout > 0) {
//System.out.println("subscribeAndGet: wait for time = " + timeout);
                    sub.wait(timeout);
                }
                else {
//System.out.println("subscribeAndGet: wait forever");
                    sub.wait();
                }
            }
        }
        catch (InterruptedException e) {
        }

        // Check the message stored for us in sub.
        // If msg is null, we timed out.
        // Tell server to forget the get if necessary.
        if (sub.isTimedOut()) {
            // remove the get from server
//System.out.println("subscribeAndGet: timed out");
            subscribeAndGets.remove(id);
            unSubscribeAndGet(subject, type, id);
            throw new TimeoutException();
        }
        else if (sub.getErrorCode() != cMsgConstants.ok) {
            throw new cMsgException("server died", sub.getErrorCode());
        }

        // If msg is received, server has removed subscription from his records.
        // Client listening thread has also removed subscription from client's
        // records (subscribeAndGets HashSet).
        return sub.getMessage();
    }


//-----------------------------------------------------------------------------


    /**
     * Method to remove a previous subscribeAndGet to receive a message of a subject
     * and type from the domain server. This method is only called when a subscribeAndGet
     * times out and the server must be told to forget about the subscribeAndGet.
     *
     * If there is an IOException, we do NOT care because the only reason unSubscribeAndGet
     * is called is to keep the server's bookeeping straight. If the server is dead, there's
     * no point in doing so.
     *
     * @param subject subject of subscription
     * @param type type of subscription
     * @param id unique id of subscribeAndGet request to delete
     */
    private void unSubscribeAndGet(String subject, String type, int id) {

        if (!connected) {
            return;
            //throw new cMsgException("not connected to server");
        }

        socketLock.lock();
        try {
            // total length of msg (not including this int) is 1st item
            domainOut.writeInt(5*4 + subject.length() + type.length());
            domainOut.writeInt(cMsgConstants.msgUnsubscribeAndGetRequest);
            domainOut.writeInt(id); // reseved for future use
            domainOut.writeInt(subject.length());
            domainOut.writeInt(type.length());
            domainOut.writeInt(0); // no namespace being sent

            // write strings & byte array
            try {
                domainOut.write(subject.getBytes("US-ASCII"));
                domainOut.write(type.getBytes("US-ASCII"));
            }
            catch (UnsupportedEncodingException e) {}
        }
        catch (IOException e) {
            //throw new cMsgException(e.getMessage());
        }
        finally {
            socketLock.unlock();
        }
    }


//-----------------------------------------------------------------------------


    /**
     * The message is sent as it would be in the {@link #send} method. The server notes
     * the fact that a response to it is expected, and sends it to all subscribed to its
     * subject and type. When a marked response is received from a client, it sends the
     * first response back to the original sender regardless of its subject or type.
     *
     * NOTE: Disconnecting when one thread is in the waiting part of a sendAndGet may cause that
     * thread to block forever. It is best to always use a timeout with sendAndGet so the thread
     * is assured of eventually resuming execution.
     *
     * @param message message sent to server
     * @param timeout time in milliseconds to wait for a reponse message
     * @return response message
     * @throws cMsgException if there are communication problems with the server;
     *                       server died; subject and/or type is null
     * @throws TimeoutException if timeout occurs
     */
    public cMsgMessage sendAndGet(cMsgMessage message, int timeout)
            throws cMsgException, TimeoutException {

        if (!hasSendAndGet) {
            throw new cMsgException("sendAndGet is not implemented by this subdomain");
        }

        String subject    = message.getSubject();
        String type       = message.getType();

        // check args first
        if (subject == null || type == null) {
            throw new cMsgException("message subject and/or type is null");
        }

        // check for null text
        String text = message.getText();
        int textLen = 0;
        if (text != null) {
            textLen = text.length();
        }

        // Payload stuff. Include the name of this sender as part of a history
        // of senders in the cMsgSenderHistory payload field. Note that msg may
        // be set not to record any history.
        message.addSenderToHistory(name);
        String payloadTxt = message.getPayloadText();
        int payloadLen = 0;
        if (payloadTxt != null) {
            payloadLen = payloadTxt.length();
        }

        int id = 0;
        cMsgGetHelper helper = null;
        boolean addedHashEntry = false;

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            // We need send msg to domain server who will see we get a response.
            // First generate a unique id for the receiveSubscribeId and senderToken field.
            //
            // We're expecting a specific response, so the senderToken is sent back
            // in the response message, allowing us to run the correct callback.

            id = uniqueId.getAndIncrement();

            // for get, create cMsgHolder object (not callback thread object)
            helper = new cMsgGetHelper();

            // track specific get requests
            sendAndGets.put(id, helper);
            addedHashEntry = true;

            int binaryLength = message.getByteArrayLength();

            socketLock.lock();
            try {
                // total length of msg (not including this int) is 1st item
                domainOut.writeInt(4*15 + subject.length() + type.length() + payloadLen +
                                   textLen + binaryLength);
                domainOut.writeInt(cMsgConstants.msgSendAndGetRequest);
                domainOut.writeInt(0); // reserved for future use
                domainOut.writeInt(message.getUserInt());
                domainOut.writeInt(id);
                domainOut.writeInt(message.getInfo() | cMsgMessage.isGetRequest);

                long now = new Date().getTime();
                // send the time in milliseconds as 2, 32 bit integers
                domainOut.writeInt((int) (now >>> 32)); // higher 32 bits
                domainOut.writeInt((int) (now & 0x00000000FFFFFFFFL)); // lower 32 bits
                domainOut.writeInt((int) (message.getUserTime().getTime() >>> 32));
                domainOut.writeInt((int) (message.getUserTime().getTime() & 0x00000000FFFFFFFFL));

                domainOut.writeInt(subject.length());
                domainOut.writeInt(type.length());
                domainOut.writeInt(0); // namespace length
                domainOut.writeInt(payloadLen);
                domainOut.writeInt(textLen);
                domainOut.writeInt(binaryLength);

                // write strings & byte array
                try {
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
                catch (UnsupportedEncodingException e) {}
                numSendAndGets++;
            }
            finally {
                socketLock.unlock();
            }

            domainOut.flush(); // no need to be protected by socketLock

        }
        catch (IOException e) {
            if (addedHashEntry) {
                sendAndGets.remove(id);
            }
            throw new cMsgException(e.getMessage());
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

        // Tell server to forget the get if necessary.
        if (helper.isTimedOut()) {
            // remove the get from server
            sendAndGets.remove(id);
            unSendAndGet(id);
            throw new TimeoutException();
        }
        else if (helper.getErrorCode() != cMsgConstants.ok) {
            throw new cMsgException("server died", helper.getErrorCode());
        }

        // If msg arrived (may be null), server has removed subscription from his records.
        // Client listening thread has also removed subscription from client's
        // records (subscribeAndGets HashSet).
        return helper.getMessage();
    }


//-----------------------------------------------------------------------------


    /**
     * Method to remove a previous sendAndGet from the domain server.
     * This method is only called when a sendAndGet times out
     * and the server must be told to forget about the sendAndGet.
     *
     * If there is an IOException, we do NOT care because the only reason unSendAndGet
     * is called is to keep the server's bookeeping straight. If the server is dead,
     * there's no point in doing so.
     *
     * @param id unique id of get request to delete
     */
    private void unSendAndGet(int id) {

        if (!connected) {
            return;
            //throw new cMsgException("not connected to server");
        }

        socketLock.lock();
        try {
            // total length of msg (not including this int) is 1st item
            domainOut.writeInt(8);
            domainOut.writeInt(cMsgConstants.msgUnSendAndGetRequest);
            domainOut.writeInt(id); // reserved for future use
            domainOut.flush();
        }
        catch (IOException e) {
            //throw new cMsgException(e.getMessage());
        }
        finally {
            socketLock.unlock();
        }
    }
    

//-----------------------------------------------------------------------------


    /**
     * This method is a synchronous call to receive a message containing monitoring data
     * which describes the state of the cMsg domain the user is connected to.
     *
     * @param  command directive for monitoring process
     * @return response message containing monitoring information
     * @throws cMsgException
     */
    @Override
    public cMsgMessage monitor(String command) {

        cMsgMessageFull msg = new cMsgMessageFull();
        msg.setText(monitorXML);
        return msg;

    }
    

//-----------------------------------------------------------------------------


    /**
     * Method to shutdown the given clients.
     * Wildcards used to match client names with the given string.
     * No failover done here because you do not want to automatically
     * shutdown clients on the new server. It's better to have this
     * call fail.
     *
     * @param client client(s) to be shutdown
     * @param includeMe  if true, it is permissible to shutdown calling client
     * @throws cMsgException if there are communication problems with the server
     */
    @Override
    public void shutdownClients(String client, boolean includeMe) throws cMsgException {

        if (!hasShutdown) {
            throw new cMsgException("shutdown is not implemented by this subdomain");
        }

        // make sure null args are sent as blanks
        if (client == null) {
            client = new String("");
        }

        int flag = includeMe ? cMsgConstants.includeMe : 0;

        // cannot run this simultaneously with any other public method
        connectLock.lock();
        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            socketLock.lock();
            try {
                // total length of msg (not including this int) is 1st item
                domainOut.writeInt(3*4 + client.length());
                domainOut.writeInt(cMsgConstants.msgShutdownClients);
                domainOut.writeInt(flag);
                domainOut.writeInt(client.length());

                // write string
                try {
                    domainOut.write(client.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {}
            }
            finally {
                socketLock.unlock();
            }

            domainOut.flush();

        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
        finally {
            connectLock.unlock();
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to shutdown the given servers.
     * Wildcards used to match server names with the given string.
     * No failover done here because you do not want to automatically
     * shutdown servers connected to the new server. It's better to
     * have this call fail.
     *
     * @param server server(s) to be shutdown
     * @param includeMyServer if true, it is permissible to shutdown calling client's cMsg server
     * @throws cMsgException if server arg is not in the correct form (host:port),
     *                       the host is unknown, client not connected to server,
     *                       or IO error.
     */
    public void shutdownServers(String server, boolean includeMyServer) throws cMsgException {

        if (!hasShutdown) {
            throw new cMsgException("shutdown is not implemented by this subdomain");
        }

        // make sure null args are sent as blanks
        if (server == null) {
            server = new String("");
        }

        int flag = includeMyServer ? cMsgConstants.includeMyServer : 0;

        // Parse the server string to see if it's in an acceptable form.
        // It must be of the form "host:port" where host should be the
        // canonical form. If it isn't, that must be corrected here.
        server = cMsgUtilities.constructServerName(server);

        // cannot run this simultaneously with any other public method
        connectLock.lock();
        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            socketLock.lock();
            try {
                // total length of msg (not including this int) is 1st item
                domainOut.writeInt(3*4 + server.length());
                domainOut.writeInt(cMsgConstants.msgShutdownServers);
                domainOut.writeInt(flag);
                domainOut.writeInt(server.length());

                // write string
                try {
                    domainOut.write(server.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {}
            }
            finally {
                socketLock.unlock();
            }

            domainOut.flush();

        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
        finally {
            connectLock.unlock();
        }
    }




    /**
     * This method gets the host and port of the domain server from the name server.
     * It also gets information about the subdomain handler object.
     * Note to those who would make changes in the protocol, keep the first three
     * ints the same. That way the server can reliably check for mismatched versions.
     *
     * @param socket socket to server
     * @throws IOException if there are communication problems with the name server
     * @throws cMsgException if the name server's domain does not match the UDL's domain;
     *                       the client cannot be registered; the domain server cannot
     *                       open a listening socket or find a port to listen on; or
     *                       the name server cannot establish a connection to the client
     */
    void talkToNameServerFromClient(Socket socket)
            throws IOException, cMsgException {

        byte[] buf = new byte[512];

        DataInputStream  in  = new DataInputStream(new BufferedInputStream(socket.getInputStream()));
        DataOutputStream out = new DataOutputStream(new BufferedOutputStream(socket.getOutputStream()));

        out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
        out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
        out.writeInt(cMsgNetworkConstants.magicNumbers[2]);
        out.writeInt(cMsgConstants.msgConnectRequest);
        out.writeInt(cMsgConstants.version);
        out.writeInt(cMsgConstants.minorVersion);
        out.writeInt(regime);
        out.writeInt(password.length());
        out.writeInt(domain.length());
        out.writeInt(subdomain.length());
        out.writeInt(subRemainder.length());
        out.writeInt(host.length());
        out.writeInt(name.length());
        out.writeInt(UDL.length());
        out.writeInt(description.length());

        // write strings & byte array
        try {
            out.write(password.getBytes("US-ASCII"));
            out.write(domain.getBytes("US-ASCII"));
            out.write(subdomain.getBytes("US-ASCII"));
            out.write(subRemainder.getBytes("US-ASCII"));
            out.write(host.getBytes("US-ASCII"));
            out.write(name.getBytes("US-ASCII"));
            out.write(UDL.getBytes("US-ASCII"));
            out.write(description.getBytes("US-ASCII"));
        }
        catch (UnsupportedEncodingException e) {
        }

        out.flush(); // no need to be protected by socketLock

        // read acknowledgment
        int error = in.readInt();

        // if there's an error, read error string then quit
        if (error != cMsgConstants.ok) {

            // read string length
            int len = in.readInt();
            if (len > buf.length) {
                buf = new byte[len+100];
            }

            // read error string
            in.readFully(buf, 0, len);
            String err = new String(buf, 0, len, "US-ASCII");

            throw new cMsgException("Error from server: " + err);
        }

        // Since everything's OK, we expect to get:
        //   1) attributes of subdomain handler object
        //   2) domain server host & port

        in.readFully(buf,0,7);

        hasSend            = (buf[0] == (byte)1);
        hasSyncSend        = (buf[1] == (byte)1);
        hasSubscribeAndGet = (buf[2] == (byte)1);
        hasSendAndGet      = (buf[3] == (byte)1);
        hasSubscribe       = (buf[4] == (byte)1);
        hasUnsubscribe     = (buf[5] == (byte)1);
        hasShutdown        = (buf[6] == (byte)1);

        // Read ports & length of host name.
        domainServerPort    = in.readInt();
        domainServerUdpPort = in.readInt();
        int hostLength      = in.readInt();

        // read host name
        if (hostLength > buf.length) {
            buf = new byte[hostLength];
        }
        in.readFully(buf, 0, hostLength);
        domainServerHost = new String(buf, 0, hostLength, "US-ASCII");
//        System.out.println("talkToNameServerFromClient: domain server host = " + domainServerHost +
//                           ", udp port = " + domainServerUdpPort);

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("        << CL: domain server host = " + domainServerHost +
                               ", port = " + domainServerPort);
        }
    }


    /**
     * Method to parse the Universal Domain Locator (UDL) into its various components.
     * cMsg domain UDL is of the form:<p>
     *       cMsg:cMsg://&lt;host&gt;:&lt;port&gt;/&lt;subdomainType&gt;/&lt;subdomain remainder&gt;?tag=value&tag2=value2 ... <p>
     *
     * Remember that for this domain:
     * 1) port is not necessary to specify but is the name server's TCP port if connecting directly
     *    or the server's UDP port if broadcasting. Defaults used if not specified are
     *    {@link cMsgNetworkConstants#nameServerPort} if connecting directly, else
     *    {@link cMsgNetworkConstants#nameServerBroadcastPort} if broadcasting
     * 2) host can be "localhost" and may also be in dotted form (129.57.35.21)
     * 3) if domainType is cMsg, subdomainType is automatically set to cMsg if not given.
     *    if subdomainType is not cMsg, it is required
     * 4) the domain name is case insensitive as is the subdomainType
     * 5) remainder is past on to the subdomain plug-in
     * 6) client's password is in tag=value part of UDL as cmsgpassword=&lt;password&gt;
     * 7) broadcast timeout is in tag=value part of UDL as broadcastTO=&lt;time out in seconds&gt;
     * 8) the tag=value part of UDL parsed here is given by regime=low or regime=high means:
     *    - low message/data throughtput client if regime=low, meaning many clients are serviced
     *      by a single server thread and all msgs retain time order,
     *    - high message/data throughput client if regime=high, meaning each client is serviced
     *      by multiple threads to maximize throughput. Msgs are NOT guaranteed to be handled in
     *      time order
     *    - if regime is not specified (default), it is assumed to be medium, where a single thread is
     *      dedicated to a single client and msgs are guaranteed to be handled in time order
     *
     * @param udl UDL to parse
     * @return an object with all the parsed UDL information in it
     * @throws cMsgException if UDL is null, or no host given in UDL
     */
    ParsedUDL parseUDL(String udl) throws cMsgException {

        if (udl == null) {
            throw new cMsgException("invalid UDL");
        }

        // strip off the cMsg:cMsg:// to begin with
        String udlLowerCase = udl.toLowerCase();
        int index = udlLowerCase.indexOf("cmsg://");
        if (index < 0) {
            throw new cMsgException("invalid UDL");
        }
        String udlRemainder = udl.substring(index+7);

        Pattern pattern = Pattern.compile("([\\w\\.\\-]+):?(\\d+)?/?(\\w+)?/?(.*)");
        Matcher matcher = pattern.matcher(udlRemainder);

        String udlHost, udlPort, udlSubdomain, udlSubRemainder;
        int regime = cMsgConstants.regimeMedium;

        if (matcher.find()) {
            // host
            udlHost = matcher.group(1);
            // port
            udlPort = matcher.group(2);
            // subdomain
            udlSubdomain = matcher.group(3);
            // remainder
            udlSubRemainder = matcher.group(4);

            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("\nparseUDL: " +
                                   "\n  host      = " + udlHost +
                                   "\n  port      = " + udlPort +
                                   "\n  subdomain = " + udlSubdomain +
                                   "\n  remainder = " + udlSubRemainder);
            }
        }
        else {
            throw new cMsgException("invalid UDL");
        }

        // need at least host
        if (udlHost == null) {
            throw new cMsgException("invalid UDL");
        }

        // if subdomain not specified, use cMsg subdomain
        if (udlSubdomain == null) {
            udlSubdomain = "cMsg";
        }

        boolean mustBroadcast = false;
        if (udlHost.equalsIgnoreCase("broadcast") || udlHost.equals("255.255.255.255")) {
            mustBroadcast = true;
//System.out.println("set mustBroadcast to true (locally in parse method)");
        }
        // if the host is "localhost", find the actual, fully qualified  host name
        else if (udlHost.equalsIgnoreCase("localhost")) {
            try {udlHost = InetAddress.getLocalHost().getCanonicalHostName();}
            catch (UnknownHostException e) {}

            if (debug >= cMsgConstants.debugWarn) {
               System.out.println("parseUDL: name server given as \"localhost\", substituting " +
                                  udlHost);
            }
        }
        else {
            try {udlHost = InetAddress.getByName(udlHost).getCanonicalHostName();}
            catch (UnknownHostException e) {}
        }

        // get name server port or guess if it's not given
        int udlPortInt;
        if (udlPort != null && udlPort.length() > 0) {
            try {udlPortInt = Integer.parseInt(udlPort);}
            catch (NumberFormatException e) {
                if (mustBroadcast) {
                    udlPortInt = cMsgNetworkConstants.nameServerBroadcastPort;
                }
                else {
                    udlPortInt = cMsgNetworkConstants.nameServerPort;
                }
                if (debug >= cMsgConstants.debugWarn) {
                    System.out.println("parseUDL: non-integer port, guessing server port is " + udlPortInt);
                }
            }
        }
        else {
            if (mustBroadcast) {
                udlPortInt = cMsgNetworkConstants.nameServerBroadcastPort;
            }
            else {
                udlPortInt = cMsgNetworkConstants.nameServerPort;
            }
            if (debug >= cMsgConstants.debugWarn) {
                System.out.println("parseUDL: guessing name server port is " + udlPortInt);
            }
        }

        if (udlPortInt < 1024 || udlPortInt > 65535) {
            throw new cMsgException("parseUDL: illegal port number");
        }

        // any remaining UDL is ...
        if (udlSubRemainder == null) {
            udlSubRemainder = "";
        }


        // find cmsgpassword parameter if it exists
        String pswd = "";
        pattern = Pattern.compile("[\\?&]cmsgpassword=(\\w+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(udlSubRemainder);
        if (matcher.find()) {
            pswd = matcher.group(1);
//System.out.println("  cmsg password = " + pswd);
        }


        // look for ?broadcastTO=value& or &broadcastTO=value&
        int timeout=0;
        pattern = Pattern.compile("[\\?&]broadcastTO=([0-9]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(udlSubRemainder);
        if (matcher.find()) {
            try {
                timeout = 1000 * Integer.parseInt(matcher.group(1));
//System.out.println("broadcast TO = " + broadcastTimeout);
            }
            catch (NumberFormatException e) {
                // ignore error and keep value of 0
            }
        }

        
        // look for ?regime=low& or &regime=low&
        pattern = Pattern.compile("[\\?&]regime=(low|high|medium)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(udlSubRemainder);
        if (matcher.find()) {
//System.out.println("regime = medium");
            try {
                if (matcher.group(1).equalsIgnoreCase("low")) {
                    regime = cMsgConstants.regimeLow;
//System.out.println("regime = low");
                }
                else if (matcher.group(1).equalsIgnoreCase("high")) {
                    regime = cMsgConstants.regimeHigh;
//System.out.println("regime = high");
                }
                else {
                    regime = cMsgConstants.regimeMedium;
//System.out.println("regime = medium");
                }
            }
            catch (NumberFormatException e) {
                // ignore error and keep default value
            }
        }
        else {
//System.out.println("regime = medium");
        }

        // store results in a class
        return new ParsedUDL(udl, udlRemainder, udlSubdomain, udlSubRemainder,
                             pswd, udlHost, udlPortInt, timeout, regime, mustBroadcast);
    }


    /**
     * This method restores subscriptions to a new server which replaced a crashed server
     * during failover.
     *
     * @throws cMsgException if subscriptions fail
     */
    private void restoreSubscriptions() throws cMsgException {
        // New server has to be notified of all existing subscriptions.
        synchronized (subscriptions) {
            for (cMsgSubscription sub : subscriptions) {
                // Only got to do 1 resubscription for each sub/type pair.
                // Don't need to bother with each cb/userObject combo.
                resubscribe(sub.getSubject(),sub.getType());
            }
        }
        // The problem with restoring subscribeAndGets is that a thread already exists and
        // is waiting for a msg to arrive. To call subscribeAndGet again will only block
        // the thread running this method. So for now, only subscribes get re-established
        // when failing over.
    }


//-----------------------------------------------------------------------------


    /** This class simply holds information obtained from parsing a UDL. */
    class ParsedUDL {
        String  UDL;
        String  UDLremainder;
        String  subdomain;
        String  subRemainder;
        String  password;
        String  nameServerHost;
        int     nameServerPort;
        int     broadcastTimeout;
        int     regime;
        boolean mustBroadcast;
        boolean valid;

        /** Constructor. */
        ParsedUDL(String udl, boolean validUDL) {
            UDL   = udl;
            valid = validUDL;
        }

        /** Constructor. */
        ParsedUDL(String s1, String s2, String s3, String s4, String s5, String s6,
                  int i1, int i2, int i3,  boolean b1) {
            UDL              = s1;
            UDLremainder     = s2;
            subdomain        = s3;
            subRemainder     = s4;
            password         = s5;
            nameServerHost   = s6;
            nameServerPort   = i1;
            broadcastTimeout = i2;
            regime           = i3;
            mustBroadcast    = b1;
            valid            = true;
        }

        /** Take all of this object's parameters and copy to this client's members. */
        void copyToLocal() {
            cMsg.this.UDL              = UDL;
            cMsg.this.UDLremainder     = UDLremainder;
            cMsg.this.subdomain        = subdomain;
            cMsg.this.subRemainder     = subRemainder;
            cMsg.this.password         = password;
            cMsg.this.nameServerHost   = nameServerHost;
            cMsg.this.nameServerPort   = nameServerPort;
            cMsg.this.broadcastTimeout = broadcastTimeout;
            cMsg.this.regime           = regime;
            cMsg.this.mustBroadcast    = mustBroadcast;
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("Copy from stored parsed UDL to local :");
                System.out.println("  UDL              = " + UDL);
                System.out.println("  UDLremainder     = " + UDLremainder);
                System.out.println("  subdomain        = " + subdomain);
                System.out.println("  subRemainder     = " + subRemainder);
                System.out.println("  password         = " + password);
                System.out.println("  nameServerHost   = " + nameServerHost);
                System.out.println("  nameServerPort   = " + nameServerPort);
                System.out.println("  broadcastTimeout = " + broadcastTimeout);
                System.out.println("  mustBroadcast    = " + mustBroadcast);
                if (regime == cMsgConstants.regimeHigh)
                    System.out.println("  regime           = high");
                else if (regime == cMsgConstants.regimeLow)
                    System.out.println("  regime           = low");
                else
                    System.out.println("  regime           = medium");
            }
        }

        /** Clear this client's members. */
        void clearLocal() {
            cMsg.this.UDL              = null;
            cMsg.this.UDLremainder     = null;
            cMsg.this.subdomain        = null;
            cMsg.this.subRemainder     = null;
            cMsg.this.password         = null;
            cMsg.this.nameServerHost   = null;
            cMsg.this.nameServerPort   = 0;
            cMsg.this.broadcastTimeout = 0;
            cMsg.this.regime           = cMsgConstants.regimeMedium;
            cMsg.this.mustBroadcast    = false;
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Class that checks the health of the domain server by reading monitoring information.
     * If there is an IOException, server is assumed dead and a disconnect is done.
     */
    class KeepAlive extends Thread {
        /** Socket communication channel with domain server. */
        private Socket socket;

        /** Socket input stream associated with channel. */
        private DataInputStream in;

        /** Do we failover or not? */
        boolean implementFailovers;

        /** List of parsed UDL objects - one for each failover UDL. */
        private ArrayList<cMsg.ParsedUDL> failovers;

        /** Setting this to true will kill this thread. */
        private boolean killThread;

        /** Kill this thread. */
        public void killThread() {
            killThread = true;
        }

        /**
         * If reconnecting to another server as part of a failover, we must change to
         * another channel for keepAlives.
         *
         * @param socket communication socket with domain server
         * @throws IOException if channel is closed
         */
        synchronized public void changeChannels(Socket socket) throws IOException {
            this.socket = socket;
            // buffered communication streams for efficiency
            in = new DataInputStream(new BufferedInputStream(socket.getInputStream()));
        }


        /**
         * Constructor.
         *
         * @param socket communication socket with domain server
         * @throws IOException if channel is closed
         */
        public KeepAlive(Socket socket, boolean implementFailovers,
                         ArrayList<cMsg.ParsedUDL> failovers) throws IOException {
            this.socket = socket;
            this.failovers = failovers;
            this.implementFailovers = implementFailovers;

            // buffered communication streams for efficiency
            in = new DataInputStream(new BufferedInputStream(socket.getInputStream()));

            // die if no more non-daemon thds running
            setDaemon(true);
        }

        /**
         * This method is executed as a thread.
         */
        public void run() {
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("Running Client Keep Alive Thread");
            }

            boolean weGotAConnection = true;

            while (weGotAConnection) {
                try {
                    // periodically send monitor info to see if the domain server is alive
                    while (true) {
                        try {
                            // quit thread
                            if (killThread) {
                                return;
                            }
                            getMonitorInfo();
                        }
                        catch (InterruptedIOException e) {
                            //System.out.println("Interrupted Client during I/O");
                        }
                    }
                }
                catch (IOException e) {
                    connected = false;
                    //e.printStackTrace();
                }

                // if we've reach here, there's an error
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("\nKeepAlive Thread: domain server is probably dead, dis/reconnect");
                }

                // start by trying to connect to the first UDL on the list
                failoverIndex = 0;
                connectFailures = 0;
                weGotAConnection = false;
                resubscriptionsComplete = false;

                while (implementFailovers && !weGotAConnection) {
                    if (connectFailures >= failovers.size()) break;

                    // get parsed & stored UDL info
                    ParsedUDL p = failovers.get(failoverIndex++);
                    if (!p.valid) {
                        connectFailures++;
                        continue;
                    }
                    // copy info locally
                    p.copyToLocal();

                    try {
                        // connect with another server
//System.out.println("\nTrying to REconnect with UDL = " + p.UDL);
                        if (mustBroadcast) {
                            connectWithBroadcast();
                        }
                        reconnect();

                        // restore subscriptions on the new server
                        try {
                            restoreSubscriptions();
                            resubscriptionsComplete = true;
                        }
                        catch (cMsgException e) {
                            // if subscriptions fail, then we do NOT use failover server
                            try { socket.close(); disconnect(); }
                            catch (Exception e1) { }
                            throw e;
                        }

                        // we got ourselves a new server, boys
                        weGotAConnection = true;
                    }
                    catch (cMsgException e) {
                        // clear effects of p.copyToLocal()
                        p.clearLocal();
                        connectFailures++;
                    }
                }
            }

            // disconnect (sockets closed here)
//System.out.println("\nTrying running disconnect from this (KA) thread");
            disconnect();
        }

        /**
         * This method reads monitoring data from the server.
         * @throws IOException if communication error
         */
        synchronized private void getMonitorInfo() throws IOException {
            // read monitor info from server
//System.out.println("     Try reading first KA int from server");
            int len = in.readInt();
            if (len > 0) {
                // read all monitor data string
                byte[] bytes = new byte[len];
                in.readFully(bytes, 0, len);
                monitorXML = new String(bytes, 0, len, "US-ASCII");
//System.out.println("     Read KA from server");
//System.out.println("...");
            }
        }

    }

//-----------------------------------------------------------------------------


    /**
     * Class that periodically sends statistical info to the domain server.
     * The server uses this to gauge the health of this client.
     */
    class UpdateServer extends Thread {
        /** Socket output stream associated with channel. */
        private DataOutputStream out;

        /** Setting this to true will kill this thread. */
        private boolean killThread;

        /** Kill this thread. */
        public void killThread() {
            killThread = true;
        }

        /**
         * If reconnecting to another server as part of a failover, we must change to
         * another channel for keepAlives.
         *
         * @param socket communication socket with domain server
         * @throws IOException if channel is closed
         */
        synchronized public void changeSockets(Socket socket) throws IOException {
            // buffered communication streams for efficiency
            out = new DataOutputStream(new BufferedOutputStream(socket.getOutputStream()));
        }


        /**
         * Constructor.
         *
         * @param socket communication socket with domain server
         * @throws IOException if socket is closed
         */
        public UpdateServer(Socket socket) throws IOException {
            // buffered communication streams for efficiency
            out = new DataOutputStream(new BufferedOutputStream(socket.getOutputStream()));
            // die if no more non-daemon thds running
            setDaemon(true);
        }

        /**
         * This method is executed as a thread.
         */
        public void run() {
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("Running client's monitor info sending thread");
            }

            while (true) {
                try {
                    // periodically send monitor info to see if the domain server is alive
                    while (true) {
                        try {
                            // quit thread
                            if (killThread) {
                                return;
                            }

                            sendMonitorInfo();

                            // sleep for 1 second and try again
                            Thread.sleep(sleepTime);
                        }
                        catch (InterruptedException e) {
                            //System.out.println("Interrupted Client during sleep");
                        }
                        catch (InterruptedIOException e) {
                            //System.out.println("Interrupted Client during I/O");
                        }
                    }
                }
                catch (IOException e) {
                    //e.printStackTrace();
                    // if we've reach here, there's an error
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("\nUpdateServer Thread: domain server is probably dead\n");
                    }
                    break;
                }

            }
        }



        /**
         * This method gathers and sends monitoring data to the server
         * as a response to the keep alive command.
         * @throws IOException if communication error
         */
        synchronized private void sendMonitorInfo() throws IOException {

            String indent1 = "      ";
            String indent2 = "        ";

            StringBuilder xml = new StringBuilder(2048);

            synchronized (subscriptions) {

                // for each subscription of this client ...
                for (cMsgSubscription sub : subscriptions) {

                    xml.append(indent1);
                    xml.append("<subscription  subject=\"");
                    xml.append(sub.getSubject());
                    xml.append("\"  type=\"");
                    xml.append(sub.getType());
                    xml.append("\">\n");

                    // run through all callbacks
                    int num=0;
                    for (cMsgCallbackThread cbThread : sub.getCallbacks()) {
                        xml.append(indent2);
                        xml.append("<callback  id=\"");
                        xml.append(num++);
                        xml.append("\"  received=\"");
                        xml.append(cbThread.getMsgCount());
                        xml.append("\"  cueSize=\"");
                        xml.append(cbThread.getCueSize());
                        xml.append("\"/>\n");
                    }

                    xml.append(indent1);
                    xml.append("</subscription>\n");
                }
            }
//System.out.println("     TRY SENDING KA TO SERVER");
            int size = xml.length() + 4*4 + 8*7;
            out.writeInt(size);
            out.writeInt(xml.length());
            out.writeInt(1); // This is a java client (0 is for C/C++)
            out.writeInt(subscribeAndGets.size()); // pending sub&gets
            out.writeInt(sendAndGets.size());      // pending send&gets

            out.writeLong(numTcpSends);
            out.writeLong(numUdpSends);
            out.writeLong(numSyncSends);
            out.writeLong(numSendAndGets);
            out.writeLong(numSubscribeAndGets);
            out.writeLong(numSubscribes);
            out.writeLong(numUnsubscribes);

            out.write(xml.toString().getBytes("US-ASCII"));
            out.flush();
//System.out.println("     SENT KA TO SERVER");
        }

    }

}
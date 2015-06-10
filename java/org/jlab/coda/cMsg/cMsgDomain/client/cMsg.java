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
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.client;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.*;

import java.io.*;
import java.net.*;
import java.nio.channels.UnresolvedAddressException;
import java.util.*;
import java.util.concurrent.atomic.AtomicBoolean;
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

    /** The parsed form of the single UDL the client is currently connected to. */
    ParsedUDL currentParsedUDL;

    //-- FAILOVER STUFF ---------------------------------------------------------------

    /** List of parsed UDL objects - one for each failover UDL. */
    private List<ParsedUDL> failoverUdls;

    /**
     * Index into the {@link #failoverUdls} list corressponding to the UDL
     * currently being used.
     */
    private volatile byte failoverIndex;

    /**
     * If more than one viable failover UDL is given or failover to a cloud is
     * indicated, then this is true, meaning
     * if any request to the server is interrupted, that method will wait a short
     * while for the failover to complete before throwing an exception or continuing
     * on.
     */
    volatile boolean useFailovers;

    /**
     * Have all the existing subscriptions been successfully resubscribed on the
     * failover server? This member is used as a flag between different threads.
     */
    private AtomicBoolean resubscriptionsComplete = new AtomicBoolean();

    /** Does this client have a local server in a cloud to failover to? */
    private boolean haveLocalCloudServer;

    /**
     * Synchronized map of all cMsg name servers in the same cloud as this client's server.
     */
    private Map<String, ParsedUDL> cloudServers;

    //------------------------------------------------------------------------------

    /** Socket over which to send UDP multicast and receive response packets from server. */
    private MulticastSocket udpSocket;

    /** Socket over which to send messages with UDP. */
    private DatagramSocket sendUdpSocket;

    /** Packet in which to send messages with UDP. */
    private DatagramPacket sendUdpPacket;

    /** Signal to coordinate the multicasting and waiting for responses. */
    private CountDownLatch multicastResponse;

    /** Domain server's host. */
    String domainServerHost;

    /** Name server's TCP port. */
    int nameServerPort;

    /** Domain server's TCP port. */
    int domainServerPort;

    /** Domain server's UDP port for udp client sends. */
    private int domainServerUdpPort;

    /** Channel for talking to domain server. */
    Socket domainOutSocket;

    /** Socket output stream associated with domainOutChannel - sends info to server. */
    DataOutputStream domainOut;

    /** Socket for checking to see that the domain server is still alive. */
    Socket keepAliveSocket;

    /** String containing monitor data received from a cMsg server as a keep alive response. */
    public String monitorXML = "";

    /** User-supplied XML fragment to send to server
        in client data for keep alive communications. */
    private String userXML;

    /** Time in milliseconds between sending monitor data / keep alives. */
    private final int sleepTime = 2000;

    /** Thread listening for messages and responding to domain server commands. */
    cMsgClientListeningThread listeningThread;

    /** Thread for sending keep alive commands to domain server to check its health. */
    KeepAlive keepAliveThread;

    /** Thread for sending periodic monitoring info to domain server to check this client's health. */
    UpdateServer updateServerThread;

    /**
     * Collection of all of this client's message subscriptions which are
     * {@link cMsgSubscription} objects (value of map is dummy so we can use
     * concurrent hashmap). A concurrent map is needed so that:
     * <ul>
     * <li> Client listening thread will not need to grab a mutex while iterating
     *      through this set to send messages to callbacks. If the callback cue fills
     *      up, then all changes to subscriptions would be impossible.
     * <li> UpdateServer thread will not need to grab a mutex while iterating thru this set.
     *      Nothing must be able to stop the update thread or the server will think
     *      this client is dead.
     * </ul>
     * A client is either a regular client or a bridge but not both.
     * That means it does not matter that a bridge client will add namespace
     * data to the stored subscription but a regular client will not.
     */
    public ConcurrentHashMap<cMsgSubscription, String> subscriptions;

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
     * Key is receiverSubscribeId object, value is {@link cMsgSubscription} object.
     */
    ConcurrentHashMap<Integer,cMsgSubscription> subscribeAndGets;

    /**
     * Collection of all of this client's {@link #sendAndGet(org.jlab.coda.cMsg.cMsgMessage, int)}
     * calls currently in execution.
     * Key is senderToken object, value is {@link cMsgGetHelper} object.
     */
    ConcurrentHashMap<Integer,cMsgGetHelper> sendAndGets;

    /**
     * Collection of all of this client's {@link #syncSend(org.jlab.coda.cMsg.cMsgMessage, int)}
     * calls currently in execution.
     * Key is senderToken object, value is {@link cMsgGetHelper} object.
     */
    ConcurrentHashMap<Integer,cMsgGetHelper> syncSends;

    /**
     * The {@link #connect} method may only be called once until {@link #disconnect}
     * is called explicitly or the client disconnects when detecting a dead server.
     * This member keeps track of the state.
     */
    private volatile boolean mayConnect;

    /**
     * True if the UDLs given in the constructor have been parsed by the
     * {@link #connect} method. Need to do this only once.
     */
    private boolean udlsParsed;

    /**
     * True if the method {@link #disconnect} was called.
     */
    private volatile boolean disconnectCalled;

    /**
     * This lock is for controlling access to the methods of this class.
     * It is inherently more flexible than synchronizing code. The {@link #connect}
     * and {@link #disconnect} methods of this object cannot be called simultaneously
     * with each other or any other method. However, the
     * {@link #send(org.jlab.coda.cMsg.cMsgMessage)} method is
     * thread-safe and may be called simultaneously from multiple threads. The
     * {@link #syncSend(org.jlab.coda.cMsg.cMsgMessage, int)}
     * method is thread-safe with other methods but not itself
     * (since it requires a response from the server) and requires an additional lock.
     * The {@link #subscribeAndGet(String, String, int)},
     * {@link #sendAndGet(org.jlab.coda.cMsg.cMsgMessage, int)},
     * {@link #subscribe(String, String, org.jlab.coda.cMsg.cMsgCallbackInterface, Object)},
     * and {@link #unsubscribe(org.jlab.coda.cMsg.cMsgSubscriptionHandle)} methods are also
     * thread-safe but require some locking for bookkeeping purposes by means of other locks.
     */
    private final ReentrantReadWriteLock methodLock = new ReentrantReadWriteLock();

    /** Lock for calling {@link #connect} or {@link #disconnect}. */
    Lock connectLock = methodLock.writeLock();

    /** Lock for calling methods other than {@link #connect} or {@link #disconnect}. */
    Lock notConnectLock = methodLock.readLock();

    /**
     * Lock to ensure
     * {@link #subscribe(String, String, org.jlab.coda.cMsg.cMsgCallbackInterface, Object)}
     * and {@link #unsubscribe(org.jlab.coda.cMsg.cMsgSubscriptionHandle)}
     * calls are sequential.
     */
    Lock subscribeLock = new ReentrantLock();

    /** Lock to ensure that methods using the socket, write in sequence. */
    Lock socketLock = new ReentrantLock();

    /** Used to create unique id numbers associated with a specific message subject/type pair. */
    AtomicInteger uniqueId;

    /** Unique id sent by server to use in our responses. */
    int uniqueClientKey;

    /** The subdomain server object or client handler implements
     * {@link #send(org.jlab.coda.cMsg.cMsgMessage)}. */
    boolean hasSend;

    /** The subdomain server object or client handler implements
     * {@link #syncSend(org.jlab.coda.cMsg.cMsgMessage, int)}. */
    boolean hasSyncSend;

    /** The subdomain server object or client handler implements
     * {@link #subscribeAndGet(String, String, int)}. */
    boolean hasSubscribeAndGet;

    /** The subdomain server object or client handler implements
     * {@link #sendAndGet(org.jlab.coda.cMsg.cMsgMessage, int)}. */
    boolean hasSendAndGet;

    /** The subdomain server object or client handler implements
     * {@link #subscribe(String, String, org.jlab.coda.cMsg.cMsgCallbackInterface, Object)}. */
    boolean hasSubscribe;

    /** The subdomain server object or client handler implements
     * {@link #unsubscribe(org.jlab.coda.cMsg.cMsgSubscriptionHandle)}. */
    boolean hasUnsubscribe;

    /** The subdomain server object or client handler implements
     * {@link #shutdownClients(String, boolean)}. */
    boolean hasShutdown;

    // For statistics/monitoring

    /** Numbers of commands sent from client to server. */
    private long numTcpSends, numUdpSends, numSyncSends, numSendAndGets,
                 numSubscribeAndGets, numSubscribes, numUnsubscribes;

//-----------------------------------------------------------------------------

    /**
     * Constructor which does NOT automatically try to connect to the name server specified.
     * That's because the UDL, UDLremainder, name, description, and debug are passed in later
     * throught the setter methods. This object is constructed generically through class names
     * found by parsing the UDL at the top level.
     *
     * @throws cMsgException if local host name cannot be found
     */
    public cMsg() throws cMsgException {
        domain = "cMsg";

        //subscriptions    = Collections.synchronizedSet(new HashSet<cMsgSubscription>(20));
        subscriptions    = new ConcurrentHashMap<cMsgSubscription, String>(20);
        subscribeAndGets = new ConcurrentHashMap<Integer,cMsgSubscription>(20);
        sendAndGets      = new ConcurrentHashMap<Integer,cMsgGetHelper>(20);
        syncSends        = new ConcurrentHashMap<Integer,cMsgGetHelper>(10);
        uniqueId         = new AtomicInteger();
        unsubscriptions  = Collections.synchronizedMap(new HashMap<Object, cMsgSubscription>(20));
        failoverUdls     = Collections.synchronizedList(new ArrayList<ParsedUDL>(20));
        cloudServers     = Collections.synchronizedMap(new LinkedHashMap<String,ParsedUDL>(20));
        mayConnect       = true;

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
     * before throwing an exception. This method is only called when the
     * notConnect lock is held.
     */
    private boolean failoverSuccessful(boolean waitForResubscribes) {

        // If only 1 valid UDL is given by client, forget about
        // waiting for failovers to complete before throwing
        // an exception.
        if (!useFailovers) return false;
//System.out.println("failoverSuccessful: Wait for failover");

        // Check every .1 seconds for 2 seconds for a new connection
        // before giving up and throwing an exception.
        for (int i=0; i < 20; i++) {
            // release lock while waiting
            notConnectLock.unlock();
            try { Thread.sleep(100); }
            catch (InterruptedException e) {}
            notConnectLock.lock();

            if (waitForResubscribes) {
                if (connected && resubscriptionsComplete.get()) {
//System.out.println("failoverSuccessful: got new connection 1");
                    return true;
                }
            }
            else {
                if (connected) {
//System.out.println("failoverSuccessful: got new connection 2");
                    return true;
                }
            }
        }

//System.out.println("failoverSuccessful: no new connection");
        return false;
    }


    /**
     * Set the UDL of the client which may be a semicolon separated list of UDLs in this domain.
     * @param UDL UDL of client
     * @throws cMsgException if UDL is null, no beginning cmsg://, no host given, unknown host
     */
    public void setUDL(String UDL) throws cMsgException {
        if (UDL == null) {
            throw new cMsgException("UDL argument is null");
        }

        this.UDL = UDL;

        // The UDL is a semicolon separated list of UDLs, separate them and
        // store them for future use in failoverUdls. Only need to do this once.
        String[] UDLstrings = UDL.split(";");

        if (debug >= cMsgConstants.debugInfo) {
            for (int i=0; i<UDLstrings.length; i++) {
                System.out.println("UDL #" + i + " = " + UDLstrings[i]);
            }
        }

        // Parse the list of UDLs and store them locally for now.
        // Throws cMsgException if there is a bad UDL in the list.
        // Do things this way so we don't ruin "failoverUdls" if there
        // is a bad UDL in the given list.
        ArrayList<ParsedUDL> failoverList = new ArrayList<ParsedUDL>(UDLstrings.length);
        for (String udl : UDLstrings) {
            failoverList.add(parseUDL(udl));
        }

        // Rewrite official list of parsed UDLs.
        synchronized (failoverUdls) {
            failoverUdls.clear();
            for (ParsedUDL p : failoverList) {
                failoverUdls.add(p);
            }
            failoverIndex = -1;

            // If we have more than one valid UDL, we can implement waiting
            // for a successful failover before aborting commands to the server
            // that were interrupted due to server failure.
            useFailovers = failoverUdls.size() > 1 ||
                    failoverUdls.get(0).failover == cMsgConstants.failoverCloud ||
                    failoverUdls.get(0).failover == cMsgConstants.failoverCloudOnly;
        }
    }


    /**
     * Get the UDL the client is currently connected to or null if no connection.
     * @return UDL the client is currently connected to or null if no connection
     */
    public String getCurrentUDL() {
        if (currentParsedUDL == null) return null;
        return currentParsedUDL.UDL;
    }


    /**
     * Get a string of information dependent upon the argument.
     * In this domain, a cmd arg of "serverName" returns the name of the cMsg name
     * server this client is connected to in the form "IPaddress:port".
     * @return string dependent on argument's value
     */
    public String getInfo(String cmd) {
        if (cmd.equals("serverName")) {
            return domainServerHost + ":" + nameServerPort;
        }
        return null;
    }


    /**
     * Method to connect to the domain server from this client.
     * This method handles multiple UDLs,
     * but passes off the real work to {@link #connectDirect}.
     *
     * @throws cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server(s)
     */
    public void connect() throws cMsgException {

        // cannot run this simultaneously with any other connects, disconnects, or locked methods
        connectLock.lock();

        try {
            // The UDL is a semicolon separated list of UDLs, separate them and
            // store them for future use in failoverUdls. Only need to do this once.
            if (!udlsParsed) {
                setUDL(UDL);
                udlsParsed = true;
            }

            cMsgException ex;

            synchronized (failoverUdls) {

                // If we're not connected, go ahead and connect.
                // If we're connected, and already using the UDL at the head of the list, return.
                // Else, disconnect from the current server and reconnect using the first UDL.
                if (!connected) {
//System.out.println("connect: disconnected so doing a connect");
                    if (!mayConnect) {
                        // We're here because disconnect was called and localDisconnect has not
                        // been run yet (keepalive thread has not yet detected server cutoff),
                        // so mayConnect is false. Run it now instead of waiting
                        // for the keepalive thread to run it. This will kill the current keepalive
                        // thread (& other threads) and pave the way for a new connection.
//System.out.println("connect: disconnected, localDisconnect has not been run yet, so do it");
                        localDisconnect(false);
                    }
                }
                else {
                    if (currentParsedUDL != null &&
                       !currentParsedUDL.mustMulticast &&
                        currentParsedUDL.UDL.equals(failoverUdls.get(0).UDL)) {
//System.out.println("connect: we're already connected to this UDL and we're not multicasting, so return");
                        return;
                    }
//System.out.println("connect: doing a reconnect");
//System.out.print("connect: doing a reconnect, waiting for you to kill server ... ");
//                    for (int i=0; i<6; i++) {
//                        try { Thread.sleep(1000); }
//                        catch (InterruptedException e) {}
//                        System.out.println((i+1) + ", ");
//                    }
//System.out.println("\nconnect: now do localDisconnect, wait .2 sec and then continue connecting");

                    // First disconnect (keeping subscriptions),
                    // equivalent to server cutting off communications.
                    localDisconnect(false);

                    // wait until keepalive thread figures out the server is gone
                    try { Thread.sleep(200); }
                    catch (InterruptedException e) {}
                }

                failoverIndex = -1;
                
                // Go through the UDL's until one works
                do {
                    // get parsed & stored UDL info & store UDL info locally
                    currentParsedUDL = failoverUdls.get(++failoverIndex);

                    // connect using that UDL info
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("Trying to connect with UDL = " + currentParsedUDL.UDL);
                    }

                    try {
                        if (currentParsedUDL.mustMulticast) {
                            connectWithMulticast();
                        }
                        connectDirect();

                        // If connect is being called for the 2nd time (with no disconnect call in between),
                        // then, subscriptions are retained locally and must be propogated to the new
                        // server we just connected to. In this case the subscriptions hash table will not
                        // be empty. If connect is not being called for the 2nd time (ie. disconnect has
                        // been called), then all subscriptions have been erased and the following call
                        // does nothing.
                        restoreSubscriptions();

                        mayConnect = false;
//System.out.println("connect: Done!");
                        return;
                    }
                    catch (cMsgException e) {
                        currentParsedUDL = null;
                        ex = e;
                    }

                } while (failoverIndex < failoverUdls.size() - 1);
            }

            throw new cMsgException("connect: all UDLs failed", ex);
        }
        finally {
            connectLock.unlock();
        }

    }

//-----------------------------------------------------------------------------


    /**
     * Method to multicast in order to find the domain server from this client.
     * Once the server is found and returns its host and port, a direct connection
     * can be made. Only called when connectLock is held.
     *
     * @throws cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server
     */
    protected void connectWithMulticast() throws cMsgException {
        // Need a new latch for each go round - one shot deal
        multicastResponse = new CountDownLatch(1);


        //-------------------------------------------------------
        // multicast on local subnet to find cMsg server
        //-------------------------------------------------------
        DatagramPacket udpPacket;
        InetAddress multicastAddr = null;
        try {
            multicastAddr = InetAddress.getByName(cMsgNetworkConstants.cMsgMulticast);
        }
        catch (UnknownHostException e) {
            e.printStackTrace();
        }

        // create byte array for multicast
        ByteArrayOutputStream baos = new ByteArrayOutputStream(1024);
        DataOutputStream out = new DataOutputStream(baos);

        try {
            // send our magic ints
            out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
            out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
            out.writeInt(cMsgNetworkConstants.magicNumbers[2]);
            // int describing our message type: multicast is from cMsg domain client
            out.writeInt(cMsgNetworkConstants.cMsgDomainMulticast);
            out.writeInt(currentParsedUDL.password.length());
            try {out.write(currentParsedUDL.password.getBytes("US-ASCII"));}
            catch (UnsupportedEncodingException e) { }
            out.flush();
            out.close();

            // create socket to receive at anonymous port & all interfaces
            udpSocket = new MulticastSocket();
            udpSocket.setReceiveBufferSize(1024);
            udpSocket.setTimeToLive(32); // Need to get thru routers

            // create multicast packet from the byte array
            byte[] buf = baos.toByteArray();
//System.out.println("connectWithMulticast: send mcast packets to port " + nameServerUdpPort);
            udpPacket = new DatagramPacket(buf, buf.length, multicastAddr, currentParsedUDL.nameServerUdpPort);
        }
        catch (IOException e) {
            try { out.close();} catch (IOException e1) {}
            try {baos.close();} catch (IOException e1) {}
            if (udpSocket != null) udpSocket.close();
            throw new cMsgException("Cannot create multicast packet", e);
        }

        // create a thread which will receive any responses to our multicast
        UdpReceiver receiver = new UdpReceiver();
        receiver.start();

        // create a thread which will send our multicast
        Multicaster sender = new Multicaster(udpPacket);
        sender.start();

        // wait up to multicast timeout
        boolean response = false;
        if (currentParsedUDL.multicastTimeout > 0) {
            try {
                if (multicastResponse.await(currentParsedUDL.multicastTimeout, TimeUnit.MILLISECONDS)) {
                    response = true;
                }
            }
            catch (InterruptedException e) {
System.out.println("INTERRUPTING WAIT FOR MULTICAST RESPONSE, (timeout specified)");
            }
        }
        // wait forever
        else {
            try { multicastResponse.await(); response = true;}
            catch (InterruptedException e) {
System.out.println("INTERRUPTING WAIT FOR MULTICAST RESPONSE, (timeout NOT specified)");
            }
        }

        sender.interrupt();

        if (!response) {
            throw new cMsgException("No response to UDP multicast received");
        }

        // Record whether this server is local or not.
        // If server client, nothing in failoverUdls so null pointer exception in get
        if (failoverIndex < failoverUdls.size()) {
            if (currentParsedUDL != null)
                currentParsedUDL.local = cMsgUtilities.isHostLocal(currentParsedUDL.nameServerHost);
        }

//System.out.println("Got a response!, multicast part finished ...");

        return;
    }

//-----------------------------------------------------------------------------

    /**
     * This class gets any response to our UDP multicast.
     */
    class UdpReceiver extends Thread {

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

            while (true) {
                try {
                    packet.setLength(1024);
                    udpSocket.receive(packet);

                    // if packet is smaller than 6 ints  ...
                    if (packet.getLength() < 24) {
                        continue;
                    }

//System.out.println("RECEIVED MULTICAST RESPONSE PACKET !!!");
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

                    currentParsedUDL.nameServerTcpPort = cMsgUtilities.bytesToInt(buf, 12); // port to do a direct connection to
                    // udpPort is next but we'll skip over it since we don't use it
                    int hostLength = cMsgUtilities.bytesToInt(buf, 20); // host to do a direct connection to

                    if ((currentParsedUDL.nameServerTcpPort < 1024 || currentParsedUDL.nameServerTcpPort > 65535) ||
                        (hostLength < 0 || hostLength > 1024 - 24)) {
//System.out.println("  Wrong format for multicast response packet");
                        continue;
                    }

                    if (packet.getLength() != 4*6 + hostLength) {
                        continue;
                    }

                    // cMsg server host
                    try { currentParsedUDL.nameServerHost = new String(buf, 24, hostLength, "US-ASCII"); }
                    catch (UnsupportedEncodingException e) {}
//System.out.println("  Got port = " + nameServerTcpPort + ", host = " + nameServerHost);
                    break;
                }
                catch (IOException e) {
                }
            }

            multicastResponse.countDown();
        }
    }

//-----------------------------------------------------------------------------

    /**
     * This class defines a thread to multicast a UDP packet to the
     * cMsg name server every second.
     */
    class Multicaster extends Thread {

        DatagramPacket packet;

        Multicaster(DatagramPacket udpPacket) {
            packet = udpPacket;
        }

        public void run() {

            try {
                /* A slight delay here will help the main thread (calling connect)
                * to be already waiting for a response from the server when we
                * multicast to the server here (prompting that response). This
                * will help insure no responses will be lost.
                */
                Thread.sleep(100);

                while (true) {

                    try {
//System.out.println("Send multicast packet to cMsg server");
                        // send a packet over each network interface
                        Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();
                        while (enumer.hasMoreElements()) {
                            NetworkInterface ni = enumer.nextElement();
                            if (ni.isUp() && ni.supportsMulticast() && !ni.isLoopback()) {
                                udpSocket.setNetworkInterface(ni);
                                udpSocket.send(packet);
                            }
                        }
                    }
                    catch (IOException e) {
                        e.printStackTrace();
                    }

                    Thread.sleep(1000);
                }
            }
            catch (InterruptedException e) {
                // time to quit
// System.out.println("Interrupted sender");
            }
        }
    }

//-----------------------------------------------------------------------------


    /**
     * Method to make the actual connection to the name & domain servers from this client.
     * Only called by method protected with connectLock (write lock), so no mutex
     * protection needed. Only called if not already connected.
     *
     * @throws cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server
     */
    private void connectDirect() throws cMsgException {

        // connect & talk to cMsg name server to check if name is unique
        Socket nsSocket = null;
        try {
            nsSocket = new Socket(currentParsedUDL.nameServerHost, currentParsedUDL.nameServerTcpPort);
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

        connectToDomainServer(false);
    }


//-----------------------------------------------------------------------------


    /**
     * Method to reconnect to another server if the existing connection dies.
     * @throws cMsgException
     */
    private void reconnect() throws cMsgException {

        // KeepAlive & serverUpdate thread needs to keep running.
        // Keep all existing callback threads for the subscribes.

        // wakeup all syncSends - they can't be saved
        for (cMsgGetHelper helper : syncSends.values()) {
            helper.setErrorCode(cMsgConstants.errorAbort);
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

        // Empty hash tables for subAndGets, sendAndGets, and syncSends
        syncSends.clear();
        sendAndGets.clear();
        subscribeAndGets.clear();

        // close all our streams and sockets
        try {domainOut.close();}       catch (IOException e) {}
        try {domainOutSocket.close();} catch (IOException e) {}
        try {keepAliveSocket.close();} catch (IOException e) {}
        // object never created for server client
        if  (sendUdpSocket != null) sendUdpSocket.close();

         // shutdown listening thread (since socket needs to change)
        listeningThread.killThread();

        // connect & talk to cMsg name server to check if name is unique
        Socket nsSocket = null;
        try {
//System.out.println("reconnect:  try connecting to host " + nameServerHost + " and port " + nameServerTcpPort);
            nsSocket = new Socket(currentParsedUDL.nameServerHost, currentParsedUDL.nameServerTcpPort);
            // Set tcpNoDelay so no packets are delayed
            nsSocket.setTcpNoDelay(true);
            // no need to set buffer sizes
        }
        catch (UnresolvedAddressException e) {
//System.out.println("reconnect:  cannot create socket to name server, unresolved addr");
            try {if (nsSocket != null) nsSocket.close();} catch (IOException e1) {}
            throw new cMsgException("reconnect: cannot create socket to name server", e);
        }
        catch (IOException e) {
//System.out.println("reconnect:  cannot create socket to name server");
            try {if (nsSocket != null) nsSocket.close();} catch (IOException e1) {}
            throw new cMsgException("reconnect: cannot create socket to name server", e);
        }

        // get host & port to send messages & other info from name server
        try {
//System.out.println("reconnect: talk to name server");
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

        connectToDomainServer(true);

        return;
    }


//-----------------------------------------------------------------------------


    /**
     * This method does the work of making 2 socket connections to the cMsgConnectionHandler
     * (inside cmsg name server). It also creates a UDP socket for the udpSend method.
     * This method does some tricky things due to the fact that users want to connect to
     * cMsg servers through ssh tunnels.
     *
     * @param reconnecting <code>true</code> if reconnecting, else <code>false</code>
     * @throws cMsgException if cannot make connections to domain server
     */
    private void connectToDomainServer(boolean reconnecting) throws cMsgException {

        //--------------------------------------------------------------------------------------------
        // We need to do some tricky things due to the fact that people want to connect to
        // a cMsg server through SSH tunnels. The original connection to the cMsg name server is
        // not a big deal as host & port are spelled out in the UDL. However, the connection to
        // the domain server must also go through an ssh tunnel. Thus, we cannot use the
        // domainServerHost and domainServerPort returned by the nameserver since we need to use
        // some local host & port specified when creating the tunnels.
        // The strategy here is to test the following options:
        //      option 1) use host originally specified in UDL since that's the one that found
        //                the server and pair that with a domain port specified explicitly in the UDL, or
        //      option 2) use original host and name server port + 1 (default domain port), or
        //      option 3) use host & port returned by name server (no SSH tunnels used)
        // in that order.
        //--------------------------------------------------------------------------------------------

        int[] ports =    {currentParsedUDL.domainServerTcpPort,
                         (currentParsedUDL.nameServerTcpPort + 1),
                          domainServerPort};

        String[] hosts = {currentParsedUDL.nameServerHost,
                          currentParsedUDL.nameServerHost,
                          domainServerHost};

        // We can simplify the logic if we can determine if a particular option
        // means we're tunneling or not. Assuming we actually make a connection
        // for a particular option, we can draw the following conclusions:
        boolean[] isSshTunneling = new boolean[ports.length];

        InetAddress serverAddr = null;
        try { serverAddr = InetAddress.getByName(domainServerHost); }
        catch (UnknownHostException e) { }

        for (int i=0; i < ports.length; i++) {
            // assume no ssh tunneling
            isSshTunneling[i] = false;

            // multicasting bypasses tunneling
            if (currentParsedUDL.mustMulticast) {
                isSshTunneling[i] = false;
            }
            // 3rd option cannot be tunneling
            else if (i == 2) {
                isSshTunneling[i] = false;
            }
            // If the server's actual domain port is different than the one
            // we used to connect to the domain server, assume tunneling.
            else if (ports[i] != domainServerPort) {
                isSshTunneling[i] = true;
            }
            // If we can't resolve the server's host, we're probably tunneling
            // since we should not have been able to connect to the name server
            // in the first place (which we just did).
            else if (serverAddr == null) {
                isSshTunneling[i] = true;
            }
            // If the actual host the server is running on is different than
            // the one we used to connect, assume tunneling.
            else if (serverAddr != null) {
                try {
                    InetAddress addr = InetAddress.getByName(hosts[i]);
                    if (!serverAddr.getCanonicalHostName().equals(addr.getCanonicalHostName())) {
                        isSshTunneling[i] = true;
                    }
                }
                catch (UnknownHostException e) {
                    // If we can't resolve the host name specified on the UDL, we never should
                    // have been able to connect in the first place for option 1 & 2 !??
                    // Don't know what's going on here since it should never happen, so do nothing.
                    // We already took care of option 3 in previous else.
                }
            }
        }


        int index = 0;
        // if no domain server port explicitly given in UDL, skip option #1
        if (currentParsedUDL.domainServerTcpPort < 1) {
            index = 1;
        }

        // see if we can connect using 1 of the 3 possible means
        do {

            try {

                domainOutSocket = null;
                keepAliveSocket = null;

                for (int i=index; i < ports.length; i++) {
                    try {
                        //-----------------------------------------------------------------------------------
                        // Do NOT use SocketChannel objects to establish communications.  The socket obtained
                        // from a SocketChannel object has its input and output streams synchronized - making
                        // simultaneous reads and writes impossible !!!
                        //-----------------------------------------------------------------------------------
                        domainOutSocket = new Socket(hosts[i], ports[i]);
                        keepAliveSocket = new Socket(hosts[i], ports[i]);

                        // if successful, try out the new connection and make sure it works
                        index = i;
//System.out.println("connectToDomainServer: connection with method #" + (i + 1) + " to host " +
//                     hosts[i] + " and port " + ports[i]);
                        break;
                    }
                    catch (IOException e) {
                        try {if (domainOutSocket != null) domainOutSocket.close();} catch (IOException e1) {}
                        try {if (keepAliveSocket != null) keepAliveSocket.close();} catch (IOException e1) {}
//System.out.println("connectToDomainServer: failed connection with method #" + (i + 1) + " to host " +
//                    hosts[i] + " and port " + ports[i]);
                        if (debug >= cMsgConstants.debugError) {
                            e.printStackTrace();
                        }
                    }
                }

                if (domainOutSocket == null || keepAliveSocket == null) {
                    throw new cMsgException("connectToDomainServer: cannot create channels to domain server");
                }
//System.out.println("connectToDomainServer: created socket to connection handler with method #" + (index + 1));

                // create request sending/receiving (to domain) socket
                try {
                    domainOutSocket.setTcpNoDelay(true);
                    domainOutSocket.setSendBufferSize(cMsgNetworkConstants.bigBufferSize);
                    domainOut = new DataOutputStream(new BufferedOutputStream(domainOutSocket.getOutputStream(),
                                                                              cMsgNetworkConstants.bigBufferSize));
                    // send magic #s to foil port-scanning
                    domainOut.writeInt(cMsgNetworkConstants.magicNumbers[0]);
                    domainOut.writeInt(cMsgNetworkConstants.magicNumbers[1]);
                    domainOut.writeInt(cMsgNetworkConstants.magicNumbers[2]);
                    // send our server-given id
                    domainOut.writeInt(uniqueClientKey);
                    domainOut.writeInt(1);
                    domainOut.flush();
                    // Expecting one byte in return to confirm connection and make ssh port
                    // forwarding fails in a timely way if no server on the other end.
                    if (domainOutSocket.getInputStream().read() < 1) {
                        throw new IOException("connectToDomainServer; failed to create message channel to domain server");
                    }
                }
                catch (IOException e) {
                    // undo everything we've just done so far
                    try {domainOutSocket.close();} catch (IOException e1) {}

                    if (debug >= cMsgConstants.debugError) {
                        e.printStackTrace();
                    }
                    throw new cMsgException("connectToDomainServer: cannot create channel to domain server", e);
                }


                // create keepAlive socket
                DataOutputStream kaOut;
                try {
                    keepAliveSocket.setTcpNoDelay(true);
                    keepAliveSocket.setSendBufferSize(cMsgNetworkConstants.bigBufferSize);
                    if (reconnecting) {
                        if (keepAliveThread != null) keepAliveThread.changeChannels(keepAliveSocket);
                    }

                    // send magic #s to foil port-scanning
                    kaOut = new DataOutputStream(new BufferedOutputStream(keepAliveSocket.getOutputStream()));
                    kaOut.writeInt(cMsgNetworkConstants.magicNumbers[0]);
                    kaOut.writeInt(cMsgNetworkConstants.magicNumbers[1]);
                    kaOut.writeInt(cMsgNetworkConstants.magicNumbers[2]);
                    // send our server-given id
                    kaOut.writeInt(uniqueClientKey);
                    kaOut.writeInt(2);
                    kaOut.flush();
                    // Expecting one byte in return to confirm connection and make ssh port
                    // forwarding fails in a timely way if no server on the other end.
                    if (keepAliveSocket.getInputStream().read() < 1) {
                        throw new IOException("connectToDomainServer; failed to create keepalive channel to domain server");
                    }
                    if (reconnecting) {
                        // updateServer thread exists, but replace socket
                        if (updateServerThread != null) updateServerThread.changeSockets(kaOut);
                    }
                }
                catch (IOException e) {
                    // undo everything we've just done so far
                    try { domainOutSocket.close(); } catch (IOException e1) {}
                    try { keepAliveSocket.close(); } catch (IOException e1) {}

                    if (debug >= cMsgConstants.debugError) {
                        e.printStackTrace();
                    }
                    throw new cMsgException("connectToDomainServer: cannot create keepAlive channel to domain server", e);
                }
//System.out.println("connectToDomainServer: created keepalive socket to connection handler with method #" + (index + 1));


                //-----------------------------------------------------------------------------------
                // If we're tunneling, we do NOT want to create a udp socket - just ignore it,
                // but the user should never use it.
                //-----------------------------------------------------------------------------------
                if (!isSshTunneling[index]) {
                    try {
                        sendUdpSocket = new DatagramSocket();
                        // connect for speed and to keep out unwanted packets
                        sendUdpSocket.connect(serverAddr, domainServerUdpPort);
                        sendUdpSocket.setSendBufferSize(cMsgNetworkConstants.biggestUdpBufferSize);
                        sendUdpPacket = new DatagramPacket(new byte[0], 0, serverAddr, domainServerUdpPort);
                        // System.out.println("udp socket connected to host = " + domainServerHost +
                        // " and port = " + domainServerUdpPort);
                    }
                    catch (SocketException e) {
                        try {keepAliveSocket.close();} catch (IOException e1) {}
                        try {domainOutSocket.close();} catch (IOException e1) {}
                        if  (sendUdpSocket != null) sendUdpSocket.close();

                        if (debug >= cMsgConstants.debugError) {
                            e.printStackTrace();
                        }
                        throw new cMsgException("connectToDomainServer: cannot create udp socket to domain server", e);
                    }
//System.out.println("connectToDomainServer: made it past UDP stuff with method #" + (index + 1));
                }

                try {
                    // Create thread to start listening on receive end of "sending" socket
                    listeningThread = new cMsgClientListeningThread(this, domainOutSocket);
                    listeningThread.start();

                    if (!reconnecting) {
                        // Create thread to handle dead server with failover capability.
                        keepAliveThread = new KeepAlive(keepAliveSocket);
                        keepAliveThread.start();

                        // Create thread to send periodic monitor data / keep alives
                        updateServerThread = new UpdateServer(kaOut);
                        updateServerThread.start();
                    }
                }
                catch (IOException e) {
                    e.printStackTrace();  //To change body of catch statement use File | Settings | File Templates.
                    if (listeningThread != null) listeningThread.killThread();
                    if (!reconnecting) {
                        if (keepAliveThread    != null) keepAliveThread.killThread();
                        if (updateServerThread != null) updateServerThread.killThread();
                    }
                }

            }
            catch (cMsgException e) {
                // if the last option failed, propagate exception
                if (index >= ports.length - 1) {
                    throw new cMsgException(e);
                }
                continue;
            }

            // if we managed to get here, the connections were successful
            domainServerPort = ports[index];
            domainServerHost = hosts[index];
            break;

        } while (index++ < ports.length - 1);

//System.out.println("connectToDomainServer: connected = true");
        connected = true;
    }


//-----------------------------------------------------------------------------


    /**
     * This method does nothing.
     * @param timeout ignored
     */
    public void flush(int timeout) {}


//-----------------------------------------------------------------------------


    /**
     * Method to close the connection to the domain server. This method results in this object
     * becoming functionally useless.
     */
    public void disconnect() {
        disconnect(true);
    }


//-----------------------------------------------------------------------------


    /**
     * Method to close the connection to the domain server. This method results in this object
     * becoming functionally useless.
     *
     * @param userCalledDisconnect set disconnectCalled to this value
     */
    private void disconnect(boolean userCalledDisconnect) {
        // The user calling disconnect means all msg flow to callbacks is stopped.
        // This only comes into play once connect (on this same object) is called
        // again and this forces the user to call start() again to get msgs flowing.
        if (userCalledDisconnect) {
            stop();
        }

        // cannot run this simultaneously with any other public method
        connectLock.lock();

        try {
//System.out.println("disconnect: CONNECTED = FALSE");
            connected = false;

            // if we already called disconnect, wait for it to take place in keepalive thread
            if (disconnectCalled) {
//System.out.println("disconnect: already called");
                return;
            }
            disconnectCalled = userCalledDisconnect;

            // Tell the server to disconnect from this client.
            // Our keepalive thread will detect the break and
            // shut everything down on this end.
            socketLock.lock();
            try {
//System.out.println("disconnect: send stuff to server");
                domainOut.writeInt(4);
                domainOut.writeInt(cMsgConstants.msgDisconnectRequest);
                domainOut.flush();
            }
            catch (IOException e) {
System.out.println("disconnect: IO error");
            }
            finally {
                socketLock.unlock();
            }
        }
        finally {
            connectLock.unlock();
        }

        // If connect is called too quickly after disconnect, the server
        // will not be finished with the disconnection and will think the
        // client is still connected. So put a small time delay here to
        // prevent that problem.
        try { Thread.sleep(50); }
        catch (InterruptedException e) {}

//System.out.println("\nReached end of disconnect method");
    }


    /**
     * Method to clean up after the connection to the domain server is closed/broken
     * and no failover is being done.
     * @param removeSubscriptions remove all subscriptions and stop all existing callback threads
     */
    private void localDisconnect(boolean removeSubscriptions) {
        // cannot run this simultaneously with any other public method
        connectLock.lock();

        try {
            // Stop keep alive threads
            // NOTE: It may be the keepAlive thread that is calling this method
            // and killing itself in the next lines.
            // In this case, it exits AFTER running this method.
            keepAliveThread.killThread();
//System.out.println("localDisconnect: interrupting KA thd, id = " + keepAliveThread);
            keepAliveThread.interrupt();
            updateServerThread.killThread();
//System.out.println("localDisconnect: interrupting updateServer thd, id = " + updateServerThread);
            updateServerThread.interrupt();

            // close all our streams and sockets
            try {domainOut.close();}       catch (IOException e) {}
            try {domainOutSocket.close();} catch (IOException e) {}
            try {keepAliveSocket.close();} catch (IOException e) {}
            // object never created for server client
            if  (sendUdpSocket != null) sendUdpSocket.close();

            // stop listening and client communication thread & close channel
            listeningThread.killThread();

            // don't stop callback threads if disconnect not called by user
            if (removeSubscriptions) {
                // stop all callback threads
                synchronized (subscriptions) {
                    for (cMsgSubscription sub : subscriptions.keySet()) {
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

                    // Keep hash tables with subscriptions if server died.
                    // Get rid of that stuff if the user explicitly called disconnect.
                    subscriptions.clear();
                    unsubscriptions.clear();
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

            // Empty hash tables for subAndGets, sendAndGets, and syncSends
            subscribeAndGets.clear();
            sendAndGets.clear();
            syncSends.clear();

            disconnectCalled = false;
            //resubscriptionsComplete = false;

            // allow connect to work again
            mayConnect = true;
        }
        finally {
            connectLock.unlock();
        }

//System.out.println("\nReached end of disconnect method");
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @param message {@inheritDoc}
     * @throws cMsgException if there are communication problems with the server;
     *                       subject and/or type is null; message is too big for
     *                       UDP packet size if doing UDP send
     */
    public void send(cMsgMessage message) throws cMsgException {

        if (!hasSend) {
            throw new cMsgException("send is not implemented by this subdomain");
        }

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

        // Payload stuff. Include the name of this sender as part of a history
        // of senders in the cMsgSenderHistory payload field. Note that msg may
        // be set not to record any history or add any history.
        long now = System.currentTimeMillis();
        String payloadTxt;
        int payloadLen = 0;
        if (message.noHistoryAdditions()) {
            payloadTxt = message.getPayloadText();
        }
        else {
            payloadTxt = message.addHistoryToPayloadText(name, host, now);
        }
        if (payloadTxt != null) {
            payloadLen = payloadTxt.length();
        }

        int binaryLength = message.getByteArrayLength();

        // go here to try the send again
        while (true) {

            // cannot run this simultaneously with connect, reconnect, or disconnect
            notConnectLock.lock();
            
            try {
                if (!connected) {
                    throw new IOException("not connected to server");
                }
                int size = 4 * 15 + subject.length() + type.length() + payloadLen +
                                  textLen + binaryLength;

                // protect communications over socket
                socketLock.lock();

                try {
//System.out.println("send: writing " + size + " bytes in send msg");
                    // total length of msg (not including this int) is 1st item
                    domainOut.writeInt(size);
                    domainOut.writeInt(cMsgConstants.msgSendRequest);
                    domainOut.writeInt(0); // reserved for future use
                    domainOut.writeInt(message.getUserInt());
                    domainOut.writeInt(message.getSysMsgId());
                    domainOut.writeInt(message.getSenderToken());
                    domainOut.writeInt(message.getInfo());

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

//System.out.println("send: flush");
                domainOut.flush();
//System.out.println("send: flush done");
                numTcpSends++;
            }
            catch (IOException e) {
//System.out.println("send: error, try failover");
                // wait awhile for possible failover
                if (failoverSuccessful(false)) {
                    continue;
                }
                throw new cMsgException(e.getMessage());
            }
            finally {
//System.out.println("send: try unlocking mutexes");
                notConnectLock.unlock();
            }

            break;
        }
//System.out.println("send: DONE");

    }


//-----------------------------------------------------------------------------


    /**
     * Method to send a message to the domain server over UDP for further distribution.<p>
     * NOTE: IP datagrams can hold up to 65535 bytes of payload.
     * Ethernet frames cannot carry more than 1,500 bytes of payload. So when a UDP packet is
     * transported over an Ethernet LAN, it may take several Ethernet packets to do it.
     * IP datagrams larger than the MTU of the link layer are fragmented and sent in multiple
     * link data units (packets). UDP datagrams can be up to 65535 bytes long (header + payload).
     * Each UDP datagram maps onto one IP datagram, which is broken into as many fragments as
     * needed by the link layer at each link along the route. Neither IP nor UDP support
     * retransmission. A dropped or damaged fragment will cause the entire IP datagram,
     * and consequently the UDP datagram, to be dropped. UDP does support reassembly of the
     * datagram, however, through the underlying IP datagram support. In IPv6, it is possible
     * to have jumbograms, datagrams with more than 65,535 bytes.
     *
     * @param message message to send
     * @throws cMsgException if there are communication problems with the server;
     *                       using ssh tunneling to server;
     *                       subject and/or type is null; message is too big for
     *                       UDP packet size if doing UDP send
     */
    private void udpSend(cMsgMessage message) throws cMsgException {

        // check to see if udp operational (might not be if using ssh tunneling to cmsg server)
        if (sendUdpPacket == null) {
            throw new cMsgException("udp sends not allowed when ssh tunneling to cmsg server");
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
        // be set not to record any history or add any history.
        long now = System.currentTimeMillis();
        String payloadTxt;
        int payloadLen = 0;
        if (message.noHistoryAdditions()) {
            payloadTxt = message.getPayloadText();
        }
        else {
            payloadTxt = message.addHistoryToPayloadText(name, host, now);
        }
        if (payloadTxt != null) {
            payloadLen = payloadTxt.length();
        }

        int binaryLength = message.getByteArrayLength();

        // total length of msg (not including first int which is this size,
        // the magic #s, & local port)
        int totalLength = 4*15 + subject.length() + type.length() + payloadLen +
                                 textLen + binaryLength;
        if (totalLength > cMsgNetworkConstants.biggestUdpBufferSize) {
            throw new cMsgException("Too big a message for UDP to send");
        }

        // create byte array for multicast
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

                // For the server to identify which client is sending this UDP msg, send the
                // messaging-sending socket TCP port here as ID that the server can recognize.
                // Since the server can get the sending host of this UDP packet, and since
                // it already knows the ports on the TCP sockets already made, it can put
                // these 2 pieces of info together to uniquely identify the client sending
                // this msg.
                out.writeInt(domainOutSocket.getLocalPort()); // TCP port of domainOutSocket

                out.writeInt(totalLength); // total length of msg (not including this int)
                out.writeInt(cMsgConstants.msgSendRequest);
                out.writeInt(0); // reserved for future use
                out.writeInt(message.getUserInt());
                out.writeInt(message.getSysMsgId());
                out.writeInt(message.getSenderToken());
                out.writeInt(message.getInfo());

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
     * {@inheritDoc}
     *
     * @param message {@inheritDoc}
     * @param timeout ignored
     * @return 0
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
        // be set not to record any history or add any history.
        long now = System.currentTimeMillis();
        String payloadTxt;
        int payloadLen = 0;
        if (message.noHistoryAdditions()) {
            payloadTxt = message.getPayloadText();
        }
        else {
            payloadTxt = message.addHistoryToPayloadText(name, host, now);
        }
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
     * {@inheritDoc}
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
                    for (cMsgSubscription sub : subscriptions.keySet()) {
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
                    subscriptions.put(newSub,"");
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
                    // "subscriptions", "unsubscriptions" are concurrent
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
     * {@inheritDoc}
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
                synchronized (subscriptions) {
                    if (sub.numberOfCallbacks() > 1) {
                        // kill callback thread
                        cbThread.dieNow(false);
                        // remove this callback from the set
                        sub.getCallbacks().remove(cbThread);
                        numUnsubscribes++;
                        return;
                    }
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
                synchronized (subscriptions) {
                    cbThread.dieNow(false);
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
                for (cMsgSubscription sub : subscriptions.keySet()) {
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
     * {@inheritDoc}
     *
     * NOTE: Disconnecting when one thread is in the waiting part of a subscribeAndGet may cause that
     * thread to block forever. It is best to always use a timeout with subscribeAndGet so the thread
     * is assured of eventually resuming execution.
     *
     * @param subject {@inheritDoc}
     * @param type    {@inheritDoc}
     * @param timeout {@inheritDoc}
     * @return {@inheritDoc}
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

            // First generate a unique id for the msg.
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
                domainOut.writeInt(id);
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
            domainOut.writeInt(id);
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
     * {@inheritDoc}
     *
     * NOTE: Disconnecting when one thread is in the waiting part of a sendAndGet may cause that
     * thread to block forever. It is best to always use a timeout with sendAndGet so the thread
     * is assured of eventually resuming execution.
     *
     * @param message {@inheritDoc}
     * @param timeout {@inheritDoc}
     * @return {@inheritDoc}
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
        // be set not to record any history or add any history.
        long now = System.currentTimeMillis();
        String payloadTxt;
        int payloadLen = 0;
        if (message.noHistoryAdditions()) {
            payloadTxt = message.getPayloadText();
        }
        else {
            payloadTxt = message.addHistoryToPayloadText(name, host, now);
        }
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

        // If msg arrived, server has removed subscription from his records.
        // Client listening thread has also removed subscription from client's
        // records (subscribeAndGets HashSet).

        // Return null (without an  exception) if local server has no subscriber
        // to send the sendAndGet-message to.
        if (helper.getMessage().isNullGetServerResponse()) {
            return null;
        }

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
     * {@inheritDoc}<p>
     * Any non-null value of the argument is used as an XML fragment
     * which is appended to the keep alive packet this client sends
     * to the server. A value of "" will remove it (null is just ignored).
     *
     * @param  command xml fragment appended to the keep alive packet
     * @return {@inheritDoc}
     */
    @Override
    public cMsgMessage monitor(String command) {

        if (command != null) {
            userXML = command;
        }

        cMsgMessageFull msg = new cMsgMessageFull();
        msg.setText(monitorXML);
        return msg;

    }
    

//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     * Wildcards used to match client names with the given string where "*" means
     * any or no characters, "?" means exactly 1 character, and "#" means
     * 1 or no positive integer.
     * No failover done here because you do not want to automatically
     * shutdown clients on the new server. It's better to have this
     * call fail.
     *
     * @param client {@inheritDoc}
     * @param includeMe  {@inheritDoc}
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
     * {@inheritDoc}
     * Wildcards used to match server names with the given string.
     * No failover done here because you do not want to automatically
     * shutdown servers connected to the new server. It's better to
     * have this call fail.
     *
     * @param server {@inheritDoc}
     * @param includeMyServer {@inheritDoc}
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
            server = "";
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
        out.writeInt(currentParsedUDL.regime);
        out.writeInt(currentParsedUDL.password.length());
        out.writeInt(domain.length());
        out.writeInt(currentParsedUDL.subdomain.length());
        out.writeInt(currentParsedUDL.subRemainder.length());
        out.writeInt(host.length());
        out.writeInt(name.length());
        out.writeInt(currentParsedUDL.UDL.length());
        out.writeInt(description.length());

        // write strings & byte array
        try {
            out.write(currentParsedUDL.password.getBytes("US-ASCII"));
            out.write(domain.getBytes("US-ASCII"));
            out.write(currentParsedUDL.subdomain.getBytes("US-ASCII"));
            out.write(currentParsedUDL.subRemainder.getBytes("US-ASCII"));
            out.write(host.getBytes("US-ASCII"));
            out.write(name.getBytes("US-ASCII"));
            out.write(currentParsedUDL.UDL.getBytes("US-ASCII"));
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
//System.out.println("ERROR STRING = " + err);

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

        // Read unique id number assigned to us by server
        uniqueClientKey     = in.readInt();

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
     *
     *   <b>cMsg:cMsg://&lt;host&gt;:&lt;port&gt;/&lt;subdomainType&gt;/&lt;subdomain remainder&gt;?tag=value&tag2=value2 ...</b><p>
     *
     * <ul>
     * <li>port is not necessary to specify but is the name server's TCP port if connecting directly
     *    or the server's UDP port if multicasting. Defaults used if not specified are
     *    {@link cMsgNetworkConstants#nameServerTcpPort} if connecting directly, else
     *    {@link cMsgNetworkConstants#nameServerUdpPort} if multicasting<p>
     * <li>host can be "localhost" and may also be in dotted form (129.57.35.21), but may not contain a colon.
     *     It can also be "multicast"<p>
     * <li>if domainType is cMsg, subdomainType is automatically set to cMsg if not given.
     *    if subdomainType is not cMsg, it is required<p>
     * <li>the domain name is case insensitive as is the subdomainType<p>
     * <li>remainder is passed on to the subdomain plug-in<p>
     * <li>client's password is in tag=value part of UDL as cmsgpassword=&lt;password&gt;<p>
     * <li>domain server port is in tag=value part of UDL as domainPort=&lt;port&gt;<p>
     * <li>multicast timeout is in tag=value part of UDL as multicastTO=&lt;time out in seconds&gt;<p>
     * <li>the tag=value part of UDL parsed here is given by regime=low or regime=high means:<p>
     *   <ul>
     *   <li>low message/data throughput client if regime=low, meaning many clients are serviced
     *       by a single server thread and all msgs retain time order<p>
     *   <li>high message/data throughput client if regime=high, meaning each client is serviced
     *       by multiple threads to maximize throughput. Msgs are NOT guaranteed to be handled in
     *       time order<p>
     *   <li>if regime is not specified (default), it is assumed to be medium, where a single thread is
     *       dedicated to a single client and msgs are guaranteed to be handled in time order<p>
     *   </ul>
     * </ul>
     *
     * @param udl UDL to parse
     * @return an object with all the parsed UDL information in it
     * @throws cMsgException if UDL is null, no beginning cmsg://, no host given, unknown host
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

        Pattern pattern = Pattern.compile("([^:/]+):?(\\d+)?/?(\\w+)?/?(.*)");
        Matcher matcher = pattern.matcher(udlRemainder);

        String udlHost, udlPort, udlSubdomain, udlSubRemainder;

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

        boolean isLocal = false;
        boolean mustMulticast = false;
        if (udlHost.equalsIgnoreCase("multicast") ||
            udlHost.equals(cMsgNetworkConstants.cMsgMulticast)) {
            mustMulticast = true;
            // "isLocal" must be determined after connection, set it false for now
//System.out.println("set mustMulticast to true (locally in parse method)");
        }
        // if the host is "localhost", find the actual, fully qualified  host name
        else if (udlHost.equalsIgnoreCase("localhost")) {
            try {udlHost = InetAddress.getLocalHost().getCanonicalHostName();}
            catch (UnknownHostException e) {
                throw new cMsgException("cannot find localhost", e);
            }
            isLocal = true;

            if (debug >= cMsgConstants.debugWarn) {
               System.out.println("parseUDL: name server given as \"localhost\", substituting " +
                                  udlHost);
            }
        }
//        else {
//            try {udlHost = InetAddress.getByName(udlHost).getCanonicalHostName();}
//            catch (UnknownHostException e) {
//                throw new cMsgException("unknown host", e);
//            }
//            isLocal = cMsgUtilities.isHostLocal(udlHost);
//        }
        else {
            try {InetAddress.getByName(udlHost);}
            catch (UnknownHostException e) {
                throw new cMsgException("unknown host", e);
            }
            isLocal = cMsgUtilities.isHostLocal(udlHost);
        }

        // get name server port or guess if it's not given
        int tcpPort, udpPort;
        if (udlPort != null && udlPort.length() > 0) {
            try {
                int udlPortInt = Integer.parseInt(udlPort);
                if (udlPortInt < 1024 || udlPortInt > 65535) {
                    throw new cMsgException("parseUDL: illegal port number");
                }

                if (mustMulticast) {
                    udpPort = udlPortInt;
                    tcpPort = cMsgNetworkConstants.nameServerTcpPort;
                }
                else {
                    udpPort = cMsgNetworkConstants.nameServerUdpPort;
                    tcpPort = udlPortInt;
                }
            }
            // should never happen
            catch (NumberFormatException e) {
                udpPort = cMsgNetworkConstants.nameServerUdpPort;
                tcpPort = cMsgNetworkConstants.nameServerTcpPort;
            }
        }
        else {
            udpPort = cMsgNetworkConstants.nameServerUdpPort;
            tcpPort = cMsgNetworkConstants.nameServerTcpPort;
            if (debug >= cMsgConstants.debugWarn) {
                System.out.println("parseUDL: guessing name server TCP port = " + tcpPort +
                 ", UDP port = " + udpPort);
            }
        }

        // any remaining UDL is ...
        if (udlSubRemainder == null) {
            udlSubRemainder = "";
        }

        // don't allow multiple, identical tags
        int counter = 0;

        // find cmsgpassword parameter if it exists
        String pswd = "";
        pattern = Pattern.compile("[\\?&]cmsgpassword=([^&]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(udlSubRemainder);
        while (matcher.find()) {
            pswd = matcher.group(1);
            counter++;
//System.out.println("  cmsg password = " + pswd);
        }

        if (counter > 1) {
            throw new cMsgException("parseUDL: only 1 password allowed");
        }

        // look for multicastTO=value
        counter=0;
        int timeout=0;
        pattern = Pattern.compile("[\\?&]multicastTO=([^&]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(udlSubRemainder);
        while (matcher.find()) {
//System.out.println("multicast timeout = " + matcher.group(1));
            try {
                timeout = 1000 * Integer.parseInt(matcher.group(1));
                if (timeout < 0) {
                    throw new cMsgException("parseUDL: multicast timeout must be integer >= 0");
                }
                counter++;
            }
            catch (NumberFormatException e) {
                throw new cMsgException("parseUDL: multicast timeout must be integer >= 0");
            }
        }

        if (counter > 1) {
            throw new cMsgException("parseUDL: only 1 multicast timeout allowed");
        }

        // look for domainPort=value
        counter=0;
        int domainPort=0;
        pattern = Pattern.compile("[\\?&]domainPort=([^&]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(udlSubRemainder);
        while (matcher.find()) {
//System.out.println("domain server port = " + matcher.group(1));
            try {
                domainPort = Integer.parseInt(matcher.group(1));
                if (domainPort < 1024 || domainPort > 65535) {
                    throw new cMsgException("parseUDL: domain server illegal port number");
                }
                counter++;
            }
            catch (NumberFormatException e) {
                throw new cMsgException("parseUDL: domain server port must be integer > 1023 and < 65536");
            }
        }

        if (counter > 1) {
            throw new cMsgException("parseUDL: only 1 domain server port allowed");
        }

        // look for regime=low, medium, or high
        counter=0;
        int regime = cMsgConstants.regimeMedium;
        pattern = Pattern.compile("[\\?&]regime=([^&]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(udlSubRemainder);
        while (matcher.find()) {
//System.out.println("regime = " + matcher.group(1));
            if (matcher.group(1).equalsIgnoreCase("low")) {
                regime = cMsgConstants.regimeLow;
            }
            else if (matcher.group(1).equalsIgnoreCase("high")) {
                regime = cMsgConstants.regimeHigh;
            }
            else if (matcher.group(1).equalsIgnoreCase("medium")) {
                regime = cMsgConstants.regimeMedium;
            }
            else {
                throw new cMsgException("parseUDL: regime must be low, medium or high");
            }
            counter++;
        }

        if (counter > 1) {
            throw new cMsgException("parseUDL: only 1 regime value allowed");
        }

        // look for failover=cloud, cloudonly, any
        counter=0;
        int failover = cMsgConstants.failoverAny;
        pattern = Pattern.compile("[\\?&]failover=([^&]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(udlSubRemainder);
        while (matcher.find()) {
//System.out.println("failover = " + matcher.group(1));
            if (matcher.group(1).equalsIgnoreCase("cloud")) {
                failover = cMsgConstants.failoverCloud;
            }
            else if (matcher.group(1).equalsIgnoreCase("cloudonly")) {
                failover = cMsgConstants.failoverCloudOnly;
            }
            else if (matcher.group(1).equalsIgnoreCase("any")) {
                failover = cMsgConstants.failoverAny;
            }
            else {
                throw new cMsgException("parseUDL: failover must be any, cloud or cloudonly");
            }
            counter++;
        }

        if (counter > 1) {
            throw new cMsgException("parseUDL: only 1 failover value allowed");
        }

        // look for cloud=local, localnow, any
        counter=0;
        int cloud = cMsgConstants.cloudAny;
        pattern = Pattern.compile("[\\?&]cloud=([^&]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(udlSubRemainder);
        while (matcher.find()) {
//System.out.println("cloud = " + matcher.group(1));
            if (matcher.group(1).equalsIgnoreCase("local")) {
                cloud = cMsgConstants.cloudLocal;
            }
            else if (matcher.group(1).equalsIgnoreCase("any")) {
                failover = cMsgConstants.cloudAny;
            }
            else {
                throw new cMsgException("parseUDL: cloud must be any or local");
            }
            counter++;
        }

        if (counter > 1) {
            throw new cMsgException("parseUDL: only 1 cloud value allowed");
        }

        // store results in a class
        return new ParsedUDL(udl, udlRemainder, udlSubdomain, udlSubRemainder,
                             pswd, udlHost, tcpPort, domainPort, udpPort, timeout,
                             regime, failover, cloud, mustMulticast, isLocal);
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
            for (cMsgSubscription sub : subscriptions.keySet()) {
                // Only got to do 1 resubscription for each sub/type pair.
                // Don't need to bother with each cb/userObject combo.
                resubscribe(sub.getSubject(),sub.getType());
            }
        }
        resubscriptionsComplete.set(true);
        // The problem with restoring subscribeAndGets is that a thread already exists and
        // is waiting for a msg to arrive. To call subscribeAndGet again will only block
        // the thread running this method. So for now, only subscribes get re-established
        // when failing over.
    }


//-----------------------------------------------------------------------------


    /** This class simply holds information obtained from parsing a UDL or
     *  information about an available cMsg server in a server cloud. */
    class ParsedUDL {
        // used only for cloud server spec
        String  serverName;

        // all used in parsed UDL, some in cloud server spec
        /**
         * The Uniform Domain Locator which tells the location of a domain. It is of the
         * form cMsg:&lt;domainType&gt;://&lt;domain dependent remainder&gt;
         */
        String  UDL;
        /** String containing the remainder part of the UDL. */
        String  UDLremainder;
        /** Subdomain being used. */
        String  subdomain;
        /** Subdomain remainder part of the UDL. */
        String  subRemainder;
        /** Optional password included in UDL for connection to server requiring one. */
        String  password;
        /** Name server's host. */
        String  nameServerHost;
        /** Name server's TCP port. */
        int     nameServerTcpPort;
        /** Name server's domain server TCP port. */
        int     domainServerTcpPort;
        /** Name server's UDP port. */
        int     nameServerUdpPort;
        /** Timeout in milliseconds to wait for server to respond to multicasts. */
        int     multicastTimeout;
        /**
         * What throughput do we expect from this client? Values may be one of:
         * {@link cMsgConstants#regimeHigh}, {@link cMsgConstants#regimeMedium},
         * {@link cMsgConstants#regimeLow}.
         */
        int     regime;
        /**
         * Manner in which to failover. Set to {@link cMsgConstants#failoverAny} if
         * failover of client can go to any of the UDLs given in {@link cMsg#connect} (default).
         * Set to {@link cMsgConstants#failoverCloud} if failover of client will go to
         * servers in the cloud first, and if none are available, then go to any of
         * the UDLs given in connect. Set to {@link cMsgConstants#failoverCloudOnly}
         * if failover of client will only go to servers in the cloud.
         */
        int     failover;
        /**
         * Manner in which to failover to a cloud. Set to {@link cMsgConstants#cloudAny}
         * if failover of client to a server in the cloud will go to any of the cloud servers
         * (default). Set to {@link cMsgConstants#cloudLocal} if failover of client to a server
         * in the cloud will go to a local cloud server first before others are considered.
         */
        int     cloud;
        /**
         * True if the host given by the UDL is \"multicast\"
         * or the string {@link cMsgNetworkConstants#cMsgMulticast}.
         * In this case we must multicast to find the server, else false.
         */
        boolean mustMulticast;
        /** Is this client connected to a local server? */
        boolean local;



        /**
         * Constructor for storing info about cloud server and used in
         *  failing over to cloud server.
         *
         * @param name name of cloud server (host:port)
         * @param pswrd password to connect to cloud server
         * @param host host cloud server is running on
         * @param tcpPort TCP port cloud server is listening on
         * @param udpPort UDP multicasting port cloud server is listening on
         * @param isLocal is cloud server running on localhost
         */
        ParsedUDL(String name, String pswrd, String host,
                  int tcpPort, int udpPort,  boolean isLocal) {
            serverName          = name;
            password            = pswrd == null ? "" : pswrd;
            nameServerHost      = host;
            nameServerTcpPort   = tcpPort;
            nameServerUdpPort   = udpPort;
            local               = isLocal;
        }

        /** Constructor used in parsing UDL. */
        ParsedUDL(String s1, String s2, String s3, String s4, String s5, String s6,
                  int i1, int i2, int i3, int i4, int i5, int i6, int i7, boolean b1, boolean b2) {
            UDL                 = s1;
            UDLremainder        = s2;
            subdomain           = s3;
            subRemainder        = s4;
            password            = s5;
            nameServerHost      = s6;
            nameServerTcpPort   = i1;
            domainServerTcpPort = i2;
            nameServerUdpPort   = i3;
            multicastTimeout    = i4;
            regime              = i5;
            failover            = i6;
            cloud               = i7;
            mustMulticast       = b1;
            local               = b2;
        }


        /** Take all of this object's parameters and copy to this client's members. */
        public String toString() {
            StringBuilder sb = new StringBuilder(1024);

            sb.append("Copy from stored parsed UDL to local :");
            sb.append("  UDL                 = "); sb.append(UDL);
            sb.append("  UDLremainder        = "); sb.append(UDLremainder);
            sb.append("  subdomain           = "); sb.append(subdomain);
            sb.append("  subRemainder        = "); sb.append(subRemainder);
            sb.append("  password            = "); sb.append(password);
            sb.append("  nameServerHost      = "); sb.append(nameServerHost);
            sb.append("  nameServerTcpPort   = "); sb.append(nameServerTcpPort);
            sb.append("  domainServerTcpPort = "); sb.append(domainServerTcpPort);
            sb.append("  nameServerUdpPort   = "); sb.append(nameServerUdpPort);
            sb.append("  multicastTimeout    = "); sb.append(multicastTimeout);
            sb.append("  mustMulticast       = "); sb.append(mustMulticast);
            sb.append("  isLocal             = "); sb.append(local);

            if (regime == cMsgConstants.regimeHigh)
                sb.append("  regime              = high");
            else if (regime == cMsgConstants.regimeLow)
                sb.append("  regime              = low");
            else
                sb.append("  regime              = medium");

            if (failover == cMsgConstants.failoverAny)
                sb.append("  failover            = any");
            else if (failover == cMsgConstants.failoverCloud)
                sb.append("  failover            = cloud");
            else
                sb.append("  failover            = cloud only");

            if (cloud == cMsgConstants.cloudAny)
                sb.append("  cloud               = any");
            else if (cloud == cMsgConstants.cloudLocal)
                sb.append("  cloud               = local");
            else
                sb.append("  cloud               = local now");

            return sb.toString();
        }

    }


//-----------------------------------------------------------------------------


    /**
     * Class that checks the health of the domain server by reading monitoring information.
     * If there is an IOException, server is assumed dead and a disconnect is done.
     */
    class KeepAlive extends Thread {

        /** Socket input stream associated with channel. */
        private DataInputStream in;

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
            // buffered communication streams for efficiency
            in = new DataInputStream(new BufferedInputStream(socket.getInputStream()));
        }


        /**
         * Constructor.
         *
         * @param socket communication socket with domain server
         * @throws IOException if channel is closed
         */
        public KeepAlive(Socket socket) throws IOException {
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
                System.out.println("********* Running Client Keep Alive Thread ************");
            }

            boolean weGotAConnection = true;

            top:
            while (weGotAConnection) {
                try {
                    // periodically send monitor info to see if the domain server is alive
                    while (true) {
                        try {
                            // quit thread
                            if (killThread) {
//System.out.println("keepAliveThread" + this + ": been told to die, return");
                                return;
                            }
                            getMonitorInfo();
                        }
                        catch (InterruptedIOException e) {
//System.out.println("keepAliveThread: interrupted IO with cMsg server, try reading monitor info again");
                        }
                    }
                }
                catch (IOException e) { }
//System.out.println("keepAliveThread" + this + ": IO Exception with cMsg server, try grabbing connectLock");
//System.out.println("keepAliveThread" + this + ": wait for 3 seconds");
//                try { Thread.sleep(3000); }
//                catch (InterruptedException e) {
//System.out.println("keepAliveThread" + this + ": interrupted, return");
//                    return;
//                }

                // Putting this lock around "getMonitorInfo" would not be good for
                // performance or even feasible (would require nonblocking socket).
                // Thus there is a small time window in which an IO error occurs and
                // a user can simultaneously call "connect". If that call succeeds,
                // the following code will really mess things up.
                connectLock.lock();
//System.out.println("keepAliveThread" + this + ": got past connectLock");
//System.out.println("keepAliveThread" + this + ": interrupted = " + this.isInterrupted());

                try {
                    if (this.isInterrupted()) {
//System.out.println("keepAliveThread" + this + ": interrupted = " + this.isInterrupted() + ", quit thread");
                        return;
                    }
                    mayConnect = false;  // set true end of localDisconnect
//System.out.println("KA: CONNECTED = FALSE");
                    connected  = false;  // also set false in beginning of disconnect
                    weGotAConnection = false;
//System.out.println("keepAliveThread: lost connection with cMsg server (IOException)");

                    // if the user called "disconnect" or only 1 UDL, do NOT do any failovers
                    if (disconnectCalled || !useFailovers) {
                        if (debug >= cMsgConstants.debugError) {
                            System.out.println("\nkeepAliveThread: user called disconnect and/or server terminated connection");
                        }
                        // Only get rid of subscriptions & end callbacks if disconnect called.
                        // If server is gone (event if we have no failovers), it's still possible
                        // for the user to call "connect" again and retain his subscriptions.
                        localDisconnect(disconnectCalled);
                        return;
                    }

//System.out.println("keepAliveThread: implement failovers");
                    // at this point we're trying to failover ...
                    boolean noMoreCloudServers = false;
                    // if no servers in cloud ...
                    if (cloudServers.size() < 1) {
                        noMoreCloudServers = true;
                    }
                    resubscriptionsComplete.set(false);
//                    if (currentParsedUDL == null) {
//                        System.out.println("keepAliveThread: currentParsedUDL = null !!!");
//                    }

                    while (!weGotAConnection && currentParsedUDL != null) {

                        // If NOT we're failing over to the cloud first, skip this part
                        if (currentParsedUDL.failover != cMsgConstants.failoverCloud &&
                            currentParsedUDL.failover != cMsgConstants.failoverCloudOnly) {
                            break;
                        }
//System.out.println("keepAliveThread: trying to failover to cloud member");

                        // If we want to failover locally, but there is no local cloud server,
                        // or there are no cloud servers of any kind ...
                        if ((currentParsedUDL.cloud == cMsgConstants.cloudLocal && !haveLocalCloudServer) ||
                                noMoreCloudServers) {
//System.out.println("keepAliveThread: No cloud members to failover to (else not the desired local ones)");
                            // try the next UDL
                            if (currentParsedUDL.failover == cMsgConstants.failoverCloud) {
//System.out.println("keepAliveThread: so go to next UDL");
                                break;
                            }
                            // if we must failover to cloud only, disconnect
                            else {
//System.out.println("keepAliveThread: so just disconnect");
                                try { localDisconnect(disconnectCalled); }
                                catch (Exception e) { }
                                return;
                            }
                        }

                        // name of server we were just connected to
                        String sName = currentParsedUDL.nameServerHost + ":" + currentParsedUDL.nameServerTcpPort;
                        // look through list of cloud servers
                        for (Map.Entry<String,ParsedUDL> entry : cloudServers.entrySet()) {
                            String n = entry.getKey();
                            ParsedUDL pUdl = entry.getValue();
                            // If we were connected to one of the cloud servers
                            // (which just failed), go to next one.
                            if (n.equals(sName)) {
                                continue;
                            }

                            // if we can failover to anything or this cloud server is local
                            if ((currentParsedUDL.cloud == cMsgConstants.cloudAny) || pUdl.local) {
                                // Construct our own "parsed" UDL using server's name and
                                // current values of other parameters.

                                // Be careful with the password which, I believe, is the
                                // only server-dependent part of "subRemainder" and must
                                // be explicitly substituted for.
                                String newSubRemainder = currentParsedUDL.subRemainder;
                                // if current UDL has a password in remainder ...
                                if (currentParsedUDL.password.length() > 0) {
                                    Pattern pattern = Pattern.compile("([\\?&])cmsgpassword=([^&]+)", Pattern.CASE_INSENSITIVE);
                                    Matcher matcher = pattern.matcher(currentParsedUDL.subRemainder);

                                    if (pUdl.password.length() > 0) {
//System.out.println("Replacing old pswd with new one");
                                        newSubRemainder = matcher.replaceFirst("$1cmsgpassword=" + pUdl.password);
                                    }
                                    else {
//System.out.println("Eliminating pswd");
                                        newSubRemainder = matcher.replaceFirst("");
                                    }
                                }
                                // else if existing UDL has no password, put one on end if necessary
                                else if (pUdl.password.length() > 0) {
//System.out.println("No cmsgpassword= in udl, CONCAT");
                                    if (currentParsedUDL.subRemainder.contains("?")) {
                                        newSubRemainder = currentParsedUDL.subRemainder.concat("&cmsgpassword="+pUdl.password);
                                    }
                                    else {
                                        newSubRemainder = currentParsedUDL.subRemainder.concat("?cmsgpassword="+pUdl.password);
                                    }
                                }

                                currentParsedUDL.UDL = "cMsg://"+ n + "/" + currentParsedUDL.subdomain + "/" + newSubRemainder;
//System.out.println("keepAliveThread: Construct new UDL as:\n" + UDL);
                                currentParsedUDL.subRemainder      = newSubRemainder;
                                currentParsedUDL.password          = pUdl.password;
                                currentParsedUDL.nameServerHost    = pUdl.nameServerHost;
                                currentParsedUDL.nameServerTcpPort = pUdl.nameServerTcpPort;
                                currentParsedUDL.mustMulticast     = false;

                                try {
                                    // connect with server
                                    connectToServer();
                                    // we got ourselves a new server, boys
                                    weGotAConnection = true;
                                    continue top;
                                }
                                catch (cMsgException e) { }
                            }
                        }
                        // Went thru list of cloud servers with nothing to show for it,
                        // so try next UDL in list
                        break;
                    }

                    // If we're here then we did NOT failover to a server in a cloud

                    synchronized (failoverUdls) {
                        // remember which UDL has just failed
                        int failedFailoverIndex = failoverIndex;
//System.out.println("keepAliveThread: current failover index = " + failoverIndex);

                        // Since we're NOT failing over to a cloud, go to next UDL
                        // Start by trying to connect to the first UDL on the list that is
                        // different than the current UDL.
                        if (failedFailoverIndex != 0) {
                            failoverIndex = 0;
//System.out.println("keepAliveThread: set failover index = " + failoverIndex);
                        }
                        else {
                            failoverIndex = 1;
//System.out.println("keepAliveThread: set failover index = " + failoverIndex);
                        }

                        while (!weGotAConnection) {

                            if (failoverIndex >= failoverUdls.size()) {
//System.out.println("keepAliveThread: ran out of UDLs to try");
                                break;
                            }

                            // skip over UDL that failed
                            if (failoverIndex == failedFailoverIndex) {
//System.out.println("keepAliveThread: skip over UDL that just failed");
                                failoverIndex++;
                                continue;
                            }

                            // get parsed & stored UDL info & copy locally
//System.out.println("keepAliveThread: use failover index = " + failoverIndex);
                            currentParsedUDL = failoverUdls.get(failoverIndex);

                            try {
                                // connect with another server
                                connectToServer();
                                // we got ourselves a new server, boys
                                weGotAConnection = true;
                            }
                            catch (cMsgException e) {
//System.out.println("keepAliveThread: FAILED with index = " + failoverIndex);
                                failoverIndex++;
                            }
                        }
                    }
                }
                finally {
                    connectLock.unlock();
                }
            }

            // disconnect (sockets closed here)
//System.out.println("keepAliveThread: run localDisconnect from this thread");
            currentParsedUDL = null;
            localDisconnect(disconnectCalled);
        }




        /**
         * Try to connect to new server.
         * @throws cMsgException if cannot connect or restore subscriptions
         */
        private void connectToServer() throws cMsgException {
                // connect with another server

                // No multicast is ever done if failing over to cloud server
                // since all cloud servers' info contains real host name and
                // TCP port only.
//System.out.println("keepAliveThread: try reconnect, mustMulticast = " + mustMulticast);
                if (currentParsedUDL.mustMulticast) {
//System.out.println("keepAliveThread: try connect w/ multicast");
                    connectWithMulticast();
                }
//System.out.println("keepAliveThread: try reconnect");
                reconnect();
//System.out.println("keepAliveThread: reconnect worked");

                // restore subscriptions on the new server
                try {
                    restoreSubscriptions();
                }
                catch (cMsgException e) {
//System.out.println("keepAliveThread: restoring subscriptions failed");
                    // if subscriptions fail, then we do NOT use failover server
                    try { disconnect(); }
                    catch (Exception e1) { }
                    throw e;
                }
        }


        /**
         * This method reads monitoring data from the server.
         * @throws IOException if communication error
         */
        synchronized private void getMonitorInfo() throws IOException {
            // read monitor info from server
//System.out.println("  getMonitorInfo: Try reading first KA int from server, isInterrupted = " +
//                    Thread.currentThread().isInterrupted());
            byte[] bytes = new byte[4096];
            int len = in.readInt();
if (len > 200000)
System.out.println("\n  111111 Try to allocate " + len + " bytes for getMonitorInfo() 111111\n");
//System.out.println("  getMonitorInfo: len xml = " + len);
            if (len > 0) {
                // read all monitor data string
                if (len > bytes.length) bytes = new byte[len];
                in.readFully(bytes, 0, len);
                monitorXML = new String(bytes, 0, len, "US-ASCII");
//System.out.println("  getMonitorInfo:  read KA from server (len = " + monitorXML.length() + "):");
//System.out.println(monitorXML);
            }

            int itemsToFollow = in.readInt();
if (itemsToFollow > 2000)
System.out.println("\n  121212 " + itemsToFollow + " items to follow 121212\n");

            if (itemsToFollow > 0) {

                // read info about servers in the cloud (for failing over to cloud members)
                synchronized(cloudServers) {
                    cloudServers.clear();
                    haveLocalCloudServer = false;
                    boolean isLocal;
                    String s, name, passwd=null;
                    int tcpPort, udpPort, hlen, plen;

                    int numServers = in.readInt();
if (numServers > 2000)
System.out.println("\n  131313 " + numServers + " number of servers 131313\n");
                    if (numServers > 0) {
                        for (int i=0; i<numServers; i++) {
                            s = "";
                            isLocal = false;

                            tcpPort = in.readInt();
if (tcpPort > 50000)
System.out.println("\n  141414 Read tcpPort = " + tcpPort + " bytes for getMonitorInfo() 141414\n");
                            udpPort = in.readInt();
if (udpPort > 50000)
System.out.println("\n  151515 Read udpPort = " + udpPort + " bytes for getMonitorInfo() 151515\n");
                            hlen    = in.readInt(); // host string len
if (hlen > 2000) {
    System.out.println("\n  222222 Try to allocate " + hlen + " bytes for getMonitorInfo() 222222\n");
    System.out.println("  len = " + len + ", items to follow = " + itemsToFollow);
    System.out.println("  xml = \n" + monitorXML);
}
                            plen    = in.readInt(); // password string len
if (plen > 2000)
System.out.println("\n  333333 Try to allocate " + plen + " bytes for getMonitorInfo() 333333\n");

//System.out.println("  getMonitorInfo: cloud server => tcpPort = " +
//                                    Integer.toHexString(tcpPort) +
//                                    ", udpPort =" + Integer.toHexString(udpPort) +
//                                    ", hlen = " + Integer.toHexString(hlen) +
//                                    ", plen = " + Integer.toHexString(plen));
                            if (hlen > 0) {
                                if (hlen > bytes.length) bytes = new byte[hlen];
                                in.readFully(bytes, 0, hlen);
                                s = new String(bytes, 0, hlen, "US-ASCII");
                                isLocal = cMsgUtilities.isHostLocal(s);
                                haveLocalCloudServer |= isLocal;
                            }
                            if (plen > 0) {
                                if (plen > bytes.length) bytes = new byte[plen];
                                in.readFully(bytes, 0, plen);
                                passwd = new String(bytes, 0, plen, "US-ASCII");
                            }
                            name = s+":"+tcpPort;
//System.out.println("  getMonitorInfo: cloud server => tcpPort = " +tcpPort +
//                   ", udpPort = " + udpPort + ", host = " + s + ", passwd = " + passwd);
                            ParsedUDL p = new ParsedUDL(name, passwd, s, tcpPort, udpPort, isLocal);
                            cloudServers.put(name,p);
                        }
                    }
                }
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

        private boolean init = true;

        private long lastTime;

        /** Kill this thread. */
        public void killThread() {
            killThread = true;
        }

        /**
         * If reconnecting to another server as part of a failover, we must change to
         * another channel for keepAlives.
         *
         * @param out output datastream for communication with domain server
         * @throws IOException if channel is closed
         */
        synchronized public void changeSockets(DataOutputStream out) throws IOException {
            // buffered communication streams for efficiency
            this.out = out;
        }


        /**
         * Constructor.
         *
         * @param out output datastream for communication with domain server
         * @throws IOException if socket is closed
         */
        public UpdateServer(DataOutputStream out) throws IOException {
            // buffered communication streams for efficiency
            this.out = out;
            // die if no more non-daemon thds running
            setDaemon(true);
        }

        /**
         * This method is executed as a thread.
         */
        public void run() {
//            if (debug >= cMsgConstants.debugInfo) {
                //System.out.println("UpdateServer" + this + ": Running thread");
//                System.out.println("----------- Running Client Update Server Thread -----------");
//                System.out.println("UpdateServer: Running thread");
//            }

            // periodically send monitor info to see if the domain server is alive
            while (true) {
                try {
                    // quit thread
                    if (killThread) {
                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("UpdateServer: EXITING\n");
                        }
                        return;
                    }

                    long now = System.currentTimeMillis();
                    if (!init) {
                        long deltaT = now - lastTime;
                        // if over 3 seconds since last sent update, print error message
//                        if (deltaT > 3000) {
//                            System.out.println("WARNING: " + (deltaT/1000) + " seconds since update last sent to server, now time is" +
//                            (new Date()));
//                        }
                    }
                    else {
                        init = false;
                    }

                    // sleep for 1 second
                    Thread.sleep(sleepTime);
                    lastTime = now;

//System.out.println("UpdateServer: send monitor info to server");
                    sendMonitorInfo();
                }
                catch (InterruptedException e) {
//System.out.println("UpdateServer" + this + ": interrupted thread during sleep, return");
                    return;
                }
                catch (InterruptedIOException e) {
//System.out.println("UpdateServer\" + this + \": interrupted thread during I/O, return");
                    return;
                }
                catch (IOException e) {
//System.out.println("UpdateServer\" + this + \": I/O error, return, our name = " + cMsg.this.getName());
                    return;
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

            // for each subscription of this client ...
            for (cMsgSubscription sub : subscriptions.keySet()) {
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
                    xml.append(cbThread.getQueueSize());
                    xml.append("\"/>\n");
                }

                xml.append(indent1);
                xml.append("</subscription>\n");
            }

            if (userXML != null && userXML.length() > 0) {
                xml.append(userXML);
            }

            int size = xml.length() + 4*4 + 8*7;
//System.out.println("     sendMonitorInfo: TRY SENDING KA TO SERVER, xml len = " + xml.length() + ", size = " + size);
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
//System.out.println("     sendMonitorInfo: SENT KA TO SERVER");
        }

    }

}
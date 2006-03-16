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
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.util.*;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.ConcurrentHashMap;
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

    /**
     * A string of the form name:nameServerHost:nameserverPort.
     * This is a variable of convenience so it does not have to
     * be calculated for each send.
     */
    String creator;

    /** Subdomain being used. */
    private String subdomain;

    /** Subdomain remainder part of the UDL. */
    private String subRemainder;

    /** Optional password included in UDL for connection to server requiring one. */
    private String password;

    //-- FAILOVER STUFF ---------------------------------------------------------------

    /** List of parsed UDL objects - one for each failover UDL. */
    private ArrayList<ParsedUDL> failovers;

    /** An array of UDLs given by user to failover to if connection to server fails. */
    private String failoverUDLs[];

    /** Number of failures to connect to the given array of UDLs. */
    private int connectFailures;

    /**
     * Index into the {@link #failoverUDLs} array corressponding to the UDL
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

    /** Server channel (contains socket). */
    ServerSocketChannel serverChannel;

    /** Name server's host. */
    String nameServerHost;

    /** Name server's port. */
    int nameServerPort;

    /** Domain server's host. */
    String domainServerHost;

    /** Domain server's port. */
    int domainServerPort;

    /** Channel for talking to domain server. */
    SocketChannel domainOutChannel;
    /** Channel for receiving from domain server. */
    SocketChannel domainInChannel;
    /** Socket input stream associated with domainInChannel - gets info from server. */
    DataInputStream  domainIn;
    /** Socket output stream associated with domainOutChannel - sends info to server. */
    DataOutputStream domainOut;

    /** Channel for checking to see that the domain server is still alive. */
    SocketChannel keepAliveChannel;

    /** Thread listening for TCP connections and responding to domain server commands. */
    cMsgClientListeningThread listeningThread;

    /** Thread for sending keep alive commands to domain server to check its health. */
    KeepAlive keepAliveThread;

    /**
     * Collection of all of this client's message subscriptions which are
     * {@link cMsgSubscription} objects. This set is synchronized. A client is either
     * a regular client or a bridge but not both. That means it does not matter that
     * a bridge client will add namespace data to the stored subscription but a regular
     * client will not.
     */
    public Set<cMsgSubscription> subscriptions;

    /**
     * Collection of all of this client's {@link #subscribeAndGet} calls currently in execution.
     * SubscribeAndGets are very similar to subscriptions and can be thought of as
     * one-shot subscriptions.
     * Key is receiverSubscribeId object, value is {@link cMsgGetHelper} object.
     */
    ConcurrentHashMap<Integer,cMsgGetHelper> subscribeAndGets;

    /**
     * Collection of all of this client's {@link #sendAndGet} calls currently in execution.
     * Key is senderToken object, value is {@link cMsgGetHelper} object.
     */
    ConcurrentHashMap<Integer,cMsgGetHelper> sendAndGets;


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

    /**
     * Lock to ensure all calls requiring return communication
     * (e.g. {@link #syncSend}) are sequential.
     */
    Lock returnCommunicationLock = new ReentrantLock();

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
    int debug = cMsgConstants.debugInfo;

//-----------------------------------------------------------------------------

    /**
     * Constructor which does NOT automatically try to connect to the name server specified.
     *
     * @throws cMsgException if local host name cannot be found
     */
    public cMsg() throws cMsgException {
        domain = "cMsg";

        subscriptions    = Collections.synchronizedSet(new HashSet<cMsgSubscription>(20));
        subscribeAndGets = new ConcurrentHashMap<Integer,cMsgGetHelper>(20);
        sendAndGets      = new ConcurrentHashMap<Integer,cMsgGetHelper>(20);
        uniqueId         = new AtomicInteger();

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

System.out.println("IN failoverSuccessful");
        // If only 1 valid UDL is given by client, forget about
        // waiting for failovers to complete before throwing
        // an exception.
        if (!useFailoverWaiting) return false;

        // Check every .1 seconds for 3 seconds for a new connection
        // before giving up and throwing an exception.
        for (int i=0; i < 30; i++) {
System.out.println("wait " + (i+1));
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
     * but passes off the real work to {@link #connectImpl}.
     *
     * @throws cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server(s)
     */
    public void connect() throws cMsgException {

        // If the UDL is a semicolon separated list of UDLs, separate them and
        // store them for future use in failovers.
        failoverUDLs = UDL.split(";");

        // If there's only 1 UDL ...
        if (failoverUDLs.length < 2) {
 System.out.println("Only 1 UDL = " + UDL);
            // parse UDL
            ParsedUDL p = parseUDL(failoverUDLs[0]);
            // store locally
            p.copyToLocal();
            // connect using that UDL
            connectImpl();
            return;
        }

        // else if there's a bunch of UDL's
        for (String s : failoverUDLs) {
            System.out.println("More than one UDL = " + s);
        }

        // parse the list of UDLs and store them
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
                System.out.println("  Bad UDL marked as invalid");
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
System.out.println("\nTrying to connect with UDL = " + p.UDL);

            try {
                connectImpl();
                return;
            }
            catch (cMsgException e) {
System.out.println(e.getMessage());
                // clear effects of p.copyToLocal()
                p.clearLocal();
                connectFailures++;
            }
        } while (connectFailures < failovers.size());

        throw new cMsgException("connect: all UDLs failed");
    }

//-----------------------------------------------------------------------------


    /**
     * Method to make the actual connection to the domain server from this client.
     *
     * @throws cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server
     */
    private void connectImpl() throws cMsgException {

        // UDL has already been parsed and stored in this object's members
        creator = name + ":" + nameServerHost + ":" + nameServerPort;

        // cannot run this simultaneously with any other public method
        connectLock.lock();
        try {
            if (connected) return;

            // read env variable for starting port number
            try {
                String env = System.getenv("CMSG_CLIENT_PORT");
                if (env != null) {
                    startingPort = Integer.parseInt(env);
                }
            }
            catch (NumberFormatException ex) {
            }

            // port #'s < 1024 are reserved
            if (startingPort < 1024) {
                startingPort = cMsgNetworkConstants.clientServerStartingPort;
            }

            // At this point, find a port to bind to. If that isn't possible, throw
            // an exception.
            try {
                serverChannel = ServerSocketChannel.open();
            }
            catch (IOException ex) {
                ex.printStackTrace();
                throw new cMsgException("connect: cannot open a listening socket");
            }

            port = startingPort;
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
                        throw new cMsgException("connect: cannot find port to listen on");
                    }
                }
            }
System.out.println("  Listening on port " + port);
            // launch thread and start listening on receive socket
            listeningThread = new cMsgClientListeningThread(this, serverChannel);
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

            // connect & talk to cMsg name server to check if name is unique
            SocketChannel channel = null;
            try {
System.out.println("        << CL: open socket to  " + nameServerHost + ":" + nameServerPort);
                channel = SocketChannel.open(new InetSocketAddress(nameServerHost, nameServerPort));
                // set socket options
                Socket socket = channel.socket();
                // Set tcpNoDelay so no packets are delayed
                socket.setTcpNoDelay(true);
                // no need to set buffer sizes
            }
            catch (IOException e) {
                // undo everything we've just done
                listeningThread.killThread();
                try {if (channel != null) channel.close();}
                catch (IOException e1) {}

                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot create channel to name server");
            }

            // get host & port to send messages & other info from name server
            try {
                talkToNameServerFromClient(channel);
            }
            catch (IOException e) {
                // undo everything we've just done
                listeningThread.killThread();
                try {channel.close();}
                catch (IOException e1) {}

                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot talk to name server");
            }

            // done talking to server
            try {
                channel.close();
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("connect: cannot close channel to name server, continue on");
                    e.printStackTrace();
                }
            }

            // create request response reading (from domain) channel
            try {
                domainInChannel = SocketChannel.open(new InetSocketAddress(domainServerHost, domainServerPort));
                // buffered communication streams for efficiency
                Socket socket = domainInChannel.socket();
                socket.setTcpNoDelay(true);
                socket.setReceiveBufferSize(2048);
                domainIn = new DataInputStream(new BufferedInputStream(socket.getInputStream(), 2048));
            }
            catch (IOException e) {
                // undo everything we've just done
                listeningThread.killThread();
                try {channel.close();}
                catch (IOException e1) {}
                try {if (domainInChannel != null) domainInChannel.close();}
                catch (IOException e1) {}

                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot create channel to domain server");
            }

            // create keepAlive socket
            try {
                keepAliveChannel = SocketChannel.open(new InetSocketAddress(domainServerHost, domainServerPort));
                Socket socket = keepAliveChannel.socket();
                socket.setTcpNoDelay(true);

                // Create thread to send periodic keep alives and handle dead server
                // and with failover capability.
                keepAliveThread = new KeepAlive(keepAliveChannel, useFailoverWaiting, failovers);
                keepAliveThread.start();
            }
            catch (IOException e) {
                // undo everything we've just done so far
                listeningThread.killThread();
                try { channel.close(); }
                catch (IOException e1) {}
                try { domainInChannel.close(); }
                catch (IOException e1) {}
                try { if (keepAliveChannel != null) keepAliveChannel.close(); }
                catch (IOException e1) {}
                if (keepAliveThread != null) keepAliveThread.killThread();

                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot create keepAlive channel to domain server");
            }

            // create request sending (to domain) channel (This takes longest so do last)
            try {
                domainOutChannel = SocketChannel.open(new InetSocketAddress(domainServerHost, domainServerPort));
                // buffered communication streams for efficiency
                Socket socket = domainOutChannel.socket();
                socket.setTcpNoDelay(true);
                socket.setSendBufferSize(65535);
                domainOut = new DataOutputStream(new BufferedOutputStream(socket.getOutputStream(), 65536));
            }
            catch (IOException e) {
                // undo everything we've just done so far
                listeningThread.killThread();
                try { channel.close(); }
                catch (IOException e1) {}
                try { domainInChannel.close(); }
                catch (IOException e1) {}
                try { keepAliveChannel.close(); }
                catch (IOException e1) {}
                keepAliveThread.killThread();
                try {if (domainOutChannel != null) domainOutChannel.close();}
                catch (IOException e1) {}

                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot create channel to domain server");
            }

            connected = true;
        }
        finally {
            connectLock.unlock();
        }

System.out.println("        << CL: done connecting to  " + nameServerHost + ":" + nameServerPort);
        return;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to force cMsg client to send pending communications with domain server.
     * In the cMsg domain implementation, this method does nothing.
     */
    public void flush() {
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
            // Stop keep alive thread & close channel so when domain server
            // shuts down, we don't detect it's dead and make a fuss.
            //
            // NOTE: It may be the keepAlive thread that is calling this method.
            // In this case, it exits after running this method.
            keepAliveThread.killThread();
            keepAliveThread.interrupt();

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

            // give server a chance to shutdown
            try { Thread.sleep(100); }
            catch (InterruptedException e) {}

            // stop listening and client communication thread & close channel
            listeningThread.killThread();

            // stop all callback threads
            for (cMsgSubscription sub : subscriptions) {
                // run through all callbacks
                for (cMsgCallbackThread cbThread : sub.getCallbacks()) {
                    // Tell the callback thread(s) to wakeup and die
                    cbThread.dieNow();
                }
            }

            // wakeup all subscribeAndGets
            for (cMsgGetHelper helper : subscribeAndGets.values()) {
                helper.message = null;
                synchronized (helper) {
                    helper.notify();
                }
            }

            // wakeup all sendAndGets
            for (cMsgGetHelper helper : sendAndGets.values()) {
                helper.message = null;
                synchronized (helper) {
                    helper.notify();
                }
            }
        }
        finally {
            connectLock.unlock();
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to reconnect to another server if the existing connection dies.
     */
    private void reconnect() throws cMsgException {

        System.out.println("\nRECONNECT:");

        // cannot run this simultaneously with any other public method
        connectLock.lock();

        try {
            //connected = false;

            // KeepAlive & listening threads needs to keep running.
            // Keep all existing callback threads for the subscribes.

            // wakeup all subscribeAndGets - they can't be saved
            for (cMsgGetHelper helper : subscribeAndGets.values()) {
                helper.message   = null;
                helper.errorCode = cMsgConstants.errorServerDied;
                synchronized (helper) {
                    helper.notify();
                }
            }

            // wakeup all existing sendAndGets - they can't be saved
            for (cMsgGetHelper helper : sendAndGets.values()) {
                helper.message   = null;
                helper.errorCode = cMsgConstants.errorServerDied;
                synchronized (helper) {
                    helper.notify();
                }
            }

            // shutdown extra listening threads
            listeningThread.killClientHandlerThreads();

            // connect & talk to cMsg name server to check if name is unique
            SocketChannel channel = null;
            try {
//System.out.println("     open socket to nameserver " + nameServerHost + ":" + nameServerPort);
                channel = SocketChannel.open(new InetSocketAddress(nameServerHost, nameServerPort));
                // set socket options
                Socket socket = channel.socket();
                // Set tcpNoDelay so no packets are delayed
                socket.setTcpNoDelay(true);
                // no need to set buffer sizes
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("reconnect: cannot create channel to name server");
            }

            // get host & port to send messages & other info from name server
            try {
//System.out.println("     talk to nameserver");
                talkToNameServerFromClient(channel);
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("reconnect: cannot talk to name server");
            }

            // done talking to server
            try {
//System.out.println("     close channel to nameserver");
                channel.close();
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("reconnect: cannot close channel to name server, continue on");
                    e.printStackTrace();
                }
            }

            // create request response reading (from domain) channel
            try {
//System.out.println("     open return channel from domain server");
                domainInChannel = SocketChannel.open(new InetSocketAddress(domainServerHost, domainServerPort));
                // buffered communication streams for efficiency
                Socket socket = domainInChannel.socket();
                socket.setTcpNoDelay(true);
                socket.setReceiveBufferSize(2048);
                domainIn = new DataInputStream(new BufferedInputStream(socket.getInputStream(), 2048));
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("reconnect: cannot create channel to domain server");
            }

            // create keepAlive socket
            try {
//System.out.println("     open keep alive channel to domain server");
                keepAliveChannel = SocketChannel.open(new InetSocketAddress(domainServerHost, domainServerPort));
                Socket socket = keepAliveChannel.socket();
                socket.setTcpNoDelay(true);

                // update keepAlive thread with new channel
                keepAliveThread.changeChannels(keepAliveChannel);
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("reconnect: cannot create keepAlive channel to domain server");
            }

            // create request sending (to domain) channel (This takes longest so do last)
            try {
System.out.println("     open request channel to domain server");
                domainOutChannel = SocketChannel.open(new InetSocketAddress(domainServerHost, domainServerPort));
                // buffered communication streams for efficiency
                Socket socket = domainOutChannel.socket();
                socket.setTcpNoDelay(true);
                socket.setSendBufferSize(65535);
                domainOut = new DataOutputStream(new BufferedOutputStream(socket.getOutputStream(), 65536));
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("reconnect: cannot create channel to domain server");
            }

            connected = true;
        }
        finally {
            connectLock.unlock();
        }

System.out.println("     done reconnecting to  " + nameServerHost + ":" + nameServerPort);
        return;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to send a message to the domain server for further distribution.
     *
     * @param message message to send
     * @throws cMsgException if there are communication problems with the server;
     *                       subject and/or type is null
     */
    public void send(cMsgMessage message) throws cMsgException {

        if (!hasSend) {
            throw new cMsgException("send is not implemented by this subdomain");
        }

        String subject = message.getSubject();
        String type    = message.getType();
        String text    = message.getText();

        // check message fields first
        if (subject == null || type == null) {
            throw new cMsgException("message subject and/or type is null");
        }

        if (text == null) {
            text = "";
        }

        // creator (this sender's name:nsHost:nsPort if msg created here)
        String msgCreator = message.getCreator();
        if (msgCreator == null) msgCreator = creator;

        int binaryLength = message.getByteArrayLength();

        // go here to try the send again
        tryagain:
        while (true) {

            try {
                // cannot run this simultaneously with connect, reconnect, or disconnect
                notConnectLock.lock();
                // protect communicatons over socket
                socketLock.lock();
                try {
                    if (!connected) {
                        throw new IOException("not connected to server");
                    }

                    // total length of msg (not including this int) is 1st item
                    domainOut.writeInt(4 * 15 + subject.length() + type.length() + msgCreator.length() +
                                       text.length() + binaryLength);
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
                    domainOut.writeInt(msgCreator.length());
                    domainOut.writeInt(text.length());
                    domainOut.writeInt(binaryLength);

                    // write strings & byte array
                    try {
                        domainOut.write(subject.getBytes("US-ASCII"));
                        domainOut.write(type.getBytes("US-ASCII"));
                        domainOut.write(msgCreator.getBytes("US-ASCII"));
                        domainOut.write(text.getBytes("US-ASCII"));
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
                finally {
                    socketLock.unlock();
                    notConnectLock.unlock();
                }

            }
            catch (IOException e) {
                // wait awhile for possible failover
                if (failoverSuccessful(false)) {
System.out.println("FAILOVER SUCCESSFUL, try send again");
                    continue tryagain;
                }
                throw new cMsgException(e.getMessage());
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
     * @return response from subdomain handler
     * @throws cMsgException if there are communication problems with the server;
     *                       subject and/or type is null
     */
    public int syncSend(cMsgMessage message) throws cMsgException {

        if (!hasSyncSend) {
            throw new cMsgException("sync send is not implemented by this subdomain");
        }

        String subject = message.getSubject();
        String type = message.getType();
        String text = message.getText();

        // check args first
        if (subject == null || type == null) {
            throw new cMsgException("message subject and/or type is null");
        }

        if (text == null) {
            text = "";
        }

        // this sender's name if msg created here
        String msgCreator = message.getCreator();
        if (msgCreator == null) msgCreator = creator;

        int binaryLength = message.getByteArrayLength();

        // go here to try the syncSend again
        tryagain:
        while (true) {

            try {
                // cannot run this simultaneously with connect, reconenct, or disconnect
                notConnectLock.lock();
                // cannot run this simultaneously with itself since it receives communication
                // back from the server
                returnCommunicationLock.lock();
                try {

                    if (!connected) {
                        throw new IOException("not connected to server");
                    }

                    socketLock.lock();
                    try {
//System.out.println("syncSend 1");
                        // total length of msg (not including this int) is 1st item
                        domainOut.writeInt(4 * 15 + subject.length() + type.length() + msgCreator.length() +
                                           text.length() + binaryLength);
                        domainOut.writeInt(cMsgConstants.msgSyncSendRequest);
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
                        domainOut.writeInt(msgCreator.length());
                        domainOut.writeInt(text.length());
                        domainOut.writeInt(binaryLength);

                        // write strings & byte array
                        try {
                            domainOut.write(subject.getBytes("US-ASCII"));
                            domainOut.write(type.getBytes("US-ASCII"));
                            domainOut.write(msgCreator.getBytes("US-ASCII"));
                            domainOut.write(text.getBytes("US-ASCII"));
                            if (binaryLength > 0) {
                                domainOut.write(message.getByteArray(),
                                                message.getByteArrayOffset(),
                                                binaryLength);
                            }
//System.out.println("syncSend 2");
                        }
                        catch (UnsupportedEncodingException e) {
                        }
                    }
                    finally {
                        socketLock.unlock();
                    }

                    domainOut.flush(); // no need to be protected by socketLock
//System.out.println("syncSend 3");
                    int response = domainIn.readInt(); // this is protected by returnCommunicationLock
//System.out.println("syncSend 4");
                    return response;
                }
                finally {
                    returnCommunicationLock.unlock();
                    notConnectLock.unlock();
                }
            }
            catch (IOException e) {
                // wait awhile for possible failover
                if (failoverSuccessful(false)) {
System.out.println("FAILOVER SUCCESSFUL, try syncSend again");
                    continue tryagain;
                }
                throw new cMsgException(e.getMessage());
            }
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to subscribe to receive messages of a subject and type from the domain server.
     * The combination of arguments must be unique. In other words, only 1 subscription is
     * allowed for a given set of subject, type, callback, and userObj.
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
     * @throws cMsgException if the callback, subject and/or type is null or blank;
     *                       an identical subscription already exists; there are
     *                       communication problems with the server
     */
    public void subscribe(String subject, String type, cMsgCallbackInterface cb, Object userObj)
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

        // go here to try the subscribe again
        tryagain:
        while (true) {

            boolean addedHashEntry  = false;
            cMsgSubscription newSub = null;
            cMsgCallbackThread cbThread = null;

            try {
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

                    int id = 0;

                    // client listening thread may be interating thru subscriptions concurrently
                    // and we may change set structure
                    synchronized (subscriptions) {

                        // for each subscription ...
                        for (cMsgSubscription sub : subscriptions) {
                            // If subscription to subject & type exist already, keep track of it locally
                            // and don't bother the server since any matching message will be delivered
                            // to this client anyway.
                            if (sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                                // Only add another callback if the callback/userObj
                                // combination does NOT already exist. In other words,
                                // a callback/argument pair must be unique for a single
                                // subscription. Otherwise it is impossible to unsubscribe.

                                // for each callback listed ...
                                for (cMsgCallbackThread cbt : sub.getCallbacks()) {
                                    // if callback and user arg already exist, reject the subscription
                                    if ((cbt.callback == cb) && (cbt.getArg() == userObj)) {
                                        throw new cMsgException("subscription already exists");
                                    }
                                }

                                // add to existing set of callbacks
                                sub.addCallback(new cMsgCallbackThread(cb, userObj));
                                return;
                            }
                        }

                        // If we're here, the subscription to that subject & type does not exist yet.
                        // We need to create it and register it with the domain server.

                        // First generate a unique id for the receiveSubscribeId field. This info
                        // allows us to unsubscribe.
                        id = uniqueId.getAndIncrement();

                        // add a new subscription & callback
                        cbThread = new cMsgCallbackThread(cb, userObj);
                        newSub   = new cMsgSubscription(subject, type, id, cbThread);

                        // client listening thread may be interating thru subscriptions concurrently
                        // and we're changing the set structure
                        subscriptions.add(newSub);
                        addedHashEntry = true;
                    }

                    socketLock.lock();
                    try {
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

                        domainOut.flush();
                    }
                    finally {
                        socketLock.unlock();
                    }
                }
                finally {
                    subscribeLock.unlock();
                    notConnectLock.unlock();
                }
            }
            catch (IOException e) {
                // undo the modification of the hashtable we made & stop the created thread
                if (addedHashEntry) {
                    // "subscriptions" is synchronized so it's mutex protected
                    subscriptions.remove(newSub);
                    cbThread.dieNow();
                }

                // wait awhile for possible failover
                if (failoverSuccessful(false)) {
System.out.println("FAILOVER SUCCESSFUL, try subscribe again");
                    continue tryagain;
                }

                throw new cMsgException(e.getMessage());
            }

            break;
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to unsubscribe a previous subscription to receive messages of a subject and type
     * from the domain server. Since many subscriptions may be made to the same subject and type
     * values, but with different callbacks and user objects, the callback and user object must
     * be specified so the correct subscription can be removed.
     *
     * Note about the server failing and an IOException being thrown. To have "unsubscribe" make
     * sense on the failover server, we must wait until all existing subscriptions have been
     * successfully resubscribed on the new server.
     *
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @param userObj any user-supplied object to be given to the callback method as an argument
     * @throws cMsgException if there are communication problems with the server; subject
     *                       and/or type is null or blank
     */
    public void unsubscribe(String subject, String type, cMsgCallbackInterface cb, Object userObj)
            throws cMsgException {

        if (!hasUnsubscribe) {
            throw new cMsgException("unsubscribe is not implemented by this subdomain");
        }

        // check args first
        if (subject == null || type == null || cb == null) {
            throw new cMsgException("subject, type or callback argument is null");
        }
        else if (subject.length() < 1 || type.length() < 1) {
            throw new cMsgException("subject or type is blank string");
        }

        // go here to try the unsubscribe again
        tryagain:
        while (true) {

            cMsgSubscription oldSub  = null;
            cMsgCallbackThread cbThread = null;

            try {
                // cannot run this simultaneously with connect or disconnect
                notConnectLock.lock();
                // cannot run this simultaneously with subscribe (get wrong order at server)
                // or itself (iterate over same hashtable)
                subscribeLock.lock();

                try {
                    if (!connected) {
                        throw new cMsgException("not connected to server");
                    }

                    // look for and remove any subscription to subject/type with this callback object
                    boolean foundMatch = false;
                    cMsgSubscription sub;
                    int id = 0;

                    // client listening thread may be interating thru subscriptions concurrently
                    // and we may change set structure
                    synchronized (subscriptions) {

                        // for each subscription ...
                        for (Iterator iter = subscriptions.iterator(); iter.hasNext();) {
                            sub = (cMsgSubscription) iter.next();
                            // if subscription to subject & type exist ...
                            if (sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                                // for each callback listed ...
                                for (Iterator iter2 = sub.getCallbacks().iterator(); iter2.hasNext();) {
                                    cMsgCallbackThread cbt = (cMsgCallbackThread) iter2.next();
                                    if ((cbt.callback == cb) && (cbt.getArg() == userObj)) {
                                        // Found our cb & userArg pair to get rid of.
                                        // However, don't kill the thread and remove it from
                                        // the callback set until the server is notified or
                                        // the server doesn't need to be notified.
                                        // That way we can "undo" the unsubscribe if there
                                        // is an IO error.
                                        foundMatch = true;
                                        cbThread = cbt;
                                        break;
                                    }
                                }

                                // if no subscription to sub/type/cb/arg, return
                                if (!foundMatch) {
                                    return;
                                }

                                // If there are still callbacks left,
                                // don't unsubscribe for this subject/type
                                if (sub.numberOfCallbacks() > 1) {
                                    // kill callback thread
                                    cbThread.dieNow();
                                    // remove this callback from the set
                                    sub.getCallbacks().remove(cbThread);
                                    return;
                                }
                                // else get rid of the whole subscription
                                else {
                                    id = sub.getId();
                                    oldSub = sub;
                                    //iter.remove();
                                }

                                break;
                            }
                        }
                    }

                    // if no subscription to sub/type, return
                    if (!foundMatch) {
                        return;
                    }

                    // notify the domain server

                    socketLock.lock();
                    try {
                        // total length of msg (not including this int) is 1st item
                        domainOut.writeInt(5 * 4 + subject.length() + type.length());
                        domainOut.writeInt(cMsgConstants.msgUnsubscribeRequest);
                        domainOut.writeInt(id); // reserved for future use
                        domainOut.writeInt(subject.length());
                        domainOut.writeInt(type.length());
                        domainOut.writeInt(0); // no namespace being sent

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

                    // Now that we've communicated with the server,
                    // delete stuff from hashes & kill threads -
                    // basically, do the unsubscribe now.
                    cbThread.dieNow();
                    synchronized (subscriptions) {
                        oldSub.getCallbacks().remove(cbThread);
                        subscriptions.remove(oldSub);
                    }

                }
                finally {
                    subscribeLock.unlock();
                    notConnectLock.unlock();
                }
            }
            catch (IOException e) {
                // wait awhile for possible failover && resubscribe is complete
                if (failoverSuccessful(true)) {
System.out.println("FAILOVER SUCCESSFUL, try unsubscribe again");
                    continue tryagain;
                }

                throw new cMsgException(e.getMessage());
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

            int id = 0;
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
                mySub.setId(id);
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
     * NOTE: Disconnecting when one thread is in the waiting part of a subscribeAndGEt may cause that
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
        cMsgGetHelper helper = null;
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

            // create cMsgGetHelper object (not callback thread object)
            helper = new cMsgGetHelper(subject, type);

            // keep track of get calls
            subscribeAndGets.put(id, helper);
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
            }
            finally {
                socketLock.unlock();
            }
            domainOut.flush();
        }
        catch (IOException e) {
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
        // If msg is null, we timed out.
        // Tell server to forget the get if necessary.
        if (helper.timedOut) {
//System.out.println("subscribeAndGet: timed out, call unSubscribeAndGet");
            // remove the get from server
            subscribeAndGets.remove(id);
            unSubscribeAndGet(subject, type, id);
            throw new TimeoutException();
        }
        else if (helper.errorCode != cMsgConstants.ok) {
            cMsgException ex = new cMsgException("server died", helper.errorCode);
            throw ex;
        }

        // If msg is received, server has removed subscription from his records.
        // Client listening thread has also removed subscription from client's
        // records (subscribeAndGets HashSet).

//System.out.println("subscribeAndGet: SUCCESS!!!");

        return helper.message;
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
        String text       = message.getText();
        String msgCreator = message.getCreator();
        // this sender's creator if msg created here
        if (msgCreator == null) msgCreator = creator;


        // check args first
        if (subject == null || type == null) {
            throw new cMsgException("message subject and/or type is null");
        }

        if (text == null) {
            text = "";
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
                domainOut.writeInt(4*15 + subject.length() + type.length() + msgCreator.length() +
                                   text.length() + binaryLength);
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
                domainOut.writeInt(msgCreator.length());
                domainOut.writeInt(text.length());
                domainOut.writeInt(binaryLength);

                // write strings & byte array
                try {
                    domainOut.write(subject.getBytes("US-ASCII"));
                    domainOut.write(type.getBytes("US-ASCII"));
                    domainOut.write(msgCreator.getBytes("US-ASCII"));
                    domainOut.write(text.getBytes("US-ASCII"));
                    if (binaryLength > 0) {
                        domainOut.write(message.getByteArray(),
                                        message.getByteArrayOffset(),
                                        binaryLength);
                    }
                }
                catch (UnsupportedEncodingException e) {}
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
        if (helper.timedOut) {
//System.out.println("sendAndGet: timed out");
            // remove the get from server
            sendAndGets.remove(id);
            unSendAndGet(id);
            throw new TimeoutException();
        }
        else if (helper.errorCode != cMsgConstants.ok) {
            cMsgException ex = new cMsgException("server died", helper.errorCode);
            throw ex;
        }

        // If msg arrived (may be null), server has removed subscription from his records.
        // Client listening thread has also removed subscription from client's
        // records (subscribeAndGets HashSet).

//System.out.println("get: SUCCESS!!!");

        return helper.message;
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
        server = cMsgMessageMatcher.constructServerName(server);

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
     * @param channel nio socket communication channel
     * @throws IOException if there are communication problems with the name server
     * @throws cMsgException if the name server's domain does not match the UDL's domain;
     *                       the client cannot be registered; the domain server cannot
     *                       open a listening socket or find a port to listen on; or
     *                       the name server cannot establish a connection to the client
     */
    void talkToNameServerFromClient(SocketChannel channel)
            throws IOException, cMsgException {

        byte[] buf = new byte[512];

        DataInputStream  in  = new DataInputStream(new BufferedInputStream(channel.socket().getInputStream()));
        DataOutputStream out = new DataOutputStream(new BufferedOutputStream(channel.socket().getOutputStream()));

        out.writeInt(cMsgConstants.msgConnectRequest);
        out.writeInt(cMsgConstants.version);
        out.writeInt(cMsgConstants.minorVersion);
        out.writeInt(port);
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

        hasSend            = (buf[0] == (byte)1) ? true : false;
        hasSyncSend        = (buf[1] == (byte)1) ? true : false;
        hasSubscribeAndGet = (buf[2] == (byte)1) ? true : false;
        hasSendAndGet      = (buf[3] == (byte)1) ? true : false;
        hasSubscribe       = (buf[4] == (byte)1) ? true : false;
        hasUnsubscribe     = (buf[5] == (byte)1) ? true : false;
        hasShutdown        = (buf[6] == (byte)1) ? true : false;

        // Read port & length of host name.
        domainServerPort = in.readInt();
        int hostLength   = in.readInt();

        // read host name
        if (hostLength > buf.length) {
            buf = new byte[hostLength];
        }
        in.readFully(buf, 0, hostLength);
        domainServerHost = new String(buf, 0, hostLength, "US-ASCII");

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("        << CL: domain server host = " + domainServerHost +
                               ", port = " + domainServerPort);
        }
    }


    /**
     * Method to parse the Universal Domain Locator (UDL) into its various components.
     *
     * @param udl UDL to parse
     * @return an object with all the parsed UDL information in it
     * @throws cMsgException if UDL is null, or no host given in UDL
     */
    ParsedUDL parseUDL(String udl) throws cMsgException {

        if (udl == null) {
            throw new cMsgException("invalid UDL");
        }
System.out.println("parseUDL: udl = " + udl);

        // cMsg domain UDL is of the form:
        //       cMsg:cMsg://<host>:<port>/<subdomainType>/<subdomain remainder>?tag=value&tag2=value2 ...
        //
        // strip off the cMsg:cMsg:// to begin with

        int index = udl.indexOf("cMsg://");
        if (index < 0) {
            throw new cMsgException("invalid UDL");
        }
        String udlRemainder = udl.substring(index+7);
System.out.println("udl is pared down to " + udlRemainder);

        // Remember that for this domain:
        // 1) port is not necessary
        // 2) host can be "localhost" and may also includes dots (.)
        // 3) if domainType is cMsg, subdomainType is automatically set to cMsg if not given.
        //    if subdomainType is not cMsg, it is required
        // 4) remainder is past on to the subdomain plug-in
        // 5) client's password is in tag=value part of UDL as cmsgpassword=<password>

        Pattern pattern = Pattern.compile("([\\w\\.]+):?(\\d+)?/?(\\w+)?/?(.*)");
        Matcher matcher = pattern.matcher(udlRemainder);

        String udlHost=null, udlPort=null, udlSubdomain=null, udlSubRemainder=null;

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

        // find cmsgpassword parameter if it exists
        String pswd = "";
        if (udlSubRemainder != null) {
            // look for ?key=value& or &key=value& pairs
            Pattern pat = Pattern.compile("(?:[&\\?](\\w+)=(\\w+)(?=&))");
            Matcher mat = pat.matcher(udlSubRemainder+"&");

            loop: while (mat.find()) {
                    for (int i=0; i < mat.groupCount()+1; i++) {
                       // if key = cmsgpassword ...
                        if (mat.group(i).equalsIgnoreCase("cmsgpassword")) {
                            // password must be value
                            pswd = mat.group(i+1);
                            if (debug >= cMsgConstants.debugInfo) {
                                System.out.println("  cmsg password = " + pswd);
                            }
                            break loop;
                        }
                        //System.out.println("group("+ i + ") = " + s6);
                    }
                  }
        }

        // need at least host
        if (udlHost == null) {
            throw new cMsgException("invalid UDL");
        }

        // if subdomain not specified, use cMsg subdomain
        if (udlSubdomain == null) {
            udlSubdomain = "cMsg";
        }

        // if the host is "localhost", find the actual, fully qualified  host name
        if (udlHost.equalsIgnoreCase("localhost")) {
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
        int udlPortInt=-1;
        if (udlPort != null && udlPort.length() > 0) {
            try {udlPortInt = Integer.parseInt(udlPort);}
            catch (NumberFormatException e) {
                udlPortInt = cMsgNetworkConstants.nameServerStartingPort;
                if (debug >= cMsgConstants.debugWarn) {
                    System.out.println("parseUDL: non-integer port, guessing server port is " + udlPortInt);
                }
            }
        }
        else {
            udlPortInt = cMsgNetworkConstants.nameServerStartingPort;
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

        // store results in a class
        return new ParsedUDL(udl, udlRemainder, udlSubdomain, udlSubRemainder,
                             pswd, udlHost, udlPortInt);
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
System.out.println("Resubscribing to " + sub.getSubject() + "/" + sub.getType());
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
        boolean valid;

        /** Constructor. */
        ParsedUDL(String udl, boolean validUDL) {
            UDL   = udl;
            valid = validUDL;
        }

        /** Constructor. */
        ParsedUDL(String s1, String s2, String s3, String s4, String s5, String s6, int i) {
            UDL            = s1;
            UDLremainder   = s2;
            subdomain      = s3;
            subRemainder   = s4;
            password       = s5;
            nameServerHost = s6;
            nameServerPort = i;
            valid          = true;
        }

        /** Take all of this object's parameters and copy to this client's members. */
        void copyToLocal() {
            cMsg.this.UDL            = UDL;
            cMsg.this.UDLremainder   = UDLremainder;
            cMsg.this.subdomain      = subdomain;
            cMsg.this.subRemainder   = subRemainder;
            cMsg.this.password       = password;
            cMsg.this.nameServerHost = nameServerHost;
            cMsg.this.nameServerPort = nameServerPort;
            System.out.println("Copy from stored parsed UDL to local :");
            System.out.println("  UDL            = "  + UDL);
            System.out.println("  UDLremainder   = "  + UDLremainder);
            System.out.println("  subdomain      = "  + subdomain);
            System.out.println("  subRemainder   = "  + subRemainder);
            System.out.println("  password       = "  + password);
            System.out.println("  nameServerHost = "  + nameServerHost);
            System.out.println("  nameServerPort = "  + nameServerPort);
        }

        /** Clear this client's members. */
        void clearLocal() {
            cMsg.this.UDL            = null;
            cMsg.this.UDLremainder   = null;
            cMsg.this.subdomain      = null;
            cMsg.this.subRemainder   = null;
            cMsg.this.password       = null;
            cMsg.this.nameServerHost = null;
            cMsg.this.nameServerPort = 0;
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Class that periodically checks the health of the domain server.
     * If the server responds to the keepAlive command, everything is OK.
     * If it doesn't, it is assumed dead and a disconnect is done.
     */
    class KeepAlive extends Thread {
        /** Socket communication channel with domain server. */
        private SocketChannel channel;

        /** Socket input stream associated with channel. */
        private DataInputStream in;

        /** Socket output stream associated with channel. */
        private DataOutputStream out;

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
         * @param channel communication channel with domain server
         * @throws IOException if channel is closed
         */
        public void changeChannels(SocketChannel channel) throws IOException {
            this.channel = channel;
            // buffered communication streams for efficiency
            in = new DataInputStream(new BufferedInputStream(channel.socket().getInputStream()));
            out = new DataOutputStream(new BufferedOutputStream(channel.socket().getOutputStream()));
        }


        /**
         * Constructor.
         *
         * @param channel communication channel with domain server
         * @throws IOException if channel is closed
         */
        public KeepAlive(SocketChannel channel, boolean implementFailovers,
                         ArrayList<cMsg.ParsedUDL> failovers) throws IOException {
            this.channel = channel;
            this.failovers = failovers;
            this.implementFailovers = implementFailovers;

            // buffered communication streams for efficiency
            in = new DataInputStream(new BufferedInputStream(channel.socket().getInputStream()));
            out = new DataOutputStream(new BufferedOutputStream(channel.socket().getOutputStream()));

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
                    // periodically check to see if the domain server is alive
                    while (true) {
                        try {
                            // quit thread
                            if (killThread) {
                                return;
                            }

                            // send keep alive command
                            //out.writeInt(4);
                            out.writeInt(cMsgConstants.msgKeepAlive);
                            out.flush();

                            // read response -  1 int
                            in.readInt();

                            // sleep for 1 second and try again
                            Thread.sleep(1000);
                        }
                        catch (InterruptedException e) {
                            System.out.println("Interrupted Client during sleep");
                        }
                        catch (InterruptedIOException e) {
                            System.out.println("Interrupted Client during I/O");
                        }
                    }
                }
                catch (IOException e) {
                    connected = false;
                    e.printStackTrace();
                }

                // if we've reach here, there's an error
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("KeepAlive Thread: domain server is probably dead, dis/reconnect\n");
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
System.out.println("\nTrying to REconnect with UDL = " + p.UDL);
                        reconnect();

                        // restore subscriptions on the new server
                        try {
                            restoreSubscriptions();
                            resubscriptionsComplete = true;
                        }
                        catch (cMsgException e) {
                            // if subscriptions fail, then we do NOT use failover server
                            try { channel.close(); disconnect(); }
                            catch (Exception e1) { }
                            throw e;
                        }

                        // we got ourselves a new server, boys
                        weGotAConnection = true;
                    }
                    catch (cMsgException e) {
System.out.println("  Got error: " + e.getMessage());
                        // clear effects of p.copyToLocal()
                        p.clearLocal();
                        connectFailures++;
                    }
                }
            }

            // close communication channel
            try { channel.close(); }
            catch (IOException e) { }

            // disconnect
System.out.println("KeepAlive: run client's disconnect & end");
            disconnect();
        }

    }


}
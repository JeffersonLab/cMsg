/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 7-Jul-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain;

import java.io.*;
import java.net.*;
import java.lang.*;
import java.util.*;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicBoolean;
import java.nio.channels.*;
import java.nio.ByteBuffer;

import org.jlab.coda.cMsg.*;

/**
 * This class implements a cMsg domain server in the cMsg domain. If this class is
 * ever rewritten in a way that allows multiple threads to concurrently access the
 * subdomainHandler object, the subdomainHandler object must be rewritten to synchronize the
 * subscribe and unsubscribe methods.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgDomainServer extends Thread {

    /** Type of domain this is. */
    private static String domainType = "cMsg";

    /** Maximum number of temporary threads allowed per client connection. */
    private static final int tempThreadsMax = 100;

    /**
     * Maximum number of permanent threads per client connection. This should
     * be at least two (2). One thread can handle syncSends which can block
     * (but only 1 syncSend at a time can be run on the client side)
     * and the other to handle other requests.
     */
    private static final int permanentThreads = 3;

    /** Port number listening on. */
    private int port;

    /** Host this is running on. */
    private String host;

    /** Level of debug output. */
    private int debug = cMsgConstants.debugInfo;

    /**
     * Object containing information about the domain client.
     * Certain members of info can only be filled in by this thread,
     * such as the listening port & host.
     */
    private cMsgClientInfo info;

    /** Server channel (contains socket). */
    private ServerSocketChannel serverChannel;

    /** Reference to subdomain handler object. */
    private cMsgSubdomainInterface subdomainHandler;

    /**
     * Thread-safe queue to hold cMsgHolder objects. This cue holds
     * requests from the client (except for subscribe and unsubscribe)
     * which are grabbed and processed by waiting worker threads.
     */
    LinkedBlockingQueue<cMsgHolder> requestCue;

    /**
     * Thread-safe queue to hold cMsgHolder objects. This cue holds
     * subscribe and unsubscribe requests from the client which are
     * then grabbed and processed by a single worker thread. The subscribe
     * and unsubscribe requests must be done sequentially.
     */
    private LinkedBlockingQueue<cMsgHolder> subscribeCue;

    /**
     * Thread-safe list of RequestThread objects. This cue is used
     * to end these threads nicely during a shutdown.
     */
    private ConcurrentLinkedQueue<RequestThread> requestThreads;

    /**
     * List of all ClientHandler objects. This list is used to
     * end these threads nicley during a shutdown.
     */
    private ArrayList<ClientHandler> handlerThreads;

    /** Current number of temporary threads. */
    private AtomicInteger tempThreads = new AtomicInteger();

    /** Keep track of whether the shutdown method of this object has already been called. */
    AtomicBoolean calledShutdown = new AtomicBoolean();

    /**
     * Keep track of whether the handleShutdown method of the subdomain
     * handler has already been called.
     */
    private AtomicBoolean calledSubdomainShutdown = new AtomicBoolean();

    /** Kill main thread if true. */
    private volatile boolean killMainThread;

    /** Kill all spawned threads if true. */
    volatile boolean killSpawnedThreads;

    /** Kill spawned threads. */
    public void killSpawnedThreads() {
        killSpawnedThreads = true;
    }

    /**
     * Getter for the subdomain handler object.
     * @return subdomain handler object
     */
    public cMsgSubdomainInterface getSubdomainHandler() {
        return subdomainHandler;
    }


    /**
     * Constructor which starts threads.
     *
     * @param handler object which handles all requests from the client
     * @param info object containing information about the client for which this
     *                    domain server was started
     * @param startingPort suggested port on which to starting listening for connections
     * @throws org.jlab.coda.cMsg.cMsgException If a port to listen on could not be found
     */
    public cMsgDomainServer(cMsgSubdomainInterface handler, cMsgClientInfo info, int startingPort) throws cMsgException {
        subdomainHandler = handler;
        // Port number to listen on
        port = startingPort;
        this.info = info;

        requestCue   = new LinkedBlockingQueue<cMsgHolder>(5000);
        subscribeCue = new LinkedBlockingQueue<cMsgHolder>(100);

        requestThreads = new ConcurrentLinkedQueue<RequestThread>();
        handlerThreads = new ArrayList<ClientHandler>(10);

        // At this point, find a port to bind to. If that isn't possible, throw
        // an exception. We want to do this in the constructor, because it's much
        // harder to do it in a separate thread and then report back the results.
        try {
            serverChannel = ServerSocketChannel.open();
        }
        catch (IOException ex) {
            ex.printStackTrace();
            cMsgException e = new cMsgException("Exiting Server: cannot open a listening socket");
            e.setReturnCode(cMsgConstants.errorSocket);
            throw e;
        }

        ServerSocket listeningSocket = serverChannel.socket();

        while (true) {
            try {
                listeningSocket.bind(new InetSocketAddress(port));
                break;
            }
            catch (IOException ex) {
                // try another port by adding one
                if (port < 65536) {
                    port++;
                }
                else {
                    ex.printStackTrace();
                    cMsgException e = new cMsgException("Exiting Server: cannot find port to listen on");
                    e.setReturnCode(cMsgConstants.errorSocket);
                    throw e;
                }
            }
        }

        // fill in info members
        info.setDomainPort(port);
        try {
            host = InetAddress.getLocalHost().getHostName();
        }
        catch (UnknownHostException ex) {
        }
        info.setDomainHost(host);

        // Start thread to monitor client's health.
        // If he dies, kill this thread.
        cMsgMonitorClient monitor =  new cMsgMonitorClient(info, this);
        monitor.setDaemon(true);
        monitor.start();
    }


    /**
     * Method to be run when this server's client is dead or disconnected and
     * the server threads will be killed. It runs the "shutdown" method of its
     * subdomainHandler object.
     * <p/>
     * Finalize methods are run after an object has become unreachable and
     * before the garbage collector is run;
     */
    public void finalize() throws cMsgException {
        if (calledSubdomainShutdown.compareAndSet(false,true)) {
            subdomainHandler.handleClientShutdown();
        }
        //System.out.println("\nFINALIZE !!!\n");
    }


    /** Method to gracefully shutdown this object's threads. */
    synchronized void shutdown() {
        //System.out.println("SHUTDOWN BEING RUN");

        // tell subdomain handler to shutdown
        if (calledSubdomainShutdown.compareAndSet(false,true)) {
            try {subdomainHandler.handleClientShutdown();}
            catch (cMsgException e) {}
        }

        // tell spawned threads to stop
        killSpawnedThreads = true;

        try { Thread.sleep(10); }
        catch (InterruptedException e) {}

        // first stop threads that get client commands over sockets
        for (ClientHandler h : handlerThreads) {
            h.interrupt();
            try {h.channel.close();}
            catch (IOException e) {}
        }

        // give threads a chance to shutdown
        try { Thread.sleep(10); }
        catch (InterruptedException e) {}

        // clear cue, no more requests should be coming in
        requestCue.clear();

        // Give request handling threads a chance to shutdown.
        // They wakeup every .5 sec
        try { Thread.sleep(800); }
        catch (InterruptedException e) {}

        // now shutdown the main thread which shouldn't take more than 1 second
        killMainThread = true;
    }


    /** This method is executed as a thread. */
    public void run() {
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("Running Domain Server");
        }

        try {
            // get things ready for a select call
            Selector selector = Selector.open();

            // set nonblocking mode for the listening socket
            serverChannel.configureBlocking(false);

            // register the channel with the selector for accepts
            serverChannel.register(selector, SelectionKey.OP_ACCEPT);

            while (true) {
                if (killMainThread) {
                    return;
                }

                // 1 second timeout
                int n = selector.select(1000);

                // if no channels (sockets) are ready, listen some more
                if (n == 0) {
                    // but first check to see if we've been commanded to die
                    if (killMainThread) {
                        return;
                    }
                    continue;
                }

                // get an iterator of selected keys (ready sockets)
                Iterator it = selector.selectedKeys().iterator();

                // look at each key
                while (it.hasNext()) {
                    SelectionKey key = (SelectionKey) it.next();

                    // is this a new connection coming in?
                    if (key.isValid() && key.isAcceptable()) {
                        ServerSocketChannel server = (ServerSocketChannel) key.channel();
                        // accept the connection from the client
                        SocketChannel channel = server.accept();

                        // set socket options
                        Socket socket = channel.socket();
                        // Set socket timeout to 1 second in case we need to die.
                        // This doesn't seem to work for some reason!
                        socket.setSoTimeout(1000);
                        // Set tcpNoDelay so no packets are delayed
                        socket.setTcpNoDelay(true);
                        // set buffer sizes
                        socket.setReceiveBufferSize(65535);
                        socket.setSendBufferSize(65535);

                        // start up client handling thread & store reference
                        handlerThreads.add(new ClientHandler(channel));

                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("cMsgDomainServer: new connection from " +
                                               info.getName() + "\n");
                        }
                    }
                    // remove key from selected set since it's been handled
                    it.remove();
                }
            }
        }
//BUGBUG
        catch (IOException ex) {
            if (debug >= cMsgConstants.debugError) {
                //ex.printStackTrace();
            }
        }

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("\nQuitting Domain Server");
        }

        return;
    }


    /**
     * Class to handle a socket connection to the client of which
     * there may be many. The current implementation has only 2.
     * One connections handles the client's keepAlive requests of
     * the server. The other handles everything else.
     */
    private class ClientHandler extends Thread {

        private SocketChannel channel;

        /** A direct buffer is necessary for nio socket IO. */
        private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

        /** Allocate int array once (used for reading in data) for efficiency's sake. */
        private int[] inComing = new int[14];

        /** Allocate byte array once (used for reading in data) for efficiency's sake. */
        private byte[] bytes = new byte[5000];


        /** Constructor. */
        ClientHandler(SocketChannel channel) {
            this.channel = channel;

            // start up permanent worker threads on the regular cue
            for (int i = 0; i < permanentThreads; i++) {
                requestThreads.add(new RequestThread(true, false));
            }
            // start up 1 permanent worker thread on the (un)subscribe cue
            requestThreads.add(new RequestThread(true, true));

            // die if no more non-daemon thds running
            setDaemon(true);

            start();
        }


        /**
         * This method handles all communication between a cMsg user who has
         * connected to a domain and this server for that domain.
         */
        public void run() {
            int msgId;
            boolean useSubscribeCue;

            try {
                while (true) {
                    try {
                        if (killSpawnedThreads) return;
                        // keep reading until we have an int (4 bytes) of data
                        if (cMsgUtilities.readSocketBytes(buffer, channel, 4, debug) < 4) {
                            // got less than 1 int, something's wrong, kill connection
                            killSpawnedThreads();
                            return;
                        }
                        if (killSpawnedThreads) return;
                    }
                    // socket timed out, check to see if we must die
                    catch (InterruptedIOException ex) {
                        //System.out.println("domain server client connection timeout");
                        if (killSpawnedThreads) return;
                        continue;
                    }
                    // interrupt was called while reading
                    catch (ClosedByInterruptException ex) {
                        //System.out.println("domain server client connection read interrupted");
                        return;
                    }

                    // make buffer readable
                    buffer.flip();

                    // read client's request
                    msgId = buffer.getInt();

                    cMsgHolder holder = null;
                    useSubscribeCue = false;

                    switch (msgId) {

                        case cMsgConstants.msgSendRequest: // receiving a message
                            holder = readSendInfo(channel);
                            break;

                        case cMsgConstants.msgSyncSendRequest: // receiving a message
                            holder = readSendInfo(channel);
                            holder.channel = channel;
                            break;

                        case cMsgConstants.msgSubscribeAndGetRequest: // 1-shot subscription of subject & type
                            holder = readSubscribeInfo(channel);
                            break;

                        case cMsgConstants.msgSendAndGetRequest: // getting a message
                            holder = readGetInfo(channel);
                            break;

                        case cMsgConstants.msgUnSendAndGetRequest: // ungetting sendAndGet request
                            holder = readUngetInfo(channel);
                            break;

                        case cMsgConstants.msgUnSubscribeAndGetRequest: // ungetting subscribeAndGet request
                            holder = readUngetInfo(channel);
                            break;

                        case cMsgConstants.msgSubscribeRequest: // subscribing to subject & type
                            holder = readSubscribeInfo(channel);
                            useSubscribeCue = true;
                            break;

                        case cMsgConstants.msgUnsubscribeRequest: // unsubscribing from a subject & type
                            holder = readSubscribeInfo(channel);
                            useSubscribeCue = true;
                            break;

                        case cMsgConstants.msgKeepAlive: // see if this end is still here
                            // send ok back as acknowledgment
                            buffer.clear();
                            buffer.putInt(cMsgConstants.ok).flip();
                            while (buffer.hasRemaining()) {
                                channel.write(buffer);
                            }
                            subdomainHandler.handleKeepAlive();
                            break;

                        case cMsgConstants.msgDisconnectRequest: // client disconnecting
                            // need to shutdown this domain server
                            if (calledShutdown.compareAndSet(false,true)) {
                                //System.out.println("SHUTDOWN TO BE RUN BY msgDisconnectRequest");
                                shutdown();
                            }
                            return;

                        case cMsgConstants.msgShutdown: // tell clients/servers to shutdown
                            holder = readShutdownInfo(channel);
                            useSubscribeCue = true;
                            break;

                        default:
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("dServer handleClient: can't understand your message " + info.getName());
                            }
                            break;
                    }


                    // if we got something to put on a cue, do it
                    if (holder != null) {
                        holder.request = msgId;
                        if (useSubscribeCue) {
                            try {subscribeCue.put(holder);}
                            catch (InterruptedException e) {}
                        }
                        else {
                            try {requestCue.put(holder);}
                            catch (InterruptedException e) {}
                        }
                    }

                    // if the cue is getting too large, add temp threads to handle the load
                    if (requestCue.size() > 2000 && tempThreads.get() < tempThreadsMax) {
                        new RequestThread();
                    }
                }
            }
            catch (cMsgException ex) {
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("dServer handleClient: command-reading thread's connection to client is dead from cMsg error");
                    ex.printStackTrace();
                }
            }
            catch (IOException ex) {
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("dServer handleClient: command-reading thread's connection to client is dead from IO error");
                    ex.printStackTrace();
                }
            }
        }


        /**
         * This method reads an incoming cMsgMessageFull from a client.
         *
         * @param channel nio socket communication channel
         * @return object holding message read from channel
         * @throws java.io.IOException If socket read or write error
         */
        private cMsgHolder readSendInfo(SocketChannel channel) throws IOException {

            // create a message
            cMsgMessageFull msg = new cMsgMessageFull();

            // keep reading until we have 14 ints of data
            cMsgUtilities.readSocketBytes(buffer, channel, 56, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // read ints
            buffer.asIntBuffer().get(inComing, 0, 14);

            msg.setVersion(inComing[0]);
            // inComing[1] is for future use
            msg.setUserInt(inComing[2]);
            msg.setSysMsgId(inComing[3]);
            msg.setSenderToken(inComing[4]);
            msg.setInfo(inComing[5]);

            // time message was sent = 2 ints (hightest byte first)
            // in milliseconds since midnight GMT, Jan 1, 1970
            long time = ((long)inComing[6] << 32) | inComing[7];
            msg.setSenderTime(new Date(time));
            // user time
            time = ((long)inComing[8] << 32) | inComing[9];
            msg.setUserTime(new Date(time));

            int lengthSubject = inComing[10];
            int lengthType    = inComing[11];
            int lengthText    = inComing[12];
            int lengthCreator = inComing[13];

            // bytes expected
            int bytesToRead = lengthSubject + lengthType + lengthText + lengthCreator;

            // read in all remaining bytes
            cMsgUtilities.readSocketBytes(buffer, channel, bytesToRead, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // allocate bigger byte array if necessary
            // (allocate more than needed for speed's sake)
            if (bytesToRead > bytes.length) {
                bytes = new byte[bytesToRead];
                //System.out.println("DS:  ALLOCATING BUFFER, bytes = " + bytesToRead);
            }

            // read into array
            buffer.get(bytes, 0, bytesToRead);

            // read subject
            msg.setSubject(new String(bytes, 0, lengthSubject, "US-ASCII"));

            // read type
            msg.setType(new String(bytes, lengthSubject, lengthType, "US-ASCII"));

            // read text
            msg.setText(new String(bytes, lengthSubject + lengthType, lengthText, "US-ASCII"));

            // read creator
            msg.setCreator(new String(bytes, lengthSubject + lengthType + lengthText,
                                      lengthCreator, "US-ASCII"));

            // fill in message object's members
            msg.setDomain(domainType);
            msg.setReceiver("cMsg domain server");
            msg.setReceiverHost(info.getDomainHost());
            msg.setReceiverTime(new Date()); // current time
            msg.setSender(info.getName());
            msg.setSenderHost(info.getClientHost());

            return new cMsgHolder(msg);
        }


        /**
         * This method reads an incoming cMsgMessageFull from a client doing a sendAndGet.
         *
         * @param channel nio socket communication channel
         * @return object holding message read from channel
         * @throws java.io.IOException If socket read or write error
         */
        private cMsgHolder readGetInfo(SocketChannel channel) throws IOException {

            // create a message
            cMsgMessageFull msg = new cMsgMessageFull();

            // keep reading until we have 12 ints of data
            cMsgUtilities.readSocketBytes(buffer, channel, 48, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // read ints
            buffer.asIntBuffer().get(inComing, 0, 12);

            msg.setVersion(inComing[0]);
            // inComing[1] is for future use
            msg.setUserInt(inComing[2]);
            msg.setSenderToken(inComing[3]);

            // time message was sent = 2 ints (hightest byte first)
            // in milliseconds since midnight GMT, Jan 1, 1970
            long time = ((long)inComing[4] << 32) | ((long)inComing[5] & 0x00000000FFFFFFFFL);
            msg.setSenderTime(new Date(time));
            // user time
            time = ((long)inComing[6] << 32) | ((long)inComing[7] & 0x00000000FFFFFFFFL);
            msg.setUserTime(new Date(time));

            int lengthSubject = inComing[8];
            int lengthType    = inComing[9];
            int lengthText    = inComing[10];
            int lengthCreator = inComing[11];

            // bytes expected
            int bytesToRead = lengthSubject + lengthType + lengthText + lengthCreator;

            // read in all remaining bytes
            cMsgUtilities.readSocketBytes(buffer, channel, bytesToRead, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // allocate bigger byte array if necessary
            // (allocate more than needed for speed's sake)
            if (bytesToRead > bytes.length) {
                bytes = new byte[bytesToRead];
            }

            // read into array
            buffer.get(bytes, 0, bytesToRead);

            // read subject
            msg.setSubject(new String(bytes, 0, lengthSubject, "US-ASCII"));

            // read type
            msg.setType(new String(bytes, lengthSubject, lengthType, "US-ASCII"));

            // read text
            msg.setText(new String(bytes, lengthSubject + lengthType, lengthText, "US-ASCII"));

            // read creator
            msg.setCreator(new String(bytes, lengthSubject + lengthType + lengthText,
                                      lengthCreator, "US-ASCII"));

            // fill in message object's members
            msg.setInfo(cMsgMessage.isGetRequest);
            msg.setGetRequest(true);
            msg.setDomain(domainType);
            msg.setReceiver("cMsg domain server");
            msg.setReceiverHost(info.getDomainHost());
            msg.setReceiverTime(new Date()); // current time
            msg.setSender(info.getName());
            msg.setSenderHost(info.getClientHost());

            return new cMsgHolder(msg);
        }


        /**
         * This method reads an incoming (un)subscribe or subscribeAndGet request from a client.
         *
         * @param channel nio socket communication channel
         * @return object holding subject, type, and id read from channel
         * @throws java.io.IOException If socket read or write error
         */
        private cMsgHolder readSubscribeInfo(SocketChannel channel) throws IOException {
            cMsgHolder holder = new cMsgHolder();

            // keep reading until we have 3 ints of data
            cMsgUtilities.readSocketBytes(buffer, channel, 12, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // read 3 ints
            buffer.asIntBuffer().get(inComing, 0, 3);

            // id of subject/type combination  (receiverSubscribedId)
            holder.id = inComing[0];
            // length of subject
            int lengthSubject = inComing[1];
            // length of type
            int lengthType = inComing[2];

            // bytes expected
            int bytesToRead = lengthSubject + lengthType;

            // read in all remaining bytes
            cMsgUtilities.readSocketBytes(buffer, channel, bytesToRead, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // allocate bigger byte array if necessary
            // (allocate more than needed for speed's sake)
            if (bytesToRead > bytes.length) {
                bytes = new byte[bytesToRead];
            }

            // read into array
            buffer.get(bytes, 0, bytesToRead);

            // read subject
            holder.subject = new String(bytes, 0, lengthSubject, "US-ASCII");

            // read type
            holder.type = new String(bytes, lengthSubject, lengthType, "US-ASCII");

            return holder;
        }


        /**
         * This method reads incoming information from a client doing a shutdown.
         *
         * @param channel nio socket communication channel
         * @return object holding message read from channel
         * @throws java.io.IOException If socket read or write error
         */
        private cMsgHolder readShutdownInfo(SocketChannel channel) throws IOException {

            // keep reading until we have 3 ints of data
            cMsgUtilities.readSocketBytes(buffer, channel, 12, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // read 3 ints
            buffer.asIntBuffer().get(inComing, 0, 3);

            int flag         = inComing[0];
            int lengthClient = inComing[1];
            int lengthServer = inComing[2];

            // bytes expected
            int bytesToRead = lengthClient + lengthServer;

            // read in all remaining bytes
            cMsgUtilities.readSocketBytes(buffer, channel, bytesToRead, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // allocate bigger byte array if necessary
            // (allocate more than needed for speed's sake)
            if (bytesToRead > bytes.length) {
                bytes = new byte[bytesToRead];
            }

            // read into array
            buffer.get(bytes, 0, bytesToRead);

            // read client
            String client = new String(bytes, 0, lengthClient, "US-ASCII");

            // read client
            String server = new String(bytes, 0, lengthServer, "US-ASCII");

            return new cMsgHolder(client, server, flag);
        }


        /**
         * This method reads an incoming unget request from a client.
         *
         * @param channel nio socket communication channel
         * @return object holding id read from channel
         * @throws java.io.IOException If socket read or write error
         */
        private cMsgHolder readUngetInfo(SocketChannel channel) throws IOException {
            // keep reading until we have 1 ints of data
            cMsgUtilities.readSocketBytes(buffer, channel, 4, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // id of subject/type combination  (senderToken actually)
            cMsgHolder holder = new cMsgHolder();
            holder.id = buffer.getInt();

            return holder;
        }
    }


    /**
     * Class for taking a cued-up request from the client and processing it.
     */
    private class RequestThread extends Thread {
        /** Is this thread temporary or permanent? */
        boolean permanent;

        /** Does this thread read from the (un)subscribe cue only*/
        boolean subscribe;

        /** A direct buffer is necessary for nio socket IO. */
        private ByteBuffer buffer = ByteBuffer.allocateDirect(8);


        /** Self-starting constructor. */
        RequestThread() {
            // by default thread is not permanent or reading (un)subscribe requests
            tempThreads.getAndIncrement();
            //System.out.println(temp +" temp");

            // die if main thread dies
            setDaemon(true);

            this.start();
        }

        /** Self-starting constructor. */
        RequestThread(boolean permanent, boolean subscribe) {
            this.permanent = permanent;
            this.subscribe = subscribe;
            if (!permanent) {
                tempThreads.getAndIncrement();
            }
            this.start();
        }

        /** Loop forever waiting for work to do. */
        public void run() {
            cMsgHolder holder = null;
            int answer;

            while (true) {

                if (killSpawnedThreads) return;

                try {
                    // try for up to 1/2 second to read a request from the cue
                    if (!subscribe) {
                        holder = requestCue.poll(500, TimeUnit.MILLISECONDS);
                    }
                    else {
                        holder = subscribeCue.poll(500, TimeUnit.MILLISECONDS);
                    }
                }
                catch (InterruptedException e) {
                }

                if (holder == null) {
                    // if this is a permanent thread, keeping trying to read requests
                    if (permanent) {
                        continue;
                    }
                    // if this is a temp thread, disappear after no requests for 1/2 second
                    else {
                        tempThreads.getAndDecrement();
                        //System.out.println(temp +" temp");
                        return;
                    }
                }

                try {
                    switch (holder.request) {

                        case cMsgConstants.msgSendRequest: // receiving a message
                            subdomainHandler.handleSendRequest(holder.message);
                            break;

                        case cMsgConstants.msgSyncSendRequest: // receiving a message
                            answer = subdomainHandler.handleSyncSendRequest(holder.message);
                            syncSendReply(holder.channel, answer);
                            break;

                        case cMsgConstants.msgSubscribeAndGetRequest: // getting a message of subject & type
                            subdomainHandler.handleSubscribeAndGetRequest(holder.subject,
                                                                          holder.type,
                                                                          holder.id);
                            break;

                        case cMsgConstants.msgSendAndGetRequest: // sending a message to a responder
                            subdomainHandler.handleSendAndGetRequest(holder.message);
                            break;

                        case cMsgConstants.msgUnSendAndGetRequest: // ungetting sendAndGet
                            subdomainHandler.handleUnSendAndGetRequest(holder.id);
                            break;

                        case cMsgConstants.msgUnSubscribeAndGetRequest: // ungetting subscribeAndGet
                            subdomainHandler.handleUnSubscribeAndGetRequest(holder.id);
                            break;

                        case cMsgConstants.msgSubscribeRequest: // subscribing to subject & type
                            subdomainHandler.handleSubscribeRequest(holder.subject, holder.type, holder.id);
                            break;

                        case cMsgConstants.msgUnsubscribeRequest: // unsubscribing from a subject & type
                            subdomainHandler.handleUnsubscribeRequest(holder.subject, holder.type, holder.id);
                            break;

                        case cMsgConstants.msgShutdown: // shutting down various clients and servers
                            subdomainHandler.handleShutdownRequest(holder.client, holder.server, holder.flag);
                            break;

                        default:
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("dServer requestThread: can't understand your message " + info.getName());
                            }
                            break;
                    }
                }
                catch (cMsgException e) {
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("dServer requestThread: thread picking commands off cue has died from cMsg error");
                        e.printStackTrace();
                    }
                }
                catch (IOException e) {
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("dServer requestThread: thread picking commands off cue has died from IO error");
                        e.printStackTrace();
                    }
                }
            }

        }


        /**
         * This method returns the value received from the subdomain handler object's
         * handleSyncSend method to the client.
         *
         * @param channel nio socket communication channel
         * @param answer  handleSyncSend return value to pass to client
         * @throws java.io.IOException If socket read or write error
         */
        private void syncSendReply(SocketChannel channel, int answer) throws IOException {
            // send back answer
            buffer.clear();
            buffer.putInt(answer).flip();
            while (buffer.hasRemaining()) {
                channel.write(buffer);
            }
        }
    }
}


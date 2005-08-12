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

package org.jlab.coda.cMsg.cMsgDomain.server;

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
import org.jlab.coda.cMsg.cMsgDomain.cMsgHolder;

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
    /** Number to keep track of threads for debugging purposes. */
    private int id;

    /** Type of domain this is. */
    private static String domainType = "cMsg";

    /** Maximum number of temporary threads allowed per client connection. */
    private static int tempThreadsMax = 20;

    /**
     * Maximum number of permanent threads per client. This should
     * be at least two (2). One thread can handle syncSends which can block
     * (but only 1 syncSend at a time can be run on the client side)
     * and the other to handle other requests.
     */
    private static int commandHandlingThreadsMax = 3;

    /** Current number of running RequestThread threads handling non-subscribe requests. */
    private int numberCommandHandlingThreads;

    /** Port number listening on. */
    private int port;

    /** Host this is running on. */
    private String host;

    /** Level of debug output. */
    private int debug = cMsgConstants.debugError;

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

    /** Reference to cMsg subdomain handler object if appropriate. */
    private org.jlab.coda.cMsg.subdomains.cMsg cMsgSubdomainHandler;

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
     * Thread-safe queue to hold cMsgHolder objects. This cue holds
     * lock and unlock requests from the client which are
     * then grabbed and processed by a single worker thread. The lock
     * and unlock requests must be done sequentially.
     */
    private LinkedBlockingQueue<cMsgHolder> lockCue;

    /** Request thread is of "normal" request types. */
    static private int NORMAL = 0;

    /** Request thread is of (un)subscribe request types. */
    static private int SUBSCRIBE = 1;

    /** Request thread is of (un)lock request types. */
    static private int LOCK = 2;

    /**
     * This is a holder for info on what to write back to the client.
     * Note that even though this is global to the class, there are only 2
     * clients which use this domain server. One of those clients only does
     * keepAlive responses. Thus, this object is used by only 1 client. That
     * client only issues 1 response-invoked command at a time by its design.
     * That means that should be NO multithreading issues with this object.
     */
    private cMsgHolder writeHolder;

    /**
     * Thread-safe list of RequestThread objects. This cue is used
     * to end these threads nicely during a shutdown.
     */
    private ConcurrentLinkedQueue<RequestThread> requestThreads;

    /**
     * The ClientHandler object which handles all incoming requests.
     * This reference is used to end this thread nicley during a shutdown.
     */
    private ClientHandler clientHandlerThread;

    /**
     * The KeepAliveHandler object which handles all incoming keep alive requests.
     * This reference is used to end this thread nicley during a shutdown.
     */
    private KeepAliveHandler keepAliveThread;

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
     * Set the time ordering property of the server.
     * If this is true, then all non-(un)subscribe commands sent to it
     * are guaranteed to be passed to the subdomain handler object in
     * the order in which they were received.
     *
     * @param timeOrdered set to true if timeordering of commands is desired
     */
    static public void setTimeOrdered(boolean timeOrdered) {
        if (timeOrdered == true) {
            commandHandlingThreadsMax = 1;
            tempThreadsMax = 0;
        }
    }


    /**
     * Constructor which starts threads.
     *
     * @param handler object which handles all requests from the client
     * @param info object containing information about the client for which this
     *                    domain server was started
     * @param startingPort suggested port on which to starting listening for connections
     * @throws cMsgException if listening socket could not be opened or a port to listen on could not be found
     */
    public cMsgDomainServer(cMsgSubdomainInterface handler, cMsgClientInfo info, int startingPort) throws cMsgException {

        subdomainHandler = handler;
        // If we're in the cMsg subdomain, create an object that has access
        // to methods besides those in the cMsgSubdomainInterface.
        if (subdomainHandler instanceof org.jlab.coda.cMsg.subdomains.cMsg) {
            cMsgSubdomainHandler = (org.jlab.coda.cMsg.subdomains.cMsg)subdomainHandler;
        }

        // Port number to listen on
        port = startingPort;
        this.info = info;

        requestCue   = new LinkedBlockingQueue<cMsgHolder>(100);
        subscribeCue = new LinkedBlockingQueue<cMsgHolder>(100);
        lockCue      = new LinkedBlockingQueue<cMsgHolder>(100);

        requestThreads = new ConcurrentLinkedQueue<RequestThread>();

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
            host = InetAddress.getLocalHost().getCanonicalHostName();
        }
        catch (UnknownHostException ex) {
        }
        info.setDomainHost(host);

//System.out.println(">>    --------------------------");
//System.out.println(">>    DS: listening on PORT " + port);
//System.out.println(">>    --------------------------");

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

        // stop thread that gets client keep alives over socket
        keepAliveThread.interrupt();
        try {keepAliveThread.channel.close();}
        catch (IOException e) {}

        // stop thread that gets client commands over socket
        clientHandlerThread.interrupt();
        try {clientHandlerThread.channel.close();}
        catch (IOException e) {}

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

        // remove client from "bridges" and "nameServers" collections
        cMsgServerBridge b = cMsgNameServer.bridges.remove(info.getName());
        Integer i = cMsgNameServer.nameServers.remove(info.getName());
        if (debug >= cMsgConstants.debugInfo) {
            if (b != null && i != null) {
                System.out.println(">>    DS: DELETED SERVER FROM BRIDGES AND NAMESERVERS");
            }
            else {
                System.out.println(">>    DS: COULD NOT DELETE SERVER FROM BRIDGES AND/OR NAMESERVERS");
            }
        }

    }


    /** This method is executed as a thread. */
    public void run() {
        int connectionNumber = 1;

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println(">>    DS: Running Domain Server");
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

                // first check to see if we've been commanded to die
                if (killMainThread)  return;
                // if no channels (sockets) are ready, listen some more
                if (n == 0) continue;

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
                        // Set tcpNoDelay so no packets are delayed
                        socket.setTcpNoDelay(true);
                        // set buffer sizes
                        socket.setReceiveBufferSize(65535);
                        socket.setSendBufferSize(65535);

                        // The first connection is for a client request handling thread
                        if (connectionNumber == 1) {
                            clientHandlerThread = new ClientHandler(channel, id++);
                        }
                        // The second connection is for a keep alive thread
                        else if (connectionNumber == 2) {
                            keepAliveThread = new KeepAliveHandler(channel);
                        }

                        connectionNumber++;

                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println(">>    DS: new connection from " +
                                               info.getName());
                            //System.out.println("socket blocking =  " + channel.isBlocking() + "\n");
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
            System.out.println("\n>>    DS: Quitting Domain Server");
        }

        return;
    }


    /**
     * Class to handle the incoming keepAlive requests from the client.
     */
    private class KeepAliveHandler extends Thread {
        /** Socket communication channel. */
        private SocketChannel channel;

        /** Input stream from client socket. */
        private DataInputStream  in;

        /** Output stream from client socket. */
        private DataOutputStream out;


        /** Constructor. */
        KeepAliveHandler(SocketChannel channel) {
            this.channel = channel;

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

            try {

                in = new DataInputStream(new BufferedInputStream(channel.socket().getInputStream(), 65536));
                out = new DataOutputStream(new BufferedOutputStream(channel.socket().getOutputStream(), 2048));

                while (true) {
                    // read client's request
                    msgId = in.readInt();
                    if (msgId != cMsgConstants.msgKeepAlive) {
                        throw new cMsgException("Wrong request, expecting keep alive request");
                    }
                    // send ok back as acknowledgment
                    out.writeInt(cMsgConstants.ok);
                    out.flush();
                    subdomainHandler.handleKeepAlive();
                }

            }
            catch (cMsgException ex) {
                if (debug >= cMsgConstants.debugError) {
                    System.out.println(">>    DS: keep alive thread's connection to client is dead from cMsg error");
                    ex.printStackTrace();
                }
            }
            catch (IOException ex) {
                if (debug >= cMsgConstants.debugError) {
                    System.out.println(">>    DS: keep alive thread's connection to client is dead from IO error");
                    ex.printStackTrace();
                }
            }
        }
    }


    /**
     * Class to handle all client requests except the keep alives.
     */
    private class ClientHandler extends Thread {
        /** Socket communication channel. */
        private SocketChannel channel;

        /** Allocate byte array once (used for reading in data) for efficiency's sake. */
        private byte[] bytes = new byte[20000];
        private int id;

        /** Input stream from client socket. */
        private DataInputStream  in;
        /** Output stream from client socket. */
        private DataOutputStream out;

        /** Channel for allowing this thread to read from request threads. */
        ReadableByteChannel localIn;
        /** Channel for allowing request thread to write to this thread. */
        WritableByteChannel localOut;

        /** Same as {@link #localIn} but used for selector. */
        SelectableChannel localInSelect;
        /** Same as {@link #localOut} but used for selector. */
        SelectableChannel localOutSelect;

        // variables to track message rate
        //double freq=0., freqAvg=0.;
        //long t1, t2, deltaT, totalT=0, totalC=0, count=0, loops=10000, ignore=5;


        /** Constructor. */
        ClientHandler(SocketChannel channel, int id) {
            this.channel = channel;
            this.id = id;
            Pipe pipe = null;

            // set up pipe communications between request worker threads and this one
            try {
                pipe           = Pipe.open();
                localIn        = (ReadableByteChannel) pipe.source();
                localOut       = (WritableByteChannel) pipe.sink();
                localInSelect  = (SelectableChannel)   pipe.source();
                localOutSelect = (SelectableChannel)   pipe.sink();
            }
            catch (IOException e) {
                System.out.println("\n>>    DS: Quitting Domain Server: cannot create pipe");
                System.exit(-1);
            }

            // start up permanent worker threads on the regular cue
            for (int i = 0; i < commandHandlingThreadsMax; i++) {
                if (numberCommandHandlingThreads < commandHandlingThreadsMax) {
                    requestThreads.add(new RequestThread(true, NORMAL, localOut));
                    numberCommandHandlingThreads++;
                }
                else {
                    break;
                }
            }

            // start up 1 and only 1 permanent worker thread on the (un)subscribe cue
            requestThreads.add(new RequestThread(true, SUBSCRIBE, null));

            // start up 1 and only 1 permanent worker thread on the (un)lock cue
            requestThreads.add(new RequestThread(true, LOCK, localOut));

            // die if no more non-daemon thds running
            setDaemon(true);
            
            start();
        }


        /**
         * This method handles all communication between a cMsg user who has
         * connected to a domain and this server for that domain.
         */
        public void run() {
            // for printing out request cue size periodically
            /*
            Date now, t;
            now = new Date();
            */

            try {
                // Use select on socket and pipe
                Selector selector = Selector.open();

                // Since using select, we MUST make channels nonblocking
                localInSelect.configureBlocking(false);
                channel.configureBlocking(false);

                // register channels with selector
                localInSelect.register(selector, SelectionKey.OP_READ);
                channel.register(selector, SelectionKey.OP_READ);

                // buffered communication streams for efficiency
                in  = new DataInputStream(new BufferedInputStream(channel.socket().getInputStream(), 65536));
                out = new DataOutputStream(new BufferedOutputStream(channel.socket().getOutputStream(), 2048));

                SelectableChannel chan = null;

                here:

                while (true) {
                    // check to see if we've been commanded to die
                    if (killSpawnedThreads)  return;


                    // block up to 3 seconds
                    int n = selector.select(3000);

                    // check again to see if we've been commanded to die
                    if (killSpawnedThreads)  return;

                    // if no channels (sockets) are ready, listen some more
                    if (n == 0) continue;

                    // get an iterator of selected keys (ready channels)
                    Iterator it = selector.selectedKeys().iterator();
                    SelectionKey outKey = null;

                    // look at each key
                    while (it.hasNext()) {
                        SelectionKey key = (SelectionKey) it.next();

                        // is this channel readable?
                        if (key.isValid() && key.isReadable()) {

                            chan = key.channel();
                            it.remove();

                            // is the channel a client socket?
                            if (chan instanceof SocketChannel) {
//System.out.println("    socket chan is readable, " + chan);
                                // unregister channel so we can change to blocking mode
                                key.cancel();
                                selector.selectNow();

                                // change to blocking mode so IO streams can be used
                                chan.configureBlocking(true);

                                // read request from client
                                readFromClient();

                                // change to nonblocking mode so channel can be used with select
                                chan.configureBlocking(false);

                                // reregister channel with select
                                channel.register(selector, SelectionKey.OP_READ);

                                continue;
                            }

                            // or is channel a request thread in this JVM?
                            else {
                                outKey = channel.keyFor(selector);
//System.out.println("    pipe chan (" + chan + ") is readable, outKey = " + outKey);
                                outKey.cancel();
                                key.cancel();
                                selector.selectNow();
                                channel.configureBlocking(true);
                                writeToClient();
                                channel.configureBlocking(false);
                                channel.register(selector, SelectionKey.OP_READ);
                                localInSelect.register(selector, SelectionKey.OP_READ);
                                continue;
                            }
                        }
                        else {
                            System.out.println(">>    DS: Some other SELECT condition");
                        }
                    }


                }
            }
            catch (IOException ex) {
                if (debug >= cMsgConstants.debugError) {
                    System.out.println(">>    DS: command-reading thread's connection to client is dead from IO error");
                    ex.printStackTrace();
                }
            }
        }


        /**
         * This method replies to certain client commands which may block or
         * take too long to process in the {@link #readFromClient} method.
         */
        private void writeToClient() throws IOException {
            ByteBuffer buffie = ByteBuffer.allocateDirect(1);
            // read 1 token byte (just a signal to call this method)
            localIn.read(buffie);

            // read item off writing cue
            switch (writeHolder.request) {
                case cMsgConstants.msgSyncSendRequest:
//System.out.println("  writing back to Client for sync send lock");
                    sendIntReply(writeHolder.answer);
                    break;

                case cMsgConstants.msgServerRegistrationLock:
//System.out.println(">>    DS: writing back to Client for registration lock");
                    sendIntReply(writeHolder.answer);
                    break;

                case cMsgConstants.msgServerCloudLock:
//System.out.println(">>    DS: writing back to Client for cloud lock");
                    sendIntReply(writeHolder.answer);
//System.out.println(">>    DS: DONE writing back to Client for cloud lock");
                    break;
                default:
                    break;
            }

        }


        /**
         * This method reads commands from a client over a socket and will reply if the reply
         * does not block or take an excessive amount of time.
         *
         * @throws IOException if socket connection to client was closed
         */
        private void readFromClient() throws IOException {

            int size, msgId, requestType;

            // read first int
            size = in.readInt();
//System.out.println("Read in size = " + size);

            // read client's request
            msgId = in.readInt();
//System.out.println("Read in msgId = " + msgId);

            // calculate rate
            /*
            if (count == 0) {
                t1 = System.currentTimeMillis();
            }
            else if (count == loops-1) {
                t2 = System.currentTimeMillis();

                deltaT  = t2 - t1; // millisec
                totalT += deltaT;
                totalC += count;
                freq    = ((double)  count) / ((double)deltaT) * 1000.;
                freqAvg = ((double) totalC) / ((double)totalT) * 1000.;
                //System.out.println(id + " t1 = " + t1 + ", t2 = " + t2 + ", deltaT = " + deltaT);
                //System.out.println(id + " count = " + count + ", totalC = " + totalC + ", totalT = " + totalT);
                count = -1;

                if (true) {
                    System.out.println(doubleToString(freq, 1) + " Hz, Avg = " +
                                       doubleToString(freqAvg, 1) + " Hz");
                }
            }
            count++;
            */

            cMsgHolder holder = null;
            requestType = NORMAL;

            switch (msgId) {

                case cMsgConstants.msgSendRequest: // receiving a message
                    holder = readSendInfo();
                    //continue here;
                    break;

                case cMsgConstants.msgSyncSendRequest: // receiving a message
//System.out.println("Try to read in sync send request");
                    holder = readSendInfo();
//System.out.println("Read in sync send request");
                    //holder.out = out;
                    break;

                case cMsgConstants.msgSubscribeAndGetRequest: // 1-shot subscription of subject & type
                    holder = readSubscribeInfo();
                    break;

                case cMsgConstants.msgSendAndGetRequest: // getting a message
                    holder = readGetInfo();
                    break;

                case cMsgConstants.msgUnSendAndGetRequest: // ungetting sendAndGet request
                    holder = readUngetInfo();
                    break;

                case cMsgConstants.msgUnSubscribeAndGetRequest: // ungetting subscribeAndGet request
                    holder = readUngetInfo();
                    break;

                case cMsgConstants.msgSubscribeRequest: // subscribing to subject & type
                    holder = readSubscribeInfo();
                    requestType = SUBSCRIBE;
                    break;

                case cMsgConstants.msgUnsubscribeRequest: // unsubscribing from a subject & type
                    holder = readSubscribeInfo();
                    requestType = SUBSCRIBE;
                    break;

                case cMsgConstants.msgDisconnectRequest: // client disconnecting
                    // need to shutdown this domain server
                    if (calledShutdown.compareAndSet(false, true)) {
                        //System.out.println("SHUTDOWN TO BE RUN BY msgDisconnectRequest");
                        shutdown();
                    }
                    return;

                case cMsgConstants.msgShutdown: // tell clients/servers to shutdown
                    holder = readShutdownInfo();
                    requestType = SUBSCRIBE;
                    break;

                case cMsgConstants.msgServerRegistrationLock: // grab lock for client registration
                    // Grabbing this lock may take up to 1/2 second so
                    // pass along to thread pool so as not to block server.
                    int delay = in.readInt();
                    holder = new cMsgHolder();
                    holder.request = cMsgConstants.msgServerRegistrationLock;
                    holder.delay = delay;
                    //holder.out = out;
                    requestType = LOCK;
                    break;

                case cMsgConstants.msgServerRegistrationUnlock: // release lock for client registration
                    holder = new cMsgHolder();
                    holder.request = cMsgConstants.msgServerRegistrationUnlock;
                    requestType = LOCK;
                    break;

                case cMsgConstants.msgServerCloudLock: // grab lock for server cloud joining
                    // Grabbing this lock may take up to 1/2 second so
                    // pass along to thread pool so as not to block server.
                    delay = in.readInt();
//System.out.println(">>    DS: got request to lock cloud, out = " + out);
                    holder = new cMsgHolder();
                    holder.request = cMsgConstants.msgServerCloudLock;
                    holder.delay = delay;
                    //holder.out = out;
                    requestType = LOCK;
                    break;

                case cMsgConstants.msgServerCloudUnlock: // release lock for server cloud joining
                    holder = new cMsgHolder();
                    holder.request = cMsgConstants.msgServerCloudUnlock;
                    requestType = LOCK;
                    break;

                case cMsgConstants.msgServerCloudJoin: // server client is (un)joining cMsg subdomain server cloud
                    int status = in.readInt();
                    cMsgServerBridge bridge = cMsgNameServer.bridges.get(info.getName());
                    if (bridge != null) {
//System.out.println(">>    DS: got request to tell us that " + bridge.server + " has joined the cloud");
                        bridge.setCloudStatus(status);
                    }
                    // if bridge can't be found, try alternate name first
                    else {
                        String alternateName = null;
                        String name  = info.getName();
                        String sPort = name.substring(name.lastIndexOf(":")+1);
                        int index = name.indexOf(".");

                        // If the name has a dot (is qualified), create unqualified name
                        if (index > -1) {
                            alternateName = name.substring(0,index) + ":" + sPort;
                        }
                        // else create qualified name
                        else {
                            try {
                                // take off ending port
                                alternateName = name.substring(0, name.lastIndexOf(":"));
                                alternateName = InetAddress.getByName(alternateName).getCanonicalHostName();
                                alternateName = alternateName + ":" + sPort;
                            }
                            catch (UnknownHostException e) {
                                break;
                            }
                        }
                        bridge = cMsgNameServer.bridges.get(alternateName);
                        if (bridge != null) {
//System.out.println(">>    DS: got request to tell us that " + bridge.server + " has joined the cloud");
                            bridge.setCloudStatus(status);
                        }
                     }
                    break;

                case cMsgConstants.msgServerSendClientNames: // in cMsg subdomain send back all local client names
//System.out.println(">>    DS: got request to send client names");
                    sendClientNames(cMsgSubdomainHandler.getClientNames());
                    break;

                default:
                    if (debug >= cMsgConstants.debugWarn) {
                        System.out.println("dServer handleClient: can't understand your message " + info.getName());
                    }
                    if (calledShutdown.compareAndSet(false, true)) {
                        //System.out.println("SHUTDOWN TO BE RUN BY msgDisconnectRequest");
                        shutdown();
                    }
                    return;
            }


            // if we got something to put on a cue, do it
            if (holder != null) {
                holder.request = msgId;
                if (requestType == NORMAL) {
                    try {
                        requestCue.put(holder);
                    }
                    catch (InterruptedException e) {
                    }
                }
                else if (requestType == SUBSCRIBE) {
                    try {
                        subscribeCue.put(holder);
                    }
                    catch (InterruptedException e) {
                    }
                }
                else {
                    try {
                        lockCue.put(holder);
                    }
                    catch (InterruptedException e) {
                    }
                }
            }
            // print out request cue size periodically
            /*
            t = new Date();
            if (now.getTime() + 5000 <= t.getTime()) {
                System.out.println(info.getName() + ":");
                System.out.println("requests = " + requestCue.size());
                System.out.println("subscribes = " + subscribeCue.size());
                System.out.println("req thds = " + requestThreads.size());
                System.out.println("handler thds = " + handlerThreads.size());
                now = t;
            }
            */

            // if the cue is getting too large, add temp threads to handle the load
            if (requestCue.size() > 2000 && tempThreads.get() < tempThreadsMax) {
                new RequestThread(localOut);
            }
        }


        /**
          * This method returns an integer value to the client.
          *
          * @param answer return value to pass to client
          * @throws java.io.IOException If socket read or write error
          */
         private void sendIntReply(int answer) throws IOException {
             // send back answer
//System.out.println("    sendIntReplay(out, " + answer +")");
             out.writeInt(answer);
             out.flush();
//System.out.println("    sent int");
         }


        /**
         * This method returns a list of local client names to the client (remote server).
         *
         * @throws java.io.IOException If socket read or write error
         */
        private void sendClientNames(String[] names) throws IOException {
            // send number of items to come
//System.out.println(">>    DS: write # of names = " + names.length);
            out.writeInt(names.length);

//System.out.println(">>    DS: send " + names.length + " client names");
            // send lengths of strings
            for (int i=0; i < names.length; i++) {
//System.out.println(">>    DS: send client name len = " + names[i].length());
                out.writeInt(names[i].length());
            }

            // send strings
            try {
                for (int i=0; i < names.length; i++) {
//System.out.println(">>    DS: send client name = " + names[i]);
                    out.write(names[i].getBytes("US-ASCII"));
                }
            }
            catch (UnsupportedEncodingException e) {}
//System.out.println(">>    DS: done writing names");
            out.flush();
        }


        /**
         * This method reads an incoming cMsgMessageFull from a client.
         *
         * @return object holding message read from channel
         * @throws java.io.IOException If socket read or write error
         */
        private cMsgHolder readSendInfo() throws IOException {

            // create a message
            cMsgMessageFull msg = new cMsgMessageFull();

            // first incoming integer is for future use
            in.skipBytes(4);

            msg.setUserInt(in.readInt());
            msg.setSysMsgId(in.readInt());
            msg.setSenderToken(in.readInt());
            msg.setInfo(in.readInt());

            // time message was sent = 2 ints (hightest byte first)
            // in milliseconds since midnight GMT, Jan 1, 1970
            long time = ((long) in.readInt() << 32) | ((long) in.readInt() & 0x00000000FFFFFFFFL);
            msg.setSenderTime(new Date(time));
            // user time
            time = ((long) in.readInt() << 32) | ((long) in.readInt() & 0x00000000FFFFFFFFL);
            msg.setUserTime(new Date(time));

            int lengthSubject = in.readInt();
            int lengthType    = in.readInt();
            int lengthCreator = in.readInt();
            int lengthText    = in.readInt();
            int lengthBinary  = in.readInt();

            // string bytes expected
            int stringBytesToRead = lengthSubject + lengthType +
                                    lengthCreator + lengthText;
            int offset = 0;

            // read all string bytes
            if (stringBytesToRead > bytes.length) {
              bytes = new byte[stringBytesToRead];
            }
            in.readFully(bytes, 0, stringBytesToRead);

            // read subject
            msg.setSubject(new String(bytes, offset, lengthSubject, "US-ASCII"));
            offset += lengthSubject;
            //System.out.println("sub = " + msg.getSubject());

            // read type
            msg.setType(new String(bytes, offset, lengthType, "US-ASCII"));
            //System.out.println("type = " + msg.getType());
            offset += lengthType;

            // read creator
            msg.setCreator(new String(bytes, offset, lengthCreator, "US-ASCII"));
            //System.out.println("creator = " + msg.getCreator());
            offset += lengthCreator;

            // read text
            msg.setText(new String(bytes, offset, lengthText, "US-ASCII"));
            //System.out.println("text = " + msg.getText());
            offset += lengthText;

            if (lengthBinary > 0) {
                byte[] b = new byte[lengthBinary];

                // read all binary bytes
                in.readFully(b, 0, lengthBinary);

                try {msg.setByteArrayNoCopy(b, 0, lengthBinary);}
                catch (cMsgException e) {}
            }

            // fill in message object's members
            msg.setVersion(cMsgConstants.version);
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
         * @return object holding message read from channel
         * @throws java.io.IOException If socket read or write error
         */
        private cMsgHolder readGetInfo() throws IOException {

            // create a message
            cMsgMessageFull msg = new cMsgMessageFull();

            // inComing[0] is for future use
            in.skipBytes(4);

            msg.setUserInt(in.readInt());
            msg.setSenderToken(in.readInt());
            msg.setInfo(in.readInt());

            // time message was sent = 2 ints (hightest byte first)
            // in milliseconds since midnight GMT, Jan 1, 1970
            long time = ((long)in.readInt() << 32) | ((long)in.readInt() & 0x00000000FFFFFFFFL);
            msg.setSenderTime(new Date(time));
            // user time
            time = ((long)in.readInt() << 32) | ((long)in.readInt() & 0x00000000FFFFFFFFL);
            msg.setUserTime(new Date(time));

            int lengthSubject = in.readInt();
            int lengthType    = in.readInt();
            int lengthCreator = in.readInt();
            int lengthText    = in.readInt();
            int lengthBinary  = in.readInt();

            // string bytes expected
            int stringBytesToRead = lengthSubject + lengthType +
                                    lengthCreator + lengthText;
            int offset = 0;

            // read all string bytes
            if (stringBytesToRead > bytes.length) {
              bytes = new byte[stringBytesToRead];
            }
            in.readFully(bytes, 0, stringBytesToRead);

            // read subject
            msg.setSubject(new String(bytes, offset, lengthSubject, "US-ASCII"));
            offset += lengthSubject;
            //System.out.println("sub = " + msg.getSubject());

            // read type
            msg.setType(new String(bytes, offset, lengthType, "US-ASCII"));
            //System.out.println("type = " + msg.getType());
            offset += lengthType;

            // read creator
            msg.setCreator(new String(bytes, offset, lengthCreator, "US-ASCII"));
            //System.out.println("creator = " + msg.getCreator());
            offset += lengthCreator;

            // read text
            msg.setText(new String(bytes, offset, lengthText, "US-ASCII"));
            //System.out.println("text = " + msg.getText());
            offset += lengthText;

            if (lengthBinary > 0) {
                byte[] b = new byte[lengthBinary];

                // read all binary bytes
                in.readFully(b, 0, lengthBinary);

                try {msg.setByteArrayNoCopy(b, 0, lengthBinary);}
                catch (cMsgException e) {}
            }

            // fill in message object's members
            msg.setVersion(cMsgConstants.version);
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
         * @return object holding subject, type, and id read from channel
         * @throws java.io.IOException If socket read or write error
         */
        private cMsgHolder readSubscribeInfo() throws IOException {
            cMsgHolder holder = new cMsgHolder();

            // id of subject/type combination  (receiverSubscribedId)
            holder.id = in.readInt();
            // length of subject
            int lengthSubject = in.readInt();
            // length of type
            int lengthType = in.readInt();
            // bytes expected
            int bytesToRead = lengthSubject + lengthType;

            // read all string bytes
            if (bytesToRead > bytes.length) {
              bytes = new byte[bytesToRead];
            }
            in.readFully(bytes, 0, bytesToRead);

            // read subject
            holder.subject = new String(bytes, 0, lengthSubject, "US-ASCII");

            // read type
            holder.type = new String(bytes, lengthSubject, lengthType, "US-ASCII");

            return holder;
        }


        /**
         * This method reads incoming information from a client doing a shutdown.
         *
         * @return object holding message read from channel
         * @throws java.io.IOException If socket read or write error
         */
        private cMsgHolder readShutdownInfo() throws IOException {

            int flag         = in.readInt();
            int lengthClient = in.readInt();
            int lengthServer = in.readInt();

            // bytes expected
            int bytesToRead = lengthClient + lengthServer;

            // read all string bytes
            if (bytesToRead > bytes.length) {
              bytes = new byte[bytesToRead];
            }
            in.readFully(bytes, 0, bytesToRead);

            // read client
            String client = new String(bytes, 0, lengthClient, "US-ASCII");

            // read client
            String server = new String(bytes, lengthClient, lengthServer, "US-ASCII");

            return new cMsgHolder(client, server, flag);
        }


        /**
         * This method reads an incoming unget request from a client.
         *
         * @return object holding id read from channel
         * @throws java.io.IOException If socket read or write error
         */
        private cMsgHolder readUngetInfo() throws IOException {

            // id of subject/type combination  (senderToken actually)
            cMsgHolder holder = new cMsgHolder();
            holder.id = in.readInt();

            return holder;
        }
    }



    /**
     * Class for taking a cued-up request from the client and processing it.
     * There are 3 different cues. One is for "normal" requests, another for
     * (un)subscribes, and another for (un)locks.
     */
    private class RequestThread extends Thread {
        /** Is this thread temporary or permanent? */
        boolean permanent;

        /** Does this thread read from the (un)subscribe, (un)lock, or normal cue? */
        int requestType;

        /**
         * Channel used to signal ClientHandler thread that a
         * return value must be sent back to client.
         */
        WritableByteChannel localOut;

        /**
         * A direct buffer is necessary for nio IO to signal ClientHandler
         * thread that a return value must be sent back to client.
         */
        private ByteBuffer buffer = ByteBuffer.allocateDirect(1);


        /** Constructor for temporary thread to work on normal request cue. */
        RequestThread(WritableByteChannel localOut) {
            this.localOut = localOut;
            buffer.clear();
            buffer.put((byte)1);

            // by default thread is not permanent or reading (un)subscribe requests
            tempThreads.getAndIncrement();
            //System.out.println(temp +" temp");

            // die if main thread dies
            setDaemon(true);
            this.start();
        }

        /** General constructor. */
        RequestThread(boolean permanent, int requestType, WritableByteChannel localOut) {
            this.permanent   = permanent;
            this.requestType = requestType;
            this.localOut    = localOut;
            buffer.clear();
            buffer.put((byte)1);

            if (!permanent) {
                tempThreads.getAndIncrement();
            }
            this.start();
        }

        /** Loop forever waiting for work to do. */
        public void run() {
            cMsgHolder holder;
            int answer;

            while (true) {

                if (killSpawnedThreads) return;

                holder = null;

                try {
                    // try for up to 1/2 second to read a request from the cue
                    if (requestType == NORMAL) {
                        holder = requestCue.poll(500, TimeUnit.MILLISECONDS);
                    }
                    else if (requestType == SUBSCRIBE) {
                        holder = subscribeCue.poll(500, TimeUnit.MILLISECONDS);
                    }
                    else {
                        holder = lockCue.poll(500, TimeUnit.MILLISECONDS);
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
                            holder.answer = subdomainHandler.handleSyncSendRequest(holder.message);
 //System.out.println("Got sync send answer");
                            writeHolder = holder;
                            buffer.flip();
//System.out.println("write to internal buffer");
                            localOut.write(buffer);
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

                        case cMsgConstants.msgServerSubscribeRequest: // subscription by another server
                            cMsgSubdomainHandler.handleServerSubscribeRequest(holder.subject,
                                                                          holder.type,
                                                                          "namespace",
                                                                          holder.id);
                            break;
                        case cMsgConstants.msgServerSubscribeAndGetRequest: // subscribeAndGet by another server
                            cMsgSubdomainHandler.handleServerSubscribeAndGetRequest(holder.subject,
                                                                                holder.type,
                                                                                "namespace",
                                                                                holder.id);

                        case cMsgConstants.msgShutdown: // shutting down various clients and servers
                            subdomainHandler.handleShutdownRequest(holder.client, holder.server, holder.flag);
                            break;

                        case cMsgConstants.msgServerRegistrationUnlock: // release lock for client registration
                            cMsgSubdomainHandler.registrationUnlock();
                            break;

                        case cMsgConstants.msgServerCloudUnlock: // release lock for server cloud joining
                            cMsgNameServer.cloudUnlock();
                            break;

                        case cMsgConstants.msgServerRegistrationLock: // grab lock for global registration
                            // Send yes (1) to indicate lock grabbed , or no (0) back as return value.
                            // This may block for up to 1/2 second.
                            boolean gotLock = cMsgSubdomainHandler.registrationLock(holder.delay);
                            holder.answer =  gotLock ? 1 : 0;
                            writeHolder = holder;
                            buffer.flip();
                            localOut.write(buffer);
                            break;

                        case cMsgConstants.msgServerCloudLock: // grab lock for server cloud joining
                            // Send yes (1) to indicate lock grabbed , or no (0) back as return value.
                            // This may block for up to 0.2 seconds.
                            //DataOutputStream out = new DataOutputStream(new BufferedOutputStream(holder.channel.socket().getOutputStream(), 2048));
//System.out.println("DOMAIN SERVER: Try to lock cloud ...");
                            gotLock = cMsgNameServer.cloudLock(holder.delay);
//System.out.println("DOMAIN SERVER:   gotLock = " + gotLock + ", send reply, out = " + holder.out);
                            holder.answer = gotLock ? 1 : 0;
                            writeHolder = holder;
                            buffer.flip();
//System.out.println("DOMAIN SERVER:   write to buffer");
                            localOut.write(buffer);
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
                   // if (debug >= cMsgConstants.debugError) {
                        System.out.println("dServer requestThread: thread picking commands off cue has died from IO error");
                        e.printStackTrace();
                   // }
                }
            }

        }

    }


}


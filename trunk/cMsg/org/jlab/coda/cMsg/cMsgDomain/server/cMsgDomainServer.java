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
import java.util.concurrent.*;
import java.util.concurrent.locks.ReentrantLock;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicBoolean;
import java.nio.channels.*;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.cMsgDomain.cMsgHolder;
import org.jlab.coda.cMsg.cMsgDomain.cMsgNotifier;
import org.jlab.coda.cMsg.cMsgDomain.cMsgSubscription;

/**
 * This class implements a cMsg domain server in the cMsg domain. One object handles all
 * connections to a single client.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgDomainServer extends Thread {

    /** Type of domain this is. */
    private static String domainType = "cMsg";

    /** Maximum number of temporary trequest-handling hreads allowed per client connection. */
    private static int tempThreadsMax = 10;

    /**
     * Number of permanent request-handling threads per client. This is in
     * addition to the thread which handles (un)subscribe requests and the thread
     * that handles lock requests.
     * There should be at least two (2). If there are requests which block
     * (such as syncSend) there will be at least one thread to handle such while the
     * other handles other requests.
     */
    private static int permanentCommandHandlingThreads = 3;

    /** Port number listening on. */
    private int port;

    /** Host this is running on. */
    private String host;

    /** Level of debug output. */
    private int debug = cMsgConstants.debugError;

    /**
     * Object containing information about the client this object is connected to.
     * Certain members of info can only be filled in by this thread,
     * such as the listening port & host.
     */
    public cMsgClientInfo info;

    /** Server channel (contains socket). */
    private ServerSocketChannel serverChannel;

    /** Output stream from this server back to client. */
    private DataOutputStream backToClient;

    /** Reference to subdomain handler object. */
    private cMsgSubdomainInterface subdomainHandler;

    /** Reference to cMsg subdomain handler object if appropriate. */
    private org.jlab.coda.cMsg.subdomains.cMsg cMsgSubdomainHandler;

    /**
     * Thread-safe queue to hold cMsgHolder objects of
     * requests from the client (except for subscribes, unsubscribes, & locks).
     * These are grabbed and processed by waiting worker threads.
     */
    private LinkedBlockingQueue<cMsgHolder> requestCue;

    /**
     * Thread-safe queue to hold cMsgHolder objects of
     * subscribe and unsubscribe requests from the client. These are
     * then grabbed and processed by a single worker thread. The subscribe
     * and unsubscribe requests must be done sequentially.
     */
    private LinkedBlockingQueue<cMsgHolder> subscribeCue;

    /**
     * Thread-safe queue to hold cMsgHolder objects of
     * lock and unlock requests from the client. These are
     * then grabbed and processed by a single worker thread. The lock
     * and unlock requests must be done sequentially.
     */
    private LinkedBlockingQueue<cMsgHolder> lockCue;

    /** Request thread is of "normal" request types. */
    static private final int NORMAL = 0;

    /** Request thread is of (un)subscribe request types. */
    static private final int SUBSCRIBE = 1;

    /** Request thread is of (un)lock request types. */
    static private final int LOCK = 2;

    /**
     * Thread-safe list of RequestThread objects. This cue is used
     * to end these threads nicely during a shutdown.
     */
    private ConcurrentLinkedQueue<RequestThread> requestThreads;

    /**
     * The ClientHandler thread reads all incoming requests. It fulfills some
     * immediately (like shutdown). Other requests are placed in appropriate
     * cues for action by RequestThreads.
     * This reference is used to end this thread nicley during a shutdown.
     */
    private ClientHandler clientHandlerThread;

    /**
     * The KeepAliveHandler thread which handles all incoming keep alive requests.
     * This reference is used to end this thread nicley during a shutdown.
     */
    private KeepAliveHandler keepAliveThread;

    /** Current number of temporary normal request-handling threads. */
    private AtomicInteger tempThreads = new AtomicInteger();

    /** A pool of threads to execute all the subscribeAndGet calls which come in. */
    private ThreadPoolExecutor subAndGetThreadPool;

    /** A pool of threads to execute all the sendAndGet calls which come in. */
    private ThreadPoolExecutor sendAndGetThreadPool;

    /** Keep track of whether the shutdown method of this object has already been called. */
    AtomicBoolean calledShutdown = new AtomicBoolean();

    /**
     * Keep track of whether the handleShutdown method of the subdomain
     * handler has already been called.
     */
    private AtomicBoolean calledSubdomainShutdown = new AtomicBoolean();

    /**
     * Hashtable of all sendAndGetter objects of this client.
     */
    private ConcurrentHashMap<Integer, cMsgServerSendAndGetter> sendAndGetters;

    /**
     * Set of all subscriptions (including the subscribeAndGets) of all clients
     * on this server. This is mutex protected by {@link #subscribeLock}.
     */
    static private HashSet<cMsgServerSubscribeInfo> subscriptions =
            new HashSet<cMsgServerSubscribeInfo>(100);

    /** Lock to ensure all access to {@link #subscriptions} is sequential. */
    static final ReentrantLock subscribeLock = new ReentrantLock();

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
            permanentCommandHandlingThreads = 1;
            tempThreadsMax = 0;
        }
    }


    /**
     * Constructor.
     *
     * @param handler object which handles all requests from the client for a particular subdomain
     * @param info object containing information about the client for which this
     *                    domain server was started
     * @param startingPort suggested port on which to starting listening for connections
     * @throws cMsgException if listening socket could not be opened or a port to listen on could not be found
     */
    public cMsgDomainServer(cMsgSubdomainInterface handler, cMsgClientInfo info, int startingPort)
            throws cMsgException {

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
        sendAndGetters = new ConcurrentHashMap<Integer, cMsgServerSendAndGetter>(10);

        // Start a thread pool for subscribeAndGet handling.
        class RejectHandler implements RejectedExecutionHandler {
            public void rejectedExecution(Runnable r, ThreadPoolExecutor executor) {
                // Just run a new thread
System.out.println("REJECT HANDLER: DS start new sendAndGet/subAndGet thread");
                Thread t = new Thread(r);
                t.setDaemon(true);
                t.start();
            }
        }
        // Run up to 5 threads with no queue. Wait 1 min before terminating
        // extra (more than 5) unused threads. Overflow tasks spawn independent
        // threads.
        subAndGetThreadPool =
                new ThreadPoolExecutor(5, 5, 60L, TimeUnit.SECONDS,
                                       new SynchronousQueue(),
                                       new RejectHandler());

        // Start a thread pool for sendAndGet handling.

        // Run up to 10 threads with no queue. Wait 1 min before terminating
        // extra (more than 3) unused threads. Overflow tasks spawn independent
        // threads.
        sendAndGetThreadPool =
                new ThreadPoolExecutor(5, 10, 60L, TimeUnit.SECONDS,
                                       new SynchronousQueue(),
                                       new RejectHandler());


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


    /** Method to gracefully shutdown this object's threads and clean things up. */
    synchronized void shutdown() {

/*
        if (info.isServer()) {
            System.out.println("\nDomain Server: SHUTDOWN run on client which is a server\n");
        }
        else {
            System.out.println("\nDomain Server: SHUTDOWN run on client which is NOT a server\n");
        }
*/


        // tell subdomain handler to shutdown
        if (calledSubdomainShutdown.compareAndSet(false,true)) {
            try {subdomainHandler.handleClientShutdown();}
            catch (cMsgException e) {
            }
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

        // Unsubscribe bridges from all subscriptions if regular client.
        // (Server clients have no subscriptions passed on to other servers
        //  as this would result in infinite loops.)
        if (!info.isServer()) {

            // Protect table of subscriptions
            subscribeLock.lock();

            try {
                // allow no changes to "bridges" while iterating
                synchronized (cMsgNameServer.bridges) {
                    // foreach bridge ...
                    for (cMsgServerBridge b : cMsgNameServer.bridges.values()) {
                        // only cloud members please
                        if (b.getCloudStatus() != cMsgNameServer.INCLOUD) {
                            continue;
                        }

                        // foreach of this client's subscriptions, unsubscribe
                        for (cMsgServerSubscribeInfo sub : subscriptions) {
                            if (sub.info != info) {
//System.out.println("    **** Forgetting unsubscribing for " + info.getName());
                                continue;
                            }

                            try {
                                if (sub.isSubscribed()) {
System.out.println("    **** unsubscribing to sub/type = " + sub.subject + "/" + sub.type + " on " +
      b.server + " from " + sub.info.getName());
                                    b.unsubscribe(sub.subject, sub.type, sub.namespace);
                                }

                                for (Map.Entry<Integer, cMsgCallbackAdapter> entry : sub.getSubAndGetters().entrySet()) {
System.out.println("    **** unsubAndGetting to sub/type = " + sub.subject + "/" + sub.type + " on " +
   b.server);
                                    b.unsubscribeAndGet(sub.subject, sub.type,
                                                        sub.namespace, entry.getValue());
                                }
                            }
                            catch (cMsgException e) {
                            }
                        }
                    }
                }

                // remove this client's subscriptions
                cMsgServerSubscribeInfo sub = null;
                for (Iterator it = subscriptions.iterator(); it.hasNext();) {
                    sub = (cMsgServerSubscribeInfo) it.next();
                    if (sub.info == info) {
System.out.println("    **** Removing subs of " + info.getName() + " from subscriptions");
                        it.remove();
                    }
                }
            }
            finally {
                subscribeLock.unlock();
            }
        }
        // else if client is a server ...
        //
        // When server which is connected to this one dies, our bridge to that server is now
        // a useless object and must be removed from "bridges" and "nameServers". The easy way
        // to do that is to realize that for every bridge to a server, there is a reciprocal
        // bridge from that server to this one. That bridge is a client of this server. When
        // it dies, its corresponding cMsgMonitorClient thread detects its death and runs this
        // method. We can use this to remove the useless object.
        else {
            // remove client from "bridges" (hashmap is concurrent)
            cMsgServerBridge b = cMsgNameServer.bridges.remove(info.getName());

            // remove client from "nameServers" (hashset is synchronized)
            boolean removed = cMsgNameServer.nameServers.remove(info.getName());

            if (debug >= cMsgConstants.debugInfo) {
                if (b != null && removed) {
                    System.out.println(">>    DS: DELETED server client FROM BRIDGES AND NAMESERVERS");
                }
                else {
                    System.out.println(">>    DS: COULD NOT DELETE Client FROM BRIDGES AND/OR NAMESERVERS");
                }
            }
        }

        // shutdown the threads in pools used for subscribeAndGet & sendAndGets
        subAndGetThreadPool.shutdownNow();
        sendAndGetThreadPool.shutdownNow();

//System.out.println("\nDomain Server: EXITING SHUTDOWN\n");
    }


    /**
     * This method is a thread which listens for TCP connections from the client.
     * There are 3 connections from each client. The first is a socket for
     * communication from this server back to the client and is used to send
     * responses to various client requests. The second socket is for keep alives
     * to be sent to the server. The third is for client request to be sent to
     * this server.
     * */
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

                        // The 1st connection is for responses to certain client requests
                        if (connectionNumber == 1) {
                            backToClient = new DataOutputStream(new BufferedOutputStream(
                                                                channel.socket().getOutputStream(), 2048));
                        }
                        // The 2nd connection is for a keep alive thread
                        else if (connectionNumber == 2) {
                            keepAliveThread = new KeepAliveHandler(channel);
                        }
                        // The 3rd connection is for a client request handling thread
                        else if (connectionNumber == 3) {
                            clientHandlerThread = new ClientHandler(channel);
                        }

                        connectionNumber++;

                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println(">>    DS: new connection from " +
                                               info.getName());
                        }
                    }
                    // remove key from selected set since it's been handled
                    it.remove();
                }
            }
        }
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

                in  = new DataInputStream(new BufferedInputStream(channel.socket().getInputStream(), 65536));
                out = new DataOutputStream(new BufferedOutputStream(channel.socket().getOutputStream(), 2048));

                while (true) {
                    // read client's request
                    msgId = in.readInt();
                    if (msgId != cMsgConstants.msgKeepAlive) {
                        throw new cMsgException("Wrong request, expecting keep alive but got " + msgId);
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

        /** Input stream from client socket. */
        private DataInputStream  in;

        // variables to track message rate
        //double freq=0., freqAvg=0.;
        //long t1, t2, deltaT, totalT=0, totalC=0, count=0, loops=10000, ignore=5;


        /** Constructor. */
        ClientHandler(SocketChannel channel) {
            this.channel = channel;

            // start up "normal", permanent worker threads on the regular cue
            for (int i = 0; i < permanentCommandHandlingThreads; i++) {
                requestThreads.add(new RequestThread(true, NORMAL));
            }

            // start up 1 and only 1 permanent worker thread on the (un)subscribe cue
            requestThreads.add(new RequestThread(true, SUBSCRIBE));

            // start up 1 and only 1 permanent worker thread on the (un)lock cue
            requestThreads.add(new RequestThread(true, LOCK));

            // die if no more non-daemon thds running
            setDaemon(true);
            
            start();
        }


        /**
         * This method handles all communication between a cMsg user who has
         * connected to a domain and this server for that domain.
         */
        public void run() {
            int size, msgId=0, requestType=NORMAL;
            cMsgHolder holder = null;
            // for printing out request cue size periodically
            /*
            Date now, t;
            now = new Date();
            */

            try {
                // Use select on socket and pipe

                // buffered communication streams for efficiency
                in = new DataInputStream(new BufferedInputStream(channel.socket().getInputStream(), 65536));

                here:
                while (true) {
                    if (killSpawnedThreads) return;

                    // read first int
                    size = in.readInt();
//System.out.println("DS Read in size = " + size);

                    // read client's request
                    msgId = in.readInt();
//System.out.println("DS Read in msgId = " + msgId);

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
                    holder = null;
                    requestType = NORMAL;

                    switch (msgId) {

                        case cMsgConstants.msgSendRequest: // client sending msg
                        case cMsgConstants.msgSyncSendRequest:
                            holder = readSendInfo();
                            break;

                        case cMsgConstants.msgSubscribeAndGetRequest: // 1-shot subscription of subject & type
                        case cMsgConstants.msgUnsubscribeAndGetRequest: // ungetting subscribeAndGet request
                            holder = readSubscribeInfo();
                            break;

                        case cMsgConstants.msgSendAndGetRequest: // sending msg & expecting response msg
                        case cMsgConstants.msgServerSendAndGetRequest:
                            holder = readGetInfo();
                            break;

                        case cMsgConstants.msgUnSendAndGetRequest: // ungetting sendAndGet request
                        case cMsgConstants.msgServerUnSendAndGetRequest:
                            holder = readUngetInfo();
                            break;

                        case cMsgConstants.msgSubscribeRequest:         // subscribing to subject & type
                        case cMsgConstants.msgServerSubscribeRequest:   // server subscribing to subject & type
                        case cMsgConstants.msgUnsubscribeRequest:       // unsubscribing from a subject & type
                        case cMsgConstants.msgServerUnsubscribeRequest: // server unsubscribing from a subject & type
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
                        case cMsgConstants.msgServerCloudLock: // grab lock for server cloud joining
                            // Grabbing this lock may take up to 1/2 second so
                            // pass along to thread pool so as not to block server.
                            int delay = in.readInt();
                            holder = new cMsgHolder();
                            holder.delay = delay;
                            requestType = LOCK;
                            break;

                        case cMsgConstants.msgServerRegistrationUnlock: // release lock for client registration
                        case cMsgConstants.msgServerCloudUnlock: // release lock for server cloud joining
                            holder = new cMsgHolder();
                            requestType = LOCK;
                            break;

                        case cMsgConstants.msgServerCloudSetStatus: // server client is joining cMsg subdomain server cloud
                            int status = in.readInt();
                            setCloudStatus(status);
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
//System.out.println("SHUTDOWN TO BE RUN due to unknown command being received");
                                shutdown();
                            }
                            return;
                    }

                    // if we got something to put on a cue, do it
                    if (holder != null) {
                        holder.request = msgId;
                        try {
                            if (requestType == NORMAL) {
                                requestCue.put(holder);
                            }
                            else if (requestType == SUBSCRIBE) {
                                subscribeCue.put(holder);
                            }
                            else {
                                lockCue.put(holder);
                            }
                        }
                        catch (InterruptedException e) {
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

                    // if the cue is almost full, add temp threads to handle the load
                    if (requestCue.remainingCapacity() < 5 && tempThreads.get() < tempThreadsMax) {
                        new RequestThread();
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
         * This method changes the status of a bridge. Currently it is used to set the
         * status of another cMsg domain server to "INCLOUD". All subscriptions and
         * subscribeAndGet calls still active are propagated to the newly joined serer.
         *
         * @param status status to set the bridge to (only {@link cMsgNameServer#INCLOUD} allowed)
         */
        private void setCloudStatus(int status) {

            // Currently setting status is only used for joining the cloud
            if (status != cMsgNameServer.INCLOUD) {
                return;
            }

            // Get the bridge to a server trying to add itself to the cloud
            cMsgServerBridge bridge = cMsgNameServer.bridges.get(info.getName());

            // In order to reach this point, the server joining the cloud has
            // grabbed all in-cloud members' cloud locks - including this server.

            // We actually do NOT care if servers added to "bridges" collection
            // during iterations (it's concurrent). We only care if INCLOUD
            // members are added during such.

            // Protect table of subscriptions. We cannot have servers joining the cloud
            // while subscribing or subAndGetting since a joining server needs this server
            // to update it with all the local subs and subAndGets. Once it's updated, then
            // it will get all the new subs and subAndGets.
            subscribeLock.lock();
            try {
                if (bridge != null) {
System.out.println(">>    DS: " + bridge.server + " has joined the cloud");
                    bridge.setCloudStatus(status);
                }
                // if bridge can't be found, try alternate name first
                else {
                    String alternateName = null;
                    String name = info.getName();
                    String sPort = name.substring(name.lastIndexOf(":") + 1);
                    int index = name.indexOf(".");

                    // If the name has a dot (is qualified), create unqualified name
                    if (index > -1) {
                        alternateName = name.substring(0, index) + ":" + sPort;
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
                            if (debug >= cMsgConstants.debugError) {
                                System.out.println("    DS: cannot find bridge to server " +
                                                   bridge.server);
                            }
                            return;
                        }
                    }
                    bridge = cMsgNameServer.bridges.get(alternateName);
                    if (bridge != null) {
System.out.println(">>    DS: " + bridge.server + " has joined the cloud");
                        bridge.setCloudStatus(status);
                    }
                    else {
                        if (debug >= cMsgConstants.debugError) {
                            System.out.println("    DS: cannot find bridge to server " +
                                               bridge.server);
                        }
                        return;
                    }
                }

System.out.println(">>    DS: size of subscriptions = " + subscriptions.size());
                // update the new "INCLOUD" bridge with all cMsg domain subscriptions
                for (cMsgServerSubscribeInfo sub : subscriptions) {
                    try {
                        if (sub.isSubscribed()) {
System.out.println("subscribing to sub/type = " + sub.subject + "/" + sub.type + "/" +
   sub.namespace + " on " + bridge.server + " from " + sub.info.getName());
                            bridge.subscribe(sub.subject, sub.type, sub.namespace);
                        }

                        for (Map.Entry<Integer, cMsgCallbackAdapter> entry : sub.getSubAndGetters().entrySet()) {
System.out.println("subAndGetting to sub/type = " + sub.subject + "/" + sub.type + "/" +
   sub.namespace + " on " +   bridge.server + " from " + sub.info.getName());
                            bridge.subscribeAndGet(sub.subject, sub.type,
                                                   sub.namespace, entry.getValue());
                        }
                    }
                    catch (cMsgException e) {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("dServer requestThread: cannot subscribe with server " +
                                               bridge.server);
                            e.printStackTrace();
                        }
                    }
                }
            }
            finally {
                subscribeLock.unlock();
            }
        }


        /**
         * This method returns a list of local client names to the client (remote server).
         *
         * @throws java.io.IOException If socket read or write error
         */
        private void sendClientNames(String[] names) throws IOException {
            // send number of items to come
//System.out.println(">>    DS: write # of names = " + names.length);
            backToClient.writeInt(names.length);

//System.out.println(">>    DS: send " + names.length + " client names");
            // send lengths of strings
            for (int i=0; i < names.length; i++) {
//System.out.println(">>    DS: send client name len = " + names[i].length());
                backToClient.writeInt(names[i].length());
            }

            // send strings
            try {
                for (int i=0; i < names.length; i++) {
//System.out.println(">>    DS: send client name = " + names[i]);
                    backToClient.write(names[i].getBytes("US-ASCII"));
                }
            }
            catch (UnsupportedEncodingException e) {}
//System.out.println(">>    DS: done writing names");
            backToClient.flush();
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

            int lengthSubject   = in.readInt();
            int lengthType      = in.readInt();
            int lengthNamespace = in.readInt();
            int lengthCreator   = in.readInt();
            int lengthText      = in.readInt();
            int lengthBinary    = in.readInt();

            // string bytes expected
            int stringBytesToRead = lengthSubject + lengthType + lengthNamespace +
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

            // read namespace
            String ns = null;
//System.out.println("DS readSubscribeInfo: ns length = " + lengthNamespace);
            if (lengthNamespace > 0) {
                ns = new String(bytes, offset, lengthNamespace, "US-ASCII");
                offset += lengthNamespace;
            }

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

            cMsgHolder h = new cMsgHolder(msg);
            h.namespace = ns;
            return h;
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
            // length of namespace
            int lengthNamespace = in.readInt();
            // bytes expected
            int bytesToRead = lengthSubject + lengthType + lengthNamespace;

            // read all string bytes
            if (bytesToRead > bytes.length) {
              bytes = new byte[bytesToRead];
            }
            in.readFully(bytes, 0, bytesToRead);

            // read subject
            holder.subject = new String(bytes, 0, lengthSubject, "US-ASCII");

            // read type
            holder.type = new String(bytes, lengthSubject, lengthType, "US-ASCII");

            // read namespace
            if (lengthNamespace > 0) {
                holder.namespace = new String(bytes, lengthSubject+lengthType, lengthNamespace, "US-ASCII");
            }

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


        /** Constructor for temporary thread to work on normal request cue. */
        RequestThread() {
            // thread is not permanent and only reads normal requests
            this(false, NORMAL);
        }

        /** General constructor. */
        RequestThread(boolean permanent, int requestType) {
            this.permanent   = permanent;
            this.requestType = requestType;

System.out.println("DS Start new request handling thread");
            if (!permanent) {
                tempThreads.getAndIncrement();
                // die if main thread dies
                setDaemon(true);
            }
            this.start();
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
             backToClient.writeInt(answer);
             backToClient.flush();
//System.out.println("    sent int");
         }

        /** Loop forever waiting for work to do. */
        public void run() {
            cMsgHolder holder;
            int answer;

            /*
            if (requestType == NORMAL)
                System.out.println("Starting up NORMAL request thread " + Thread.currentThread());
            else if (requestType == SUBSCRIBE)
                System.out.println("Starting up SUBSCRIBE request thread " + Thread.currentThread());
            else
                System.out.println("Starting up LOCK request thread " + Thread.currentThread());
            */

            while (true) {

                if (killSpawnedThreads) return;

                holder = null;
                cMsgSubscription sub = null;

                try {
                    // try for up to 1/2 second to read a request from the cue
                    if (requestType == NORMAL) {
//System.out.println("WAITING FOR REQUESTS in " + Thread.currentThread());
                        holder = requestCue.poll(500, TimeUnit.MILLISECONDS);
                    }
                    else if (requestType == SUBSCRIBE) {
//System.out.println("WAITING FOR SUBSCRIBES in " + Thread.currentThread());
                        holder = subscribeCue.poll(500, TimeUnit.MILLISECONDS);
                    }
                    else {
//System.out.println("WAITING FOR LOCKS in " + Thread.currentThread());
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
                            answer = subdomainHandler.handleSyncSendRequest(holder.message);
                            sendIntReply(answer);
 //System.out.println("Got sync send answer");
                            break;

                        case cMsgConstants.msgSendAndGetRequest: // sending a message to a responder
//System.out.println("Domain Server: got msgSendAndGetRequest from client, ns = " + holder.namespace);
                            // If not cMsg subdomain just call subdomain handler.
                            //if (!(subdomainHandler instanceof org.jlab.coda.cMsg.subdomains.cMsg)) {
                            if (cMsgSubdomainHandler == null) {
//System.out.println("Domain Server: call regular send&Get");
                                subdomainHandler.handleSendAndGetRequest(holder.message);
                                break;
                            }
                            handleCmsgSubdomainSendAndGet(holder);
                            break;

                        case cMsgConstants.msgUnSendAndGetRequest: // ungetting sendAndGet
                            // This will fire notifier if one exists.
                            // The fired notifier will take care of unSendAndGetting any bridges.
//System.out.println("Domain Server: got msgUnSendAndGetRequest from client, ns = " + holder.namespace);
                            subdomainHandler.handleUnSendAndGetRequest(holder.id);
                            break;

                        case cMsgConstants.msgSubscribeAndGetRequest: // getting a message of subject & type
                            // if not cMsg subdomain, just call subdomain handler
                            if (cMsgSubdomainHandler == null) {
//System.out.println("Domain Server: call regular sub&Get");
                                subdomainHandler.handleSubscribeAndGetRequest(holder.subject,
                                                                              holder.type,
                                                                              holder.id);
                                break;
                            }
                            handleCmsgSubdomainSubscribeAndGet(holder);
                            break;

                        case cMsgConstants.msgUnsubscribeAndGetRequest: // ungetting subscribeAndGet
                            // this will fire notifier if one exists (cmsg subdomain)
                            subdomainHandler.handleUnsubscribeAndGetRequest(holder.subject,
                                                                            holder.type,
                                                                            holder.id);
                            // for cmsg subdomain
                            if (cMsgSubdomainHandler != null) {
                                handleCmsgSubdomainUnsubscribeAndGet(holder);
                            }
                            break;

                        case cMsgConstants.msgSubscribeRequest: // subscribing to subject & type
                            subdomainHandler.handleSubscribeRequest(holder.subject,
                                                                    holder.type,
                                                                    holder.id);
                            // for cmsg subdomain
                            if (cMsgSubdomainHandler != null) {
                                handleCmsgSubdomainSubscribe(holder);
                            }
                            break;

                        case cMsgConstants.msgUnsubscribeRequest: // unsubscribing from a subject & type
                            subdomainHandler.handleUnsubscribeRequest(holder.subject,
                                                                      holder.type,
                                                                      holder.id);
                            // for cmsg subdomain
                            if (cMsgSubdomainHandler != null) {
                                handleCmsgSubdomainUnsubscribe(holder);
                            }
                            break;

                        case cMsgConstants.msgServerSubscribeRequest: // subscription by another server
//System.out.println("Domain Server: got serverSubscribe for bridge client, namespace = " + holder.namespace);
                            cMsgSubdomainHandler.handleServerSubscribeRequest(holder.subject,
                                                                              holder.type,
                                                                              holder.namespace);
                            break;

                        case cMsgConstants.msgServerUnsubscribeRequest: // unsubscribing by another server
//System.out.println("Domain Server: got serverUNSubscribe for bridge client");
                            cMsgSubdomainHandler.handleServerUnsubscribeRequest(holder.subject,
                                                                                holder.type,
                                                                                holder.namespace);
                            break;

                        case cMsgConstants.msgServerSendAndGetRequest: // sendAndGet by another server
//System.out.println("Domain Server: got msgServerSendAndGetRequest from bridge client, ns = " + holder.namespace);
                            cMsgSubdomainHandler.handleServerSendAndGetRequest(holder.message,
                                                                               holder.namespace);
                            break;

                        case cMsgConstants.msgServerUnSendAndGetRequest: // unsubscribing by another server
//System.out.println("Domain Server: got msgServerUnSendAndGetRequest from bridge client, ns = " + holder.namespace);
                            cMsgSubdomainHandler.handleUnSendAndGetRequest(holder.id);
                            break;

                        case cMsgConstants.msgShutdown: // shutting down various clients and servers
                            subdomainHandler.handleShutdownRequest(holder.client, holder.server, holder.flag);
                            break;

                        case cMsgConstants.msgServerRegistrationUnlock: // release lock for client registration
                            cMsgSubdomainHandler.registrationUnlock();
                            break;

                        case cMsgConstants.msgServerRegistrationLock: // grab lock for global registration
                            // Send yes (1) to indicate lock grabbed , or no (0) back as return value.
                            // This may block for up to 1/2 second.
                            boolean gotLock = cMsgSubdomainHandler.registrationLock(holder.delay);
                            answer =  gotLock ? 1 : 0;
                            sendIntReply(answer);
                            break;

                        case cMsgConstants.msgServerCloudUnlock: // release lock for server cloud joining
                            try {
//System.out.println("TRY TO UNLOCK CLOUD");
                                cMsgNameServer.cloudUnlock();
//System.out.println("UNLOCKED CLOUD");
                            }
                            catch (Exception e) {
                                System.out.println("CANNOT UNLOCK CLOUD");
                                e.printStackTrace();  //To change body of catch statement use File | Settings | File Templates.
                            }
                            break;

                        case cMsgConstants.msgServerCloudLock: // grab lock for server cloud joining
                            // Send yes (1) to indicate lock grabbed , or no (0) back as return value.
                            // This may block for up to 0.2 seconds.
                            //DataOutputStream out = new DataOutputStream(new BufferedOutputStream(holder.channel.socket().getOutputStream(), 2048));
//System.out.println("DOMAIN SERVER: Try to lock cloud ...");
                            gotLock = cMsgNameServer.cloudLock(holder.delay);
//System.out.println("DOMAIN SERVER:   gotLock = \"" + gotLock + "\", send reply");
                            answer = gotLock ? 1 : 0;
                            sendIntReply(answer);
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



        private void handleCmsgSubdomainSendAndGet(cMsgHolder holder) throws cMsgException {
            // If not in a cMsg server cloud, just call subdomain handler.
            if (cMsgNameServer.bridges.size() < 1) {
//System.out.println("Domain Server: call regular send&Get");
                subdomainHandler.handleSendAndGetRequest(holder.message);
                return;
            }

            cMsgCallbackAdapter cb = null;
            cMsgNotifier notifier  = null;
            holder.namespace = info.getNamespace();

            // We're in cMsg domain server cloud, so take care of connected server issues.
            //
            // First create an object (notifier) which will tell us if someone
            // has sent a matching message to our client. Then we can tell connected
            // servers to cancel the sendAndGet.
            notifier = new cMsgNotifier();
            notifier.id = holder.message.getSenderToken();
            notifier.latch = new CountDownLatch(1);
            notifier.client = info;

            // Do a local sendAndGet. This associates the notifier
            // object with this call.
            int smId = cMsgSubdomainHandler.handleServerSendAndGetRequest(holder.message,
                                                                          holder.namespace,
                                                                          notifier);
//System.out.println("Domain Server: called serverSub&GetRequest & got sysMsgId = " + smId);

//System.out.println("Domain Server: sendAndGet cb given id, sysMsgid =  " +
//                   holder.message.getSenderToken() + ", " + smId);
            cb = cMsgServerBridge.getSendAndGetCallback(holder.message.getSenderToken(),
                                                        smId);

            // Run thd that waits for notifier and cleans up afterwards
            cMsgServerSendAndGetter getter =
                    new cMsgServerSendAndGetter(notifier, cb, sendAndGetters);
            sendAndGetters.put(notifier.id, getter);
            sendAndGetThreadPool.execute(getter);

            for (cMsgServerBridge b : cMsgNameServer.bridges.values()) {
                // Only deal with cloud members
                if (b.getCloudStatus() != cMsgNameServer.INCLOUD) {
                    continue;
                }

                try {
                    // if (local) message already arrived, bail out
                    if (notifier.latch.getCount() < 1) break;
                    // This sendAndGet will pass on a received message by calling subdomain
                    // handler object's "bridgeSend" method which will, in turn,
                    // fire off the notifier. The notifier was associated with
                    // this sendAndGet by the above calling of the
                    // "handleServerSendAndGetRequest" method.
//System.out.println("Domain Server: call bridge sendAndGet for " + b.server);
                    b.sendAndGet(holder.message, holder.namespace, cb);
                }
                catch (cMsgException e) {
                    if (debug >= cMsgConstants.debugWarn) {
                        System.out.println("dServer requestThread: cannot sendAndGet with server " +
                                           b.server);
                        e.printStackTrace();
                    }
                }
            }

        }



        private void handleCmsgSubdomainSubscribeAndGet(cMsgHolder holder) throws cMsgException {

            cMsgCallbackAdapter cb = null;
            cMsgNotifier notifier  = null;
            cMsgServerSubscribeInfo sub = null;
            holder.namespace = info.getNamespace();

            // Can't have bridges joining cloud while (un)subscribeAndGetting
            subscribeLock.lock();
            try {
                // If we're in cMsg subdomain, take care of connected server issues.
                //
                // First create an object (notifier) which will tell us if someone
                // has sent a matching message to our client. Then we can tell connected
                // servers to cancel the order (subscription). We can also clean up
                // entries in the hashtable storing subscription info.
                notifier = new cMsgNotifier();
                notifier.id = holder.id;
                notifier.latch = new CountDownLatch(1);
                notifier.client = info;

                cb = cMsgServerBridge.getSubAndGetCallback();

                // Here we use "subscribe" to implement a "subscribeAndGet" for other servers
                for (cMsgServerBridge b : cMsgNameServer.bridges.values()) {
                    // Only deal with cloud members
                    if (b.getCloudStatus() != cMsgNameServer.INCLOUD) {
                        continue;
                    }

                    try {
                        // if message already arrived, bail out
                        if (notifier.latch.getCount() < 1) break;
                        // This subscribe will pass on a received message by calling subdomain
                        // handler object's "bridgeSend" method which will, in turn,
                        // fire off the notifier. The notifier was associated with
                        // this subscription by the above calling of the
                        // "handleServerSubscribeAndGetRequest" method.
//System.out.println("Domain Server: call bridge subscribe for " + b.server);
                        b.subscribeAndGet(holder.subject, holder.type,
                                          holder.namespace, cb);
                    }
                    catch (cMsgException e) {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("dServer requestThread: cannot subscribe with server " +
                                               b.server);
                            e.printStackTrace();
                        }
                    }
                }

                // Do a local subscribeAndGet. This associates the notifier
                // object with the subscription. The call to this method MUST COME
                // AFTER the bridges' subscribeAndGets. If not then there is a race
                // condition in which subscribes and unsubscribes get out of order.
//System.out.println("Domain Server: call serverSub&GetRequest with id = " + holder.id);
                cMsgSubdomainHandler.handleServerSubscribeAndGetRequest(holder.subject,
                                                                        holder.type,
                                                                        notifier);

                // Keep track of this subscribeAndGet (just like we do in the cMsg
                // subdomain handler object) so later, if another server joins the
                // cloud, we can tell that server about the subscribeAndGet.
                boolean subscriptionExists = false;
                for (Iterator it = subscriptions.iterator(); it.hasNext();) {
                    sub = (cMsgServerSubscribeInfo) it.next();
                    if (sub.info == info &&
                            sub.namespace.equals(info.getNamespace()) &&
                            sub.subject.equals(holder.subject) &&
                            sub.type.equals(holder.type)) {

                        // found existing subscription so add this client to its list
                        subscriptionExists = true;
                        break;
                    }
                }

                // add this client to an exiting subscription
                if (subscriptionExists) {
//System.out.println("    DS: add sub&Get with id = " + holder.id);
                    sub.addSubAndGetter(holder.id, cb);
                }
                // or else create a new subscription
                else {
//System.out.println("    DS: create subscribeInfo & add sub&Get with id = " + holder.id);
                    sub = new cMsgServerSubscribeInfo(holder.subject, holder.type,
                                                      holder.namespace, info,
                                                      holder.id, cb);
                    subscriptions.add(sub);
                }
if (subscriptions.size() > 10 && subscriptions.size() % 10 == 0) {
    System.out.println("sub size = " + subscriptions.size());
}
            }
            finally {
                subscribeLock.unlock();
            }

            // Run thd that waits for notifier and cleans up server subscriptions
            cMsgServerSubscribeAndGetter getter =
                        new cMsgServerSubscribeAndGetter(notifier, holder, cb,
                                                         subscriptions, sub);
            subAndGetThreadPool.execute(getter);
        }


        private void handleCmsgSubdomainUnsubscribeAndGet(cMsgHolder holder) throws cMsgException {
            // Cannot have servers joining cloud while a subscription is removed
            subscribeLock.lock();
            try {
                cMsgServerSubscribeInfo sub = null;
                // keep track of all subscriptions removed by this client
                for (Iterator it = subscriptions.iterator(); it.hasNext();) {
                    sub = (cMsgServerSubscribeInfo) it.next();
                    if (sub.info == info &&
                            sub.namespace.equals(info.getNamespace()) &&
                            sub.subject.equals(holder.subject) &&
                            sub.type.equals(holder.type)) {

//System.out.println("    DS: removing sub&Get with id = " + holder.id);
                        sub.removeSubAndGetter(holder.id);
                        break;
                    }
                }
                // get rid of this subscription if no more subscribers left
                if (sub.numberOfSubscribers() < 1) {
//System.out.println("    DS: removing sub object for sub&Get");
                    subscriptions.remove(sub);
                }
//if (subscriptions.size() > 100 && subscriptions.size()%100 == 0) {
//    System.out.println("sub size = " + subscriptions.size());
//}
            }
            finally {
                subscribeLock.unlock();
            }
        }



        private void handleCmsgSubdomainSubscribe(cMsgHolder holder) throws cMsgException {
//System.out.println("Domain Server: got subscribe for reg client " + holder.subject + " " + holder.type);
            holder.namespace = info.getNamespace();

            // Cannot have servers joining cloud while a subscription is added
            subscribeLock.lock();
            try {
                cMsgServerSubscribeInfo sub = null;
                // Regular client is subscribing to sub/type.
                // Pass this on to any cMsg subdomain bridges.
                if (cMsgNameServer.bridges.size() > 0) {
                    for (cMsgServerBridge b : cMsgNameServer.bridges.values()) {
//System.out.println("Domain Server: call bridge subscribe");
                        // only cloud members please
                        if (b.getCloudStatus() != cMsgNameServer.INCLOUD) {
                            continue;
                        }
                        try {
                            b.subscribe(holder.subject, holder.type, holder.namespace);
                        }
                        catch (cMsgException e) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("dServer requestThread: cannot subscribe with server " +
                                                   b.server);
                                e.printStackTrace();
                            }
                        }
                    }
                }

                // Keep track of all subscriptions/sub&Gets made by this client.
                boolean subExists = false;
                for (Iterator it = subscriptions.iterator(); it.hasNext();) {
                    sub = (cMsgServerSubscribeInfo) it.next();
                    if (sub.info == info &&
                            sub.namespace.equals(holder.namespace) &&
                            sub.subject.equals(holder.subject) &&
                            sub.type.equals(holder.type)) {

                        subExists = true;
                        break;
                    }
                }

                // add this client to an exiting subscription
                if (subExists) {
                    // this will happen if subscribeAndGet preceeds a subscribe
//System.out.println("    DS: add subscribe to existing subscription");
                    sub.addSubscription();
                }
                // or else create a new subscription
                else {
//System.out.println("    DS: create subscribeInfo & add subscribe with sub/type/ns = " +
//   holder.subject + "/" + holder.type + "/" + holder.namespace);
                    sub = new cMsgServerSubscribeInfo(holder.subject, holder.type,
                                                holder.namespace, info);
                    subscriptions.add(sub);
                }
            }
            finally {
                subscribeLock.unlock();
            }
//System.out.println("    DS: size of subscription = " + subscriptions.size());

        }


        private void handleCmsgSubdomainUnsubscribe(cMsgHolder holder) throws cMsgException {
//System.out.println("Domain Server: got UNSubscribe for bridge client");
            // Cannot have servers joining cloud while a subscription is removed
            subscribeLock.lock();
            try {
                cMsgServerSubscribeInfo sub = null;
                // Regular client is unsubscribing to sub/type.
                // Pass this on to any cMsg subdomain bridges.
                if (cMsgNameServer.bridges.size() > 0) {
                    for (cMsgServerBridge b : cMsgNameServer.bridges.values()) {
//System.out.println("Domain Server: call bridge unsubscribe");
                        // only cloud members please
                        if (b.getCloudStatus() != cMsgNameServer.INCLOUD) {
                            continue;
                        }
                        b.unsubscribe(holder.subject, holder.type, info.getNamespace());
                    }
                }

                // keep track of all subscriptions removed by this client
                for (Iterator it = subscriptions.iterator(); it.hasNext();) {
                    sub = (cMsgServerSubscribeInfo) it.next();
                    if (sub.info == info &&
                            sub.namespace.equals(info.getNamespace()) &&
                            sub.subject.equals(holder.subject) &&
                            sub.type.equals(holder.type)) {

//System.out.println("    DS: removing subscribe with sub/type/ns = " +
//   holder.subject + "/" + holder.type + "/" + info.getNamespace());
                        sub.removeSubscription();
                        break;
                    }
                }
                // get rid of this subscription if no more subscribers left
                if (sub.numberOfSubscribers() < 1) {
//System.out.println("    DS: removing sub object for subscribe");
                    subscriptions.remove(sub);
                }
            }
            finally {
                subscribeLock.unlock();
            }
        }


    }


}


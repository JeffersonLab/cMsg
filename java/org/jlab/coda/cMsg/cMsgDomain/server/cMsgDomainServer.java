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
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/


package org.jlab.coda.cMsg.cMsgDomain.server;

import java.io.*;
import java.net.*;
import java.lang.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.nio.channels.*;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.*;


/**
 * Domain Server which services a number of clients through select and nonblocking sockets.
 */
class cMsgDomainServer extends Thread {

    /** Type of domain this is. */
    static String domainType = "cMsg";

    /** Reference back to object that created this object. */
    private cMsgNameServer nameServer;

    /** Level of debug output. */
    private int debug;

    /** Maximum number of temporary request-handling threads allowed per client connection. */
    private int tempThreadsMax = 3;

    /**
     * Number of permanent request-handling threads per client. This is in
     * addition to the thread which handles sequential requests.
     * There should be at least two (2). If there are requests which block
     * (say sending a message to a client with full received msg queue)
     * there will be at least one thread to handle such while the
     * other handles other requests.
     */
    private int permanentCommandHandlingThreads = 1;

    /**
     * Object containing information about the client this object is connected to.
     * Certain members of info can only be filled in by this thread,
     * such as the listening port.
     */
    cMsgClientData info;

    /** Input stream from client. */
    private DataInputStream  in;

    /** Boolean specifying if this object creates and listens on a udp socket. */
    private boolean noUdp;

    /** Socket to receive UDP sends from the client. */
    private DatagramSocket udpSocket;

    /**
     * Thread-safe queue to hold cMsgHolder objects of
     * requests from the client (except for subscribes, unsubscribes, & locks).
     * These are grabbed and processed by waiting worker threads.
     */
    private LinkedBlockingQueue<cMsgHolder> requestQueue;

    /**
     * Thread-safe queue to hold cMsgHolder objects of subscribe, unsubscribe,
     * lock and unlock requests from the client - in fact, any requests needing
     * to be processed sequentially. These are then grabbed and processed by a
     * single worker thread.
     */
    private LinkedBlockingQueue<cMsgHolder> sequentialQueue;

    /** Request thread is of "normal" request types. */
    static private final int NORMAL = 0;

    /** Request thread is of sequential request types. */
    static private final int SEQUENTIAL = 1;

    /**
     * Thread-safe list of request handling Thread objects. This cue is used
     * to end these threads nicely during a shutdown.
     */
    private ConcurrentLinkedQueue<Thread> requestThreads;

    /**
     * The ClientHandler thread reads all incoming requests. It fulfills some
     * immediately while other requests are placed in appropriate queues
     * for action by RequestThreads.
     * This reference is used to end this thread nicley during a shutdown.
     */
    private ClientHandler clientHandlerThread;

    /**
     * The UdpSendHandler thread reads all incoming UDP sends which are placed
     * in the appropriate queue for action by RequestThreads.
     * This reference is used to end this thread nicley during a shutdown.
     */
    private UdpSendHandler udpHandlerThread;

    /** Current number of temporary, normal, request-handling threads. */
    private AtomicInteger tempThreads = new AtomicInteger();

    /** A pool of threads to execute all the server subscribeAndGet calls which come in. */
    private ThreadPoolExecutor subAndGetThreadPool;

    /** A pool of threads to execute all the server sendAndGet calls which come in. */
    private ThreadPoolExecutor sendAndGetThreadPool;

    /** Keep track of whether the shutdown method of this object has already been called. */
    AtomicBoolean calledShutdown = new AtomicBoolean();

    /** Hashtable of all sendAndGetter objects of this client. */
    private ConcurrentHashMap<Integer, cMsgServerSendAndGetter> sendAndGetters;

    /** Kill all spawned threads if true. */
    volatile boolean killSpawnedThreads;

    /** Kill spawned threads. */
    public void killSpawnedThreads() {
        killSpawnedThreads = true;
    }

    /**
     * Getter for the UDP port being used.
     * @return UDP port being used; if none used, return -1
     */
    public int getUdpPort() {
        if (noUdp)
            return -1;
        return udpSocket.getLocalPort();
    }

    /**
     * Getter for the subdomain handler object.
     * @return subdomain handler object
     */
    public cMsgSubdomainInterface getSubdomainHandler() {
        return info.subdomainHandler;
    }



    /** This method prints out the sizes of all objects which store other objects. */
    private void printSizes() {
        System.out.println("\n\nSIZES:");
        System.out.println("     request cue    = " + requestQueue.size());
        System.out.println("     sequential cue = " + sequentialQueue.size());
        System.out.println("     sendAndGetters = " + sendAndGetters.size());

        System.out.println();

        nameServer.printSizes();

        // print static stuff for cMsg subdomain class
        org.jlab.coda.cMsg.cMsgDomain.subdomains.cMsg.printStaticSizes();

        System.out.println();

        // print sizes for our specific cMsg subdomain handler
        if (info.cMsgSubdomainHandler != null) {
            info.cMsgSubdomainHandler.printSizes();
        }
    }


    /**
     * Constructor.
     *
     * @param nameServer nameServer object which created (is creating) this object
     * @param info object containing client data
     * @param noUdp  if true, do not create and listen on a udp socket.
     * @param debug  level of debug output.
     * 
     * @throws cMsgException if UDP listening socket could not be opened
     */
    public cMsgDomainServer(cMsgNameServer nameServer, cMsgClientData info,
                            boolean noUdp, int debug) throws cMsgException {


//System.out.println("Creating cMsgDomainServer");
        this.info = info;
        this.noUdp = noUdp;
        this.debug = debug;
        this.nameServer = nameServer;

        requestQueue = new LinkedBlockingQueue<cMsgHolder>(100);
        sequentialQueue = new LinkedBlockingQueue<cMsgHolder>(50);

        requestThreads = new ConcurrentLinkedQueue<Thread>();
        sendAndGetters = new ConcurrentHashMap<Integer, cMsgServerSendAndGetter>(10);

        // Start a thread pool for subscribeAndGet handling.
        class RejectHandler implements RejectedExecutionHandler {
            public void rejectedExecution(Runnable r, ThreadPoolExecutor executor) {
                // Just run a new thread
                Thread t = new Thread(r);
                t.setDaemon(true);
                t.start();
            }
        }
        // Run up to 5 threads with no queue. Wait 1 min before terminating
        // extra (more than 0) unused threads. Overflow tasks spawn independent
        // threads.
        subAndGetThreadPool =
                new ThreadPoolExecutor(0, 5, 60L, TimeUnit.SECONDS,
                                       new SynchronousQueue<Runnable>(),
                                       new RejectHandler());

        // Start a thread pool for sendAndGet handling.

        // Run up to 5 threads with no queue. Wait 1 min before terminating
        // extra (more than 0) unused threads. Overflow tasks spawn independent
        // threads.
        sendAndGetThreadPool =
                new ThreadPoolExecutor(0, 5, 60L, TimeUnit.SECONDS,
                                       new SynchronousQueue<Runnable>(),
                                       new RejectHandler());

        // For the client who wants to do sends with udp,
        // create a socket on an available udp port.
        if (!noUdp) {
            try {
                // Create socket to receive at all interfaces
                udpSocket = new DatagramSocket();
                udpSocket.setReceiveBufferSize(cMsgNetworkConstants.biggestUdpBufferSize);
//System.out.println("CREATED datagram socket at port " + udpSocket.getLocalPort());
            }
            catch (IOException ex) {
                ex.printStackTrace();
                cMsgException e = new cMsgException("Exiting Server: cannot create UDP listening socket");
                e.setReturnCode(cMsgConstants.errorSocket);
                throw e;
            }

            info.setDomainUdpPort(udpSocket.getLocalPort());
        }
    }



    /**
     * Start reading and writing over the sockets. Start threads to process client requests.
     *
     * @throws IOException if streams to & from client cannot be opened
     */
    void startThreads() throws IOException {

        // Finish making the "deliverer" object. Use this channel
        // to communicate back to the client. Channel set to blocking.
        info.getDeliverer().createClientConnection(info.getMessageChannel(), true);

        // We want to use streams for communication with the client.
        // They are faster and easier to use than nonblocking channels. First
        // we must take care of the blocking (synchronization) which occurs
        // between the input and output streams derived from channel objects.
        // The SocketChannels must be wrapped by a ByteChannel, and then streams
        // derived from the ByteChannel can be used without synchronization
        // problems. (http://bugs.sun.com/bugdatabase/view_bug.do?bug_id=4774871)
        ByteChannel bc = cMsgUtilities.wrapChannel(info.getMessageChannel());
        in = new DataInputStream(new BufferedInputStream(Channels.newInputStream(bc), 65536));

        // self-starting threads:

        // read client TCP input
        clientHandlerThread = new ClientHandler();

        // read client UDP input
        if (!noUdp) {
            udpHandlerThread = new UdpSendHandler();
        }
    }

    
    /** Method to gracefully shutdown this object's threads and clean things up. */
    synchronized void shutdown() {
//System.out.println("SHUTTING DOWN domain server");

        // tell subdomain handler to shutdown
        if (info.calledSubdomainShutdown.compareAndSet(false,true)) {
            try {info.subdomainHandler.handleClientShutdown();}
            catch (cMsgException e) {
            }
        }

        // tell spawned threads to stop
        killSpawnedThreads = true;

        try { Thread.sleep(10); }
        catch (InterruptedException e) {}

        // stop thread that gets client commands over socket
        if (clientHandlerThread != null) {
            clientHandlerThread.interrupt();
        }

        // stop thread that gets client sends over udp, close socket
        if (udpHandlerThread != null) {
            udpHandlerThread.interrupt();
            // Closing the socket here is necessary to get the socket to wake up -
            // thus making the thread die.
            if (!udpSocket.isClosed()) {
                 udpSocket.close();
            }
        }

        // give threads a chance to shutdown
        try { Thread.sleep(10); }
        catch (InterruptedException e) {}

        // clear Qs, no more requests should be coming in
        requestQueue.clear();
        sequentialQueue.clear();

        // close connection from message deliverer to client
        if (info.getDeliverer() != null) {
            info.getDeliverer().close();
        }

        // close all sockets to client
        try {
            info.keepAliveChannel.close();
            info.getMessageChannel().close();
        }
        catch (IOException e) {}

        // Shutdown request-handling threads
        for (Thread t : requestThreads) {
            t.interrupt();
        }

        // Unsubscribe bridges from all subscriptions if regular client.
        // (Server clients have no subscriptions passed on to other servers
        //  as this would result in infinite loops.)
//System.out.println("    **** Removing subs of " + info.getName() + " from subscriptions");
            if (!info.isServer()) {
                // Protect table of subscriptions
                nameServer.subscribeLock.lock();

                try {
                    // allow no changes to "bridges" while iterating
                    synchronized (nameServer.bridges) {
                        // foreach bridge ...
                        for (cMsgServerBridge b : nameServer.bridges.values()) {
                            // only cloud members please
                            if (b.getCloudStatus() != cMsgNameServer.INCLOUD) {
                                continue;
                            }

                            // foreach of this client's subscriptions, unsubscribe
                            for (cMsgServerSubscribeInfo sub : nameServer.subscriptions) {
                                if (sub.info != info) {
//System.out.println("    **** Forgetting unsubscribing for " + info.getName());
                                    continue;
                                }

                                try {
                                    if (sub.isSubscribed()) {
//System.out.println("    **** unsubscribing to sub/type = " + sub.subject + "/" + sub.type + " on " +
//      b.serverName + " from " + sub.info.getName());
                                        b.unsubscribe(sub.subject, sub.type, sub.namespace);
                                    }

                                    for (Map.Entry<Integer, cMsgCallbackAdapter> entry : sub.getSubAndGetters().entrySet()) {
//System.out.println("    **** unsubAndGetting to sub/type = " + sub.subject + "/" + sub.type + " on " +
//   b.serverName);
                                        b.unsubscribeAndGet(sub.subject, sub.type,
                                                            sub.namespace, entry.getValue());
                                    }
                                }
                                catch (IOException e) {
                                }
                            }
                        }
                    }

                    // remove this client's subscriptions
                    cMsgServerSubscribeInfo sub;
                    for (Iterator it = nameServer.subscriptions.iterator(); it.hasNext();) {
                        sub = (cMsgServerSubscribeInfo) it.next();
                        if (sub.info == info) {
//System.out.println("    **** Removing subs of " + info.getName() + " from subscriptions");
                            it.remove();
                        }
                    }
                }
                finally {
                    nameServer.subscribeLock.unlock();
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
                cMsgServerBridge b = nameServer.bridges.remove(info.getName());

                // clean up the server client - shutdown thread pool, clear hashes
                if (b != null) {
                    b.client.cleanup();
                }

                // remove client from "nameServers" (hashmap is concurrent)
                cMsgClientData cd = nameServer.nameServers.remove(info.getName());

                if (debug >= cMsgConstants.debugInfo) {
                    if (b != null && cd != null) {
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

        // remove from name server's hash table
        nameServer.domainServers.remove(this);
//System.out.println("\nDomain Server: EXITING SHUTDOWN\n");
    }




   /**
     * Class to handle all client sends over UDP.
     */
    private class UdpSendHandler extends Thread {

         /** Allocate byte array once (used for reading in data) for efficiency's sake. */
        byte[] buf = new byte[cMsgNetworkConstants.biggestUdpPacketSize];

        /** Index into buffer of received UDP packet. */
        int bufIndex;

        /** Constructor. */
        UdpSendHandler() {
            // Permanent worker threads are already started up on the regular cue
            // by the ClientHandler thread. All we do is put sends on that cue.

            // die if no more non-daemon thds running
            setDaemon(true);

            start();
            //System.out.println("+");
        }

        /**
         * This method handles all communication between a cMsg user who has
         * connected to a domain and this server for that domain.
         */
        public void run() {

            // create a packet to be written into
            DatagramPacket packet = new DatagramPacket(buf, cMsgNetworkConstants.biggestUdpPacketSize);

            // interpret packets
            try {
                while (true) {
                    if (killSpawnedThreads) return;
                    packet.setLength(cMsgNetworkConstants.biggestUdpPacketSize);
                    udpSocket.receive(packet);
//System.out.println("RECEIVED SEND AS UDP PACKET !!!");

                    if (killSpawnedThreads) return;

                    // check magic #s coming in
                    if (cMsgUtilities.bytesToInt(buf,0) != cMsgNetworkConstants.magicNumbers[0] ||
                        cMsgUtilities.bytesToInt(buf,4) != cMsgNetworkConstants.magicNumbers[1] ||
                        cMsgUtilities.bytesToInt(buf,8) != cMsgNetworkConstants.magicNumbers[2]) {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("DS udpSendHandler: received bogus udp packet");
                        }
                        continue;
                    }
                    //else {
                    //    System.out.println("Packet has good magic #s");
                   // }

                    // pick apart byte array received
                    bufIndex = 12;

                    // skip first int which is size of data to come;
                    int msgId = cMsgUtilities.bytesToInt(buf, bufIndex += 4);

                    if (msgId != cMsgConstants.msgSendRequest) {
                        // problems
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("DS udpSendHandler: can't understand your message " + info.getName());
                        }
                        if (calledShutdown.compareAndSet(false, true)) {
//System.out.println("SHUTDOWN TO BE RUN due to unknown command being received");
                            shutdown();
                        }
                        return;
                    }

                    // connect for speed and to keep out unwanted packets
                    if (!udpSocket.isConnected()) {
                        udpSocket.connect(packet.getAddress(), packet.getPort());
                    }

                    info.monData.udpSends++;

                    cMsgHolder holder = readSendInfo();
                    holder.request = msgId;
                    try { requestQueue.put(holder); }
                    catch (InterruptedException e) { }

                    // if the cue is almost full, add temp threads to handle the load
                    if (requestQueue.remainingCapacity() < 10 && tempThreads.get() < tempThreadsMax) {
                        new RequestQueueThread();
                    }
                }
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("dServer udpSendHandler: I/O ERROR in domain server, udp receiver");
                }
            }
            finally {
                // We're here if there is an IO error. Close socket and kill this thread.
                //System.out.println("-");
                if (!udpSocket.isClosed()) {
                     udpSocket.close();
                }
            }
        }


        /**
         * This method reads an incoming cMsgMessageFull from a client doing a send.
         *
         * @return object holding message read from UDP packet
         * @throws IOException if socket read or write error
         */
        private cMsgHolder readSendInfo() throws IOException {

            // create a message
            cMsgMessageFull msg = new cMsgMessageFull();

            // pick apart byte array received
            bufIndex += 4; // skip for future use
            msg.setUserInt(cMsgUtilities.bytesToInt(buf, bufIndex+=4));
            msg.setSysMsgId(cMsgUtilities.bytesToInt(buf, bufIndex+=4));
            msg.setSenderToken(cMsgUtilities.bytesToInt(buf, bufIndex+=4));
            // mark msg as having been sent over wire
            msg.setInfo(cMsgUtilities.bytesToInt(buf, bufIndex+=4) | cMsgMessage.wasSent);
            // set info to mark msg as unexpanded
            msg.setExpandedPayload(false);

            // time message was sent = 2 ints (hightest byte first)
            // in milliseconds since midnight GMT, Jan 1, 1970
            long time = ((long) (cMsgUtilities.bytesToInt(buf, bufIndex+=4)) << 32) |
                        ((long) (cMsgUtilities.bytesToInt(buf, bufIndex+=4)) & 0x00000000FFFFFFFFL);
            msg.setSenderTime(new Date(time));

            // user time
            time = ((long) (cMsgUtilities.bytesToInt(buf, bufIndex+=4)) << 32) |
                   ((long) (cMsgUtilities.bytesToInt(buf, bufIndex+=4)) & 0x00000000FFFFFFFFL);
            msg.setUserTime(new Date(time));

            int lengthSubject    = cMsgUtilities.bytesToInt(buf, bufIndex+=4);
            int lengthType       = cMsgUtilities.bytesToInt(buf, bufIndex+=4);
            int lengthPayloadTxt = cMsgUtilities.bytesToInt(buf, bufIndex+=4);
            int lengthText       = cMsgUtilities.bytesToInt(buf, bufIndex+=4);
            int lengthBinary     = cMsgUtilities.bytesToInt(buf, bufIndex+=4);

            // read subject
            msg.setSubject(new String(buf, bufIndex += 4, lengthSubject, "US-ASCII"));
            //System.out.println("sub = " + msg.getSubject());

            // read type
            msg.setType(new String(buf, bufIndex += lengthSubject, lengthType, "US-ASCII"));
            //System.out.println("type = " + msg.getType());

            // read payload text
            if (lengthPayloadTxt > 0) {
                msg.setPayloadText(new String(buf, bufIndex += lengthType, lengthPayloadTxt, "US-ASCII"));
                //System.out.println("payload text = " + msg.getPayloadText());
            }

            // read text
            msg.setText(new String(buf, bufIndex += lengthPayloadTxt, lengthText, "US-ASCII"));
            //System.out.println("text = " + msg.getText());

            if (lengthBinary > 0) {
                try {msg.setByteArray(buf, bufIndex += lengthText, lengthBinary);}
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


    }




    /**
     * Class to handle all client requests except the keep alives.
     */
    private class ClientHandler extends Thread {

        /** Allocate byte array once (used for reading in data) for efficiency's sake. */
        byte[] bytes = new byte[20000];

        // variables to track message rate
        //double freq=0., freqAvg=0.;
        //long t1, t2, deltaT, totalT=0, totalC=0, count=0, loops=10000, ignore=5;


        /** Constructor.  */
        ClientHandler() {

            // start up "normal", permanent worker threads on the regular cue
            for (int i = 0; i < permanentCommandHandlingThreads; i++) {
                requestThreads.add(new RequestQueueThread(true));
            }

            // start up 1 and only 1 worker thread on the sequential queue
            // in which requests must be processed in order
            requestThreads.add(new SequentialQueueThread());

            // die if no more non-daemon thds running
            setDaemon(true);

            start();
        }


        /**
         * This method handles all communication between a cMsg user who has
         * connected to a domain and this server for that domain.
         */
        public void run() {
            int msgId, requestType;
            cMsgHolder holder;
            // for printing out request cue size periodically
            //Date now, t;
            //now = new Date();

            try {

                here:
                while (true) {
                    if (killSpawnedThreads) return;

                    // size of coming data is first int
//System.out.println("******DS: Try reading size******");
                    int size = in.readInt();
//System.out.println(">>    DS: Read in size = " + size);

                    // read client's request
                    msgId = in.readInt();
//System.out.println(">>    DS: Read in msgId = " + msgId);

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
                            info.monData.tcpSends++;
                        case cMsgConstants.msgSyncSendRequest:
                            holder = readSendInfo();
                            break;

                        case cMsgConstants.msgSubscribeAndGetRequest: // 1-shot subscription of subject & type
//System.out.println(">>    DS: Got Sub&Get request");
                        case cMsgConstants.msgUnsubscribeAndGetRequest: // ungetting subscribeAndGet request
                            holder = readSubscribeInfo();
                            break;

                        case cMsgConstants.msgSendAndGetRequest: // sending msg & expecting response msg
//System.out.println("GOT Send&Get request");
                        case cMsgConstants.msgServerSendAndGetRequest: // server sending msg & expecting response msg
                            holder = readGetInfo();
                            break;

                        case cMsgConstants.msgUnSendAndGetRequest: // ungetting sendAndGet request
                        case cMsgConstants.msgServerUnSendAndGetRequest: // server ungetting sendAndGet request
                            holder = readUngetInfo();
                            break;

                        case cMsgConstants.msgSubscribeRequest:         // subscribing to subject & type
                        case cMsgConstants.msgServerSubscribeRequest:   // server subscribing to subject & type
                        case cMsgConstants.msgUnsubscribeRequest:       // unsubscribing from a subject & type
                        case cMsgConstants.msgServerUnsubscribeRequest: // server unsubscribing from a subject & type
                            holder = readSubscribeInfo();
                            requestType = SEQUENTIAL;
                            break;

                        case cMsgConstants.msgMonitorRequest:
                            // Client requesting monitor data function is now obsolete, but is used to test
                            // client communication with server when using ssh tunnels.
                            break;

                        case cMsgConstants.msgDisconnectRequest: // client disconnecting
                            // need to shutdown this domain server
                            if (calledShutdown.compareAndSet(false, true)) {
//System.out.println("SHUTDOWN TO BE RUN BY msgDisconnectRequest");
                                if (debug >= cMsgConstants.debugSevere) {
System.out.println("Client " + info.getName() + " called disconnect");
                                }
                                shutdown();
                            }
                            return;

                        case cMsgConstants.msgServerShutdownSelf: // tell this name server to shutdown
                            nameServer.shutdown();
                            break;

                        case cMsgConstants.msgShutdownClients: // tell clients to shutdown
                        case cMsgConstants.msgShutdownServers: // tell servers to shutdown
                        case cMsgConstants.msgServerShutdownClients: // tell local clients to shutdown
                            holder = readShutdownInfo();
                            requestType = SEQUENTIAL; // use request cue not commonly used
                            break;

                        case cMsgConstants.msgServerRegistrationLock: // grab lock for client registration
                        case cMsgConstants.msgServerCloudLock: // grab lock for server cloud joining
                            // Grabbing this lock may take up to 1/2 second so
                            // pass along to thread pool so as not to block server.
//System.out.println(">>    DS: got msgServerCloudLock request");
                            int delay = in.readInt();
                            holder = new cMsgHolder();
                            holder.delay = delay;
                            requestType = SEQUENTIAL;
                            break;

                        case cMsgConstants.msgServerRegistrationUnlock: // release lock for client registration
                        case cMsgConstants.msgServerCloudUnlock: // release lock for server cloud joining
                            holder = new cMsgHolder();
                            requestType = SEQUENTIAL;
                            break;

                        case cMsgConstants.msgServerCloudSetStatus: // server client is joining cMsg subdomain server cloud
                            int status = in.readInt();
                            holder = new cMsgHolder();
                            holder.id = status;
                            requestType = SEQUENTIAL;
                            break;

                        case cMsgConstants.msgServerSendClientNames: // in cMsg subdomain send back all local client names
//System.out.println(">>    DS: got request to send client names");
                            holder = new cMsgHolder();
                            requestType = SEQUENTIAL;
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

                    // if we got something to put on a queue, do it
                    if (holder != null) {
                        holder.request = msgId;
                        try {
                            if (requestType == NORMAL) {
                                requestQueue.put(holder);
                            }
                            else {
                                sequentialQueue.put(holder);
                            }
                        }
                        catch (InterruptedException e) {
                        }
                    }

                    // print out request queue sizes periodically
                    //t = new Date();
                    //if (now.getTime() + 10000 <= t.getTime()) {
                    //    printSizes();
                    //    now = t;
                    //}

                    // if the queue is 80% full, add temp threads to handle the load
                    if ( (requestQueue.remainingCapacity() < 20) &&
                         (tempThreads.get() < tempThreadsMax) ) {
                        new RequestQueueThread();
                    }
                }
            }
            catch (InterruptedIOException ex) {
                // If this thread has been interrupted, quit
                if (debug >= cMsgConstants.debugError) {
                    System.out.println(">>    DS: command-reading thread has been interrupted, quit");
                }
                return;
            }
            catch (IOException ex) {
                if (debug >= cMsgConstants.debugError) {
                    System.out.println(">>    DS: command-reading thd's client (" + info.getName() + ") connection is dead, IO error");
                }
            }
            finally {
                if (calledShutdown.compareAndSet(false, true)) {
//System.out.println("SHUTDOWN TO BE RUN since IO error");
                    shutdown();
                }
            }
        }


        /**
         * This method reads an incoming cMsgMessageFull from a client doing a
         * send or syncSend.
         *
         * @return object holding message read from channel
         * @throws IOException if socket read or write error
         */
        private cMsgHolder readSendInfo() throws IOException {

            // create a message
            cMsgMessageFull msg = new cMsgMessageFull();

            int ssid = in.readInt();     // for syncSend id
            msg.setUserInt(in.readInt());
            msg.setSysMsgId(in.readInt());
            msg.setSenderToken(in.readInt());
            // mark msg as having been sent over wire
            msg.setInfo(in.readInt() | cMsgMessage.wasSent);
            // mark msg as unexpanded
            msg.setExpandedPayload(false);

            // time message was sent = 2 ints (hightest byte first)
            // in milliseconds since midnight GMT, Jan 1, 1970
            long time = ((long) in.readInt() << 32) | ((long) in.readInt() & 0x00000000FFFFFFFFL);
            msg.setSenderTime(new Date(time));
            // user time
            time = ((long) in.readInt() << 32) | ((long) in.readInt() & 0x00000000FFFFFFFFL);
            msg.setUserTime(new Date(time));

            int lengthSubject    = in.readInt();
            int lengthType       = in.readInt();
            int lengthPayloadTxt = in.readInt();
            int lengthText       = in.readInt();
            int lengthBinary     = in.readInt();

            // string bytes expected
            int stringBytesToRead = lengthSubject + lengthType +
                                    lengthPayloadTxt + lengthText;
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

            // read payload text
            if (lengthPayloadTxt > 0) {
                msg.setPayloadText(new String(bytes, offset, lengthPayloadTxt, "US-ASCII"));
                //System.out.println("payload text = " + msg.getPayloadText());
                offset += lengthPayloadTxt;
            }

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

            return new cMsgHolder(msg, ssid);
        }


        /**
         * This method reads an incoming cMsgMessageFull from a client doing a sendAndGet.
         *
         * @return object holding message read from channel
         * @throws IOException if socket read or write error
         */
        private cMsgHolder readGetInfo() throws IOException {

            // create a message
            cMsgMessageFull msg = new cMsgMessageFull();

            // inComing[0] is for future use
            in.skipBytes(4);

            msg.setUserInt(in.readInt());
            msg.setSenderToken(in.readInt());
            // mark msg as having been sent over wire
            msg.setInfo(in.readInt() | cMsgMessage.wasSent);
            // mark msg as unexpanded
            msg.setExpandedPayload(false);

            // time message was sent = 2 ints (hightest byte first)
            // in milliseconds since midnight GMT, Jan 1, 1970
            long time = ((long)in.readInt() << 32) | ((long)in.readInt() & 0x00000000FFFFFFFFL);
            msg.setSenderTime(new Date(time));
            // user time
            time = ((long)in.readInt() << 32) | ((long)in.readInt() & 0x00000000FFFFFFFFL);
            msg.setUserTime(new Date(time));

            int lengthSubject    = in.readInt();
            int lengthType       = in.readInt();
            int lengthNamespace  = in.readInt();
            int lengthPayloadTxt = in.readInt();
            int lengthText       = in.readInt();
            int lengthBinary     = in.readInt();

            // string bytes expected
            int stringBytesToRead = lengthSubject + lengthType + lengthNamespace +
                                    lengthPayloadTxt + lengthText;
            int offset = 0;

            // read all string bytes
            if (stringBytesToRead > bytes.length) {
              bytes = new byte[stringBytesToRead];
            }
            in.readFully(bytes, 0, stringBytesToRead);

            // read subject
            msg.setSubject(new String(bytes, offset, lengthSubject, "US-ASCII"));
            offset += lengthSubject;

            // read type
            msg.setType(new String(bytes, offset, lengthType, "US-ASCII"));
            offset += lengthType;

            // read namespace
            String ns = null;
            if (lengthNamespace > 0) {
                ns = new String(bytes, offset, lengthNamespace, "US-ASCII");
                offset += lengthNamespace;
            }

            // read payload text
            if (lengthPayloadTxt > 0) {
                msg.setPayloadText(new String(bytes, offset, lengthPayloadTxt, "US-ASCII"));
                offset += lengthPayloadTxt;
            }

            // read text
            msg.setText(new String(bytes, offset, lengthText, "US-ASCII"));
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
         * This method reads an incoming (un)subscribe or (un)subscribeAndGet
         * request from a client.
         *
         * @return object holding subject, type, namespace and id read from channel
         * @throws IOException if socket read or write error
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
         * This method reads incoming information from a client doing a shutdown
         * of other clients or servers.
         *
         * @return object holding message read from channel
         * @throws IOException if socket read or write error
         */
        private cMsgHolder readShutdownInfo() throws IOException {

            int flag         = in.readInt();
            int lengthClient = in.readInt();

            // read all string bytes
            if (lengthClient > bytes.length) {
              bytes = new byte[lengthClient];
            }
            in.readFully(bytes, 0, lengthClient);

            // read client
            String client = new String(bytes, 0, lengthClient, "US-ASCII");

            return new cMsgHolder(client, (flag == 1));
        }


        /**
         * This method reads an incoming unSendAndGet request from a client.
         *
         * @return object holding id read from channel
         * @throws IOException if socket read or write error
         */
        private cMsgHolder readUngetInfo() throws IOException {

            // id of subject/type combination  (senderToken actually)
            cMsgHolder holder = new cMsgHolder();
            holder.id = in.readInt();

            return holder;
        }
    }



    /**
     * Class for taking and processing a queued-up request from the client
     * having to do with sequential requests of various sorts and shutdown commands.
     */
    private class SequentialQueueThread extends Thread {

        /** Constructor. */
        SequentialQueueThread() {
            // die if main thread dies
            setDaemon(true);
            this.start();
        }


        /** Loop forever waiting for work to do. */
        public void run() {
            cMsgHolder holder;
            int answer;

            while (true) {

                if (killSpawnedThreads || Thread.currentThread().isInterrupted()) {
                    return;
                }

                holder = null;

                // Grab item off queue. Cannot use timeout
                //  here due to bug in Java library.
                try { holder = sequentialQueue.take(); }
                catch (InterruptedException e) { }

                if (holder == null) {
                    continue;
                }

                try {
                    switch (holder.request) {

                        case cMsgConstants.msgServerShutdownClients: // tell local clients to shutdown
                            info.subdomainHandler.handleShutdownClientsRequest(holder.client, holder.include);
                            break;

                        case cMsgConstants.msgShutdownClients: // shutting down various clients
                            // shutdown local clients
                            info.subdomainHandler.handleShutdownClientsRequest(holder.client, holder.include);
                            // send this command to other servers
                            if (nameServer.bridges.size() > 0) {
                                for (cMsgServerBridge b : nameServer.bridges.values()) {
                                    // only cloud members please
                                    if (b.getCloudStatus() != cMsgNameServer.INCLOUD) {
                                        continue;
                                    }
                                    b.shutdownClients(holder.client, holder.include);
                                }
                            }
                            break;

                        case cMsgConstants.msgShutdownServers: // shutting down various servers
                            // Shutdown servers we're connected to by bridges
                            // if their names match the given string.
                            if (nameServer.bridges.size() > 0) {
                                for (cMsgServerBridge b : nameServer.bridges.values()) {
                                    // only cloud members please
                                    if (b.getCloudStatus() != cMsgNameServer.INCLOUD) {
                                        continue;
                                    }
                                    // if names match, shut it down
                                    if (cMsgSubscription.matches(holder.client, b.serverName, true)) {
                                        b.shutdownServer();
                                    }
                                }
                            }
                            // shut ourselves down if directed to
                            if (holder.include && cMsgSubscription.matches(holder.client,
                                                                             nameServer.getServerName(),
                                                                             true)) {
                                nameServer.shutdown();
                            }
                            break;

                        case cMsgConstants.msgServerRegistrationUnlock: // release lock for client registration
                            info.cMsgSubdomainHandler.registrationUnlock();
                            break;

                        case cMsgConstants.msgServerRegistrationLock: // grab lock for global registration
                            // Send yes (1) to indicate lock grabbed , or no (0) back as return value.
                            // This may block for up to 1/2 second.
//System.out.println(">>    DS: Try to lock for registration ...");
                            boolean gotLock = info.cMsgSubdomainHandler.registrationLock(holder.delay);
//System.out.println(">>    DS: got reg lock = \"" + gotLock + "\", send reply");
                            answer =  gotLock ? 1 : 0;
                            info.getDeliverer().deliverMessage(answer, 0, cMsgConstants.msgServerRegistrationLockResponse);
                            break;

                        case cMsgConstants.msgServerCloudUnlock: // release lock for server cloud joining
                            try {
//System.out.println(">>    DS: TRY TO UNLOCK CLOUD");
                                nameServer.cloudUnlock();
//System.out.println(">>    DS: UNLOCKED CLOUD");
                            }
                            catch (Exception e) {
                                System.out.println("CANNOT UNLOCK CLOUD");
                                e.printStackTrace();
                            }
                            break;

                        case cMsgConstants.msgServerCloudLock: // grab lock for server cloud joining
                            // Send yes (1) to indicate lock grabbed , or no (0) back as return value.
                            // This may block for up to 0.2 seconds.
//System.out.println(">>    DS: Try to lock cloud ...");
                            gotLock = nameServer.cloudLock(holder.delay);
//System.out.println(">>    DS: got cloud lock = \"" + gotLock + "\", send reply");
                            answer = gotLock ? 1 : 0;
                            info.getDeliverer().deliverMessage(answer, 0, cMsgConstants.msgServerCloudLockResponse);
                            break;

                        case cMsgConstants.msgServerCloudSetStatus: // server client is joining cMsg subdomain server cloud
                            setCloudStatus(holder.id);
                            break;

                        case cMsgConstants.msgServerSendClientNames: // in cMsg subdomain send back all local client names
//System.out.println(">>    DS: got request to send client names");
                            info.getDeliverer().deliverMessage(info.cMsgSubdomainHandler.getClientNamesAndNamespaces(),
                                                               cMsgConstants.msgServerSendClientNamesResponse);
                            break;

                        case cMsgConstants.msgSubscribeRequest: // subscribing to subject & type
//System.out.println(">>    DS: got subscribe request from local client");
                            info.monData.subscribes++;
                            info.subdomainHandler.handleSubscribeRequest(holder.subject,
                                                                    holder.type,
                                                                    holder.id);
                            // for cmsg subdomain
                            if (info.cMsgSubdomainHandler != null) {
//System.out.println(">>    DS: subscribe request in cmsg subdomain");
                                handleCmsgSubdomainSubscribe(holder);
                            }
                            break;

                        case cMsgConstants.msgUnsubscribeRequest: // unsubscribing from a subject & type
                            info.monData.unsubscribes++;
                            info.subdomainHandler.handleUnsubscribeRequest(holder.subject,
                                                                      holder.type,
                                                                      holder.id);
                            // for cmsg subdomain
                            if (info.cMsgSubdomainHandler != null) {
                                handleCmsgSubdomainUnsubscribe(holder);
                            }
                            break;

                        case cMsgConstants.msgServerSubscribeRequest: // subscription by another server
//System.out.println(">>    DS: got serverSubscribe for bridge client, namespace = " + holder.namespace);
                            info.cMsgSubdomainHandler.handleServerSubscribeRequest(holder.subject,
                                                                              holder.type,
                                                                              holder.namespace);
                            break;

                        case cMsgConstants.msgServerUnsubscribeRequest: // unsubscribing by another server
//System.out.println(">>    DS: got serverUnsubscribe for bridge client");
                            info.cMsgSubdomainHandler.handleServerUnsubscribeRequest(holder.subject,
                                                                                holder.type,
                                                                                holder.namespace);
                            break;


                        default:
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("dServer lockThread: can't understand your message " + info.getName());
                            }
                            break;
                    }
                }
                catch (cMsgException e) {
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("dServer lockThread: thread picking commands off queue has died from cMsg error");
                        e.printStackTrace();
                    }
                }
                catch (IOException e) {
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("dServer lockThread: thread picking commands off queue has died from IO error");
                        e.printStackTrace();
                    }
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
            cMsgServerBridge bridge = nameServer.bridges.get(info.getName());

            // In order to reach this point, the server joining the cloud has
            // grabbed all in-cloud members' cloud locks - including this server.

            // We actually do NOT care if servers added to "bridges" collection
            // during iterations (it's concurrent). We only care if INCLOUD
            // members are added during such.

            // Protect table of subscriptions. We cannot have servers joining the cloud
            // while subscribing or subAndGetting since a joining server needs this server
            // to update it with all the local subs and subAndGets. Once it's updated, then
            // it will get all the new subs and subAndGets.
            nameServer.subscribeLock.lock();
            try {
                if (bridge != null) {
                    bridge.setCloudStatus(status);
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("    DS: " + bridge.serverName + " has joined the cloud");
                    }
                }
                // if bridge can't be found, try alternate name first
                else {
                    String alternateName;
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
                                                   bridge.serverName);
                            }
                            return;
                        }
                    }
                    bridge = nameServer.bridges.get(alternateName);
                    if (bridge != null) {
                        bridge.setCloudStatus(status);
                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("    DS: " + bridge.serverName + " has joined the cloud");
                        }
                    }
                    else {
                        if (debug >= cMsgConstants.debugError) {
                            System.out.println("    DS: cannot find bridge to server " +  bridge.serverName);
                        }
                        return;
                    }
                }

                // update the new "INCLOUD" bridge with all cMsg domain subscriptions
                for (cMsgServerSubscribeInfo sub : nameServer.subscriptions) {
                    try {
                        if (sub.isSubscribed()) {
//System.out.println("subscribing to sub/type = " + sub.subject + "/" + sub.type + "/" +
//   sub.namespace + " on " + bridge.serverName + " from " + sub.info.getName());
                            bridge.subscribe(sub.subject, sub.type, sub.namespace);
                        }

                        for (Map.Entry<Integer, cMsgCallbackAdapter> entry : sub.getSubAndGetters().entrySet()) {
//System.out.println("subAndGetting to sub/type = " + sub.subject + "/" + sub.type + "/" +
//   sub.namespace + " on " +   bridge.serverName + " from " + sub.info.getName());
                            bridge.subscribeAndGet(sub.subject, sub.type,
                                                   sub.namespace, entry.getValue());
                        }
                    }
                    catch (IOException e) {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("dServer requestThread: cannot subscribe with server " +
                                               bridge.serverName);
                            e.printStackTrace();
                        }
                    }
                }
            }
            finally {
                nameServer.subscribeLock.unlock();
            }
        }


       /**
        * This method handles what extra things need to be done in the
        * cMsg subdomain when a subscribe request is made by the client.
        *
        * @param holder object that holds request information
        * @throws cMsgException if trying to add more than 1 identical subscription
        */
       private void handleCmsgSubdomainSubscribe(cMsgHolder holder) throws cMsgException {
//System.out.println("    DS: got subscribe for reg client " + holder.subject + " " + holder.type);
            holder.namespace = info.getNamespace();

            // Cannot have servers joining cloud while a subscription is added
            nameServer.subscribeLock.lock();
            try {
                cMsgServerSubscribeInfo sub = null;
                // Regular client is subscribing to sub/type.
                // Pass this on to any cMsg subdomain bridges.
                if (nameServer.bridges.size() > 0) {
                    for (cMsgServerBridge b : nameServer.bridges.values()) {
//System.out.println("    DS: call bridge subscribe");
                        // only cloud members please
                        if (b.getCloudStatus() != cMsgNameServer.INCLOUD) {
                            continue;
                        }
                        try {
                            b.subscribe(holder.subject, holder.type, holder.namespace);
                        }
                        catch (IOException e) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("dServer requestThread: cannot subscribe with server " +
                                                   b.serverName);
                                e.printStackTrace();
                            }
                        }
                    }
                }

                // Keep track of all subscriptions/sub&Gets made by this client.
                boolean subExists = false;
                for (Iterator it = nameServer.subscriptions.iterator(); it.hasNext();) {
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
                    nameServer.subscriptions.add(sub);
                }
            }
            finally {
                nameServer.subscribeLock.unlock();
            }
//System.out.println("    DS: size of subscription = " + subscriptions.size());

        }


        /**
         * This method handles the extra things that need to be done in the
         * cMsg subdomain when an unsubscribe request is made by the client.
         *
         * @param holder object that holds request information
         */
        private void handleCmsgSubdomainUnsubscribe(cMsgHolder holder) {
//System.out.println("Domain Server: got UNSubscribe for bridge client");
            // Cannot have servers joining cloud while a subscription is removed
            nameServer.subscribeLock.lock();
            try {
                cMsgServerSubscribeInfo sub = null;
                // Regular client is unsubscribing to sub/type.
                // Pass this on to any cMsg subdomain bridges.
                if (nameServer.bridges.size() > 0) {
                    for (cMsgServerBridge b : nameServer.bridges.values()) {
//System.out.println("Domain Server: call bridge unsubscribe");
                        // only cloud members please
                        if (b.getCloudStatus() != cMsgNameServer.INCLOUD) {
                            continue;
                        }
                        try {
                            b.unsubscribe(holder.subject, holder.type, info.getNamespace());
                        }
                        catch (IOException e) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("dServer requestThread: cannot unsubscribe with server " +
                                                   b.serverName);
                                e.printStackTrace();
                            }
                        }
                    }
                }

                // keep track of all subscriptions removed by this client
                for (Iterator it = nameServer.subscriptions.iterator(); it.hasNext();) {
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
                if (sub != null && sub.numberOfSubscribers() < 1) {
//System.out.println("    DS: removing sub object for subscribe");
                    nameServer.subscriptions.remove(sub);
                }
            }
            finally {
                nameServer.subscribeLock.unlock();
            }
        }
    }


    /**
     * Class for taking and processing a queued-up request from the client
     * having to do with everything EXCEPT lock, subscribe, and other
     * sequential commands.
     */
    private class RequestQueueThread extends Thread {
        /** Is this thread temporary or permanent? */
        boolean permanent;

        /** Constructor for temporary thread to work on normal request queue. */
        RequestQueueThread() {
            // thread is not permanent and only reads normal requests
            this(false);
        }

        /** General constructor. */
        RequestQueueThread(boolean permanent) {
            this.permanent = permanent;

            if (!permanent) {
                tempThreads.getAndIncrement();
            }

            // die if main thread dies
            setDaemon(true);

            this.start();
        }


        /** Loop forever waiting for work to do. */
        public void run() {
            cMsgHolder holder;
            int answer;

            while (true) {

                if (killSpawnedThreads || Thread.currentThread().isInterrupted()) {
                    return;
                }

                holder = null;

                if (permanent) {
                    try { holder = requestQueue.take(); }
                    catch (InterruptedException e) { }

                    if (holder == null) { continue; }
                }
                else {
                    try {
                        // try for up to 1/2 second to read a request from the queue
                        holder = requestQueue.poll(500, TimeUnit.MILLISECONDS);
                    }
                    catch (InterruptedException e) { }

                    // disappear after no requests for 1/2 second
                    if (holder == null) {
                        tempThreads.getAndDecrement();
                        return;
                    }
                }

                try {
                    switch (holder.request) {

                        case cMsgConstants.msgSendRequest: // receiving a message
                            info.subdomainHandler.handleSendRequest(holder.message);
                            break;

                        case cMsgConstants.msgSyncSendRequest: // receiving a message
                            info.monData.syncSends++;
                            answer = info.subdomainHandler.handleSyncSendRequest(holder.message);
                            info.getDeliverer().deliverMessage(answer, holder.ssid, cMsgConstants.msgSyncSendResponse);
                            break;

                        case cMsgConstants.msgSendAndGetRequest: // sending a message to a responder
                            info.monData.sendAndGets++;
//System.out.println("Domain Server: got msgSendAndGetRequest from client, ns = " + holder.namespace);
                            // If not cMsg subdomain just call subdomain handler.
                            if (info.cMsgSubdomainHandler == null) {
//System.out.println("Domain Server: call NON-CMSG subdomain send&Get");
                                info.subdomainHandler.handleSendAndGetRequest(holder.message);
                                break;
                            }
                            handleCmsgSubdomainSendAndGet(holder);
                            break;

                        case cMsgConstants.msgUnSendAndGetRequest: // ungetting sendAndGet
                            // This will fire notifier if one exists.
                            // The fired notifier will take care of unSendAndGetting any bridges.
//System.out.println("Domain Server: got msgUnSendAndGetRequest from client, ns = " + holder.namespace);
                            info.subdomainHandler.handleUnSendAndGetRequest(holder.id);
                            break;

                        case cMsgConstants.msgSubscribeAndGetRequest: // getting 1 message of subject & type
                            info.monData.subAndGets++;
                            // if not cMsg subdomain, just call subdomain handler
//System.out.println(">>    DS: got msgSubAndGetRequest from client, ns = " + holder.namespace);
                            if (info.cMsgSubdomainHandler == null) {
//System.out.println(">>    DS: call regular sub&Get");
                                info.subdomainHandler.handleSubscribeAndGetRequest(holder.subject,
                                                                              holder.type,
                                                                              holder.id);
                                break;
                            }
                            handleCmsgSubdomainSubscribeAndGet(holder);
                            break;

                        case cMsgConstants.msgUnsubscribeAndGetRequest: // ungetting subscribeAndGet
                            // this will fire notifier if one exists (cmsg subdomain)
                            info.subdomainHandler.handleUnsubscribeAndGetRequest(holder.subject,
                                                                            holder.type,
                                                                            holder.id);
                            // for cmsg subdomain
                            if (info.cMsgSubdomainHandler != null) {
                                handleCmsgSubdomainUnsubscribeAndGet(holder);
                            }
                            break;

                        case cMsgConstants.msgServerSendAndGetRequest: // sendAndGet by another server
//System.out.println("Domain Server: got msgServerSendAndGetRequest from bridge client, ns = " + holder.namespace);
                            info.cMsgSubdomainHandler.handleServerSendAndGetRequest(holder.message,
                                                                               holder.namespace);
                            break;

                        case cMsgConstants.msgServerUnSendAndGetRequest: // unsubscribing by another server
//System.out.println("Domain Server: got msgServerUnSendAndGetRequest from bridge client, ns = " + holder.namespace);
                            info.cMsgSubdomainHandler.handleUnSendAndGetRequest(holder.id);
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
                        System.out.println("dServer requestThread: thread picking commands off queue has died from cMsg error");
                        e.printStackTrace();
                    }
                }
                catch (IOException e) {
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("dServer requestThread: thread picking commands off queue has died from IO error");
                        e.printStackTrace();
                    }
                }
            }

        }


        /**
         * This method handles what needs to be done in the cMsg subdomain when
         * a sendAndGet request is made by the client.
         *
         * @param holder object that holds request information
         * @throws cMsgException if IO error in sending message
         */
        private void handleCmsgSubdomainSendAndGet(cMsgHolder holder) throws cMsgException {
            // If not in a cMsg server cloud, just call subdomain handler.
            if (nameServer.standAlone || nameServer.bridges.size() < 1) {
//System.out.println("    DS: call regular cmsg subdomain send&Get");
                info.subdomainHandler.handleSendAndGetRequest(holder.message);
                return;
            }

            cMsgCallbackAdapter cb;
            cMsgNotifier notifier;
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
            int smId = info.cMsgSubdomainHandler.handleServerSendAndGetRequest(holder.message,
                                                                          holder.namespace,
                                                                          notifier);
//System.out.println("    DS: called serverSub&GetRequest & got sysMsgId = " + smId);

//System.out.println("    DS: sendAndGet cb given id, sysMsgid =  " +
//                   holder.message.getSenderToken() + ", " + smId);
            cb = cMsgServerBridge.getSendAndGetCallback(holder.message.getSenderToken(),
                                                        smId);

            // Run thd that waits for notifier and cleans up afterwards
            for (cMsgServerBridge b : nameServer.bridges.values()) {
                // Only deal with cloud members
                if (b.getCloudStatus() != cMsgNameServer.INCLOUD) {
                    continue;
                }

                try {
                    // if (local) message already arrived, bail out
                    if (notifier.latch.getCount() < 1) {
//System.out.println("    DS: sendAndGet BAIL-OUT for " + b.serverName);
                        break;
                    }
                    // This sendAndGet will pass on a received message by calling subdomain
                    // handler object's "bridgeSend" method which will, in turn,
                    // fire off the notifier. The notifier was associated with
                    // this sendAndGet by the above calling of the
                    // "handleServerSendAndGetRequest" method.
//System.out.println("    DS: call bridge sendAndGet for " + b.serverName);
                    b.sendAndGet(holder.message, holder.namespace, cb);
                }
                catch (IOException e) {
                    if (debug >= cMsgConstants.debugWarn) {
                        System.out.println(">>    DS: requestThread: error on sendAndGet with server " +
                                           b.serverName);
                        e.printStackTrace();
                    }
                }
            }

            cMsgServerSendAndGetter getter =
                    new cMsgServerSendAndGetter(nameServer, notifier, sendAndGetters);
            sendAndGetters.put(notifier.id, getter);
            sendAndGetThreadPool.execute(getter);


        }


        /**
         * This method handles what needs to be done in the cMsg subdomain when
         * a subscribeAndGet request is made by the client.
         *
         * @param holder object that holds request information
         * @throws cMsgException if IO error in sending message
         */
        private void handleCmsgSubdomainSubscribeAndGet(cMsgHolder holder) throws cMsgException {

            boolean localOnly = true;
            cMsgCallbackAdapter cb = null;
            cMsgNotifier notifier  = null;
            cMsgServerSubscribeInfo sub = null;
            holder.namespace = info.getNamespace();

            // Can't have bridges joining cloud while (un)subscribeAndGetting
            nameServer.subscribeLock.lock();
            try {
                // If not in a cMsg server cloud, just call subdomain handler.
                if (nameServer.standAlone || nameServer.bridges.size() < 1) {
//System.out.println(">>    DS: (in handleCmsgSubdomainSubscribeAndGet) call regular cmsg subdomain sub&Get");
                    info.subdomainHandler.handleSubscribeAndGetRequest(holder.subject,
                                                                  holder.type,
                                                                  holder.id);
                }
                else {
                    // Take care of connected server issues.
                    // First create an object (notifier) which will tell us if someone
                    // has sent a matching message to our client. Then we can tell connected
                    // servers to cancel the order (subscription). We can also clean up
                    // entries in the hashtable storing subscription info.
                    localOnly = false;
                    notifier = new cMsgNotifier();
                    notifier.id = holder.id;
                    notifier.latch = new CountDownLatch(1);
                    notifier.client = info;

                    cb = cMsgServerBridge.getSubAndGetCallback();

                    // Here we use "subscribe" to implement a "subscribeAndGet" for other servers
                    for (cMsgServerBridge b : nameServer.bridges.values()) {
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
//System.out.println(">>    DS: call bridge subscribe for " + b.serverName);
                            b.subscribeAndGet(holder.subject, holder.type,
                                              holder.namespace, cb);
                        }
                        catch (IOException e) {
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("dServer requestThread: cannot subscribe with server " +
                                                   b.serverName);
                                e.printStackTrace();
                            }
                        }
                    }

                    // Do a local subscribeAndGet. This associates the notifier
                    // object with the subscription. The call to this method MUST COME
                    // AFTER the bridges' subAndGets. If not, then there is a race
                    // condition in which subscribes and unsubscribes get out of order.
//System.out.println(">>    DS: call serverSub&GetRequest with id = " + holder.id);
                    info.cMsgSubdomainHandler.handleServerSubscribeAndGetRequest(holder.subject,
                                                                            holder.type,
                                                                            notifier);
                }

                // Keep track of this subscribeAndGet (just like we do in the cMsg
                // subdomain handler object) so later, if another server joins the
                // cloud, we can tell that server about the subscribeAndGet.
                boolean subscriptionExists = false;
                for (Iterator it = nameServer.subscriptions.iterator(); it.hasNext();) {
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
//System.out.println(">>    DS: add sub&Get with id = " + holder.id);
                    sub.addSubAndGetter(holder.id, cb);
                }
                // or else create a new subscription
                else {
//System.out.println(">>    DS: create subscribeInfo & add sub&Get with id = " + holder.id);
                    sub = new cMsgServerSubscribeInfo(holder.subject, holder.type,
                                                      holder.namespace, info,
                                                      holder.id, cb);
                    nameServer.subscriptions.add(sub);
                }
            }
            finally {
                nameServer.subscribeLock.unlock();
            }

            // Run thd that waits for notifier and cleans up server subscriptions
            if (!localOnly) {
                cMsgServerSubscribeAndGetter getter =
                        new cMsgServerSubscribeAndGetter(nameServer,
                                                         notifier, cb,
                                                         nameServer.subscriptions, sub);
                subAndGetThreadPool.execute(getter);
            }
        }


        /**
         * This method handles the extra things that need to be done in the
         * cMsg subdomain when an unsubscribeAndGet request is made by the client.
         *
         * @param holder object that holds request information
         */
        private void handleCmsgSubdomainUnsubscribeAndGet(cMsgHolder holder) {
            // Cannot have servers joining cloud while a subscription is removed
            nameServer.subscribeLock.lock();
            try {
                cMsgServerSubscribeInfo sub = null;
                // keep track of all subscriptions removed by this client
                for (Iterator it = nameServer.subscriptions.iterator(); it.hasNext();) {
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
                if (sub != null && sub.numberOfSubscribers() < 1) {
//System.out.println("    DS: removing sub object for sub&Get");
                    nameServer.subscriptions.remove(sub);
                }
//if (subscriptions.size() > 100 && subscriptions.size()%100 == 0) {
//    System.out.println("sub size = " + subscriptions.size());
//}
            }
            finally {
                nameServer.subscribeLock.unlock();
            }
        }


    }


}

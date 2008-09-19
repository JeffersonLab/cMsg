/*----------------------------------------------------------------------------*
 *  Copyright (c) 2008        Jefferson Science Associates,                   *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 7-May-2008, Jefferson Lab                                    *
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
import java.nio.channels.*;
import java.nio.ByteBuffer;

import org.jlab.coda.cMsg.*;



/**
 * Domain Server which services a number of clients through select and nonblocking sockets.
 */
public class cMsgDomainServerSelect extends Thread {
    /** Type of domain this is. */
    static String domainType = "cMsg";

    /** Maximum number of clients to service simultaneously. */
    private int clientsMax;

    /** UDP Port this server is listening on. */
    private int udpPort;

    /** Reference back to object that created this object. */
    private cMsgNameServer nameServer;

    /** Level of debug output. */
    private int debug;

    /**
     * Object containing information about the client this object is connected to.
     * Certain members of info can only be filled in by this thread,
     * such as the listening port.
     */
    cMsgClientData info;

    /**
     * Set of all clients in this domain server. The value is just a dummy
     * so the concurrent hashmap could be used.
     */
    ConcurrentHashMap<cMsgClientData, String> clients;

    /**
     * Set of all clients waiting to be registed with the selector of this domain server.
     * The value is just a dummy so the concurrent hashmap could be used.
     */
    ConcurrentHashMap<cMsgClientData, String> clients2register;

    /** Selector object each client's channel is registered with. */
    Selector selector;

    /** Channel to receive UDP sends from the client. */
    private DatagramChannel udpChannel;

    /** Socket to receive UDP sends from the client. */
    private DatagramSocket udpSocket;

    /**
     * Thread-safe queue to hold cMsgHolder objects of
     * requests from the client.
     * These are grabbed and processed by waiting worker thread.
     */
    private LinkedBlockingQueue<cMsgHolder> bufferQ;

    /**
     * Thread that handles all client requests.
     */
    private RequestHandler requestHandlerThread;

    /** A pool of threads to execute all the subscribeAndGet calls which come in. */
    private ThreadPoolExecutor subAndGetThreadPool;

    /** A pool of threads to execute all the sendAndGet calls which come in. */
    private ThreadPoolExecutor sendAndGetThreadPool;

    /** Keep track of whether the shutdown method of this object has already been called. */
    AtomicBoolean calledShutdown = new AtomicBoolean();

    /** Hashtable of all sendAndGetter objects of this client. */
    private ConcurrentHashMap<Integer, cMsgServerSendAndGetter> sendAndGetters;

    /** Kill main thread if true. */
    private volatile boolean killMainThread;

    /**
     * Getter for the UDP port being used.
     * @return UDP port being used
     */
    public int getUdpPort() {
        return udpPort;
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
        System.out.println("     request   cue   = " + bufferQ.size());
        System.out.println("     sendAndGetters  = " + sendAndGetters.size());
        System.out.println("     clients         = " + clients.size());

//        System.out.println();
//
//        nameServer.printSizes();
//
//        // print static stuff for cMsg subdomain class
//        org.jlab.coda.cMsg.cMsgDomain.subdomains.cMsg.printStaticSizes();
//
//        System.out.println();
//
//        // print sizes for our specific cMsg subdomain handler
//        if (info.cMsgSubdomainHandler != null) {
//            info.cMsgSubdomainHandler.printSizes();
//        }
    }


    /**
     * Constructor.
     *
     * @param nameServer nameServer object which created (is creating) this object
     * @param startingPort suggested port on which to starting listening for connections
     * @param clientsMax   maximum number of clients serviced by this object at one time
     * @param debug  level of debug output.
     *
     * @throws cMsgException if listening socket could not be opened or a port to listen on could not be found
     * @throws IOException if selector cannot be opened
     */
    public cMsgDomainServerSelect(cMsgNameServer nameServer, int startingPort,
                                  int clientsMax, int debug)
            throws cMsgException, IOException {

System.out.println("Creating cMsgDomainServerSelect with clientsMax = " + clientsMax);
        this.debug       = debug;
        this.nameServer  = nameServer;
        this.clientsMax  = clientsMax;

        clients2register = new ConcurrentHashMap<cMsgClientData, String>(clientsMax);
        clients          = new ConcurrentHashMap<cMsgClientData, String>(clientsMax);
        bufferQ          = new LinkedBlockingQueue<cMsgHolder>(1000);
        sendAndGetters   = new ConcurrentHashMap<Integer, cMsgServerSendAndGetter>(10);

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
        // extra (more than 1) unused threads. Overflow tasks spawn independent
        // threads.
        subAndGetThreadPool =
                new ThreadPoolExecutor(1, 5, 60L, TimeUnit.SECONDS,
                                       new SynchronousQueue<Runnable>(),
                                       new RejectHandler());

        // Start a thread pool for sendAndGet handling.

        // Run up to 10 threads with no queue. Wait 1 min before terminating
        // extra (more than 1) unused threads. Overflow tasks spawn independent
        // threads.
        sendAndGetThreadPool =
                new ThreadPoolExecutor(1, 10, 60L, TimeUnit.SECONDS,
                                       new SynchronousQueue<Runnable>(),
                                       new RejectHandler());

        // For the client wants to do sends with udp, create a socket on an available udp port
        try {
            // Create socket to receive at all interfaces
            udpChannel = DatagramChannel.open();
            udpSocket  = udpChannel.socket();
        }
        catch (IOException ex) {
            ex.printStackTrace();
            cMsgException e = new cMsgException("Exiting Server: cannot create socket to listen on");
            e.setReturnCode(cMsgConstants.errorSocket);
            throw e;
        }

        udpPort = startingPort;
        while (true) {
            try {
                udpSocket.bind(new InetSocketAddress(udpPort));
                udpSocket.setReceiveBufferSize(cMsgNetworkConstants.biggestUdpBufferSize);
                break;
            }
            catch (SocketException ex) {
                udpPort++;
            }
        }
//System.out.println("udp channel port = " + udpSocket.getLocalPort());
        // allow clients to be registered with the selector
        selector = Selector.open();
    }


    /**
     * Start reading and writing over the sockets. Start threads to process client requests.
     *
     * @throws IOException if I/O problems
     */
    void startThreads() throws IOException {
        // self-starting thread
        requestHandlerThread = new RequestHandler();
        this.start();
    }


    synchronized private int numberOfClients() {
        return clients.size();
    }

    synchronized private void removeClient(cMsgClientData info) {
        String s = clients.remove(info);
        if (s != null) {
            //System.out.println("removeClient: value removed = " + s);
            makeDomainServerAvailable();
        }
        else {
            //System.out.println("removeClient: NOTHING removed");
        }
    }

    synchronized private void makeDomainServerAvailable() {
        if (clients.size() < clientsMax && !nameServer.availableDomainServers.contains(this)) {
            nameServer.availableDomainServers.add(this);
        }
    }

    /**
     * Method to allow another client to send to this domain server. Only {@link #clientsMax}
     * number of clients may use this domain server. Before this method is called, the client
     * has already created 2 permanent TCP sockets to this server.
     *
     * @param info information on client trying to connect to this domain server
     * @return
     */
    boolean addClient(cMsgClientData info) throws IOException {

        synchronized (this) {
            if (clients.size() >= clientsMax) {
                return false;
            }

            clients.put(info, "");
        }

        this.info = info;

        // For the client who wants to do sends with udp (MAY NOT BE USEFUL !!)
        info.udpSocket = udpSocket;

        // Fill in info members so this data can be sent back
        info.setDomainUdpPort(udpSocket.getLocalPort());

        // Finish making the "deliverer" object. Use this channel
        // to communicate back to the client.
        info.getDeliverer().createClientConnection(info.getMessageChannel(), false);
        //info.messageChannel.configureBlocking(false);

        // Put client's channel in list to be registered with the selector for reading
        // (once selector is woken up).
        clients2register.put(info, "");
        selector.wakeup();

        // Once client is safely registered, we can put this object
        // back into the list so other clients can be added.
        makeDomainServerAvailable();

        return true;
    }



    /**
     * Method to be run when this server's clients are dead or disconnected and
     * the server threads will be killed. <p/>
     * Finalize methods are run after an object has become unreachable and
     * before the garbage collector is run;
     */
    public void finalize() throws cMsgException {
        try { super.finalize();}
        catch (Throwable throwable) { }
    }


    /** Method to gracefully shutdown this object's threads and clean things up. */
    synchronized void shutdown() {
//System.out.println("SHUTTING DOWN domain server select");

        // tell subdomain handlers to shutdown
        for (cMsgClientData cd : clients.keySet()) {
            if (cd.calledSubdomainShutdown.compareAndSet(false,true)) {
                try {cd.subdomainHandler.handleClientShutdown();}
                catch (cMsgException e) { }
            }
        }

        // remove clients from keepalive monitoring
        nameServer.domainServersSelect.remove(this);

        // keep clients from being added to this object
        nameServer.availableDomainServers.remove(this);

        // clear cue, no more requests should be coming in
        bufferQ.clear();

        // shutdown request-handling thread
        requestHandlerThread.interrupt();

        // close udp socket
        if (!udpSocket.isClosed()) {
             udpSocket.close();
        }

        // close all sockets to clients
        for (cMsgClientData cd : clients.keySet()) {
            try {
                cd.keepAliveChannel.close();
                cd.getMessageChannel().close();
            }
            catch (IOException e) {}
        }

        // give threads a chance to shutdown
        try { Thread.sleep(10); }
        catch (InterruptedException e) {}

        // Shutdown this domain server's listening thread's socket.
        // Shouldn't take more than 1 second.
        killMainThread = true;

        // Unsubscribe bridges from all subscriptions if regular client.
        // (Server clients have no subscriptions passed on to other servers
        //  as this would result in infinite loops.)
//System.out.println("    **** Removing subs of " + info.getName() + " from subscriptions");
        for (cMsgClientData cd : clients.keySet()) {

            if (!cd.isServer()) {
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
                                if (sub.info != cd) {
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
                        if (sub.info == cd) {
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
                cMsgServerBridge b = nameServer.bridges.remove(cd.getName());

                // clean up the server client - shutdown thread pool, clear hashes
                if (b!=null) {
                    b.client.cleanup();
                }

                // remove client from "nameServers" (hashset is synchronized)
                boolean removed = nameServer.nameServers.remove(cd.getName());

                if (debug >= cMsgConstants.debugInfo) {
                    if (b != null && removed) {
                        System.out.println(">>    DS: DELETED server client FROM BRIDGES AND NAMESERVERS");
                    }
                    else {
                        System.out.println(">>    DS: COULD NOT DELETE Client FROM BRIDGES AND/OR NAMESERVERS");
                    }
                }
            }
        }

        // shutdown the threads in pools used for subscribeAndGet & sendAndGets
        subAndGetThreadPool.shutdownNow();
        sendAndGetThreadPool.shutdownNow();

//System.out.println("\nDomain Server: EXITING SHUTDOWN\n");
    }


    /**
     * Method to gracefully remove client from this domain server.
     * @param cd client data object
     */
    synchronized void deleteClient(cMsgClientData cd) {
//System.out.println("Try deleting client " + cd.getName());
        // remove from hashmap (otherwise the cMsgMonitorClient object will try to run this method)
        removeClient(cd);

        // go through Q and remove out-dated requests
//System.out.println("buffer size = " + bufferQ.size());
        if (clientsMax == 1) {
//System.out.println("clear bufferQ");
            bufferQ.clear();
        }
        else {
            cMsgHolder hldr;
//System.out.println("Before iterator Q size = " + bufferQ.size());
            for (Iterator it = bufferQ.iterator(); it.hasNext();) {
                // pick out requests from current client
                hldr = (cMsgHolder)it.next();
                if (hldr == null) {
//System.out.println("NULL IN ITERATOR !!! Q size = " + bufferQ.size());
                    continue;
                }
                if (hldr.data != cd) continue;
                it.remove();
            }
        }

        // tell client's subdomain handler to shutdown
        if (cd.calledSubdomainShutdown.compareAndSet(false,true)) {
//System.out.println("Try calling subdh handleClientShutdown method");
            try {cd.subdomainHandler.handleClientShutdown();}
            catch (cMsgException e) { e.printStackTrace(); }
        }

        // close all sockets to client
        try {
            cd.keepAliveChannel.close();
            cd.getMessageChannel().close();
        }
        catch (IOException e) {}

        // close connection from message deliverer to client
        if (cd.getDeliverer() != null) {
            cd.getDeliverer().close();
        }

        // Unsubscribe bridges from all subscriptions if regular client.
        // (Server clients have no subscriptions passed on to other servers
        //  as this would result in infinite loops.)
//System.out.println("    **** Removing subs of " + cd.getName() + " from subscriptions");

        if (!cd.isServer()) {
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
                            if (sub.info != cd) {
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
                    if (sub.info == cd) {
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
            cMsgServerBridge b = nameServer.bridges.remove(cd.getName());

            // clean up the server client - shutdown thread pool, clear hashes
            if (b!=null) {
                b.client.cleanup();
            }

            // remove client from "nameServers" (hashset is synchronized)
            boolean removed = nameServer.nameServers.remove(cd.getName());

            if (debug >= cMsgConstants.debugInfo) {
                if (b != null && removed) {
                    System.out.println(">>    DS: DELETED server client FROM BRIDGES AND NAMESERVERS");
                }
                else {
                    System.out.println(">>    DS: COULD NOT DELETE Client FROM BRIDGES AND/OR NAMESERVERS");
                }
            }
        }
//System.out.println(" End");

//System.out.println("\nDomain Server: EXITING deleteClient for " + cd.getName() + "\n" );
    }


    /**
     * This method is a thread which uses select to read messages from clients.
     */
    public void run() {

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println(">>    DS: Running Domain Server");
        }

        int bytes;
        SelectableChannel selChannel;
        SocketChannel sockChannel;

        try {
            // get things ready for a select call
            //selector = Selector.open();

            // set nonblocking mode for the udp socket
            udpChannel.configureBlocking(false);

            // register the channel with the selector for reading
            udpChannel.register(selector, SelectionKey.OP_READ, info);

            // direct byte Buffer for UDP IO use
            ByteBuffer udpBuffer = ByteBuffer.allocateDirect(cMsgNetworkConstants.biggestUdpBufferSize);

            while (true) {
                
                // 1 second timeout
                int n = selector.select(1000);

                // register any clients waiting for it
                if (clients2register.size() > 0) {
                    for (cMsgClientData cli : clients2register.keySet()) {
//System.out.println("Registering client " + cli.getName());
                        cli.getMessageChannel().register(selector, SelectionKey.OP_READ, cli);
                        clients2register.remove(cli);
                    }
                }

                // first check to see if we've been commanded to die
                if (killMainThread) return;

                // if no channels (sockets) are ready, listen some more
                if (n == 0) continue;

                // get an iterator of selected keys (ready sockets)
                Iterator it = selector.selectedKeys().iterator();

                // look at each key
                while (it.hasNext()) {
                    SelectionKey key = (SelectionKey) it.next();

                    // channel ready to read?
                    if (key.isValid() && key.isReadable()) {

                        // read message and put on queue
                        cMsgClientData info = (cMsgClientData) key.attachment();
//System.out.println("client " + info.getName() + " is readable");
                        selChannel = key.channel();

                        // TCP channel being read
                        if (selChannel != udpChannel) {
                            sockChannel = (SocketChannel) selChannel;
                            // first read size of incoming data
                            if (info.readingSize) {
//System.out.println("  try reading size");
                                info.buffer.limit(4);
                                bytes = sockChannel.read(info.buffer);
//System.out.println("  done reading size, bytes = " + bytes);
                                // for End-of-stream ...
                                if (bytes == -1) {
                                    // error handling
                                    deleteClient(info);
                                    it.remove();
                                    continue;
                                }
                                // if we've read 4 bytes ...
                                if (info.buffer.position() > 3) {
                                    info.buffer.flip();
                                    info.size =info. buffer.getInt();
//System.out.println("  read size = " + info.size);
                                    info.buffer.clear();
                                    if (info.size > info.buffer.capacity()) {
//System.out.println("  create new, large direct bytebuffer");
                                        info.buffer = ByteBuffer.allocateDirect(info.size);
                                        info.buffer.clear();
                                    }
                                    info.buffer.limit(info.size);
                                    info.readingSize = false;
                                }
                            }

                            // read the rest of the data
                            if (!info.readingSize) {
//System.out.println("  try reading rest of udpBuffer");
//                            System.out.println("  udpBuffer capacity = " + info.udpBuffer.capacity() + ", limit = " + info.udpBuffer.limit()
//                             + ", position = " + info.udpBuffer.position() );
                                bytes = sockChannel.read(info.buffer);
                                // for End-of-stream ...
                                if (bytes == -1) {
                                    // error handling
                                    deleteClient(info);
                                    it.remove();
                                    continue;
                                }
                                info.bytesRead += bytes;
//System.out.println("  bytes read = " + info.bytesRead);

                                // if we've read everything ...
                                if (info.bytesRead >= info.size) {
                                    // put on Q, this will block if Q full
                                    try {
                                        byte[] b = new byte[info.bytesRead];
                                        info.buffer.flip();
                                        info.buffer.get(b, 0, info.bytesRead);
//System.out.println("  read request, putting udpBuffer in Q");
                                        //if (bufferQ.remainingCapacity() == 0) {
                                        //    System.out.println("   " + info.getName() + " has a FULL Q -> blocking");
                                        //}
                                        bufferQ.put(new cMsgHolder(b, info, false));
                                    }
                                    catch (InterruptedException e) {
                                        if (killMainThread) {
                                            return;
                                        }
                                    }
                                    info.buffer.clear();
                                    info.bytesRead = 0;
                                    info.readingSize = true;
                                    //it.remove();
                                    //continue;
                                }
                            }
                        }

                        // UDP channel being read
                        else {
                            try {
//System.out.println("client " + info.getName() + " is UDP readable");
                                udpChannel.receive(udpBuffer);
                                udpBuffer.flip();
                                if (udpBuffer.getInt() != cMsgNetworkConstants.magicNumbers[0] ||
                                    udpBuffer.getInt() != cMsgNetworkConstants.magicNumbers[1] ||
                                    udpBuffer.getInt() != cMsgNetworkConstants.magicNumbers[2]) {
                                    if (debug >= cMsgConstants.debugWarn) {
                                        System.out.println(" received bogus udp packet");
                                    }
                                    it.remove();
                                    continue;
                                }
//                                else {
//                                    System.out.println("passed magic # test");
//                                }
                                info.size = udpBuffer.getInt();
//System.out.println("  read size in UDP = " + info.size);
                                // if packet is too big, ignore it
                                if (4 + info.size > udpBuffer.capacity()) {
                                    it.remove();
                                    continue;
                                }

                                try {
                                    byte[] b = new byte[info.size];
                                    udpBuffer.get(b, 0, info.size);
//System.out.println("  read UDP request, putting udpBuffer in Q");
                                    bufferQ.put(new cMsgHolder(b, info, true));
                                }
                                catch (InterruptedException e) {
                                    if (killMainThread) {
                                        return;
                                    }
                                }
                                udpBuffer.clear();
                                info.bytesRead = 0;
                                info.readingSize = true;
                                //it.remove();
                                //continue;
                            }
                            catch (IOException e) {
                                // something wrong with packet, so just ignore it
                            }
                        }
                    }
                    // remove key from selected set since it's been handled
                    it.remove();
                }
            }
        }
        catch (IOException ex) {
            ex.printStackTrace();
        }
        finally {
            try {selector.close();}
            catch (IOException e) { }
        }

        return;
    }



    /**
     * Class to handle all client requests except the keep alives.
     */
    private class RequestHandler extends Thread {

        // variables to track message rate
        //double freq=0., freqAvg=0.;
        //long t1, t2, deltaT, totalT=0, totalC=0, count=0, loops=10000, ignore=5;


        /**
         * Constructor.
         */
        RequestHandler() {
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
            cMsgHolder holder;
            cMsgClientData info = null;

            while (true) {
                try {
                    // Grab item off cue. Can't use timeout due to bug in Java library (v4, v5).
                    try { holder = bufferQ.take(); }
                    catch (InterruptedException e) {
//System.out.println("RequestHandler thread ending");
                        return;
                    }

                    byte[] array = holder.array;
                    info = holder.data;
                    msgId = cMsgUtilities.bytesToInt(array, 0);

                    switch (msgId) {

                        case cMsgConstants.msgSendRequest: // client sending msg
                            if (holder.isUdpChannel) {
                                info.monData.udpSends++;
                            }
                            else {
                                info.monData.tcpSends++;
                            }
                            holder = readSendInfo(array, info);
                            info.subdomainHandler.handleSendRequest(holder.message);
                            break;

                        case cMsgConstants.msgSyncSendRequest:
                            info.monData.syncSends++;
                            holder = readSendInfo(array, info);
                            // this will need to be modified to act like subAndGet
                            int i = info.subdomainHandler.handleSyncSendRequest(holder.message);
                            info.getDeliverer().deliverMessage(i, holder.ssid, cMsgConstants.msgSyncSendResponse);
                            break;

                        case cMsgConstants.msgSubscribeAndGetRequest: // 1-shot subscription of subject & type
                            holder = readSubscribeInfo(array);
                            info.monData.subAndGets++;
                            // if not cMsg subdomain, just call subdomain handler
                            if (info.cMsgSubdomainHandler == null) {
                                info.subdomainHandler.handleSubscribeAndGetRequest(holder.subject,
                                                                                   holder.type,
                                                                                   holder.id);
                                break;
                            }
                            handleCmsgSubdomainSubscribeAndGet(holder, info);
                            break;

                        case cMsgConstants.msgUnsubscribeAndGetRequest: // ungetting subscribeAndGet request
//System.out.println("Domain Server: got msgUnsubscribeAndGetRequest from client");
                            holder = readSubscribeInfo(array);
                            // this will fire notifier if one exists (cmsg subdomain)
                            info.subdomainHandler.handleUnsubscribeAndGetRequest(holder.subject,
                                                                                 holder.type,
                                                                                 holder.id);
                            // for cmsg subdomain
                            if (info.cMsgSubdomainHandler != null) {
                                handleCmsgSubdomainUnsubscribeAndGet(holder, info);
                            }
                            break;

                        case cMsgConstants.msgSendAndGetRequest: // sending msg & expecting response msg
                            holder = readGetInfo(array, info);
                            info.monData.sendAndGets++;
//System.out.println("Domain Server: got msgSendAndGetRequest from client, ns = " + holder.namespace);
                            // If not cMsg subdomain just call subdomain handler.
                            if (info.cMsgSubdomainHandler == null) {
//System.out.println("Domain Server: call NON-CMSG subdomain send&Get");
                                info.subdomainHandler.handleSendAndGetRequest(holder.message);
                                break;
                            }
                            handleCmsgSubdomainSendAndGet(holder, info);
                            break;

                        case cMsgConstants.msgUnSendAndGetRequest: // ungetting sendAndGet
                            holder = readUngetInfo(array);
                            // This will fire notifier if one exists.
                            // The fired notifier will take care of unSendAndGetting any bridges.
//System.out.println("Domain Server: got msgUnSendAndGetRequest from client, ns = " + holder.namespace);
                            info.subdomainHandler.handleUnSendAndGetRequest(holder.id);
                            break;


                        case cMsgConstants.msgServerSendAndGetRequest: // server sending msg & expecting response msg
//System.out.println("Domain Server: got msgServerSendAndGetRequest from bridge client, ns = " + holder.namespace);
                            holder = readGetInfo(array, info);
                            info.cMsgSubdomainHandler.handleServerSendAndGetRequest(holder.message,
                                                                                    holder.namespace);
                            break;

                        case cMsgConstants.msgServerUnSendAndGetRequest: // server ungetting sendAndGet request
//System.out.println("Domain Server: got msgServerUnSendAndGetRequest from bridge client, ns = " + holder.namespace);
                            holder = readUngetInfo(array);
                            info.cMsgSubdomainHandler.handleUnSendAndGetRequest(holder.id);
                            break;

                        case cMsgConstants.msgSubscribeRequest: // subscribing to subject & type
                            holder = readSubscribeInfo(array);
                            info.monData.subscribes++;
                            info.subdomainHandler.handleSubscribeRequest(holder.subject,
                                                                         holder.type,
                                                                         holder.id);
                            // for cmsg subdomain
                            if (info.cMsgSubdomainHandler != null) {
                                handleCmsgSubdomainSubscribe(holder, info);
                            }
                            break;

                        case cMsgConstants.msgUnsubscribeRequest: // unsubscribing from a subject & type
                            holder = readSubscribeInfo(array);
                            info.monData.unsubscribes++;
                            info.subdomainHandler.handleUnsubscribeRequest(holder.subject,
                                                                           holder.type,
                                                                           holder.id);
                            // for cmsg subdomain
                            if (info.cMsgSubdomainHandler != null) {
                                handleCmsgSubdomainUnsubscribe(holder, info);
                            }
                            break;

                        case cMsgConstants.msgServerSubscribeRequest: // server subscribing to subject & type
//System.out.println("Domain Server: got serverSubscribe for bridge client, namespace = " + holder.namespace);
                            holder = readSubscribeInfo(array);
                            info.cMsgSubdomainHandler.handleServerSubscribeRequest(holder.subject,
                                                                                   holder.type,
                                                                                   holder.namespace);
                            break;

                        case cMsgConstants.msgServerUnsubscribeRequest: // server unsubscribing from a subject & type
//System.out.println("Domain Server: got serverUNSubscribe for bridge client");
                            holder = readSubscribeInfo(array);
                            info.cMsgSubdomainHandler.handleServerUnsubscribeRequest(holder.subject,
                                                                                     holder.type,
                                                                                     holder.namespace);
                            break;



                        case cMsgConstants.msgMonitorRequest: // client requesting monitor data   BUGBUG
                            sendMonitorData(nameServer.fullMonitorXML);
                            break;

                        case cMsgConstants.msgDisconnectRequest: // client disconnecting   BUGBUG
                            // need to shutdown this domain server
                            if (calledShutdown.compareAndSet(false, true)) {
//System.out.println("SHUTDOWN TO BE RUN BY msgDisconnectRequest");
                                shutdown();
                            }
                            return;

                        case cMsgConstants.msgServerShutdownSelf: // tell this name server to shutdown
                            nameServer.shutdown();
                            break;



                        case cMsgConstants.msgServerShutdownClients: // tell local clients to shutdown
                            holder = readShutdownInfo(array);
                            info.subdomainHandler.handleShutdownClientsRequest(holder.client, holder.include);
                            break;

                        case cMsgConstants.msgShutdownClients: // shutting down various clients
                            holder = readShutdownInfo(array);
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
                            holder = readShutdownInfo(array);
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

                        case cMsgConstants.msgServerRegistrationLock: // grab lock for global registration  BUGBUG
                            // Grabbing this lock may take up to 1/2 second
                            int delay = cMsgUtilities.bytesToInt(array, 4);
                            // Send yes (1) to indicate lock grabbed , or no (0) back as return value.
                            // This may block for up to 1/2 second.
                            boolean gotLock = info.cMsgSubdomainHandler.registrationLock(delay);
                            int answer =  gotLock ? 1 : 0;
                            info.getDeliverer().deliverMessage(answer, 0, cMsgConstants.msgServerRegistrationLockResponse);
                            break;

                        case cMsgConstants.msgServerCloudUnlock: // release lock for server cloud joining
                            try {
//System.out.println("TRY TO UNLOCK CLOUD");
                                nameServer.cloudUnlock();
//System.out.println("UNLOCKED CLOUD");
                            }
                            catch (Exception e) {
                                System.out.println("CANNOT UNLOCK CLOUD");
                                e.printStackTrace();
                            }
                            break;

                        case cMsgConstants.msgServerCloudLock: // grab lock for server cloud joining   BUGBUG
                            // Grabbing this lock may take up to 1/2 second
                            delay = cMsgUtilities.bytesToInt(array, 4);
                            // Send yes (1) to indicate lock grabbed , or no (0) back as return value.
                            // This may block for up to 0.2 seconds.
//System.out.println("DOMAIN SERVER: Try to lock cloud ...");
                            gotLock = nameServer.cloudLock(delay);
//System.out.println("DOMAIN SERVER:   gotLock = \"" + gotLock + "\", send reply");
                            answer = gotLock ? 1 : 0;
                            info.getDeliverer().deliverMessage(answer, 0, cMsgConstants.msgServerCloudLockResponse);
                            break;



                        case cMsgConstants.msgServerCloudSetStatus: // server client is joining cMsg subdomain server cloud
                            int status = cMsgUtilities.bytesToInt(array, 4);
                            setCloudStatus(status, info);
                            break;

                        case cMsgConstants.msgServerSendClientNames: // in cMsg subdomain send back all local client names
//System.out.println(">>    DS: got request to send client names");
                            info.getDeliverer().deliverMessage(info.cMsgSubdomainHandler.getClientNamesAndNamespaces(),
                                                               cMsgConstants.msgServerSendClientNamesResponse);
                            break;

                        default:
                            if (debug >= cMsgConstants.debugWarn) {
                                System.out.println("dServer handleClient: can't understand your message " + info.getName());
                            }
//System.out.println("Remove connection to client " + info.getName() + " since unknown command received");
//System.out.println("Call deleteClient 1");
                            deleteClient(info);
                    }
                }
                catch (cMsgException ex) {
//System.out.println("Call deleteClient 2");
                    deleteClient(info);
                }
                catch (IOException ex) {
//System.out.println("Call deleteClient 3");
                    deleteClient(info);
                }
            }
        }



        /**
         * This method changes the status of a bridge. Currently it is used to set the
         * status of another cMsg domain nameserver to "INCLOUD". All subscriptions and
         * subscribeAndGet calls still active are propagated to the newly joined server.
         *
         * @param status status to set the bridge to (only {@link cMsgNameServer#INCLOUD} allowed)
         */
        private void setCloudStatus(int status, cMsgClientData info) {

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
         * This method returns a monitoring data to the client.
         *
         * @param xml data string in xml format
         * @throws IOException If socket read or write error
         */
        private void sendMonitorData(String xml) throws IOException {
            // send the time in milliseconds as 2, 32 bit integers
            long now = new Date().getTime();
            info.streamToClient.writeInt((int) (now >>> 32)); // higher 32 bits
            info.streamToClient.writeInt((int) (now & 0x00000000FFFFFFFFL)); // lower 32 bits

            // send length of xml string to come
            info.streamToClient.writeInt(xml.length());

            // send xml string
            try {
                info.streamToClient.write(xml.getBytes("US-ASCII"));
            }
            catch (UnsupportedEncodingException e) {}

            info.streamToClient.flush();
        }


        /**
         * This method reads an incoming cMsgMessageFull from a client doing a
         * send or syncSend.
         *
         * @return object holding message read from channel
         * @throws IOException if socket read or write error
         */
        private cMsgHolder readSendInfo(byte[] array, cMsgClientData info) throws IOException {

            // create a message
            cMsgMessageFull msg = new cMsgMessageFull();

            // skip size
            int index = 4;
            // first incoming integer used for syncSend id
            int ssid = cMsgUtilities.bytesToInt(array, index);          index += 4;
            msg.setUserInt(cMsgUtilities.bytesToInt(array, index));     index += 4;
            msg.setSysMsgId(cMsgUtilities.bytesToInt(array, index));    index += 4;
            msg.setSenderToken(cMsgUtilities.bytesToInt(array, index)); index += 4;
            // mark msg as having been sent over wire
            msg.setInfo(cMsgUtilities.bytesToInt(array, index) | cMsgMessage.wasSent); index += 4;
            // mark msg as unexpanded
            msg.expandedPayload(false);

            // time message was sent = 2 ints (hightest byte first)
            // in milliseconds since midnight GMT, Jan 1, 1970
            long time = ((long) cMsgUtilities.bytesToInt(array, index) << 32) | ((long) cMsgUtilities.bytesToInt(array, index+4) & 0x00000000FFFFFFFFL);
            msg.setSenderTime(new Date(time));
            index += 8;

            // user time
            time = ((long) cMsgUtilities.bytesToInt(array, index) << 32) | ((long) cMsgUtilities.bytesToInt(array, index+4) & 0x00000000FFFFFFFFL);
            msg.setUserTime(new Date(time));
            index += 8;

            // String lengths
            int lengthSubject     = cMsgUtilities.bytesToInt(array, index);    index += 4;
            int lengthType        = cMsgUtilities.bytesToInt(array, index);    index += 4;
            int lengthPayloadTxt  = cMsgUtilities.bytesToInt(array, index);    index += 4;
            int lengthText        = cMsgUtilities.bytesToInt(array, index);    index += 4;
            int lengthBinary      = cMsgUtilities.bytesToInt(array, index);    index += 4;

            // read subject
            msg.setSubject(new String(array, index, lengthSubject, "US-ASCII"));
            //System.out.println("subject = " + msg.getSubject());
            index += lengthSubject;

            // read type
            msg.setType(new String(array, index, lengthType, "US-ASCII"));
            //System.out.println("type = " + msg.getType());
            index += lengthType;

            // read payload text       BUGBUG: expand payload??
            if (lengthPayloadTxt > 0) {
                String s = new String(array, index, lengthPayloadTxt, "US-ASCII");
                // setting the payload text is done by setFieldsFromText
                //System.out.println("payload text = " + s);
                index += lengthPayloadTxt;
                try {
                    msg.setFieldsFromText(s, cMsgMessage.allFields);
                }
                catch (cMsgException e) {
                    System.out.println("msg payload is in the wrong format: " + e.getMessage());
                }
            }

            // read text
            if (lengthText > 0) {
                msg.setText(new String(array, index, lengthText, "US-ASCII"));
                //System.out.println("text = " + msg.getText());
                index += lengthText;
            }
            else {
                msg.setText("");
            }

            // read binary array
            if (lengthBinary > 0) {
                try {
                    msg.setByteArrayNoCopy(array, index, lengthBinary);
                }
                catch (cMsgException e) {
                }
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
         * This method handles what needs to be done in the cMsg subdomain when
         * a subscribeAndGet request is made by the client.
         *
         * @param holder object that holds request information
         * @throws cMsgException if IO error in sending message
         */
        private void handleCmsgSubdomainSubscribeAndGet(cMsgHolder holder, cMsgClientData info) throws cMsgException {

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
//System.out.println("    DSS: call regular cmsg subdomain sub&Get");
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
//System.out.println("    DS: call bridge subscribe for " + b.serverName);
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
//System.out.println("    DS: call serverSub&GetRequest with id = " + holder.id);
                    info.cMsgSubdomainHandler.handleServerSubscribeAndGetRequest(holder.subject,
                                                                            holder.type,
                                                                            notifier);
                }

                // Keep track of this subscribeAndGet (just like we do in the cMsg
                // subdomain handler object) so later, if another server joins the
                // cloud, we can tell that server about the subscribeAndGet.
                boolean subscriptionExists = false;
                for (cMsgServerSubscribeInfo subscription : nameServer.subscriptions) {
                    sub = subscription;
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
        private void handleCmsgSubdomainUnsubscribeAndGet(cMsgHolder holder, cMsgClientData info) {
            // Cannot have servers joining cloud while a subscription is removed
            nameServer.subscribeLock.lock();
            try {
                cMsgServerSubscribeInfo sub = null;
                // keep track of all subscriptions removed by this client
                for (cMsgServerSubscribeInfo subscription : nameServer.subscriptions) {
// BUGBUG: cMsgServerSubscribeInfo needs to be updated with cMsgClientData
                    sub = subscription;
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
            }
            finally {
                nameServer.subscribeLock.unlock();
            }
        }


        /**
         * This method handles what needs to be done in the cMsg subdomain when
         * a sendAndGet request is made by the client.
         *
         * @param holder object that holds request information
         * @throws cMsgException if IO error in sending message
         */
        private void handleCmsgSubdomainSendAndGet(cMsgHolder holder, cMsgClientData info) throws cMsgException {
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
                   // if (debug >= cMsgConstants.debugWarn) {
                        System.out.println("DS requestThread: error on sendAndGet with server " +
                                           b.serverName);
                        e.printStackTrace();
                  //  }
                }
            }

            cMsgServerSendAndGetter getter =
                    new cMsgServerSendAndGetter(nameServer, notifier, sendAndGetters);
            sendAndGetters.put(notifier.id, getter);
            sendAndGetThreadPool.execute(getter);


        }


        /**
         * This method reads an incoming cMsgMessageFull from a client doing a sendAndGet.
         *
         * @return object holding message read from channel
         * @throws IOException if socket read or write error
         */
        private cMsgHolder readGetInfo(byte[] array, cMsgClientData info) throws IOException {

            // create a message
            cMsgMessageFull msg = new cMsgMessageFull();

            // skip size, skip first incoming integer (for future use)
            int index = 8;

            msg.setUserInt(cMsgUtilities.bytesToInt(array, index));     index += 4;
            msg.setSenderToken(cMsgUtilities.bytesToInt(array, index)); index += 4;
            // mark msg as having been sent over wire
            msg.setInfo(cMsgUtilities.bytesToInt(array, index) | cMsgMessage.wasSent); index += 4;
            // mark msg as unexpanded
            msg.expandedPayload(false);

            // time message was sent = 2 ints (hightest byte first)
            // in milliseconds since midnight GMT, Jan 1, 1970
            long time = ((long) cMsgUtilities.bytesToInt(array, index) << 32) | ((long) cMsgUtilities.bytesToInt(array, index+4) & 0x00000000FFFFFFFFL);
            msg.setSenderTime(new Date(time));
            index += 8;

            // user time
            time = ((long) cMsgUtilities.bytesToInt(array, index) << 32) | ((long) cMsgUtilities.bytesToInt(array, index+4) & 0x00000000FFFFFFFFL);
            msg.setUserTime(new Date(time));
            index += 8;

            // String lengths
            int lengthSubject     = cMsgUtilities.bytesToInt(array, index);    index += 4;
            int lengthType        = cMsgUtilities.bytesToInt(array, index);    index += 4;
            int lengthNamespace   = cMsgUtilities.bytesToInt(array, index);    index += 4;
            int lengthPayloadTxt  = cMsgUtilities.bytesToInt(array, index);    index += 4;
            int lengthText        = cMsgUtilities.bytesToInt(array, index);    index += 4;
            int lengthBinary      = cMsgUtilities.bytesToInt(array, index);    index += 4;

            // read subject
            msg.setSubject(new String(array, index, lengthSubject, "US-ASCII"));
            //System.out.println("subject = " + msg.getSubject());
            index += lengthSubject;

            // read type
            msg.setType(new String(array, index, lengthType, "US-ASCII"));
            //System.out.println("type = " + msg.getType());
            index += lengthType;

            // read namespace
            String ns = null;
            if (lengthNamespace > 0) {
                ns = new String(array, index, lengthNamespace, "US-ASCII");
                index += lengthNamespace;
            }

            // read payload text    BUGBUG: expand payload??
            if (lengthPayloadTxt > 0) {
                String s = new String(array, index, lengthPayloadTxt, "US-ASCII");
                msg.setPayloadText(s);
                index += lengthPayloadTxt;
                // setting the payload text is done by setFieldsFromText
                //System.out.println("payload text = " + s);
//                try {
//                    msg.setFieldsFromText(s, cMsgMessage.allFields);
//                }
//                catch (cMsgException e) {
//                    System.out.println("msg payload is in the wrong format: " + e.getMessage());
//                }
            }

            // read text
            if (lengthText > 0) {
                msg.setText(new String(array, index, lengthText, "US-ASCII"));
                //System.out.println("text = " + msg.getText());
                index += lengthText;
            }

            // read binary array, copy the data? BUGBUG
            if (lengthBinary > 0) {
                try {
                    msg.setByteArrayNoCopy(array, index, lengthBinary);
                }
                catch (cMsgException e) {
                }
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
         * This method reads an incoming unSendAndGet request from a client.
         *
         * @return object holding id read from channel
         * @throws IOException if socket read or write error
         */
        private cMsgHolder readUngetInfo(byte[] array) throws IOException {

            // id of subject/type combination  (senderToken actually)
            cMsgHolder holder = new cMsgHolder();
            // id of subject/type combination  (receiverSubscribedId)
            holder.id = cMsgUtilities.bytesToInt(array, 4);

            return holder;
        }


        /**
         * This method reads an incoming (un)subscribe or (un)subscribeAndGet
         * request from a client.
         *
         * @return object holding subject, type, namespace and id read from channel
         * @throws IOException if socket read or write error
         */
        private cMsgHolder readSubscribeInfo(byte[] array) throws IOException {
            cMsgHolder holder = new cMsgHolder();

            // skip size
            int index = 4;

            // id of subject/type combination  (receiverSubscribedId)
            holder.id           = cMsgUtilities.bytesToInt(array, index); index += 4;
            // length of subject
            int lengthSubject   = cMsgUtilities.bytesToInt(array, index); index += 4;
            // length of type
            int lengthType      = cMsgUtilities.bytesToInt(array, index); index += 4;
            // length of namespace
            int lengthNamespace = cMsgUtilities.bytesToInt(array, index); index += 4;

            // read subject
            holder.subject = new String(array, index, lengthSubject, "US-ASCII");
            index += lengthSubject;

            // read type
            holder.type = new String(array, index, lengthType, "US-ASCII");
            index += lengthType;

            // read namespace
            if (lengthNamespace > 0) {
                holder.namespace = new String(array, index, lengthNamespace, "US-ASCII");
            }

            return holder;
        }


        /**
         * This method handles what extra things need to be done in the
         * cMsg subdomain when a subscribe request is made by the client.
         *
         * @param holder object that holds request information
         * @throws cMsgException if trying to add more than 1 identical subscription
         */
        private void handleCmsgSubdomainSubscribe(cMsgHolder holder, cMsgClientData info) throws cMsgException {
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
                 for (cMsgServerSubscribeInfo subscription : nameServer.subscriptions) {
                     sub = subscription;
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
         private void handleCmsgSubdomainUnsubscribe(cMsgHolder holder, cMsgClientData info) {
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
                 for (cMsgServerSubscribeInfo subscription : nameServer.subscriptions) {
                     sub = subscription;
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


        /**
         * This method reads incoming information from a client doing a shutdown
         * of other clients or servers.
         *
         * @return object holding message read from channel
         * @throws IOException if socket read or write error
         */
        private cMsgHolder readShutdownInfo(byte[] array) throws IOException {

            // skip size
            int index = 4;
            int flag         = cMsgUtilities.bytesToInt(array, index); index += 4;
            int lengthClient = cMsgUtilities.bytesToInt(array, index); index += 4;

            // read client
            String client = new String(array, index, lengthClient, "US-ASCII");

            return new cMsgHolder(client, (flag == 1));
        }


    }



}

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
import java.util.concurrent.locks.ReentrantLock;
import java.util.concurrent.locks.Condition;
import java.nio.channels.Selector;
import java.nio.channels.SelectionKey;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;

import org.jlab.coda.cMsg.*;

/**
 * This class implements a cMsg domain server in the cMsg domain. If this class is
 * ever rewritten in a way that allows multiple threads to concurrently access the
 * clientHandler object, the clientHandler object must be rewritten to synchronize the
 * subscribe and unsubscribe methods.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgDomainServer extends Thread {

    /** Type of domain this is. */
    private String domainType = "cMsg";

    /** Port number listening on. */
    private int port;

    /** Host this is running on. */
    private String host;

    /** Keep reference to cMsg name server which created this object. */
    cMsgHandleRequests clientHandler;

    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugNone;

    /**
     * Object containing information about the domain client.
     * Certain members of info can only be filled in by this thread,
     * such as the listening port & host.
     */
    cMsgClientInfo info;

    /** Server channel (contains socket). */
    ServerSocketChannel serverChannel;

    private static final int MAX_THREADS = 50;
    private ThreadPool pool = new ThreadPool(10, MAX_THREADS);

    /** Tell the server to kill this and all spawned threads. */
    boolean killAllThreads;

    /** Kill this and all spawned threads. */
    public void killAllThreads() {
        killAllThreads = true;
    }

    /**
     * Gets boolean value specifying whether to kill this and all spawned threads.
     *
     * @return value specifying whether this thread has been told to kill itself or not
     */
    public boolean getKillAllThreads() {
        return killAllThreads;
    }


    /**
     * Gets object which handles client requests.
     *
     * @return client handler object
     */
    public cMsgHandleRequests getClientHandler() {
        return clientHandler;
    }

    /**
     * Constructor which starts threads.
     *
     * @param handler object which handles all requests from the client
     * @param info object containing information about the client for which this
     *                    domain server was started
     * @param startingPort suggested port on which to starting listening for connections
     * @throws cMsgException If a port to listen on could not be found
     */
    public cMsgDomainServer(cMsgHandleRequests handler, cMsgClientInfo info, int startingPort) throws cMsgException {
        this.clientHandler = handler;
        // Port number to listen on
        port = startingPort;
        this.info = info;

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
        info.domainPort = port;
        try {
            host = InetAddress.getLocalHost().getHostName();
        }
        catch (UnknownHostException ex) {
        }
        info.domainHost = host;

        // Start thread to monitor client's health.
        // If he dies, kill this thread.
        cMsgMonitorClient monitor =  new cMsgMonitorClient(info, this);
        monitor.setDaemon(true);
        monitor.start();
    }


    /**
     * Method to be run when this server's client is dead or disconnected and
     * the server threads will be killed. It runs the "shutdown" method of its
     * cMsgHandleRequest (subdomain handler) object.
     *
     * Finalize methods are run after an object has become unreachable and
     * before the garbage collector is run;
     */
    public void finalize() throws cMsgException {
        clientHandler.handleClientShutdown();
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
                // 3 second timeout
                int n = selector.select(3000);

                // if no channels (sockets) are ready, listen some more
                if (n == 0) {
                    // but first check to see if we've been commanded to die
                    if (getKillAllThreads()) {
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
                        // let us know (in the next select call) if this socket is ready to read
                        cMsgUtilities.registerChannel(selector, channel, SelectionKey.OP_READ);

                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("cMsgDomainServer: new connection from " +
                                               info.name + "\n");
                        }
                    }

                    // is there data to read on this channel?
                    if (key.isValid() && key.isReadable()) {
                        //SocketChannel channel = (SocketChannel) key.channel();
                        handleClient(key);
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
            System.out.println("\n\nQuitting Domain Server");
        }

        return;
    }


    private void handleClient(SelectionKey key) {
        WorkerThread worker = pool.getWorker();

        if (worker == null) {
            // No threads available, do nothing, the selection
            // loop will keep calling this method until a
            // thread becomes available.  This design could
            // be improved.
            return;
        }

        // invoking this wakes up the worker thread then returns
        worker.serviceChannel(key);
    }


    /**
     * A very simple thread pool class. The pool size is set at
     * construction time but can expand up to a limit. Threads are
     * cycled through a FIFO idle queue.
     */
    private class ThreadPool {
        /** Thread-safe queue to hold worker threads. */
        LinkedBlockingQueue<WorkerThread> cue;
        /** Current number of threads in pool. */
        int poolSize;
        /** Size limit of pool. */
        final int sizeLimit;

        /**
         * Constructor with initial pool size and limit inputs.
         * @param poolSize initial number of worker threads in pool
         * @param sizeLimit maximum number of worker threads in pool
         */
        ThreadPool(int poolSize, int sizeLimit) {
            this.poolSize  = poolSize;
            this.sizeLimit = sizeLimit;
            cue = new LinkedBlockingQueue<WorkerThread>(sizeLimit);

            // fill up the pool with worker threads
            for (int i = 0; i < poolSize; i++) {
                WorkerThread thread = new WorkerThread(this);

                // set thread name for debugging, start it
                thread.setName("Worker" + (i + 1));
                thread.start();

                // add to list of idle threads
                cue.offer(thread);
            }
        }

        /**
         * Find an idle worker thread, if any.  Could return null.
         * @return worker thread
         */
        WorkerThread getWorker() {
            WorkerThread worker = null;

            // return first available thread
            worker = cue.poll();
            if (worker != null) {
                return worker;
            }

            synchronized (cue) {
                if (poolSize < sizeLimit) {
                    // increment while mutex protected
                    poolSize++;

                    // create another worker
                    worker = new WorkerThread(this);
                    worker.setName("Worker" + poolSize);
                    worker.start();
                    return worker;
                }
            }

            // block here waiting for next thread
            try { worker = cue.take(); }
            catch (InterruptedException e) {}

            return (worker);
        }

        /**
         * Called by the worker thread to return itself to the idle pool.
         * @param worker thread to be returned to the pool
         */
        void returnWorker(WorkerThread worker) {
            if (!cue.offer(worker)) {
                return;
            }
        }
    }


    /**
     * A worker thread class which reads the socket for a client instruction
     * and information. There is one client connection for one worker thread.
     * Each instance is constructed with a reference to the owning thread
     * pool object. When started, the thread is forever waiting to be awakened
     * to service by the channel associated with a SelectionKey object.
     *
     * The worker is tasked by calling its serviceChannel() method
     * with a SelectionKey object.  The serviceChannel() method stores
     * the key reference in the thread object then calls notify()
     * to wake it up.  When the channel has been dealt with, the worker
     * thread returns itself to its parent pool.
     */
    private class WorkerThread extends Thread {
        /** A direct buffer is necessary for nio socket IO. */
        private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);
        /** Thread pool this worker thread belongs to. */
        private ThreadPool pool;
        /** From select statement. */
        private SelectionKey key;

        /** Allocate int array once (used for reading in data) for efficiency's sake. */
        private int[] inComing = new int[10];

        /** Allocate byte array once (used for reading in data) for efficiency's sake. */
        byte[] bytes = new byte[5000];

        // The following 3 members are holders for information that comes from the client
        // so that information can be passed on to the object which handles all the client
        // requests.

        /** Identifier that uniquely determines the subject/type pair for a client subscription. */
        private int receiverSubscribeId;
        /** Message subject. */
        private String subject;
        /** Message type. */
        private String type;

        /**
         * Lock used with condition variable to wait on signal. Slightly more
         * efficient than wait & notify.
         */
        private ReentrantLock lock = new ReentrantLock();
        /** Condition variable to wait on signal. */
        private Condition threadNeeded = lock.newCondition();


        /**
         * Constructor.
         * @param pool reference to thread pool this worker thread is a member of
         */
        WorkerThread(ThreadPool pool) {
            this.pool = pool;
        }

        /** Loop forever waiting for work to do. */
        public void run() {

            //System.out.println(this.getName() + " is ready");

            while (true) {
                lock.lock();
                try {
                    while (key == null)  {
                        threadNeeded.await();
                    }
                }
                catch (InterruptedException e) {
                    e.printStackTrace();
                    // clear interrupt status
                    this.interrupted();
                }
                finally {
                    lock.unlock();
                }

/*
                try {
                    // sleep and release object lock
                    this.wait();
                }
                catch (InterruptedException e) {
                    e.printStackTrace();
                    // clear interrupt status
                    this.interrupted();
                }

                if (key == null) {continue;}
*/

                try {
                    handleClient(key);
                }
                catch (Exception e) {
                    System.out.println("Caught '" + e + "' closing channel");
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("dServer handleClient: I/O ERROR in cMsg client " + info.name + ": " + e.getMessage());
                    }

                    // close channel and nudge selector
                    try {
                        key.channel().close();
                    }
                    catch (IOException ex) {
                        ex.printStackTrace();
                    }

                    key.selector().wakeup();
                }

                key = null;

                // done, ready for more, return to pool
                this.pool.returnWorker(this);
            }
        }

        /**
         * Called to initiate a unit of work by this worker thread
         * on the provided SelectionKey object.  This method is
         * synchronized, as is the run() method, so only one key
         * can be serviced at a given time.
         * Before waking the worker thread, and before returning
         * to the main selection loop, this key's interest set is
         * updated to remove OP_READ.  This will cause the selector
         * to ignore read-readiness for this channel while the
         * worker thread is servicing it.
         *
         * @param key selection key containing socket to client
         */
        void serviceChannel(SelectionKey key) {
            this.key = key;
            key.interestOps(key.interestOps() & (~SelectionKey.OP_READ));
            // awaken the thread
            //this.notify();
            lock.lock();
            threadNeeded.signal();
            lock.unlock();
        }


        /**
         * This method handles all communication between a cMsg user who has
         * connected to a domain and this server for that domain.
         *
         * @param key selection key contains socket to client
         */
        private void handleClient(SelectionKey key) throws IOException, cMsgException {
            int msgId = 0, answer = 0;
            cMsgMessage msg;
            SocketChannel channel = (SocketChannel) key.channel();

            // keep reading until we have an int (4 bytes) of data
            if (cMsgUtilities.readSocketBytes(buffer, channel, 4, debug) < 4) {
                // got less than 1 int, something's wrong, kill connection
                throw new cMsgException("Command from client is not proper format");
            }

            // make buffer readable
            buffer.flip();

            // read client's request
            msgId = buffer.getInt();

            switch (msgId) {

                case cMsgConstants.msgSendRequest: // receiving a message
                    // read the message here
                    //if (debug >= cMsgConstants.debugInfo) {
                    //    System.out.println("dServer handleClient: got SEND request from " + info.name);
                    //}
                    msg = readSendInfo(channel);
                    clientHandler.handleSendRequest(msg);
                    break;

                case cMsgConstants.msgSyncSendRequest: // receiving a message
                    // read the message here
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("dServer handleClient: got syncSend request from " + info.name);
                    }
                    msg = readSendInfo(channel);
                    answer = clientHandler.handleSyncSendRequest(msg);
                    syncSendReply(channel, answer);
                    break;

                case cMsgConstants.msgGetRequest: // getting a message of subject & type
                    // read the message here
                    //if (debug >= cMsgConstants.debugInfo) {
                    //    System.out.println("dServer handleClient: got GET request from " + info.name);
                    //}
                    // get subject, type, and receiverSubscribeId
                    msg = readGetInfo(channel);
                    clientHandler.handleGetRequest(msg);
                    break;

                case cMsgConstants.msgUngetRequest: // ungetting from a subject & type
                    // read the subject and type here
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("dServer handleClient: got unget request from " + info.name);
                    }
                    readSubscribeInfo(channel);
                    clientHandler.handleUngetRequest(subject, type);
                    break;

                case cMsgConstants.msgSubscribeRequest: // subscribing to subject & type
                    // read the subject and type here
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("dServer handleClient: got subscribe request from " + info.name);
                    }
                    // get subject, type, and receiverSubscribeId
                    readSubscribeInfo(channel);
                    clientHandler.handleSubscribeRequest(subject, type, receiverSubscribeId);
                    break;

                case cMsgConstants.msgUnsubscribeRequest: // unsubscribing from a subject & type
                    // read the subject and type here
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("dServer handleClient: got unsubscribe request from " + info.name);
                    }
                    readSubscribeInfo(channel);
                    clientHandler.handleUnsubscribeRequest(subject, type, receiverSubscribeId);
                    break;

                case cMsgConstants.msgKeepAlive: // see if this end is still here
                    //if (debug >= cMsgConstants.debugInfo) {
                    //    System.out.println("dServer handleClient: got keep alive from " + info.name);
                    //}
                    // send ok back as acknowledgment
                    buffer.clear();
                    buffer.putInt(cMsgConstants.ok).flip();
                    while (buffer.hasRemaining()) {
                        channel.write(buffer);
                    }
                    clientHandler.handleKeepAlive();
                    break;

                case cMsgConstants.msgDisconnectRequest: // client disconnecting
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("dServer handleClient: got disconnect from " + info.name);
                    }
                    // send ok back as acknowledgment
                    buffer.clear();
                    buffer.putInt(cMsgConstants.ok).flip();
                    while (buffer.hasRemaining()) {
                        channel.write(buffer);
                    }
                    // close channel and unregister from selector
                    channel.close();
                    // tell client handler to shutdown
                    clientHandler.handleClientShutdown();
                    // need to shutdown this domain server
                    killAllThreads();
                    break;

                case cMsgConstants.msgShutdown: // told this domain server to shutdown
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("handleClient: got shutdown from " + info.name);
                    }
                    // send ok back as acknowledgment
                    buffer.clear();
                    buffer.putInt(cMsgConstants.ok).flip();
                    while (buffer.hasRemaining()) {
                        channel.write(buffer);
                    }
                    // close channel and unregister from selector
                    channel.close();
                    // tell client handler to shutdown
                    clientHandler.handleClientShutdown();
                    // need to shutdown this domain server
                    killAllThreads();
                    break;

                default:
                    if (debug >= cMsgConstants.debugWarn) {
                        System.out.println("dServer handleClient: can't understand your message " + info.name);
                    }
                    break;
            }

            // resume interest in OP_READ
            key.interestOps(key.interestOps() | SelectionKey.OP_READ);

            // cycle the selector so this key is active again
            key.selector().wakeup();

            return;
        }


        /**
         * This method reads an incoming cMsgMessage from a client.
         *
         * @param channel nio socket communication channel
         * @return message read from channel
         * @throws IOException If socket read or write error
         */
        private cMsgMessage readSendInfo(SocketChannel channel) throws IOException {

            // create a message
            cMsgMessage msg = new cMsgMessage();

            // keep reading until we have 10 ints of data
            cMsgUtilities.readSocketBytes(buffer, channel, 40, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // read 10 ints
            buffer.asIntBuffer().get(inComing, 0, 10);

            // system message id
            msg.setSysMsgId(inComing[0]);
            // is sender doing get request?
            msg.setGetRequest(inComing[1] == 0 ? false : true);
            // is sender doing get response?
            msg.setGetResponse(inComing[2] == 0 ? false : true);
            // sender id
            msg.setSenderId(inComing[3]);
            // time message sent in seconds since midnight GMT, Jan 1, 1970
            msg.setSenderTime(new Date(((long) inComing[4]) * 1000));
            // sender message id
            msg.setSenderMsgId(inComing[5]);
            // sender token
            msg.setSenderToken(inComing[6]);
            // length of message subject
            int lengthSubject = inComing[7];
            // length of message type
            int lengthType = inComing[8];
            // length of message text
            int lengthText = inComing[9];

            // bytes expected
            int bytesToRead = lengthSubject + lengthType + lengthText;

            // read in all remaining bytes
            cMsgUtilities.readSocketBytes(buffer, channel, bytesToRead, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // allocate bigger byte array if necessary
            // (allocate more than needed for speed's sake)
            if (bytesToRead > bytes.length) {
                bytes = new byte[bytesToRead];
                System.out.println("DS:  ALLOCATING BUFFER, bytes = " + bytesToRead);
            }

            // read into array
            buffer.get(bytes, 0, bytesToRead);

            // read subject
            msg.setSubject(new String(bytes, 0, lengthSubject, "US-ASCII"));

            // read type
            msg.setType(new String(bytes, lengthSubject, lengthType, "US-ASCII"));

            // read text
            msg.setText(new String(bytes, lengthSubject + lengthType, lengthText, "US-ASCII"));

            // fill in message object's members
            msg.setDomain(domainType);
            msg.setReceiver("cMsg domain server");
            msg.setReceiverHost(host);
            msg.setReceiverTime(new Date()); // current time
            msg.setSender(info.name);
            msg.setSenderHost(info.clientHost);

            return msg;
        }


        /**
         * This method reads an incoming cMsgMessage from a client doing a "get".
         *
         * @param channel nio socket communication channel
         * @return message read from channel
         * @throws IOException If socket read or write error
         */
        private cMsgMessage readGetInfo(SocketChannel channel) throws IOException {

            // create a message
            cMsgMessage msg = new cMsgMessage();

            // keep reading until we have 9 ints of data
            cMsgUtilities.readSocketBytes(buffer, channel, 36, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // read 9 ints
            buffer.asIntBuffer().get(inComing, 0, 9);

            // is sender doing specific get or just 1-shot subscribe?
            msg.setGetRequest(inComing[0] == 1 ? true : false);
            // sender's unique receiverSubscribeId (for general get)
            msg.setReceiverSubscribeId(inComing[1]);
            // sender id
            msg.setSenderId(inComing[2]);
            // time message sent in seconds since midnight GMT, Jan 1, 1970
            msg.setSenderTime(new Date(((long) inComing[3]) * 1000));
            // sender message id
            msg.setSenderMsgId(inComing[4]);
            // sender token (for specific get)
            msg.setSenderToken(inComing[5]);
            // length of message subject
            int lengthSubject = inComing[6];
            // length of message type
            int lengthType = inComing[7];
            // length of message text
            int lengthText = inComing[8];

            // bytes expected
            int bytesToRead = lengthSubject + lengthType + lengthText;

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

            // fill in message object's members
            msg.setDomain(domainType);
            msg.setReceiver("cMsg domain server");
            msg.setReceiverHost(host);
            msg.setReceiverTime(new Date()); // current time
            msg.setSender(info.name);
            msg.setSenderHost(info.clientHost);

            return msg;
        }


        /**
         * This method returns the value received from the subdomain handler object's
         * handleSyncSend method to the client.
         *
         * @param channel nio socket communication channel
         * @param answer  handleSyncSend return value to pass to client
         * @throws IOException If socket read or write error
         */
        private void syncSendReply(SocketChannel channel, int answer) throws IOException {

            // send back answer
            buffer.clear();
            buffer.putInt(answer).flip();
            while (buffer.hasRemaining()) {
                channel.write(buffer);
            }
        }


        /**
         * This method reads an incoming subscribe request from a client.
         *
         * @param channel nio socket communication channel
         * @throws IOException If socket read or write error
         */
        private void readSubscribeInfo(SocketChannel channel) throws IOException {
            // keep reading until we have 3 ints of data
            cMsgUtilities.readSocketBytes(buffer, channel, 12, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // read 3 ints
            buffer.asIntBuffer().get(inComing, 0, 3);

            // id of subject/type combination  (receiverSubscribedId)
            receiverSubscribeId = inComing[0];
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
            subject = new String(bytes, 0, lengthSubject, "US-ASCII");
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  subject = " + subject);
            }

            // read type
            type = new String(bytes, lengthSubject, lengthType, "US-ASCII");
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  type = " + type);
            }

            return;
        }

    }
 }

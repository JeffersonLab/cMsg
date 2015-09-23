/*----------------------------------------------------------------------------*
 *  Copyright (c) 2008        Jefferson Science Associates,                   *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 18-Jun-2008, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.server;

import org.jlab.coda.cMsg.cMsgConstants;
import org.jlab.coda.cMsg.cMsgNetworkConstants;
import org.jlab.coda.cMsg.cMsgException;

import java.nio.channels.*;
import java.nio.ByteBuffer;
import java.net.InetSocketAddress;
import java.net.SocketException;
import java.io.IOException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.Iterator;

/**
 * This class creates all permanent connections to clients and passes these sockets
 * to either a cMsgDomainServer or cMsgDomainServerSelect object.
 */
class cMsgConnectionHandler extends Thread {

    /** Convenient storage class. */
    private class clientInfoStorage {
        /** How many connections have been made for this client now? */
        int connectionsMade;

        /** Did attempt to make connections time out? */
        boolean timedOut;

        /**
         * Two connections are to be made. The first is for sending messages to server and responses
         * back to client. The second is for keepalives and monitoring data to go back and forth.
         * The question is, which connection was made first?
         */
        boolean messageSocketFirst = true;

        /** Object containing information about the client now trying to connect to this server. */
        cMsgClientData info;

        /** Use this object to signal caller that both the client's connections have been made.  */
        CountDownLatch finishedConnectionsLatch = new CountDownLatch(1);
    }


    /** Map in which to store the clients currently trying to connect and their associated info. */
    ConcurrentHashMap<Integer, clientInfoStorage> clients = new ConcurrentHashMap<Integer, clientInfoStorage>(100);

    /** Name server object. */
    cMsgNameServer nameServer;

    /** Server channel (contains socket). */
    private ServerSocketChannel serverChannel;

    /**
     * Unique key sent to connecting clients for ID purposes
     * (so this object can identify its responses).
     */
    private AtomicInteger clientKey = new AtomicInteger(0);

    /** Kill this thread if true. */
    volatile boolean killThisThread;

    /** Debug level. */
    int debug;


    /**
     * Constructor.
     * @param nameServer server which contains this object.
     * @param debug debug level which controls debugging output.
     */
    public cMsgConnectionHandler(cMsgNameServer nameServer, int debug) {
        this.nameServer = nameServer;
        this.debug = debug;
    }

    /** Kills this thread. */
    void killThread() {
        killThisThread = true;
        this.interrupt();
    }

    /**
     * Get a unique key to use as client identification which is used
     * in forming the 2 permanent client connections using this object.
     *
     * @return unique key
     */
    synchronized public int getUniqueKey() {
        int uniqueKey;

        // check to see if key is already an entry in the hash table (highly unlikely)
        while (true) {
            uniqueKey = clientKey.incrementAndGet();
            if (!clients.containsKey(uniqueKey)) {
                break;
            }
        }

        return uniqueKey;
    }

    /**
     * This method allows 2 connections from a client to begin.
     * It should be used in conjunction with {@link #gotConnections} which notifies
     * caller that connections were made or timeout occurred.<p>
     * Be sure to call this method <b>before</b> sending the name server response to the
     * the initial client communication.
     *
     * @param info client info object
     */
    public void allowConnections(cMsgClientData info) {
        // create object to store data of interest
        clientInfoStorage storage = new clientInfoStorage();
        storage.info = info;

        // store that object for later retrieval
        clients.put(info.clientKey, storage);
    }


    /**
     * This method determines whether or not 2 connections from a client were made
     * after the process was initialized by the method {@link #allowConnections}.
     * This method returns when the connections are complete or after the timeout period
     * (whichever is sooner).<p>
     * Be sure to call this method <b>after</b> finishing the name server response to the
     * initial client communication.
     *
     * @param info client info object
     * @param secondsToWait number of seconds to wait for connections to be made before returning false
     * @return {@code true} if connections made, else {@code false}
     */
    public boolean gotConnections(cMsgClientData info, int secondsToWait) {

        boolean gotConnections = false;
        int clientKey = info.clientKey;

        clientInfoStorage storage = clients.get(clientKey);
        if (storage == null) {
            return false;
        }

        try {
            // wait for notification that both connections are complete
            gotConnections = storage.finishedConnectionsLatch.await(secondsToWait, TimeUnit.SECONDS);
        }
        catch (InterruptedException e) {}

        // if we've timed out or been interrupted, clean up after ourselves
        if (!gotConnections) {
            cleanupConnections(storage);
        }
        // else general clean up
        else {
            clients.remove(clientKey);
        }

        return gotConnections;
    }


    /**
     * Cleanup debris when connections are not made, partially made,
     * or {@link #gotConnections} has timed out.
     * @param storage object storing client info
     */
    public void cleanupConnections(clientInfoStorage storage) {
        if (storage == null) return;

        // coordinate use of "storage" with server thread
        synchronized (storage) {
            clients.remove(storage.info.clientKey);
            storage.timedOut = true;

            if (storage.info.getMessageChannel() != null) {
                try {
                    storage.info.getMessageChannel().close();
                }
                catch (IOException e) { }
            }

            if (storage.info.keepAliveChannel != null) {
                try {
                    storage.info.keepAliveChannel.close();
                }
                catch (IOException e) { }
            }
        }
    }


    /**
     * This method is a thread which listens for TCP connections from the client.
     * There are 2 connections from each client. The first is a socket for the main
     * communication between this server and the client. The second socket is for keep
     * alives between them.
     */
    public void run() {

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println(">>    CCH: Running Client Connection Handler");
        }
        Selector selector = null;

        // Create channel and bind to port. If that isn't possible, exit.
        try {
            serverChannel = ServerSocketChannel.open();
            serverChannel.socket().setReuseAddress(true);
            serverChannel.socket().bind(new InetSocketAddress(nameServer.domainServerPort));
        }
        catch (IOException ex) {
            ex.printStackTrace();
            System.out.println("Exiting Server: cannot open a listening socket on port " + nameServer.domainServerPort);
            System.exit(-1);
        }

        try {
            // get things ready for a select call
            selector = Selector.open();
            // set nonblocking mode for the listening socket
            serverChannel.configureBlocking(false);
            // register the channel with the selector for accepts
            serverChannel.register(selector, SelectionKey.OP_ACCEPT);

        }
        catch (IOException ex) {
            ex.printStackTrace();
            System.out.println("Exiting Server: cannot register listening socket with selector");
            System.exit(-1);
        }

        // tell startServer() that this thread has started
        nameServer.preConnectionThreadsStartedSignal.countDown();

        try {

            while (true) {

                try {
                    // 1 second timeout
                    int n = selector.select(1000);

                    // first check to see if we've been commanded to die
                    if (killThisThread) return;

                    // if no channels (sockets) are ready, listen some more
                    if (n == 0) continue;

                    // get an iterator of selected keys (ready sockets)
                    Iterator it = selector.selectedKeys().iterator();

                    // look at each key
                    while (it.hasNext()) {

                        SelectionKey key = (SelectionKey) it.next();
                        it.remove();

                        if (!key.isValid()) {
                            return;
                        }

                        // is this a new connection coming in?
                        if (key.isAcceptable()) {
                            // accept the connection from the client
                            SocketChannel sc = ((ServerSocketChannel) key.channel()).accept();
                            sc.configureBlocking( false );
                            sc.socket().setTcpNoDelay( true ); // stop Nagling, send all data immediately
                            sc.register( selector, SelectionKey.OP_READ );
//System.out.println(">>    CCH: register new client connection");
                        }

                        // is this a channel open for reading?
                        else if (key.isReadable()) {

                            SocketChannel channel = (SocketChannel) key.channel();

                            clientInfoStorage storage;
                            try {
                                storage = readIncomingMessage(key);
                                // if read not complete ...
                                if (storage == null) {
                                    continue;
                                }
                            }
                            catch (Exception e) {
                                channel.close(); // this will cancel key
                                continue;
                            }

//System.out.println(">>    CCH: new connection, num = " + storage.connectionsMade);

                            // Synchronization is used to coordinate with the gotConnections()
                            // method which may be in the process of timing out for this client.
                            synchronized (storage) {
                                // if this client has already timedout, forget about it
                                if (!storage.timedOut) {
                                    // record connection just made
                                    storage.connectionsMade++;

                                    // 1st connection from client
                                    if (storage.connectionsMade == 1) {
                                        if (storage.messageSocketFirst) {
                                            // set TCP buffer sizes
                                            channel.socket().setSendBufferSize(131072);
                                            channel.socket().setReceiveBufferSize(131072);
                                            // save channel in info object
                                            storage.info.setMessageChannel(channel);
                                        }
                                        else {
                                            channel.socket().setSendBufferSize(131072);// was 8192
                                            channel.socket().setReceiveBufferSize(8192);
                                            storage.info.keepAliveChannel = channel;
                                        }

                                        // record when client connected
                                        storage.info.monData.birthday = System.currentTimeMillis();
                                    }
                                    // 2nd connection from client
                                    else if (storage.connectionsMade == 2) {
                                        if (storage.messageSocketFirst) {
                                            channel.socket().setSendBufferSize(131072);  // was 8192
                                            channel.socket().setReceiveBufferSize(8192);
                                            storage.info.keepAliveChannel = channel;
                                        }
                                        else {
                                            channel.socket().setSendBufferSize(131072);
                                            channel.socket().setReceiveBufferSize(131072);
                                            storage.info.setMessageChannel(channel);
                                            storage.info.monData.birthday = System.currentTimeMillis();
                                        }
                                        
                                        // report back that connections from this client are done
                                        storage.finishedConnectionsLatch.countDown();
                                    }
                                }
                            }

                            // Don't need to listen to channel (here) anymore.
                            // These channels are used by other threads.
                            key.cancel();
                        }
                    }
                }
                catch (IOException ex) { }
            }
        }
        finally {
            try {serverChannel.close();} catch (IOException e) { }
            try {selector.close();}      catch (IOException e) { }
        }
    }




    /**
     * An intelligent read-into-bytebuffer method that seamlessly copes with partial reads.
     *
     * NOTE: this server REQUIRES that all incoming messages be preceded by a 3 "magic" ints
     * which comprise the magic password and must also include the key sent from the cMsgNameServer
     * to the client before trying to connect here.
     *
     * @param key key from return of "select" call
     * @return client info storage object if finished reading, else null
     * @throws IOException
     * @throws cMsgException if client sends magic numbers that are wrong or client key that is bad
     */
    private clientInfoStorage readIncomingMessage( SelectionKey key ) throws IOException, cMsgException {

        // The number of bytes to read from a client initially
        int BYTES_TO_READ = 20;

        SocketChannel channel = (SocketChannel) key.channel();

        // Fetch the buffer we were using on a previous partial read,
        // or create a new one from scratch.
        ByteBuffer buffer = (ByteBuffer) key.attachment();
        if (buffer == null) {
            buffer = ByteBuffer.allocate(BYTES_TO_READ);
            key.attach(buffer);
            buffer.clear();
            buffer.limit(BYTES_TO_READ);
        }

        // Check to see if this is a legitimate client or some imposter.
        // Don't want to block on read since it may not be a real client
        // and may block forever - tying up the server.

        // read data
        int bytesRead = channel.read(buffer);

        // if end-of-stream ...
        if (bytesRead == -1) {
            throw new IOException("Socket lost connection during read operation");
        }

//System.out.println("readIncomingMessage:  bytes read = " + bytesRead + ", position = " + buffer.position());

        // if read all necessary data ...
        if (buffer.position() >= BYTES_TO_READ) {

            // look to see if client's sent the right magic #s
            buffer.flip();

            int magic1 = buffer.getInt();
            int magic2 = buffer.getInt();
            int magic3 = buffer.getInt();

            if (magic1 != cMsgNetworkConstants.magicNumbers[0] ||
                magic2 != cMsgNetworkConstants.magicNumbers[1] ||
                magic3 != cMsgNetworkConstants.magicNumbers[2])  {

                throw new cMsgException("Wrong magic numbers sent from client");
            }

            // look to see if client's sent a good client key
            int uniqueClientKey = buffer.getInt();

            clientInfoStorage storage = clients.get(uniqueClientKey);
            if (storage == null) {
                throw new cMsgException("Bad key sent from client or timed out");
            }

            // Look to see which connection was made first.
            // Channel type is 1 for message, 2 for keepalive.
            int channelType = buffer.getInt();
            if (storage.connectionsMade < 1) {
                storage.messageSocketFirst = channelType == 1;
            }

            // Send one byte back so cmsg client can determine if connection failed or not.
            // Normally this is not an issue; however, when using ssh port forwarding / tunnels,
            // the ssh client is not timely in reporting errors. This speeds up the return of an
            // error to the cmsg client.
            buffer.clear();
            buffer.put((byte)9);
            buffer.flip();

            channel.write(buffer);

            return storage;
        }

        // try reading more later
        return null;
    }



}

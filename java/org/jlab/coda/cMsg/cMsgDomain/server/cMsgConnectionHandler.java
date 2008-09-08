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

import java.nio.channels.*;
import java.nio.ByteBuffer;
import java.net.Socket;
import java.net.InetSocketAddress;
import java.io.IOException;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.Iterator;

/**
 * This class creates all permanent connections to clients and passes these sockets
 * to either a cMsgDomainServer or cMsgDomainServerSelect object.
 */
public class cMsgConnectionHandler extends Thread {

    /**
     * Object containing information about the client this object is connected to.
     * Certain members of info can only be filled in by this thread,
     * such as the listening port & host.
     */
    cMsgClientData info;

    /** Name server object. */
    cMsgNameServer nameServer;

    /** Debug level. */
    int debug;

    /** Server channel (contains socket). */
    private ServerSocketChannel serverChannel;

    /** Which connection are we making now? A zero value prevents spurious connections from doing evil. */
    int connectionNumber;

    /** Kill this thread if true. */
    volatile boolean killThisThread;

    /**
     * Use this to signal that this server's listening thread has been started
     * so bridges may be created.
     */
    CountDownLatch finishedConnectionsLatch;


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
     * This method makes 2 connections to a client. It returns when the connections are complete
     * with a timeout of 1 second. Be sure to call this method before finishing the server
     * response to the initial client communication so it can be waiting for the expected
     * client connections.
     *
     * @param info client info object
     * @return true if connections made, else false
     */
    synchronized public boolean makeConnections(cMsgClientData info) {
        this.info = info;
        connectionNumber = 1;
        finishedConnectionsLatch = new CountDownLatch(1);
        try {
            boolean ok = finishedConnectionsLatch.await(1, TimeUnit.SECONDS);
            connectionNumber = 0;
            return ok;
        }
        catch (InterruptedException e) {}
        connectionNumber = 0;
        return false;
    }


    /**
     * This method allows 2 connections from a client to begin.
     * It should be used in conjunction with {@link #gotConnections}, and
     * both of these methods should be used while being simultaneously
     * synchronized or protected by the same lock.
     * Be sure to call this method before finishing the name server response to
     * the initial client communication so it can be waiting for the expected
     * client connections. 
     *
     * @param info client info object
     */
    synchronized public void allowConnections(cMsgClientData info) {
        this.info = info;
        connectionNumber = 1;
        finishedConnectionsLatch = new CountDownLatch(1);
    }


    /**
     * This method determines whether or not 2 connections from a client were made
     * after the process was initialized by the method {@link #allowConnections}.
     * Both of these methods should be used while being simultaneously
     * synchronized or protected by the same lock.
     * This method returns when the connections are complete (timeout of 1 second).
     * Be sure to call this method after finishing the name server response to the
     * initial client communication.
     *
     * @return true if connections made, else false
     */
    synchronized public boolean gotConnections() {
        try {
            boolean ok = finishedConnectionsLatch.await(1, TimeUnit.SECONDS);
            connectionNumber = 0;
            return ok;
        }
        catch (InterruptedException e) {}
        connectionNumber = 0;
        return false;
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
            System.out.println("Exiting Server: cannot registor listening socket with selector");
            System.exit(-1);
        }

        /* Direct buffer for reading 3 magic ints with nonblocking IO. */
        int BYTES_TO_READ = 12;
        ByteBuffer buffer = ByteBuffer.allocateDirect(BYTES_TO_READ);

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
                    keyLoop:
                     while (it.hasNext()) {
                         SelectionKey key = (SelectionKey) it.next();

                        // is this a new connection coming in?
                        if (!key.isValid()) {
                            return;
                        }

                        if (key.isAcceptable()) {
                            
                            ServerSocketChannel server = (ServerSocketChannel) key.channel();
                            // accept the connection from the client
                            SocketChannel channel = server.accept();

                            // Check to see if this is a legitimate rc server client or some imposter.
                            // Don't want to block on read here since it may not be a real client
                            // and may block forever - tying up the server.
                            int bytes, bytesRead=0, loops=0;
                            buffer.clear();
                            buffer.limit(BYTES_TO_READ);
                            channel.configureBlocking(false);

                            // read magic numbers
                            while (bytesRead < BYTES_TO_READ) {
//System.out.println("  try reading rest of Buffer");
//System.out.println("  Buffer capacity = " + buffer.capacity() + ", limit = " + buffer.limit()
//                    + ", position = " + buffer.position() );
                                bytes = channel.read(buffer);
                                // for End-of-stream ...
                                if (bytes == -1) {
                                    it.remove();
                                    continue keyLoop;
                                }
                                bytesRead += bytes;
//System.out.println("  bytes read = " + bytesRead);

                                // if we've read everything, look to see if it's sent the magic #s
                                if (bytesRead >= BYTES_TO_READ) {
                                    buffer.flip();
                                    int magic1 = buffer.getInt();
                                    int magic2 = buffer.getInt();
                                    int magic3 = buffer.getInt();
                                    if (magic1 != cMsgNetworkConstants.magicNumbers[0] ||
                                        magic2 != cMsgNetworkConstants.magicNumbers[1] ||
                                        magic3 != cMsgNetworkConstants.magicNumbers[2])  {
//System.out.println("  Magic numbers did NOT match");
                                        it.remove();
                                        continue keyLoop;
                                    }
                                }
                                else {
                                    // give client 10 loops (.1 sec) to send its stuff, else no deal
                                    if (++loops > 10) {
                                        it.remove();
                                        continue keyLoop;
                                    }
                                    try { Thread.sleep(10); }
                                    catch (InterruptedException e) { }
                                }
                            }

                            // set socket options
                            Socket socket = channel.socket();
                            // Set tcpNoDelay so no packets are delayed
                            socket.setTcpNoDelay(true);
 
//System.out.println(">>    CCH: new connection, num = " + connectionNumber);

                            // The 1st connection is for a client request handling thread
                            if (connectionNumber == 1) {
                                // set recv buffer size
                                socket.setReceiveBufferSize(131072);
                                // save it in info object
                                info.setMessageChannel(channel);
                                // record when client connected
                                info.monData.birthday = System.currentTimeMillis();
                                // next connection should be 2nd from this client
                                connectionNumber = 2;
                            }
                            // The 2nd connection is for a keep alive thread
                            else if (connectionNumber == 2) {
                                // set recv buffer size
                                socket.setReceiveBufferSize(4096);
                                // save it in info object
                                info.keepAliveChannel = channel;
                                // if there is a spurious connection to this port, ignore it
                                connectionNumber = 0;
                                // done with this client
                                finishedConnectionsLatch.countDown();
                            }
                            // spurious connection (ie. port-scanning)
                            else if (connectionNumber == 0) {
                                channel.close();
                                if (debug >= cMsgConstants.debugInfo) {
                                    System.out.println(">>    CCH: attempt at spurious connection");
                                }
                            }

                            if (debug >= cMsgConstants.debugInfo) {
                                System.out.println(">>    CCH: new connection " + (connectionNumber - 1) + " from " +
                                        info.getName());
                            }
                        }

                        it.remove();
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

}

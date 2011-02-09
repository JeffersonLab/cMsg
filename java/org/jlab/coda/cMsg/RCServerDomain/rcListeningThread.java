/*----------------------------------------------------------------------------*
 *  Copyright (c) 2006        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 20-Nov-2006, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.RCServerDomain;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgCallbackThread;
import org.jlab.coda.cMsg.common.cMsgGetHelper;
import org.jlab.coda.cMsg.common.cMsgMessageFull;
import org.jlab.coda.cMsg.common.cMsgSubscription;

import java.io.BufferedInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.net.*;
import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.channels.SelectionKey;
import java.nio.channels.Selector;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.charset.Charset;
import java.util.Date;
import java.util.Iterator;
import java.util.Set;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * This class implements a thread to listen to runcontrol clients in the
 * runcontrol server domain over TCP.
 */
class rcListeningThread extends Thread {

    /** Start here looking for an unused TCP port. */
    static private AtomicInteger startingTcpPort =
            new AtomicInteger(cMsgNetworkConstants.rcServerPort);

    /** Type of domain this is. */
    private String domainType = "rcs";

    /** cMsg server that created this object. */
    private RCServer server;

    /** Tcp server listening port. */
    int tcpPort;

    /** Server channel (contains socket). */
    private ServerSocketChannel serverChannel;

    /** Level of debug output for this class. */
    private int debug;

    /** Setting this to true will kill this thread. */
    private boolean killThread;




    /** Kills this thread. */
    void killThread() {
        // stop threads that get commands/messages over sockets
        killThread = true;
        this.interrupt();
    }


    /**
     * Get the TCP listening port of this server.
     * @return TCP listening port of this server
     */
    public int getTcpPort() {
        return tcpPort;
    }


    /**
     * Constructor for regular clients.
     *
     * @param server RC server that created this object
     */
    public rcListeningThread(RCServer server) throws cMsgException {

        this.server = server;
        debug = server.getDebug();

        createTCPServerChannel();

        // die if no more non-daemon thds running
        setDaemon(true);
    }


    /**
     * Creates a TCP listening socket for a runcontrol client to connect to.
     *
     * @throws org.jlab.coda.cMsg.cMsgException if socket cannot be created or cannot bind to a port
     */
    private void createTCPServerChannel() throws cMsgException {

        ServerSocket listeningSocket;
        try {
            serverChannel = ServerSocketChannel.open();
            listeningSocket = serverChannel.socket();
            listeningSocket.setReuseAddress(true);
        }
        catch (IOException e) {
            // close channel
            if (serverChannel != null) try { serverChannel.close(); } catch (IOException e1) { }
            throw new cMsgException("connect: cannot create server socket", e);
        }

        //----------------------------------------
        // start looking for a TCP listening port
        //----------------------------------------

        synchronized (startingTcpPort) {

            // If starting port # incremented too much so it cycled around, reset to orig value.
            if (startingTcpPort.incrementAndGet() < cMsgNetworkConstants.rcServerPort) {
                startingTcpPort.set(cMsgNetworkConstants.rcServerPort);
            }

            // If starting rc server port # is running into starting rc client port #'s,
            // it means that there are more clients than the difference between to 2
            // starting port #s. So try hopping 3x the difference to a group of (hopefully)
            // unused ports.
            int diff = cMsgNetworkConstants.rcClientPort - cMsgNetworkConstants.rcServerPort;
            if ((diff > 0) && (startingTcpPort.get() == cMsgNetworkConstants.rcClientPort)) {
                if (diff < 500) diff = 500;
                startingTcpPort.set(cMsgNetworkConstants.rcClientPort + 3*diff);
            }

            tcpPort = startingTcpPort.get();
//System.out.println("start at port " + port);
            // At this point, find a port to bind to. If that isn't possible, throw
            // an exception.
            while (true) {
                try {
//System.out.println("rcServer/tcp list thd: bind TCP socket to " + port);
                    listeningSocket.bind(new InetSocketAddress(tcpPort));
                    break;
                }
                catch (IOException ex) {
                    // try another port by adding one
                    if (tcpPort < 65535) {
                        tcpPort++;
//System.out.println("rcServer/tcp list thd: try another port, " + port);
                        try { Thread.sleep(20);  }
                        catch (InterruptedException e) {}
                    }
                    else {
                        // close channel
                        try { serverChannel.close(); } catch (IOException e) { }
                        tcpPort = 0;
                        ex.printStackTrace();
                        throw new cMsgException("connect: cannot find port to listen on", ex);
                    }
                }
            }

            startingTcpPort.set(tcpPort + 1);
//System.out.println("TCP on " + port);
        }
    }


    /** This method is executed as a thread. */
    public void run() {

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("Running Client Listening Thread:");
        }

        // Direct buffer for reading TCP nonblocking IO
        ByteBuffer buffer = ByteBuffer.allocateDirect(16384);

        // rc client channel
        SocketChannel myChannel = null;

        // Socket input stream associated with channel
        DataInputStream in = null;

        cMsgMessageFull msg;
        boolean readingSize = true;
        int bytes, bytesRead=0, size=0, msgId=0;

        Selector selector = null;

        try {
            // get things ready for a select call
            selector = Selector.open();

            // set nonblocking mode for the listening socket
            serverChannel.configureBlocking(false);

            // register the channel with the selector for accepts
            serverChannel.register(selector, SelectionKey.OP_ACCEPT);

            // RC server object is waiting for this thread to start in connect method,
            // so tell it we've started.
            synchronized(this) {
                notifyAll();
            }

            while (true) {
                // 2 second timeout
                int n = selector.select(2000);

                // if no channels (sockets) are ready, listen some more
                if (n < 1) {
                    // but first check to see if we've been commanded to die
                    if (killThread) return;

                    selector.selectedKeys().clear();
                    continue;
                }

                if (killThread) return;

                // get an iterator of selected keys (ready sockets)
                Iterator it = selector.selectedKeys().iterator();

                // look at each key
                while (it.hasNext()) {
                    SelectionKey key = (SelectionKey) it.next();

                    // is this a new connection coming in?
                    if (key.isValid()) {

                        // accept connection from rc client
                        if (key.isAcceptable()) {
                            ServerSocketChannel server = (ServerSocketChannel) key.channel();
                            // accept the connection from the client
                            SocketChannel channel = server.accept();
                            channel.configureBlocking(false);

                            // set socket options
                            Socket socket = channel.socket();
                            // Set tcpNoDelay so no packets are delayed
                            socket.setTcpNoDelay(true);
                            // set buffer sizes
                            socket.setReceiveBufferSize(65535);
                            socket.setSendBufferSize(65535);

                            if (debug >= cMsgConstants.debugInfo) {
                                System.out.println("rcTcpListeningThread: new connection");
                            }

                            // register this channel (socket) for reading
                            channel.register(selector, SelectionKey.OP_READ);

                            // save channel for later use
                            myChannel = channel;

                            // buffered communication streams for efficiency
                            in = new DataInputStream(new BufferedInputStream(
                                     myChannel.socket().getInputStream(), 65536));

                            buffer.clear();
                            buffer.limit(8);
                            readingSize = true;
                        }

                        // read input from rc client
                        else if (key.isReadable()) {

//System.out.println("  try reading size & msgId");
                            // FIRST, read size & msgId
                            if (readingSize) {
                                try {
                                    bytes = myChannel.read(buffer);
                                }
                                catch (IOException e) {
                                    // client has died
                                    key.cancel();
                                    it.remove();
                                    continue;
                                }

                                // for End-of-stream ...
                                if (bytes == -1) {
                                    // error handling
//System.out.println("  TCP ERROR: reading size & msgId for channel");
//System.out.println("           : pos = " + buffer.position());
                                    if (buffer.position() > 3) {
//System.out.println("           : size = " + buffer.getInt());
                                    }
                                    key.cancel();
                                    it.remove();
                                    continue;
                                }

                                // if we've read 8 bytes (2 ints) ...
                                if (buffer.position() > 7) {
                                    buffer.flip();
                                    size  = buffer.getInt();
                                    msgId = buffer.getInt();
//System.out.println("  read size = " + size + ", msgId = " + msgId);
                                    if (size-4 > buffer.capacity()) {
//System.out.println("  create new, large direct bytebuffer from " + clientData.buffer.capacity() + " to " + clientData.size);
                                        buffer = ByteBuffer.allocateDirect(size-4);
                                    }

                                    buffer.clear();
                                    buffer.limit(size-4);
                                    readingSize = false;
                                    bytesRead = 0;
                                }
                            }

                            // SECOND, read message after size & msgId read
                            if (!readingSize) {
                                // fully read buffer before parsing into cMsg message
                                try {
//System.out.println("  try reading rest of buffer");
//System.out.println("  buffer capacity = " + buffer.capacity() + ", limit = " +
//                   buffer.limit() + ", position = " + buffer.position() );
                                    bytes = myChannel.read(buffer);
                                }
                                catch (IOException ex) {
                                    // client has died
                                    key.cancel();
                                    it.remove();
                                    continue;
                                }

                                // for End-of-stream ...
                                if (bytes == -1) {
                                    key.cancel();
                                    it.remove();
                                    continue;
                                }

                                bytesRead += bytes;
//System.out.println("  bytes read = " + bytesRead);
                                if (bytesRead >= size-4) {
                                    buffer.flip();

                                    switch (msgId) {

                                        case cMsgConstants.msgSubscribeResponse: // receiving a message
                                            // read the message here
                                            msg = readIncomingMessageNB(buffer);

                                            // run callbacks for this message
                                            runCallbacks(msg);
                                            break;

                                        case cMsgConstants.msgGetResponse: // receiving a message for sendAndGet
                                            // read the message
                                            msg = readIncomingMessageNB(buffer);
                                            msg.setGetResponse(true);

                                            // wakeup caller with this message
                                            wakeGets(msg);
                                            break;

                                        default:
                                            if (debug >= cMsgConstants.debugWarn) {
                                                System.out.println("rcTcpListeningThread: can't understand rc client message = " + msgId);
                                            }
                                            break;
                                    }

                                    bytesRead = 0;
                                    readingSize = true;
                                    buffer.clear();
                                    buffer.limit(8);
                                }
                            }
                        }
                    }

                    // remove key from selected set since it's been handled
                    it.remove();
                }
            }
        }
        catch (IOException ex) {
            if (debug >= cMsgConstants.debugError) {
                System.out.println("rcTcpListenThread: I/O ERROR in rc server");
                System.out.println("rcTcpListenThread: close TCP server socket, port = " +
                        myChannel.socket().getLocalPort());
                ex.printStackTrace();
            }
        }
        finally {
            try {if (in != null) in.close();}               catch (IOException ex) {}
            try {if (myChannel != null) myChannel.close();} catch (IOException ex) {}
            try {serverChannel.close();} catch (IOException ex) {}
            try {selector.close();}      catch (IOException ex) {}
        }

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("Quitting TCP Listening Thread");
        }

        return;
    }



    /**
     * This method reads an incoming message from the RC client.
     *
     * @return message read from channel
     * @throws java.io.IOException if socket read or write error
     */
    private cMsgMessageFull readIncomingMessageNB(ByteBuffer buffer) {

        int len;

        // create a message
        cMsgMessageFull msg = new cMsgMessageFull();

        msg.setVersion(buffer.getInt());
        msg.setUserInt(buffer.getInt());
        // mark the message as having been sent over the wire & having expanded payload
        msg.setInfo(buffer.getInt() | cMsgMessage.wasSent | cMsgMessage.expandedPayload);
        msg.setSenderToken(buffer.getInt());

        // time message was sent = 2 ints (hightest byte first)
        // in milliseconds since midnight GMT, Jan 1, 1970
        long time = (buffer.getLong());
        msg.setSenderTime(new Date(time));

        // user time
        time = (buffer.getLong());
        msg.setUserTime(new Date(time));

        // String lengths
        int lengthSender      = buffer.getInt();
        int lengthSubject     = buffer.getInt();
        int lengthType        = buffer.getInt();
        int lengthPayloadTxt  = buffer.getInt();
        int lengthText        = buffer.getInt();
        int lengthBinary      = buffer.getInt();

        // decode buffer as ASCII into CharBuffer
        Charset cs = Charset.forName("ASCII");
        CharBuffer chBuf = cs.decode(buffer);

        // read sender
        msg.setSender(chBuf.subSequence(0, lengthSender).toString());
//System.out.println("sender = " + msg.getSender());
        len = lengthSender;

        // read subject
        msg.setSubject(chBuf.subSequence(len, len+lengthSubject).toString());
//System.out.println("subject = " + msg.getSubject());
        len += lengthSubject;

        // read type
        msg.setType(chBuf.subSequence(len,len+lengthType).toString());
//System.out.println("type = " + msg.getType());
        len += lengthType;

        // read payload text
        if (lengthPayloadTxt > 0) {
            String s = chBuf.subSequence(len,len+lengthPayloadTxt).toString();
            // setting the payload text is done by setFieldsFromText
//System.out.println("payload text = " + s);
            len += lengthPayloadTxt;
            try {
                msg.setFieldsFromText(s, cMsgMessage.allFields);
            }
            catch (cMsgException e) {
                System.out.println("msg payload is in the wrong format: " + e.getMessage());
            }
        }

        // read text
        if (lengthText > 0) {
            msg.setText(chBuf.subSequence(len,len+lengthText).toString());
            len += lengthText;
//System.out.println("text = " + msg.getText());
        }

        // read binary array
        if (lengthBinary > 0) {
            byte[] array = new byte[lengthBinary];
            buffer.position(buffer.position()+len);
            buffer.get(array, 0, lengthBinary);
            msg.setByteArrayNoCopy(array);
        }

        // fill in message object's members
        msg.setDomain(domainType);
        msg.setReceiver(server.getName());
        msg.setReceiverHost(server.getHost());
        msg.setReceiverTime(new Date()); // current time

//System.out.println("MESSAGE RECEIVED\n\n");

        return msg;
    }


    /**
     * This method runs all appropriate callbacks - each in their own thread -
     * for server subscribe and subscribeAndGet calls.
     *
     * @param msg incoming message
     */
    private void runCallbacks(cMsgMessageFull msg) {

        if (server.subscribeAndGets.size() > 0) {
            // for each subscribeAndGet called by this server ...
            cMsgSubscription sub;
            for (Iterator i = server.subscribeAndGets.values().iterator(); i.hasNext();) {
                sub = (cMsgSubscription) i.next();
                if (sub.matches(msg.getSubject(), msg.getType())) {

                    sub.setTimedOut(false);
                    sub.setMessage(msg.copy());
                    // Tell the subscribeAndGet-calling thread to wakeup
                    // and retrieve the held msg
                    synchronized (sub) {
                        sub.notify();
                    }
                }
                i.remove();
            }
        }

        // handle subscriptions
        Set<cMsgSubscription> set = server.subscriptions;

        if (set.size() > 0) {
            // if callbacks have been stopped, return
            if (!server.isReceiving()) {
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("runCallbacks: all subscription callbacks have been stopped");
                }
                return;
            }

            // set is NOT modified here
            synchronized (set) {
                // for each subscription of this server ...
                for (cMsgSubscription sub : set) {
                    // if subject & type of incoming message match those in subscription ...
                    if (sub.matches(msg.getSubject(), msg.getType())) {
                        // run through all callbacks
                        for (cMsgCallbackThread cbThread : sub.getCallbacks()) {
                            // The callback thread copies the message given
                            // to it before it runs the callback method on it.
                            cbThread.sendMessage(msg);
                        }
                    }
                }
            }
        }
    }


    /**
     * This method wakes up a thread in a server waiting in the sendAndGet method
     * and delivers a message to it.
     *
     * @param msg incoming message
     */
    private void wakeGets(cMsgMessageFull msg) {

        cMsgGetHelper helper = server.sendAndGets.remove(msg.getSenderToken());
        if (helper == null) {
            return;
        }
        helper.setTimedOut(false);
        // Do NOT need to copy msg as only 1 receiver gets it
        helper.setMessage(msg);

        // Tell the sendAndGet-calling thread to wakeup and retrieve the held msg
        synchronized (helper) {
            helper.notify();
        }
    }



}

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
import java.nio.channels.*;
import java.nio.charset.Charset;
import java.util.Date;
import java.util.Iterator;
import java.util.Set;
import java.util.concurrent.CountDownLatch;
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

    /** Tcp listening port. */
    int tcpPort;

    /** Udp listening port. */
    int udpPort;

    /** Tcp server channel (contains socket). */
    private ServerSocketChannel serverChannel;

    /** Channel to receive UDP sends from the clients. */
    private DatagramChannel udpChannel;

    /** RC client channel */
    private SocketChannel tcpChannel;

    /** Socket input stream associated with RC client channel */
    private DataInputStream in;

    /** Level of debug output for this class. */
    private int debug;

    /** Setting this to true will kill this thread. */
    private boolean killThread;

    /** Let connect() know that the client has established a connection to this server. */
    final CountDownLatch startLatch = new CountDownLatch(1);

    /** Exit handler for listening thread. */
    private Thread.UncaughtExceptionHandler exitHandler;


    /** Define exit handler for listening thread. */
    private class MyExitHandler implements Thread.UncaughtExceptionHandler {
        /**
         * Method invoked when the given thread terminates due to the
         * given uncaught exception.
         * <p>Any exception thrown by this method will be ignored by the
         * Java Virtual Machine.
         * @param t the thread
         * @param e the exception
         */
        public void uncaughtException(Thread t, Throwable e) {
            // Close all streams & sockets before exiting thread
System.out.println("rcListeningThread: invoke listening thread EXIT HANDLER to close sockets");
            try {
                if (rcListeningThread.this.in != null) rcListeningThread.this.in.close();
            } catch (Exception ex) {}

            try {
                if (rcListeningThread.this.tcpChannel != null) rcListeningThread.this.tcpChannel.close();
            } catch (Exception ex) {}

            try {
                if (rcListeningThread.this.udpChannel != null) rcListeningThread.this.udpChannel.close();
            } catch (Exception ex) {}

            try {
                rcListeningThread.this.serverChannel.close();
            } catch (Exception ex) {}
        }
    }



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
     * Get the UDP listening port of this server.
     * @return UDP listening port of this server
     */
    public int getUdpPort() {
        return udpPort;
    }


    /**
     * Constructor for regular clients.
     *
     * @param server RC server that created this object
     */
    public rcListeningThread(RCServer server) throws cMsgException {

        this.server = server;
        debug = server.getDebug();
        udpPort = server.localUdpPort;

        createTCPServerChannel();
        createUDPServerChannel();

        // Die if no more non-daemon threads running
        setDaemon(true);

        // Create exit handler for listening thread
        exitHandler = new MyExitHandler();
        setUncaughtExceptionHandler(exitHandler);
    }


    /**
     * Creates a UDP receiving socket for a runcontrol client to send to.
     *
     * @throws IOException if socket cannot be created
     */
    private void createUDPServerChannel() throws cMsgException {
        // For the client who wants to do sends with udp,
        // create a socket on an available udp port.
        try {
            // Create socket to receive at all interfaces
            udpChannel = DatagramChannel.open();
            DatagramSocket udpSocket = udpChannel.socket();

            // First try the port given in the UDL (if any).
            if (udpPort > 0) {
                try {
                    udpSocket.bind(new InetSocketAddress(udpPort));
//System.out.println("rcListeningThread: listening on url specified UDP port " + udpPort);
                }
                catch (SocketException e) {
                    // bind to ephemeral port since error
                    udpSocket.bind(new InetSocketAddress(0));
//System.out.println("rcListeningThread: error binding, listening on ephemeral port " + udpSocket.getLocalPort());
                }
            }
            else {
                // bind to ephemeral port
                udpSocket.bind(new InetSocketAddress(0));
//System.out.println("rcListeningThread: listening on ephemeral port " + udpSocket.getLocalPort());
            }

            udpPort = udpSocket.getLocalPort();
            udpSocket.setReuseAddress(true);
            udpSocket.setReceiveBufferSize(cMsgNetworkConstants.biggestUdpBufferSize);
        }
        catch (IOException ex) {
            if (udpChannel != null) try { udpChannel.close(); } catch (IOException e1) { }
            cMsgException e = new cMsgException("rcListeningThread: cannot create UDP server socket", ex);
            e.setReturnCode(cMsgConstants.errorSocket);
            throw e;
        }
    }


    /**
     * Creates a TCP listening socket for a runcontrol client to connect to.
     *
     * @throws cMsgException if socket cannot be created or cannot bind to a port
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
            throw new cMsgException("rcListeningThread: cannot create TCP server socket", e);
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
            // starting port #s. So try hopping 1500 to a group of (hopefully)
            // unused ports.
            if (startingTcpPort.get() == cMsgNetworkConstants.rcClientPort) {
                startingTcpPort.set(cMsgNetworkConstants.rcClientPort + 1500);
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


    long lastPrintTime = 0L;

    /** This method is executed as a thread. */
    public void run() {

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("Running Client Listening Thread:");
        }

        // Reference to either tcpBuffer or udpBuffer
        ByteBuffer dataBuffer = null;

        // We want separate buffers for TCP & UDP. The TCP sender sends
        // all data from 1 message together. The UDP sender sends a packet
        // which this thread receives at once. A problem could arise with
        // only 1 buffer on this end if, a partial TCP read is done and that
        // is followed by a UDP read which would clear the buffer and wipe
        // out what was partially read for TCP.

        // Direct buffer for reading TCP nonblocking IO
        ByteBuffer tcpBuffer = ByteBuffer.allocateDirect(16384);

        // Direct byte Buffer for UDP IO use
        ByteBuffer udpBuffer = ByteBuffer.allocateDirect(cMsgNetworkConstants.biggestUdpBufferSize);

        String channelType;
        cMsgMessageFull msg;
        boolean readingSize = true, okToParseMsg = false;
        int bytes, bytesRead=0, size=0, msgId=0;
        int prescalePrintOut = 0;
        SocketAddress senderAddress;

        Selector selector = null;

        try {
            // get things ready for a select call
            selector = Selector.open();

            // set nonblocking mode for the listening socket
            serverChannel.configureBlocking(false);

            // register the channel with the selector for accepts
            serverChannel.register(selector, SelectionKey.OP_ACCEPT);

            try {
                // set nonblocking mode for the udp socket
                udpChannel.configureBlocking(false);

                // register the channel with the selector for reading
                udpChannel.register(selector, SelectionKey.OP_READ, "UDP");
            }
            catch (IOException e) { /* should never happen */ }

            // RC server object is waiting for this thread to start in connect method,
            // so tell it we've started.
            synchronized(this) {
                notifyAll();
            }

            while (true) {
                // 2 second timeout
                int n = selector.select(2000);

                // if no additional channels (sockets) are ready
                // since last select call, listen some more.
                if (n < 1) {
                    // but first check to see if we've been commanded to die
                    if (killThread) return;

                    selector.selectedKeys().clear();

                    // If it's been over 6 minutes since printing out udp msg,
                    // then print out here instead.
                    long deltaT = System.currentTimeMillis() - lastPrintTime;
                    if (deltaT >= 360000) {
System.out.println("rcListeningThread: " + server.getName() + " woke from select, time since last print = " + (deltaT/1000) + " sec");
                        lastPrintTime = System.currentTimeMillis();
                    }

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
                                System.out.println("rcListeningThread: new connection");
                            }

                            // register this channel (socket) for reading
                            channel.register(selector, SelectionKey.OP_READ, "TCP");

                            // save channel for later use
                            tcpChannel = channel;

                            // buffered communication streams for efficiency
                            in = new DataInputStream(new BufferedInputStream(
                                     tcpChannel.socket().getInputStream(), 65536));

                            tcpBuffer.clear();
                            tcpBuffer.limit(8);
                            readingSize = true;

                            // TCP connection established from RC client,
                            // RC server connect() can now return.
System.out.println("rcListeningThread: established TCP connection from client");
                            startLatch.countDown();
                        }

                        // read input from rc client, tcp or udp
                        else if (key.isReadable()) {

                            channelType = (String) key.attachment();

                            // if channel is TCP ...
                            if (channelType.equals("TCP")) {
                                SocketChannel readChannel = (SocketChannel)key.channel();

                                // FIRST, read size & msgId
                                if (readingSize) {
                                    try {
                                        bytes = readChannel.read(tcpBuffer);
                                    }
                                    catch (IOException e) {
                                        // client has died
                                        key.cancel();
                                        it.remove();
                                        continue;
                                    }

                                    // if End-of-stream (client died) ...
                                    if (bytes == -1) {
                                        key.cancel();
                                        it.remove();
                                        continue;
                                    }

                                    // if we've read 8 bytes (2 ints) ...
                                    if (tcpBuffer.position() > 7) {
                                        tcpBuffer.flip();
                                        size  = tcpBuffer.getInt();
                                        msgId = tcpBuffer.getInt();
                                        if (size > 1500) {
System.out.println("rcListeningThread: " + server.getName() + " tcp size = " + size + ", msgId = " + msgId);
                                        }

                                        if (size-4 > tcpBuffer.capacity()) {
//System.out.println("  create new, large direct bytebuffer from " + clientData.buffer.capacity() + " to " + clientData.size);
                                            tcpBuffer = ByteBuffer.allocateDirect(size-4);
                                        }

                                        tcpBuffer.clear();
                                        tcpBuffer.limit(size-4);
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
                                        bytes = readChannel.read(tcpBuffer);
                                    }
                                    catch (IOException ex) {
                                        // client has died
                                        key.cancel();
                                        it.remove();
                                        continue;
                                    }

                                    // if End-of-stream (client died) ...
                                    if (bytes == -1) {
                                        key.cancel();
                                        it.remove();
                                        continue;
                                    }

                                    bytesRead += bytes;
//System.out.println("  bytes read = " + bytesRead);
                                    // if we've read everything ...
                                    if (bytesRead >= size-4) {
                                        tcpBuffer.flip();
                                        dataBuffer = tcpBuffer;
                                        okToParseMsg = true;
                                    }
                                }

                                tcpChannel = readChannel;
                            }

                            // else if channel is UDP ...
                            else {
//System.out.println("  client is UDP readable");
                                udpBuffer.clear();
                                DatagramChannel readChannel = (DatagramChannel)key.channel();

                                // receive packet
                                try {
                                    senderAddress = readChannel.receive(udpBuffer);
                                    if (senderAddress == null) {
                                        // This should not happen as select() says
                                        // there is something to read on this channel.
System.out.println("rcListeningThread: " + server.getName() + " nothing to read in udp channel");
                                        it.remove();
                                        continue;
                                    }
                                }
                                catch (IOException e) {
System.out.println("rcListeningThread: " + server.getName() + " IO error reading udp packet");
                                    it.remove();
                                    continue;
                                }

                                udpBuffer.flip();
                                // Enough data for next 5 ints? (3 magic ints, size, id)
                                if (udpBuffer.limit() < 4*5) {
System.out.println("rcListeningThread: " + server.getName() + " udp packet is too small, " + udpBuffer.limit());
                                    it.remove();
                                    continue;
                                }

                                int m1 = udpBuffer.getInt();
                                int m2 = udpBuffer.getInt();
                                int m3 = udpBuffer.getInt();

                                if (m1 != cMsgNetworkConstants.magicNumbers[0] ||
                                    m2 != cMsgNetworkConstants.magicNumbers[1] ||
                                    m3 != cMsgNetworkConstants.magicNumbers[2]) {
System.out.println("rcListeningThread: " + server.getName() + " received bogus udp packet (bad magic ints)");
                                    it.remove();
                                    continue;
                                }

                                // Find size & msgId of data to come.
                                size  = udpBuffer.getInt();
                                msgId = udpBuffer.getInt();

                                // Enough data in buffer for msg?
                                if (4*4 + size > udpBuffer.limit()) {
System.out.println("rcListeningThread: " + server.getName() + " not enough data in packet (" + udpBuffer.limit() +
                   ") to read complete msg (" + (16 + size) + "), ignore it");
                                    it.remove();
                                    continue;
                                }

                                dataBuffer = udpBuffer;
                                okToParseMsg = true;

                                if (prescalePrintOut++ % 300 == 0) {
System.out.println("rcListeningThread: " + server.getName() + " received udp msg #" + prescalePrintOut);
                                    lastPrintTime = System.currentTimeMillis();
                                }

                                udpChannel = readChannel;
                            }

                            if (okToParseMsg) {

                                switch (msgId) {

                                    case cMsgConstants.msgSubscribeResponse: // receiving a message
                                        // read the message here
                                        msg = readIncomingMessageNB(dataBuffer);

                                        // run callbacks for this message
                                        runCallbacks(msg);
                                        break;

                                    case cMsgConstants.msgGetResponse: // receiving a message for sendAndGet
                                        // read the message
                                        msg = readIncomingMessageNB(dataBuffer);
                                        msg.setGetResponse(true);

                                        // wakeup caller with this message
                                        wakeGets(msg);
                                        break;

                                    default:
System.out.println("rcListeningThread: " + server.getName() + " bad client msg cmd, " + msgId);
                                        break;
                                }

                                bytesRead = 0;
                                readingSize = true;
                                okToParseMsg = false;
                                tcpBuffer.clear();
                                tcpBuffer.limit(8);
                            }
                        }
                    }

                    // remove key from selected set since it's been handled
                    it.remove();
                }
            }
        }
        catch (IOException ex) {
//            if (debug >= cMsgConstants.debugError) {
                System.out.println("rcListenThread: I/O ERROR in rc server");
                System.out.println("rcListenThread: close TCP server socket, port = " +
                                           tcpChannel.socket().getLocalPort());
                ex.printStackTrace();
//            }
        }
        finally {
            try {if (in != null) in.close();}               catch (IOException ex) {}
            try {if (tcpChannel != null)  tcpChannel.close();} catch (IOException ex) {}
            try {if (udpChannel != null) udpChannel.close();} catch (IOException ex) {}
            try {serverChannel.close();} catch (IOException ex) {}
            try {selector.close();}      catch (IOException ex) {}
        }

//        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("rcListeningThread: " + server.getName() + " quit TCP/UDP listening thread");
//        }

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
        boolean delivered = false;

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
                    delivered = true;
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
                            delivered = true;
                        }
                    }
                }
            }
        }
        if (!delivered) {
            System.out.println("runCallbacks: no callbacks to deliver msg to");
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
System.out.println("wakeGets: originating sendAndGet not in table, discard response");
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

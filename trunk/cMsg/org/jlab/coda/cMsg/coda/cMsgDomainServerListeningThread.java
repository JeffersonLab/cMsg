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

package org.jlab.coda.cMsg.coda;

import java.io.*;
import java.net.*;
import java.lang.*;
import java.nio.channels.*;
import java.nio.ByteBuffer;
import java.util.Iterator;

import org.jlab.coda.cMsg.*;

/**
 * This class implements a thread which listens for connections to a cMsg
 * name server for a particular cMsg domain. Once a connection is made,
 * this class handles all the communication with the client.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgDomainServerListeningThread extends Thread {

    /** cMsgDomainServer object which spawned this thread. */
    cMsgDomainServer server;

    /**
     * Object containing information about the domain client.
     * Certain members of info can only be filled in by this thread,
     * such as the listening port & host.
     */
    cMsgClientInfo info;

    /** Server channel (contains socket). */
    ServerSocketChannel serverChannel;

    /** Port number to listen on. */
    private int port;

    /** Host this is running on. */
    private String host;

    /** A direct buffer is necessary for nio socket IO. */
    ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

    /** Level of debug output for this class. */
    private int debug;

    /**
     * Get listening port of this object.
     */
    public int getPort() {
        return port;
    }

    /**
     * Get local host running this object.
     */
    public String getHost() {
        return host;
    }

    /**
     * Creates a new cMsgDomainServer object.
     *
     * @param server       domain server object which started this thread
     * @param info         object containing information about the doman server's client
     * @param startingPort suggested port on which to starting listening for connections
     * @param debug        level of debug output for this object
     * @throws cMsgException If a port to listen on could not be found
     */
    cMsgDomainServerListeningThread(cMsgDomainServer server,
                                    cMsgClientInfo info,
                                    int startingPort, int debug) throws cMsgException {
        // Port number to listen on
        port = startingPort;
        this.info = info;
        this.server = server;
        this.debug = debug;

        // At this point, find a port to bind to. If that isn't possible, throw
        // an exception. We want to do this in the constructor, because it's much
        // harder to do it in a separate thread and then report back the results.
        try {
            serverChannel = ServerSocketChannel.open();
        }
        catch (IOException ex) {
            ex.printStackTrace();
            throw new cMsgException("Exiting Server: cannot open a listening socket");
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
                    throw new cMsgException("Exiting Server: cannot find port to listen on");
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
    }

    /** This method is executed as a thread. */
    public void run() {
        System.out.println("Running Domain Server");

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
                    if (server.getKillAllThreads()) {
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
                        registerChannel(selector, channel, SelectionKey.OP_READ);

                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("\ncMsgDomainServer: registered client\n");
                        }
                    }

                    // is there data to read on this channel?
                    if (key.isValid() && key.isReadable()) {
                        SocketChannel channel = (SocketChannel) key.channel();
                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("\n\ncMsgDomainServer: client request");
                        }
                        handleClient(channel);
                    }

                    // remove key from selected set since it's been handled
                    it.remove();
                }
            }
        }
        catch (IOException ex) {
        }
        System.out.println("Quitting Domain Server");
        return;
    }


    private void registerChannel(Selector selector, SocketChannel channel, int ops) {
        if (channel == null) {
            return;
        }

        try {
            // set socket options, first make socket nonblocking
            channel.configureBlocking(false);

            // get socket
            Socket socket = channel.socket();
            // Set tcpNoDelay so no packets are delayed
            socket.setTcpNoDelay(true);
            // set buffer sizes
            socket.setReceiveBufferSize(65535);
            socket.setSendBufferSize(65535);

            channel.register(selector, ops);
        }
        catch (IOException e) {
            e.printStackTrace();
        }
    }


    /** Read a minimum of number of bytes from the channel. */
    private int readSocketBytes(SocketChannel channel, int bytes) throws IOException {

        int n, tries = 0, count = 0;

        buffer.clear();
        buffer.limit(bytes);

        // Keep reading until we have exactly "bytes" number of bytes,
        // or have tried "tries" number of times to read.

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("readSocketBytes: will read " + bytes + " bytes");
        }
        while (count < bytes) {
            if ((n = channel.read(buffer)) < 0) {
                throw new IOException("readSocketBytes: client's socket is dead");
            }
            if (tries > 5000) {
                throw new IOException("readSocketBytes: too many tries to read " + n + " bytes");
            }
            tries++;
            count += n;
            if (debug >= cMsgConstants.debugInfo && tries%1000 == 0) {
                System.out.println("readSocketBytes: called read " + tries + " times, read " + n + " bytes");
            }
            try {Thread.sleep(1);} catch (InterruptedException e) {}
        }
        return count;
    }



    /**
     * This method handles all communication between a cMsg user who has
     * connected to a domain and this server for that domain.
     */
    private void handleClient(SocketChannel channel) {

        cMsgHandleRequestCoda requestHandler = new cMsgHandleRequestCoda();

        String subject = "";
        String type = "";

        try {
            // keep reading until we have an int (4 bytes) of data
            if (readSocketBytes(channel, 4) < 0) {
                return;
            }

            // make buffer readable
            buffer.flip();

            // read client's request
            int msgId = buffer.getInt();
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("handleClient: got request = " + msgId);
            }

            switch (msgId) {

                case cMsgConstants.msgSendRequest: // receiving a message
                    // read the message here
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("handleClient: got send request");
                    }
                    cMsgMessage msg = readIncomingMessage(channel);
                    requestHandler.handleSendRequest(msg);
                    //buffer.clear();
                    //channel.close();
                    break;

                case cMsgConstants.msgSubscribeRequest: // subscribing to subject & type
                    // read the subject and type here
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("handleClient: got subscribe request");
                    }
                    readSubscribeInfo(channel);
                    requestHandler.handleSubscribeRequest(subject, type);
                    break;

                case cMsgConstants.msgUnsubscribeRequest: // unsubscribing to a subject & type
                    // read the subject and type here
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("handleClient: got unsubscribe request");
                    }
                    readUnsubscribeInfo(channel);
                    requestHandler.handleUnsubscribeRequest(subject, type);
                    break;

                case cMsgConstants.msgKeepAlive: // see if this end is still here
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("handleClient: got keep alive");
                    }
                    requestHandler.handleKeepAlive();
                    // send ok back as acknowledgment
                    buffer.clear();
                    buffer.putInt(cMsgConstants.ok).flip();
                    while (buffer.hasRemaining()) {
                        channel.write(buffer);
                    }
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("handleClient: sent keep alive response");
                    }
                    break;

                case cMsgConstants.msgDisconnectRequest: // client disconnecting
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("handleClient: got disconnect");
                    }
                    requestHandler.handleDisconnect();
                    // send ok back as acknowledgment
                    buffer.clear();
                    buffer.putInt(cMsgConstants.ok).flip();
                    while (buffer.hasRemaining()) {
                        channel.write(buffer);
                    }
                    // close channel and unregister from selector
                    channel.close();
                    break;

                case cMsgConstants.msgShutdown: // told this server to shutdown
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("handleClient: got shutdown");
                    }
                    requestHandler.handleShutdown();
                    // send ok back as acknowledgment
                    buffer.clear();
                    buffer.putInt(cMsgConstants.ok).flip();
                    while (buffer.hasRemaining()) {
                        channel.write(buffer);
                    }
                    // close channel and unregister from selector
                    channel.close();
                    break;

                default:
                    if (debug >= cMsgConstants.debugWarn) {
                        System.out.println("handleClient: can't understand your message!");
                    }
                    break;
            }

            return;
        }
        catch (IOException e) {
            //e.printStackTrace();
            if (debug >= cMsgConstants.debugError) {
                System.out.println("Tcp Server: I/O error in cMsg client");
            }
            try {channel.close();}
            catch (IOException e1) {
                e1.printStackTrace();
            }
        }
    }



    private cMsgMessage readIncomingMessage(SocketChannel channel) throws IOException {

        // create a message
        cMsgMessage msg = new cMsgMessage();

        // keep reading until we have 3 ints of data
        readSocketBytes(channel, 12);

        // go back to reading-from-buffer mode
        buffer.flip();

        // read 3 ints
        int[] inComing = new int[3];
        buffer.asIntBuffer().get(inComing);

        // length of message subject
        int lengthSubject = inComing[0];

        // length of message type
        int lengthType = inComing[1];

        // length of message text
        int lengthText = inComing[2];

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("readIncomingMessages:" + " lenSubject = " + lengthSubject +
                               ", lenType = " + lengthType + ", lenText = " + lengthText);
        }

        // bytes expected
        int bytesToRead = lengthSubject + lengthType + lengthText;

        // read in all remaining bytes
        readSocketBytes(channel, bytesToRead);

        // go back to reading-from-buffer mode
        buffer.flip();

        // allocate byte array
        int lengthBuf = lengthSubject > lengthType ? lengthSubject : lengthType;
        lengthBuf = lengthBuf > lengthText ? lengthBuf : lengthText;
        byte[] buf = new byte[lengthBuf];

        // read subject
        buffer.get(buf, 0, lengthSubject);
        msg.setSubject(new String(buf, 0, lengthSubject, "US-ASCII"));
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("  subject = " + msg.getSubject());
        }

        // read type
        buffer.get(buf, 0, lengthType);
        msg.setType(new String(buf, 0, lengthType, "US-ASCII"));
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("  type = " + msg.getType());
        }

        // read text
        buffer.get(buf, 0, lengthText);
        msg.setText(new String(buf, 0, lengthText, "US-ASCII"));
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("  text = " + msg.getText());
        }

        // send ok back as acknowledgment
        buffer.clear();
        buffer.putInt(cMsgConstants.ok).flip();
        while (buffer.hasRemaining()) {
            channel.write(buffer);
        }

        return msg;
    }


    private void readSubscribeInfo(SocketChannel channel) throws IOException {
        // keep reading until we have 3 ints of data
        readSocketBytes(channel, 12);

        // go back to reading-from-buffer mode
        buffer.flip();

        // read 3 ints
        int[] inComing = new int[3];
        buffer.asIntBuffer().get(inComing);

        // id of subject/type combination
        int uniqueId = inComing[0];

        // length of subject
        int lengthSubject = inComing[1];

        // length of type
        int lengthType = inComing[2];

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("readSubscribeInfo:" + " uniqueId = " + uniqueId +
                               ", lenSubject = " + lengthSubject +
                               ", lenType = " + lengthType);
        }

        // bytes expected
        int bytesToRead = lengthSubject + lengthType;

        // read in all remaining bytes
        readSocketBytes(channel, bytesToRead);

        // go back to reading-from-buffer mode
        buffer.flip();

        // allocate byte array
        int lengthBuf = lengthSubject > lengthType ? lengthSubject : lengthType;
        byte[] buf = new byte[lengthBuf];

        // read subject
        buffer.get(buf, 0, lengthSubject);
        String subject = new String(buf, 0, lengthSubject, "US-ASCII");
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("  subject = " + subject);
        }

        // read type
        buffer.get(buf, 0, lengthType);
        String type = new String(buf, 0, lengthType, "US-ASCII");
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("  type = " + type);
        }

        // send ok back as acknowledgment
        buffer.clear();
        buffer.putInt(cMsgConstants.ok).flip();
        while (buffer.hasRemaining()) {
            channel.write(buffer);
        }

        return;
    }


    private void readUnsubscribeInfo(SocketChannel channel) throws IOException {
        // keep reading until we have 3 ints of data
        readSocketBytes(channel, 12);

        // go back to reading-from-buffer mode
        buffer.flip();

        // read 2 ints
        int[] inComing = new int[2];
        buffer.asIntBuffer().get(inComing);

        // length of subject
        int lengthSubject = inComing[0];

        // length of type
        int lengthType = inComing[1];

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("readUnsubscribeInfo:" + " lenSubject = " + lengthSubject +
                               ", lenType = " + lengthType);
        }

        // bytes expected
        int bytesToRead = lengthSubject + lengthType;

        // read in all remaining bytes
        readSocketBytes(channel, bytesToRead);

        // go back to reading-from-buffer mode
        buffer.flip();

        // allocate byte array
        int lengthBuf = lengthSubject > lengthType ? lengthSubject : lengthType;
        byte[] buf = new byte[lengthBuf];

        // read subject
        buffer.get(buf, 0, lengthSubject);
        String subject = new String(buf, 0, lengthSubject, "US-ASCII");
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("  subject = " + subject);
        }

        // read type
        buffer.get(buf, 0, lengthType);
        String type = new String(buf, 0, lengthType, "US-ASCII");
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("  type = " + type);
        }

        // send ok back as acknowledgment
        buffer.clear();
        buffer.putInt(cMsgConstants.ok).flip();
        while (buffer.hasRemaining()) {
            channel.write(buffer);
        }

        return;
    }
  
  
}

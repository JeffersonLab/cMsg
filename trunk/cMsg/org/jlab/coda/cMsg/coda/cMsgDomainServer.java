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
import java.util.*;
import java.nio.channels.Selector;
import java.nio.channels.SelectionKey;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;

import org.jlab.coda.cMsg.*;

/**
 * This class implements a cMsg domain server in the CODA cMsg domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgDomainServer extends Thread {

    /** Type of domain this is. */
    private String domainType = "CODA";

    /** Port number listening on. */
    private int port;

    /** Host this is running on. */
    private String host;

    // The following members are holders for information that comes from the client
    // so that information can be passed on to the object which handles all the client
    // requests.

    /** Identifier that uniquely determines the subject/type pair for a client subscription. */
    private int receiverSubscribeId;
    /** Message subject. */
    private String subject;
    /** Message type. */
    private String type;


    /** Keep reference to cMsg name server which created this object. */
    cMsgHandleRequests clientHandler;

    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugInfo;

    /**
     * Object containing information about the domain client.
     * Certain members of info can only be filled in by this thread,
     * such as the listening port & host.
     */
    cMsgClientInfo info;

    /** Server channel (contains socket). */
    ServerSocketChannel serverChannel;

    /** A direct buffer is necessary for nio socket IO. */
    ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

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

        // Start thread to monitor client's health.
        // If he dies, kill this thread.
        cMsgMonitorClient monitor =  new cMsgMonitorClient(info, this);
        monitor.start();
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
                                               info.clientName + "\n");
                        }
                    }

                    // is there data to read on this channel?
                    if (key.isValid() && key.isReadable()) {
                        SocketChannel channel = (SocketChannel) key.channel();
                        handleClient(channel);
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



    /**
     * This method handles all communication between a cMsg user who has
     * connected to a domain and this server for that domain.
     *
     * @param channel nio socket communication channel
     */
    private void handleClient(SocketChannel channel) {

        try {
            // keep reading until we have an int (4 bytes) of data
            if (cMsgUtilities.readSocketBytes(buffer, channel, 4, debug) < 0) {
                return;
            }

            // make buffer readable
            buffer.flip();

            // read client's request
            int msgId = buffer.getInt();

            switch (msgId) {

                case cMsgConstants.msgSendRequest: // receiving a message
                    // read the message here
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("dServer handleClient: got send request from " + info.clientName);
                    }
                    cMsgMessage msg = readIncomingMessage(channel);
                    clientHandler.handleSendRequest(info.clientName, msg);
                    break;

                case cMsgConstants.msgSubscribeRequest: // subscribing to subject & type
                    // read the subject and type here
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("dServer handleClient: got subscribe request from " + info.clientName);
                    }
                    readSubscribeInfo(channel);
                    clientHandler.handleSubscribeRequest(info.clientName, subject, type,
                                                         receiverSubscribeId);
                    break;

                case cMsgConstants.msgUnsubscribeRequest: // unsubscribing to a subject & type
                    // read the subject and type here
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("dServer handleClient: got unsubscribe request from " + info.clientName);
                    }
                    readUnsubscribeInfo(channel);
                    clientHandler.handleUnsubscribeRequest(info.clientName, subject, type);
                    break;

                case cMsgConstants.msgKeepAlive: // see if this end is still here
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("dServer handleClient: got keep alive from " + info.clientName);
                    }
                    // send ok back as acknowledgment
                    buffer.clear();
                    buffer.putInt(cMsgConstants.ok).flip();
                    while (buffer.hasRemaining()) {
                        channel.write(buffer);
                    }
                    clientHandler.handleKeepAlive(info.clientName);
                    break;

                case cMsgConstants.msgDisconnectRequest: // client disconnecting
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("dServer handleClient: got disconnect from " + info.clientName);
                    }
                    // send ok back as acknowledgment
                    buffer.clear();
                    buffer.putInt(cMsgConstants.ok).flip();
                    while (buffer.hasRemaining()) {
                        channel.write(buffer);
                    }
                    // close channel and unregister from selector
                    channel.close();
                    clientHandler.handleDisconnect(info.clientName);
                    break;

                case cMsgConstants.msgShutdown: // told this server to shutdown
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("handleClient: got shutdown from " + info.clientName);
                    }
                    // send ok back as acknowledgment
                    buffer.clear();
                    buffer.putInt(cMsgConstants.ok).flip();
                    while (buffer.hasRemaining()) {
                        channel.write(buffer);
                    }
                    // close channel and unregister from selector
                    channel.close();
                    clientHandler.handleShutdown(info.clientName);
                    // need to shutdown this server now
                    killAllThreads();
                    break;

                default:
                    if (debug >= cMsgConstants.debugWarn) {
                        System.out.println("dServer handleClient: can't understand your message " + info.clientName);
                    }
                    break;
            }

            return;
        }
        catch (IOException e) {
            //e.printStackTrace();
            if (debug >= cMsgConstants.debugError) {
                System.out.println("dServer handleClient: I/O ERROR in cMsg client");
            }
            try {channel.close();}
            catch (IOException e1) {
                e1.printStackTrace();
            }
        }
        catch (cMsgException e) {
            e.printStackTrace();
            if (debug >= cMsgConstants.debugError) {
                System.out.println("dServer handleClient: cMsg ERROR in cMsg client");
            }
            try {channel.close();}
            catch (IOException e1) {
                e1.printStackTrace();
            }
        }
    }



    /**
     * This method reads an incoming cMsgMessage from a client.
     *
     * @param channel nio socket communication channel
     * @throws IOException If socket read or write error
     * @return message read from channel
     */
    private cMsgMessage readIncomingMessage(SocketChannel channel) throws IOException {

        // create a message
        cMsgMessage msg = new cMsgMessage();

        // keep reading until we have 8 ints of data
        cMsgUtilities.readSocketBytes(buffer, channel, 32, debug);

        // go back to reading-from-buffer mode
        buffer.flip();

        // read 8 ints
        int[] inComing = new int[8];
        buffer.asIntBuffer().get(inComing);

        // system message id
        msg.setSysMsgId(inComing[0]);
        // sender id
        msg.setSenderId(inComing[1]);
        // time message sent in seconds since midnight GMT, Jan 1, 1970
        msg.setSenderTime(new Date(((long)inComing[2])*1000));
        // sender message id
        msg.setSenderMsgId(inComing[3]);
        // sender token
        msg.setSenderToken(inComing[4]);
        // length of message subject
        int lengthSubject = inComing[5];
        // length of message type
        int lengthType = inComing[6];
        // length of message text
        int lengthText = inComing[7];

//        if (debug >= cMsgConstants.debugInfo) {
//            System.out.println("readIncomingMessages:" + " lenSubject = " + lengthSubject +
//                               ", lenType = " + lengthType + ", lenText = " + lengthText);
//        }

        // bytes expected
        int bytesToRead = lengthSubject + lengthType + lengthText;

        // read in all remaining bytes
        cMsgUtilities.readSocketBytes(buffer, channel, bytesToRead, debug);

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

        // fill in message object's members
        msg.setDomain(domainType);
        msg.setReceiver("CODA domain server");
        msg.setReceiverHost(host);
        msg.setReceiverTime(new Date()); // current time
        msg.setSender(info.clientName);
        msg.setSenderHost(info.clientHost);

        return msg;
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
        int[] inComing = new int[3];
        buffer.asIntBuffer().get(inComing);

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

        // allocate byte array
        int lengthBuf = lengthSubject > lengthType ? lengthSubject : lengthType;
        byte[] buf = new byte[lengthBuf];

        // read subject
        buffer.get(buf, 0, lengthSubject);
        subject = new String(buf, 0, lengthSubject, "US-ASCII");
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("  subject = " + subject);
        }

        // read type
        buffer.get(buf, 0, lengthType);
        type = new String(buf, 0, lengthType, "US-ASCII");
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


    /**
     * This method reads an incoming unsubscribe request from a client.
     *
     * @param channel nio socket communication channel
     * @throws IOException If socket read or write error
     */
    private void readUnsubscribeInfo(SocketChannel channel) throws IOException {
        // keep reading until we have 3 ints of data
        cMsgUtilities.readSocketBytes(buffer, channel, 8, debug);

        // go back to reading-from-buffer mode
        buffer.flip();

        // read 2 ints
        int[] inComing = new int[2];
        buffer.asIntBuffer().get(inComing);
        // length of subject
        int lengthSubject = inComing[0];
        // length of type
        int lengthType = inComing[1];

        // bytes expected
        int bytesToRead = lengthSubject + lengthType;

        // read in all remaining bytes
        cMsgUtilities.readSocketBytes(buffer, channel, bytesToRead, debug);

        // go back to reading-from-buffer mode
        buffer.flip();

        // allocate byte array
        int lengthBuf = lengthSubject > lengthType ? lengthSubject : lengthType;
        byte[] buf = new byte[lengthBuf];

        // read subject
        buffer.get(buf, 0, lengthSubject);
        subject = new String(buf, 0, lengthSubject, "US-ASCII");
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("  subject = " + subject);
        }

        // read type
        buffer.get(buf, 0, lengthType);
        type = new String(buf, 0, lengthType, "US-ASCII");
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

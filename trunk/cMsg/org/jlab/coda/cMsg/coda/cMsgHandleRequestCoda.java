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

import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgHandleRequests;
import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgConstants;

import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.net.Socket;
import java.net.InetSocketAddress;
import java.nio.channels.Channel;
import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;
import java.io.IOException;
import java.io.UnsupportedEncodingException;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Jul 14, 2004
 * Time: 2:34:00 PM
 * To change this template use File | Settings | File Templates.
 */
public class cMsgHandleRequestCoda implements cMsgHandleRequests {
    /** A direct buffer is necessary for nio socket IO. */
    ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

    HashMap clients = new HashMap(100);

    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugInfo;


    /** Method to handle message sent by domain client. */
    public void registerClient(String name, String host, int port) throws cMsgException {
        System.out.println("registering client");
        // Check to see if name is taken already
        if (clients.containsKey(name)) {
            throw new cMsgException("client already exists");
        }

        cMsgClientInfo info = new cMsgClientInfo(name, port, host);
        clients.put(name, info);
    }

    /** Method to handle message sent by domain client. */
    public void unregisterClient(String name) {
        System.out.println("unregistering client");
        clients.remove(name);
    }

    /** Method to handle message sent by domain client. */
    public void handleSendRequest(String name, cMsgMessage msg) throws cMsgException {
        System.out.println("handling send request");
        // Scan through all clients
        Iterator iter = clients.keySet().iterator();
        String client;
        cMsgClientInfo info;
        while (iter.hasNext()) {
            client = (String) iter.next();
            // Don't deliver a message to the sender
            if (client.equals(name)) {
                continue;
            }
            info = (cMsgClientInfo) clients.get(client);

            // Look at all subscriptions
            Iterator it = info.subscriptions.iterator();
            cMsgSubscription sub;
            SocketChannel channel = null;
            while (it.hasNext()) {
                sub = (cMsgSubscription) it.next();
                // if subscription matches the msg ...
                if (sub.subject.equals(msg.getSubject()) && sub.type.equals(msg.getType())) {
                    // Deliver this msg to this client. If there
                    //  is no socket connection, make one.
                    if (info.channel == null) {
                        try {
                            channel = SocketChannel.open(new InetSocketAddress(info.clientHost, info.clientPort));
                            // set socket options
                            Socket socket = channel.socket();
                            // Set tcpNoDelay so no packets are delayed
                            socket.setTcpNoDelay(true);
                            // set buffer sizes
                            socket.setReceiveBufferSize(65535);
                            socket.setSendBufferSize(65535);
                        }
                        catch (IOException e) {
                            throw new cMsgException(e.getMessage());
                        }
                        info.channel = channel;
                    }
                    deliverMessage(channel, msg);
                }
            }
        }
    }

    /** Method to handle subscribe request sent by domain client. */
    public void handleSubscribeRequest(String name, String subject, String type,
                                       int receiverSubscribeId) throws cMsgException {
        System.out.println("handling subscribe request");
        // Each client (name) has a cMsgClientInfo object associated with it
        // that contains all relevant information. Retrieve that object
        // from the "clients" table, add subscription to it.
        cMsgClientInfo info = (cMsgClientInfo) clients.get(name);
        if (info == null) {
            throw new cMsgException("no client information stored for " + name);
        }

        // Do not add duplicate subscription
        Iterator it = info.subscriptions.iterator();
        cMsgSubscription sub;
        while (it.hasNext()) {
            sub = (cMsgSubscription) it.next();
            if (receiverSubscribeId == sub.id  ||
                    sub.subject.equals(subject) && sub.type.equals(type)) {
                throw new cMsgException("subscription already exists for subject = " + subject +
                                        " and type = " + type);
            }
        }

        // add new subscription
        sub = new cMsgSubscription(subject, type, receiverSubscribeId);
        info.subscriptions.add(sub);
    }

    /** Method to handle unsubscribe request sent by doman client. */
    public void handleUnsubscribeRequest(String name, String subject, String type) {
        System.out.println("handling unsubscribe request");
        // Each client (name) has a cMsgClientInfo object associated with it
        // that contains all relevant information. Retrieve that object
        // from the "clients" table, add update the subscription data.
        cMsgClientInfo info = (cMsgClientInfo) clients.get(name);
        if (info == null) {
            //throw new cMsgException("no client information stored for " + name);
            return;
        }
        Iterator it = info.subscriptions.iterator();
        cMsgSubscription sub;
        while (it.hasNext()) {
            sub = (cMsgSubscription) it.next();
            if (sub.subject.equals(subject) && sub.type.equals(type)) {
                it.remove();
                return;
            }
        }
    }

    /** Method to handle keepalive sent by domain client
     *  checking to see if the socket is still up. */
    public void handleKeepAlive(String name) {
        System.out.println("handling keep alive");
    }

    /** Method to handle a disconnect request sent by domain client. */
    public void handleDisconnect(String name) {
        System.out.println("handling shutdown");
    }

    /** Method to handle a shutdown request sent by domain client. */
    public void handleShutdown(String name) {
        System.out.println("handling shutdown");
    }

    /**
     * Method to send a message received from one client to another client
     * subscribed to its subject and type.
     */
    private void deliverMessage(SocketChannel channel, cMsgMessage msg) throws cMsgException {
        // get ready to write
        buffer.clear();

        // write 9 ints
        buffer.putInt(msg.getSysMsgId());
        buffer.putInt(msg.getReceiverSubscribeId());
        buffer.putInt(msg.getSenderId());
        buffer.putInt(msg.getSenderMsgId());
        buffer.putInt(msg.getSender().length());
        buffer.putInt(msg.getSenderHost().length());
        buffer.putInt(msg.getSubject().length());
        buffer.putInt(msg.getType().length());
        buffer.putInt(msg.getText().length());

        // write strings
        try {
            buffer.put(msg.getSender().getBytes("US_ASCII"));
            buffer.put(msg.getSenderHost().getBytes("US_ASCII"));
            buffer.put(msg.getSubject().getBytes("US_ASCII"));
            buffer.put(msg.getType().getBytes("US_ASCII"));
            buffer.put(msg.getText().getBytes("US_ASCII"));
        }
        catch (UnsupportedEncodingException e) {}

        try {
            // send buffer over the socket
            buffer.flip();
            while (buffer.hasRemaining()) {
                channel.write(buffer);
            }
            // read acknowledgment & keep reading until we have 1 int of data
            cMsgUtilities.readSocketBytes(buffer, channel, 4, debug);
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }

        // go back to reading-from-buffer mode
        buffer.flip();

        int error = buffer.getInt();

        if (error != cMsgConstants.ok) {
            throw new cMsgException("error in sending message");
        }

        return;
    }

}

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

package org.jlab.coda.cMsg.plugins;

import org.jlab.coda.cMsg.cMsgHandleRequests;
import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgConstants;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsg.cMsgClientInfo;
import org.jlab.coda.cMsg.cMsg.cMsgSubscription;

import java.util.*;
import java.net.Socket;
import java.net.InetSocketAddress;
import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.io.File;

/**
 * Class to handles all client cMsg requests.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsg implements cMsgHandleRequests {
    /** Hash table to store clients. Name is key and cMsgClientInfo is value. */
    private static HashMap clients = new HashMap(100);

    /** List of subscriptions matching the msg. */
    private ArrayList subList  = new ArrayList(100);

    /** List of client info objects corresponding to entries in "subList" subscriptions. */
    private ArrayList infoList = new ArrayList(100);

    /** A direct buffer is necessary for nio socket IO. */
    private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugError;

    /** Remainder of UDL client used to connect to domain server. */
    private String UDLRemainder;

    /** Name of client using this subdomain handler. */
    private String name;


    /**
     * Implement a simple wildcard matching scheme where "*" means any or no characters and
     * "?" means 1 or no character.
     *
     * @param regexp string that is a regular expression (can contain wildcards)
     * @param s string to be matched
     * @return true if there is a match, false if there is not
     */
    static private boolean matches(String regexp, String s) {
        // It's a match if regexp (subscription string) is null
        if (regexp == null) return true;

        // If the message's string is null, something's wrong
        if (s == null) return false;

        // The first order of business is to take the regexp arg and modify it so that it is
        // a regular expression that Java can understand. This means takings all occurrences
        // of "*" and "?" and adding a period in front.
        String rexp = regexp.replaceAll("\\*", ".*");
        rexp = rexp.replaceAll("\\?", ".?");

        // Now see if there's a match with the string arg
        if (s.matches(rexp)) return true;
        return false;
    }

    /**
      * Method to give the subdomain handler the appropriate part
      * of the UDL the client used to talk to the domain server.
      *
      * @param UDLRemainder last part of the UDL appropriate to the subdomain handler
      * @throws cMsgException
      */
    public void setUDLRemainder(String UDLRemainder) throws cMsgException {
        this.UDLRemainder = UDLRemainder;    
    }

    /**
     * Method to see if domain client is registered.
     * @param name name of client
     * @return true if client registered, false otherwise
     */
    public boolean isRegistered(String name) {
        if (clients.containsKey(name)) return true;
        return false;
    }

    /**
     * Method to register domain client.
     *
     * @param name name of client
     * @param host host client is running on
     * @param port port client is listening on
     * @throws cMsgException if client already exists
     */
    public void registerClient(String name, String host, int port) throws cMsgException {
        // Check to see if name is taken already
        if (clients.containsKey(name)) {
            cMsgException e = new cMsgException("client already exists");
            e.setReturnCode(cMsgConstants.errorNameExists);
            throw e;
        }

        cMsgClientInfo info = new cMsgClientInfo(name, port, host);
        synchronized (clients) {
            clients.put(name, info);
        }

        this.name = name;
    }


    /**
     * Method to handle message sent by domain client. The message's subject and type
     * are matched against all client subscriptions. The message is sent to all clients
     * with matching subscriptions.  This method is run after all exchanges between
     * domain server and client.
     *
     * @param msg message from sender
     * @throws cMsgException if a channel to the client is closed, cannot be created,
     *                          or socket properties cannot be set
     */
    public void handleSendRequest(cMsgMessage msg) throws cMsgException {
        String client;
        cMsgSubscription sub;
        cMsgClientInfo   info;
        HashSet subscriptions;

        subList.clear();
        infoList.clear();

        // Scan through all clients.
        // Cannot have clients hashtable changing during this exercise
        synchronized (clients) {
            Iterator iter = clients.keySet().iterator();

            while (iter.hasNext()) {
                client = (String) iter.next();
                // Don't deliver a message to the sender
                if (client.equals(name)) {
                    continue;
                }
                info = (cMsgClientInfo) clients.get(client);
                subscriptions = info.getSubscriptions();

                // Look at all subscriptions
                Iterator it = subscriptions.iterator();
                while (it.hasNext()) {
                    sub = (cMsgSubscription) it.next();
                    // if subscription matches the msg ...
                    // if (sub.subject.equals(msg.getSubject()) && sub.type.equals(msg.getType())) {
                    if (matches(sub.getSubject(), msg.getSubject()) &&
                              matches(sub.getType(), msg.getType()))  {
                        // store sub and info for later use (in non-synchronized code)
                        subList.add(sub);
                        infoList.add(info);
                    }
                }
            }
        }

        // Once we have the subscription, msg, and client info, no more need for sychronization
        SocketChannel channel = null;

        for (int i = 0; i < subList.size(); i++) {

            info = (cMsgClientInfo)  infoList.get(i);
            sub  = (cMsgSubscription) subList.get(i);

            // Deliver this msg to this client. If there is no socket connection, make one.
            if (info.getChannel() == null) {
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("handleSendRequest: make a socket connection to " + info.getName());
                }
                try {
                    channel = SocketChannel.open(new InetSocketAddress(info.getClientHost(),
                                                                       info.getClientPort()));
                    // set socket options
                    Socket socket = channel.socket();
                    // Set tcpNoDelay so no packets are delayed
                    socket.setTcpNoDelay(true);
                    // set buffer sizes
                    socket.setReceiveBufferSize(65535);
                    socket.setSendBufferSize(65535);
                }
                catch (IOException e) {
                    if (debug >= cMsgConstants.debugError) {
                        //e.printStackTrace();
                    }
                    continue;
                    //throw new cMsgException(e.getMessage());
                }
                info.setChannel(channel);
            }

            try {
                deliverMessage(info.getChannel(), sub.getId(), msg);
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    //e.printStackTrace();
                }
                continue;
            }
        }
    }


    /**
     * Method to handle subscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     *
     * @param subject message subject to subscribe to
     * @param type message type to subscribe to
     * @param receiverSubscribeId message id refering to these specific subject and type values
     * @throws cMsgException if no client information is available or a subscription for this
     *                          subject and type already exists
     */
    public void handleSubscribeRequest(String subject, String type,
                                       int receiverSubscribeId) throws cMsgException {
        // Each client (name) has a cMsgClientInfo object associated with it
        // that contains all relevant information. Retrieve that object
        // from the "clients" table, add subscription to it.
        cMsgClientInfo info = (cMsgClientInfo) clients.get(name);
        if (info == null) {
            throw new cMsgException("handleSubscribeRequest: no client information stored for " + name);
        }

        // do not add duplicate subscription
        HashSet subscriptions = info.getSubscriptions();
        Iterator it = subscriptions.iterator();
        cMsgSubscription sub;
        while (it.hasNext()) {
            sub = (cMsgSubscription) it.next();
            if (receiverSubscribeId == sub.getId()  ||
                    sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                throw new cMsgException("handleSubscribeRequest: subscription already exists for subject = " +
                                        subject + " and type = " + type);
            }
        }

        // add new subscription
        sub = new cMsgSubscription(subject, type, receiverSubscribeId);
        subscriptions.add(sub);
    }


    /**
     * Method to handle sunsubscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     */
     public void handleUnsubscribeRequest(String subject, String type) {
        cMsgClientInfo info = (cMsgClientInfo) clients.get(name);
        if (info == null) {
            return;
        }
        HashSet subscriptions = info.getSubscriptions();
        Iterator it = subscriptions.iterator();
        cMsgSubscription sub;
        while (it.hasNext()) {
            sub = (cMsgSubscription) it.next();
            if (sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                it.remove();
                return;
            }
        }
    }


    /**
     * Method to handle keepalive sent by domain client checking to see
     * if the domain server socket is still up. Normally nothing needs to
     * be done as the domain server simply returns an "OK" to all keepalives.
     * This method is run after all exchanges between domain server and client.
     */
    public void handleKeepAlive() {
    }


    /**
     * Method to get a single message from the server for a given
     * subject and type.
     *
     * @param subject subject of message to get
     * @param type type of message to get
     * @return cMsgMessage message obtained by this get
     */
    public cMsgMessage handleGetRequest(String subject, String type) {
        return null;
    }


    /**
     * Method to handle a client or domain server shutdown.
     * This method is run after all exchanges between domain server and client but
     * before the domain server thread is killed (since that is what is running this
     * method).
     */
    public void handleClientShutdown() {
        synchronized (clients) {
            clients.remove(name);
        }
    }


    /**
     * Method to handle a complete name server down.
     * This method is run after all exchanges between domain server and client but
     * before the server is killed (since that is what is running this
     * method).
     */
    public void handleServerShutdown() {
    }


    /**
     * Method to deliver a message to a client that is
     * subscribed to the message's subject and type.
     *
     * @param channel communication channel to client
     * @param id message id refering to message's subject and type
     * @param msg message to be sent
     * @throws IOException if the message cannot be sent over the channel
     *                          or client returns an error
     */
    private void deliverMessage(SocketChannel channel, int id, cMsgMessage msg) throws IOException {
        // get ready to write
        buffer.clear();

        // write 12 ints
        int outGoing[] = new int[12];
        outGoing[0]  = cMsgConstants.msgSubscribeResponse;
        outGoing[1]  = msg.getSysMsgId();
        outGoing[2]  = id;
        outGoing[3]  = msg.getSenderId();
        // send the current time in seconds since Jan 1, 1970 as senderTime
        outGoing[4]  = (int) (((new Date()).getTime())/1000L);
        outGoing[5]  = msg.getSenderMsgId();
        outGoing[6]  = msg.getSenderToken();
        outGoing[7]  = msg.getSender().length();
        outGoing[8]  = msg.getSenderHost().length();
        outGoing[9]  = msg.getSubject().length();
        outGoing[10] = msg.getType().length();
        outGoing[11] = msg.getText().length();

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("    DELIVERING MESSAGE");
            System.out.println("      msg: " +                 outGoing[0]);
            System.out.println("      SysMsgId: " +            outGoing[1]);
            System.out.println("      ReceiverSubscribeId: " + outGoing[2]);
            System.out.println("      SenderId: " +            outGoing[3]);
            System.out.println("      Time: " +                outGoing[4]);
            System.out.println("      SenderMsgId: " +         outGoing[5]);
            System.out.println("      SenderToken: " +         outGoing[6]);
            System.out.println("      Sender length: " +       outGoing[7]);
            System.out.println("      SenderHost length: " +   outGoing[8]);
            System.out.println("      Subject length: " +      outGoing[9]);
            System.out.println("      Type length: " +         outGoing[10]);
            System.out.println("      Text length: " +         outGoing[11]);

            System.out.println("      Sender: " +       msg.getSender());
            System.out.println("      SenderHost: " +   msg.getSenderHost());
            System.out.println("      Subject: " +      msg.getSubject());
            System.out.println("      Type: " +         msg.getType());
            System.out.println("      Text: " +         msg.getText());
        }

        // send ints over together using view buffer
        buffer.asIntBuffer().put(outGoing);

        // position original buffer at position of view buffer
        buffer.position(48);

        // write strings
        try {
            //buffer.put("blah blah".getBytes("US-ASCII"));
            buffer.put(msg.getSender().getBytes("US-ASCII"));
            buffer.put(msg.getSenderHost().getBytes("US-ASCII"));
            buffer.put(msg.getSubject().getBytes("US-ASCII"));
            buffer.put(msg.getType().getBytes("US-ASCII"));
            buffer.put(msg.getText().getBytes("US-ASCII"));
        }
        catch (UnsupportedEncodingException e) {
            e.printStackTrace();
        }

        // send buffer over the socket
        buffer.flip();
        while (buffer.hasRemaining()) {
            channel.write(buffer);
        }

        // read acknowledgment & keep reading until we have 1 int of data
        //cMsgUtilities.readSocketBytes(buffer, channel, 4, debug);
        /*
        // go back to reading-from-buffer mode
        buffer.flip();

        int error = buffer.getInt();

        if (error != cMsgConstants.ok) {
            throw new cMsgException("deliverMessage: error in sending message");
        }
        */
        return;
    }

}

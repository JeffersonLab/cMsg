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

/**
 * Class to handles all client cMsg requests.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsg implements cMsgHandleRequests {
    /** Hash table to store clients. Name is key and cMsgClientInfo is value. */
    private static Map clients = Collections.synchronizedMap(new HashMap(100));

    /** Hash table to store specific get in progress. sysMsgId of get msg is key,
     * and client name is value. */
    private static Map specificGets = Collections.synchronizedMap(new HashMap(100));

    /** Used to create a unique id number associated with a specific message. */
    private static int sysMsgId = 0;

    /** List of subscriptions & gets matching the msg. */
    private ArrayList subGetList  = new ArrayList(100);

    /** List of client info objects corresponding to entries in "subGetList" subscriptions. */
    private ArrayList infoList = new ArrayList(100);

    /** A direct buffer is necessary for nio socket IO. */
    private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugWarn;

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
     * Method to tell if the "send" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSendRequest}
     * method.
     *
     * @return true if send implemented in {@link #handleSendRequest}
     */
    public boolean hasSend() {return true;};


    /**
     * Method to tell if the "syncSsend" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSyncSendRequest}
     * method.
     *
     * @return true if send implemented in {@link #handleSyncSendRequest}
     */
    public boolean hasSyncSend() {return false;};


    /**
     * Method to tell if the "get" cMsg API function is implemented
     * by this interface implementation in the {@link #handleGetRequest}
     * method.
     *
     * @return true if get implemented in {@link #handleGetRequest}
     */
    public boolean hasGet() {return true;};


    /**
     * Method to tell if the "subscribe" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSubscribeRequest}
     * method.
     *
     * @return true if subscribe implemented in {@link #handleSubscribeRequest}
     */
    public boolean hasSubscribe() {return true;};


    /**
     * Method to tell if the "unsubscribe" cMsg API function is implemented
     * by this interface implementation in the {@link #handleUnsubscribeRequest}
     * method.
     *
     * @return true if unsubscribe implemented in {@link #handleUnsubscribeRequest}
     */
    public boolean hasUnsubscribe() {return true;};

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
        synchronized (clients) {
            // Check to see if name is taken already
            if (clients.containsKey(name)) {
                cMsgException e = new cMsgException("client already exists");
                e.setReturnCode(cMsgConstants.errorNameExists);
                throw e;
            }

            cMsgClientInfo info = new cMsgClientInfo(name, port, host);
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
     * @param message message from sender
     * @throws cMsgException if a channel to the client is closed, cannot be created,
     *                          or socket properties cannot be set
     */
    public void handleSendRequest(cMsgMessage message) throws cMsgException {
        String client;
        cMsgSubscription sub;
        cMsgClientInfo   info;
        HashSet subscriptions, gets;

        subGetList.clear();
        infoList.clear();
        // If message is sent in response to a get ...
        if (message.isGetResponse()) {
            int id = message.getSysMsgId();
            // Recall the client who originally sent the get request
            // and remove the item from the hashtable
            info = (cMsgClientInfo) specificGets.remove(new Integer(id));

            // Add to list of clients getting messages.
            // In this case there will only be 1 on that list.
            if (info != null) {
                infoList.add(info);
                // subscription is not used, but here for ease of programming
                subGetList.add(new cMsgSubscription(null, null, 0));
//System.out.println("Handler sending msg for SPECIFIC GET");
            }
            // If someone else responded to the get first, tough luck!
            else return;
        }

        else {
            // Scan through all clients.
            // Cannot have clients hashtable changing during this exercise.
            synchronized (clients) {
                Iterator iter = clients.keySet().iterator();

                while (iter.hasNext()) {
                    client = (String) iter.next();
                    // Don't deliver a message to the sender
                    //if (client.equals(name)) {
                    //    continue;
                    //}
                    info = (cMsgClientInfo) clients.get(client);
                    gets = info.getGets();
                    subscriptions = info.getSubscriptions();
                    Iterator it;

                    // Look at all subscriptions
                    synchronized (subscriptions) {
                        it = subscriptions.iterator();
                        while (it.hasNext()) {
                            sub = (cMsgSubscription) it.next();
                            // if subscription matches the msg ...
                            if (matches(sub.getSubject(), message.getSubject()) &&
                                    matches(sub.getType(), message.getType())) {
                                // store sub and info for later use (in non-synchronized code)
                                subGetList.add(sub);
                                infoList.add(info);
                                // no more subscriptions of this sub/type for this client, go to next
//System.out.println("Handler sending msg for SUBSCRIBE");
                                break;
                            }
                        }
                    }

                    // Look at all gets
                    synchronized (gets) {
                        it = gets.iterator();
                        while (it.hasNext()) {
                            sub = (cMsgSubscription) it.next();
                            // if get matches the msg ...
                            if (matches(sub.getSubject(), message.getSubject()) &&
                                    matches(sub.getType(), message.getType())) {
                                // store get and info for later use (in non-synchronized code)
                                subGetList.add(sub);
                                infoList.add(info);
                                // no more gets of this sub/type for this client,
                                // so delete the get and go to next client
                                it.remove();
//System.out.println("Handler sending msg for GENERAL GET");
                                break;
                            }
                        }
                    }
                }
            }
        }

        // Once we have the subscription/get, msg, and client info,
        // no more need for sychronization
        SocketChannel channel = null;

        for (int i = 0; i < subGetList.size(); i++) {

            info = (cMsgClientInfo)     infoList.get(i);
            sub  = (cMsgSubscription) subGetList.get(i);

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
                deliverMessage(info.getChannel(), sub.getId(), message);
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
     * Method to handle message sent by domain client in synchronous mode.
     * It requires a synchronous integer response from this object but is
     * not implemented in the cMsg (this) subdomain. It's here only in order
     * to implement the required interface.
     *
     * @param message message from sender
     * @return response from this object
     */
    public int handleSyncSendRequest(cMsgMessage message) {
        return 0;
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

        synchronized (subscriptions) {
            Iterator it = subscriptions.iterator();
            cMsgSubscription sub;
            while (it.hasNext()) {
                sub = (cMsgSubscription) it.next();
                if (receiverSubscribeId == sub.getId() ||
                        sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                    throw new cMsgException("handleSubscribeRequest: subscription already exists for subject = " +
                                            subject + " and type = " + type);
                }
            }

            // add new subscription
            sub = new cMsgSubscription(subject, type, receiverSubscribeId);
            subscriptions.add(sub);
        }

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

        synchronized (subscriptions) {
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
    }


    /**
      * Method to synchronously get a single message from the server for a given
      * subject and type -- perhaps from a specified receiver.
      *
      * @param message message requesting what sort of message to get
      * @throws cMsgException if no client information is available or a subscription for this
      *                          subject and type already exists
      */
     public void handleGetRequest(cMsgMessage message) throws cMsgException {
        // Each client (name) has a cMsgClientInfo object associated with it
        // that contains all relevant information. Retrieve that object
        // from the "clients" table, add get (actually a subscription) to it.
        cMsgClientInfo info = (cMsgClientInfo) clients.get(name);
        if (info == null) {
            throw new cMsgException("handleGetRequest: no client information stored for " + name);
        }

        // If sender is not doing a specific get, do a get "subscribe".
        if (!message.isGetRequest()) {
            // do not add duplicate get
            HashSet gets = info.getGets();

            int receiverSubscribeId = message.getReceiverSubscribeId();
            String subject = message.getSubject();
            String type    = message.getType();

            synchronized (gets) {
                Iterator it = gets.iterator();
                cMsgSubscription sub;
                while (it.hasNext()) {
                    sub = (cMsgSubscription) it.next();
                    if (receiverSubscribeId == sub.getId() ||
                            sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                        throw new cMsgException("handleGetRequest: get already exists for subject = " +
                                                subject + " and type = " + type);
                    }
                }

                // add new get
                sub = new cMsgSubscription(subject, type, receiverSubscribeId);
                gets.add(sub);
            }
        }
        // Else specially register this get so its response can be identified.
        // Use sysMsgId which we must generate now.
        else {
            synchronized (specificGets) {
                int id = sysMsgId++;
                message.setSysMsgId(id);
                specificGets.put(new Integer(id), info);
            }

            // now send this message on its way to any receivers out there
            handleSendRequest(message);
        }

     }


    /**
     * Method to handle unget request sent by domain client (hidden from user).
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     */
    public void handleUngetRequest(String subject, String type) {
        cMsgClientInfo info = (cMsgClientInfo) clients.get(name);
        if (info == null) {
            return;
        }

        HashSet gets = info.getGets();

        synchronized (gets) {
            Iterator it = gets.iterator();
            cMsgSubscription sub;
            while (it.hasNext()) {
                sub = (cMsgSubscription) it.next();
                if (sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                    it.remove();
                    return;
                }
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
     * Method to handle a client or domain server shutdown.
     * This method is run after all exchanges between domain server and client but
     * before the domain server thread is killed (since that is what is running this
     * method).
     */
    public void handleClientShutdown() {
        if (debug >= cMsgConstants.debugWarn) {
            System.out.println("dHandler: SHUTDOWN client " + name);
        }
        clients.remove(name);
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

        // write 14 ints
        int outGoing[] = new int[14];
        outGoing[0]  = cMsgConstants.msgSubscribeResponse;
        outGoing[1]  = msg.getSysMsgId();
        outGoing[2]  = msg.isGetRequest()  ? 1 : 0;
        outGoing[3]  = msg.isGetResponse() ? 1 : 0;
        outGoing[4]  = id;  // receiverSubscribeId, 0 for specific gets
        outGoing[5]  = msg.getSenderId();
        outGoing[6]  = (int) (msg.getSenderTime().getTime()/1000L);
        outGoing[7]  = msg.getSenderMsgId();
        outGoing[8]  = msg.getSenderToken();
        outGoing[9]  = msg.getSender().length();
        outGoing[10] = msg.getSenderHost().length();
        outGoing[11] = msg.getSubject().length();
        outGoing[12] = msg.getType().length();
        outGoing[13] = msg.getText().length();

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("    DELIVERING MESSAGE");
            System.out.println("      msg: " +                 outGoing[0]);
            System.out.println("      SysMsgId: " +            outGoing[1]);
            System.out.println("      isGetRequest: " +        outGoing[2]);
            System.out.println("      isGetResponse: " +       outGoing[3]);
            System.out.println("      ReceiverSubscribeId: " + outGoing[4]);
            System.out.println("      SenderId: " +            outGoing[5]);
            System.out.println("      Time: " +                outGoing[6]);
            System.out.println("      SenderMsgId: " +         outGoing[7]);
            System.out.println("      SenderToken: " +         outGoing[8]);
            System.out.println("      Sender length: " +       outGoing[9]);
            System.out.println("      SenderHost length: " +   outGoing[10]);
            System.out.println("      Subject length: " +      outGoing[11]);
            System.out.println("      Type length: " +         outGoing[12]);
            System.out.println("      Text length: " +         outGoing[13]);

            System.out.println("      Sender: " +       msg.getSender());
            System.out.println("      SenderHost: " +   msg.getSenderHost());
            System.out.println("      Subject: " +      msg.getSubject());
            System.out.println("      Type: " +         msg.getType());
            System.out.println("      Text: " +         msg.getText());
        }

        // send ints over together using view buffer
        buffer.asIntBuffer().put(outGoing);

        // position original buffer at position of view buffer
        buffer.position(56);

        // write strings
        try {
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

        return;
    }

}

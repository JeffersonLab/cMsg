/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 14-Jul-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsg;

import org.jlab.coda.cMsg.*;

import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.net.*;
import java.nio.ByteBuffer;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * This class implements a cMsg client in the cMsg domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsg extends cMsgImpl {

    /** Port number to listen on. */
    private int port;

    /** This cMsg client's host. */
    private String host;

    /** Port number from which to start looking for a suitable listening port. */
    private int startingPort;

    /** Server channel (contains socket). */
    private ServerSocketChannel serverChannel;

    /** A direct buffer is necessary for nio socket IO. */
    private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

    /** Name server's host. */
    private String nameServerHost;

    /** Name server's port. */
    private int nameServerPort;

    /** Domain server's host. */
    private String domainServerHost;

    /** Domain server's port. */
    private int domainServerPort;

    /** Channel for talking to domain server. */
    private SocketChannel domainChannel;

    /** Channel for checking to see that the domain server is still alive. */
    private SocketChannel keepAliveChannel;

    /** Thread listening for TCP connections and responding to domain server commands. */
    private cMsgClientListeningThread listeningThread;

    /** Thread for sending keep alive commands to domain server to check its health. */
    private KeepAlive keepAliveThread;

    /**
     * Collection of all of this client's message subscriptions which are
     * {@link cMsgSubscription} objects.
     */
    Set subscriptions;

    /**
     * Collection of all of this client's {@link #get} calls, NOT directed to a specific
     * reciever, currently in execution. If the message sent in a get call has its
     * {@link cMsgMessage#isGetRequest} method set to true, then the message is destined
     * for a specific receiver and is NOT included in this collection.
     * This set contains {@link cMsgSubscription} objects since general gets are very
     * similar to subscriptions and can be thought of as a one-shot subscription.
     */
    Set generalGets;

    /**
     * Collection of all of this client's {@link #get} calls, directed to a specific
     * reciever, currently in execution. If the message sent in a get call has its
     * {@link cMsgMessage#isGetRequest} method set to true, then the message is destined
     * for a specific receiver and is included in the collection.
     *
     * Key is senderToken object, value is {@link cMsgMessageHolder} object.
     */
    Map specificGets;

    /** Used to create unique id numbers associated with a specific message subject/type pair. */
    private int uniqueId;

    /** The subdomain server object or client handler implements {@link #send}. */
    private boolean hasSend;

    /** The subdomain server object or client handler implements {@link #syncSend}. */
    private boolean hasSyncSend;

    /** The subdomain server object or client handler implements {@link #get}. */
    private boolean hasGet;

    /** The subdomain server object or client handler implements {@link #subscribe}. */
    private boolean hasSubscribe;

    /** The subdomain server object or client handler implements {@link #unsubscribe}. */
    private boolean hasUnsubscribe;

    /** Level of debug output for this class. */
    int debug = cMsgConstants.debugError;


//-----------------------------------------------------------------------------

    /**
     * Constructor which automatically tries to connect to the name server specified.
     *
     * @param UDL Uniform Domain Locator which specifies the server to connect to
     * @param name name of this client which must be unique in this domain
     * @param description description of this client
     * @throws cMsgException if domain in not implemented or there are problems communicating
     *                       with the name/domain server.
     */
    public cMsg(String UDL, String name, String description) throws cMsgException {
        this.UDL = UDL;
        this.name = name;
        this.description = description;
        subscriptions = Collections.synchronizedSet(new HashSet(20));
        generalGets   = Collections.synchronizedSet(new HashSet(20));
        specificGets  = Collections.synchronizedMap(new HashMap(20));

        // store our host's name
        try {
            host = InetAddress.getLocalHost().getHostName();
        }
        catch (UnknownHostException e) {
            throw new cMsgException("cMsg: cannot find host name");
        }

        // parse the UDL - Uniform Domain Locator
        parseUDL(UDL);

        if (!domain.equalsIgnoreCase("cMsg")) {
            throw new cMsgException("cMsg: cMsg " + domain + " domain is not implemented yet");
        }

        connect();
    }


//-----------------------------------------------------------------------------


    /**
     * Method to connect to the domain server.
     *
     * @throws cMsgException if there are communication problems with the server
     */
    synchronized public void connect() throws cMsgException {

        if (connected) return;

        Jgetenv env = null;

        // read env variable for starting port number
        try {
            env = new Jgetenv();
            startingPort = Integer.parseInt(env.echo("CMSG_CLIENT_PORT"));
        }
        catch (NumberFormatException ex) {
        }
        catch (JgetenvException ex) {
        }

        // port #'s < 1024 are reserved
        if (startingPort < 1024) {
            startingPort = cMsgNetworkConstants.clientServerStartingPort;
        }

        // At this point, find a port to bind to. If that isn't possible, throw
        // an exception.
        try {
            serverChannel = ServerSocketChannel.open();
        }
        catch (IOException ex) {
            ex.printStackTrace();
            throw new cMsgException("connect: cannot open a listening socket");
        }

        port = startingPort;
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
                    throw new cMsgException("connect: cannot find port to listen on");
                }
            }
        }

        // launch pend thread and start listening on receive socket
        listeningThread = new cMsgClientListeningThread(this, serverChannel);
        listeningThread.start();

        // Wait for indication thread is actually running before
        // continuing on. This thread must be running before we talk to
        // the name server since the server tries to communicate with
        // the listening thread.
        synchronized (listeningThread) {
            if (!listeningThread.isAlive()) {
                try {
                    listeningThread.wait();
                }
                catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }

        // connect & talk to cMsg name server to check if name is unique
        SocketChannel channel = null;
        try {
            channel = SocketChannel.open(new InetSocketAddress(nameServerHost, nameServerPort));
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
                e.printStackTrace();
            }
            throw new cMsgException("connect: cannot create channel to name server");
        }

        // get host & port to send messages to
        try {
            getHostAndPortFromNameServer(channel);
        }
        catch (IOException e) {
            if (debug >= cMsgConstants.debugError) {
                e.printStackTrace();
            }
            throw new cMsgException("connect: cannot talk to name server");
        }

        // done talking to server
        try {
            channel.close();
        }
        catch (IOException e) {
            if (debug >= cMsgConstants.debugError) {
                System.out.println("connect: cannot close channel to name server, continue on");
                e.printStackTrace();
            }
        }

        // create sending (to domain) channel
        try {
            domainChannel = SocketChannel.open(new InetSocketAddress(domainServerHost, domainServerPort));
            Socket socket = channel.socket();
            socket.setTcpNoDelay(true);
            socket.setReceiveBufferSize(65535);
            socket.setSendBufferSize(65535);
        }
        catch (IOException e) {
            if (debug >= cMsgConstants.debugError) {
                e.printStackTrace();
            }
            throw new cMsgException("connect: cannot create channel to domain server");
        }

        // create keepAlive socket
        try {
            keepAliveChannel = SocketChannel.open(new InetSocketAddress(domainServerHost, domainServerPort));
            Socket socket = channel.socket();
            socket.setTcpNoDelay(true);
            socket.setReceiveBufferSize(65535);
            socket.setSendBufferSize(65535);
        }
        catch (IOException e) {
            if (debug >= cMsgConstants.debugError) {
                e.printStackTrace();
            }
            throw new cMsgException("connect: cannot create keepAlive channel to domain server");
        }

        // create thread to send periodic keep alives and handle dead server
        keepAliveThread = new KeepAlive(this, keepAliveChannel);
        keepAliveThread.start();

        connected = true;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to close the connection to the domain server. This method results in this object
     * becoming functionally useless.
     */
    synchronized public void disconnect() {

        if (!connected) return;
        connected = false;

        // stop listening and client communication thread & close channel
        listeningThread.killThread();

        // stop keep alive thread & close channel
        keepAliveThread.killThread();

        // stop all callback threads
        Iterator iter = subscriptions.iterator();
        for (; iter.hasNext();) {
            cMsgSubscription sub = (cMsgSubscription) iter.next();

            // run through all callbacks
            Iterator iter2 = sub.getCallbacks().iterator();
            for (; iter2.hasNext();) {
                cMsgCallbackThread cbThread = (cMsgCallbackThread) iter2.next();
                // Tell the callback thread to wakeup and die
                cbThread.dieNow();
            }
        }

        // wakeup all gets
        iter = specificGets.values().iterator();
        for (; iter.hasNext();) {
            cMsgMessageHolder holder = (cMsgMessageHolder) iter.next();
            holder.message = null;
            synchronized (holder) {
                holder.notify();
            }
        }

    }


//-----------------------------------------------------------------------------


    /**
     * Method to send a message to the domain server for further distribution.
     *
     * @param message message to send
     * @throws cMsgException if there are communication problems with the server
     */
    synchronized public void send(cMsgMessage message) throws cMsgException {

        if (!connected) {
            throw new cMsgException("not connected to server");
        }

        if (!hasSend) {
            throw new cMsgException("send is not implemented by this subdomain");
        }

        String subject = message.getSubject();
        String type    = message.getType();
        String text    = message.getText();

        // check args first
        if (subject == null || type == null)  {
            throw new cMsgException("message subject or type is null");
        }
        else if (subject.length() < 1 || type.length() < 1) {
            throw new cMsgException("message subject or type is blank string");
        }

        // watch out for null text
        if (text == null) {
            message.setText("");
            text = message.getText();
        }

        int outGoing[] = new int[11];
        // message id to domain server
        outGoing[0] = cMsgConstants.msgSendRequest;
        // system message
        outGoing[1] = message.getSysMsgId();
        // is get request
        outGoing[2] = message.isGetRequest() ? 1 : 0;
        // is get response
        outGoing[3] = message.isGetResponse() ? 1 : 0;
        // sender id
        outGoing[4] = message.getSenderId();
        // time message sent (right now) in seconds
        outGoing[5] = (int) ((new Date()).getTime()/1000L);
        // sender message id
        outGoing[6] = message.getSenderMsgId();
        // sender token
        outGoing[7] = message.getSenderToken();

        // length of "subject" string
        outGoing[8]  = subject.length();
        // length of "type" string
        outGoing[9]  = type.length();
        // length of "text" string
        outGoing[10] = text.length();

        // get ready to write
        buffer.clear();
        // send ints over together using view buffer
        buffer.asIntBuffer().put(outGoing);
        // position original buffer at position of view buffer
        buffer.position(44);

        // write strings
        try {
            buffer.put(subject.getBytes("US-ASCII"));
            buffer.put(type.getBytes("US-ASCII"));
            buffer.put(text.getBytes("US-ASCII"));
        }
        catch (UnsupportedEncodingException e) {
            e.printStackTrace();
        }

        try {
            // send buffer over the socket
            buffer.flip();
            while (buffer.hasRemaining()) {
                domainChannel.write(buffer);
            }
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to send a message to the domain server for further distribution
     * and wait for a response from the subdomain handler that got it.
     *
     * @param message message
     * @return response from subdomain handler
     * @throws cMsgException
     */
    synchronized public int syncSend(cMsgMessage message) throws cMsgException {

        if (!connected) {
            throw new cMsgException("not connected to server");
        }

        if (!hasSyncSend) {
            throw new cMsgException("send is not implemented by this subdomain");
        }

        String subject = message.getSubject();
        String type    = message.getType();
        String text    = message.getText();

        // check args first
        if (subject == null || type == null)  {
            throw new cMsgException("message subject or type is null");
        }
        else if (subject.length() < 1 || type.length() < 1) {
            throw new cMsgException("message subject or type is blank string");
        }

        // watch out for null text
        if (text == null) {
            message.setText("");
            text = message.getText();
        }

        int outGoing[] = new int[11];
        // message id to domain server
        outGoing[0] = cMsgConstants.msgSyncSendRequest;
        // system message
        outGoing[1] = message.getSysMsgId();
        // is get request
        outGoing[2] = message.isGetRequest() ? 1 : 0;
        // is get response
        outGoing[3] = message.isGetResponse() ? 1 : 0;
        // sender id
        outGoing[4] = message.getSenderId();
        // time message sent (right now) in seconds
        outGoing[5] = (int) ((new Date()).getTime()/1000L);
        // sender message id
        outGoing[6] = message.getSenderMsgId();
        // sender token
        outGoing[7] = message.getSenderToken();

        // length of "subject" string
        outGoing[8]  = subject.length();
        // length of "type" string
        outGoing[9]  = type.length();
        // length of "text" string
        outGoing[10] = text.length();

        // get ready to write
        buffer.clear();
        // send ints over together using view buffer
        buffer.asIntBuffer().put(outGoing);
        // position original buffer at position of view buffer
        buffer.position(44);

        // write strings
        try {
            buffer.put(subject.getBytes("US-ASCII"));
            buffer.put(type.getBytes("US-ASCII"));
            buffer.put(text.getBytes("US-ASCII"));
        }
        catch (UnsupportedEncodingException e) {
            e.printStackTrace();
        }

        try {
            // send buffer over the socket
            buffer.flip();
            while (buffer.hasRemaining()) {
                domainChannel.write(buffer);
            }
            // read acknowledgment - 1 int of data
            cMsgUtilities.readSocketBytes(buffer, domainChannel, 4, debug);
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }

        // go back to reading-from-buffer mode
        buffer.flip();

        int response = buffer.getInt();
        return response;

    }


//-----------------------------------------------------------------------------


    /**
     * Method to force cMsg client to send pending communications with domain server.
     * In the cMsg domain implementation, this method does nothing.
     */
    synchronized public void flush() {
        return;
    }


//-----------------------------------------------------------------------------


    /**
     * This method does two separate things depending on the specifics of message in the
     * argument. If the message to be sent has its "getRequest" field set to be true using
     * {@link cMsgMessage#isGetRequest()}, then the message is sent as it would be in the
     * {@link #send} method. The server notes the fact that a response to it is expected,
     * and sends it to everyone subscribed to its subject and type. When a marked response is
     * received from a client, it sends that first response back to the original sender
     * regardless of its subject or type.
     *
     * In a second usage, if the message did NOT set its "getRequest" field to be true,
     * then the server grabs the first incoming message of the requested subject and type
     * and sends that to the original sender in response to the get.
     *
     * @param message message sent to server
     * @param timeout time in milliseconds to wait for a reponse message
     * @return response message
     * @throws cMsgException if there are communication problems with the server
     */
    public cMsgMessage get(cMsgMessage message, int timeout) throws cMsgException {
        if (!connected) {
            throw new cMsgException("not connected to server");
        }

        if (!hasGet) {
            throw new cMsgException("get is not implemented by this subdomain");
        }

        String subject = message.getSubject();
        String type    = message.getType();
        String text    = message.getText();

        // check args first
        if (subject == null || type == null)  {
            throw new cMsgException("message subject or type is null");
        }
        else if (subject.length() < 1 || type.length() < 1) {
            throw new cMsgException("message subject or type is blank string");
        }

        // watch out for null text
        if (text == null) {
            message.setText("");
            text = message.getText();
        }

        boolean getExists = false;
        cMsgSubscription sub = null;
        cMsgMessageHolder holder = null;
        Integer specificGetId = null;

        synchronized (this) {
            // Allow several gets to be grouped under one subscription,
            // so that one message from the server can be sent for all.
            //
            // This is only possible if the get is NOT expecting a return
            // message from a specific receiver (getRequest field is not
            // true in message), and is thus just a 1-shot subscribe.

            if (!message.isGetRequest()) {
                // for each get ...
                for (Iterator iter = generalGets.iterator(); iter.hasNext();) {
                    sub = (cMsgSubscription) iter.next();
                    // if get for subject & type exists ...
                    if (sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                        // add to existing set of callbacks
                        holder = new cMsgMessageHolder();
                        sub.addHolder(holder);
//System.out.println("get: add callback to existing get & return");
                        getExists = true;
                        break;
                    }
                }
            }
            if (!getExists) {
                // If we're here, the subscription to that subject & type does not exist yet.
                // Or, we're doing a get and expecting a response from specific receiver.
                // We need send msg to domain server who will see we get a response.
                //
                // First generate a unique id for the receiveSubscribeId and senderToken field.
                //
                // For the 1-shot subscribe:
                // The receiverSubscribeId is sent back by the domain server in the future when
                // messages of this subject and type are sent to this cMsg client. This helps
                // eliminate the need to parse subject and type each time a message arrives.
                //
                // For a specific get:
                // If we're expecting a specific response, then the senderToken is sent back
                // in the response message, allowing us to run the correct callback.

                uniqueId++;

                // for get, create cMsgMessageHolder object (not callback thread object)
                holder = new cMsgMessageHolder();

                // track specific get requests
                if (message.isGetRequest()) {
                    specificGetId = new Integer(uniqueId);
                    specificGets.put(specificGetId, holder);
                }
                // track general gets
                else {
                    sub = new cMsgSubscription(subject, type, uniqueId, holder);
                    generalGets.add(sub);
                }

                int outGoing[] = new int[10];
                // message id to domain server
                outGoing[0] = cMsgConstants.msgGetRequest;
                // getRequest flag
                outGoing[1] = message.isGetRequest() ? 1 : 0;
                // send unique id for receiverSubscribeId
                outGoing[2] = uniqueId;
                // sender id
                outGoing[3] = message.getSenderId();
                // time message sent (right now)
                outGoing[4] = (int) ((new Date()).getTime()/1000L);
                // sender message id
                outGoing[5] = message.getSenderMsgId();
                // send unique id for sender token
                outGoing[6] = uniqueId;

                // length of "subject" string
                outGoing[7] = subject.length();
                // length of "type" string
                outGoing[8] = type.length();
                // length of "text" string
                outGoing[9] = text.length();

                // get ready to write
                buffer.clear();
                // send ints over together using view buffer
                buffer.asIntBuffer().put(outGoing);
                // position original buffer at position of view buffer
                buffer.position(40);

                // write strings
                try {
                    buffer.put(subject.getBytes("US-ASCII"));
                    buffer.put(type.getBytes("US-ASCII"));
                    buffer.put(text.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {
                    e.printStackTrace();
                }

                try {
                    // send buffer over the socket
                    buffer.flip();
                    while (buffer.hasRemaining()) {
                        domainChannel.write(buffer);
                    }
                }
                catch (IOException e) {
                    throw new cMsgException(e.getMessage());
                }
            }
        }  // end synchronized

        // WAIT for the msg-receiving thread to wake us up
        try {
            synchronized (holder) {
                if (timeout > 0) {
                    holder.wait(timeout);
                }
                else {
                    holder.wait();
                }
            }
        }
        catch (InterruptedException e) {
        }


        // Check the message stored for us in holder.
        // If msg is null, we timed out.
        // Tell server to forget the get if necessary.
        if (holder.message == null) {
            System.out.println("get: timed out");
            synchronized (this) {
                // remove the specific get from books
                if (message.isGetRequest()) {
                    specificGets.remove(specificGetId);
                }
                // if general get, deal with 1-shot "subscription" ...
                else {
                    // remove holder from client's get "subscription"
                    sub.removeHolder(holder);

                    // if there are no gets calls active, remove entire subscription
                    if (sub.numberOfGets() < 1) {
                        // remove subscription
                        generalGets.remove(sub);
                        // tell server to delete get request
                        unget(sub);
                    }
                }
            }
            return null;
        }

        // If msg is not null, server has removed subscription from his records.
        // Client listening thread has also removed subscription from client's
        // records (generalGets HashSet).

        // Make a copy of message and return it.
        cMsgMessage msg = holder.message.copy();
//System.out.println("get: SUCCESS!!!");

        return msg;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to unget a previous get to receive a message of a subject and type
     * from the domain server. Since many simultaneous gets may be made from different
     * threads to the same subject and type* values, but with different callbacks,
     * the callback must be specified so the correct get can be removed.
     * This method is only called when a get timeouts out and the server must be
     * told to forget about the get.
     *
     * @param sub subscription to delete
     * @throws cMsgException if there are communication problems with the server
     */
    synchronized private void unget(cMsgSubscription sub)
            throws cMsgException {

System.out.println("unget: in");
        if (!connected) {
            throw new cMsgException("not connected to server");
        }

        // if there are still callbacks left, don't unget for this sub (subject/type)
        if (sub.numberOfCallbacks() > 0) {
            return;
        }

        // notify the domain server

        int[] outGoing = new int[3];
        // first send message id to server
        outGoing[0] = cMsgConstants.msgUngetRequest;
        // send length of subject
        outGoing[1] = sub.getSubject().length();
        // send length of type
        outGoing[2] = sub.getType().length();

        // get ready to write
        buffer.clear();
        // send ints over together using view buffer
        buffer.asIntBuffer().put(outGoing);
        // position original buffer at position of view buffer
        buffer.position(12);

        // write strings
        try {
            buffer.put(sub.getSubject().getBytes("US-ASCII"));
            buffer.put(sub.getType().getBytes("US-ASCII"));
        }
        catch (UnsupportedEncodingException e) {
        }

        try {
            // send buffer over the socket
            buffer.flip();
            while (buffer.hasRemaining()) {
                domainChannel.write(buffer);
            }
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to subscribe to receive messages of a subject and type from the domain server.
     *
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @param userObj any user-supplied object to be given to the callback method as an argument
     * @throws cMsgException if there are communication problems with the server
     */
    synchronized public void subscribe(String subject, String type, cMsgCallback cb, Object userObj)
            throws cMsgException {

        if (!connected) {
            throw new cMsgException("not connected to server");
        }

        if (!hasSubscribe) {
            throw new cMsgException("subscribe is not implemented by this subdomain");
        }

        // check args first
        if (subject == null || type == null || cb == null) {
            throw new cMsgException("subject, type or callback argument is null");
        }
        else if (subject.length() < 1 || type.length() < 1) {
            throw new cMsgException("subject or type is blank string");
        }

        // add to callback list if subscription to same subject/type exists
        cMsgSubscription sub;
        // for each subscription ...
        for (Iterator iter = subscriptions.iterator(); iter.hasNext();) {
            sub = (cMsgSubscription) iter.next();
            // if subscription to subject & type exists ...
            if (sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                // add to existing set of callbacks
                sub.addCallback(new cMsgCallbackThread(cb, userObj));
                return;
            }
        }

        // If we're here, the subscription to that subject & type does not exist yet.
        // We need to create it and register it with the domain server.

        // First generate a unique id for the receiveSubscribeId field. This info is
        // sent back by the domain server in the future when messages of this subject
        // and type are sent to this cMsg client. This helps eliminate the need to
        // parse subject and type each time a message arrives.
        uniqueId++;

        // add a new subscription & callback
        sub = new cMsgSubscription(subject, type, uniqueId, new cMsgCallbackThread(cb, userObj));
        subscriptions.add(sub);

        int[] outGoing = new int[4];
        // first send message id to server
        outGoing[0] = cMsgConstants.msgSubscribeRequest;
        // send unique id of this subscription
        outGoing[1] = uniqueId;
        // send length of subscription subject
        outGoing[2] = subject.length();
        // send length of subscription type
        outGoing[3] = type.length();

        // get ready to write
        buffer.clear();
        // send ints over together using view buffer
        buffer.asIntBuffer().put(outGoing);
        // position original buffer at position of view buffer
        buffer.position(16);

        // write strings
        try {
            buffer.put(subject.getBytes("US-ASCII"));
            buffer.put(type.getBytes("US-ASCII"));
        }
        catch (UnsupportedEncodingException e) {
        }

        try {
            // send buffer over the socket
            buffer.flip();
            while (buffer.hasRemaining()) {
                domainChannel.write(buffer);
            }
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to unsubscribe a previous subscription to receive messages of a subject and type
     * from the domain server. Since many subscriptions may be made to the same subject and type
     * values, but with different callbacks, the callback must be specified so the correct
     * subscription can be removed.
     *
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @throws cMsgException if there are communication problems with the server
     */
    synchronized public void unsubscribe(String subject, String type, cMsgCallback cb)
            throws cMsgException {

        if (!connected) {
            throw new cMsgException("not connected to server");
        }

        if (!hasUnsubscribe) {
            throw new cMsgException("unsubscribe is not implemented by this subdomain");
        }

        // check args first
        if (subject == null || type == null || cb == null) {
            throw new cMsgException("subject, type or callback argument is null");
        }
        else if (subject.length() < 1 || type.length() < 1) {
            throw new cMsgException("subject or type is blank string");
        }

        // look for and remove any subscription to subject/type with this callback object
        cMsgSubscription sub;

        // client listening thread may be interating thru subscriptions concurrently
        // and we may change set structure
        synchronized (subscriptions) {

            // for each subscription ...
            for (Iterator iter = subscriptions.iterator(); iter.hasNext();) {
                sub = (cMsgSubscription) iter.next();
                // if subscription to subject & type exist ...
                if (sub.getSubject().equals(subject) && sub.getType().equals(type)) {

                    // for each callback listed ...
                    for (Iterator iter2 = sub.getCallbacks().iterator(); iter2.hasNext();) {
                        cMsgCallbackThread cbThread = (cMsgCallbackThread) iter2.next();
                        if (cbThread.callback == cb) {
                            // remove this callback from the set
                            iter2.remove();
                        }
                    }

                    // If there are still callbacks left,
                    // don't unsubscribe for this subject/type
                    if (sub.numberOfCallbacks() > 0) {
                        return;
                    }
                    // else get rid of the whole subscription
                    else {
                        iter.remove();
                    }

                    break;
                }
            }
        }

        // notify the domain server

        int[] outGoing = new int[3];
        // first send message id to server
        outGoing[0] = cMsgConstants.msgUnsubscribeRequest;
        // send length of subject
        outGoing[1] = subject.length();
        // send length of type
        outGoing[2] = type.length();

        // get ready to write
        buffer.clear();
        // send ints over together using view buffer
        buffer.asIntBuffer().put(outGoing);
        // position original buffer at position of view buffer
        buffer.position(12);

        // write strings
        try {
            buffer.put(subject.getBytes("US-ASCII"));
            buffer.put(type.getBytes("US-ASCII"));
        }
        catch (UnsupportedEncodingException e) {
        }

        try {
            // send buffer over the socket
            buffer.flip();
            while (buffer.hasRemaining()) {
                domainChannel.write(buffer);
            }
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
    }


//-----------------------------------------------------------------------------

    /**
     * This method gets the host and port of the domain server from the name server.
     *
     * @param channel nio socket communication channel
     * @throws IOException if there are communication problems with the name server
     */
    private void getHostAndPortFromNameServer(SocketChannel channel) throws IOException, cMsgException {

        int[] outGoing = new int[7];

        // first send message id to server
        outGoing[0] = cMsgConstants.msgConnectRequest;
        // send my listening port (as an int) to server
        outGoing[1] = port;
        // send domain type of server I'm expecting to connect to
        outGoing[2] = domain.length();
        // send subdomain type of server I'm expecting to use
        outGoing[3] = subdomain.length();
        // send UDL remainder for use by subdomain plugin
        outGoing[4] = UDLremainder.length();
        // send length of my host name to server
        outGoing[5] = host.length();
        // send length of my name to server
        outGoing[6] = name.length();

        // get ready to write
        buffer.clear();
        // send ints over together using view buffer
        buffer.asIntBuffer().put(outGoing);
        // position original buffer at position of view buffer
        buffer.position(28);

        try {
            // send the type of domain server I'm expecting to connect to
            buffer.put(domain.getBytes("US-ASCII"));

            // send subdomain type of server I'm expecting to use
            buffer.put(subdomain.getBytes("US-ASCII"));

            // send UDL remainder for use by subdomain plugin
            buffer.put(UDLremainder.getBytes("US-ASCII"));

            // send my host name to server
            buffer.put(host.getBytes("US-ASCII"));

            // send my name to server
            buffer.put(name.getBytes("US-ASCII"));
        }
        catch (UnsupportedEncodingException e) {
        }

        // get ready to write
        buffer.flip();

        // write everything
        while (buffer.hasRemaining()) {
            channel.write(buffer);
        }

        // read acknowledgment & keep reading until we have 1 int of data
        cMsgUtilities.readSocketBytes(buffer, channel, 4, debug);

        // go back to reading-from-buffer mode
        buffer.flip();

        int error = buffer.getInt();

        // if there's an error, quit
        if (error != cMsgConstants.ok) {
            throw new cMsgException(cMsgUtilities.printError(error, cMsgConstants.debugNone));
        }

        // Since everything's OK, we expect to get:
        //   1) attributes of subdomain handler object
        //   2) domain server host & port

        // Read attributes
        cMsgUtilities.readSocketBytes(buffer, channel, 5, debug);
        buffer.flip();

        hasSend        = (buffer.get() == (byte)1) ? true : false;
        hasSyncSend    = (buffer.get() == (byte)1) ? true : false;
        hasGet         = (buffer.get() == (byte)1) ? true : false;
        hasSubscribe   = (buffer.get() == (byte)1) ? true : false;
        hasUnsubscribe = (buffer.get() == (byte)1) ? true : false;

        // Read port & length of host name.
        // first read 2 ints of data
        cMsgUtilities.readSocketBytes(buffer, channel, 8, debug);
        buffer.flip();

        domainServerPort = buffer.getInt();
        int hostLength   = buffer.getInt();

        // read host name
        cMsgUtilities.readSocketBytes(buffer, channel, hostLength, debug);

        // go back to reading-from-buffer mode
        buffer.flip();

        // allocate byte array
        byte[] buf = new byte[hostLength];

        // read subject
        buffer.get(buf, 0, hostLength);
        domainServerHost = new String(buf, 0, hostLength, "US-ASCII");
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("  domain server host = " + domainServerHost +
                               ", port = " + domainServerPort);
        }
    }


    /**
     * Method to parse the Universal Domain Locator into its various components.
     *
     * @param UDL Universal Domain Locator of the form domainType://host:port/remainder
     * @throws cMsgException if UDL is null, or no domainType or host given in UDL
     */
    private void parseUDL(String UDL) throws cMsgException {

        if (UDL == null) {
            throw new cMsgException("invalid UDL");
        }

        // cMsg domain UDL is of the form:
        //       cMsg:<domainType>://<host>:<port>/<subdomainType>/<remainder>
        // 1) initial cMsg: in not necessary
        // 2) port is not necessary
        // 3) host can be "localhost"
        // 4) if domainType is cMsg, subdomainType is automatically set to cMsg if not given.
        //    if subdomainType is not cMsg, it is required
        // 5) remainder is past on to the subdomain plug-in


        Pattern pattern = Pattern.compile("(cMsg)?:?(\\w+)://(\\w+):?(\\d+)?/?(\\w+)?/?(.*)");
        Matcher matcher = pattern.matcher(UDL);

        String s0=null, s1=null, s2=null, s3=null, s4=null, s5=null;

        if (matcher.find()) {
            // cMsg
            s0 = matcher.group(1);
            // domain
            s1 = matcher.group(2);
            // host
            s2 = matcher.group(3);
            // port
            s3 = matcher.group(4);
            // subdomain
            s4 = matcher.group(5);
            // remainder
            s5 = matcher.group(6);
        }
        else {
            throw new cMsgException("invalid UDL");
        }

        if (debug >= cMsgConstants.debugInfo) {
           System.out.println("\nparseUDL: " +
                              "\n  space     = " + s0 +
                              "\n  domain    = " + s1 +
                              "\n  host      = " + s2 +
                              "\n  port      = " + s3 +
                              "\n  subdomain = " + s4 +
                              "\n  remainder = " + s5);
        }

        // must be in cMsg space
        if (s0 != null && !s0.equals("cMsg")) {
            throw new cMsgException("invalid UDL");
        }

        // need at least domain and host
        if (s1 == null || s2 == null) {
            throw new cMsgException("invalid UDL");
        }

        // if subdomain not specified
        if (s4 == null) {
            // if we're in cMsg domain & subdomain not specified, cMsg is subdomain
            if (s1.equals("cMsg")) {
                s4 = "cMsg";
            }
            else {
                throw new cMsgException("invalid UDL");
            }
        }

        domain = s1;
        nameServerHost = s2;
        subdomain = s4;

        // if the host is "localhost", find the actual host name
        if (nameServerHost.equals("localhost")) {
            try {nameServerHost = InetAddress.getLocalHost().getHostName();}
            catch (UnknownHostException e) {}
            if (debug >= cMsgConstants.debugWarn) {
               System.out.println("parseUDL: name server given as \"localhost\", substituting " +
                                  nameServerHost);
            }
        }

        // get name server port or guess if it's not given
        if (s3 != null && s3.length() > 0) {
            try {nameServerPort = Integer.parseInt(s3);}
            catch (NumberFormatException e) {}
        }
        else {
            nameServerPort = cMsgNetworkConstants.nameServerStartingPort;
            if (debug >= cMsgConstants.debugWarn) {
                System.out.println("parseUDL: guessing that the name server port is " + nameServerPort);
            }
        }

        if (nameServerPort < 1024 || nameServerPort > 65535) {
            throw new cMsgException("parseUDL: illegal port number");
        }

        // any remaining UDL is put here
        UDLremainder = s5;
        if (s5 == null) {
            s5 = "";
        }
    }

}


/**
 * Class that periodically checks the health of the domain server.
 * If the server responds to the keepAlive command, everything is OK.
 * If it doesn't, it is assumed dead and a disconnect is done.
 */
class KeepAlive extends Thread {
    /** Socket communication channel with domain server. */
    private SocketChannel channel;

    /** A direct buffer is necessary for nio socket IO. */
    private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

    /** cMsg client object. */
    private cMsg client;

    /** Level of debug output for this class. */
    private int debug;

    /** Setting this to true will kill this thread. */
    private boolean killThread;

    /** Kill this thread. */
    public void killThread() {
        killThread = true;
    }


    /**
     * Constructor.
     *
     * @param client client that is monitoring the health of the domain server
     * @param channel communication channel with domain server
     */
    public KeepAlive(cMsg client, SocketChannel channel) {
        this.client  = client;
        this.channel = channel;
        this.debug   = client.debug;
    }

    /** This method is executed as a thread. */
    public void run() {
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("Running Client Keep Alive Thread");
        }

        try {
            // periodically check to see if the domain server is alive
            while(true) {
                // quit thread
                if (killThread) {
                    return;
                }

                // send keep alive command
                buffer.clear();
                buffer.putInt(cMsgConstants.msgKeepAlive).flip();
                while (buffer.hasRemaining()) {
                    channel.write(buffer);
                }

                // read response -  1 int
                cMsgUtilities.readSocketBytes(buffer, channel, 4, cMsgConstants.debugInfo);

                // go back to reading-from-buffer mode
                buffer.flip();

                // read 1 int
                buffer.getInt();

               // sleep for 3 seconds and try again
               try {Thread.sleep(3000);}
               catch (InterruptedException e) {}
            }
        }
        catch (IOException e) {
        }

        // if we've reach here, there's an error, do a disconnect
        if (debug >= cMsgConstants.debugError) {
            System.out.println("KeepAlive Thread: domain server is probably dead, disconnect\n");
        }

        // close communication channel
        try {channel.close();}
        catch (IOException e) {}

        // disconnect
        client.disconnect();

    }
}
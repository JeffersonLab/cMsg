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

import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.net.*;
import java.util.Date;
import java.util.HashSet;
import java.util.Iterator;
import java.util.regex.Pattern;
import java.util.regex.Matcher;

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
     * Set of all subscriptions (cMsgSubscription objects) to unique subject/type pairs.
     * Each subscription has a set of callbacks associated with it.
     */

    // bug bug, does this need protection??
    HashSet subscriptions;

    /** Used to create unique id numbers associated with a specific message subject/type pair. */
    private int uniqueId;

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
        subscriptions = new HashSet(20);

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

        /*
         * Wait for indication thread is actually running before
         * continuing on. This thread must be running before we talk to
         * the name server since the server tries to communicate with
         * the listening thread.
         */
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

        // stop all callback threads as well
        Iterator iter = subscriptions.iterator();
        for (; iter.hasNext();) {
            cMsgSubscription sub = (cMsgSubscription) iter.next();

            // run through all callbacks
            Iterator iter2 = sub.callbacks.iterator();
            for (; iter2.hasNext();) {
                cMsgCallbackThread cbThread = (cMsgCallbackThread) iter2.next();
                // Tell the callback thread to wakeup and die
                cbThread.killThread();
                synchronized (cbThread) {
                    cbThread.notify();
                }
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

        if (!connected) return;

        int outGoing[] = new int[9];
        // message id to domain server
        outGoing[0] = cMsgConstants.msgSendRequest;
        // system message
        outGoing[1] = message.getSysMsgId();
        // sender id
        outGoing[2] = message.getSenderId();
        // time message sent (right now)
        outGoing[3] = (int) ((new Date()).getTime());
        // sender message id
        outGoing[4] = message.getSenderMsgId();
        // sender token
        outGoing[5] = message.getSenderToken();

        // length of "subject" string
        outGoing[6] = message.getSubject().length();
        // length of "type" string
        outGoing[7] = message.getType().length();
        // length of "text" string
        outGoing[8] = message.getText().length();

        // get ready to write
        buffer.clear();
        // send ints over together using view buffer
        buffer.asIntBuffer().put(outGoing);
        // position original buffer at position of view buffer
        buffer.position(36);

        // write strings
        try {
            buffer.put(message.getSubject().getBytes("US-ASCII"));
            buffer.put(message.getType().getBytes("US-ASCII"));
            buffer.put(message.getText().getBytes("US-ASCII"));
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
            // read acknowledgment & keep reading until we have 1 int of data
            //cMsgUtilities.readSocketBytes(buffer, domainChannel, 4, debug);
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }

        // go back to reading-from-buffer mode
       // buffer.flip();

        //int error = buffer.getInt();

        //if (error != cMsgConstants.ok) {
       //     throw new cMsgException("send: error in sending message");
       // }
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

        if (!connected) return;

        // add to callback list if subscription to same subject/type exists

        // for each subscription
        cMsgSubscription sub;
        for (Iterator iter = subscriptions.iterator(); iter.hasNext(); ) {
            sub = (cMsgSubscription) iter.next();
            // if subscription to subject & type exists ...
            if (sub.subject.equals(subject) && sub.type.equals(type)) {
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
            // read acknowledgment & keep reading until we have 1 int of data
            //cMsgUtilities.readSocketBytes(buffer, domainChannel, 4, debug);
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }

        // go back to reading-from-buffer mode
        //buffer.flip();

        //int error = buffer.getInt();

        //if (error != cMsgConstants.ok) {
        //    throw new cMsgException("subscribe: error in subscribing");
        //}
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

        if (!connected) return;

        // look for and remove any subscription to subject/type with this callback object

        // for each subscription
        cMsgSubscription sub;
        for (Iterator iter = subscriptions.iterator(); iter.hasNext(); ) {
            sub = (cMsgSubscription) iter.next();
            // if subscription to subject & type exist ...
            if (sub.subject.equals(subject) && sub.type.equals(type)) {
                // for each callback listed
                for (Iterator iter2 = sub.callbacks.iterator(); iter2.hasNext(); ) {
                    cMsgCallbackThread cbThread = (cMsgCallbackThread) iter2.next();
                    if (cbThread.callback == cb) {
                        // remove this callback from the set
                        iter2.remove();
                    }
                }
                // if there are still callbacks left, don't unsubscribe for this subject/type
                if (sub.numberOfCallbacks() > 0) {
                    return;
                }
                break;
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
            // read acknowledgment & keep reading until we have 1 int of data
            //cMsgUtilities.readSocketBytes(buffer, domainChannel, 4, debug);
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }

        // go back to reading-from-buffer mode
        //buffer.flip();

        //int error = buffer.getInt();

        //if (error != cMsgConstants.ok) {
        //    throw new cMsgException("subscribe: error in subscribing");
        //}
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

        // Since everything's OK, we expect to get domain server host & port.
        // Read port & length of host name.

        // read 2 ints of data
        cMsgUtilities.readSocketBytes(buffer, channel, 8, debug);

        buffer.flip();

        domainServerPort = buffer.getInt();
        int hostLength = buffer.getInt();

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
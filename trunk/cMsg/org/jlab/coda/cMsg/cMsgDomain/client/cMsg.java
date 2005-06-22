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

package org.jlab.coda.cMsg.cMsgDomain.client;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.cMsgDomain.*;

import java.io.*;
import java.net.*;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.util.*;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * This class implements a cMsg client in the cMsg domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsg extends cMsgDomainAdapter {

    /** Port number to listen on. */
    private int port;

    /**
     * A string of the form name:nameServerHost:nameserverPort.
     * This is a variable of convenience so it does not have to
     * be calculated for each send.
     */
    private String creator;

    /** Subdomain being used. */
    private String subdomain;

    /** String containing the subdomain remainder part of the UDL. */
    private String subRemainder;

    /** Port number from which to start looking for a suitable listening port. */
    private int startingPort;

    /** Server channel (contains socket). */
    private ServerSocketChannel serverChannel;

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
    /** Socket input stream associated with domainChannel - gets info from server. */
    private DataInputStream  domainIn;
    /** Socket output stream associated with domainChannel - sends info to server. */
    private DataOutputStream domainOut;

    /** Channel for checking to see that the domain server is still alive. */
    private SocketChannel keepAliveChannel;

    /** Thread listening for TCP connections and responding to domain server commands. */
    private cMsgClientListeningThread listeningThread;

    /** Thread for sending keep alive commands to domain server to check its health. */
    private KeepAlive keepAliveThread;

    /**
     * Collection of all of this client's message subscriptions which are
     * {@link cMsgSubscription} objects. This set is synchronized.
     */
    Set<cMsgSubscription> subscriptions;

    /**
     * Collection of all of this client's {@link #subscribeAndGet} calls, NOT directed to
     * a specific receiver, currently in execution.
     * SubscribeAndGets are very similar to subscriptions and can be thought of as
     * one-shot subscriptions.
     *
     * Key is receiverSubscribeId object, value is {@link cMsgHolder} object.
     */
    ConcurrentHashMap<Integer,cMsgHolder> subscribeAndGets;

    /**
     * Collection of all of this client's {@link #sendAndGet} calls, directed to a specific
     * receiver, currently in execution.
     *
     * Key is senderToken object, value is {@link cMsgHolder} object.
     */
    ConcurrentHashMap<Integer,cMsgHolder> sendAndGets;

    /**
     * This lock is for controlling access to the methods of this class.
     * It is inherently more flexible than synchronizing code. The {@link #connect}
     * and {@link #disconnect} methods of this object cannot be called simultaneously
     * with each other or any other method. However, the {@link #send} method is
     * thread-safe and may be called simultaneously from multiple threads. The
     * {@link #syncSend} method is thread-safe with other methods but not itself
     * (since it requires a response from the server) and requires an additional lock.
     * The {@link #subscribeAndGet}, @link #sendAndGet}, {@link #subscribe}, and
     * {@link #unsubscribe} methods are also thread-safe but require some locking
     * for bookkeeping purposes by means of other locks.
     */
    private final ReentrantReadWriteLock methodLock = new ReentrantReadWriteLock();

    /** Lock for calling {@link #connect} or {@link #disconnect}. */
    private Lock connectLock = methodLock.writeLock();

    /** Lock for calling methods other than {@link #connect} or {@link #disconnect}. */
    private Lock notConnectLock = methodLock.readLock();

    /** Lock to ensure {@link #syncSend} calls are sequential. */
    private Lock syncSendLock = new ReentrantLock();

    /** Lock to ensure {@link #subscribe} and {@link #unsubscribe} calls are sequential. */
    private Lock subscribeLock = new ReentrantLock();

    /** Lock to ensure that methods using the socket write in sequence. */
    private Lock socketLock = new ReentrantLock();

    /** Used to create unique id numbers associated with a specific message subject/type pair. */
    private AtomicInteger uniqueId;

    /** The subdomain server object or client handler implements {@link #send}. */
    private boolean hasSend;

    /** The subdomain server object or client handler implements {@link #syncSend}. */
    private boolean hasSyncSend;

    /** The subdomain server object or client handler implements {@link #subscribeAndGet}. */
    private boolean hasSubscribeAndGet;

    /** The subdomain server object or client handler implements {@link #sendAndGet}. */
    private boolean hasSendAndGet;

    /** The subdomain server object or client handler implements {@link #subscribe}. */
    private boolean hasSubscribe;

    /** The subdomain server object or client handler implements {@link #unsubscribe}. */
    private boolean hasUnsubscribe;

    /** The subdomain server object or client handler implements {@link #shutdown}. */
    private boolean hasShutdown;

    /** Level of debug output for this class. */
    int debug = cMsgConstants.debugError;


//-----------------------------------------------------------------------------


    /**
     * Get the name of the subdomain whose plugin is being used.
     * @return subdomain name
     */
    public String getSubdomain() {return(subdomain);}


//-----------------------------------------------------------------------------

    /**
     * Constructor which does NOT automatically try to connect to the name server specified.
     *
     * @throws cMsgException if local host name cannot be found
     */
    public cMsg() throws cMsgException {
        domain = "cMsg";

        subscriptions    = Collections.synchronizedSet(new HashSet<cMsgSubscription>(20));
        subscribeAndGets = new ConcurrentHashMap<Integer,cMsgHolder>(20);
        sendAndGets      = new ConcurrentHashMap<Integer,cMsgHolder>(20);
        uniqueId         = new AtomicInteger();

        // store our host's name
        try {
            host = InetAddress.getLocalHost().getHostName();
        }
        catch (UnknownHostException e) {
            throw new cMsgException("cMsg: cannot find host name");
        }

        // create a shutdown handler class which does a disconnect
        class myShutdownHandler implements cMsgShutdownHandlerInterface {
            cMsgDomainInterface cMsgObject;

            myShutdownHandler(cMsgDomainInterface cMsgObject) {
                this.cMsgObject = cMsgObject;
            }

            public void handleShutdown() {
                try {cMsgObject.disconnect();}
                catch (cMsgException e) {}
            }
        }

        // Now make an instance of the shutdown handler
        // setShutdownHandler(new myShutdownHandler());
    }


//-----------------------------------------------------------------------------


    /**
     * Method to connect to the domain server from regular cMsg client.
     *
     * @throws cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server
     */
    public void connect() throws cMsgException {
        connectReal(false);
    }


//-----------------------------------------------------------------------------


    /**
     * Method to connect to the domain server from a cMsg server acting as a bridge.
     *
     * @throws cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server
     */
    public void serverConnect() throws cMsgException {
        connectReal(true);
    }


//-----------------------------------------------------------------------------


    /**
     * Method to connect to the domain server.
     *
     * @param fromServer boolean stating whether server is being connected to by another server
     * @throws cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server
     */
    private void connectReal(boolean fromServer) throws cMsgException {

        // parse the domain-specific portion of the UDL (Uniform Domain Locator)
        parseUDL();
        creator = name+":"+nameServerHost+":"+nameServerPort;

        // cannot run this simultaneously with any other public method
        connectLock.lock();
        try {
            if (connected) return;

            // read env variable for starting port number
            try {
                String env = System.getenv("CMSG_CLIENT_PORT");
                if (env != null) {
                    startingPort = Integer.parseInt(env);
                }
            }
            catch (NumberFormatException ex) {
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
                // no need to set buffer sizes
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot create channel to name server");
            }

            // get host & port to send messages & other info from name server
            try {
                talkToNameServer(channel, fromServer);
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
                // buffered communication streams for efficiency
                Socket socket = domainChannel.socket();
                socket.setTcpNoDelay(true);
                socket.setReceiveBufferSize(65535);
                socket.setSendBufferSize(65535);
                domainIn  = new DataInputStream(new BufferedInputStream(socket.getInputStream(), 2048));
                domainOut = new DataOutputStream(new BufferedOutputStream(socket.getOutputStream(), 65536));
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
                Socket socket = keepAliveChannel.socket();
                socket.setTcpNoDelay(true);

                // create thread to send periodic keep alives and handle dead server
                keepAliveThread = new KeepAlive(this, keepAliveChannel);
                keepAliveThread.start();
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    e.printStackTrace();
                }
                throw new cMsgException("connect: cannot create keepAlive channel to domain server");
            }

            connected = true;
        }
        finally {
            connectLock.unlock();
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to close the connection to the domain server. This method results in this object
     * becoming functionally useless.
     */
    public void disconnect() {
        // cannot run this simultaneously with any other public method
        connectLock.lock();

        try {
            if (!connected) return;
            connected = false;

            // Stop keep alive thread & close channel so when domain server
            // shuts down, we don't detect it's dead and make a fuss.
            keepAliveThread.killThread();

            // give thread a chance to shutdown
            try { Thread.sleep(100); }
            catch (InterruptedException e) {}

            // tell server we're disconnecting
            socketLock.lock();
            try {
                domainOut.writeInt(4);
                domainOut.writeInt(cMsgConstants.msgDisconnectRequest);
                domainOut.flush();
            }
            catch (IOException e) {
            }
            finally {
                socketLock.unlock();
            }

            // give server a chance to shutdown
            try { Thread.sleep(100); }
            catch (InterruptedException e) {}

            // stop listening and client communication thread & close channel
            listeningThread.killThread();

            // stop all callback threads
            for (cMsgSubscription sub : subscriptions) {
                // run through all callbacks
                for (cMsgCallbackThread cbThread : sub.getCallbacks()) {
                    // Tell the callback thread(s) to wakeup and die
                    cbThread.dieNow();
                }
            }

            // wakeup all gets
            Iterator iter = sendAndGets.values().iterator();
            for (; iter.hasNext();) {
                cMsgHolder holder = (cMsgHolder) iter.next();
                holder.message = null;
                synchronized (holder) {
                    holder.notify();
                }
            }

        }
        finally {
            connectLock.unlock();
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to send a message to the domain server for further distribution.
     *
     * @param message message to send
     * @throws cMsgException if there are communication problems with the server
     */
    public void send(cMsgMessage message) throws cMsgException {
        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();
        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            if (!hasSend) {
                throw new cMsgException("send is not implemented by this subdomain");
            }

            String subject = message.getSubject();
            String type = message.getType();
            String text = message.getText();

            // check message fields first
            if (subject == null || type == null) {
                throw new cMsgException("message subject and/or type is null");
            }

            if (text == null) {
                text = "";
            }

            // creator (this sender's name:nsHost:nsPort if msg created here)
            String msgCreator = message.getCreator();
            if (msgCreator == null) msgCreator = creator;

            int binaryLength = message.getByteArrayLength();

            socketLock.lock();
            try {
                // total length of msg (not including this int) is 1st item
                domainOut.writeInt(4*15 + subject.length() + type.length() + creator.length() +
                                text.length() + binaryLength);
                domainOut.writeInt(cMsgConstants.msgSendRequest);
                domainOut.writeInt(0); // reserved for future use
                domainOut.writeInt(message.getUserInt());
                domainOut.writeInt(message.getSysMsgId());
                domainOut.writeInt(message.getSenderToken());
                domainOut.writeInt(message.getInfo());

                long now = new Date().getTime();
                // send the time in milliseconds as 2, 32 bit integers
                domainOut.writeInt((int) (now >>> 32)); // higher 32 bits
                domainOut.writeInt((int) (now & 0x00000000FFFFFFFFL)); // lower 32 bits
                domainOut.writeInt((int) (message.getUserTime().getTime() >>> 32));
                domainOut.writeInt((int) (message.getUserTime().getTime() & 0x00000000FFFFFFFFL));

                domainOut.writeInt(subject.length());
                domainOut.writeInt(type.length());
                domainOut.writeInt(msgCreator.length());
                domainOut.writeInt(text.length());
                domainOut.writeInt(binaryLength);

                // write strings & byte array
                try {
                    domainOut.write(subject.getBytes("US-ASCII"));
                    domainOut.write(type.getBytes("US-ASCII"));
                    domainOut.write(msgCreator.getBytes("US-ASCII"));
                    domainOut.write(text.getBytes("US-ASCII"));
                    if (binaryLength > 0) {
                        domainOut.write(message.getByteArray(),
                                        message.getByteArrayOffset(),
                                        binaryLength);
                    }
                }
                catch (UnsupportedEncodingException e) {}
            }
            finally {
                socketLock.unlock();
            }

            domainOut.flush(); // no need to be protected by socketLock
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
        finally {
            notConnectLock.unlock();
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
    public int syncSend(cMsgMessage message) throws cMsgException {
        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();
        // cannot run this simultaneously with itself
        syncSendLock.lock();
        try {

            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            if (!hasSyncSend) {
                throw new cMsgException("sync send is not implemented by this subdomain");
            }

            String subject = message.getSubject();
            String type = message.getType();
            String text = message.getText();

            // check args first
            if (subject == null || type == null) {
                throw new cMsgException("message subject and/or type is null");
            }

            if (text == null) {
                text = "";
            }

            // this sender's name if msg created here
            String msgCreator = message.getCreator();
            if (msgCreator == null) msgCreator = creator;

            int binaryLength = message.getByteArrayLength();

            socketLock.lock();
            try {
                // total length of msg (not including this int) is 1st item
                domainOut.writeInt(4 * 15 + subject.length() + type.length() + creator.length() +
                                text.length() + binaryLength);
                domainOut.writeInt(cMsgConstants.msgSendRequest);
                domainOut.writeInt(0); // reserved for future use
                domainOut.writeInt(message.getUserInt());
                domainOut.writeInt(message.getSysMsgId());
                domainOut.writeInt(message.getSenderToken());
                domainOut.writeInt(message.getInfo());

                long now = new Date().getTime();
                // send the time in milliseconds as 2, 32 bit integers
                domainOut.writeInt((int) (now >>> 32)); // higher 32 bits
                domainOut.writeInt((int) (now & 0x00000000FFFFFFFFL)); // lower 32 bits
                domainOut.writeInt((int) (message.getUserTime().getTime() >>> 32));
                domainOut.writeInt((int) (message.getUserTime().getTime() & 0x00000000FFFFFFFFL));

                domainOut.writeInt(subject.length());
                domainOut.writeInt(type.length());
                domainOut.writeInt(msgCreator.length());
                domainOut.writeInt(text.length());
                domainOut.writeInt(binaryLength);

                // write strings & byte array
                try {
                    domainOut.write(subject.getBytes("US-ASCII"));
                    domainOut.write(type.getBytes("US-ASCII"));
                    domainOut.write(msgCreator.getBytes("US-ASCII"));
                    domainOut.write(text.getBytes("US-ASCII"));
                    if (binaryLength > 0) {
                        domainOut.write(message.getByteArray(),
                                        message.getByteArrayOffset(),
                                        binaryLength);
                    }
                }
                catch (UnsupportedEncodingException e) {}
            }
            finally {
                socketLock.unlock();
            }

            domainOut.flush(); // no need to be protected by socketLock
            int response = domainIn.readInt(); // this is protected by syncSendLock
            return response;

        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
        finally {
            syncSendLock.unlock();
            notConnectLock.unlock();
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to force cMsg client to send pending communications with domain server.
     * In the cMsg domain implementation, this method does nothing.
     */
    public void flush() {
        return;
    }


//-----------------------------------------------------------------------------


    /**
     * This method is like a one-time subscribe. The server grabs the first incoming
     * message of the requested subject and type and sends that to the caller.
     *
     * NOTE: Disconnecting when one thread is in the waiting part of a get may cause that
     * thread to block forever. It is best to always use a timeout with "get" so the thread
     * is assured of eventually resuming execution.
     *
     * @param subject subject of message desired from server
     * @param type type of message desired from server
     * @param timeout time in milliseconds to wait for a message
     * @return response message
     * @throws cMsgException
     * @throws TimeoutException if timeout occurs
     */
    public cMsgMessage subscribeAndGet(String subject, String type, int timeout)
            throws cMsgException, TimeoutException {

        int id;
        cMsgHolder holder = null;

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            if (!hasSubscribeAndGet) {
                throw new cMsgException("subscribeAndGet is not implemented by this subdomain");
            }

            // check args first
            if (subject == null || type == null) {
                throw new cMsgException("message subject or type is null");
            }
            else if (subject.length() < 1 || type.length() < 1) {
                throw new cMsgException("message subject or type is blank string");
            }

            // First generate a unique id for the receiveSubscribeId and senderToken field.
            //
            // The receiverSubscribeId is sent back by the domain server in the future when
            // messages of this subject and type are sent to this cMsg client. This helps
            // eliminate the need to parse subject and type each time a message arrives.

            id = uniqueId.getAndIncrement();

            // for get, create cMsgHolder object (not callback thread object)
            holder = new cMsgHolder(subject, type);

            // keep track of get calls
            subscribeAndGets.put(id, holder);

            socketLock.lock();
            try {
                // total length of msg (not including this int) is 1st item
                domainOut.writeInt(4*4 + subject.length() + type.length());
                domainOut.writeInt(cMsgConstants.msgSubscribeAndGetRequest);
                domainOut.writeInt(id); // reserved for future use
                domainOut.writeInt(subject.length());
                domainOut.writeInt(type.length());

                // write strings & byte array
                try {
                    domainOut.write(subject.getBytes("US-ASCII"));
                    domainOut.write(type.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {}
            }
            finally {
                socketLock.unlock();
            }
            domainOut.flush();
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
        // release lock 'cause we can't block connect/disconnect forever
        finally {
            notConnectLock.unlock();
        }

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
        if (holder.timedOut) {
            System.out.println("subscribeAndGet: timed out");
            // remove the get from server
            subscribeAndGets.remove(id);
            unSubscribeAndGet(id);
            throw new TimeoutException();
        }

        // If msg is received, server has removed subscription from his records.
        // Client listening thread has also removed subscription from client's
        // records (subscribeAndGets HashSet).

//System.out.println("subscribeAndGet: SUCCESS!!!");

        return holder.message;
    }

    /**
     * The message is sent as it would be in the {@link #send} method. The server notes
     * the fact that a response to it is expected, and sends it to all subscribed to its
     * subject and type. When a marked response is received from a client, it sends that
     * first response back to the original sender regardless of its subject or type.
     *
     * NOTE: Disconnecting when one thread is in the waiting part of a get may cause that
     * thread to block forever. It is best to always use a timeout with "get" so the thread
     * is assured of eventually resuming execution.
     *
     * @param message message sent to server
     * @param timeout time in milliseconds to wait for a reponse message
     * @return response message
     * @throws cMsgException
     * @throws TimeoutException if timeout occurs
     */
    public cMsgMessage sendAndGet(cMsgMessage message, int timeout)
            throws cMsgException, TimeoutException {
        int id;
        cMsgHolder holder = null;

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            if (!hasSendAndGet) {
                throw new cMsgException("sendAndGet is not implemented by this subdomain");
            }

            String subject = message.getSubject();
            String type = message.getType();
            String text = message.getText();

            // check args first
            if (subject == null || type == null) {
                throw new cMsgException("message subject and/or type is null");
            }

            if (text == null) {
                text = "";
            }

            // We need send msg to domain server who will see we get a response.
            // First generate a unique id for the receiveSubscribeId and senderToken field.
            //
            // We're expecting a specific response, so the senderToken is sent back
            // in the response message, allowing us to run the correct callback.

            id = uniqueId.getAndIncrement();

            // for get, create cMsgHolder object (not callback thread object)
            holder = new cMsgHolder();

            // track specific get requests
            sendAndGets.put(id, holder);

            // this sender's creator if msg created here
            String msgCreator = message.getCreator();
            if (msgCreator == null) msgCreator = creator;

            int binaryLength = message.getByteArrayLength();

            socketLock.lock();
            try {
                // total length of msg (not including this int) is 1st item
                domainOut.writeInt(4*14 + subject.length() + type.length() + creator.length() +
                                   text.length() + binaryLength);
                domainOut.writeInt(cMsgConstants.msgSendAndGetRequest);
                domainOut.writeInt(0); // reserved for future use
                domainOut.writeInt(message.getUserInt());
                domainOut.writeInt(id);
                domainOut.writeInt(message.getInfo() | cMsgMessage.isGetRequest);

                long now = new Date().getTime();
                // send the time in milliseconds as 2, 32 bit integers
                domainOut.writeInt((int) (now >>> 32)); // higher 32 bits
                domainOut.writeInt((int) (now & 0x00000000FFFFFFFFL)); // lower 32 bits
                domainOut.writeInt((int) (message.getUserTime().getTime() >>> 32));
                domainOut.writeInt((int) (message.getUserTime().getTime() & 0x00000000FFFFFFFFL));

                domainOut.writeInt(subject.length());
                domainOut.writeInt(type.length());
                domainOut.writeInt(msgCreator.length());
                domainOut.writeInt(text.length());
                domainOut.writeInt(binaryLength);

                // write strings & byte array
                try {
                    domainOut.write(subject.getBytes("US-ASCII"));
                    domainOut.write(type.getBytes("US-ASCII"));
                    domainOut.write(msgCreator.getBytes("US-ASCII"));
                    domainOut.write(text.getBytes("US-ASCII"));
                    if (binaryLength > 0) {
                        domainOut.write(message.getByteArray(),
                                        message.getByteArrayOffset(),
                                        binaryLength);
                    }
                }
                catch (UnsupportedEncodingException e) {}
            }
            finally {
                socketLock.unlock();
            }

            domainOut.flush(); // no need to be protected by socketLock

        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
        // release lock 'cause we can't block connect/disconnect forever
        finally {
            notConnectLock.unlock();
        }

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

        // Tell server to forget the get if necessary.
        if (holder.timedOut) {
            System.out.println("sendAndGet: timed out");
            // remove the get from server
            sendAndGets.remove(id);
            unSendAndGet(id);
            throw new TimeoutException();
        }

        // If msg arrived (may be null), server has removed subscription from his records.
        // Client listening thread has also removed subscription from client's
        // records (subscribeAndGets HashSet).

//System.out.println("get: SUCCESS!!!");

        return holder.message;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to remove a previous sendAndGet to receive a message of a subject and type
     * from the domain server. This method is only called when a sendAndGet times out
     * and the server must be told to forget about the get.
     *
     * @param id unique id of get request to delete
     * @throws cMsgException if there are communication problems with the server
     */
    private void unSendAndGet(int id) throws cMsgException {

        if (!connected) {
            throw new cMsgException("not connected to server");
        }

        socketLock.lock();
        try {
            // total length of msg (not including this int) is 1st item
            domainOut.writeInt(8);
            domainOut.writeInt(cMsgConstants.msgUnSendAndGetRequest);
            domainOut.writeInt(id); // reserved for future use
            domainOut.flush();
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
        finally {
            socketLock.unlock();
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to remove a previous subscribeAndGet to receive a message of a subject
     * and type from the domain server. This method is only called when a subscribeAndGet
     * times out and the server must be told to forget about the get.
     *
     * @param id unique id of get request to delete
     * @throws cMsgException if there are communication problems with the server
     */
    private void unSubscribeAndGet(int id) throws cMsgException {

        if (!connected) {
            throw new cMsgException("not connected to server");
        }

        socketLock.lock();
        try {
            // total length of msg (not including this int) is 1st item
            domainOut.writeInt(8);
            domainOut.writeInt(cMsgConstants.msgUnSubscribeAndGetRequest);
            domainOut.writeInt(id); // reserved for future use
            domainOut.flush();
        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
        finally {
            socketLock.unlock();
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to subscribe to receive messages of a subject and type from the domain server.
     * The combination of arguments must be unique. In other words, only 1 subscription is
     * allowed for a given set of subject, type, callback, and userObj.
     *
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @param userObj any user-supplied object to be given to the callback method as an argument
     * @throws cMsgException if the callback, subject, or type is null; the subject or type is
     *                       blank; an identical subscription already exists; there are
     *                       communication problems with the server
     */
    public void subscribe(String subject, String type, cMsgCallbackInterface cb, Object userObj)
            throws cMsgException {

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();
        // cannot run this simultaneously with unsubscribe (get wrong order at server)
        // or itself (iterate over same hashtable)
        subscribeLock.lock();

        try {
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

            // for each subscription ...
            for (cMsgSubscription sub : subscriptions) {
                // if subscription to subject & type exists ...
                if (sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                    // Only add another callback if the callback/userObj
                    // combination does NOT already exist. In other words,
                    // a callback/argument pair must be unique for a single
                    // subscription. Otherwise it is impossible to unsubscribe.

                    // for each callback listed ...
                    for (cMsgCallbackThread cbThread : sub.getCallbacks()) {
                        // if callback and user arg already exist, reject the subscription
                        if ((cbThread.callback == cb) && (cbThread.getArg() == userObj)) {
                            throw new cMsgException("subscription already exists");
                        }
                    }

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
            int id = uniqueId.getAndIncrement();

            // add a new subscription & callback
            cMsgSubscription sub = new cMsgSubscription(subject, type, id,
                                                        new cMsgCallbackThread(cb, userObj));
            subscriptions.add(sub);

            socketLock.lock();
            try {
                // total length of msg (not including this int) is 1st item
                domainOut.writeInt(4*4 + subject.length() + type.length());
                domainOut.writeInt(cMsgConstants.msgSubscribeRequest);
                domainOut.writeInt(id); // reserved for future use
                domainOut.writeInt(subject.length());
                domainOut.writeInt(type.length());

                // write strings & byte array
                try {
                    domainOut.write(subject.getBytes("US-ASCII"));
                    domainOut.write(type.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {}
            }
            finally {
                socketLock.unlock();
            }

            domainOut.flush();

        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
        finally {
            subscribeLock.unlock();
            notConnectLock.unlock();
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to unsubscribe a previous subscription to receive messages of a subject and type
     * from the domain server. Since many subscriptions may be made to the same subject and type
     * values, but with different callbacks and user objects, the callback and user object must
     * be specified so the correct subscription can be removed.
     *
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @param userObj any user-supplied object to be given to the callback method as an argument
     * @throws cMsgException if there are communication problems with the server
     */
    public void unsubscribe(String subject, String type, cMsgCallbackInterface cb, Object userObj)
            throws cMsgException {

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();
        // cannot run this simultaneously with subscribe (get wrong order at server)
        // or itself (iterate over same hashtable)
        subscribeLock.lock();

        try {
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
            int id = 0;

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
                            if ((cbThread.callback == cb) && (cbThread.getArg() == userObj)) {
                                // kill callback thread(s)
                                cbThread.dieNow();
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
                            id = sub.getId();
                            iter.remove();
                        }

                        break;
                    }
                }
            }

            // notify the domain server

            socketLock.lock();
            try {
                // total length of msg (not including this int) is 1st item
                domainOut.writeInt(4*4 + subject.length() + type.length());
                domainOut.writeInt(cMsgConstants.msgUnsubscribeRequest);
                domainOut.writeInt(id); // reserved for future use
                domainOut.writeInt(subject.length());
                domainOut.writeInt(type.length());

                // write strings & byte array
                try {
                    domainOut.write(subject.getBytes("US-ASCII"));
                    domainOut.write(type.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {}
            }
            finally {
                socketLock.unlock();
            }

            domainOut.flush();

        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
        finally {
            subscribeLock.unlock();
            notConnectLock.unlock();
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to shutdown the given clients and/or servers.
     *
     * @param client client(s) to be shutdown
     * @param server server(s) to be shutdown
     * @param flag   flag describing the mode of shutdown
     * @throws cMsgException
     */
    public void shutdown(String client, String server, int flag) throws cMsgException {
        // cannot run this simultaneously with any other public method
        connectLock.lock();
        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            if (!hasShutdown) {
                throw new cMsgException("shutdown is not implemented by this subdomain");
            }

            // make sure null args are sent as blanks
            if (client == null) {
                client = new String("");
            }
            if (server == null) {
                server = new String("");
            }

            socketLock.lock();
            try {
                // total length of msg (not including this int) is 1st item
                domainOut.writeInt(4*4 + client.length() + server.length());
                domainOut.writeInt(cMsgConstants.msgShutdown);
                domainOut.writeInt(flag); // reserved for future use
                domainOut.writeInt(client.length());
                domainOut.writeInt(server.length());

                // write strings & byte array
                try {
                    domainOut.write(client.getBytes("US-ASCII"));
                    domainOut.write(server.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {}
            }
            finally {
                socketLock.unlock();
            }

            domainOut.flush();

        }
        catch (IOException e) {
            throw new cMsgException(e.getMessage());
        }
        finally {
            connectLock.unlock();
        }
    }


//-----------------------------------------------------------------------------


    /**
     * This method acts as a multiplexer to send client to server communications to
     * the method {@link #talkToNameServerFromClient} and server to server communications
     * to the method {@link #talkToNameServerFromServer}.
     *
     * @param channel nio socket communication channel
     * @param fromServer boolean stating whether server is being connected by
     *                   another server or by a client
     * @throws IOException if there are communication problems with the name server
     * @throws cMsgException if the name server's domain does not match the UDL's domain,'
     *                       the client cannot be registered, the domain server cannot
     *                       open a listening socket or find a port to listen on, or
     *                       the name server cannot establish a connection to the client
     */
    private void talkToNameServer(SocketChannel channel, boolean fromServer)
            throws IOException, cMsgException {
        if (fromServer) {
            talkToNameServerFromServer(channel);
        }
        else {
            talkToNameServerFromClient(channel);
        }
    }


//-----------------------------------------------------------------------------


    /**
     * This method gets the host and port of the domain server from the name server.
     * It also gets information about the subdomain handler object.
     * Note to those who would make changes in the protocol, keep the first three
     * ints the same. That way the server can reliably check for mismatched versions.
     *
     * @param channel nio socket communication channel
     * @throws IOException if there are communication problems with the name server
     */
    private void talkToNameServerFromServer(SocketChannel channel)
            throws IOException, cMsgException {
        byte[] buf = new byte[512];

        DataInputStream  in  = new DataInputStream(new BufferedInputStream(channel.socket().getInputStream()));
        DataOutputStream out = new DataOutputStream(new BufferedOutputStream(channel.socket().getOutputStream()));

        out.writeInt(cMsgConstants.msgServerConnectRequest);
        out.writeInt(cMsgConstants.version);
        out.writeInt(cMsgConstants.minorVersion);
        out.writeInt(port);
        out.writeInt(domain.length());
        out.writeInt(subdomain.length());
        out.writeInt(subRemainder.length());
        out.writeInt(host.length());
        out.writeInt(name.length());
        out.writeInt(UDL.length());
        out.writeInt(description.length());

        // write strings & byte array
        try {
            out.write(domain.getBytes("US-ASCII"));
            out.write(subdomain.getBytes("US-ASCII"));
            out.write(subRemainder.getBytes("US-ASCII"));
            out.write(host.getBytes("US-ASCII"));
            out.write(name.getBytes("US-ASCII"));
            out.write(UDL.getBytes("US-ASCII"));
            out.write(description.getBytes("US-ASCII"));
        }
        catch (UnsupportedEncodingException e) {
        }

        out.flush(); // no need to be protected by socketLock

        // read acknowledgment
        int error = in.readInt();

        // if there's an error, read error string then quit
        if (error != cMsgConstants.ok) {

            // read string length
            int len = in.readInt();
            if (len > buf.length) {
                buf = new byte[len+100];
            }

            // read error string
            in.readFully(buf, 0, len);
            String err = new String(buf, 0, len, "US-ASCII");

            throw new cMsgException("Error from server: " + err);
        }

        // Since everything's OK, we expect to get:
        //   1) attributes of subdomain handler object
        //   2) domain server host & port

        in.readFully(buf,0,7);

        hasSend            = (buf[0] == (byte)1) ? true : false;
        hasSyncSend        = (buf[1] == (byte)1) ? true : false;
        hasSubscribeAndGet = (buf[2] == (byte)1) ? true : false;
        hasSendAndGet      = (buf[3] == (byte)1) ? true : false;
        hasSubscribe       = (buf[4] == (byte)1) ? true : false;
        hasUnsubscribe     = (buf[5] == (byte)1) ? true : false;
        hasShutdown        = (buf[6] == (byte)1) ? true : false;

        // Read port & length of host name.
        domainServerPort = in.readInt();
        int hostLength   = in.readInt();

        // read host name
        if (hostLength > buf.length) {
            buf = new byte[hostLength];
        }
        in.readFully(buf, 0, hostLength);
        domainServerHost = new String(buf, 0, hostLength, "US-ASCII");

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("  domain server host = " + domainServerHost +
                               ", port = " + domainServerPort);
        }
    }


//-----------------------------------------------------------------------------


    /**
     * This method gets the host and port of the domain server from the name server.
     * It also gets information about the subdomain handler object.
     * Note to those who would make changes in the protocol, keep the first three
     * ints the same. That way the server can reliably check for mismatched versions.
     *
     * @param channel nio socket communication channel
     * @throws IOException if there are communication problems with the name server
     * @throws cMsgException if the name server's domain does not match the UDL's domain,'
     *                       the client cannot be registered, the domain server cannot
     *                       open a listening socket or find a port to listen on, or
     *                       the name server cannot establish a connection to the client
     */
    private void talkToNameServerFromClient(SocketChannel channel)
            throws IOException, cMsgException {

        byte[] buf = new byte[512];

        DataInputStream  in  = new DataInputStream(new BufferedInputStream(channel.socket().getInputStream()));
        DataOutputStream out = new DataOutputStream(new BufferedOutputStream(channel.socket().getOutputStream()));

        out.writeInt(cMsgConstants.msgConnectRequest);
        out.writeInt(cMsgConstants.version);
        out.writeInt(cMsgConstants.minorVersion);
        out.writeInt(port);
        out.writeInt(domain.length());
        out.writeInt(subdomain.length());
        out.writeInt(subRemainder.length());
        out.writeInt(host.length());
        out.writeInt(name.length());
        out.writeInt(UDL.length());
        out.writeInt(description.length());

        // write strings & byte array
        try {
            out.write(domain.getBytes("US-ASCII"));
            out.write(subdomain.getBytes("US-ASCII"));
            out.write(subRemainder.getBytes("US-ASCII"));
            out.write(host.getBytes("US-ASCII"));
            out.write(name.getBytes("US-ASCII"));
            out.write(UDL.getBytes("US-ASCII"));
            out.write(description.getBytes("US-ASCII"));
        }
        catch (UnsupportedEncodingException e) {
        }

        out.flush(); // no need to be protected by socketLock

        // read acknowledgment
        int error = in.readInt();

        // if there's an error, read error string then quit
        if (error != cMsgConstants.ok) {

            // read string length
            int len = in.readInt();
            if (len > buf.length) {
                buf = new byte[len+100];
            }

            // read error string
            in.readFully(buf, 0, len);
            String err = new String(buf, 0, len, "US-ASCII");

            throw new cMsgException("Error from server: " + err);
        }

        // Since everything's OK, we expect to get:
        //   1) attributes of subdomain handler object
        //   2) domain server host & port

        in.readFully(buf,0,7);

        hasSend            = (buf[0] == (byte)1) ? true : false;
        hasSyncSend        = (buf[1] == (byte)1) ? true : false;
        hasSubscribeAndGet = (buf[2] == (byte)1) ? true : false;
        hasSendAndGet      = (buf[3] == (byte)1) ? true : false;
        hasSubscribe       = (buf[4] == (byte)1) ? true : false;
        hasUnsubscribe     = (buf[5] == (byte)1) ? true : false;
        hasShutdown        = (buf[6] == (byte)1) ? true : false;

        // Read port & length of host name.
        domainServerPort = in.readInt();
        int hostLength   = in.readInt();

        // read host name
        if (hostLength > buf.length) {
            buf = new byte[hostLength];
        }
        in.readFully(buf, 0, hostLength);
        domainServerHost = new String(buf, 0, hostLength, "US-ASCII");

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("  domain server host = " + domainServerHost +
                               ", port = " + domainServerPort);
        }
    }


    /**
     * Method to parse the domain-specific portion of the Universal Domain Locator
     * (UDL) into its various components.
     *
     * @throws cMsgException if UDL is null, or no host given in UDL
     */
    private void parseUDL() throws cMsgException {

        if (UDLremainder == null) {
            throw new cMsgException("invalid UDL");
        }

        // cMsg domain UDL is of the form:
        //       cMsg:<domainType>://<host>:<port>/<subdomainType>/<subdomain remainder>
        //
        // We could parse the whole UDL, but we've also been passed the UDL with
        // the "cMsg:<domainType>://" stripped off, in UDLremainder. So just parse
        // that.
        //
        // Remember that for this domain:
        // 1) port is not necessary
        // 2) host can be "localhost"
        // 3) if domainType is cMsg, subdomainType is automatically set to cMsg if not given.
        //    if subdomainType is not cMsg, it is required
        // 4) remainder is past on to the subdomain plug-in

        Pattern pattern = Pattern.compile("(\\w+):?(\\d+)?/?(\\w+)?/?(.*)");
        Matcher matcher = pattern.matcher(UDLremainder);

        String s2=null, s3=null, s4=null, s5=null;

        if (matcher.find()) {
            // host
            s2 = matcher.group(1);
            // port
            s3 = matcher.group(2);
            // subdomain
            s4 = matcher.group(3);
            // remainder
            s5 = matcher.group(4);
        }
        else {
            throw new cMsgException("invalid UDL");
        }

        if (debug >= cMsgConstants.debugInfo) {
           System.out.println("\nparseUDL: " +
                              "\n  host      = " + s2 +
                              "\n  port      = " + s3 +
                              "\n  subdomain = " + s4 +
                              "\n  remainder = " + s5);
        }

        // need at least host
        if (s2 == null) {
            throw new cMsgException("invalid UDL");
        }

        // if subdomain not specified, use cMsg subdomain
        if (s4 == null) {
            s4 = "cMsg";
        }

        // domain is set in constructor to "cMsg"
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
        subRemainder = s5;
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

    /** Socket input stream associated with channel. */
    private DataInputStream  in;

    /** Socket output stream associated with channel. */
    private DataOutputStream out;

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
    public KeepAlive(cMsg client, SocketChannel channel) throws IOException {
        this.client  = client;
        this.channel = channel;
        this.debug   = client.debug;
        // buffered communication streams for efficiency
        in  = new DataInputStream(new BufferedInputStream(channel.socket().getInputStream()));
        out = new DataOutputStream(new BufferedOutputStream(channel.socket().getOutputStream()));

        // die if no more non-daemon thds running
        setDaemon(true);
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
                out.writeInt(4);
                out.writeInt(cMsgConstants.msgKeepAlive);
                out.flush();

                // read response -  1 int
                in.readInt();

               // sleep for 1 second and try again
               try {Thread.sleep(1500);}
               catch (InterruptedException e) {}
            }
        }
        catch (IOException e) {
            e.printStackTrace();
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
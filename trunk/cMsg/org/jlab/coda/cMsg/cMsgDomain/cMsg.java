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

package org.jlab.coda.cMsg.cMsgDomain;

import org.jlab.coda.cMsg.*;

import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.net.*;
import java.nio.ByteBuffer;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.util.*;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.concurrent.atomic.AtomicInteger;
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

    /** This cMsg client's host. */
    private String host;

    /** Subdomain being used. */
    private String subdomain;

    /** String containing the subdomain remainder part of the UDL. */
    private String subRemainder;

    /** Port number from which to start looking for a suitable listening port. */
    private int startingPort;

    /** Server channel (contains socket). */
    private ServerSocketChannel serverChannel;

    /**
     * A direct buffer is necessary for nio socket IO. This buffer is used in
     * the {@link #send}.
     */
    private ByteBuffer sendBuffer = ByteBuffer.allocateDirect(2048);

    /**
     * A direct buffer is necessary for nio socket IO. This buffer is used in
     * the {@link #syncSend}.
     */
    private ByteBuffer syncSendBuffer = ByteBuffer.allocateDirect(2048);

    /**
     * A direct buffer is necessary for nio socket IO. This buffer is used in
     * {@link #subscribeAndGet} and {@link #sendAndGet}.
     */
    private ByteBuffer getBuffer = ByteBuffer.allocateDirect(2048);

    /**
     * A direct buffer is necessary for nio socket IO. This buffer is used in
     * the mutually exclusive {@link #subscribe} and {@link #unsubscribe} methods.
     * There is no way for these methods to use this buffer simulaneously.
     */
    private ByteBuffer subBuffer = ByteBuffer.allocateDirect(2048);

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
     * Collection of all of this client's {@link #subscribeAndGet} calls, NOT directed to
     * a specific receiver, currently in execution.
     * General gets are very similar to subscriptions and can be thought of as
     * one-shot subscriptions.
     *
     * Key is senderToken object, value is {@link cMsgHolder} object.
     */
    Map generalGets;

    /**
     * Collection of all of this client's {@link #sendAndGet} calls, directed to a specific
     * receiver, currently in execution.
     *
     * Key is senderToken object, value is {@link cMsgHolder} object.
     */
    Map specificGets;

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

    /** Lock to ensure {@link #send} calls use {@link #sendBuffer} one at a time. */
    private Lock sendBufferLock = new ReentrantLock();

    /**
     * Lock to ensure {@link #subscribeAndGet} and {@link #sendAndGet} calls use
     * {@link #getBuffer} one at a time.
     */
    private Lock getBufferLock = new ReentrantLock();

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
     * Constructor which automatically tries to connect to the name server specified.
     *
     * @throws cMsgException if domain in not implemented or there are problems communicating
     *                       with the name/domain server.
     */
    public cMsg() throws cMsgException {
        //this.UDL = UDL;
        //this.name = name;
        //this.description = description;
        domain = "cMsg";

        subscriptions = Collections.synchronizedSet(new HashSet(20));
        generalGets   = Collections.synchronizedMap(new HashMap(20));
        specificGets  = Collections.synchronizedMap(new HashMap(20));
        uniqueId      = new AtomicInteger();

        // store our host's name
        try {
            host = InetAddress.getLocalHost().getHostName();
        }
        catch (UnknownHostException e) {
            throw new cMsgException("cMsg: cannot find host name");
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to connect to the domain server.
     *
     * @throws cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server
     */
    public void connect() throws cMsgException {

        // parse the domain-specific portion of the UDL (Uniform Domain Locator)
        parseUDL();

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

            // get host & port to send messages & other info from name server
            try {
                talkToNameServer(channel);
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

//bug bug talk to server;

        try {
            if (!connected) return;
            connected = false;

            // tell server we're disconnecting
            sendBuffer.clear();
            sendBuffer.putInt(cMsgConstants.msgDisconnectRequest);
            try {
                sendBuffer.flip();
                while (sendBuffer.hasRemaining()) {
                    domainChannel.write(sendBuffer);
                }
            }
            catch (IOException e) {
            }

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
                    // Tell the callback thread(s) to wakeup and die
                    cbThread.dieNow();
                }
            }

            // wakeup all gets
            iter = specificGets.values().iterator();
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

            // check args first
            if (subject == null || type == null) {
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

            int outGoing[] = new int[12];
            outGoing[0] = cMsgConstants.msgSendRequest;
            outGoing[1]  = cMsgConstants.version;
            outGoing[2]  = message.getPriority();
            outGoing[3]  = message.getUserInt();
            outGoing[4]  = message.getSysMsgId();
            outGoing[5]  = message.getSenderToken();
            outGoing[6]  = message.isGetResponse() ? 1 : 0;
            outGoing[7]  = (int) ((new Date()).getTime() / 1000L);
            outGoing[8]  = (int) (message.getUserTime().getTime() / 1000L);

            outGoing[9]  = subject.length();
            outGoing[10] = type.length();
            outGoing[11] = text.length();

            // lock to prevent parallel sends from using same buffer
            sendBufferLock.lock();
            try {
                // get ready to write
                sendBuffer.clear();
                // send ints over together using view buffer
                sendBuffer.asIntBuffer().put(outGoing);
                // position original buffer at position of view buffer
                sendBuffer.position(48);

                // write strings
                try {
                    sendBuffer.put(subject.getBytes("US-ASCII"));
                    sendBuffer.put(type.getBytes("US-ASCII"));
                    sendBuffer.put(text.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {
                    e.printStackTrace();
                }

                try {
                    // send buffer over the socket
                    sendBuffer.flip();
                    while (sendBuffer.hasRemaining()) {
                        domainChannel.write(sendBuffer);
                    }
                }
                catch (IOException e) {
                    throw new cMsgException(e.getMessage());
                }
            }
            finally {
                sendBufferLock.unlock();
            }
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
                throw new cMsgException("send is not implemented by this subdomain");
            }

            String subject = message.getSubject();
            String type = message.getType();
            String text = message.getText();

            // check args first
            if (subject == null || type == null) {
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

            int outGoing[] = new int[12];
            outGoing[0]  = cMsgConstants.msgSyncSendRequest;
            outGoing[1]  = cMsgConstants.version;
            outGoing[2]  = message.getPriority();
            outGoing[3]  = message.getUserInt();
            outGoing[4]  = message.getSysMsgId();
            outGoing[5]  = message.getSenderToken();
            outGoing[6]  = message.isGetResponse() ? 1 : 0;
            outGoing[7]  = (int) ((new Date()).getTime() / 1000L);
            outGoing[8]  = (int) (message.getUserTime().getTime() / 1000L);

            outGoing[9]  = subject.length();
            outGoing[10] = type.length();
            outGoing[11] = text.length();

            // get ready to write
            syncSendBuffer.clear();
            // send ints over together using view buffer
            syncSendBuffer.asIntBuffer().put(outGoing);
            // position original buffer at position of view buffer
            syncSendBuffer.position(48);

            // write strings
            try {
                syncSendBuffer.put(subject.getBytes("US-ASCII"));
                syncSendBuffer.put(type.getBytes("US-ASCII"));
                syncSendBuffer.put(text.getBytes("US-ASCII"));
            }
            catch (UnsupportedEncodingException e) {
                e.printStackTrace();
            }

            try {
                // send buffer over the socket
                syncSendBuffer.flip();
                while (syncSendBuffer.hasRemaining()) {
                    domainChannel.write(syncSendBuffer);
                }
                // read acknowledgment - 1 int of data
                cMsgUtilities.readSocketBytes(syncSendBuffer, domainChannel, 4, debug);
            }
            catch (IOException e) {
                throw new cMsgException(e.getMessage());
            }

            // go back to reading-from-buffer mode
            syncSendBuffer.flip();

            int response = syncSendBuffer.getInt();
            return response;

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
     */
    public cMsgMessage subscribeAndGet(String subject, String type, int timeout)
            throws cMsgException {

        int id;
        cMsgHolder holder = null;

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            if (!hasSubscribeAndGet) {
                throw new cMsgException("get is not implemented by this subdomain");
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
            holder = new cMsgHolder();

            // keep track of get calls
            generalGets.put(id, holder);

            int[] outGoing = new int[4];
            // first send message id to server
            outGoing[0] = cMsgConstants.msgSubscribeAndGetRequest;
            // send unique id of this subscription
            outGoing[1] = id;
            // send length of subscription subject
            outGoing[2] = subject.length();
            // send length of subscription type
            outGoing[3] = type.length();

            // lock to prevent parallel gets from using same buffer
            getBufferLock.lock();
            try {
                // get ready to write
                getBuffer.clear();
                // send ints over together using view buffer
                getBuffer.asIntBuffer().put(outGoing);
                // position original buffer at position of view buffer
                getBuffer.position(16);

                // write strings
                try {
                    getBuffer.put(subject.getBytes("US-ASCII"));
                    getBuffer.put(type.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {
                    e.printStackTrace();
                }

                try {
                    // send buffer over the socket
                    getBuffer.flip();
                    while (getBuffer.hasRemaining()) {
                        domainChannel.write(getBuffer);
                    }
                }
                catch (IOException e) {
                    throw new cMsgException(e.getMessage());
                }
            }
            finally {
                getBufferLock.unlock();
            }
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
        if (holder.message == null) {
            System.out.println("get: timed out");
            // remove the get from server
            generalGets.remove(id);
            unget(id);
            return null;
        }

        // If msg is not null, server has removed subscription from his records.
        // Client listening thread has also removed subscription from client's
        // records (generalGets HashSet).

//System.out.println("get: SUCCESS!!!");

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
     */
    public cMsgMessage sendAndGet(cMsgMessage message, int timeout) throws cMsgException {
        int id;
        cMsgHolder holder = null;

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            if (!hasSendAndGet) {
                throw new cMsgException("get is not implemented by this subdomain");
            }

            String subject = message.getSubject();
            String type = message.getType();
            String text = message.getText();

            // check args first
            if (subject == null || type == null) {
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

            // We need send msg to domain server who will see we get a response.
            // First generate a unique id for the receiveSubscribeId and senderToken field.
            //
            // We're expecting a specific response, so the senderToken is sent back
            // in the response message, allowing us to run the correct callback.

            id = uniqueId.getAndIncrement();

            // for get, create cMsgHolder object (not callback thread object)
            holder = new cMsgHolder();

            // track specific get requests
            specificGets.put(id, holder);

            int outGoing[] = new int[10];
            outGoing[0] = cMsgConstants.msgSendAndGetRequest;
            outGoing[1] = cMsgConstants.version;
            outGoing[2] = message.getPriority();
            outGoing[3] = message.getUserInt();
            outGoing[4] = id;
            outGoing[5] = (int) ((new Date()).getTime() / 1000L);
            outGoing[6] = (int) (message.getUserTime().getTime() / 1000L);

            outGoing[7] = subject.length();
            outGoing[8] = type.length();
            outGoing[9] = text.length();

            // lock to prevent parallel gets from using same buffer
            getBufferLock.lock();
            try {
                // get ready to write
                getBuffer.clear();
                // send ints over together using view buffer
                getBuffer.asIntBuffer().put(outGoing);
                // position original buffer at position of view buffer
                getBuffer.position(40);

                // write strings
                try {
                    getBuffer.put(subject.getBytes("US-ASCII"));
                    getBuffer.put(type.getBytes("US-ASCII"));
                    getBuffer.put(text.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {
                    e.printStackTrace();
                }

                try {
                    // send buffer over the socket
                    getBuffer.flip();
                    while (getBuffer.hasRemaining()) {
                        domainChannel.write(getBuffer);
                    }
                }
                catch (IOException e) {
                    throw new cMsgException(e.getMessage());
                }
            }
            finally {
                getBufferLock.unlock();
            }
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
        if (holder.message == null) {
            System.out.println("get: timed out");
            // remove the get from server
            specificGets.remove(id);
            unget(id);
            return null;
        }

        // If msg is not null, server has removed subscription from his records.
        // Client listening thread has also removed subscription from client's
        // records (generalGets HashSet).

//System.out.println("get: SUCCESS!!!");

        return holder.message;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to unget a previous get to receive a message of a subject and type
     * from the domain server.
     * This method is only called when a get timeouts out and the server must be
     * told to forget about the get.
     *
     * @param id unique id of get request to delete
     * @throws cMsgException if there are communication problems with the server
     */
    private void unget(int id)
            throws cMsgException {

        System.out.println("unget: in");
        if (!connected) {
            throw new cMsgException("not connected to server");
        }

        // lock to prevent parallel (un)gets from using same buffer
        getBufferLock.lock();
        try {
            // get ready to write
            getBuffer.clear();
            // send ints over
            getBuffer.putInt(cMsgConstants.msgUngetRequest);
            getBuffer.putInt(id);

            try {
                // send buffer over the socket
                getBuffer.flip();
                while (getBuffer.hasRemaining()) {
                    domainChannel.write(getBuffer);
                }
            }
            catch (IOException e) {
                throw new cMsgException(e.getMessage());
            }
        }
        finally {
            getBufferLock.unlock();
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
    public void subscribe(String subject, String type, cMsgCallbackInterface cb, Object userObj)
            throws cMsgException {

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();
        // cannot run this simultaneously with unsubscribe (get wrong order at server)
        // or itself (use same buffer)
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
            int id = uniqueId.getAndIncrement();

            // add a new subscription & callback
            sub = new cMsgSubscription(subject, type, id, new cMsgCallbackThread(cb, userObj));
            subscriptions.add(sub);

            int[] outGoing = new int[4];
            // first send message id to server
            outGoing[0] = cMsgConstants.msgSubscribeRequest;
            // send unique id of this subscription
            outGoing[1] = id;
            // send length of subscription subject
            outGoing[2] = subject.length();
            // send length of subscription type
            outGoing[3] = type.length();

            // get ready to write
            subBuffer.clear();
            // send ints over together using view buffer
            subBuffer.asIntBuffer().put(outGoing);
            // position original buffer at position of view buffer
            subBuffer.position(16);

            // write strings
            try {
                subBuffer.put(subject.getBytes("US-ASCII"));
                subBuffer.put(type.getBytes("US-ASCII"));
            }
            catch (UnsupportedEncodingException e) {
            }

            try {
                // send buffer over the socket
                subBuffer.flip();
                while (subBuffer.hasRemaining()) {
                    domainChannel.write(subBuffer);
                }
            }
            catch (IOException e) {
                throw new cMsgException(e.getMessage());
            }
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
     * values, but with different callbacks, the callback must be specified so the correct
     * subscription can be removed.
     *
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @throws cMsgException if there are communication problems with the server
     */
    public void unsubscribe(String subject, String type, cMsgCallbackInterface cb)
            throws cMsgException {

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();
        // cannot run this simultaneously with subscribe (get wrong order at server)
        // or itself (use same buffer)
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
                            if (cbThread.callback == cb) {
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

            int[] outGoing = new int[4];
            // first send message id to server
            outGoing[0] = cMsgConstants.msgUnsubscribeRequest;
            // first send message id to server
            outGoing[1] = id;
            // send length of subject
            outGoing[2] = subject.length();
            // send length of type
            outGoing[3] = type.length();

            // get ready to write
            subBuffer.clear();
            // send ints over together using view buffer
            subBuffer.asIntBuffer().put(outGoing);
            // position original buffer at position of view buffer
            subBuffer.position(16);

            // write strings
            try {
                subBuffer.put(subject.getBytes("US-ASCII"));
                subBuffer.put(type.getBytes("US-ASCII"));
            }
            catch (UnsupportedEncodingException e) {
            }

            try {
                // send buffer over the socket
                subBuffer.flip();
                while (subBuffer.hasRemaining()) {
                    domainChannel.write(subBuffer);
                }
            }
            catch (IOException e) {
                throw new cMsgException(e.getMessage());
            }
        }
        finally {
            subscribeLock.unlock();
            notConnectLock.unlock();
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
    private void talkToNameServer(SocketChannel channel) throws IOException, cMsgException {

        // A direct buffer is necessary for nio socket IO.
        ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

        int[] outGoing = new int[11];

        // first send message id to server
        outGoing[0] = cMsgConstants.msgConnectRequest;
        // major version
        outGoing[1] = cMsgConstants.version;
        // minor version
        outGoing[2] = cMsgConstants.minorVersion;
        // send my listening port (as an int) to server
        outGoing[3] = port;
        // send domain type of server I'm expecting to connect to
        outGoing[4] = domain.length();
        // send subdomain type of server I'm expecting to use
        outGoing[5] = subdomain.length();
        // send remainder for use by subdomain plugin
        outGoing[6] = subRemainder.length();
        // send length of my host name to server
        outGoing[7] = host.length();
        // send length of my name to server
        outGoing[8] = name.length();
        // send length of my UDL to server
        outGoing[9] = UDL.length();
        // send length of my description to server
        outGoing[10] = description.length();

        // get ready to write
        buffer.clear();
        // send ints over together using view buffer
        buffer.asIntBuffer().put(outGoing);
        // position original buffer at position of view buffer
        buffer.position(44);

        try {
            // send the type of domain server I'm expecting to connect to
            buffer.put(domain.getBytes("US-ASCII"));

            // send subdomain type of server I'm expecting to use
            buffer.put(subdomain.getBytes("US-ASCII"));

            // send UDL remainder for use by subdomain plugin
            buffer.put(subRemainder.getBytes("US-ASCII"));

            // send my host name to server
            buffer.put(host.getBytes("US-ASCII"));

            // send my name to server
            buffer.put(name.getBytes("US-ASCII"));

            // send UDL for completeness
            buffer.put(UDL.getBytes("US-ASCII"));

            // send description for completeness
            buffer.put(description.getBytes("US-ASCII"));
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

        // if there's an error, read error string then quit
        if (error != cMsgConstants.ok) {
            // read string length
            cMsgUtilities.readSocketBytes(buffer, channel, 4, debug);
            buffer.flip();
            int len = buffer.getInt();

            // read error string
            cMsgUtilities.readSocketBytes(buffer, channel, len, debug);
            buffer.flip();
            byte[] buf = new byte[len];
            // read string
            buffer.get(buf, 0, len);
            String err = new String(buf, 0, len, "US-ASCII");
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  error code = " + error + ", string = " + err);
            }

            throw new cMsgException(cMsgUtilities.printError(error, cMsgConstants.debugNone) +
                                    ": " + err);
        }

        // Since everything's OK, we expect to get:
        //   1) attributes of subdomain handler object
        //   2) domain server host & port

        // Read attributes
        cMsgUtilities.readSocketBytes(buffer, channel, 6, debug);
        buffer.flip();

        hasSend            = (buffer.get() == (byte)1) ? true : false;
        hasSyncSend        = (buffer.get() == (byte)1) ? true : false;
        hasSubscribeAndGet = (buffer.get() == (byte)1) ? true : false;
        hasSendAndGet      = (buffer.get() == (byte)1) ? true : false;
        hasSubscribe       = (buffer.get() == (byte)1) ? true : false;
        hasUnsubscribe     = (buffer.get() == (byte)1) ? true : false;

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

        // read host
        buffer.get(buf, 0, hostLength);
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
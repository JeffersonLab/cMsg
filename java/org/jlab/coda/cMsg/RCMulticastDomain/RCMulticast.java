/*----------------------------------------------------------------------------*
 *  Copyright (c) 2006        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 26-Apr-2006, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.RCMulticastDomain;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.*;

import java.net.*;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.CountDownLatch;
import java.util.*;
import java.io.*;

/**
 * This class implements the runcontrol multicast (rcm) domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class RCMulticast extends cMsgDomainAdapter {

    /** This runcontrol multicast server's UDP listening port obtained from UDL or default value. */
    int udpPort;

    /** The local port used temporarily while multicasting for other rc multicast servers. */
    int localTempPort;

    /** Signal to coordinate the multicasting and waiting for responses. */
    CountDownLatch multicastResponse = new CountDownLatch(1);

    /** The host of the responding server to initial multicast probes of the local subnet. */
    String respondingHost;

    /** Runcontrol's experiment id. */
    String expid;

    /** Only allow response to rc clients if server is properly started. */
    volatile boolean acceptingClients;

    /** Only allow response to rc clients if we have a subscription to pass client info to. */
    volatile boolean hasSubscription;

    /**
      * Collection of all of this server's subscriptions which are
      * {@link cMsgSubscription} objects. This set is synchronized.
      */
     Set<cMsgSubscription> subscriptions;

    /** Socket over which to UDP multicast to and check for other rc multicast servers. */
    private MulticastSocket multicastSocket;

    /** Timeout in milliseconds to wait for server to respond to multicasts. Default is 3 sec. */
    private int multicastTimeout = 2000;

    /** Thread that listens for UDP multicasts to this server and then responds. */
    private rcListeningThread listener;

    /**
     * This lock is for controlling access to the methods of this class.
     * The {@link #connect} and {@link #disconnect} methods of this object cannot be
     * called simultaneously with each other or any other method.
     */
    private final ReentrantReadWriteLock methodLock = new ReentrantReadWriteLock();

    /** Lock for calling {@link #connect} or {@link #disconnect}. */
    private Lock connectLock = methodLock.writeLock();

    /** Lock for calling methods other than {@link #connect} or {@link #disconnect}. */
    private Lock notConnectLock = methodLock.readLock();

    /** Lock to ensure {@link #subscribe(String, String, org.jlab.coda.cMsg.cMsgCallbackInterface, Object)}
     *  and {@link #unsubscribe(org.jlab.coda.cMsg.cMsgSubscriptionHandle)}
     *  calls are sequential. */
    private Lock subscribeLock = new ReentrantLock();

    /**
     * HashMap of all of this server's callback threads (keys) and their associated
     * subscriptions (values). The cMsgCallbackThread object of a new subscription
     * is returned (as an Object) as the unsubscribe handle. When this object is
     * passed as the single argument of an unsubscribe, a quick lookup of the
     * subscription is done using this hashmap.
     */
    private Map<Object, cMsgSubscription> unsubscriptions;



    public RCMulticast() throws cMsgException {
        domain = "rcm";
        subscriptions    = new HashSet<cMsgSubscription>(20);
        unsubscriptions  = Collections.synchronizedMap(new HashMap<Object, cMsgSubscription>(20));

        // store our host's name
        try {
            // send dotted-decimal if possible
            try {
                host = InetAddress.getLocalHost().getHostAddress();
System.out.println("RC Multicast server: setting host to " + host);
            }
            catch (UnknownHostException e) {
                host = InetAddress.getLocalHost().getCanonicalHostName();
System.out.println("RC Multicast server: setting host to " + host);
            }
        }
        catch (UnknownHostException e) {
System.out.println("RC Multicast server: cannot find localhost name");
            throw new cMsgException("cMsg: cannot find host name", e);
        }

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
        setShutdownHandler(new myShutdownHandler(this));
    }


    /**
     * Method to connect to rc clients from this server.
     *
     * @throws cMsgException if there are problems parsing the UDL or
     *                       creating the UDP socket
     */
    public void connect() throws cMsgException {

        parseUDL(UDLremainder);

        // cannot run this simultaneously with any other public method
        connectLock.lock();

        try {
            if (connected) return;

            // Start listening for udp packets.
            listener = new rcListeningThread(this, udpPort);
            listener.start();
            // Wait for indication listener thread is actually running before
            // continuing on. This thread must be running before we look to
            // see what other servers are out there.
            synchronized (listener) {
                if (!listener.isAlive()) {
                    try {
                        listener.wait();
                    }
                    catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }
            }

            // First need to check to see if there is another RCMulticastServer
            // on this port with this EXPID. If so, abandon ship.
            //-------------------------------------------------------
            // multicast on local subnet to find other servers
            //-------------------------------------------------------
            DatagramPacket udpPacket;

            // create byte array for multicast
            ByteArrayOutputStream baos = new ByteArrayOutputStream(1024);
            DataOutputStream out = new DataOutputStream(baos);

            try {
                // Put our TCP listening port, our name, and
                // the EXPID (experiment id string) into byte array.

                // this multicast is from an rc multicast domain server
                out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
                out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
                out.writeInt(cMsgNetworkConstants.magicNumbers[2]);
                out.writeInt(cMsgNetworkConstants.rcDomainMulticastServer);
                // port is irrelevant
                out.writeInt(0);
                out.writeInt(name.length());
                out.writeInt(expid.length());
                try {
                    out.write(name.getBytes("US-ASCII"));
                    out.write(expid.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {
                }
                out.flush();
                out.close();

                // create socket to send multicasts to other RCMulticast servers
                multicastSocket = new MulticastSocket();
                multicastSocket.setTimeToLive(32);
                localTempPort = multicastSocket.getLocalPort();

                InetAddress rcServerMulticastAddress=null;
                try {rcServerMulticastAddress = InetAddress.getByName(cMsgNetworkConstants.rcMulticast); }
                catch (UnknownHostException e) {}

                // create packet to multicast from the byte array
                byte[] buf = baos.toByteArray();
                udpPacket = new DatagramPacket(buf, buf.length, rcServerMulticastAddress, udpPort);
                baos.close();
            }
            catch (IOException e) {
                listener.killThread();
                try { out.close();} catch (IOException e1) {}
                try {baos.close();} catch (IOException e1) {}
                if (multicastSocket != null) multicastSocket.close();

                if (debug >= cMsgConstants.debugError) {
                    System.out.println("I/O Error: " + e);
                }
                throw new cMsgException(e.getMessage());
            }

            // create a thread which will send our multicast
            Multicaster sender = new Multicaster(udpPacket);
            sender.start();

            // wait up to multicast timeout seconds
            boolean response = false;
            try {
                if (multicastResponse.await(multicastTimeout, TimeUnit.MILLISECONDS)) {
//System.out.println("Got a response!");
                    response = true;
                }
            }
            catch (InterruptedException e) { }

            sender.interrupt();

            if (response) {
//System.out.println("Another RC Multicast server is running at port "  + udpPort +
//                   " host " + respondingHost + " with EXPID = " + expid);
                // stop listening thread
                listener.killThread();
                multicastSocket.close();
//                try {Thread.sleep(500);}
//                catch (InterruptedException e) {}

                throw new cMsgException("Another RC Multicast server is running at port " + udpPort +
                                        " host " + respondingHost + " with EXPID = " + expid);
            }
//System.out.println("No other RC Multicast server is running, so start this one up!");
            acceptingClients = true;

            // Releasing the socket AFTER THE ABOVE LINE diminishes the chance that
            // a client on the same host will grab that port and be filtered
            // out as being this same server's multicast.
            multicastSocket.close();

            // reclaim memory
            //multicastResponse = null;  // TODO: Race condition results in this object being used
                                         // TODO: in the rcListeningThread after being set to null.

            connected = true;
        }
        finally {
            connectLock.unlock();
        }

        return;
    }


    /**
     * Method to stop listening for packets from rc clients.
     */
    public void disconnect() {
        // cannot run this simultaneously with connect or send
        connectLock.lock();
        try {
            if (!connected) return;
            connected = false;
            listener.killThread();
        }
        finally {
            connectLock.unlock();
        }
    }


    /**
     * Method to send an abort command to the rc client. Fill in the senderHost
     * with the host and the userInt with the port of the rc client to abort.
     *
     * @param message {@inheritDoc}
     * @throws cMsgException if there are communication problems with the rc client
     */
    public void send(cMsgMessage message) throws cMsgException {

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();

        Socket socket = null;
        DataOutputStream out = null;

        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }
            socket = new Socket(message.getSenderHost(), message.getUserInt());
            // Set tcpNoDelay so packet not delayed
            socket.setTcpNoDelay(true);

            out = new DataOutputStream(new BufferedOutputStream(socket.getOutputStream()));
            out.writeInt(4);
            out.writeInt(cMsgConstants.msgRcAbortConnect);
            out.flush();

            out.close();
            socket.close();
        }
        catch (IOException e) {
            if (out != null) try {out.close();} catch (IOException e1) {}
            if (socket != null) try {socket.close();} catch (IOException e1) {}
            throw new cMsgException(e.getMessage(), e);
        }
        finally {
            notConnectLock.unlock();
        }

    }


    /**
     * Method to parse the Universal Domain Locator (UDL) into its various components.
     * RC Multicast domain UDL is of the form:<p>
     *       cMsg:rcm://&lt;udpPort&gt;/&lt;expid&gt;?multicastTO=&lt;timeout&gt;<p>
     *
     * The intial cMsg:rcm:// is stripped off by the top layer API
     *
     * Remember that for this domain:<p>
     * <ul>
     * <li>udp listening port is optional and defaults to {@link  cMsgNetworkConstants#rcMulticastPort} <p>
     * <li>the experiment id is required If none is given, an exception is thrown<p>
     * <li>the multicast timeout is in seconds and sets the time of sending out multicasts
     *     trying to locate other rc multicast servers already running on its port. Default
     *     is 2 seconds<p>
     * </ul>
     *
     * @param udlRemainder partial UDL to parse
     * @throws cMsgException if udlRemainder is null
     */
    private void parseUDL(String udlRemainder) throws cMsgException {

        if (udlRemainder == null) {
            throw new cMsgException("invalid UDL");
        }

        Pattern pattern = Pattern.compile("(\\d+)?/([^?&]+)(.*)");
        Matcher matcher = pattern.matcher(udlRemainder);

        String udlPort, udlExpid, remainder;

        if (matcher.find()) {
            // port
            udlPort = matcher.group(1);
            // port
            udlExpid = matcher.group(2);
            // remainder
            remainder = matcher.group(3);

            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("\nparseUDL: " +
                        "\n  port  = " + udlPort +
                        "\n  expid = " + udlExpid +
                        "\n  junk  = " + remainder);
            }
        }
        else {
            throw new cMsgException("invalid UDL");
        }

        // get multicast port or use env var or default if it's not given
        if (udlPort != null && udlPort.length() > 0) {
            try {
                udpPort = Integer.parseInt(udlPort);
            }
            catch (NumberFormatException e) {
                if (debug >= cMsgConstants.debugWarn) {
                    System.out.println("parseUDL: non-integer port specified in UDL = " + udlPort);
                }
            }
        }

        // next, try the environmental variable RC_MULTICAST_PORT
        if (udpPort < 1) {
            try {
                String env = System.getenv("RC_MULTICAST_PORT");
                if (env != null) {
                    udpPort = Integer.parseInt(env);
                }
            }
            catch (NumberFormatException ex) {
                System.out.println("parseUDL: bad port number specified in RC_MULTICAST_PORT env variable");
            }
        }

        // use default as last resort
        if (udpPort < 1) {
            udpPort = cMsgNetworkConstants.rcMulticastPort;
            if (debug >= cMsgConstants.debugWarn) {
                System.out.println("parseUDL: using default multicast port = " + udpPort);
            }
        }

        if (udpPort < 1024 || udpPort > 65535) {
            throw new cMsgException("parseUDL: illegal port number");
        }


        // if no expid, return
        if (udlExpid == null) {
            throw new cMsgException("parseUDL: must specify the EXPID");
        }
        expid = udlExpid;
//System.out.println("expid = " + expid);


        // any remaining UDL is ...
        if (remainder == null) {
            UDLremainder = "";
        }
        else {
            UDLremainder = remainder;
        }

        // if no remaining UDL to parse, return
        if (remainder == null) {
            return;
        }

        // now look for ?multicastTO=value& or &multicastTO=value&
        pattern = Pattern.compile("[\\?&]multicastTO=([0-9]+)", Pattern.CASE_INSENSITIVE);
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                multicastTimeout = 1000 * Integer.parseInt(matcher.group(1));
                if (multicastTimeout < 1) {
                    multicastTimeout = 2000;
                }
//System.out.println("multicast TO = " + multicastTimeout);
            }
            catch (NumberFormatException e) {
                // ignore error and keep default
            }
        }

    }


//-----------------------------------------------------------------------------


    /**
     * Method to subscribe to receive messages from rc clients. In this domain,
     * subject and type are ignored and set to the preset values of "s" and "t".
     *
     * @param subject ignored and set to "s"
     * @param type    ignored and set to "t"
     * @param cb      {@inheritDoc}
     * @param userObj {@inheritDoc}
     * @return {@inheritDoc}
     * @throws cMsgException if the callback, subject and/or type is null or blank;
     *                       an identical subscription already exists; if not connected
     *                       to an rc client
     */
    public cMsgSubscriptionHandle subscribe(String subject, String type, cMsgCallbackInterface cb, Object userObj)
            throws cMsgException {

        // Subject and type are ignored in this domain so just
        // set them to some standard values
        subject = "s";
        type    = "t";

        cMsgCallbackThread cbThread = null;
        cMsgSubscription newSub;

        try {
            // cannot run this simultaneously with connect or disconnect
            notConnectLock.lock();
            // cannot run this simultaneously with unsubscribe or itself
            subscribeLock.lock();

            if (!connected) {
                throw new cMsgException("not connected to rc client");
            }

            // Add to callback list if subscription to same subject/type exists.

            // Listening thread may be iterating through subscriptions concurrently
            // and we may change set structure so synchronize.
            synchronized (subscriptions) {

                // for each subscription ...
                for (cMsgSubscription sub : subscriptions) {
                    // If subscription to subject & type exist already...
                    if (sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                        // add to existing set of callbacks
                        cbThread = new cMsgCallbackThread(cb, userObj, domain, subject, type);
                        sub.addCallback(cbThread);
                        unsubscriptions.put(cbThread, sub);
                        return cbThread;
                    }
                }

                // If we're here, the subscription to that subject & type does not exist yet.
                // We need to create and register it.

                // add a new subscription & callback
                cbThread = new cMsgCallbackThread(cb, userObj, domain, subject, type);
                newSub = new cMsgSubscription(subject, type, 0, cbThread);
                unsubscriptions.put(cbThread, newSub);

                // client listening thread may be iterating through subscriptions concurrently
                // and we're changing the set structure
                subscriptions.add(newSub);

                // once we have a subscription we can respond to clients
                hasSubscription = true;
            }
        }
        finally {
            subscribeLock.unlock();
            notConnectLock.unlock();
        }

        return cbThread;
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @param obj {@inheritDoc}
     * @throws cMsgException if there is no connection with rc clients; object is null
     */
    public void unsubscribe(cMsgSubscriptionHandle obj) throws cMsgException {

        // check arg first
        if (obj == null) {
            throw new cMsgException("argument is null");
        }

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();
        // cannot run this simultaneously with subscribe or itself
        subscribeLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to rc client");
            }

            // Delete stuff from hashes & kill threads.
            // If there are still callbacks left,
            // don't unsubscribe for this subject/type.
            synchronized (subscriptions) {
                cMsgSubscription sub = unsubscriptions.remove(obj);

                // if not already unsubscribed. do so
                if (sub != null) {
                    cMsgCallbackThread cbThread = (cMsgCallbackThread) obj;
                    cbThread.dieNow(false);
                    sub.getCallbacks().remove(cbThread);
                    if (sub.numberOfCallbacks() < 1) {
                        subscriptions.remove(sub);
                    }
                }

                // If there are no more subscriptions, we may not respond to clients
                // (since they will stop multicasting/searching for server).
                if (subscriptions.size() < 1) {
                    hasSubscription = false;
                }

            }
        }
        finally {
            subscribeLock.unlock();
            notConnectLock.unlock();
        }

    }


//-----------------------------------------------------------------------------



    /**
     * This class defines a thread to multicast a UDP packet to the
     * RC Multicast server every second.
     */
    class Multicaster extends Thread {

        DatagramPacket packet;

        Multicaster(DatagramPacket udpPacket) {
            packet = udpPacket;
        }


        public void run() {
            try {
                /* A slight delay here will help the main thread (calling connect)
                * to be already waiting for a response from the server when we
                * multicast to the server here (prompting that response). This
                * will help insure no responses will be lost.
                */
                Thread.sleep(100);

                while (true) {

                    try {
//System.out.println("  Send multicast packet to RC Multicast server over each interface");
                        Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();
                        while (enumer.hasMoreElements()) {
                            NetworkInterface ni = enumer.nextElement();

//System.out.println("RC Multicast server: found interface " + ni +
//                           ", up = " + ni.isUp() +
//                           ", loopback = " + ni.isLoopback() +
//                           ", has multicast = " + ni.supportsMulticast());

                            if (ni.isUp() && ni.supportsMulticast() && !ni.isLoopback()) {
//System.out.println("RC Multicast server: sending mcast packet over " + ni.getName());
                                multicastSocket.setNetworkInterface(ni);
                                multicastSocket.send(packet);
                            }
                        }
                    }
                    catch (IOException e) {
                        // probably multicastSocket closed in connect()
                        return;
                    }

                    Thread.sleep(300);
                }
            }
            catch (InterruptedException e) {
                // time to quit
 //System.out.println("Interrupted sender");
            }
        }
    }

}

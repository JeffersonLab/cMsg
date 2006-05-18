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
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.RCBroadcastDomain;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.cMsgDomain.client.cMsgGetHelper;
import org.jlab.coda.cMsg.cMsgDomain.client.cMsgCallbackThread;

import java.net.*;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeoutException;
import java.util.Set;
import java.util.Collections;
import java.util.HashSet;

/**
 * This class implements the runcontrol broadcast (rdb) domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class RCBroadcast extends cMsgDomainAdapter {

    /** This runcontrol broadcast server's UDP listening port obtained from UDL or default value. */
    int broadcastPort;

    /** Runcontrol's experiment id. */
    String expid;

    /** Thread that listens for UDP broad/unicasts to this server and responds. */
    rcListeningThread listener;

    /**
     * This lock is for controlling access to the methods of this class.
     * The {@link #connect} and {@link #disconnect} methods of this object cannot be
     * called simultaneously with each other or any other method.
     */
    private final ReentrantReadWriteLock methodLock = new ReentrantReadWriteLock();

    /** Lock for calling {@link #connect} or {@link #disconnect}. */
    Lock connectLock = methodLock.writeLock();

    /** Lock for calling methods other than {@link #connect} or {@link #disconnect}. */
    Lock notConnectLock = methodLock.readLock();

    /** Lock to ensure {@link #subscribe} and {@link #unsubscribe} calls are sequential. */
    Lock subscribeLock = new ReentrantLock();

    /** Level of debug output for this class. */
    int debug = cMsgConstants.debugNone;

   /**
     * Collection of all of this server's subscriptions which are
     * {@link cMsgSubscription} objects. This set is synchronized.
     */
    Set<cMsgSubscription> subscriptions;

    /**
     * Collection of all of this server's {@link #subscribeAndGet} calls currently in execution.
     * SubscribeAndGets are very similar to subscriptions and can be thought of as
     * one-shot subscriptions. This set is synchronized and contains objects of class
     * {@link org.jlab.coda.cMsg.cMsgDomain.client.cMsgGetHelper}.
     */
    Set<cMsgGetHelper> subscribeAndGets;

    /**
     * HashMap of all of this server's callback threads (keys) and their associated
     * subscriptions (values). The cMsgCallbackThread object of a new subscription
     * is returned (as an Object) as the unsubscribe handle. When this object is
     * passed as the single argument of an unsubscribe, a quick lookup of the
     * subscription is done using this hashmap.
     */
    private ConcurrentHashMap<Object, cMsgSubscription> unsubscriptions;


    public RCBroadcast() throws cMsgException {

        subscriptions    = Collections.synchronizedSet(new HashSet<cMsgSubscription>(20));
        subscribeAndGets = Collections.synchronizedSet(new HashSet<cMsgGetHelper>(20));
        unsubscriptions  = new ConcurrentHashMap<Object, cMsgSubscription>(20);

        // store our host's name
        try {
            host = InetAddress.getLocalHost().getCanonicalHostName();
        }
        catch (UnknownHostException e) {
            throw new cMsgException("cMsg: cannot find host name");
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
     * @throws org.jlab.coda.cMsg.cMsgException if there are problems parsing the UDL or
     *                       creating the UDP socket
     */
    public void connect() throws cMsgException {

        parseUDL(UDLremainder);

        // cannot run this simultaneously with any other public method
        connectLock.lock();

        try {
            if (connected) return;

            // Start listening for udp packets
            listener = new rcListeningThread(this, broadcastPort);
            listener.start();

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

        connected = false;
        listener.killThread();

        connectLock.unlock();
    }


    /**
      * Method to parse the Universal Domain Locator (UDL) into its various components.
      *
      * @param udlRemainder partial UDL to parse
      * @throws cMsgException if udlRemainder is null
      */
    void parseUDL(String udlRemainder) throws cMsgException {

        if (udlRemainder == null) {
            throw new cMsgException("invalid UDL");
        }

        // RC Broadcast domain UDL is of the form:
        //       cMsg:rcb://<udpPort>?expid=<expid>
        //
        // The intial cMsg:rcb:// is stripped off by the top layer API
        //
        // Remember that for this domain:
        // 1) udp listening port is optional and defaults to 6543 (cMsgNetworkConstants.rcBroadcastPort)
        // 2) the experiment id is given by the optional parameter expid. If none is
        //    given, the environmental variable EXPID is used. if that is not defined,
        //    an exception is thrown

        Pattern pattern = Pattern.compile("(\\d+)?/?(.*)");
        Matcher matcher = pattern.matcher(udlRemainder);

        String udlPort = null, remainder = null;

        if (matcher.find()) {
            // port
            udlPort = matcher.group(1);
            // remainder
            remainder = matcher.group(2);

            if (debug >= cMsgConstants.debugInfo) {
            System.out.println("\nparseUDL: " +
                               "\n  port = " + udlPort +
                               "\n  junk = " + remainder);
            }
        }
        else {
            throw new cMsgException("invalid UDL");
        }

        // get broadcast port or use default if it's not given
        if (udlPort != null && udlPort.length() > 0) {
            try {
                broadcastPort = Integer.parseInt(udlPort);
            }
            catch (NumberFormatException e) {
                broadcastPort = cMsgNetworkConstants.rcBroadcastPort;
                if (debug >= cMsgConstants.debugWarn) {
                    System.out.println("parseUDL: non-integer port, using broadcast port = " + broadcastPort);
                }
            }
        }
        else {
            broadcastPort = cMsgNetworkConstants.rcBroadcastPort;
            if (debug >= cMsgConstants.debugWarn) {
                System.out.println("parseUDL: using broadcast port = " + broadcastPort);
            }
        }

        if (broadcastPort < 1024 || broadcastPort > 65535) {
            throw new cMsgException("parseUDL: illegal port number");
        }

        // any remaining UDL is ...
        if (remainder == null) {
            UDLremainder = "";
        }
        else {
            UDLremainder = remainder;
        }

        // Find our experiment id,
        //   in udl if it exists ...
        if (remainder != null) {
            // look for ?key=value& or &key=value& pairs
            Pattern pat = Pattern.compile("(?:[&\\?](\\w+)=(\\w+)(?=&))");
            Matcher mat = pat.matcher(remainder + "&");

            loop: while (mat.find()) {
                for (int i = 0; i < mat.groupCount() + 1; i++) {
                    // if key = expid ...
                    if (mat.group(i).equalsIgnoreCase("expid")) {
                        // expid must be value
                        expid = mat.group(i + 1);
                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("  expid = " + expid);
                        }
                        break loop;
                    }
                }
            }
        }

        //   try looking in environmental variable EXPID ...
        if (expid == null) {
            String exp = System.getenv("EXPID");
            if (exp != null) {
                expid = exp;
            }
            // no value specified for expid so throw exception
            else {
                throw new cMsgException("no value for EXPID given");
            }
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to subscribe to receive messages from rc clients. In this domain,
     * subject and type are ignored and set to the preset values of "s" and "t".
     * The combination of arguments must be unique. In other words, only 1 subscription is
     * allowed for a given set of callback, and userObj.
     *
     * @param subject ignored and set to "s"
     * @param type ignored and set to "t"
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @param userObj any user-supplied object to be given to the callback method as an argument
     * @return handle object to be used for unsubscribing
     * @throws cMsgException if the callback, subject and/or type is null or blank;
     *                       an identical subscription already exists; if not connected
     *                       to an rc client
     */
    public Object subscribe(String subject, String type, cMsgCallbackInterface cb, Object userObj)
            throws cMsgException {

        // Subject and type are ignored in this domain so just
        // set them to some standard values
        subject = "s";
        type    = "t";

        cMsgCallbackThread cbThread = null;
        cMsgSubscription newSub = null;

        try {
            // cannot run this simultaneously with connect or disconnect
            notConnectLock.lock();
            // cannot run this simultaneously with unsubscribe or itself
            subscribeLock.lock();

            if (!connected) {
                throw new cMsgException("not connected to rc client");
            }

            // Add to callback list if subscription to same subject/type exists.

            // Listening thread may be interating thru subscriptions concurrently
            // and we may change set structure so synchronize.
            synchronized (subscriptions) {

                // for each subscription ...
                for (cMsgSubscription sub : subscriptions) {
                    // If subscription to subject & type exist already...
                    if (sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                        // Only add another callback if the callback/userObj
                        // combination does NOT already exist. In other words,
                        // a callback/argument pair must be unique for a single
                        // subscription. Otherwise it is impossible to unsubscribe.

                        // for each callback listed ...
                        for (cMsgCallbackThread cbt : sub.getCallbacks()) {
                            // if callback and user arg already exist, reject the subscription
                            if ((cbt.getCallback() == cb) && (cbt.getArg() == userObj)) {
                                throw new cMsgException("subscription already exists");
                            }
                        }

                        // add to existing set of callbacks
                        cbThread = new cMsgCallbackThread(cb, userObj);
                        sub.addCallback(cbThread);
                        unsubscriptions.put(cbThread, sub);
                        return (Object) cbThread;
                    }
                }

                // If we're here, the subscription to that subject & type does not exist yet.
                // We need to create and register it.

                // add a new subscription & callback
                cbThread = new cMsgCallbackThread(cb, userObj);
                newSub = new cMsgSubscription(subject, type, 0, cbThread);
                unsubscriptions.put(cbThread, newSub);

                // client listening thread may be interating thru subscriptions concurrently
                // and we're changing the set structure
                subscriptions.add(newSub);
            }
        }
        finally {
            subscribeLock.unlock();
            notConnectLock.unlock();
        }

        return (Object) cbThread;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to unsubscribe a previous subscription.
     *
     * @param obj the object "handle" returned from a subscribe call
     * @throws cMsgException if there is no connection with rc clients; object is null
     */
    public void unsubscribe(Object obj)
            throws cMsgException {

        // check arg first
        if (obj == null) {
            throw new cMsgException("argument is null");
        }

        cMsgSubscription sub = unsubscriptions.remove(obj);
        // already unsubscribed
        if (sub == null) {
            return;
        }
        cMsgCallbackThread cbThread = (cMsgCallbackThread) obj;

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
            cbThread.dieNow();
            synchronized (subscriptions) {
                sub.getCallbacks().remove(cbThread);
                if (sub.numberOfCallbacks() < 1) {
                    subscriptions.remove(sub);
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
     * This method is like a one-time subscribe. The rc server grabs an incoming
     * message and sends that to the caller. In this domain, subject and type are
     * ignored and set to the preset values of "s" and "t".
     *
     * @param subject ignored and set to "s"
     * @param type ignored and set to "t"
     * @param timeout time in milliseconds to wait for a message
     * @return response message
     * @throws cMsgException if there are communication problems with rc client;
     *                       subject and/or type is null or blank
     * @throws java.util.concurrent.TimeoutException if timeout occurs
     */
    public cMsgMessage subscribeAndGet(String subject, String type, int timeout)
            throws cMsgException, TimeoutException {

        // Subject and type are ignored in this domain so just
        // set them to some standard values
        subject = "s";
        type    = "t";

        cMsgGetHelper helper = null;

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to rc client");
            }

            // create cMsgGetHelper object (not callback thread object)
            helper = new cMsgGetHelper(subject, type);

            // keep track of get calls
            subscribeAndGets.add(helper);
        }
        // release lock 'cause we can't block connect/disconnect forever
        finally {
            notConnectLock.unlock();
        }

        // WAIT for the msg-receiving thread to wake us up
        try {
            synchronized (helper) {
                if (timeout > 0) {
                    helper.wait(timeout);
                }
                else {
                    helper.wait();
                }
            }
        }
        catch (InterruptedException e) {
        }

        // Check the message stored for us in helper.
        if (helper.isTimedOut()) {
            // remove the get
            subscribeAndGets.remove(helper);
            throw new TimeoutException();
        }

        // If msg is received, server has removed subscription from his records.
        // Client listening thread has also removed subscription from client's
        // records (subscribeAndGets HashSet).
        return helper.getMessage();
    }


}
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

package org.jlab.coda.cMsg.subdomains;

import org.jlab.coda.cMsg.cMsgDomain.cMsgSubscription;
import org.jlab.coda.cMsg.cMsgDomain.cMsgNotifier;
import org.jlab.coda.cMsg.*;

import java.util.*;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.ReentrantLock;
import java.io.IOException;

/**
 * Class to handles all client cMsg requests.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsg extends cMsgSubdomainAdapter {
    /** Used to create a unique id number associated with a specific message. */
    static private AtomicInteger sysMsgId = new AtomicInteger();

    /** HashMap to store server clients. Name is key and cMsgClientInfo is value. */
    static private ConcurrentHashMap<String,cMsgClientInfo> servers =
            new ConcurrentHashMap<String,cMsgClientInfo>(100);

    /** HashMap to store clients. Name is key and cMsgClientInfo is value. */
    static private ConcurrentHashMap<String,cMsgClientInfo> clients =
            new ConcurrentHashMap<String,cMsgClientInfo>(100);

    /**
     * HashMap to store specific "get" in progress. sysMsgId of get msg is key,
     * and client name is value.
     */
    static private ConcurrentHashMap<Integer,cMsgClientInfo> specificGets =
            new ConcurrentHashMap<Integer,cMsgClientInfo>(100);

    /**
     * Convenience class for storing data in a hashmap used for removing
     * sendAndGets which have timed out.
     */
    static private class DeleteGetInfo {
        String name;
        int senderToken;
        int sysMsgId;
        DeleteGetInfo(String name, int senderToken, int sysMsgId) {
            this.name = name;
            this.senderToken = senderToken;
            this.sysMsgId = sysMsgId;
        }
    }

    /**
     * HashMap to store mappings of local client's senderTokens to static sysMsgIds.
     * This allows the cancellation of a "sendAndGet" using a senderToken (which the
     * client knows) which can then be used to look up the sysMsgId and cancel the get.
     */
    static private ConcurrentHashMap<Integer,DeleteGetInfo> deleteGets =
            new ConcurrentHashMap<Integer,DeleteGetInfo>(100);

    /** Set of all subscriptions (including the subscribeAndGets) of regular & bridge clients. */
    static private HashSet<cMsgSubscription> subscriptions =
            new HashSet<cMsgSubscription>(100);

    /** This lock is used in global registrations for regular clients and server clients. */
    static private final ReentrantLock registrationLock = new ReentrantLock();

    /** Lock to ensure all access to {@link #subscriptions} is sequential. */
    static private final ReentrantLock subscribeLock = new ReentrantLock();

    /** Set of client info objects to send the message to. */
    private HashSet<cMsgClientInfo> sendToSet = new HashSet<cMsgClientInfo>(100);

    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugError;

    /** Remainder of UDL client used to connect to domain server. */
    private String UDLRemainder;

    /** Namespace this client sends messages to. */
    private String namespace;

    /** Name of client using this subdomain handler. */
    private String name;

    /** Object containing informatoin about the client this object corresponds to. */
    private cMsgClientInfo myInfo;



    /** No-arg constructor. */
    public cMsg() {}


    /**
     * Method to notify other objects (say, bridge to another server) that a new
     * subscription has been made, and also return the new subscription. This method
     * only notifies if a regular client makes a new subscription -- not another server.
     */
    public cMsgSubscription waitForNewSubscription() {
        cMsgSubscription sub = null;

        return sub;
    }


    /**
     *
     * @param delay time in milliseconds to wait for the lock before timing out
     * @return
     */
    static public boolean registrationLock(int delay) {
        try {
            return registrationLock.tryLock(delay, TimeUnit.MILLISECONDS);
        }
        catch (InterruptedException e) {
            return false;
        }
    }


    static public void registrationUnlock() {
        registrationLock.unlock();
    }

    public String[] getClientNames() {
//System.out.println("subdh: in getClientNames, size = " + clients.size());
        String[] s = new String[clients.size()];
        int i=0;
        for (String q : clients.keySet()) {
//System.out.println("subdh: getClientNames: add string = " + q);
            s[i++] = q;
        }
//System.out.println("subdh: return");
        return s;
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
    public boolean hasSyncSend() {return true;};


    /**
     * Method to tell if the "subscribeAndGet" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSubscribeAndGetRequest}
     * method.
     *
     * @return true if subscribeAndGet implemented in {@link #handleSubscribeAndGetRequest}
     */
    public boolean hasSubscribeAndGet() {return true;}


    /**
     * Method to tell if the "sendAndGet" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSendAndGetRequest}
     * method.
     *
     * @return true if sendAndGet implemented in {@link #handleSendAndGetRequest}
     */
    public boolean hasSendAndGet() {return true;}


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
     * Method to tell if the "shutdown" cMsg API function is implemented
     * by this interface implementation in the {@link #handleShutdownRequest}
     * method.
     *
     * @return true if shutdown implemented in {@link #handleShutdownRequest}
     */
    public boolean hasShutdown() {
        return true;
    }


    /**
     * Method to give the subdomain handler the appropriate part
     * of the UDL the client used to talk to the domain server.
     * In the cMsg subdomain of the cMsg domain, each client sends messages to a namespace.
     * If no namespace is specified, the namespace is "/defaultNamespace".
     * The namespace is specified in the client supplied UDL as follows:
     *     cMsg:cMsg://<host>:<port>/cMsg/<namespace>
     * A single beginning forward slash is enforced in a namespace.
     * A question mark will terminate but will not be included in the namespace.
     * All trailing forward slashes will be removed.
     *
     * @param UDLRemainder last part of the UDL appropriate to the subdomain handler
     * @throws cMsgException
     */
    public void setUDLRemainder(String UDLRemainder) throws cMsgException {
        this.UDLRemainder = UDLRemainder;

        // if no namespace specified, set to default
        if (UDLRemainder == null || UDLRemainder.length() < 1) {
            namespace = "/defaultNamespace";
            if (debug >= cMsgConstants.debugInfo) {
               System.out.println("setUDLRemainder:  namespace = " + namespace);
            }
            return;
        }

        // parse UDLRemainder to find the namespace this client is in
        Pattern pattern = Pattern.compile("([\\w/]+)[?]*.*");
        Matcher matcher = pattern.matcher(UDLRemainder);

        String s = null;

        if (matcher.lookingAt()) {
            s = matcher.group(1);
        }
        else {
            throw new cMsgException("invalid namespace");
        }

        if (s == null) {
            throw new cMsgException("invalid namespace");
        }

        // strip off all except one beginning slash and all ending slashes
        while (s.startsWith("/")) {
            s = s.substring(1);
        }
        while (s.endsWith("/")) {
            s = s.substring(0, s.length()-1);
        }
        namespace = "/" + s;

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("setUDLRemainder:  namespace = " + namespace);
        }
    }


    /**
     * Method to give the subdomain handler on object able to deliver messages
     * to the client. This copy of the deliverer is not used in the cMsg subdomain --
     * only the one in the cMsgClientInfo object.
     *
     * @param deliverer object able to deliver messages to the client
     */
    public void setMessageDeliverer(cMsgDeliverMessageInterface deliverer) {
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
     * @param info information about client
     * @throws cMsgException if client already exists or argument is null
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {
        // Need meaningful client information
        if (info == null) {
            cMsgException e = new cMsgException("argument is null");
            e.setReturnCode(cMsgConstants.errorBadArgument);
            throw e;
        }

        String clientName = info.getName();
//System.out.println("subdh: put " + clientName + " into clients hashmap");
        cMsgClientInfo ci = clients.putIfAbsent(clientName, info);
        // Check to see if name was taken already.
        // If ci is not null, this key already existed.
        if (ci != null) {
            cMsgException e = new cMsgException("client already exists");
            e.setReturnCode(cMsgConstants.errorAlreadyExists);
            throw e;
        }

        this.name   = clientName;
//System.out.println("subdh: register client with info = " + info);
        this.myInfo = info;

        // this client is registered in this namespace
        info.setNamespace(namespace);
    }


    /**
     * Method to register cMsg domain server as client.
     * Name is of the form "nameServerHost:nameServerPort".
     *
     * @param info information about client
     */
    public void registerServer(cMsgClientInfo info) {
        String clientName = info.getName();

        cMsgClientInfo ci = servers.putIfAbsent(clientName, info);

        this.name   = clientName;
        this.myInfo = info;

        // this client is registered in this namespace
        info.setNamespace(namespace);
    }


    /**
     * This method handles a message sent by a local bridge object's callback.
     * The message's subject and type are matched against all clients' subscriptions.
     * For each client, the message is compared to each of its subscriptions until
     * a match is found. At that point, the message is sent to that client.
     * The client is responsible for finding all the matching gets
     * and subscribes and distributing the message among them as necessary.
     * No messages are sent to server clients to avoid infinite loops.
     *
     * This method is synchronized because the use of infoList is not
     * thread-safe otherwise. Multiple threads in the domain server can be calling
     * this object's methods simultaneously.
     *
     * @param message message from sender
     * @param namespace namespace of original message sender
     * @throws cMsgException if a channel to the client is closed, cannot be created,
     *                          or socket properties cannot be set
     */
    synchronized public void bridgeSend(cMsgMessage message, String namespace) throws cMsgException {
//System.out.println("IN bridgeSend!!!");
        if (message == null) return;

        cMsgClientInfo info;
        sendToSet.clear();

        // If message is sent in response to a specific get ...
        if (message.isGetResponse()) {
            int id = message.getSysMsgId();
            // Recall the client who originally sent the get request
            // and remove the item from the hashtable
            info = specificGets.remove(id);
            deleteGets.remove(id);

            // If this is the first response to a sendAndGet ...
            if (info != null) {
                try {
//System.out.println(" handle send msg for send&get to " + info.getName());
                    if (message.isNullGetResponse()) {
                        info.getDeliverer().deliverMessage(message, cMsgConstants.msgGetResponseIsNull);
                    }
                    else {
                        info.getDeliverer().deliverMessage(message, cMsgConstants.msgGetResponse);
                    }
                }
                catch (IOException e) {
                    return;
                }
                return;
            }
            // If this is an Nth response to the sendAndGet ...
            else if (message.isNullGetResponse()) {
                // if the message is a null response, just dump it
                return;
            }
            // If we're here, it's a normal message.
            // Send it like any other to all subscribers.
        }


        // ONLY REGULAR CLIENTS SEND MESSAGES, BRIDGES ONLY SUBSCRIBE

        // Scan through subscriptions of regular clients. Don't send to bridges.
        // Lock for subscriptions
        subscribeLock.lock();
        try {
            cMsgSubscription sub;
            Iterator it = subscriptions.iterator();

            while (it.hasNext()) {
                sub = (cMsgSubscription)it.next();

                // send only to matching namespace
                if (!namespace.equalsIgnoreCase(sub.getNamespace())) {
                    continue;
                }

                // subscription subject and type must match msg's
                if (!cMsgMessageMatcher.matches(sub.getSubjectRegexp(), message.getSubject(), false) ||
                    !cMsgMessageMatcher.matches(sub.getTypeRegexp(), message.getType(), false)) {
                    continue;
                }

                // Put all subscribers and subscribeAndGetters of this
                // subscription in the set of clients to send to.
                sendToSet.addAll(sub.getClientSubscribers());
                sendToSet.addAll(sub.getClientSubAndGetters().keySet());

                // Clear subAndGetter lists as they're only a 1-shot deal.
                // Note that all subscribeAndGets are done by local clients.
                // Servers implement sub&Get with subscribes.
                sub.getClientSubAndGetters().clear();

                // fire off all notifiers for this subscription
                for (cMsgNotifier notifier : sub.getNotifiers()) {
//System.out.println("subdh: bridgeSend: Firing notifier");
                    notifier.latch.countDown();
                }
                sub.clearNotifiers();

                // delete sub if no more subscribers
                if (sub.numberOfSubscribers() < 1) {
//System.out.println("subdh: bridgeSend: remove subscription");
                    it.remove();
                }

            }

        }
        finally {
            subscribeLock.unlock();
        }

        // Once we have the subscription/get, msg, and client info,
        // no more need for sychronization

        for (cMsgClientInfo client : sendToSet) {

            // Deliver this msg to this client.
            try {
                client.getDeliverer().deliverMessage(message, cMsgConstants.msgSubscribeResponse);
            }
            catch (IOException e) {
                continue;
            }
        }

    }


    /**
     * This method handles a message sent by the domain client. The message's subject and type
     * are matched against all clients' subscriptions. For each client, the message is
     * compared to each of its subscriptions until a match is found. At that point, the message
     * is sent to that client. The client is responsible for finding all the matching gets
     * and subscribes and distributing the message among them as necessary.
     *
     * This method is synchronized because the use of infoList is not
     * thread-safe otherwise. Multiple threads in the domain server can be calling
     * this object's methods simultaneously.
     *
     * @param message message from sender
     * @throws cMsgException if a channel to the client is closed, cannot be created,
     *                          or socket properties cannot be set
     */
    synchronized public void handleSendRequest(cMsgMessageFull message) throws cMsgException {
//System.out.println("\nhandleSendRequest(subdh): REGULAR SEND\n");
        if (message == null) return;

        cMsgClientInfo info;
        sendToSet.clear();

        // If message is sent in response to a specific get ...
        if (message.isGetResponse()) {
            int id = message.getSysMsgId();
            // Recall the client who originally sent the get request
            // and remove the item from the hashtable
            info = specificGets.remove(id);
            deleteGets.remove(id);

            // If this is the first response to a sendAndGet ...
            if (info != null) {
                try {
//System.out.println(" handle send msg for send&get to " + info.getName());
                    if (message.isNullGetResponse()) {
                        info.getDeliverer().deliverMessage(message, cMsgConstants.msgGetResponseIsNull);
                    }
                    else {
                        info.getDeliverer().deliverMessage(message, cMsgConstants.msgGetResponse);
                    }
                }
                catch (IOException e) {
                    return;
                }
                return;
            }
            // If this is an Nth response to the sendAndGet ...
            else if (message.isNullGetResponse()) {
                // if the message is a null response, just dump it
                return;
            }
            // If we're here, it's a normal message.
            // Send it like any other to all subscribers.
        }


        // ONLY REGULAR CLIENTS SEND MESSAGES, BRIDGES ONLY SUBSCRIBE

        // Scan through all subscriptions.
        // Lock for subscriptions
        subscribeLock.lock();
        try {
            cMsgSubscription sub;
            Iterator it = subscriptions.iterator();

            while (it.hasNext()) {
                sub = (cMsgSubscription)it.next();

                // send only to matching namespace
//System.out.println("handleSendRequest(subdh): compare sub ns = " + sub.getNamespace() +
//                   " to my ns = " + namespace);
                if (!namespace.equalsIgnoreCase(sub.getNamespace())) {
                    continue;
                }

                // subscription subject and type must match msg's
                if (!cMsgMessageMatcher.matches(sub.getSubjectRegexp(), message.getSubject(), false) ||
                    !cMsgMessageMatcher.matches(sub.getTypeRegexp(), message.getType(), false)) {
                    continue;
                }

                // Put all subscribers and subscribeAndGetters of this
                // subscription in the set of clients to send to.
//System.out.println("handleSendRequest(subdh): add client to send list");
                sendToSet.addAll(sub.getAllSubscribers());
                sendToSet.addAll(sub.getClientSubAndGetters().keySet());

//System.out.println("  A# of subscribers = " + sub.getAllSubscribers().size());
//System.out.println("  A# of sub&Getters = " + sub.getClientSubAndGetters().size());
                //Iterator it1 = sub.getClientSubAndGetters().keySet().iterator();
                //cMsgClientInfo info1 =  (cMsgClientInfo)it1.next();
                //System.out.println("  subs count of sub&Getters = " + sub.getClientSubAndGetters().get(info1));
                // clear subAndGetter list as it's only a 1-shot deal
                sub.getClientSubAndGetters().clear();
//System.out.println("  B# of subscribers = " + sub.getAllSubscribers().size());
//System.out.println("  B# of sub&Getters = " + sub.getClientSubAndGetters().size());
                //System.out.println("  subs count of sub&Getters = " + sub.getClientSubAndGetters().get(info1));

                // fire off all notifiers for this subscription
                for (cMsgNotifier notifier : sub.getNotifiers()) {
//System.out.println("handleSendRequest(subdh): Firing notifier");
                    notifier.latch.countDown();
                }
                sub.clearNotifiers();

                // delete sub if no more subscribers
                if (sub.numberOfSubscribers() < 1) {
//System.out.println("handleSendRequest(subdh): remove subscription");
                    it.remove();
                }

            }

        }
        finally {
            subscribeLock.unlock();
        }

        // Once we have the subscription/get, msg, and client info,
        // no more need for sychronization

        for (cMsgClientInfo client : sendToSet) {

            // Deliver this msg to this client.
            try {
//System.out.println("handleSendRequest(subdh): send msg to client " + client.getName());
                client.getDeliverer().deliverMessage(message, cMsgConstants.msgSubscribeResponse);
            }
            catch (IOException e) {
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
     * @throws cMsgException if a channel to the client is closed, cannot be created,
     *                          or socket properties cannot be set
     */
    public int handleSyncSendRequest(cMsgMessageFull message) throws cMsgException {
        handleSendRequest(message);
        return 0;
    }


    /**
     * Method to handle subscribe request sent by another cMsg server.
     *
     * @param subject message subject subscribed to
     * @param type    message type subscribed to
     * @param namespace namespace message resides in
     * @throws cMsgException
     */
    public void handleServerSubscribeRequest(String subject, String type, String namespace)
            throws cMsgException {

        boolean subscriptionExists = false;
        cMsgSubscription sub = null;

        subscribeLock.lock();

        try {

            Iterator it = subscriptions.iterator();

            while (it.hasNext()) {
                sub = (cMsgSubscription) it.next();
                if (sub.getNamespace().equals(namespace) &&
                        sub.getSubject().equals(subject) &&
                        sub.getType().equals(type)) {

                    if (sub.containsSubscriber(myInfo)) {
                        throw new cMsgException("handleServerSubscribeRequest: subscription already exists for subject = " +
                                                subject + " and type = " + type);
                    }
                    // found existing subscription to subject and type so add this client to its list
                    subscriptionExists = true;
                    break;
                }
            }

            // add this client to an exiting subscription
            if (subscriptionExists) {
//System.out.println("subdh handleServerSubscribeRequest ADD sub: subject = " + subject + ", type = " + type + ", ns = " + namespace);
                sub.addSubscriber(myInfo);
            }
            // or else create a new subscription
            else {
                sub = new cMsgSubscription(subject, type, namespace);
//System.out.println("subdh handleServerSubscribeRequest NEW sub: subject = " + subject + ", type = " + type + ", ns = " + namespace);
                sub.addSubscriber(myInfo);
                subscriptions.add(sub);
            }
        }
        finally {
            // Lock for subscriptions
            subscribeLock.unlock();
        }
    }


    /**
     * Method to handle subscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     *
     * @param subject  message subject to subscribe to
     * @param type     message type to subscribe to
     * @param id       message id refering to these specific subject and type values
     * @throws cMsgException if a subscription for this subject and type already exists
     */
    public void handleSubscribeRequest(String subject, String type, int id)
            throws cMsgException {

        boolean subscriptionExists = false;
        cMsgSubscription sub = null;

        subscribeLock.lock();

        try {

            Iterator it = subscriptions.iterator();

            while (it.hasNext()) {
                sub = (cMsgSubscription) it.next();
                if (sub.getNamespace().equals(namespace) &&
                        sub.getSubject().equals(subject) &&
                        sub.getType().equals(type)) {

                    if (sub.containsSubscriber(myInfo)) {
                        throw new cMsgException("handleSubscribeRequest: subscription already exists for subject = " +
                                                subject + " and type = " + type);
                    }
                    // found existing subscription to subject and type so add this client to its list
                    subscriptionExists = true;
                    break;
                }
            }

            // add this client to an exiting subscription
            if (subscriptionExists) {
                sub.addSubscriber(myInfo);
                sub.addClientSubscriber(myInfo);
            }
            // or else create a new subscription
            else {
                sub = new cMsgSubscription(subject, type, namespace);
//System.out.println("subdh handleSubscribeRequest: subject = " + subject + ", type = " + type + ", ns = " + namespace);
                sub.addSubscriber(myInfo);
                sub.addClientSubscriber(myInfo);
                subscriptions.add(sub);
            }
        }
        finally {
            // Lock for subscriptions
            subscribeLock.unlock();
        }
    }


    /**
     * Method to handle sunsubscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     *
     * @param subject  message subject to subscribe to
     * @param type     message type to subscribe to
     */
     public void handleUnsubscribeRequest(String subject, String type, int id) {
        cMsgSubscription sub = null;

        subscribeLock.lock();

        try {

            Iterator it = subscriptions.iterator();

            while (it.hasNext()) {
                sub = (cMsgSubscription) it.next();
                if (sub.getNamespace().equals(namespace) &&
                        sub.getSubject().equals(subject) &&
                        sub.getType().equals(type)) {

                    sub.removeSubscriber(myInfo);
                    sub.removeClientSubscriber(myInfo);
                    break;
                }
            }

            // get rid of this subscription if no more subscribers left
            if (sub.numberOfSubscribers() < 1) {
                subscriptions.remove(sub);
            }
        }
        finally {
            // Lock for subscriptions
            subscribeLock.unlock();
        }
    }



    /**
     * Method to handle sunsubscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     *
     * @param subject  message subject to subscribe to
     * @param type     message type to subscribe to
     */
     public void handleServerUnsubscribeRequest(String subject, String type, String namespace) {
        cMsgSubscription sub = null;

        subscribeLock.lock();

        try {

            Iterator it = subscriptions.iterator();

            while (it.hasNext()) {
                sub = (cMsgSubscription) it.next();
                if (sub.getNamespace().equals(namespace) &&
                        sub.getSubject().equals(subject) &&
                        sub.getType().equals(type)) {
//System.out.println("SERVER UNSUBSCRIBE");
                    sub.removeSubscriber(myInfo);
                    break;
                }
            }

            // get rid of this subscription if no more subscribers left
            if (sub.numberOfSubscribers() < 1) {
                subscriptions.remove(sub);
            }
        }
        finally {
            // Lock for subscriptions
            subscribeLock.unlock();
        }
    }



    /**
     * Method to synchronously get a single message from a receiver by sending out a
     * message to be responded to.
     *
     * @param message message requesting what sort of message to get
     * @throws cMsgException if a channel to the client is closed, cannot be created,
     *                          or socket properties cannot be set
     */
    public void handleSendAndGetRequest(cMsgMessageFull message) throws cMsgException {
        // Create a unique number
        int id = sysMsgId.getAndIncrement();
        // Put that into the message
        message.setSysMsgId(id);
        // Store this client's info with the number as the key so any response to it
        // can retrieve this associated client
        specificGets.put(id, myInfo);
        // Allow for cancelation of this sendAndGet
        DeleteGetInfo dgi = new DeleteGetInfo(name, message.getSenderToken(), id);
        deleteGets.put(id, dgi);

        /*
        if (deleteGets.size() % 500 == 0) {
            System.out.println("sdHandler: deleteGets size = " + deleteGets.size());
        }
        if (specificGets.size() % 500 == 0) {
            System.out.println("sdHandler: specificGets = " + specificGets.size());
        }
        */
        
        // Now send this message on its way to any receivers out there.
        // SenderToken and sysMsgId get sent back by response. The sysMsgId
        // tells us which client to send to and the senderToken tells the
        // client which "get" to wakeup.
        handleSendRequest(message);
    }



    /**
     * Method to handle remove sendAndGet request sent by domain client
     * (hidden from user).
     *
     * @param id message id refering to these specific subject and type values
     */
    public void handleUnSendAndGetRequest(int id) {
        int sysId = -1;
        DeleteGetInfo dgi;

        // Scan through list of name/senderToken value pairs. (This combo is unique.)
        // Find the one that matches ours and get its associated sysMsgId number.
        // Use that number as a key to remove the specificGet (sendAndGet).
        for (Iterator i=deleteGets.values().iterator(); i.hasNext(); ) {
            dgi = (DeleteGetInfo) i.next();
            if (dgi.name.equals(name) && dgi.senderToken == id) {
                sysId = dgi.sysMsgId;
                i.remove();
                break;
            }
        }

        // If it has already been removed, forget about it
        if (sysId < 0) {
            return;
        }

        specificGets.remove(sysId);
    }


    // BUG BUG, registering sub & setting notifier must be "simultaneous" - no message sent
    // during that interval (synchronizing works but may be too restrictive
    /**
     * Method to synchronously get a single message from the local server for a one-time
     * subscription of a subject and type by an outside server.
     *
     * @param subject message subject subscribed to
     * @param type    message type subscribed to
     * @param notifier object which allows the subdomain handler to notify other objects
     *                 that a message matching this subscription has been sent (by a local
     *                 client)
     * @throws cMsgException
     */
    public void handleServerSubscribeAndGetRequest(String subject, String type,
                                                   cMsgNotifier notifier)
                                                                throws cMsgException {

        boolean subscriptionExists = false;
        cMsgSubscription sub = null;

        subscribeLock.lock();

        try {

            Iterator it = subscriptions.iterator();
//System.out.println("In handleServerSub&GetRequest:");
            while (it.hasNext()) {
                sub = (cMsgSubscription) it.next();
                if (sub.getNamespace().equals(namespace) &&
                        sub.getSubject().equals(subject) &&
                        sub.getType().equals(type)) {

                    // found existing subscription to subject and type so add this client to its list
                    subscriptionExists = true;
                    break;
                }
            }

            // add this client to an exiting subscription
            if (subscriptionExists) {
                sub.addSubAndGetter(myInfo);
            }
            // or else create a new subscription
            else {
                sub = new cMsgSubscription(subject, type, namespace);
                sub.addSubAndGetter(myInfo);
                subscriptions.add(sub);
            }
//System.out.println("  add notifier");
            // Need to unsubscribe from remote servers if sub&Get is cancelled.
            // This object notifies of the need to do so.
            sub.addNotifier(notifier);
        }
        finally {
            // Lock for subscriptions
            subscribeLock.unlock();
        }
    }


    /**
     * Method to synchronously get a single message from the server for a one-time
     * subscription of a subject and type.
     *
     * @param subject message subject subscribed to
     * @param type    message type subscribed to
     * @param id      message id refering to these specific subject and type values
     */
    public void handleSubscribeAndGetRequest(String subject, String type, int id) {
        boolean subscriptionExists = false;
        cMsgSubscription sub = null;

        // Lock for subscriptions
        subscribeLock.lock();

        try {
            Iterator it = subscriptions.iterator();

//System.out.println("In sub&GetRequest:");
            while (it.hasNext()) {
                sub = (cMsgSubscription) it.next();
                if (sub.getNamespace().equals(namespace) &&
                        sub.getSubject().equals(subject) &&
                        sub.getType().equals(type)) {

                    // found existing subscription to subject and type so add this client to its list
                    subscriptionExists = true;
                    break;
                }
            }

            // add this client to an exiting subscription
            if (subscriptionExists) {
//System.out.println("    add sub&Gettter");
                sub.addSubAndGetter(myInfo);
            }
            // or else create a new subscription
            else {
//System.out.println("    create subscription");
                sub = new cMsgSubscription(subject, type, namespace);
                sub.addSubAndGetter(myInfo);
                subscriptions.add(sub);
            }
        }
        finally {
            // Lock for subscriptions
            subscribeLock.unlock();
        }
//System.out.println("    subs count of sub&Getters = " + sub.getClientSubAndGetters().get(myInfo));
    }


    /**
     * Method to handle remove subscribeAndGet request sent by domain client
     * (hidden from user).
     *
     * @param id message id refering to these specific subject and type values
     */
    public void handleUnsubscribeAndGetRequest(String subject, String type, int id) {
        cMsgSubscription sub = null;

        subscribeLock.lock();

        try {
            Iterator it = subscriptions.iterator();

//System.out.println("In UN Sub&GetRequest: s,t,ns = " + subject + ", " + type + ", " + namespace);
            while (it.hasNext()) {
                sub = (cMsgSubscription) it.next();
//System.out.println("  sub subject,type,ns = " + sub.getSubject() + ", " +
//                                   sub.getType() + ", " + sub.getNamespace());
                if (sub.getNamespace().equals(namespace) &&
                        sub.getSubject().equals(subject) &&
                        sub.getType().equals(type)) {
//System.out.println("  remove Sub&Getter from sub object");
//System.out.println("  subs A# of sub&Getters = " + sub.getClientSubAndGetters().size());
//System.out.println("  subs count of sub&Getters = " + sub.getClientSubAndGetters().get(myInfo));
                    sub.removeSubAndGetter(myInfo);
//System.out.println("  removed Sub&Getter from sub object");
                    break;
                }
            }

            // If a msg was sent and sub removed simultaneously while a sub&Get (on client)
            // timed out so an unSub&Get was sent, ignore the unSub&get.
            if (sub == null) return;

            // get rid of this subscription if no more subscribers left
//System.out.println("  subs B# of sub&Getters = " + sub.getClientSubAndGetters().size());
//System.out.println("  subs count of sub&Getters = " + sub.getClientSubAndGetters().get(myInfo));
            if (sub.numberOfSubscribers() < 1) {
//System.out.println("  remove subscription object");
                subscriptions.remove(sub);
            }

            // fire notifier if one exists, then get rid of it
            for (cMsgNotifier notifier : sub.getNotifiers()) {
                if (notifier.client == myInfo  && notifier.id == id) {
//System.out.println("  fire notifier now thenand remove from subscription object, id = " + id);
                    notifier.latch.countDown();
                    sub.removeNotifier(notifier);
                    break;
                }
            }
        }
        finally {
            subscribeLock.unlock();
        }
    }


    /**
     * Method to handle shutdown request sent by domain client.
     *
     * @param client client(s) to be shutdown
     * @param server server(s) to be shutdown
     * @param flag   flag describing the mode of shutdown
     * @throws cMsgException
     */
    public void handleShutdownRequest(String client, String server,
                                      int flag) throws cMsgException {

//System.out.println("dHandler: try to kill client " + client);
        // Match all clients that need to be shutdown.
        // Scan through all clients.
        cMsgClientInfo info;

        for (String clientName : clients.keySet()) {
            // Do not shutdown client sending this command, unless told to with flag "includeMe"
            if ( ((flag & cMsgConstants.includeMe) == 0) && (clientName.equals(name)) ) {
                System.out.println("  dHandler: skip client " + clientName);
                continue;
            }

            if (cMsgMessageMatcher.matches(client, clientName, true)) {
                try {
                    System.out.println("  dHandler: deliver shutdown message to client " + clientName);
                    info = clients.get(clientName);
                    info.getDeliverer().deliverMessage(null, cMsgConstants.msgShutdown);
                }
                catch (IOException e) {
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("dHandler: cannot tell client " + name + " to shutdown");
                    }
                }
            }
        }

        // match all servers that need to be shutdown (not implemented yet)
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

}

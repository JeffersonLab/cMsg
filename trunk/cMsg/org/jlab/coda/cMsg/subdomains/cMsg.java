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
import org.jlab.coda.cMsg.*;

import java.util.*;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.ConcurrentHashMap;
import java.nio.ByteBuffer;
import java.io.IOException;

/**
 * Class to handles all client cMsg requests.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsg extends cMsgSubdomainAdapter {
    /** Used to create a unique id number associated with a specific message. */
    private static AtomicInteger sysMsgId = new AtomicInteger();

    /** HashMap to store clients. Name is key and cMsgClientInfo is value. */
    private static ConcurrentHashMap<String,cMsgClientInfo> clients =
            new ConcurrentHashMap<String,cMsgClientInfo>(100);

    /**
     * HashMap to store specific "get" in progress. sysMsgId of get msg is key,
     * and client name is value.
     */
    private static ConcurrentHashMap<Integer,cMsgClientInfo> specificGets =
            new ConcurrentHashMap<Integer,cMsgClientInfo>(100);

    /**
     * Convenience class for storing data in a hashmap used for removing
     * sendAndGets which have timed out.
     */
    private class DeleteGetInfo {
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
    private static ConcurrentHashMap<Integer,DeleteGetInfo> deleteGets =
            new ConcurrentHashMap<Integer,DeleteGetInfo>(100);

    /** List of client info objects corresponding to entries in "subGetList" subscriptions. */
    private ArrayList infoList = new ArrayList(100);

    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugError;

    /** Remainder of UDL client used to connect to domain server. */
    private String UDLRemainder;

    /** Namespace this client sends messages to. */
    private String namespace;

    /** Name of client using this subdomain handler. */
    private String name;

    /** Object used to deliver messages to the client. */
    private cMsgDeliverMessageInterface deliverer;


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
     * to the client.
     *
     * @param deliverer object able to deliver messages to the client
     * @throws cMsgException
     */
    public void setMessageDeliverer(cMsgDeliverMessageInterface deliverer) throws cMsgException {
        if (deliverer == null) {
            throw new cMsgException("cMsg subdomain must be able to deliver messages, set the deliverer.");
        }
        this.deliverer = deliverer;
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
     * @throws cMsgException if client already exists
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {
        String clientName = info.getName();

        cMsgClientInfo ci = clients.putIfAbsent(clientName, info);
        // Check to see if name was taken already.
        // If ci is not null, this key already existed.
        if (ci != null) {
            cMsgException e = new cMsgException("client already exists");
            e.setReturnCode(cMsgConstants.errorAlreadyExists);
            throw e;
        }

        this.name = clientName;

        // this client is registered in this namespace
        info.setNamespace(namespace);
    }


    /**
     * This method handles a message sent by the domain client. The message's subject and type
     * are matched against all clients' subscriptions. For each client, the message is
     * compared to each of its subscriptions until a match is found. At that point, the message
     * is sent to that client. The client is responsible for finding all the matching gets
     * and subscribes and distributing the message among them as necessary.
     *
     * This method is synchronized because the use of rsIdLists and infoList is not
     * thread-safe otherwise. Multiple threads in the domain server can be calling
     * this object's methods simultaneously.
     *
     * @param message message from sender
     * @throws cMsgException if a channel to the client is closed, cannot be created,
     *                          or socket properties cannot be set
     */
    synchronized public void handleSendRequest(cMsgMessageFull message) throws cMsgException {

        if (message == null) return;

        cMsgClientInfo info;
        infoList.clear();

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
                        deliverer.deliverMessage(message, info, cMsgConstants.msgGetResponseIsNull);
                    }
                    else {
                        deliverer.deliverMessage(message, info, cMsgConstants.msgGetResponse);
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

        // Scan through all clients.
        cMsgSubscription sub;
        HashSet subscriptions, gets;
        boolean haveMatch;

        for (String client : clients.keySet()) {
            // Don't deliver a message to the sender
            //if (client.equals(name)) {
            //    continue;
            //}
            info = clients.get(client);
            gets = info.getGets();
            subscriptions = info.getSubscriptions();
            Iterator it;
            haveMatch = false;

            // First test to see if the messages sent by this client (in namespace)
            // can be sent to other clients in their namespaces.
            if (!namespace.equalsIgnoreCase(info.getNamespace())) {
//System.out.println(" handleSendRequest message in wrong namespace (" + info.getNamespace()
//                   + ") for " + info.getName());
                continue;
            }

            // read lock for client's "subscription" and "gets" hashsets
            info.getReadLock().lock();

            try {
                // Look at all subscriptions
                it = subscriptions.iterator();
                while (it.hasNext()) {
                    sub = (cMsgSubscription) it.next();
                    // if subscription matches the msg ...
                    if (cMsgMessageMatcher.matches(sub.getSubjectRegexp(),
                                                   message.getSubject(), false) &&
                        cMsgMessageMatcher.matches(sub.getTypeRegexp(),
                                                   message.getType(), false)) {

                        haveMatch = true;
                        // we know we must send at least 1 message, so we're done
//System.out.println(" handle send msg for subscribe to " + info.getName());
                        break;
                    }
                }

                // Look at all gets (since we need to remove all those that match)
                it = gets.iterator();
                while (it.hasNext()) {
                    sub = (cMsgSubscription) it.next();
                    // if get matches the msg ...
                    if (cMsgMessageMatcher.matches(sub.getSubjectRegexp(),
                                                   message.getSubject(), false) &&
                        cMsgMessageMatcher.matches(sub.getTypeRegexp(),
                                                   message.getType(), false)) {

                        haveMatch = true;
                        // get subscription is 1-shot deal so now remove it
                        it.remove();
//System.out.println(" handle send msg for subscribe&get to " + info.getName());
                    }
                }
            }
            finally {
                info.getReadLock().unlock();
            }

            // store info only if client getting a msg
            if (haveMatch) {
                infoList.add(info);
            }
        }

        // Once we have the subscription/get, msg, and client info,
        // no more need for sychronization

        for (int i = 0; i < infoList.size(); i++) {

            info = (cMsgClientInfo) infoList.get(i);

            // Deliver this msg to this client.
            try {
                deliverer.deliverMessage(message, info, cMsgConstants.msgSubscribeResponse);
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
     * Method to handle subscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     *
     * @param subject  message subject to subscribe to
     * @param type     message type to subscribe to
     * @param id       message id refering to these specific subject and type values
     * @throws cMsgException if no client information is available or a subscription for this
     *                          subject and type already exists
     */
    public void handleSubscribeRequest(String subject, String type,
                                       int id) throws cMsgException {
        // Each client (name) has a cMsgClientInfo object associated with it
        // that contains all relevant information. Retrieve that object
        // from the "clients" table, add subscription to it.
        cMsgClientInfo info = clients.get(name);
        if (info == null) {
            throw new cMsgException("handleSubscribeRequest: no client information stored for " + name);
        }

        // do not add duplicate subscription
        HashSet subscriptions = info.getSubscriptions();

        info.getWriteLock().lock();
        try {
            Iterator it = subscriptions.iterator();
            cMsgSubscription sub;
            while (it.hasNext()) {
                sub = (cMsgSubscription) it.next();
                if (id == sub.getId() ||
                        sub.getSubject().equals(subject) && sub.getType().equals(type)) {
                    throw new cMsgException("handleSubscribeRequest: subscription already exists for subject = " +
                                            subject + " and type = " + type);
                }
            }

            // add new subscription
            sub = new cMsgSubscription(subject, type, id);
            subscriptions.add(sub);
        }
        finally {
            info.getWriteLock().unlock();
        }

    }


    /**
     * Method to handle sunsubscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     *
     * @param subject  message subject to subscribe to
     * @param type     message type to subscribe to
     * @param id       message id refering to these specific subject and type values
     */
     public void handleUnsubscribeRequest(String subject, String type, int id) {
        cMsgClientInfo info = clients.get(name);
        if (info == null) {
            return;
        }
        HashSet subscriptions = info.getSubscriptions();

        info.getWriteLock().lock();
        try {
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
        finally {
            info.getWriteLock().unlock();
        }
    }



    /**
     * Method to synchronously get a single message from a receiver by sending out a
     * message to be responded to.
     *
     * @param message message requesting what sort of message to get
     * @throws cMsgException if no client information is available
     */
    public void handleSendAndGetRequest(cMsgMessageFull message) throws cMsgException {
        // Each client (name) has a cMsgClientInfo object associated with it
        // that contains all relevant information. Retrieve that object
        // from the "clients" table, add get (actually a subscription) to it.
        cMsgClientInfo info = clients.get(name);
        if (info == null) {
            throw new cMsgException("handleGetRequest: no client information stored for " + name);
        }

        int id = sysMsgId.getAndIncrement();
        message.setSysMsgId(id);
        specificGets.put(id, info);
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
     * Method to synchronously get a single message from the server for a one-time
     * subscription of a subject and type.
     *
     * @param subject message subject subscribed to
     * @param type    message type subscribed to
     * @param id      message id refering to these specific subject and type values
     * @throws cMsgException if no client information is available
     */
    public void handleSubscribeAndGetRequest(String subject, String type, int id)
            throws cMsgException {
        cMsgClientInfo info = clients.get(name);
        if (info == null) {
            throw new cMsgException("handleGetRequest: no client information stored for " + name);
        }

        // add new get "subscription" and thereby wait for a matching message to come in
        cMsgSubscription sub = new cMsgSubscription(subject, type, id);
        info.getWriteLock().lock();
        info.getGets().add(sub);
        info.getWriteLock().unlock();
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



    /**
     * Method to handle remove subscribeAndGet request sent by domain client
     * (hidden from user).
     *
     * @param id message id refering to these specific subject and type values
     */
    public void handleUnSubscribeAndGetRequest(int id) {
        cMsgClientInfo info = clients.get(name);
        if (info == null) {
            return;
        }

        HashSet gets = info.getGets();

        info.getWriteLock().lock();
        try {
            Iterator it = gets.iterator();
            cMsgSubscription sub;
            while (it.hasNext()) {
                sub = (cMsgSubscription) it.next();
                if (sub.getId() == id) {
//System.out.println("Removed general Get");
                    it.remove();
                    return;
                }
            }
        }
        finally {
            info.getWriteLock().unlock();
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

System.out.println("dHandler: try to kill client " + client);
        // Match all clients that need to be shutdown.
        // Scan through all clients.
        cMsgClientInfo info;

        for (String clientName : clients.keySet()) {
            // Do not shutdown client sending this command, unless told to with flag "includeMe"
            if ( ((flag & cMsgConstants.includeMe) == 0) && (clientName.equals(name)) ) {
                System.out.println("  dHandler: skip client " + clientName);
                continue;
            }

            info = clients.get(clientName);
            if (cMsgMessageMatcher.matches(client, clientName, true)) {
                try {
                    System.out.println("  dHandler: deliver shutdown message to client " + clientName);
                    deliverer.deliverMessage(null, info, cMsgConstants.msgShutdown);
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

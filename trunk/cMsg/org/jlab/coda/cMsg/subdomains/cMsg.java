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

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgConstants;
import org.jlab.coda.cMsg.cMsgMessageFull;
import org.jlab.coda.cMsg.cMsgClientInfo;
import org.jlab.coda.cMsg.cMsgDomain.cMsgSubscription;
import org.jlab.coda.cMsg.cMsgSubdomainAdapter;

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

    /**
     * List of lists, with each sublist made of receiverSubscribeId's for a single client.
     * This is used so only 1 copy of a msg gets sent to 1 client. With that msg, a sublist
     * of ids is also sent, so the client knows which callbacks the message is for (without
     * having to parse the subject/type).
     */
    private ArrayList rsIdLists = new ArrayList(100);

    /** A direct buffer is necessary for nio socket IO. */
    private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugError;

    /** Remainder of UDL client used to connect to domain server. */
    private String UDLRemainder;

    /** Namespace this client sends messages to. */
    private String namespace;

    /** Name of client using this subdomain handler. */
    private String name;



    /**
     * Implement a simple wildcard matching scheme where "*" means any or no characters and
     * "?" means 1 or no character.
     *
     * @param regexp subscription string that can contain wildcards (* and ?)
     * @param s message string to be matched (can be blank which only matches *)
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
            e.setReturnCode(cMsgConstants.errorNameExists);
            throw e;
        }

        this.name = clientName;

        // this client is registered in this namespace
        info.setNamespace(namespace);
    }


    /**
     * Method to handle message sent by domain client. The message's subject and type
     * are matched against all client subscriptions. The message is sent to all clients
     * with matching subscriptions.  This method is run after all exchanges between
     * domain server and client.
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

        cMsgClientInfo   info;
        infoList.clear();
        rsIdLists.clear();

        // If message is sent in response to a specific get ...
        if (message.isGetResponse()) {
            int id = message.getSysMsgId();
            // Recall the client who originally sent the get request
            // and remove the item from the hashtable
            info = specificGets.remove(id);
            deleteGets.remove(id);

            // If this is the first response to a sendAndGet ...
            if (info != null) {
                // Deliver this msg to this client. If there is no socket connection, make one.
                if (info.getChannel() == null) {
                    try {
                        createChannel(info);
                    }
                    catch (IOException e) {
                        return;
                    }
                }

                try {
//System.out.println(" handle send msg for send&get to " + info.getName());
                    if (message.isNullGetResponse()) {
                        deliverMessage(info.getChannel(), buffer, message, null,
                                       cMsgConstants.msgGetResponseIsNull);
                    }
                    else {
                        deliverMessage(info.getChannel(), buffer, message, null,
                                       cMsgConstants.msgGetResponse);
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
            ArrayList idList = new ArrayList(20);
            haveMatch = false;

            // read lock for client's "subscription" and "gets" hashsets
            info.getReadLock().lock();
            try {
                // Look at all subscriptions
                it = subscriptions.iterator();
                while (it.hasNext()) {
                    sub = (cMsgSubscription) it.next();
                    // if subscription matches the msg ...
                    if (namespace.equalsIgnoreCase(info.getNamespace()) &&
                            matches(sub.getSubject(), message.getSubject()) &&
                            matches(sub.getType(), message.getType())) {
                        // store sub and info for later use (in non-synchronized code)
                        idList.add(sub.getId());
                        haveMatch = true;
//System.out.println(" handle send msg for subscribe to " + info.getName());
                    }
                }

                // Look at all gets
                it = gets.iterator();
                while (it.hasNext()) {
                    sub = (cMsgSubscription) it.next();
                    // if get matches the msg ...
                    if (namespace.equalsIgnoreCase(info.getNamespace()) &&
                            matches(sub.getSubject(), message.getSubject()) &&
                            matches(sub.getType(), message.getType())) {
                        // store get and info for later use (in non-synchronized code)
                        idList.add(sub.getId());
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
                rsIdLists.add(idList);
                infoList.add(info);
            }
        }

        // Once we have the subscription/get, msg, and client info,
        // no more need for sychronization
        ArrayList idList;

        for (int i = 0; i < infoList.size(); i++) {

            info = (cMsgClientInfo) infoList.get(i);
            idList = (ArrayList) rsIdLists.get(i);

            // Deliver this msg to this client. If there is no socket connection, make one.
            if (info.getChannel() == null) {
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("handleSendRequest: make a socket connection to " + info.getName());
                }
                try {
                    createChannel(info);
                }
                catch (IOException e) {
                    continue;
                }
            }

            try {
                deliverMessage(info.getChannel(), buffer, message, idList,
                               cMsgConstants.msgSubscribeResponse);
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

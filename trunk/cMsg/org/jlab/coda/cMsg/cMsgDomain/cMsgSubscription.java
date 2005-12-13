/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 11-Aug-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain;

import org.jlab.coda.cMsg.cMsgMessageMatcher;
import org.jlab.coda.cMsg.cMsgClientInfo;
import org.jlab.coda.cMsg.cMsgCallbackAdapter;
import org.jlab.coda.cMsg.cMsgDomain.client.cMsgCallbackThread;

import java.util.*;
import java.util.regex.Pattern;

/**
 * Class to store a client's subscription to a particular message subject and type.
 * Used by both the cMsg domain API and cMsg subdomain handler objects.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgSubscription {

    /** Subject subscribed to. */
    private String subject;

    /** Subject turned into regular expression that understands * and ?. */
    private String subjectRegexp;

    /** Type subscribed to. */
    private String type;

    /** Type turned into regular expression that understands * and ?. */
    private String typeRegexp;

    /** Compiled regular expression given in {@link #subjectRegexp}. */
    private Pattern subjectPattern;

    /** Compiled regular expression given in {@link #typeRegexp}. */
    private Pattern typePattern;

    /**
     * Id which eliminates the need to parse subject and type
     * strings upon client's receipt of a message. Sometimes referred
     * to as receiverSubscribeId.
     */
    private int id;

    /**
     * This refers to a cMsg subdomain's namespace in which this subscription resides.
     * It is useful only when another server becomes a client for means of bridging
     * messages. In this case, this special client does not reside in 1 namespace but
     * represents subscriptions from different namespaces. This is used on the server
     * side.
     */
    private String namespace;

    /** Used to notify servers that their subscribeAndGet is complete. */
    private HashSet<cMsgNotifier> notifiers;

    /**
     * This set contains all of the callback thread objects
     * {@link org.jlab.coda.cMsg.cMsgDomain.client.cMsgCallbackThread}
     * used on the client side.
     */
    private HashSet<cMsgCallbackThread> callbacks;

    /**
     * This set contains all clients (regular and bridge) subscribed to this exact subject, type,
     * and namespace and is used on the server side.
     */
    private HashSet<cMsgClientInfo> allSubscribers;

    /**
     * This map contains all regular clients (servers do not call
     * subscribeAndGet but use subscribe to implement it) who have
     * called {@link org.jlab.coda.cMsg.cMsg#subscribeAndGet}
     * with this exact subject, type, and namespace. A count is
     * keep of how many times subscribeAndGet for a particular
     * client has been called. The client info object is the key
     * and count is the value. This is used on the server side.
     */
    private HashMap<cMsgClientInfo, Integer> clientSubAndGetters;

    /**
     * This set contains only regular clients subscribed to this exact subject, type,
     * and namespace and is used on the server side.
     */
    private HashSet<cMsgClientInfo> clientSubscribers;



    /**
     * Constructor used by cMsg subdomain handler.
     * @param subject subscription subject
     * @param type subscription type
     * @param namespace namespace subscription exists in
     */
    public cMsgSubscription(String subject, String type, String namespace) {
        this.subject = subject;
        this.type = type;
        this.namespace = namespace;
        subjectRegexp  = cMsgMessageMatcher.escape(subject);
        typeRegexp     = cMsgMessageMatcher.escape(type);
        subjectPattern = Pattern.compile(subjectRegexp);
        typePattern    = Pattern.compile(typeRegexp);
        notifiers      = new HashSet<cMsgNotifier>(30);
        allSubscribers       = new HashSet<cMsgClientInfo>(30);
        clientSubAndGetters  = new HashMap<cMsgClientInfo, Integer>(30);
        clientSubscribers    = new HashSet<cMsgClientInfo>(30);
    }


    /**
     * Constructor used by cMsg domain API.
     * @param subject subscription subject
     * @param type subscription type
     * @param id unique id referring to subject and type combination
     * @param cbThread object containing callback, its argument, and the thread to run it
     */
    public cMsgSubscription(String subject, String type, int id, cMsgCallbackThread cbThread) {
        this.subject = subject;
        this.type = type;
        this.id = id;
        subjectRegexp  = cMsgMessageMatcher.escape(subject);
        typeRegexp     = cMsgMessageMatcher.escape(type);
        subjectPattern = Pattern.compile(subjectRegexp);
        typePattern    = Pattern.compile(typeRegexp);
        notifiers      = new HashSet<cMsgNotifier>(30);
        clientSubAndGetters  = new HashMap<cMsgClientInfo, Integer>(30);
        allSubscribers       = new HashSet<cMsgClientInfo>(30);
        clientSubscribers    = new HashSet<cMsgClientInfo>(30);
        callbacks            = new HashSet<cMsgCallbackThread>(30);
        callbacks.add(cbThread);
    }


    /**
     * Gets subject subscribed to.
     * @return subject subscribed to
     */
    public String getSubject() {
        return subject;
    }

    /**
     * Gets subject turned into regular expression that understands * and ?.
     * @return subject subscribed to in regexp form
     */
    public String getSubjectRegexp() {
        return subjectRegexp;
    }

    /**
     * Gets subject turned into compiled regular expression pattern.
     * @return subject subscribed to in compiled regexp form
     */
    public Pattern getSubjectPattern() {
        return subjectPattern;
    }

    /**
     * Gets type subscribed to.
     * @return type subscribed to
     */
    public String getType() {
        return type;
    }

    /**
     * Gets type turned into regular expression that understands * and ?.
     * @return type subscribed to in regexp form
     */
     public String getTypeRegexp() {
         return typeRegexp;
     }

    /**
     * Gets type turned into compiled regular expression pattern.
     * @return type subscribed to in compiled regexp form
     */
    public Pattern getTypePattern() {
        return typePattern;
    }

    /**
     * Gets the id which client associates with a particular subject and type pair.
     * @return receiverSubscribeId
     */
    public int getId() {
        return id;
    }

    /**
     * Gets the namespace in the cMsg subdomain in which this subscription resides.
     * @return namespace subscription resides in
     */
    public String getNamespace() {
        return namespace;
    }

    /**
     * Sets the namespace in the cMsg subdomain in which this subscription resides.
     * @param namespace namespace subscription resides in
     */
    public void setNamespace(String namespace) {
        this.namespace = namespace;
    }

    //-----------------------------------------------------
    // Methods for dealing with callbacks
    //-----------------------------------------------------

    /**
     * Gets the hashset storing callback threads.
     * @return hashset storing callback threads
     */
    public HashSet<cMsgCallbackThread> getCallbacks() {
        return callbacks;
    }


    /**
     * Method to add a callback thread.
     * @param cbThread  object containing callback thread, its argument,
     *                  and the thread to run it
     */
    public void addCallback(cMsgCallbackThread cbThread) {
        callbacks.add(cbThread);
    }


    /**
     * Method to remove a callback thread.
     * @param cbThread  object containing callback thread to be removed
     */
    public void removeCallback(cMsgCallbackThread cbThread) {
        callbacks.remove(cbThread);
    }


    /**
     * Method to return the number of callback threads registered.
     * @return number of callback registered
     */
    public int numberOfCallbacks() {
        return callbacks.size();
    }


    //-------------------------------------------------------------------------
    // Methods for dealing with clients subscribed to the sub/type
    //-------------------------------------------------------------------------
    public HashSet<cMsgClientInfo> getClientSubscribers() {
        return clientSubscribers;
    }

    public boolean addClientSubscriber(cMsgClientInfo client) {
        return clientSubscribers.add(client);
    }

    public boolean removeClientSubscriber(cMsgClientInfo client) {
        return clientSubscribers.remove(client);
    }


    //-------------------------------------------------------------------------
    // Methods for dealing with clients & servers subscribed to the sub/type
    //-------------------------------------------------------------------------
    public HashSet<cMsgClientInfo> getAllSubscribers() {
        return allSubscribers;
    }

    public boolean containsSubscriber(cMsgClientInfo client) {
        return allSubscribers.contains(client);
    }

    public boolean addSubscriber(cMsgClientInfo client) {
        return allSubscribers.add(client);
    }

    public boolean removeSubscriber(cMsgClientInfo client) {
        return allSubscribers.remove(client);
    }


    //-------------------------------------------------------------------------------
    // Methods for dealing with clients who subscribeAndGet to the sub/type/namespace
    //-------------------------------------------------------------------------------
    public HashMap<cMsgClientInfo, Integer> getSubAndGetters() {
        return clientSubAndGetters;
    }

    public void addSubAndGetter(cMsgClientInfo client) {
//System.out.println("      SUB: addSub&Getter arg = " + client);
        Integer count = clientSubAndGetters.get(client);
        if (count == null) {
//System.out.println("      SUB: set sub&Getter cnt to 1");
            clientSubAndGetters.put(client, 1);
        }
        else {
//System.out.println("      SUB: set sub&Getter cnt to " + (count + 1));
            clientSubAndGetters.put(client, count + 1);
        }
    }

    public void clearSubAndGetters() {
        clientSubAndGetters.clear();
    }

    public void removeSubAndGetter(cMsgClientInfo client) {
        Integer count = clientSubAndGetters.get(client);
//System.out.println("      SUB: removeSub&Getter: count = " + count);
        if (count == null || count < 2) {
//System.out.println("      SUB: remove sub&Getter completely (count = 0)");
            clientSubAndGetters.remove(client);
        }
        else {
//System.out.println("      SUB: reduce sub&Getter cnt to " + (count - 1));
            clientSubAndGetters.put(client, count - 1);
        }
    }



    public int numberOfSubscribers() {
        return (allSubscribers.size() + clientSubAndGetters.size());
    }


    //--------------------------------------------------------------------------
    // Methods for dealing with servers' notifiers of subscribeAndGet completion
    //--------------------------------------------------------------------------

    public void addNotifier(cMsgNotifier notifier) {
        notifiers.add(notifier);
    }

    public void removeNotifier(cMsgNotifier notifier) {
        notifiers.remove(notifier);
    }

    public void clearNotifiers() {
        notifiers.clear();
    }

    public Set<cMsgNotifier> getNotifiers() {
        return notifiers;
    }
    public int numberOfNotifiers() {
        return (notifiers.size());
    }
}

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
import org.jlab.coda.cMsg.cMsgDomain.client.cMsgCallbackThread;

import java.util.HashSet;
import java.util.HashMap;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.concurrent.atomic.AtomicInteger;

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

    /**
     * This set contains all of the callback objects {@link org.jlab.coda.cMsg.cMsgDomain.client.cMsgCallbackThread}
     * used on the client side.
     */
    private HashSet<cMsgCallbackThread> callbacks;

    /** This set contains all clients subscribed to this exact subject and type and is used on the server side. */
    private HashSet<cMsgClientInfo> subscribers;

    private class subAndGetClient {
        cMsgClientInfo info;
        int id;

        public subAndGetClient(cMsgClientInfo info, int id) {
            this.info = info;
            this.id = id;
        }
    }

    /**
     * This hashtable contains all clients who have called {@link org.jlab.coda.cMsg.cMsg#sendAndGet}
     * with this exact subject and type. The client info object is the key and a unique id identifying
     * the operation is the value. This is used on the server side.
     */
    private ArrayList<subAndGetClient> subAndGetters;


    /**
     * Constructor used by cMsg subdomain handler.
     * @param subject subscription subject
     * @param type subscription type
     */
    public cMsgSubscription(String subject, String type, String namespace) {
        this.subject = subject;
        this.type = type;
        this.namespace = namespace;
        subjectRegexp = cMsgMessageMatcher.escape(subject);
        typeRegexp    = cMsgMessageMatcher.escape(type);
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
        subjectRegexp = cMsgMessageMatcher.escape(subject);
        typeRegexp    = cMsgMessageMatcher.escape(type);
        callbacks = new HashSet<cMsgCallbackThread>(30);
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
     * Method to add a callback.
     * @param cbThread  object containing callback, its argument, and the thread to run it
     */
    public void addCallback(cMsgCallbackThread cbThread) {
        callbacks.add(cbThread);
    }


    /**
     * Method to remove a callback.
     * @param cbThread  object containing callback to be removed
     */
    public void removeCallback(cMsgCallbackThread cbThread) {
        callbacks.remove(cbThread);
    }


    /**
     * Method to return the number of callbacks registered.
     * @return number of callback registered
     */
    public int numberOfCallbacks() {
        return callbacks.size();
    }

    //-------------------------------------------------------------------------
    // Methods for dealing with clients subscribed to the sub/type
    //-------------------------------------------------------------------------

    public boolean containsSubscriber(cMsgClientInfo client) {
        return subscribers.contains(client);
    }

    public boolean addSubscriber(cMsgClientInfo client) {
        return subscribers.add(client);
    }

    public boolean removeSubscriber(cMsgClientInfo client) {
        return subscribers.remove(client);
    }

    public boolean addSubAndGetter(cMsgClientInfo client, int id) {
        subAndGetClient clientObj = new subAndGetClient(client, id);
        return subAndGetters.add(clientObj);
    }

    public void removeSubAndGetters() {
        subAndGetters.clear();
    }

    public boolean removeSubAndGetter(cMsgClientInfo client, int id) {
        subAndGetClient subCli = null;
        Iterator it = subAndGetters.iterator();

        while (it.hasNext()) {
            subCli = (subAndGetClient) it.next();
            if (subCli.info == client && subCli.id == id) {
                it.remove();
                return true;
            }
        }
        return false;
    }

    public boolean containsSubAndGetter(cMsgClientInfo client, int id) {
        subAndGetClient subCli = null;
        Iterator it = subAndGetters.iterator();

        while (it.hasNext()) {
            subCli = (subAndGetClient) it.next();
            if (subCli.info == client && subCli.id == id) {
                return true;
            }
        }
        return false;
    }

    public int numberOfSubscribers() {
        return (subscribers.size() + subAndGetters.size());
    }

}

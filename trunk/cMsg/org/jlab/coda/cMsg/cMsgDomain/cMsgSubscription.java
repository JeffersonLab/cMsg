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
import org.jlab.coda.cMsg.cMsgDomain.client.cMsgCallbackThread;

import java.util.HashSet;

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
     * represents subscriptions from different namespaces.
     */
    private String namespace;

    /** This set contains all of the callback objects {@link org.jlab.coda.cMsg.cMsgDomain.client.cMsgCallbackThread}. */
    private HashSet<cMsgCallbackThread> callbacks;


    /**
     * Constructor used by cMsg subdomain handler.
     * @param subject subscription subject
     * @param type subscription type
     * @param id unique id referring to subject and type combination
     */
    public cMsgSubscription(String subject, String type, int id) {
        this.subject = subject;
        this.type = type;
        this.id = id;
        subjectRegexp = cMsgMessageMatcher.escape(subject);
        typeRegexp    = cMsgMessageMatcher.escape(type);
        callbacks = new HashSet<cMsgCallbackThread>(30);
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
}

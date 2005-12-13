/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 6-Dec-2005, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.server;

import org.jlab.coda.cMsg.cMsgCallbackAdapter;
import org.jlab.coda.cMsg.cMsgClientInfo;
import org.jlab.coda.cMsg.cMsgException;

import java.util.HashMap;

/**
 * Class to store the record of a client's subscription to a particular message
 * subject, type, and namespace. It also stores a record of all subscribeAndGet
 * calls to the same subject, type, and namespace. Used by the cMsgComainServer class.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgServerSubscribeInfo {
    /** Subject subscribed to. */
    String subject;

    /** Type subscribed to. */
    String type;

    /**
     * This refers to a cMsg subdomain's namespace in which this subscription resides.
     * It is useful only when another server becomes a client for means of bridging
     * messages. In this case, this special client does not reside in 1 namespace but
     * represents subscriptions from different namespaces. This is used on the server
     * side.
     */
    String namespace;

    /** Client which created the subscription/subAndGet. */
    cMsgClientInfo info;

    /** If this is 1, the client did a subscription, else if 0 only subscribeAndGets. */
    int subscribed;

    /**
     * This map contains all regular clients (servers do not call
     * subscribeAndGet but use subscribe to implement it) who have
     * called {@link org.jlab.coda.cMsg.cMsg#subscribeAndGet}
     * with this exact subject, type, and namespace. A count is
     * keep of how many times subscribeAndGet for a particular
     * client has been called. The client info object is the key
     * and count is the value. This is used on the server side.
     */
    HashMap<Integer, cMsgCallbackAdapter> subAndGetters;


    /**
     * Constructor used by cMsgDomainServer object basically for storage of subject, type,
     * namespace and callbacks for sendAndGets.
     * @param subject subscription subject
     * @param type subscription type
     */
    public cMsgServerSubscribeInfo(String subject, String type, String namespace,
                             cMsgClientInfo info) {
        this.subject   = subject;
        this.type      = type;
        this.namespace = namespace;
        this.info      = info;

        subscribed = 1;

        subAndGetters  = new HashMap<Integer, cMsgCallbackAdapter>(30);
    }


    /**
     * Constructor used by cMsgDomainServer object basically for storage of subject, type,
     * namespace and callbacks for sendAndGets.
     * @param subject subscription subject
     * @param type subscription type
     */
    public cMsgServerSubscribeInfo(String subject, String type, String namespace,
                            cMsgClientInfo info, int id, cMsgCallbackAdapter cb) {
        this.subject   = subject;
        this.type      = type;
        this.namespace = namespace;
        this.info      = info;

        subAndGetters  = new HashMap<Integer, cMsgCallbackAdapter>(30);
        subAndGetters.put(id, cb);
    }


    /**
     * Gets subject subscribed to.
     * @return subject subscribed to
     */
    public String getSubject() {
        return subject;
    }

    /**
     * Gets type subscribed to.
     * @return type subscribed to
     */
    public String getType() {
        return type;
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
      * Calling this method means this client is now subscribed to this sub/type/namespace.
      * Previously only a subscribeAndGet to this sub/type/ns was done.
      */
     public void addSubscription() throws cMsgException {
        if (subscribed > 0) throw new cMsgException("Only 1 subscribe please");
        subscribed = 1;
     }

    /**
      * Calling this method means this client is not subscribed to this sub/type/namespace
      * anymore.
      */
     public void removeSubscription() {
         subscribed = 0;
     }

     /** Is the client subscribed (true) or are there only subscribeAndGets (false)? */
    public boolean isSubscribed() {
        if (subscribed > 0) return true;
        return false;
    }

    //-------------------------------------------------------------------------------
    // Methods for dealing with clients who subscribeAndGet to the sub/type/namespace
    //-------------------------------------------------------------------------------
    public HashMap<Integer, cMsgCallbackAdapter> getSubAndGetters() {
        return subAndGetters;
    }

    public void addSubAndGetter(int id, cMsgCallbackAdapter cb) {
        subAndGetters.put(id, cb);
    }

    public void removeSubAndGetter(int id) {
        subAndGetters.remove(id);
    }


    public int numberOfSubscribers() {
        return (subscribed + subAndGetters.size());
    }

}

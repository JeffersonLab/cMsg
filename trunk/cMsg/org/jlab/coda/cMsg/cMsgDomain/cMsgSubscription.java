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

import java.util.HashSet;

/**
 * Class to store a client's subscription to a particular message subject and type.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgSubscription {

    /** Subject subscribed to. */
    private String subject;

    /** Type subscribed to. */
    private String type;

    /**
     * Id which eliminates the need to parse subject and type
     * strings upon client's receipt of a message. Sometimes referred
     * to as receiverSubscribeId.
     */
    private int id;

    /**
     * When this class is used with subscriptions, this set contains all
     * of the callback objects {@link cMsgCallbackThread}. When this class
     * is used with general gets, this set contains message storage objects
     * {@link cMsgMessageHolder}
     */
    private HashSet callbacks;


    /**
     * Constructor.
     * @param subject subscription subject
     * @param type subscription type
     * @param id unique id referring to subject and type combination
     */
    public cMsgSubscription(String subject, String type, int id) {
        this.subject = subject;
        this.type = type;
        this.id = id;
        callbacks = new HashSet(30);
    }


    /**
     * Constructor.
     * @param subject subscription subject
     * @param type subscription type
     * @param id unique id referring to subject and type combination
     * @param cbThread object containing callback, its argument, and the thread to run it
     */
    public cMsgSubscription(String subject, String type, int id, cMsgCallbackThread cbThread) {
        this.subject = subject;
        this.type = type;
        this.id = id;
        callbacks = new HashSet(30);
        callbacks.add(cbThread);
    }


    /**
     * Constructor.
     * @param subject subscription subject
     * @param type subscription type
     * @param id unique id referring to subject and type combination
     * @param holder object for holding message and synchronizing on
     */
    public cMsgSubscription(String subject, String type, int id, cMsgMessageHolder holder) {
        this.subject = subject;
        this.type = type;
        this.id = id;
        callbacks = new HashSet(30);
        callbacks.add(holder);
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
     * Gets the id which client associates with a particular
     * subject and type pair.
     * @return receiverSubscribeId
     */
    public int getId() {
        return id;
    }


    /**
     * Gets the hashset storing callback threads.
     * @return hashset storing callback threads
     */
    public HashSet getCallbacks() {
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


    /**
     * Gets the hashset storing message holders.
     * @return hashset storing message holders
     */
    public HashSet getHolders() {
        return callbacks;
    }


    /**
     * Method to add a helping object for running a "get" method.
     * @param holder  object for holding message and synchronizing on
     */
    public void addHolder(cMsgMessageHolder holder) {
        callbacks.add(holder);
    }


    /**
     * Method to remove a helping object for running a "get" method.
     * @param holder  object to be removed
     */
    public void removeHolder(cMsgMessageHolder holder) {
        callbacks.remove(holder);
    }


    /**
     * Method to return the number of active get calls.
     * @return number of active get calls
     */
    public int numberOfGets() {
        return callbacks.size();
    }
}

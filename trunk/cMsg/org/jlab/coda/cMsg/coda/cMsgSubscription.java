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

package org.jlab.coda.cMsg.coda;

import java.util.HashSet;

/**
 * Class to store a client's subscription to a particular message subject and type.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgSubscription {

    /** Subject subscribed to. */
    String subject;

    /** Type subscribed to. */
    String type;

    /**
     * Id which eliminates the need to parse subject and type
     * strings upon client's receipt of a message. Sometimes referred
     * to as receiverSubscribeId.
     */
    int id;

    /** Set of all callback information objects (cMsgCallbackInfo). */
    HashSet callbacks;


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
     * @param info object containing callback to be added and its argument
     */
    public cMsgSubscription(String subject, String type, int id, cMsgCallbackInfo info) {
        this.subject = subject;
        this.type = type;
        this.id = id;
        callbacks = new HashSet(30);
        callbacks.add(info);
    }


    /**
     * Method to add a callback.
     * @param cbInfo  object containing callback to be added and its argument
     */
    public void addCallback(cMsgCallbackInfo cbInfo) {
        callbacks.add(cbInfo);
    }


    /**
     * Method to remove a callback.
     * @param cbInfo  object containing callback to be removed
     */
    public void removeCallback(cMsgCallbackInfo cbInfo) {
        callbacks.remove(cbInfo);
    }


    /**
     * Method to return the number of callbacks registered.
     * @return number of callback registered
     */
    public int numberOfCallbacks() {
        return callbacks.size();
    }
}

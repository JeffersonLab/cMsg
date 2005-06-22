/*----------------------------------------------------------------------------*
 *  Copyright (c) 2005        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 8-Jun-2005, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.server;

import org.jlab.coda.cMsg.cMsgMessageFull;
import org.jlab.coda.cMsg.cMsgDeliverMessageInterface;

/**
 * This class delivers messages from the subdomain handler objects in the cMsg
 * domain to a particular internal client. Since the client is internal to the
 * domain server, just store the message in this object and wakeup anyone waiting
 * for it.
 */
public class cMsgInternalDeliverer implements cMsgDeliverMessageInterface {

    /** Class which allows waiting for notification of some event completion. */
    class WaitNotify extends Object {
        synchronized public void waitNow() throws InterruptedException {wait();}
        synchronized public void wakeUp() {notifyAll();}
    }

    /** Object used to notify listeners that a messge has arrived. */
    private WaitNotify waiter;

    /** Place to store the message to be passed to the internal client. */
    cMsgMessageFull message;

    /**
     * Create a message delivering object for use with the internal client.
     */
    public cMsgInternalDeliverer() {
        waiter = new WaitNotify();
    }


    /**
     * Method to deliver a message from a domain server's subdomain handler to a client.
     *
     * @param msg message to sent to client
     * @param msgType type of communication with the client
     */
    synchronized public void deliverMessage(cMsgMessageFull msg, int msgType) {
        message = msg;
        waiter.wakeUp();
    }

    /**
     * Method to deliver a message from a domain server's subdomain handler to a client
     * and receive acknowledgment that the message was received.
     *
     * @param msg message to sent to client
     * @param msgType type of communication with the client
     * @return true if message acknowledged by receiver or acknowledgment undesired, otherwise false
     */
    synchronized public boolean deliverMessageAndAcknowledge(cMsgMessageFull msg, int msgType) {
        message = msg;
        waiter.wakeUp();
        return true;
    }


}

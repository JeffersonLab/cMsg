/*----------------------------------------------------------------------------*
 *  Copyright (c) 2009        Jefferson Science Associates,                   *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 6-Feb-2009, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;

import org.jlab.coda.cMsg.common.cMsgCallbackInterface;

/**
 * This interface is implemented by the object returned by calling {@link cMsg#subscribe}.
 * The returned object may be used to query and control the subscription to some degree.
 */
public interface cMsgSubscriptionHandle {
    /**
     * Gets the number of messages in the queue.
     * @return number of messages in the queue
     */
    public int getQueueSize();

    /**
     * Returns true if queue is full.
     * @return true if queue is full
     */
    public boolean isQueueFull();

    /**
     * Clears the queue of all messages.
     */
    public void clearQueue();

    /**
     * Gets the total number of messages passed to the callback.
     * @return total number of messages passed to the callback
     */
    public long getMsgCount();

    /**
     * Gets the domain in which this subscription lives.
     * @return the domain in which this subscription lives
     */
    public String getDomain();

    /**
      * Gets the subject of this subscription.
      * @return the subject of this subscription
      */
    public String getSubject();

    /**
     * Gets the type of this subscription.
     * @return the type of this subscription
     */
    public String getType();

    /**
     * Gets the callback object.
     * @return user callback object
     */
    public cMsgCallbackInterface getCallback() ;

    /**
     * Gets the subscription's user object argument.
     * @return subscription's user object argument
     */
    public Object getUserObject();

}

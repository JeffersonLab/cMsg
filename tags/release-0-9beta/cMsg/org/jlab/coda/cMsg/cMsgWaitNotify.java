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

package org.jlab.coda.cMsg;

/**
 * This class allows waiting for notification of some event completion.
 * It can also store an object to be passed to the object being notified.
 */
public class cMsgWaitNotify {
    /** An object can be stored here and passed on to the one being notified. */
    Object storage;

    /** Wait until woken up. */
    synchronized public void waitNow() throws InterruptedException {
        wait();
    }

    /** Wake up all who have waited on this object by calling #waitNow. */
    synchronized public void wakeWaiters() {
        notifyAll();
    }

    /** Store object to be passed and wake up all who have waited on this object by calling #waitNow. */
    synchronized public void setObjectAndWakeWaiters(Object o) {
        storage = o;
        notifyAll();
    }

    /** Wait until woken up and given a return object. */
    synchronized public Object waitAndGetObject() throws InterruptedException {
        wait();
        return storage;
    }

}

/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 2-Jan-2006, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;


/**
 * This class is used to help in implementing some sendAndGet, subscribeAndGet, and
 * syncSend methods.
 * This is true in the cMsg domain for the client and in the RCBroadcast and RCServer
 * domains for servers.
 * An object of this class stores a msg from a sender to the method's caller and
 * is used to synchronize/wait/notify on. It also indicates whether the call timed
 * out or not.
 */
public class cMsgGetHelper {
    /**  Message object. */
    cMsgMessageFull message;

    /** Has the "subscribeAndGet" or "sendAndGet" call timed out? */
    volatile boolean timedOut;

    /**
     * When a "subscribeAndGet" or "sendAndGet" is woken up by an error condition,
     * such as "the server died", this code is set.
     */
    int errorCode;

    /** Used to store syncSend return value. */
    int intVal;


    public cMsgGetHelper() {
        timedOut  = true;
        errorCode = cMsgConstants.ok;
    }


    /**
     * Returns the message object.
     * @return the message object.
     */
    public cMsgMessageFull getMessage() {
        return message;
    }

    /**
     * Sets the messge object;
     * @param message the message object
     */
    public void setMessage(cMsgMessageFull message) {
        this.message = message;
    }

    /**
     * Returns true if the "syncSend" response has not yet been received and
     * the client still needs to wait for it.
     * @return true if the "syncSend" response has not yet been received and
     *              the client still needs to wait for it
     */
    public boolean needToWait() {
        return timedOut;
    }

    /**
     * Returns true if the "subscribeAndGet" or "sendAndGet" call timed out.
     * @return true if the "subscribeAndGet" or "sendAndGet" call timed out.
     */
    public boolean isTimedOut() {
        return timedOut;
    }

    /**
     * Set boolean telling whether he "subscribeAndGet" or "sendAndGet" call timed out or not.
     * @param timedOut boolean telling whether he "subscribeAndGet" or "sendAndGet" call timed out or not.
     */
    public void setTimedOut(boolean timedOut) {
        this.timedOut = timedOut;
    }

    /**
     * Gets the error code from when a "subscribeAndGet" or "sendAndGet" is woken up by an error condition.
     * @return error code
     */
    public int getErrorCode() {
        return errorCode;
    }

    /**
     * Sets the error code from when a "subscribeAndGet" or "sendAndGet" is woken up by an error condition.
     * @param errorCode error code
     */
    public void setErrorCode(int errorCode) {
        this.errorCode = errorCode;
    }

    /**
     * Gets intVal from when a "syncSend" is woken up.
     * @return syncSend intVal
     */
    public int getIntVal() {
        return intVal;
    }

    /**
     * Sets intVal from when a "syncSend" is woken up.
     * @param intVal syncSend response
     */
    public void setIntVal(int intVal) {
        this.intVal = intVal;
    }


}

/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 10-Sep-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.coda;

import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgCallback;

/**
 * This class is used to run a message callback in its own thread.
 * The thread is self-starting and waits to execute the callback.
 * All it needs is a notify.
 */
public class cMsgCallbackThread extends Thread {
    /** Message to be passed to the callback. */
    private cMsgMessage message;

    /** User argument to be passed to the callback. */
    private Object arg;

    /** Callback to be run. */
    cMsgCallback callback;

    /** Get message to be given to the callback. */
    public cMsgMessage getMessage() {return message;}

    /** Set message to be given to the callback. */
    public void setMessage(cMsgMessage message) {this.message = message;}

    cMsgCallbackThread(cMsgCallback callback, Object arg) {
        this.callback = callback;
        this.arg = arg;
        this.message = message;
        start();
    }

    /** This method is executed as a thread which runs the callback method */
    public void run() {
        while(true) {
            synchronized(this) {
                try {
                    // Wait to run the callback until notified
                    wait();
                }
                catch (InterruptedException e) {
                    // if interrupted, it's a sign for this thread to die
                    return;
                }
            }
            //System.out.println("cMsgCallbackThread: will run callback");
            callback.callback(message, arg);
        }
    }
}

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

package org.jlab.coda.cMsg.cMsgDomain;

import org.jlab.coda.cMsg.cMsgMessageFull;
import org.jlab.coda.cMsg.cMsgCallback;
import org.jlab.coda.cMsg.cMsgException;

import java.util.List;
import java.util.LinkedList;
import java.util.Collections;

/**
 * This class is used to run a message callback in its own thread.
 * The thread is self-starting and waits to execute the callback.
 * All it needs is a notify.
 */
public class cMsgCallbackThread extends Thread {
    /** List of ordered messages to be passed to the callback. */
    private List<cMsgMessageFull> messageList;

    //private int lastOdd=1,lastEven=0, num;
    private int size;

    /** User argument to be passed to the callback. */
    private Object arg;

    /** Callback to be run. */
    cMsgCallback callback;

    /** Setting this to true will kill this thread as soon as possible. */
    private boolean dieNow;

    /** Setting this to true will kill this thread immediate after running the callback. */
    private boolean dieAfterCallback;

    /** Count of how may supplemental threads are currently active. */
    private int threads;

    /**
     * Object to synchronize on when changing the value of threads and
     * in the case of get, telling the getter to wakeup. It does double
     * duty, but they don't interfere.
     */
    Object sync = new Object();

    /** Place to temporarily store the returned message from a get. */
    cMsgMessageFull message;


    /** If this thread is waiting, it's woken up. */
    synchronized public void wakeup() {
        notify();
    }

    /** Kills this thread as soon as possible. If it's waiting, it's woken up first. */
    synchronized public void dieNow() {
        dieNow = true;
        notifyAll();
    }

    /** Kills this thread after running the callback. If it's currently waiting, it's woken up. */
    synchronized public void dieAfterCallback() {
        dieAfterCallback = true;
        notifyAll();
    }

    /**
     * Class defining threads which can be run in parallel when many incoming
     * messages all need to run the same callback.
     */
    class SupplementalThread extends Thread {

        SupplementalThread() {
            setDaemon(true);
            start();
        }

        /** This method is executed as a thread which runs the callback method */
        public void run() {
            cMsgMessageFull message;
            int empty;

            while (true) {
                empty = 0;
                // only wait if no messages to run callback on
                while (messageList.size() < 1) {
                    // self-destruct if woken 9 times with no messages available
                    if (++empty%10 == 0) {
                        synchronized (sync) {
                            threads--;
                            //System.out.println("t -= " + threads);
                        }
                        return;
                    }

                    try {
                        // Wait to run the callback until notified by client's listening thread
                        // that there are messages to run the callback on.
                        synchronized (cMsgCallbackThread.this) {
                            cMsgCallbackThread.this.wait(200);
                        }
                    }
                    catch (InterruptedException e) {
                        e.printStackTrace();
                        System.exit(-1);
                    }

                    if (dieNow) {
                        return;
                    }
                }

                if (dieNow) {
                    return;
                }

                // grab a message off the list if possible
                synchronized (messageList) {
                    if (messageList.size() > 0) {
                        message = messageList.remove(0);
                    }
                    else {
                        message = null;
                    }
                }

                if (message != null) {
                    callback.callback(message, arg);
                }
            }
        }
    }

    /**
     * Constructor used to pass an arbitrary argument to callback method.
     * @param callback callback to be run when message arrives
     * @param arg user-supplied argument for callback
     */
    cMsgCallbackThread(cMsgCallback callback, Object arg) {
        this.callback = callback;
        this.arg = arg;
        messageList = Collections.synchronizedList(new LinkedList());
        start();
    }

    /**
     * Constructor used to pass "this" object (being constructed at this very
     * moment) to the callback method.
     * @param callback callback to be run when message arrives
     */
    cMsgCallbackThread(cMsgCallback callback) {
        this.callback = callback;
        this.arg = this;
        messageList = Collections.synchronizedList(new LinkedList());
        start();
    }


    /**
     * Set message to be given to the callback.
     * @param message message to be passed to callback
     * @throws cMsgException if there are too many messages to handle
     */
    public void sendMessage(cMsgMessageFull message) throws cMsgException {
        messageList.add(message);
        size = messageList.size();
        if (size % 1000 == 0) {
            //System.out.println(size+"");
        }
        if (size > callback.getMaximumCueSize()) {
            if (callback.maySkipMessages()) {
                messageList.subList(0, size - callback.getSkipSize()).clear();
            }
            else {
                throw new cMsgException("too many messages for callback to handle");
            }
        }
    }


    /** This method is executed as a thread which runs the callback method */
    public void run() {
        cMsgMessageFull message, msgCopy;
        int threadsAdded, need, maxToAdd, wantToAdd;

        while (true) {
            threadsAdded = 0;

            if (!callback.mustSerializeMessages() &&
                threads < callback.getMaximumCueSize() &&
                messageList.size() > callback.getMessagesPerThread()) {

                // find number of threads needed
                need = messageList.size()/callback.getMessagesPerThread();

                // at this point, threads may only decrease, it is only increased below

                // add more threads if necessary
                if (need > threads) {
                    // maximum # of threads that can be added w/o exceeding limit
                    maxToAdd  = callback.getMaximumThreads() - threads;

                    // number of threads we want to add to handle the load
                    wantToAdd = need - threads;

                    // number of threads that we will add
                    threadsAdded = maxToAdd > wantToAdd ? wantToAdd : maxToAdd;

                    for (int i=0; i < threadsAdded; i++) {
                        new SupplementalThread();
                    }

                    // do the following bookkeeping under mutex protection
                    if (threadsAdded > 0) {
                        synchronized (sync) {
                            threads += threadsAdded;
                            //System.out.println("t += " + threads);
                        }
                    }
                }
            }

            // only wait if no messages to run callback on
            while (messageList.size() < 1) {
                // die immediately if commanded to, or die now even if told
                // to run callback first because there are no messages to
                // pass to the callback
                if (dieNow || dieAfterCallback) {
                    return;
                }

                try {
                    // Wait to run the callback until notified by client's listening thread
                    // that there are messages to run the callback on.
                    synchronized (this) {
                        wait();
                    }
                }
                catch (InterruptedException e) {
                    e.printStackTrace();
// BUG BUG DO WE WANT TO DIE LIKE THIS?
                    System.exit(-1);
                }

            }

            if (dieNow) {
                return;
            }

            // grab a message off the list if possible
            // Run callback method with proper argument
            if (messageList.size() > 0) {
                message = messageList.remove(0);
            }
            else {
                message = null;
                continue;
            }

            // first copy the msg so multiple callbacks don't clobber each other
            msgCopy = message.copy();

            // run callback method
            callback.callback(msgCopy, arg);

            // die if commanded to
            if (dieAfterCallback || dieNow) {
                return;
            }
        }
    }
}

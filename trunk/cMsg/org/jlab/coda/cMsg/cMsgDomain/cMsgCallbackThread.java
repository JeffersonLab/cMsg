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
import org.jlab.coda.cMsg.cMsgCallbackInterface;
import org.jlab.coda.cMsg.cMsgException;

import java.util.ArrayList;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * This class is used to run a message callback in its own thread.
 * The thread is self-starting and waits to execute the callback.
 * All it needs is a notify.
 */
public class cMsgCallbackThread extends Thread {
    /** List of ordered messages to be passed to the callback. */
    private LinkedBlockingQueue<cMsgMessageFull> messageCue;

    /** List of message that need to be dumped. */
    private ArrayList<cMsgMessageFull> dumpList;

    /** User argument to be passed to the callback. */
    private Object arg;

    /** Callback to be run. */
    cMsgCallbackInterface callback;

    /** Count of how may supplemental threads are currently active. */
    private AtomicInteger threads = new AtomicInteger();

    /** Place to temporarily store the returned message from a get. */
    cMsgMessageFull message;

    /** Setting this to true will kill this thread as soon as possible. */
    private volatile boolean dieNow;


    /** Kills this thread as soon as possible. */
    public void dieNow() {
        dieNow = true;
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
            int i, empty;

            while (true) {
                empty = 0;
                message = null;

                while (message == null) {
                    // die immediately if commanded to
                    if (dieNow || ++empty % 10 == 0) {
                        i = threads.getAndDecrement();
                        //System.out.println("t -= " + i);
                        return;
                    }

                    try {
                        message = messageCue.poll(200, TimeUnit.MILLISECONDS);
                    }
                    catch (InterruptedException e) {
                    }
                }

                if (dieNow) {
                    return;
                }

                // run callback with copied msg so multiple callbacks don't clobber each other
                callback.callback(message.copy(), arg);
            }
        }
    }

    /**
     * Constructor used to pass an arbitrary argument to callback method.
     * @param callback callback to be run when message arrives
     * @param arg user-supplied argument for callback
     */
    cMsgCallbackThread(cMsgCallbackInterface callback, Object arg) {
        this.callback = callback;
        this.arg      = arg;
        messageCue    = new LinkedBlockingQueue<cMsgMessageFull>(callback.getMaximumCueSize());
        dumpList      = new ArrayList<cMsgMessageFull>(callback.getSkipSize());
        start();
    }

    /**
     * Constructor used to pass "this" object (being constructed at this very
     * moment) to the callback method.
     * @param callback callback to be run when message arrives
     */
    cMsgCallbackThread(cMsgCallbackInterface callback) {
        this.callback = callback;
        this.arg      = this;
        messageCue    = new LinkedBlockingQueue<cMsgMessageFull>(callback.getMaximumCueSize());
        dumpList      = new ArrayList<cMsgMessageFull>(callback.getSkipSize());
        start();
    }


    /**
     * Set message to be given to the callback.
     * @param message message to be passed to callback
     * @throws cMsgException if there are too many messages to handle
     */
    public void sendMessage(cMsgMessageFull message) throws cMsgException {
        // if the cue if full, dump some messages if possible, else throw exception
        if (!messageCue.offer(message)) {
            if (!callback.maySkipMessages()) {
                throw new cMsgException("too many messages for callback to handle");
            }

            messageCue.drainTo(dumpList, callback.getSkipSize());
            dumpList.clear();
            messageCue.offer(message);
        }

        //if (messageCue.size() > 0 && messageCue.size() % 1000 == 0) {
        //    System.out.println("" + messageCue.size());
        //}
    }


    /** This method is executed as a thread which runs the callback method */
    public void run() {
        cMsgMessageFull message;
        int j, threadsAdded, threadsExisting, need, maxToAdd, wantToAdd;

        while (true) {
            threadsExisting = threads.get();
            threadsAdded = 0;
            message = null;

            if (!callback.mustSerializeMessages() &&
                threadsExisting < callback.getMaximumCueSize() &&
                messageCue.size() > callback.getMessagesPerThread()) {

                // find number of threads needed
                need = messageCue.size()/callback.getMessagesPerThread();

                // at this point, threads may only decrease, it is only increased below

                // add more threads if necessary
                if (need > threadsExisting) {
                    // maximum # of threads that can be added w/o exceeding limit
                    maxToAdd  = callback.getMaximumThreads() - threadsExisting;

                    // number of threads we want to add to handle the load
                    wantToAdd = need - threadsExisting;

                    // number of threads that we will add
                    threadsAdded = maxToAdd > wantToAdd ? wantToAdd : maxToAdd;

                    for (int i=0; i < threadsAdded; i++) {
                        new SupplementalThread();
                    }

                    // do the following bookkeeping under mutex protection
                    if (threadsAdded > 0) {
                        j = threads.getAndAdd(threadsAdded);
                        //System.out.println("t += " + j);
                    }
                }
            }

            while (message == null) {
                // die immediately if commanded to
                if (dieNow) {
                    return;
                }

                try {
                    message = messageCue.poll(1000, TimeUnit.MILLISECONDS);
                }
                catch (InterruptedException e) {
                }
            }

            if (dieNow) {
                return;
            }

            // run callback with copied msg so multiple callbacks don't clobber each other
            callback.callback(message.copy(), arg);
        }
    }
}

/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 10-Sep-2004, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.common;

import org.jlab.coda.cMsg.cMsgSubscriptionHandle;

import java.util.ArrayList;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * This class is used to run a message callback in its own thread.
 * The thread is self-starting and waits to execute the callback.
 * All it needs is a notify.
 */
public class cMsgCallbackThread extends Thread implements cMsgSubscriptionHandle {
    /** Subscription's domain. */
    String domain;

    /** Subscription's subject. */
    String subject;

    /** Subscription's type. */
    String type;

    /** List of messages to be passed to the callback. */
    private LinkedBlockingQueue<cMsgMessageFull> messageQueue;

    /** List of messages that need to be dumped. */
    private ArrayList<cMsgMessageFull> dumpList;

    /** User argument to be passed to the callback. */
    private Object arg;

    /** Callback to be run. */
    cMsgCallbackInterface callback;

    /** Count of how many supplemental threads are currently active. */
    private AtomicInteger threads = new AtomicInteger();

    /** Place to temporarily store the returned message from a get. */
    cMsgMessageFull message;

    /** Number of messages passed to the callback. */
    long msgCount;

    /** Number of identical subscriptions made with the same callback and arg. */
    private int count;

    /** Setting this to true will kill this thread as soon as possible. */
    private volatile boolean dieNow;


    /**
     * This method kills this thread as soon as possible. If unsubscribe or disconnect
     * is called in a callback using the same connection, then the unsubscribe
     * or disconnect will interrupt the callback currently calling them.
     * To avoid this, set the argument to false.
     *
     * @param callInterrupt if true interrupt is called on callback thread,
     *                      else interrupt is not called.
     */
    public void dieNow(boolean callInterrupt) {
        dieNow = true;
        //System.out.println("CallbackThd: Will interrupt callback thread");
        if (callInterrupt) this.interrupt();
        //System.out.println("CallbackThd: Interrupted callback thread");
    }

    /**
     * Class to return info on callback's running environment to the callback.
     * In this case we tell callback the cue size.
     */
    private class myContext extends cMsgMessageContextDefault {
        public String getDomain()  { return domain; }
        public String getSubject() { return subject; }
        public String getType()    { return type; }
        public int getQueueSize()  { return messageQueue.size(); }
    }

    /** Object that tells callback user the context info including the cue size. */
    private myContext context;

    /**
     * Gets the number of messages passed to the callback.
     * @return number of messages passed to the callback
     */
    public long getMsgCount() {
        return msgCount;
    }

    /**
     * Gets the domain in which this subscription lives.
     * @return the domain in which this subscription lives
     */
    public String getDomain()  { return domain; }

    /**
     * Gets the subject of this subscription.
     * @return the subject of this subscription
     */
    public String getSubject() { return subject; }

    /**
     * Gets the type of this subscription.
     * @return the type of this subscription
     */
    public String getType()    { return type; }

    /**
     * Gets the number of messages in the queue.
     * @return number of messages in the queue
     */
    public int getQueueSize() {
        return messageQueue.size();
    }

    /**
     * Returns true if queue is full.
     * @return true if queue is full
     */
    public boolean isQueueFull() {
        return messageQueue.remainingCapacity() < 1;
    }

    /**
     * Clears the queue of all messages.
     */
    public void clearQueue() {
        messageQueue.clear();
    }

    /**
     * Gets the callback object.
     * @return user callback object
     */
    public cMsgCallbackInterface getCallback() {
        return callback;
    }

    /**
     * Gets the subscription's user object argument.
     * @return subscription's user object argument
     */
    public Object getUserObject() {
        return arg;
    }

    /**
     * Gets the number of identical subscriptions.
     * @return the number of identical subscriptions
     */
    public int getCount() {
        return count;
    }

    /**
     * Sets the number of identical subscriptions.
     * @param count the number of identical subscriptions
     */
    public void setCount(int count) {
        this.count = count;
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
                message = null;

                while (message == null) {
                    // die immediately if commanded to
                    if (dieNow || ++empty % 10 == 0) {
                        //System.out.println("t -= " + threads.getAndDecrement());
                        return;
                    }

                    try {
                        message = messageQueue.poll(200, TimeUnit.MILLISECONDS);
                        message.setContext(context);
                    }
                    catch (InterruptedException e) {
                    }
                }

                if (dieNow) {
                    return;
                }

                // run callback with copied msg so multiple callbacks don't clobber each other
                msgCount++;
                callback.callback(message.copy(), arg);
            }
        }
    }


    /**
     * Constructor.
     *
     * @param callback callback to be run when message arrives
     * @param arg user-supplied argument for callback
     * @param domain
     * @param subject
     * @param type
     */
    public cMsgCallbackThread(cMsgCallbackInterface callback, Object arg,
                              String domain, String subject, String type) {
        this.callback = callback;
        this.arg      = arg;
        this.domain   = domain;
        this.subject  = subject;
        this.type     = type;
        messageQueue  = new LinkedBlockingQueue<cMsgMessageFull>(callback.getMaximumQueueSize());
        dumpList      = new ArrayList<cMsgMessageFull>(callback.getSkipSize());
        count         = 1;
        context       = new myContext();

        start();
    }


    /**
     * Put message on a queue of messages waiting to be taken by the callback.
     * @param message message to be passed to callback
     */
    public void sendMessage(cMsgMessageFull message) {
        // if the queue is full ...
        if (!messageQueue.offer(message)) {
            // If we're being terminated, return. This way, we won't block.
            if (dieNow) return;

//System.out.println("QUEUE FULL");
            // if messages may not be skipped ...
            if (!callback.maySkipMessages()) {
                try {
                    // Block trying to put msg on queue. That should propagate
                    // back pressure through the whole cmsg system.
                    messageQueue.put(message);
                }
                catch (InterruptedException e) {
                }
            }
            else {
                messageQueue.drainTo(dumpList, callback.getSkipSize());
                dumpList.clear();
                messageQueue.offer(message);
//System.out.println("QUEUE DRAINED");
            }
        }
//            try {Thread.sleep(1);}
//            catch (InterruptedException e) {}

//if (messageQueue.size() > 0 && messageQueue.size() % 100 == 0) {
//    System.out.println("" + messageQueue.size());
//}
    }


    /** This method is executed as a thread which runs the callback method */
    public void run() {
        cMsgMessageFull message;
        int threadsAdded, threadsExisting, need, maxToAdd, wantToAdd;

        while (true) {
            threadsExisting = threads.get();
            threadsAdded = 0;
            message = null;

            if (!callback.mustSerializeMessages() &&
                threadsExisting < callback.getMaximumQueueSize() &&
                messageQueue.size() > callback.getMessagesPerThread()) {

                // find number of threads needed
                need = messageQueue.size()/callback.getMessagesPerThread();

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
                    //if (threadsAdded > 0) {
                    //    System.out.println("t += " + threads.getAndAdd(threadsAdded));
                    //}
                }
            }

            // While loop only necessary when calling messageQueue.poll.
            // That call was replaced as it had a mem leak in Java 1.5.
            while (message == null) {
                // die immediately if commanded to
                if (dieNow || Thread.currentThread().isInterrupted()) {
                    return;
                }

                // Cannot do a messageQueue.poll(1000, TimeUnit.MILLISECONDS)
                // because of a bug in Java 1.5 of a memory leak for a timeout in
                // a LinkedBlockingQueue.
                // BUGBUG
                try {
                    message = messageQueue.take();
                    message.setContext(context);
                }
                catch (InterruptedException e) {
                    //System.out.println("CallbackThd: Interrupted a messageQueue take");
                }
            }

            if (dieNow) {
                return;
            }

            // run callback with copied msg so multiple callbacks don't clobber each other
            msgCount++;
            callback.callback(message.copy(), arg);
        }
    }
}

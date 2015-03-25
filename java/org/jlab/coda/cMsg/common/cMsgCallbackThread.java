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
import org.jlab.coda.cMsg.cMsgCallbackInterface;

import java.util.*;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.CountDownLatch;
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

    private CountDownLatch latch;
    private volatile boolean pause;


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
        clearQueue();
        // wake up paused threads so they can die
        restart();
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
     * This method stops any further calling of the callback. Any threads currently running
     * the callback continue normally. Messages are still being delivered to this callback's
     * queue.
     */
    synchronized public void pause() {
        if (pause || dieNow) return;
        // this object is good for only 1 pause/resume cycle
        latch = new CountDownLatch(1);
        pause = true;
    }

    /**
     * This method (re)starts any calling of the callback delayed by the {@link #pause} method.
     * Would like to call this method "resume", but that name is taken by the "Thread" class.
     */
    synchronized public void restart() {
        if (!pause) return;
        pause = false;
        // tell things to finish waiting
        latch.countDown();
    }

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
     * Class defining threads (which can be run in parallel) that
     * will run the callback.
     */
    class WorkerThread extends Thread {

        /** Is this a temporary or permanent worker thread? */
        boolean permanent;

        WorkerThread() {
            this(false);
        }

        WorkerThread(boolean permanent) {
            this.permanent = permanent;
            setDaemon(true);
            start();
        }

        /** This method is executed as a thread which runs the callback method */
        public void run() {
            int empty;
            cMsgMessageFull message, msgCopy;

            while (true) {
                empty = 0;
                message = null;

                while (message == null) {
                    // die immediately if commanded to
                    if (dieNow) {
//System.out.println("Worker: die now 1");
                        return;
                    }

                    // remove temp worker thread if no work available
                    if (!permanent && ++empty % 10 == 0) {
//System.out.println("Worker: end this temp worker thread");
                        threads.decrementAndGet();
                        return;
                    }

                    // In Java 1.5, don't do a messageQueue.poll() because of a bug -
                    // a memory leak for a timeout in a LinkedBlockingQueue.
                    try {
                        message = messageQueue.poll(200, TimeUnit.MILLISECONDS);
                    }
                    catch (InterruptedException e) {
                    }
                }

//System.out.println("Worker, callback: got msg from Q");

                if (dieNow) {
//System.out.println("Worker: die now 2");
                    return;
                }

                // pause if necessary
                if (pause) {
                    try {
                        // wait till restart is called
//System.out.println("Worker: wait for latch");
                        latch.await();
//System.out.println("Worker: done waiting for latch");
                    }
                    catch (InterruptedException e) {
                    }

                    if (dieNow) {
//System.out.println("Worker: die now 3");
                        return;
                    }
                }
                // run callback with copied msg so multiple callbacks don't clobber each other
                msgCount++;
                msgCopy = message.copy();
                msgCopy.setContext(context);
                try {
                    if (subject.equalsIgnoreCase("ControlDesigner")) {
                        System.out.println("cMsgCbThd: RUN cb(" + callback + "), sub=" + subject + ", type=" + type);
                    }
                    callback.callback(msgCopy, arg);
                }
                catch (Exception e) {
                    // Ignore any exceptions thrown to avoid killing this thread.
                    // Don't want processing of messages to stop as that may back
                    // everything up the TCP pipe.
System.out.println("Error in callback: sub=" + subject + ",type=" + type + ",msg=" + e.getMessage());
                    e.printStackTrace();
                }
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

        setDaemon(true);

        // Start one permanent worker thread
        new WorkerThread(true);
        threads.incrementAndGet();
//System.out.println("Worker: +1");

        // If messages are NOT serialized, start up the thread
        // which manages the number of worker threads.
        if (!callback.mustSerializeMessages()) {
            start();
        }
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

System.out.println("cMsgCbThd: Q FULL");
            // if messages may not be skipped ...
            if (!callback.maySkipMessages()) {
                try {
                    // Block trying to put msg on queue. That will propagate
                    // back pressure through the whole cmsg system.
                    while (!messageQueue.offer(message, 10, TimeUnit.SECONDS)) {
                        System.out.println("cMsgCbThd: can't place msg on full cb Q, wait 10 sec,");
                        System.out.println("Q size = " + messageQueue.size() +
                                           "subject = " + subject + ", type = " + type);
                    }
                }
                catch (InterruptedException e) {
                }
            }
            else {
                messageQueue.drainTo(dumpList, callback.getSkipSize());
                dumpList.clear();
                messageQueue.offer(message);
System.out.println("cMsgCbThd: Q DRAINED & new msg placed on Q");
            }
        }
//        else {
//            System.out.println("cMsgCallbackThread: msg placed on callback's Q");
//        }
//            try {Thread.sleep(1);}
//            catch (InterruptedException e) {}

//if (messageQueue.size() > 0 && messageQueue.size() % 100 == 0) {
//    System.out.println("" + messageQueue.size());
//}
    }


    /** This method is executed as a thread which starts up the right number of worker threads */
    public void run() {
        boolean qSizeNotChanged;
        int threadsAdded, threadsExisting, need, maxToAdd, wantToAdd, qSize;

        while (true) {

            if (dieNow) {
//System.out.println("CB: -1");
                return;
            }

            qSize = messageQueue.size();
            threadsExisting = threads.get();
//System.out.println(threadsExisting + " threads, Q size = " + qSize);

            if (threadsExisting < callback.getMaximumThreads() &&
                qSize > callback.getMessagesPerThread()) {

                // find number of threads needed
                need = qSize/callback.getMessagesPerThread();
//System.out.println("need  " + need + " threads");

                // at this point, # of threads may only decrease, it is only increased below

                // add more threads if necessary
                if (need > threadsExisting) {
                    // maximum # of threads that can be added w/o exceeding limit
                    maxToAdd  = callback.getMaximumThreads() - threadsExisting;

                    // number of threads we want to add to handle the load
                    wantToAdd = need - threadsExisting;

                    // number of threads that we will add
                    threadsAdded = maxToAdd > wantToAdd ? wantToAdd : maxToAdd;
//System.out.println("starting " + threadsAdded + " more thread(s)");

                    for (int i=0; i < threadsAdded; i++) {
                        new WorkerThread();
                        // Update the number of threads running.
                        // Do it here instead of in worker thread
                        // since that takes some time to start up.
                        threads.incrementAndGet();
//System.out.println("Worker: +1");
                    }
                }
            }

            // sleep until Q size changes
            qSizeNotChanged = qSize - messageQueue.size() == 0;
            while (qSizeNotChanged) {
                if (dieNow) {
//System.out.println("CB: -1");
                    return;
                }
                try {Thread.sleep(40); }  // sleep 40 millisec
                catch (InterruptedException e) {}
                qSizeNotChanged = qSize - messageQueue.size() == 0;
            }
        }
    }
}

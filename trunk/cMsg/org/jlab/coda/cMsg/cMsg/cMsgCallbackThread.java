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

package org.jlab.coda.cMsg.cMsg;

import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgCallback;
import org.jlab.coda.cMsg.cMsgException;

import java.util.LinkedList;

/**
 * This class is used to run a message callback in its own thread.
 * The thread is self-starting and waits to execute the callback.
 * All it needs is a notify.
 */
public class cMsgCallbackThread extends Thread {
    /** List of ordered messages to be passed to the callback. */
    private LinkedList messageList;

    //private int lastOdd=1,lastEven=0, num;
    private int size;

    /** User argument to be passed to the callback. */
    private Object arg;

    /** Callback to be run. */
    cMsgCallback callback;

    /** Setting this to true will kill this thread. */
    private boolean killThread;

    private Object sync = new Object();

    private int threads;

    /** Kills this thread. */
    public void killThread() {
        killThread = true;
    }


    class SupplementalThread extends Thread {

        SupplementalThread() {
            setDaemon(true);
            start();
        }

        /** This method is executed as a thread which runs the callback method */
        public void run() {
            cMsgMessage message;
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

                    if (killThread) {
                        return;
                    }
                }

                if (killThread) {
                    return;
                }

                // grab a message off the list if possible
                synchronized (messageList) {
                    if (messageList.size() > 0) {
                        message = (cMsgMessage) messageList.remove(0);
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

    /** Set message to be given to the callback. */
    public void sendMessage(cMsgMessage message) throws cMsgException {
        synchronized (messageList) {
            messageList.add(message);
            size = messageList.size();
            if (size%1000 == 0) {
                //System.out.println(size+"");
            }
            if (size > 60000) {
                if (callback.maySkipMessages()) {
                    messageList.subList(0, size-10000).clear();
                }
                else {
                    throw new cMsgException("too many messages for callback to handle");
                }
            }
        }
    }

    cMsgCallbackThread(cMsgCallback callback, Object arg) {
        this.callback = callback;
        this.arg = arg;
        messageList = new LinkedList();
        start();
    }

    /** This method is executed as a thread which runs the callback method */
    public void run() {
        cMsgMessage message;
        SupplementalThread thd;
        int threadsAdded;

        while (true) {
            threadsAdded = 0;

            if (!callback.mustSerializeMessages() && threads < 1000 && messageList.size() > 50) {
                // find number of threads needed (1 per 50 messages)
                int need = messageList.size()/50;

                // add more threads if necessary
                if (need > threads) {
                    threadsAdded = need - threads;
                    for (int i=0; i < threadsAdded; i++) {
                        thd = new SupplementalThread();
                    }

                    // do the following bookkeeping under mutex protection
                    synchronized (sync) {
                        threads += threadsAdded;
                        //System.out.println("t += " + threads);
                    }
                }
            }

            // only wait if no messages to run callback on
            while (messageList.size() < 1) {
                try {
                    // Wait to run the callback until notified by client's listening thread
                    // that there are messages to run the callback on.
                    synchronized (this) {
                        wait();
                    }
                }
                catch (InterruptedException e) {
                    e.printStackTrace();
                    System.exit(-1);
                }
                if (killThread) {
                    return;
                }
            }

            if (killThread) {
                return;
            }

            // grab a message off the list if possible
            synchronized (messageList) {
                // Run callback method with proper argument
                if (messageList.size() > 0) {
                    message = (cMsgMessage) messageList.remove(0);
                }
                else {
                    message = null;
                }
            }

            if (message != null) {
                callback.callback(message, arg);
                /*
                num = Integer.parseInt(message.getText());
                if (num % 2 > 0) {
                    if (num - lastOdd != 2) {
                        System.out.println("         " + lastOdd + " -> " + message.getText());
                    }
                    lastOdd = num;
                }
                else {
                    if (num - lastEven != 2) {
                        System.out.println(lastEven + " -> " + message.getText());
                    }
                    lastEven = num;
                }
                */
            }
        }
    }
}

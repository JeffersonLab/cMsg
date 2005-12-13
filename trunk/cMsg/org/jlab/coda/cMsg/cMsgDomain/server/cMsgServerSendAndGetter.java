/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 16-Nov-2005, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.server;

import org.jlab.coda.cMsg.cMsgDomain.cMsgNotifier;
import org.jlab.coda.cMsg.cMsgDomain.cMsgHolder;
import org.jlab.coda.cMsg.cMsgDomain.cMsgSubscription;
import org.jlab.coda.cMsg.cMsgClientInfo;
import org.jlab.coda.cMsg.cMsgCallbackAdapter;
import org.jlab.coda.cMsg.cMsgException;

import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

/**
 * This class handles a server client's sendAndGet request and propagates it to all the
 * connected servers. It takes care of all the details of getting a response and forwarding
 * that to the client as well as cancelling the request to servers after the first
 * response is received.
 */
public class cMsgServerSendAndGetter implements Runnable {
    static int counter;
    /**
     * Wait on this object which tells us when a matching message has been sent
     * to a subscribeAndGet so we can notify bridges to cancel their subscriptions
     * (which are used to implement remote subscribeAndGets).
     */
    cMsgNotifier notifier;

    /** Contains the information to use for unSendAndGetting. */

    /** Contains all subscriptions made by this server. */
    cMsgCallbackAdapter cb;
    ConcurrentHashMap sendAndGetters;


    public cMsgServerSendAndGetter(cMsgNotifier notifier,
                                    cMsgCallbackAdapter cb,
                                    ConcurrentHashMap sendAndGetters) {
        this.cb = cb;
        this.notifier = notifier;
        this.sendAndGetters = sendAndGetters;
    }

    public void run() {
        counter++;
        if (counter > 10) {
            System.out.println("getter threads = " + counter);
        }
        // Wait for a signal to cancel remote subscriptions
//System.out.println("cMsgServerSendAndGetter object: Wait on notifier");
        try {
            if (Thread.currentThread().isInterrupted()) {
                return;
            }
            notifier.latch.await();
        }
        catch (InterruptedException e) {
            // If we've been interrupted it's because the client has died
            // and the server is cleaning up all the threads (including
            // this one).
            return;
        }

//System.out.println("cMsgServerSendAndGetter object: notifier fired");

            for (cMsgServerBridge b : cMsgNameServer.bridges.values()) {
                //System.out.println("Domain Server: call bridge subscribe");
                try {
                    // only cloud members please
                    if (b.getCloudStatus() != cMsgNameServer.INCLOUD) {
                        continue;
                    }
//System.out.println("cMsgServerSendAndGetter object: unSendAndGet to bridge " + b.server +
//                   ", id = " + notifier.id);

                    b.unSendAndGet(notifier.id);
                }
                catch (cMsgException e) {
                    // e.printStackTrace();
                    // ignore exceptions as server is probably gone and so no matter
                }
            }
//System.out.println("");

            // Clear entry in hash table of subAndGetters
            sendAndGetters.remove(notifier.id);
        counter--;

    }



}

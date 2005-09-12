/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 30-Aug-2005, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.server;

import org.jlab.coda.cMsg.cMsgDomain.cMsgHolder;
import org.jlab.coda.cMsg.cMsgDomain.cMsgNotifier;
import org.jlab.coda.cMsg.cMsgException;
import java.util.HashSet;

/**
 * This class handles a client's subscribeAndGet request and propagates it to all the
 * connected servers. It takes care of all the details of getting a response and forwarding
 * that to the client as well as cancelling the request to servers after the first
 * response is received.
 */
public class cMsgServerSubscribeAndGetter extends Thread {

    /**
     * Wait on this object which tells us when a matching message has been sent
     * to a subscribeAndGet so we can notify bridges to cancel their subscriptions
     * (which are used to implement remote subscribeAndGets).
     */
    cMsgNotifier notifier;

    /** Set of bridges for which subscriptions were made in implementing a subscribeAndGet. */
    HashSet<cMsgServerBridge> serverSubs;

    /** Contains the subscription information to use for unsubscribing. */
    cMsgHolder holder;

    public cMsgServerSubscribeAndGetter(cMsgNotifier notifier, cMsgHolder holder,
                                        HashSet<cMsgServerBridge> serverSubs) {
        this.notifier   = notifier;
        this.holder     = holder;
        this.serverSubs = serverSubs;

        // die if main thread dies
        setDaemon(true);

        // run thread
        this.start();
    }

    public void run() {
        // Wait for a signal to cancel remote subscriptions
System.out.println("cMsgServerSubscribeAndGetter object: Wait on notifier");
        try {notifier.latch.await();}
        catch (InterruptedException e) {}

System.out.println("cMsgServerSubscribeAndGetter object: notifier fired");
        if (serverSubs.size() > 0) {
            for (cMsgServerBridge b : serverSubs) {
                //System.out.println("Domain Server: call bridge subscribe");
                try {
//System.out.println("cMsgServerSubscribeAndGetter object: unsubscribe for " + b.server);
                    b.unsubscribe(holder.subject, holder.type, holder.namespace);
                }
                catch (cMsgException e) {
                    e.printStackTrace();
                    // ignore exceptions as server is probably gone and so no matter
                }
            }
        }
    }


}

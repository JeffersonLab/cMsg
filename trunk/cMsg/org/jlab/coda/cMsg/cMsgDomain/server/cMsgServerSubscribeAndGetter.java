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
import org.jlab.coda.cMsg.cMsgDomain.cMsgSubscription;
import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgClientInfo;

import java.util.Iterator;
import java.util.Set;

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

    /** Contains the subscription information to use for unsubscribing. */
    cMsgHolder holder;

    Set subscriptions;
    cMsgClientInfo info;

    public cMsgServerSubscribeAndGetter(cMsgNotifier notifier, cMsgHolder holder,
                                        Set subscriptions, cMsgClientInfo info) {
        this.notifier   = notifier;
        this.holder     = holder;
        this.subscriptions = subscriptions;
        this.info = info;

        // die if main thread dies
        setDaemon(true);

        // run thread
        this.start();
    }

    public void run() {
        // Wait for a signal to cancel remote subscriptions
//System.out.println("cMsgServerSubscribeAndGetter object: Wait on notifier");
        try {notifier.latch.await();}
        catch (InterruptedException e) {}

//System.out.println("cMsgServerSubscribeAndGetter object: notifier fired");

        cMsgDomainServer.joinCloudLock.lock();
        try {
            for (cMsgServerBridge b : cMsgNameServer.bridges.values()) {
                //System.out.println("Domain Server: call bridge subscribe");
                try {
                    // only cloud members please
                    if (b.getCloudStatus() != cMsgNameServer.INCLOUD) {
                        continue;
                    }
//System.out.println("cMsgServerSubscribeAndGetter object: unsubscribe to bridge " + b.server +
//                   "  sub = " + holder.subject + ", type = " + holder.type + ", ns = " + holder.namespace);
                    b.unsubscribe(holder.subject, holder.type, holder.namespace);

                }
                catch (cMsgException e) {
                    e.printStackTrace();
                    // ignore exceptions as server is probably gone and so no matter
                }
            }

            cMsgSubscription sub = null;
            Iterator it = subscriptions.iterator();
            while (it.hasNext()) {
                sub = (cMsgSubscription) it.next();
                if (sub.getSubject().equals(holder.subject) &&
                        sub.getType().equals(holder.type)) {
//System.out.println("cMsgServerSubscribeAndGetter object: remove sub&Get from sub object");
                    sub.removeSubAndGetter(info);
                    break;
                }
            }

            // If a msg was sent and sub removed simultaneously while a sub&Get (on client)
            // timed out so an unSub&Get was sent, ignore the unSub&get.
//System.out.println("cMsgServerSubscribeAndGetter object: # of subscribers= " + sub.numberOfSubscribers());
            if ((sub != null) && (sub.numberOfSubscribers() < 1)) {
//System.out.println("cMsgServerSubscribeAndGetter object: remove whole subscription");
                subscriptions.remove(sub);
            }
//System.out.println("");
        }
        finally {
            cMsgDomainServer.joinCloudLock.unlock();
        }

    }


}

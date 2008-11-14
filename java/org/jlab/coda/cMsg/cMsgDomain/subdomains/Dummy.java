/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 30-Jan-2006, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.subdomains;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgSubdomainAdapter;
import org.jlab.coda.cMsg.common.cMsgDeliverMessageInterface;
import org.jlab.coda.cMsg.common.cMsgClientInfo;
import org.jlab.coda.cMsg.common.cMsgMessageFull;


/**
 * This class is a subdomain which does nothing. It was used for finding a
 * memory leak in the cMsg server.
 */
public class Dummy extends cMsgSubdomainAdapter {

    private long counter;
    private long now;
    private cMsgDeliverMessageInterface deliverer;
    

    public Dummy() {
        now = System.currentTimeMillis();
    }

    public void setUDLRemainder(String UDLRemainder) {
        return;
    }

    public void registerClient(cMsgClientInfo info) throws cMsgException {
        // Get an object enabling this handler to communicate
        // with only this client in this cMsg subdomain.
        deliverer = info.getDeliverer();
        return;
    }

    synchronized public void handleSendRequest(cMsgMessageFull message) {
        // for printing out request cue size periodically
        counter++;
        long t = System.currentTimeMillis();
        if (now + 10000L <= t) {
            System.out.println("Message count = " + counter);
            now = t;
        }
        return;
    }

    public int handleSyncSendRequest(cMsgMessageFull message) {
        return 0;
    }


    public void handleSubscribeAndGetRequest(String subject, String type, int id) {
        return;
    }


    public void handleSendAndGetRequest(cMsgMessageFull message) {
        return;
    }


    public void handleUnSendAndGetRequest(int id) {
        return;
    }


    public void handleUnsubscribeAndGetRequest(String subject, String type, int id) {
        return;
    }


    public void handleSubscribeRequest(String subject, String type, int id) {
        return;
    }

    public void handleUnsubscribeRequest(String subject, String type, int id) {
        return;
    }


    public void handleShutdownClientsRequest(String client, boolean includeMe) {
        return;
    }


    public void handleClientShutdown() {
        return;
    }


    public boolean hasSend() { return true; }

    public boolean hasSyncSend() { return true; }

    public boolean hasSubscribeAndGet() { return true; }

    public boolean hasSendAndGet() { return true; }

    public boolean hasSubscribe() { return true; }

    public boolean hasUnsubscribe() { return true; }

    public boolean hasShutdown() { return true; }




}

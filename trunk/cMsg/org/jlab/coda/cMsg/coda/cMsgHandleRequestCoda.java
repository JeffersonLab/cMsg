package org.jlab.coda.cMsg.coda;

import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgHandleRequests;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Jul 14, 2004
 * Time: 2:34:00 PM
 * To change this template use File | Settings | File Templates.
 */
public class cMsgHandleRequestCoda implements cMsgHandleRequests {
    /** Method to handle message sent by doman client. */
    public void handleSendRequest(cMsgMessage msg) {
        System.out.println("handling send request");
    }

    /** Method to handle subscribe request sent by doman client. */
    public void handleSubscribeRequest(String subject, String type) {
        System.out.println("handling subscribe request");
    }

    /** Method to handle unsubscribe request sent by doman client. */
    public void handleUnsubscribeRequest(String subject, String type) {
        System.out.println("handling unsubscribe request");
    }

    /** Method to handle keepalive sent by doman client
     *  checking to see if the socket is still up. */
    public void handleKeepAlive() {
        System.out.println("handling keep alive");
    }

    /** Method to handle a disconnect request sent by doman client. */
    public void handleDisconnect() {
        System.out.println("handling shutdown");
    }

    /** Method to handle a shutdown request sent by doman client. */
    public void handleShutdown() {
        System.out.println("handling shutdown");
    }
}

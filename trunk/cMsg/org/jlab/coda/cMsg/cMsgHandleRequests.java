package org.jlab.coda.cMsg;

/**
 * This interface is for an object that a domain server uses to respond to client demands.
 *
 *  Created by IntelliJ IDEA.
 * User: timmer
 * Date: Jul 14, 2004
 * Time: 1:16:47 PM
 */
public interface cMsgHandleRequests {
    /** Method to handle message sent by doman client. */
    public void handleSendRequest(cMsgMessage msg);

    /** Method to handle subscribe request sent by doman client. */
    public void handleSubscribeRequest(String subject, String type);

    /** Method to handle unsubscribe request sent by doman client. */
    public void handleUnsubscribeRequest(String subject, String type);

    /** Method to handle keepalive sent by doman client
     *  checking to see if the socket is still up. */
    public void handleKeepAlive();

    /** Method to handle a disconnect request sent by doman client. */
    public void handleDisconnect();

    /** Method to handle a shutdown request sent by doman client. */
    public void handleShutdown();
}

/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 7-Jul-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

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
    public void registerClient(String name, String host, int port) throws cMsgException;

    /** Method to handle message sent by doman client. */
    public void unregisterClient(String name);

    /** Method to handle message sent by doman client. */
    public void handleSendRequest(String name, cMsgMessage msg) throws cMsgException;

    /** Method to handle subscribe request sent by doman client. */
    public void handleSubscribeRequest(String name, String subject, String type,
                                       int receiverSubscribeId) throws cMsgException;

    /** Method to handle unsubscribe request sent by doman client. */
    public void handleUnsubscribeRequest(String name, String subject, String type) throws cMsgException;

    /** Method to handle keepalive sent by doman client
     *  checking to see if the socket is still up. */
    public void handleKeepAlive(String name);

    /** Method to handle a disconnect request sent by doman client. */
    public void handleDisconnect(String name);

    /** Method to handle a shutdown request sent by doman client. */
    public void handleShutdown(String name);
}

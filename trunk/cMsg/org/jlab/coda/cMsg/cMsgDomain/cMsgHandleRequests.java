/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 14-Jul-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgMessage;

/**
 * This interface is an API for an object that a domain server
 * uses to respond to client demands. An implementation of this
 * interface handles all communication with a particular subdomain
 * (such as SmartSockets or JADE agents).
 *
 * Implementors of this interface must understand that each
 * client using cMsg will have its own handler object from a class
 * implementing this interface. Several clients may concurrently use
 * objects of the same class. Thus implementations must be thread-safe.
 * Furthermore, when the name server shuts dowm, the method handleServerShutdown
 * may be executed more than once for the same reason.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public interface cMsgHandleRequests {
    /**
     * Method to give the subdomain handler the appropriate part
     * of the UDL the client used to talk to the domain server.
     *
     * @param UDLRemainder last part of the UDL appropriate to the subdomain handler
     * @throws org.jlab.coda.cMsg.cMsgException
     */
    public void setUDLRemainder(String UDLRemainder) throws cMsgException;


    /**
     * Method to register domain client.
     *
     * @param info information about client
     * @throws cMsgException
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException;


    /**
     * Method to handle message sent by domain client.
     *
     * @param message message from sender
     * @throws cMsgException
     */
    public void handleSendRequest(cMsgMessage message) throws cMsgException;


    /**
     * Method to handle message sent by domain client in synchronous mode.
     * It requries an integer response from the subdomain handler.
     *
     * @param message message from sender
     * @return response from subdomain handler
     * @throws cMsgException
     */
    public int handleSyncSendRequest(cMsgMessage message) throws cMsgException;


    /**
     * Method to synchronously get a single message from the server for a given
     * subject and type -- perhaps from a specified receiver.
     *
     * @param message message requesting what sort of message to get
     * @throws cMsgException
     */
    public void handleGetRequest(cMsgMessage message) throws cMsgException;


    /**
     * Method to handle unget request sent by domain client (hidden from user).
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     * @throws cMsgException
     */
    public void handleUngetRequest(String subject, String type) throws cMsgException;


    /**
      * Method to handle subscribe request sent by domain client.
      *
      * @param subject message subject to subscribe to
      * @param type message type to subscribe to
      * @param receiverSubscribeId message id refering to these specific subject and type values
      * @throws cMsgException
      */
    public void handleSubscribeRequest(String subject, String type,
                                       int receiverSubscribeId) throws cMsgException;

    /**
     * Method to handle unsubscribe request sent by domain client.
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     * @param receiverSubscribeId message id refering to these specific subject and type values
     * @throws cMsgException
     */
    public void handleUnsubscribeRequest(String subject, String type,
                                         int receiverSubscribeId) throws cMsgException;


    /**
     * Method to handle keepalive sent by domain client checking to see
     * if the domain server socket is still up.
     *
     * @throws cMsgException
     */
    public void handleKeepAlive() throws cMsgException;


    /**
     * Method to handle a client or domain server down.
     *
     * @throws cMsgException
     */
    public void handleClientShutdown() throws cMsgException;

    /**
     * Method to handle a complete name server down.
     *
     * @throws cMsgException
     */
    public void handleServerShutdown() throws cMsgException;


    /**
     * Method to tell if the "send" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSendRequest}
     * method.
     *
     * @return true if send implemented in {@link #handleSendRequest}
     */
    public boolean hasSend();


    /**
     * Method to tell if the "syncSend" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSyncSendRequest}
     * method.
     *
     * @return true if send implemented in {@link #handleSyncSendRequest}
     */
    public boolean hasSyncSend();


    /**
     * Method to tell if the "get" cMsg API function is implemented
     * by this interface implementation in the {@link #handleGetRequest}
     * method.
     *
     * @return true if get implemented in {@link #handleGetRequest}
     */
    public boolean hasGet();


    /**
     * Method to tell if the "subscribe" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSubscribeRequest}
     * method.
     *
     * @return true if subscribe implemented in {@link #handleSubscribeRequest}
     */
    public boolean hasSubscribe();


    /**
     * Method to tell if the "unsubscribe" cMsg API function is implemented
     * by this interface implementation in the {@link #handleUnsubscribeRequest}
     * method.
     *
     * @return true if unsubscribe implemented in {@link #handleUnsubscribeRequest}
     */
    public boolean hasUnsubscribe();


}

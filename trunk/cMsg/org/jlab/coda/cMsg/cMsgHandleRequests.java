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

package org.jlab.coda.cMsg;

/**
 * This interface is an API for an object that a domain server
 * uses to respond to client demands. An implementation of this
 * interface handles all communication with a particular subdomain
 * (such as SmartSockets or JADE agents).
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
     * @throws cMsgException
     */
    public void setUDLRemainder(String UDLRemainder) throws cMsgException;


    /**
     * Method to register domain client.
     *
     * @param name name of client
     * @param host host client is running on
     * @param port port client is listening on
     * @throws cMsgException
     */
    public void registerClient(String name, String host, int port) throws cMsgException;


    /**
     * Method to see if domain client is registered.
     *
     * @param name name of client
     * @return true if client registered, false otherwise
     * @throws cMsgException
     */
    public boolean isRegistered(String name) throws cMsgException;


    /**
     * Method to handle message sent by domain client.
     *
     * @param name name of client
     * @param msg message from sender
     * @throws cMsgException
     */
    public void handleSendRequest(String name, cMsgMessage msg) throws cMsgException;


    /**
     * Method to get a single message from the server for a given
     * subject and type.
     *
     * @param name name of client
     * @param subject subject of message to get
     * @param type type of message to get
     * @return cMsgMessage message obtained by this get
     * @throws cMsgException
     */
    public cMsgMessage handleGetRequest(String name, String subject, String type) throws cMsgException;


    /**
      * Method to handle subscribe request sent by domain client.
      *
      * @param name name of client
      * @param subject message subject to subscribe to
      * @param type message type to subscribe to
      * @param receiverSubscribeId message id refering to these specific subject and type values
      * @throws cMsgException
      */
    public void handleSubscribeRequest(String name, String subject, String type,
                                       int receiverSubscribeId) throws cMsgException;

    /**
     * Method to handle sunsubscribe request sent by domain client.
     *
     * @param name name of client
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     * @throws cMsgException
     */
    public void handleUnsubscribeRequest(String name, String subject, String type) throws cMsgException;


    /**
     * Method to handle keepalive sent by domain client checking to see
     * if the domain server socket is still up.
     *
     * @param name name of client
     * @throws cMsgException
     */
    public void handleKeepAlive(String name) throws cMsgException;


    /**
     * Method to handle a domain server down.
     *
     * @param name name of client
     * @throws cMsgException
     */
    public void handleShutdown(String name) throws cMsgException;

}

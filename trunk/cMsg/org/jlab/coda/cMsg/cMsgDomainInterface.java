/*----------------------------------------------------------------------------*
*  Copyright (c) 2004        Southeastern Universities Research Association, *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    E.Wolin, 29-Jun-2004, Jefferson Lab                                     *
*                                                                            *
*    Authors: Elliott Wolin                                                  *
*             wolin@jlab.org                    Jefferson Lab, MS-6B         *
*             Phone: (757) 269-7365             12000 Jefferson Ave.         *
*             Fax:   (757) 269-5800             Newport News, VA 23606       *
*                                                                            *
*             Carl Timmer                                                    *
*             timmer@jlab.org                   Jefferson Lab, MS-6B         *
*             Phone: (757) 269-5130             12000 Jefferson Ave.         *
*             Fax:   (757) 269-5800             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;

import java.lang.*;


/**
 * This interface is the cMsg client API for all domains.
 *
 * @author Elliott Wolin
 * @author Carl Timmer
 * @version 1.0
 */
public interface cMsgDomainInterface {

    /**
     * Method to connect to a particular domain server.
     *
     * @throws cMsgException
     */
    public void connect() throws cMsgException;

    /**
     * Method to close the connection to the domain server. This method results in this object
     * becoming functionally useless.
     *
     * @throws cMsgException
     */
    public void disconnect() throws cMsgException;

    /**
     * Method to determine if this object is still connected to the domain server or not.
     *
     * @return true if connected to domain server, false otherwise
     */
    public boolean isConnected();

    /**
     * Method to send a message to the domain server for further distribution.
     *
     * @param message message
     * @throws cMsgException
     */
    public void send(cMsgMessage message) throws cMsgException;

    /**
     * Method to send a message to the domain server for further distribution
     * and wait for a response from the subdomain handler that got it.
     *
     * @param message message
     * @return response from subdomain handler
     * @throws cMsgException
     */
    public int syncSend(cMsgMessage message) throws cMsgException;

    /**
     * Method to force cMsg client to send pending communications with domain server.
     * @throws cMsgException
     */
    public void flush() throws cMsgException;

    /**
     * This method is like a one-time subscribe. The server grabs the first incoming
     * message of the requested subject and type and sends that to the caller.
     *
     * @param subject subject of message desired from server
     * @param type type of message desired from server
     * @param timeout time in milliseconds to wait for a message
     * @return response message
     * @throws cMsgException
     */
    public cMsgMessage subscribeAndGet(String subject, String type, int timeout) throws cMsgException;

    /**
     * The message is sent as it would be in the {@link #send} method. The server notes
     * the fact that a response to it is expected, and sends it to all subscribed to its
     * subject and type. When a marked response is received from a client, it sends that
     * first response back to the original sender regardless of its subject or type.
     *
     * @param message message sent to server
     * @param timeout time in milliseconds to wait for a reponse message
     * @return response message
     * @throws cMsgException
     */
    public cMsgMessage sendAndGet(cMsgMessage message, int timeout) throws cMsgException;

    /**
     * Method to subscribe to receive messages of a subject and type from the domain server.
     *
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @param userObj any user-supplied object to be given to the callback method as an argument
     * @throws cMsgException
     */
    public void subscribe(String subject, String type, cMsgCallbackInterface cb, Object userObj) throws cMsgException;


    /**
     * Method to unsubscribe a previous subscription to receive messages of a subject and type
     * from the domain server. Since many subscriptions may be made to the same subject and type
     * values, but with different callabacks, the callback must be specified so the correct
     * subscription can be removed.
     *
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @throws cMsgException
     */
    public void unsubscribe(String subject, String type, cMsgCallbackInterface cb) throws cMsgException;

    /**
     * Method to start or activate the subscription callbacks.
     */
    public void start();

    /**
     * Method to stop or deactivate the subscription callbacks.
     */
    public void stop();

    /**
     * Get the name of the domain connected to.
     * @return domain name
     */
    public String getDomain();

    /**
     * Get the name of the client.
     * @return client's name
     */
    public String getName();

    /**
     * Set the name of the client.
     * @param name name of client
     */
    public void setName(String name);

    /**
     * Get the client's description.
     * @return client's description
     */
    public String getDescription();

    /**
     * Set the description of the client.
     * @param description description of client
     */
    public void setDescription(String description);

    /**
     * Get the client's UDL.
     * @return client's DUL
     */
    public String getUDL();

    /**
     * Set the UDL of the client.
     *
     * @param UDL UDL of client UDL
     */
    public void setUDL(String UDL);

    /**
     * Get the client's UDL remainder.
     * @return client's DUL remainder
     */
    public String getUDLRemainder();

    /**
     * Set the UDL remainder of the client. The cMsg class parses the
     * UDL and strips off the beginning domain information. The remainder is
     * passed on to the domain implementations (implementors of this interface).
     *
     * @param UDLRemainder UDL remainder of client UDL
     */
    public void setUDLRemainder(String UDLRemainder);

    /**
     * Get the host the client is running on.
     * @return client's host
     */
    public String getHost();

    /**
     * Get boolean tells whether callbacks are activated or not. The
     * start and stop methods activate and deactivate the callbacks.
     * @return true if callbacks are activated, false if they are not
     */
    public boolean isReceiving();
}

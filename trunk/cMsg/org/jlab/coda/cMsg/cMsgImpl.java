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
 * This class provides a very basic (non-functional/dummy) implementation
 * of the cMsgInterface interface. Its non-getter/setter methods throw a
 * cMsgException saying that the method is not implemented. It is like the
 * swing Adapter classes.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgImpl implements cMsgInterface {

    /** Boolean indicating whether this client is connected to the domain server or not. */
    protected volatile boolean connected;

    /** Boolean indicating whether this client's callbacks are active or not. */
    protected boolean receiving;

    /**
     * The Uniform Domain Locator which tells the location of a name server. It is of the
     * form cMsg:<domain>://<host>:<port>/<subdomain>/remainder
     */
    protected String  UDL;

    /** String containing the remainder part of the UDL. */
    protected String  UDLremainder;

    /** Domain being connected to. */
    protected String  domain;

    /** Subdomain whose plugin is being used. */
    protected String  subdomain;

    /** Name of this client. Must be unique in the domain. */
    protected String  name;

    /** Description of the client. */
    protected String  description;

    /** Host the client is running on. */
    protected String  host;


//-----------------------------------------------------------------------------


    /**
     * Method to determine if this object is still connected to the domain server or not.
     *
     * @return true if connected to domain server, false otherwise
     */
    public boolean isConnected() {
        return connected;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to connect to the domain server.
     *
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void connect() throws cMsgException {
        throw new cMsgException("connect is not implemented yet");
    }


//-----------------------------------------------------------------------------


    /**
     * Method to close the connection to the domain server. This method results in this object
     * becoming functionally useless.
     *
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void disconnect() throws cMsgException {
        throw new cMsgException("disconnect is not implemented yet");
    }


//-----------------------------------------------------------------------------


    /**
     * Method to send a message to the domain server for further distribution.
     *
     * @param message message
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void send(cMsgMessage message) throws cMsgException {
        throw new cMsgException("send is not implemented yet");
    }


//-----------------------------------------------------------------------------

    /**
     * Method to send a message to the domain server for further distribution
     * and wait for a response from the subdomain handler that got it.
     *
     * @param message message
     * @return response from subdomain handler
     * @throws cMsgException
     */
    public int syncSend(cMsgMessage message) throws cMsgException {
        throw new cMsgException("syncSend is not implemented yet");
    }


//-----------------------------------------------------------------------------


    /**
     * Method to force cMsg client to send pending communications with domain server.
     *
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void flush() throws cMsgException {
        throw new cMsgException("flush is not implemented yet");
    }


//-----------------------------------------------------------------------------


    /**
     * This method does two separate things depending on the specifics of message in the
     * argument. If the message to be sent has its "getRequest" field set to be true using
     * {@link cMsgMessage#isGetRequest()}, then the message is sent as it would be in the
     * {@link #send} method. The server notes the fact that a response to it is expected,
     * and sends it to all subscribed to its subject and type. When a marked response is
     * received from a client, it sends that first response back to the original sender
     * regardless of its subject or type.
     *
     * In a second usage, if the message did NOT set its "getRequest" field to be true,
     * then the server grabs the first incoming message of the requested subject and type
     * and sends that to the original sender in response to the get.
     *
     * @param message message sent to server
     * @param timeout time in milliseconds to wait for a reponse message
     * @return response message
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public cMsgMessage get(cMsgMessage message, int timeout) throws cMsgException {
        throw new cMsgException("get is not implemented yet");
    }


//-----------------------------------------------------------------------------


    /**
     * Method to subscribe to receive messages of a subject and type from the domain server.
     *
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @param userObj any user-supplied object to be given to the callback method as an argument
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void subscribe(String subject, String type, cMsgCallback cb, Object userObj) throws cMsgException {
        throw new cMsgException("subscribe is not implemented yet");
    }


//-----------------------------------------------------------------------------


    /**
     * Method to unsubscribe a previous subscription to receive messages of a subject and type
     * from the domain server. Since many subscriptions may be made to the same subject and type
     * values, but with different callbacks, the callback must be specified so the correct
     * subscription can be removed.
     *
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void unsubscribe(String subject, String type, cMsgCallback cb) throws cMsgException {
        throw new cMsgException("unsubscribe is not implemented yet");
    }


//-----------------------------------------------------------------------------


    /**
     * Method to start or activate the subscription callbacks.
     */
    public void start() {
	    receiving = true;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to stop or deactivate the subscription callbacks.
     */
    public void stop() {
        receiving = false;
    }


//-----------------------------------------------------------------------------


    /**
     * Get the name of the domain connected to.
     * @return domain name
     */
    public String getDomain() {return(domain);}


//-----------------------------------------------------------------------------


    /**
     * Get the name of the subdomain whose plugin is being used.
     * @return subdomain name
     */
    public String getSubdomain() {return(subdomain);}


//-----------------------------------------------------------------------------


    /**
     * Get the name of the client.
     * @return client's name
     */
    public String getName() {return(name);}


//-----------------------------------------------------------------------------


    /**
     * Get the client's description.
     * @return client's description
     */
    public String getDescription() {return(description);}


//-----------------------------------------------------------------------------


    /**
     * Get the host the client is running on.
     * @return client's host
     */
    public String getHost() {return(host);}


//-----------------------------------------------------------------------------


    /**
     * Get boolean tells whether callbacks are activated or not. The
     * start and stop methods activate and deactivate the callbacks.
     * @return true if callbacks are activated, false if they are not
     */
    public boolean isReceiving() {return(receiving);}


}

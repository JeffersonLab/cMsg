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

package org.jlab.coda.cMsg.common;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgSubscriptionHandle;
import org.jlab.coda.cMsg.cMsgCallbackInterface;

import java.lang.*;
import java.util.concurrent.TimeoutException;


/**
 * This interface is the cMsg client API for cMsg domain.
 *
 * @author Elliott Wolin
 * @author Carl Timmer
 * @version 1.0
 */
public interface cMsgDomainInterface {

    /**
     * Method to connect to a particular domain.
     *
     * @throws org.jlab.coda.cMsg.cMsgException for cMsg error.
     */
    public void connect() throws cMsgException;

    /**
     * Method to close the connection to the domain. This method results in this object
     * becoming functionally useless.
     *
     * @throws org.jlab.coda.cMsg.cMsgException for cMsg error.
     */
    public void disconnect() throws cMsgException;

    /**
     * Method to determine if this object is still connected to the domain or not.
     *
     * @return true if connected to domain server, false otherwise
     */
    public boolean isConnected();

    /**
     * Method to send a message to the domain for further distribution.
     *
     * @param message message to send
     * @throws org.jlab.coda.cMsg.cMsgException for cMsg error.
     */
    public void send(cMsgMessage message) throws cMsgException;

    /**
     * Method to send a message to the domain for further distribution
     * and wait for a response from the subdomain handler that got it.
     *
     * @param message message to send
     * @param timeout time in milliseconds to wait for a response
     * @return response from subdomain handler (0 for cMsg domain/subdomain)
     * @throws org.jlab.coda.cMsg.cMsgException for cMsg error.
     */
    public int syncSend(cMsgMessage message, int timeout) throws cMsgException;

    /**
     * Method to force cMsg client to send pending communications with domain.
     * @param timeout time in milliseconds to wait for completion
     * @throws org.jlab.coda.cMsg.cMsgException for cMsg error.
     */
    public void flush(int timeout) throws cMsgException;

    /**
     * This method is like a one-time subscribe. The domain grabs the first incoming
     * message of the requested subject and type and sends that to the caller.
     *
     * @param subject subject of message desired from server
     * @param type type of message desired from server
     * @param timeout time in milliseconds to wait for a message
     * @return response message
     * @throws org.jlab.coda.cMsg.cMsgException for cMsg error.
     * @throws TimeoutException if timeout occurs
     */
    public cMsgMessage subscribeAndGet(String subject, String type, int timeout)
            throws cMsgException, TimeoutException;

    /**
     * The message is sent as it would be in the {@link #send} method and a single synchronous
     * response is received. The domain notes the fact that a response to it is expected,
     * and sends it to all subscribed to its subject and type.
     * When a marked response is received from a client, it sends that
     * first response back to the original sender regardless of its subject or type.
     * The response may be null.
     *
     * @param message message sent to server
     * @param timeout time in milliseconds to wait for a response message, zero means wait forever.
     * @return response message
     * @throws org.jlab.coda.cMsg.cMsgException for cMsg error.
     * @throws TimeoutException if timeout occurs
     */
    public cMsgMessage sendAndGet(cMsgMessage message, int timeout)
            throws cMsgException, TimeoutException;

    /**
     * Method to subscribe to receive messages of a subject and type from the domain.
     *
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @param userObj any user-supplied object to be given to the callback method as an argument
     * @return handle object to be used for unsubscribing
     * @throws org.jlab.coda.cMsg.cMsgException for cMsg error.
     */
    public cMsgSubscriptionHandle subscribe(String subject, String type, cMsgCallbackInterface cb, Object userObj)
            throws cMsgException;

    /**
     * Method to unsubscribe a previous subscription to receive messages of a subject and type
     * from the domain.
     *
     * @param handle the object returned from a subscribe call
     * @throws org.jlab.coda.cMsg.cMsgException for cMsg error.
     */
    public void unsubscribe(cMsgSubscriptionHandle handle)
           throws cMsgException;

    /**
     * This method is a synchronous call to receive a message containing monitoring data
     * which describes the state of the cMsg domain the user is connected to.
     *
     * @param  command directive for monitoring process
     * @return response message containing monitoring information
     * @throws org.jlab.coda.cMsg.cMsgException for cMsg error.
     */
    public cMsgMessage monitor(String command)
            throws cMsgException;

    /**
     * Method to start or activate the subscription callbacks.
     */
    public void start();

    /**
     * Method to stop or deactivate the subscription callbacks.
     */
    public void stop();

    /**
     * Method to shutdown the given client(s).
     *
     * @param client client(s) to be shutdown
     * @param includeMe  if true, it is permissible to shutdown calling client
     * @throws org.jlab.coda.cMsg.cMsgException for cMsg error.
     */
    public void shutdownClients(String client, boolean includeMe) throws cMsgException;

    /**
     * Method to shutdown the given server(s).
     *
     * @param server server(s) to be shutdown
     * @param includeMyServer  if true, it is permissible to shutdown calling client's
     *                         cMsg server
     * @throws org.jlab.coda.cMsg.cMsgException for cMsg error.
     */
    public void shutdownServers(String server, boolean includeMyServer) throws cMsgException;

    /**
     * Method to set the shutdown handler of the client.
     *
     * @param handler shutdown handler
     */
    public void setShutdownHandler(cMsgShutdownHandlerInterface handler);

    /**
     * Method to get the shutdown handler of the client.
     *
     * @return shutdown handler object
     */
    public cMsgShutdownHandlerInterface getShutdownHandler();

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
     * @param UDL UDL of client
     * @throws org.jlab.coda.cMsg.cMsgException for cMsg error.
     */
    public void setUDL(String UDL) throws cMsgException;

    /**
     * Get the UDL the client is currently connected to.
     * @return UDL the client is currently connected to
     */
    public String getCurrentUDL();

    /**
     * Get the client's UDL remainder.
     * @return client's UDL remainder
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
     * Get the host of the server (if any) that this client is connected to.
     * @return server's host
     */
    public String getServerHost();

    /**
     * Get the port of the server (if any) that this client is connected to.
     * @return server's port
     */
    public int getServerPort();

    /**
     * General purpose I/O method which gets a string that the implementation
     * class wants to return up to the top (this) level API.
     * @param s input string
     * @return  output string
     */
    public String getInfo(String s);

    /**
     * Method telling whether callbacks are activated or not. The
     * start and stop methods activate and deactivate the callbacks.
     * @return true if callbacks are activated, false if they are not
     */
    public boolean isReceiving();

    /**
     * Set client's level of debug output.
     * @param debug client's level of debug output
     */
    public void setDebug(int debug);

    /**
     * Get client's level of debug output.
     * @return client's level of debug output
     */
    public int getDebug();

}

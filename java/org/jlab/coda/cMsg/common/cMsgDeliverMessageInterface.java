/*----------------------------------------------------------------------------*
 *  Copyright (c) 2005        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 8-Feb-2005, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.common;

import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgException;

import java.io.IOException;

/**
 * <p>Classes that implement this interface provide a means for a subdomain handler
 * object to talk to its client - providing the client with responses to its requests.
 * It is assumed that a single object of this type talks to one specific client. Thus
 * the deliverMessage method does not have an argument
 * specifying the client. The implementing class should store and use this information
 * aside from the use of this interface.</p>
 */
public interface cMsgDeliverMessageInterface {

    /**
     * Method to close all system resources that need closing.
     */
    public void close();

    /**
     * Method to deliver a message from a domain server's subdomain handler to a client.
     *
     * @param msg     message to sent to client
     * @param msgType type of communication with the client
     * @throws cMsgException if cMsg error.
     * @throws java.io.IOException if IO error.
     */
    public void deliverMessage(cMsgMessage msg, int msgType)
            throws cMsgException, IOException;

    /**
     * Method to deliver an array of strings from a domain server's subdomain handler to a client.
     *
     * @param strs array of strings to sent to client
     * @param msgType type of communication with the client
     * @throws cMsgException if cMsg error.
     * @throws java.io.IOException if IO error.
     */
    public void deliverMessage(String[] strs, int msgType)
            throws cMsgException, IOException;

    /**
     * Method to deliver 2 integers from a domain server's subdomain handler to a client.
     *
     * @param i integer to sent to client
     * @param j integer to sent to client
     * @param msgType type of communication with the client
     * @throws cMsgException if cMsg error.
     * @throws java.io.IOException if IO error.
     */
    public void deliverMessage(int i, int j, int msgType)
            throws cMsgException, IOException;

}

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

package org.jlab.coda.cMsg;

import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;
import java.util.List;
import java.io.IOException;

/**
 * Classes that implement this interface provide a means for a subdomain handler
 * object to talk to its client - providing the client with responses to its requests.
 * Various types of messages may be sent. These are already defined and are: <p>
 * <ul>
 * <li>{@link cMsgConstants#msgGetResponse} for a message sent in response to a {@link cMsg#sendAndGet}<p>
 * <li>{@link cMsgConstants#msgGetResponseIsNull} for a null sent in response to a {@link cMsg#sendAndGet}<p>
 * <li>{@link cMsgConstants#msgSubscribeResponse} for a message sent in response to a {@link cMsg#subscribe}<p>
 * </ul>
 */
public interface cMsgDeliverMessageInterface {

    /**
     * Method to deliver a message from a domain server's subdomain handler to a client.
     *
     * @param msg     message to sent to client
     * @param client  information about client
     * @param msgType type of communication with the client
     * @throws cMsgException
     * @throws java.io.IOException
     */
    public void deliverMessage(cMsgMessageFull msg, cMsgClientInfo client, int msgType)
            throws cMsgException, IOException;

    /**
     * Method to deliver a message from a domain server's subdomain handler to a client
     * and receive acknowledgment that the message was received.
     *
     * @param msg     message to sent to client
     * @param client  information about client
     * @param msgType type of communication with the client
     * @return true if message acknowledged by receiver, otherwise false
     * @throws cMsgException
     * @throws java.io.IOException
     */
    public boolean deliverMessageAndAcknowledge(cMsgMessageFull msg, cMsgClientInfo client, int msgType)
            throws cMsgException, IOException;

}

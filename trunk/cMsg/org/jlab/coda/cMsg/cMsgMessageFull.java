/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 3-Dec-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;

import java.util.Date;

/**
 * Class that contains the full functionality of a message. It extends the class
 * that users have access to by defining setters and getters that the user has
 * no need of. This class is for use only by packages that are part of the cMsg
 * implementation.
 */
public class cMsgMessageFull extends cMsgMessage {

    /** Constructor. */
    public cMsgMessageFull() {
    }

    /** Constructor using existing cMsgMessage. */
    public cMsgMessageFull(cMsgMessage m) {

        this.setSysMsgId(m.getSysMsgId());
        this.setGetResponse(m.isGetResponse());
        this.setGetRequest(m.isGetRequest());
        this.setDomain(m.getDomain());
        this.setVersion(m.getVersion());
        this.setSubject(m.getSubject());
        this.setType(m.getType());
        this.setText(m.getText());
        this.setPriority(m.getPriority());
        this.setUserInt(m.getUserInt());
        this.setUserTime(m.getUserTime());
        this.setSender(m.getSender());
        this.setSenderHost(m.getSenderHost());
        this.setSenderTime(m.getSenderTime());
        this.setReceiver(m.getReceiver());
        this.setReceiverHost(m.getReceiverHost());
        this.setReceiverTime(m.getReceiverTime());
    }


    /**
     * Creates a complete copy of this message.
     *
     * @return copy of this message.
     */
    public cMsgMessageFull copy() {
        cMsgMessageFull msg = null;
        try {
            msg = (cMsgMessageFull) this.clone();
        }
        catch (CloneNotSupportedException e) {
        }
        return msg;
    }

    /**
     * Creates a proper response message to this message sent by a client calling "sendAndGet".
     *
     * @return message with the response fields properly set.
     * @throws cMsgException if this message was not sent from a "sendAndGet" method call
     */
    public cMsgMessageFull response() throws cMsgException {
        // If this message was not sent from a "get" method call,
        // a proper response is not possible, since the sysMsgId
        // and senderToken fields will not have been properly set.
        if (!getRequest) {
            throw new cMsgException("this message not sent by client calling get");
        }
        cMsgMessageFull msg = new cMsgMessageFull();
        msg.sysMsgId = sysMsgId;
        msg.senderToken = senderToken;
        msg.getResponse = true;
        return msg;
    }


    // general quantities


    /**
     * Set system id of message. Used by the system in doing sendAndGet.
     * @param sysMsgId system id of message.
     */
    public void setSysMsgId(int sysMsgId) {this.sysMsgId = sysMsgId;}


    /**
     * Set domain this message exists in.
     * @param domain domain this message exists in.
     */
    public void setDomain(String domain) {this.domain = domain;}


    /**
     * Specify whether this message is a "sendAndGet" request.
     * @param getRequest true if this message is a "sendAndGet" request
     */
    public void setGetRequest(boolean getRequest) {this.getRequest = getRequest;}


    /**
     * Sets the version number of this message. The version number must be the same as the
     * version number of the cMsg package - given by {@link cMsgConstants#version}.
     * @param version version number of message
     */
    public void setVersion(int version) {
        if (version < 0) version = 0;
        this.version = version;
    }


    // sender quantities


    /**
     * Set message sender.
     * @param sender message sender.
     */
    public void setSender(String sender) {this.sender = sender;}


    /**
     * Set message sender's host computer.
     * @param senderHost message sender's host computer.
     */
    public void setSenderHost(String senderHost) {this.senderHost = senderHost;}


    /**
     * Set time message was sent.
     *
     * @param time time message sent.
     */
    public void setSenderTime(Date time) {
        this.senderTime = time.getTime();
    }


     /**
     * Set sender's token. Used by the system in doing sendAndGet.
     * @param senderToken sender's token.
     */
    public void setSenderToken(int senderToken) {this.senderToken = senderToken;}


    // receiver quantities


    /**
     * Set message receiver.
     * @param receiver message receiver.
     */
    public void setReceiver(String receiver) {this.receiver = receiver;}


    /**
     * Set message receiver's host computer.
     * @param receiverHost message receiver's host computer.
     */
    public void setReceiverHost(String receiverHost) {this.receiverHost = receiverHost;}



    /**
      * Set time message was receivered.
      * @param time time message received.
      */
    public void setReceiverTime(Date time) {this.receiverTime = time.getTime();}


    /**
     * Get receiver's id number corresponding to a subject & type pair of a message subscription.
     * @return receiver's subscription id number.
     */
    public int getReceiverSubscribeId() {return receiverSubscribeId;}
    /**
     * Set receiver's subscription id number.
     * @param receiverSubscribeId  receiver's subscription id number.
     */
    public void setReceiverSubscribeId(int receiverSubscribeId) {this.receiverSubscribeId = receiverSubscribeId;}

}

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
import java.util.*;


/**
 * This class implements a message in the cMsg messaging system.
 *
 * @author Elliott Wolin
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgMessage implements Cloneable {

    /**
     * Is message a sendAndGet request? -- stored in first bit of info.
     * This is only for internal use.
     */
    public static final int isGetRequest  = 0x0001;
    /**
     * Is message a response to a sendAndGet? -- stored in second bit of info.
     * This is only for internal use.
     */
    public static final int isGetResponse = 0x0002;
    /**
     * Is the response message null instead of a message? -- stored in third bit of info.
     * This is only for internal use.
     */
    public static final int isNullGetResponse = 0x0004;

    // general quantities

    /**
     * Unique message id created by cMsg system.
     * Used by domain server to track client's "sendAndGet" calls.
     */
    int sysMsgId;

    /** Message exists in this domain. */
    String domain;

    /**
     * Condensed information stored in bits.
     * Is message a sendAndGet request? -- stored in first bit.
     * Is message a response to a sendAndGet? -- stored in second bit.
     * Is the response message null instead of a message? -- stored in third bit.
     *
     * @see #isGetRequest
     * @see #isGetResponse
     * @see #isNullGetResponse
     */
    int info;

    /** Version number of cMsg. */
    int version;

    /** Creator of the this message. */
    String creator;


    // user-settable quantities


    /** Subject of message. */
    String subject;

    /** Type of message. */
    String type;

    /** Text of message. */
    String text;

    /** Message priority set by user where 0 is both the default and the lowest value. */
    int priority;

    /** Integer supplied by user. */
    int userInt;

    /** Time supplied by user in milliseconds from midnight GMT, Jan 1st, 1970. */
    long userTime;


    // sender quantities


    /** Unique name of message sender. */
    String sender;

    /** Host sender is running on. */
    String senderHost;

    /** Time message was sent in milliseconds from midnight GMT, Jan 1st, 1970. */
    long senderTime;

    /** Field used by domain server in implementing "sendAndGet". */
    int senderToken;


    // receiver quantities


    /** Unique name of message receiver. */
    String receiver;

    /** Host receiver is running on. */
    String receiverHost;

    /** Time message was received in milliseconds from midnight GMT, Jan 1st, 1970. */
    long receiverTime;

    /**
     * Message receiver's id number corresponding to a subject & type pair
     * of a message subscription.
     */
    int receiverSubscribeId;


    /** The constructor for a blank message. */
    public cMsgMessage() {}


    /** The constructor which copies a given message, EXCEPT for the creator field. */
    public cMsgMessage(cMsgMessage msg) {
        sysMsgId            = msg.sysMsgId;
        domain              = msg.domain;
        info                = msg.info;
        version             = msg.version;
        //creator             = msg.creator;
        subject             = msg.subject;
        type                = msg.type;
        text                = msg.text;
        priority            = msg.priority;
        userInt             = msg.userInt;
        userTime            = msg.userTime;
        sender              = msg.sender;
        senderHost          = msg.senderHost;
        senderTime          = msg.senderTime;
        senderToken         = msg.senderToken;
        receiver            = msg.receiver;
        receiverHost        = msg.receiverHost;
        receiverTime        = msg.receiverTime;
        receiverSubscribeId = msg.receiverSubscribeId;
    }


    /**
     * Creates a complete copy of this message.
     *
     * @return copy of this message.
     */
    public cMsgMessage copy() {
        cMsgMessage msg = null;
        try {
            msg = (cMsgMessage) this.clone();
        }
        catch (CloneNotSupportedException e) {
        }
        return msg;
    }


    /**
     * Creates a proper response message to this message which was sent by a client calling
     * sendAndGet.
     *
     * @return message with the response fields properly set.
     * @throws cMsgException if this message was not sent from a "sendAndGet" method call
     */
    public cMsgMessage response() throws cMsgException {
        // If this message was not sent from a "sendAndGet" method call,
        // a proper response is not possible, since the sysMsgId
        // and senderToken fields will not have been properly set.
        if (!isGetRequest()) {
            throw new cMsgException("this message not sent by client calling sendAndGet");
        }
        cMsgMessage msg = new cMsgMessage();
        msg.sysMsgId = sysMsgId;
        msg.senderToken = senderToken;
        msg.info = isGetResponse;
        return msg;
    }


    /**
     * Creates a proper response message to this message which was sent by a client calling
     * sendAndGet. In this case, the response message is encoded so that the receiver of this
     * message (original sendAndGet caller) does not receive a message at all, but only a null.
     *
     * @return message with the response fields properly set so original sender gets a null
     * @throws cMsgException if this message was not sent from a "sendAndGet" method call
     */
    public cMsgMessage nullResponse() throws cMsgException {
        // If this message was not sent from a "get" method call,
        // a proper response is not possible, since the sysMsgId
        // and senderToken fields will not have been properly set.
        if (!isGetRequest()) {
            throw new cMsgException("this message not sent by client calling sendAndGet");
        }
        cMsgMessage msg = new cMsgMessage();
        msg.sysMsgId = sysMsgId;
        msg.senderToken = senderToken;
        msg.info = isGetResponse | isNullGetResponse;
        return msg;
    }


    /**
     * Converts existing message to response of supplied message.
     */
    public void makeResponse(cMsgMessage msg) {
        this.sysMsgId    = msg.getSysMsgId();
        this.senderToken = msg.getSenderToken();
        this.info = isGetResponse;
    }


    public void makeNullResponse(cMsgMessage msg) {
        this.sysMsgId    = msg.getSysMsgId();
        this.senderToken = msg.getSenderToken();
        this.info = isGetResponse | isNullGetResponse;
    }


    // general quantities


    /**
     * Get system id of message. Irrelevant to the user, used only by the system.
     * @return system id of message.
     */
    public int getSysMsgId() {return sysMsgId;}


    /**
     * Get domain this message exists in.
     * @return domain message exists in.
     */
    public String getDomain() {return domain;}


    /**
     * Is this message a response to a "sendAndGet" request?
     * @return true if this message is a response to a "sendAndGet" request.
     */
    public boolean isGetResponse() {return ((info & isGetResponse) == isGetResponse ? true : false);}
    /**
     * Is this message a response to a "sendAndGet" request with a null
     * pointer to be delivered instead of this message?
     * @return true if this message is a response to a "sendAndGet" request
     *         with a null pointer to be delivered instead of this message
     */
    public boolean isNullGetResponse() {return ((info & isNullGetResponse) == isNullGetResponse ? true : false);}
    /**
     * Specify whether this message is a response to a "sendAndGet" message.
     * @param getResponse true if this message is a response to a "sendAndGet" message
     */
    public void setGetResponse(boolean getResponse) {
        info = getResponse ? info|isGetResponse : info & ~isGetResponse;
    }


    /**
     * Is this message a "sendAndGet" request?
     * @return true if this message is a "sendAndGet" request
     */
    public boolean isGetRequest() {return ((info & isGetRequest) == isGetRequest ? true : false);}


    /**
     * Gets information compacted into a bit pattern. This method is not useful
     * to the general user and is for use only by system developers.
     *
     * @return integer containing bit pattern of information
     */
    public int getInfo() {
        return info;
    }


    /**
     * Gets the version number of this message which is the same
     * as that of the cMsg software package that created it.
     * @return version number of message.
     */
    public int getVersion() {
        return version;
    }


    /**
     * Gets the creator of this message.
     * @return creator of this message.
     */
    public String getCreator() {
        return creator;
    }


    // user-settable quantities


    /**
     * Get subject of message.
     * @return subject of message.
     */
    public String getSubject() {return subject;}
    /**
     * Set subject of message.
     * @param subject subject of message.
     */
    public void setSubject(String subject) {this.subject = subject;}


    /**
     * Get type of message.
     * @return type of message.
     */
    public String getType() {return type;}
    /**
     * Set type of message.
     * @param type type of message.
     */
    public void setType(String type) {this.type = type;}


    /**
     * Get text of message.
     * @return text of message.
     */
    public String getText() {return text;}
    /**
     * Set text of message.
     * @param text ext of message.
     */
    public void setText(String text) {this.text = text;}


    /**
     * Get message's priority.
     * @return priority of message.
     */
    public int getPriority() {return priority;}
    /**
      * Set message's priority.
      * @param priority message's priority.
      */
    public void setPriority(int priority) {
        if (priority < 0) priority = 0;
        this.priority = priority;
    }


    /**
     * Get user supplied integer.
     * @return user supplied integer.
     */
    public int getUserInt() {return userInt;}
    /**
     * Set message sender's id.
     * @param userInt message sender's id.
     */
    public void setUserInt(int userInt) {this.userInt = userInt;}


    /**
     * Get user supplied time.
     * @return user supplied time.
     */
    public Date getUserTime() {return new Date(userTime);}
    /**
     * Set time.
     * @param time time.
     */
    public void setUserTime(Date time) {this.userTime = time.getTime();}


    // sender


    /**
     * Get message sender.
     * @return message sender.
     */
    public String getSender() {return sender;}


    /**
     * Get message sender's host computer.
     * @return message sender's host computer.
     */
    public String getSenderHost() {return senderHost;}


    /**
     * Get time message was sent.
     * @return time message sent.
     */
    public Date getSenderTime() {return new Date(senderTime);}


    /**
     * Get sender's token. Used to track asynchronous responses to
     * messages requesting responses from other clients. Irrelevant to the user,
     * used only by the system.
     */
     public int getSenderToken() {return senderToken;}


    // receiver


    /**
     * Get message receiver.
     * @return message receiver.
     */
    public String getReceiver() {return receiver;}


    /**
     * Get message receiver's host computer.
     * @return message receiver's host computer.
     */
    public String getReceiverHost() {return receiverHost;}


    /**
     * Get time message was received.
     * @return time message received.
     */
    public Date getReceiverTime() {return new Date(receiverTime);}


    /**
     * Get receiver subscribe id.
     * @return receiver subscribe id.
     */
    public int getReceiverSubscribeId() {return receiverSubscribeId;}





    /**
      * Returns XML representation of message as a string
      */
    public String toString() {
        return(
               "<cMsgMessage date=\"" + (new Date()) + "\"\n"
            + "     " + "version              = \"" + this.getVersion() + "\"\n"
            + "     " + "domain               = \"" + this.getDomain() + "\"\n"
            + "     " + "sysMsgId             = \"" + this.getSysMsgId() + "\"\n"
            + "     " + "is get request       = \"" + this.isGetRequest() + "\"\n"
            + "     " + "is get response      = \"" + this.isGetResponse() + "\"\n"
            + "     " + "is null get response = \"" + this.isNullGetResponse() + "\"\n"
            + "     " + "creator              = \"" + this.getCreator() + "\"\n"
            + "     " + "sender               = \"" + this.getSender() + "\"\n"
            + "     " + "senderHost           = \"" + this.getSenderHost() + "\"\n"
            + "     " + "senderTime           = \"" + this.getSenderTime() + "\"\n"
            + "     " + "senderToken          = \"" + this.getSenderToken() + "\"\n"
            + "     " + "userInt              = \"" + this.getUserInt() + "\"\n"
            + "     " + "userTime             = \"" + this.getUserTime() + "\"\n"
            + "     " + "priority             = \"" + this.getPriority() + "\"\n"
            + "     " + "receiver             = \"" + this.getReceiver() + "\"\n"
            + "     " + "receiverHost         = \"" + this.getReceiverHost() + "\"\n"
            + "     " + "receiverTime         = \"" + this.getReceiverTime() + "\"\n"
            + "     " + "receiverSubscribeId  = \"" + this.getReceiverSubscribeId() + "\"\n"
            + "     " + "subject              = \"" + this.getSubject() + "\"\n"
            + "     " + "type                 = \"" + this.getType() + "\">\n"
            + "<![CDATA[\n" + this.getText() + "\n]]>\n"
            + "</cMsgMessage>\n\n");
    }

}

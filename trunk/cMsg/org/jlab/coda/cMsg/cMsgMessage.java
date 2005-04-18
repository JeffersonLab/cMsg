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
    public static final int isGetRequest  = 0x1;
    /**
     * Is message a response to a sendAndGet? -- stored in second bit of info.
     * This is only for internal use.
     */
    public static final int isGetResponse = 0x2;
    /**
     * Is the response message null instead of a message? -- stored in third bit of info.
     * This is only for internal use.
     */
    public static final int isNullGetResponse = 0x4;

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

    /** Class member reserved for future use. */
    int reserved;


    // user-settable quantities


    /** Subject of message. */
    String subject;

    /** Type of message. */
    String type;

    /** Text of message. */
    String text;

    /** Integer supplied by user. */
    int userInt;

    /** Time supplied by user in milliseconds from midnight GMT, Jan 1st, 1970. */
    long userTime;

    /** Byte array of message. */
    byte[] bytes;

    /** Offset into byte array of first element. */
    int offset;

    /** Length of byte array elements to use. */
    int length;


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
        bytes               = (byte[]) msg.bytes.clone();
        reserved            = msg.reserved;
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
            msg.bytes = (byte[]) this.bytes.clone();
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


    ///////////////////////
    // general quantities
    ///////////////////////


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
     * Specify whether this message is a response to a "sendAndGet" message.
     * @param getResponse true if this message is a response to a "sendAndGet" message
     */
    public void setGetResponse(boolean getResponse) {
        info = getResponse ? info|isGetResponse : info & ~isGetResponse;
    }


    /**
     * Is this message a response to a "sendAndGet" request with a null
     * pointer to be delivered instead of this message?
     * @return true if this message is a response to a "sendAndGet" request
     *         with a null pointer to be delivered instead of this message
     */
    public boolean isNullGetResponse() {return ((info & isNullGetResponse) == isNullGetResponse ? true : false);}
    /**
     * Specify whether this message is a response to a "sendAndGet" request with a null
     * pointer to be delivered instead of this message.
     * @param nullGetResponse true if this message is a response to a "sendAndGet" request
     *                        with a null pointer to be delivered instead of this message
     */
    public void setNullGetResponse(boolean nullGetResponse) {
        info = nullGetResponse ? info|isNullGetResponse : info & ~isNullGetResponse;
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


    /////////////////////////////
    // user-settable quantities
    /////////////////////////////


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
     * @param text text of message.
     */
    public void setText(String text) {this.text = text;}


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

    // byte array stuff

    /**
     * Get byte array of message.
     * @return byte array of message.
     */
    public byte[] getByteArray() {return bytes;}
    /**
     * Set byte array of message.
     * @param b byte array of message.
     */
    public void setByteArray(byte[] b) {
        if (b == null) {
            bytes  = null;
            offset = 0;
            length = 0;
            return;
        }
        bytes  = (byte[]) b.clone();
        offset = 0;
        length = b.length;
    }
    /**
     * Set byte array of message.
     * @param b byte array of message.
     * @param offset index into byte array to bytes of interest.
     * @param length number of bytes of interest.
     */
    public void setByteArray(byte[] b, int offset, int length) throws cMsgException {
        if ((length < 0) || (length > b.length)) {
            throw new cMsgException("length is out of array bounds");
        }
        if ((offset < 0) || (offset > b.length-1)) {
            throw new cMsgException("offset is out of array bounds");
        }
        if ((offset + length) > b.length) {
            throw new cMsgException("offset + length is out of array bounds");
        }

        if (b == null) {
            bytes = null;
            this.offset = 0;
            this.length = 0;
            return;
        }

        if (offset == 0 && length == b.length) {
            setByteArray(b);
            return;
        }

        this.offset = 0;
        this.length = length;
        bytes  = new byte[length];

        for (int i=0; i<length; i++) {
            bytes[i] = b[offset+i];
        }
    }

    /**
     * Set byte array of message to the given argument without
     * copying the byte array itself - only the reference is copied.
     * @param b byte array of message.
     */
    public void setByteArrayNoCopy(byte[] b) {
        if (b == null) {
            bytes  = null;
            offset = 0;
            length = 0;
            return;
        }
        bytes  = b;
        offset = 0;
        length = b.length;
    }
    /**
     * Set byte array of message to the given argument without
     * copying the byte array itself - only the reference is copied.
     * @param b byte array of message.
     */
    public void setByteArrayNoCopy(byte[] b, int offset, int length) throws cMsgException {
        if ((length < 0) || (length > b.length)) {
            throw new cMsgException("length is out of array bounds");
        }
        if ((offset < 0) || (offset > b.length-1)) {
            throw new cMsgException("offset is out of array bounds");
        }
        if ((offset + length) > b.length) {
            throw new cMsgException("offset + length is out of array bounds");
        }

        if (b == null) {
            bytes = null;
            this.offset = 0;
            this.length = 0;
            return;
        }

        bytes = b;
        this.offset = offset;
        this.length = length;
    }


    /**
     * Get byte array length of data of interest. This may be smaller
     * than the total length of the array if the user is only interested
     * in a portion of the array.
     * @return length of byte array's data of interest.
     */
    public int getByteArrayLength() {return length;}
    /**
     * Set byte array length of data of interest. This may be smaller
     * than the total length of the array if the user is only interested
     * in a portion of the array.
     * @param length of byte array's data of interest.
     */
    public void setByteArrayLength(int length) throws cMsgException {
        if ((length < 0) || (length > bytes.length)) {
            throw new cMsgException("length is out of array bounds");
        }
        if ((offset + length) > bytes.length) {
            throw new cMsgException("offset + length is out of array bounds");
        }
        this.length = length;
    }


    /**
     * Get byte array index to data of interest. This may be non-zero
     * if the user is only interested in a portion of the array.
     * @return index to byte array's data of interest.
     */
    public int getByteArrayOffset() {return offset;}
    /**
     * Set byte array index to data of interest. This may be non-zero
     * if the user is only interested in a portion of the array.
     * @param offset index to byte array's data of interest.
     */
    public void setByteArrayOffset(int offset) throws cMsgException {
        if ((offset < 0) || (offset > bytes.length-1)) {
            throw new cMsgException("offset is out of array bounds");
        }
        if ((offset + length) > bytes.length) {
            throw new cMsgException("offset + length is out of array bounds");
        }
        this.offset = offset;
    }


    /////////////
    // sender
    /////////////


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


    /////////////
    // receiver
    /////////////


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
            + "     " + "getRequest           = \"" + this.isGetRequest() + "\"\n"
            + "     " + "getResponse          = \"" + this.isGetResponse() + "\"\n"
            + "     " + "nullGetResponse      = \"" + this.isNullGetResponse() + "\"\n"
            + "     " + "creator              = \"" + this.getCreator() + "\"\n"
            + "     " + "sender               = \"" + this.getSender() + "\"\n"
            + "     " + "senderHost           = \"" + this.getSenderHost() + "\"\n"
            + "     " + "senderTime           = \"" + this.getSenderTime() + "\"\n"
            + "     " + "senderToken          = \"" + this.getSenderToken() + "\"\n"
            + "     " + "userInt              = \"" + this.getUserInt() + "\"\n"
            + "     " + "userTime             = \"" + this.getUserTime() + "\"\n"
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

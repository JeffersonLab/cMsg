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
public class cMsgMessage {
    /** Unique message id created by cMsg system. */
    int      sysMsgId;
    /**
     * Message receiver's id number corresponding to a subject & type pair
     * of a message subscription.
     */
    int      receiverSubscribeId;


    /** Unique name of message sender. */
    String   sender;
    /**
     * Unique id of message sender. This distinguishes between two identically
     * named senders - one of whom dies and is replaced by the other.
     */
    int      senderId;
    /** Host sender is running on. */
    String   senderHost;
    /** Time message was sent. */
    Date     senderTime;
    /** Unique message id created by sender. */
    int      senderMsgId;
    /**
     * Sender given integer used to track asynchronous responses to
     * messages requesting responses from other clients.
     */
    int      senderToken;


    /** Unique name of message receiver. */
    String   receiver;
    /** Host receiver is running on. */
    String   receiverHost;
    /** Time message was received. */
    Date     receiverTime;


    /** Message exists in this domain. */
    String   domain;
    /** Subject of message. */
    String   subject;
    /** Type of message. */
    String   type;
    /** Text of message. */
    String   text;


    /** Get domain this message exists in. */
    public String getDomain() {return domain;}
    /**
     * Set domain this message exists in.
     * @param domain domain this message exists in.
     */
    public void   setDomain(String domain) {this.domain = domain;}

    /** Get subject of message. */
    public String getSubject() {return subject;}
    /**
     * Set subject of message.
     * @param subject subject of message.
     */
    public void   setSubject(String subject) {this.subject = subject;}

    /** Get text of message. */
    public String getText() {return text;}
    /**
     * Set text of message.
     * @param text ext of message.
     */
    public void   setText(String text) {this.text = text;}

    /** Get type of message. */
    public String getType() {return type;}
    /**
     * Set type of message.
     * @param type type of message.
     */
    public void   setType(String type) {this.type = type;}

    /** Get system id of message. */
    public int    getSysMsgId() {return sysMsgId;}
    /**
     * Set system id of message. Set automatically by cMsg system.
     * @param sysMsgId system id of message.
     */
    public void   setSysMsgId(int sysMsgId) {this.sysMsgId = sysMsgId;}

    // receiver

    /** Get message receiver. */
    public String getReceiver() {return receiver;}
    /**
     * Set message receiver.  Set automatically by cMsg system.
     * @param receiver message receiver.
     */
    public void   setReceiver(String receiver) {this.receiver = receiver;}

    /** Get message receiver's host computer. */
    public String getReceiverHost() {return receiverHost;}
    /**
     * Set message receiver's host computer. Set automatically by cMsg system.
     * @param receiverHost message receiver's host computer.
     */
    public void   setReceiverHost(String receiverHost) {this.receiverHost = receiverHost;}

    /** Get receiver's id number corresponding to a subject & type pair of a message subscription. */
    public int    getReceiverSubscribeId() {return receiverSubscribeId;}
    /**
     * Set receiver's subscription id number.
     * @param receiverSubscribeId  receiver's subscription id number.
     */
    public void   setReceiverSubscribeId(int receiverSubscribeId) {this.receiverSubscribeId = receiverSubscribeId;}

    /** Get time message was received. */
    public Date   getReceiverTime() {return receiverTime;}
    /**
      * Set time message was receivered. Set automatically by cMsg system.
      * @param receiverTime time message received.
      */
    public void   setReceiverTime(Date receiverTime) {this.receiverTime = receiverTime;}

    // sender

    /** Get message sender. */
    public String getSender() {return sender;}
    /**
     * Set message sender.
     * @param sender message sender.
     */
    public void   setSender(String sender) {this.sender = sender;}

    /** Get message sender's host computer. */
    public String getSenderHost() {return senderHost;}
    /**
      * Set message sender's host computer. Set automatically by cMsg system.
      * @param senderHost message sender's host computer.
      */
    public void   setSenderHost(String senderHost) {this.senderHost = senderHost;}

    /**
      * Get unique id of message sender. This id distinguishes between two identically
      * named senders - one of whom dies and is replaced by the other.
      */
    public int    getSenderId() {return senderId;}
    /**
      * Set message sender's id.
      * @param senderId message sender's id.
      */
    public void   setSenderId(int senderId) {this.senderId = senderId;}

    /** Get sender message's id. */
    public int    getSenderMsgId() {return senderMsgId;}
    /**
      * Set sender message's id.
      * @param senderMsgId sender message's id.
      */
    public void   setSenderMsgId(int senderMsgId) {this.senderMsgId = senderMsgId;}

    /** Get time message was sent. */
    public Date   getSenderTime() {return senderTime;}
    /**
      * Set time message was sent. Set automatically by cMsg system.
      * @param senderTime time message sent.
      */
    public void   setSenderTime(Date senderTime) {this.senderTime = senderTime;}

    /**
     * Get sender's token. Used to track asynchronous responses to
     * messages requesting responses from other clients.
     */
    public int    getSenderToken() {return senderToken;}
    /**
      * Set sender's token.
      * @param senderToken sender's token.
      */
    public void   setSenderToken(int senderToken) {this.senderToken = senderToken;}

}

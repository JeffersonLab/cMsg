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
 * This class implements a cMsg name server for a particular cMsg domain.
 *
 * @author Elliott Wolin
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgMessage {
    
    int      domainId;
    int      sysMsgId;
    int      receiverSubscribeId;

    String   sender;
    int      senderId; /* in case fred dies and resurrects */
    String   senderHost;
    Date     senderTime;
    int      senderMsgId;

    String   receiver;
    String   receiverHost;
    Date     receiverTime;

    String   domain;
    String   subject;
    String   type;
    String   text;

    public String getDomain() {return domain;}
    public void setDomain(String domain) {this.domain = domain;}

    public int getDomainId() {return domainId;}
    public void setDomainId(int domainId) {this.domainId = domainId;}

    public String getReceiver() {return receiver;}
    public void setReceiver(String receiver) {this.receiver = receiver;}

    public String getReceiverHost() {return receiverHost;}
    public void setReceiverHost(String receiverHost) {this.receiverHost = receiverHost;}

    public int getReceiverSubscribeId() {return receiverSubscribeId;}
    public void setReceiverSubscribeId(int receiverSubscribeId) {this.receiverSubscribeId = receiverSubscribeId;}

    public Date getReceiverTime() {return receiverTime;}
    public void setReceiverTime(Date receiverTime) {this.receiverTime = receiverTime;}

    public String getSender() {return sender;}
    public void setSender(String sender) {this.sender = sender;}

    public String getSenderHost() {return senderHost;}
    public void setSenderHost(String senderHost) {this.senderHost = senderHost;}

    public int getSenderId() {return senderId;}
    public void setSenderId(int senderId) {this.senderId = senderId;}

    public int getSenderMsgId() {return senderMsgId;}
    public void setSenderMsgId(int senderMsgId) {this.senderMsgId = senderMsgId;}

    public Date getSenderTime() {return senderTime;}
    public void setSenderTime(Date senderTime) {this.senderTime = senderTime;}

    public String getSubject() {return subject;}
    public void setSubject(String subject) {this.subject = subject;}

    public int getSysMsgId() {return sysMsgId;}
    public void setSysMsgId(int sysMsgId) {this.sysMsgId = sysMsgId;}

    public String getText() {return text;}
    public void setText(String text) {this.text = text;}

    public String getType() {return type;}
    public void setType(String type) {this.type = type;}

//-----------------------------------------------------------------------------
}        //  end class definition
//-----------------------------------------------------------------------------

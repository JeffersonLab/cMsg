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
import java.sql.PreparedStatement;
import java.sql.ResultSet;
import java.sql.SQLException;


/**
 * This class implements a message in the cMsg messaging system.
 *
 * @author Elliott Wolin
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgMessage implements Cloneable {
    // general quantities

    /**
     * Unique message id created by cMsg system.
     * Used by domain server to track client's "get" calls.
     */
    private int sysMsgId;

    /** Message exists in this domain. */
    private String domain;

    /** Subject of message. */
    private String subject;

    /** Type of message. */
    private String type;

    /** Text of message. */
    private String text;

    /** Is this message a get request? */
    boolean getRequest;

    /** Is this message a response to a get request? */
    boolean getResponse;


    // sender quantities

    /** Unique name of message sender. */
    private String sender;

    /**
     * Unique id of message sender. This distinguishes between two identically
     * named senders - one of whom dies and is replaced by the other.
     */
    private int senderId;

    /** Host sender is running on. */
    private String senderHost;

    /** Time message was sent in milliseconds from midnight GMT, Jan 1st, 1970. */
    private long senderTime;

    /** Unique message id created by sender. */
    private int senderMsgId;

    /**
     * Sender given integer used to track asynchronous responses to
     * messages requesting responses from other clients.
     */
    private int senderToken;

    // receiver quantities

    /**
     * Message receiver's id number corresponding to a subject & type pair
     * of a message subscription.
     */
    private int receiverSubscribeId;

    /** Unique name of message receiver. */
    private String receiver;

    /** Host receiver is running on. */
    private String receiverHost;

    /** Time message was received in milliseconds from midnight GMT, Jan 1st, 1970. */
    private long receiverTime;



    /**
     * Creates a proper response message to this message sent by a client calling
     * @return message with the response fields properly set
     * @throws cMsgException if this message was not sent from a "get" method call
     */
    public cMsgMessage response() throws cMsgException {
        // If this message was not sent from a "get" method call,
        // a proper response is not possible, since the sysMsgId
        // and senderToken fields will not have been properly set.
        if (!getRequest) {
            throw new cMsgException("this message not sent by client calling get");
        }
        cMsgMessage msg = new cMsgMessage();
        msg.sysMsgId    = sysMsgId;
        msg.senderToken = senderToken;
        msg.getResponse = true;
        return msg;
    }


    /**
     * Creates a complete copy of this message.
     * @return copy of this message
     */
    public cMsgMessage copy() {
        cMsgMessage msg = null;
        try {msg = (cMsgMessage) this.clone();}
        catch (CloneNotSupportedException e) {}
        return msg;
    }


    // general quantities


    /** Get domain this message exists in. */
    public String getDomain() {return domain;}
    /**
     * Set domain this message exists in.
     * @param domain domain this message exists in.
     */
    public void setDomain(String domain) {this.domain = domain;}


    /** Get subject of message. */
    public String getSubject() {return subject;}
    /**
     * Set subject of message.
     * @param subject subject of message.
     */
    public void setSubject(String subject) {this.subject = subject;}


    /** Get text of message. */
    public String getText() {return text;}
    /**
     * Set text of message.
     * @param text ext of message.
     */
    public void setText(String text) {this.text = text;}


    /** Get type of message. */
    public String getType() {return type;}
    /**
     * Set type of message.
     * @param type type of message.
     */
    public void setType(String type) {this.type = type;}


    /** Get system id of message. */
    public int getSysMsgId() {return sysMsgId;}
    /**
     * Set system id of message. Set automatically by cMsg system.
     * @param sysMsgId system id of message.
     */
    public void setSysMsgId(int sysMsgId) {this.sysMsgId = sysMsgId;}

    /**
     * Is this message a response to a "get" request?
     * @return true if this message is a response to a "get" request
     */
    public boolean isGetResponse() {return getResponse;}
    /**
     * Specify whether this message is a response to a "get" message.
     * @param getResponse true if this message is a response to a "get" message
     */
    public void setGetResponse(boolean getResponse) {this.getResponse = getResponse;}

    /**
     * Is this message a "get" request?
     * @return true if this message is a "get" request
     */
    public boolean isGetRequest() {return getRequest;}
    /**
     * Specify whether this message is a "get" request.
     * @param getRequest true if this message is a "get" request
     */
    public void setGetRequest(boolean getRequest) {this.getRequest = getRequest;}


    // receiver


    /** Get message receiver. */
    public String getReceiver() {return receiver;}
    /**
     * Set message receiver.  Set automatically by cMsg system.
     * @param receiver message receiver.
     */
    public void setReceiver(String receiver) {this.receiver = receiver;}


    /** Get message receiver's host computer. */
    public String getReceiverHost() {return receiverHost;}
    /**
     * Set message receiver's host computer. Set automatically by cMsg system.
     * @param receiverHost message receiver's host computer.
     */
    public void setReceiverHost(String receiverHost) {this.receiverHost = receiverHost;}


    /** Get receiver's id number corresponding to a subject & type pair of a message subscription. */
    public int getReceiverSubscribeId() {return receiverSubscribeId;}
    /**
     * Set receiver's subscription id number.
     * @param receiverSubscribeId  receiver's subscription id number.
     */
    public void setReceiverSubscribeId(int receiverSubscribeId) {this.receiverSubscribeId = receiverSubscribeId;}

    /** Get time message was received. */
    public Date   getReceiverTime() {return new Date(receiverTime);}
    /**
      * Set time message was receivered. Set automatically by cMsg system.
      * @param time time message received.
      */
    public void setReceiverTime(Date time) {this.receiverTime = time.getTime();}


    // sender


    /** Get message sender. */
    public String getSender() {return sender;}
    /**
     * Set message sender.
     * @param sender message sender.
     */
    public void setSender(String sender) {this.sender = sender;}


    /** Get message sender's host computer. */
    public String getSenderHost() {return senderHost;}
    /**
      * Set message sender's host computer. Set automatically by cMsg system.
      * @param senderHost message sender's host computer.
      */
    public void setSenderHost(String senderHost) {this.senderHost = senderHost;}


    /**
      * Get unique id of message sender. This id distinguishes between two identically
      * named senders - one of whom dies and is replaced by the other.
      */
    public int getSenderId() {return senderId;}
    /**
      * Set message sender's id.
      * @param senderId message sender's id.
      */
    public void setSenderId(int senderId) {this.senderId = senderId;}


    /** Get sender message's id. */
    public int getSenderMsgId() {return senderMsgId;}
    /**
      * Set sender message's id.
      * @param senderMsgId sender message's id.
      */
    public void setSenderMsgId(int senderMsgId) {this.senderMsgId = senderMsgId;}


    /** Get time message was sent. */
    public Date getSenderTime() {return new Date(senderTime);}
    /**
      * Set time message was sent. Set automatically by cMsg system.
      * @param time time message sent.
      */
    public void setSenderTime(Date time) {this.senderTime = time.getTime();}


    /**
     * Get sender's token. Used to track asynchronous responses to
     * messages requesting responses from other clients.
     */
    public int getSenderToken() {return senderToken;}
    /**
      * Set sender's token.
      * @param senderToken sender's token.
      */
    public void setSenderToken(int senderToken) {this.senderToken = senderToken;}


    /**
      * Returns XML representation of message as a string
      */
    public String toString() {
        return(
               "<cMsgMessage date=\"" + (new Date()) + "\"\n"
            + "     " + "domain         = \"" + this.getDomain() + "\"\n"
            + "     " + "sysMsgId       = \"" + this.getSysMsgId() + "\"\n"
            + "     " + "sender         = \"" + this.getSender() + "\"\n"
            + "     " + "senderHost     = \"" + this.getSenderHost() + "\"\n"
            + "     " + "senderTime     = \"" + this.getSenderTime() + "\"\n"
            + "     " + "senderId       = \"" + this.getSenderId() + "\"\n"
            + "     " + "senderMsgId    = \"" + this.getSenderMsgId() + "\"\n"
            + "     " + "receiver       = \"" + this.getReceiver() + "\"\n"
            + "     " + "receiverHost   = \"" + this.getReceiverHost() + "\"\n"
            + "     " + "receiverTime   = \"" + this.getReceiverTime() + "\"\n"
            + "     " + "subject        = \"" + this.getSubject() + "\"\n"
            + "     " + "type           = \"" + this.getType() + "\">\n"
            + "<![CDATA[\n" + this.getText() + "\n]]>\n"
            + "</cMsgMessage>\n\n");
    }


    /**
      * Returns SQL create table string.
      */
    public static String createTableString(String table, String idOption, String timeType,
                                           String textType, String tableOptions) {
        return(
               "create table " + table + " (id int not null " + idOption + "," +
               " domain varchar(255), sysMsgId int," +
               " sender varchar(128), senderHost varchar(128), senderTime " + timeType +", senderId int, senderMsgId int," +
               " receiver varchar(128), receiverHost varchar(128), receiverTime " + timeType +"," +
               " subject varchar(255), type varchar(128), text " + textType + ") " +
               tableOptions
               );
    }


    /**
      * Returns SQL preparted statement string.
      */
    public static String createPreparedStatementString(String table, String insertOption, boolean setID) {
        return(
               "insert " + insertOption + " into " + table + " (" +
               (setID?"id,":"") +
               "domain,sysMsgId," +
               "sender,senderHost,senderTime,senderId,senderMsgId," +
               "receiver,receiverHost,receiverTime," +
               "subject,type,text" +
               ") values (" +
               (setID?"?,":"") +
               "?,?," +
               "?,?,?,?,?," +
               "?,?,?," +
               "?,?,?" +
               ")"
               );
    }


    /**
      * Returns SQL preparted statement string for message.
      */
    public void fillPreparedStatement(PreparedStatement pStmt, boolean setID) {

//             msg.setReceiver("cMsg:queue");
//             new java.sql.Timestamp(msg.getSenderTime().getTime()));
//        pStmt.set(2,this.get());

    }


    /**
      * Fills message from SQL result set.
      */
    public void fillFromResultSet(ResultSet rs) throws cMsgException {

        if(rs!=null) {
            try {
                this.setDomain(rs.getString("domain"));
                this.setSysMsgId(rs.getInt("sysMsgId"));

                this.setSender(rs.getString("sender"));
                this.setSenderHost(rs.getString("senderHost"));
                this.setSenderId(rs.getInt("senderId"));
                this.setSenderTime(rs.getTime("senderTime"));
                this.setSenderMsgId(rs.getInt("senderMsgId"));

                this.setReceiver(rs.getString("receiver"));
                this.setReceiverHost(rs.getString("receiverHost"));
                this.setReceiverTime(rs.getTime("receiverTime"));

                this.setSubject(rs.getString("subject"));
                this.setType(rs.getString("type"));
                this.setText(rs.getString("text"));

            } catch (SQLException e) {
                cMsgException ce = new cMsgException("?unable to fill msg from result set");
                ce.setReturnCode(1);
                throw ce;
            }

        } else {

            this.setDomain("");
            this.setSysMsgId(0);

            this.setSender("");
            this.setSenderHost("");
            this.setSenderId(0);
            this.setSenderTime(new Date());
            this.setSenderMsgId(0);

            this.setReceiver("");
            this.setReceiverHost("");
            this.setReceiverTime(new Date());

            this.setSubject("");
            this.setType("");
            this.setText("");
        }
    }


}

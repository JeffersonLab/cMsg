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
    // general quantities

    /**
     * Unique message id created by cMsg system.
     * Used by domain server to track client's "sendAndGet" calls.
     */
    private int sysMsgId;

    /** Message exists in this domain. */
    private String domain;

    /** Is this message a sendAndGet request? */
    private boolean getRequest;

    /** Is this message a response to a sendAndGet request? */
    private boolean getResponse;

    /** Version number of cMsg. */
    private int version;


    // user-settable quantities


    /** Subject of message. */
    private String subject;

    /** Type of message. */
    private String type;

    /** Text of message. */
    private String text;

    /** Message priority set by user where 0 is both the default and the lowest value. */
    private int priority;

    /** Integer supplied by user. */
    private int userInt;

    /** Time supplied by user in milliseconds from midnight GMT, Jan 1st, 1970. */
    private long userTime;


    // sender quantities


    /** Unique name of message sender. */
    private String sender;

    /** Host sender is running on. */
    private String senderHost;

    /** Time message was sent in milliseconds from midnight GMT, Jan 1st, 1970. */
    private long senderTime;

   /** Field used by domain server in implementing "sendAndGet". */
    private int senderToken;


    // receiver quantities


    /** Unique name of message receiver. */
    private String receiver;

    /** Host receiver is running on. */
    private String receiverHost;

    /** Time message was received in milliseconds from midnight GMT, Jan 1st, 1970. */
    private long receiverTime;

    /**
     * Message receiver's id number corresponding to a subject & type pair
     * of a message subscription.
     */
    private int receiverSubscribeId;



    /**
     * Creates a proper response message to this message sent by a client calling
     * @return message with the response fields properly set.
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
     * @return copy of this message.
     */
    public cMsgMessage copy() {
        cMsgMessage msg = null;
        try {msg = (cMsgMessage) this.clone();}
        catch (CloneNotSupportedException e) {}
        return msg;
    }


    // general quantities


    /**
     * Get system id of message.
     * @return system id of message.
     */
    public int getSysMsgId() {return sysMsgId;}
    /**
     * Set system id of message. Used by the system in doing sendAndGet.
     * The user should not use this method. The cMsg system overwrites any user input.
     * @param sysMsgId system id of message.
     */
    public void setSysMsgId(int sysMsgId) {this.sysMsgId = sysMsgId;}


    /**
     * Get domain this message exists in.
     * @return domain message exists in.
     */
    public String getDomain() {return domain;}
    /**
     * Set domain this message exists in.
     * The user should not use this method. The cMsg system overwrites any user input.
     * @param domain domain this message exists in.
     */
    public void setDomain(String domain) {this.domain = domain;}


    /**
     * Is this message a response to a "sendAndGet" request?
     * @return true if this message is a response to a "sendAndGet" request.
     */
    public boolean isGetResponse() {return getResponse;}
    /**
     * Specify whether this message is a response to a "sendAndGet" message.
     * @param getResponse true if this message is a response to a "sendAndGet" message
     */
    public void setGetResponse(boolean getResponse) {this.getResponse = getResponse;}


    /**
     * Is this message a "sendAndGet" request?
     * @return true if this message is a "sendAndGet" request
     */
    public boolean isGetRequest() {return getRequest;}
    /**
     * Specify whether this message is a "sendAndGet" request.
     * The user should not use this method. The cMsg system overwrites any user input.
     * @param getRequest true if this message is a "sendAndGet" request
     */
    public void setGetRequest(boolean getRequest) {this.getRequest = getRequest;}


    /**
     * Gets the version number of this message which is the same
     * as that of the cMsg software package that created it.
     * @return version number of message.
     */
    public int getVersion() {
        return version;
    }
    /**
     * Sets the version number of this message. The version number must be the same as the
     * version number of the cMsg package - given by {@link cMsgConstants#version}.
     * The user should not use this method. The cMsg system overwrites any user input.
     * @param version version number of message
     */
    public void setVersion(int version) {
        if (version < 0) version = 0;
        this.version = version;
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
     * Set message sender.
     * The user should not use this method. The cMsg system overwrites any user input.
     * @param sender message sender.
     */
    public void setSender(String sender) {this.sender = sender;}


    /**
     * Get message sender's host computer.
     * @return message sender's host computer.
     */
    public String getSenderHost() {return senderHost;}
    /**
     * Set message sender's host computer. Set automatically by cMsg system.
     * The user should not use this method. The cMsg system overwrites any user input.
     * @param senderHost message sender's host computer.
     */
    public void setSenderHost(String senderHost) {this.senderHost = senderHost;}


    /**
     * Get time message was sent.
     * @return time message sent.
     */
    public Date getSenderTime() {return new Date(senderTime);}
    /**
     * Set time message was sent. Set automatically by cMsg system.
     * The user should not use this method. The cMsg system overwrites any user input.
     *
     * @param time time message sent.
     */
    public void setSenderTime(Date time) {
        this.senderTime = time.getTime();
    }


    /**
     * Get sender's token. Used to track asynchronous responses to
     * messages requesting responses from other clients.
     */
    public int getSenderToken() {return senderToken;}
    /**
     * Set sender's token. Used by the system in doing sendAndGet.
     * The user should not use this method. The cMsg system overwrites any user input.
     * @param senderToken sender's token.
     */
    public void setSenderToken(int senderToken) {this.senderToken = senderToken;}


    // receiver


    /**
     * Get message receiver.
     * @return message receiver.
     */
    public String getReceiver() {return receiver;}
    /**
     * Set message receiver.  Set automatically by cMsg system.
     * The user should not use this method. The cMsg system overwrites any user input.
     * @param receiver message receiver.
     */
    public void setReceiver(String receiver) {this.receiver = receiver;}


    /**
     * Get message receiver's host computer.
     * @return message receiver's host computer.
     */
    public String getReceiverHost() {return receiverHost;}
    /**
     * Set message receiver's host computer. Set automatically by cMsg system.
     * The user should not use this method. The cMsg system overwrites any user input.
     * @param receiverHost message receiver's host computer.
     */
    public void setReceiverHost(String receiverHost) {this.receiverHost = receiverHost;}


    /**
     * Get time message was received.
     * @return time message received.
     */
    public Date   getReceiverTime() {return new Date(receiverTime);}
    /**
      * Set time message was receivered. Set automatically by cMsg system.
     * The user should not use this method. The cMsg system overwrites any user input.
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
     * The user should not use this method. The cMsg system overwrites any user input.
     * @param receiverSubscribeId  receiver's subscription id number.
     */
    public void setReceiverSubscribeId(int receiverSubscribeId) {this.receiverSubscribeId = receiverSubscribeId;}



    /**
      * Returns XML representation of message as a string
      */
    public String toString() {
        return(
               "<cMsgMessage date=\"" + (new Date()) + "\"\n"
            + "     " + "domain         = \"" + this.getDomain() + "\"\n"
            + "     " + "version        = \"" + this.getVersion() + "\"\n"
            + "     " + "getResponse    = \"" + this.isGetResponse() + "\"\n"
            + "     " + "getRequest     = \"" + this.isGetRequest() + "\"\n"
            + "     " + "sender         = \"" + this.getSender() + "\"\n"
            + "     " + "senderHost     = \"" + this.getSenderHost() + "\"\n"
            + "     " + "senderTime     = \"" + this.getSenderTime() + "\"\n"
            + "     " + "userInt        = \"" + this.getUserInt() + "\"\n"
            + "     " + "priority       = \"" + this.getPriority() + "\"\n"
            + "     " + "receiver       = \"" + this.getReceiver() + "\"\n"
            + "     " + "receiverHost   = \"" + this.getReceiverHost() + "\"\n"
            + "     " + "receiverTime   = \"" + this.getReceiverTime() + "\"\n"
            + "     " + "subject        = \"" + this.getSubject() + "\"\n"
            + "     " + "type           = \"" + this.getType() + "\">\n"
            + "<![CDATA[\n" + this.getText() + "\n]]>\n"
            + "</cMsgMessage>\n\n");
    }


//     /**
//       * Returns SQL create table string.
//       */
//     public static String createTableString(String table, String idOption, String timeType,
//                                            String textType, String tableOptions) {
//         return(
//                "create table " + table + " (id int not null " + idOption + "," +
//                " domain varchar(255), sysMsgId int," +
//                " sender varchar(128), senderHost varchar(128), senderTime " + timeType +", senderId int, senderMsgId int," +
//                " receiver varchar(128), receiverHost varchar(128), receiverTime " + timeType +"," +
//                " subject varchar(255), type varchar(128), text " + textType + ") " +
//                tableOptions
//                );
//     }


//     /**
//       * Returns SQL preparted statement string.
//       */
//     public static String createPreparedStatementString(String table, String insertOption, boolean setID) {
//         return(
//                "insert " + insertOption + " into " + table + " (" +
//                (setID?"id,":"") +
//                "domain,sysMsgId," +
//                "sender,senderHost,senderTime,senderId,senderMsgId," +
//                "receiver,receiverHost,receiverTime," +
//                "subject,type,text" +
//                ") values (" +
//                (setID?"?,":"") +
//                "?,?," +
//                "?,?,?,?,?," +
//                "?,?,?," +
//                "?,?,?" +
//                ")"
//                );
//     }


//     /**
//       * Returns SQL preparted statement string for message.
//       */
//     public void fillPreparedStatement(PreparedStatement pStmt, boolean setID) {

// //             msg.setReceiver("cMsg:queue");
// //             new java.sql.Timestamp(msg.getSenderTime().getTime()));
// //        pStmt.set(2,this.get());

//     }


//     /**
//       * Fills message from SQL result set.
//       */
//     public void fillFromResultSet(ResultSet rs) throws cMsgException {

//         if(rs!=null) {
//             try {
//                 this.setDomain(rs.getString("domain"));
//                 this.setSysMsgId(rs.getInt("sysMsgId"));

//                 this.setSender(rs.getString("sender"));
//                 this.setSenderHost(rs.getString("senderHost"));
//                 this.setSenderId(rs.getInt("senderId"));
//                 this.setSenderTime(rs.getTime("senderTime"));
//                 this.setSenderMsgId(rs.getInt("senderMsgId"));

//                 this.setReceiver(rs.getString("receiver"));
//                 this.setReceiverHost(rs.getString("receiverHost"));
//                 this.setReceiverTime(rs.getTime("receiverTime"));

//                 this.setSubject(rs.getString("subject"));
//                 this.setType(rs.getString("type"));
//                 this.setText(rs.getString("text"));

//             } catch (SQLException e) {
//                 cMsgException ce = new cMsgException("?unable to fill msg from result set");
//                 ce.setReturnCode(1);
//                 throw ce;
//             }

//         } else {

//             this.setDomain("");
//             this.setSysMsgId(0);

//             this.setSender("");
//             this.setSenderHost("");
//             this.setSenderId(0);
//             this.setSenderTime(new Date());
//             this.setSenderMsgId(0);

//             this.setReceiver("");
//             this.setReceiverHost("");
//             this.setReceiverTime(new Date());

//             this.setSubject("");
//             this.setType("");
//             this.setText("");
//         }
//     }



}

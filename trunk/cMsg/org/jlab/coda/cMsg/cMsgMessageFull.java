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

import java.io.*;
import java.util.Date;
import javax.xml.parsers.* ;
import org.w3c.dom.*;
import org.xml.sax.*;


/**
 * This class contains the full functionality of a message. It extends the class
 * that users have access to by defining setters and getters that the user has
 * no need of. This class is for use only by packages that are part of the cMsg
 * implementation.
 */
public class cMsgMessageFull extends cMsgMessage {

    /** Constructor. */
    public cMsgMessageFull() {
    }


    /** Constructor using XML string generated by cMsgMessage.toString(). */
    public cMsgMessageFull(String XML) throws cMsgException {

        DocumentBuilderFactory f = DocumentBuilderFactory.newInstance();
        f.setIgnoringComments(true);
        f.setCoalescing(true);
        f.setValidating(false);

        try {
            DocumentBuilder p = f.newDocumentBuilder();
            //Document d = p.parse(new StringBufferInputStream(XML));
            Document d = p.parse(new ByteArrayInputStream(XML.getBytes()));
            Element e = d.getDocumentElement();

            fillMsgFromElement(e);

        } catch (ParserConfigurationException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        } catch (SAXException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        } catch (IOException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        }
    }


    /** Constructor using XML string generated by cMsgMessage.toString(). */
    public cMsgMessageFull(File file) throws cMsgException {

        DocumentBuilderFactory f = DocumentBuilderFactory.newInstance();
        f.setIgnoringComments(true);
        f.setCoalescing(true);
        f.setValidating(false);

        try {
            DocumentBuilder p = f.newDocumentBuilder();
            Document d = p.parse(file);
            Element e = d.getDocumentElement();

            fillMsgFromElement(e);

        } catch (ParserConfigurationException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        } catch (SAXException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        } catch (IOException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        }
    }


    // auxiliary routine fills message from Element
    private void fillMsgFromElement(Element e) throws cMsgException {

        this.setVersion(Integer.parseInt(e.getAttribute("version")));
        this.setDomain(e.getAttribute("domain"));
        this.setSysMsgId(Integer.parseInt(e.getAttribute("sysMsgId")));

        this.setGetRequest(Boolean.getBoolean(e.getAttribute("getRequest")));
        this.setGetResponse(Boolean.getBoolean(e.getAttribute("getResponse")));
        this.setNullGetResponse(Boolean.getBoolean(e.getAttribute("nullGetResponse")));

        this.setCreator(e.getAttribute("creator"));

        this.setSender(e.getAttribute("sender"));
        this.setSenderHost(e.getAttribute("senderHost"));
        //this.setSenderTime(new Date(e.getAttribute("senderTime")));
        try {
            this.setSenderTime(new Date(Long.parseLong(e.getAttribute("senderTime"))));
        }
        catch (NumberFormatException ex) {
            this.setSenderTime(new Date());
        }
        this.setSenderToken(Integer.parseInt(e.getAttribute("senderToken")));

        this.setUserInt(Integer.parseInt(e.getAttribute("userInt")));
        this.setPriority(Integer.parseInt(e.getAttribute("priority")));
        //this.setUserTime(new Date(e.getAttribute("userTime")));
        try {
            this.setUserTime(new Date(Long.parseLong(e.getAttribute("userTime"))));
        }
        catch (NumberFormatException ex) {
            this.setUserTime(new Date());
        }

        this.setReceiver(e.getAttribute("receiver"));
        this.setReceiverHost(e.getAttribute("receiverHost"));
        //this.setReceiverTime(new Date(e.getAttribute("receiverTime")));
        try {
            this.setReceiverTime(new Date(Long.parseLong(e.getAttribute("receiverTime"))));
        }
        catch (NumberFormatException ex) {
            this.setReceiverTime(new Date());
        }
        this.setReceiverSubscribeId(Integer.parseInt(e.getAttribute("receiverSubscribeId")));

        this.setSubject(e.getAttribute("subject"));
        this.setType(e.getAttribute("type"));

        this.setText((e.getFirstChild().getNodeValue()).trim());
    }


    /** Constructor using existing cMsgMessage. */
    public cMsgMessageFull(cMsgMessage m) {

        this.setVersion(m.getVersion());
        this.setDomain(m.getDomain());
        this.setSysMsgId(m.getSysMsgId());
        this.setInfo(m.getInfo());
        this.setCreator(m.getCreator());

        this.setSender(m.getSender());
        this.setSenderHost(m.getSenderHost());
        this.setSenderTime(m.getSenderTime());
        this.setSenderToken(m.getSenderToken());

        this.setUserInt(m.getUserInt());
        this.setPriority(m.getPriority());
        this.setUserTime(m.getUserTime());

        this.setReceiver(m.getReceiver());
        this.setReceiverHost(m.getReceiverHost());
        this.setReceiverTime(m.getReceiverTime());
        this.setReceiverSubscribeId(m.getReceiverSubscribeId());

        this.setSubject(m.getSubject());
        this.setType(m.getType());
        this.setText(m.getText());
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
     * Creates a proper response message to this message which was sent by a client calling
     * sendAndGet.
     *
     * @return message with the response fields properly set.
     * @throws cMsgException if this message was not sent from a "sendAndGet" method call
     */
    public cMsgMessageFull response() throws cMsgException {
        // If this message was not sent from a "sendAndGet" method call,
        // a proper response is not possible, since the sysMsgId
        // and senderToken fields will not have been properly set.
        if (!isGetRequest()) {
            throw new cMsgException("this message not sent by client calling sendAndGet");
        }
        cMsgMessageFull msg = new cMsgMessageFull();
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
    public cMsgMessageFull nullResponse() throws cMsgException {
        // If this message was not sent from a "get" method call,
        // a proper response is not possible, since the sysMsgId
        // and senderToken fields will not have been properly set.
        if (!isGetRequest()) {
            throw new cMsgException("this message not sent by client calling sendAndGet");
        }
        cMsgMessageFull msg = new cMsgMessageFull();
        msg.sysMsgId = sysMsgId;
        msg.senderToken = senderToken;
        msg.info = isGetResponse | isNullGetResponse;
        return msg;
    }



    /**
     * Converts existing message to response of supplied message.
     */
    public void makeResponse(cMsgMessageFull msg) {
        this.sysMsgId    = msg.getSysMsgId();
        this.senderToken = msg.getSenderToken();
        this.info = isGetResponse;
    }


    public void makeNullResponse(cMsgMessageFull msg) {
        this.sysMsgId    = msg.getSysMsgId();
        this.senderToken = msg.getSenderToken();
        this.info = isGetResponse | isNullGetResponse;
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
    public void setGetRequest(boolean getRequest) {
        info = getRequest ? info|isGetRequest : info & ~isGetRequest;
    }


    /**
     * Set the info member.
     * @param info value of info member
     */
    public void setInfo(int info) {
        this.info = info;
    }


    /**
     * Sets the version number of this message. The version number must be the same as the
     * version number of the cMsg package - given by {@link cMsgConstants#version}.
     * @param version version number of message
     */
    public void setVersion(int version) {
        if (version < 0) version = 0;
        this.version = version;
    }


    /**
     * Sets the creator of this message.
     * @param creator creator of this message.
     */
    public void setCreator(String creator) {this.creator = creator;}


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

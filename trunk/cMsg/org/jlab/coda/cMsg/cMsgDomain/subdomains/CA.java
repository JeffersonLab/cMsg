// still to do:
//   server shutdown



/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *     E.Wolin, 5-oct-2004                                                    *
 *                                                                            *
 *     Author: Elliott Wolin                                                  *
 *             wolin@jlab.org                    Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-7365             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/


package org.jlab.coda.cMsg.cMsgDomain.subdomains;

import org.jlab.coda.cMsg.cMsgConstants;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgDomain.cMsgClientInfo;
import org.jlab.coda.cMsg.cMsgDomain.cMsgHandleRequestsAbstract;

import gov.aps.jca.*;
import gov.aps.jca.dbr.DBR;

import java.nio.ByteBuffer;
import java.io.IOException;


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * cMsg subdomain handler for channel access (CA) subdomain.
 *
 * Executes send/get as CA put/get command.
 *
 * cMsgSend mapped to CA put.
 * cMsgGet mapped to CA get.
 *
 * Uses JCA+CAJ.
 *
 * @author Elliott Wolin
 * @version 1.0
 */
public class CA extends cMsgHandleRequestsAbstract {


    /** registration params. */
    private cMsgClientInfo myClientInfo;


    /** UDL remainder for this subdomain handler. */
    private String myUDLRemainder;


    /** JCALibrary and context. */
    static private JCALibrary jca  = null;
    static private Context context = null;
    

    // get JCA library and context
    static {
	try {
	    jca     = JCALibrary.getInstance();
	    context = jca.createContext(JCALibrary.CHANNEL_ACCESS_JAVA);
	} catch (Exception e) {
	    e.printStackTrace();
	    System.err.println(e);
	}
    }


    /** direct buffer needed for nio socket IO. */
    private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);


    /** misc params. */
    static double contextPend = 3.0;
    static double getPend     = 3.0;
    static double putPend     = 3.0;



//-----------------------------------------------------------------------------


    /**
     * Method to tell if the "send" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSendRequest}
     * method.
     *
     * @return true if get implemented in {@link #handleSendRequest}
     */
    public boolean hasSend() {
        return true;
    };


//-----------------------------------------------------------------------------


    /**
     * Method to tell if the "syncSend" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSyncSendRequest}
     * method.
     *
     * @return true if send implemented in {@link #handleSyncSendRequest}
     */
    public boolean hasSyncSend() {
        return true;
    };


//-----------------------------------------------------------------------------


    /**
     * Method to tell if the "get" cMsg API function is implemented
     * by this interface implementation in the {@link #handleGetRequest}
     * method.
     *
     * @return true if get implemented in {@link #handleGetRequest}
     */
    public boolean hasGet() {
        return true;
    };


//-----------------------------------------------------------------------------


    /**
     * Method to tell if the "subscribe" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSubscribeRequest}
     * method.
     *
     * @return true if subscribe implemented in {@link #handleSubscribeRequest}
     */
    public boolean hasSubscribe() {
        return false;
    };


//-----------------------------------------------------------------------------


    /**
     * Method to tell if the "unsubscribe" cMsg API function is implemented
     * by this interface implementation in the {@link #handleUnsubscribeRequest}
     * method.
     *
     * @return true if unsubscribe implemented in {@link #handleUnsubscribeRequest}
     */
    public boolean hasUnsubscribe() {
        return false;
    };


//-----------------------------------------------------------------------------


    /**
     * Method to give the subdomain handler the appropriate part
     * of the UDL the client used to talk to the domain server.
     *
     * @param UDLRemainder last part of the UDL appropriate to the subdomain handler
     * @throws cMsgException
     */
    public void setUDLRemainder(String UDLRemainder) throws cMsgException {
        myUDLRemainder=UDLRemainder;
    }
    

//-----------------------------------------------------------------------------


    /**
     * Method to register domain client.
     *
     * @param info information about client
     * @throws cMsgException if unable to register
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {
	myClientInfo = info;
    }
    

//-----------------------------------------------------------------------------


    /**
     * Method to handle message sent by client.
     *
     * @param msg message from sender
     * @throws cMsgException if a channel to the client is closed, cannot be created,
     *                          or socket properties cannot be set
     */
    public void handleSendRequest(cMsgMessage msg) throws cMsgException {

	// create channel 
	Channel channel;
	try {
	    channel = context.createChannel(msg.getSubject());
	    context.pendIO(contextPend);
	} catch  (CAException e) {
	    cMsgException ce = new cMsgException(e.toString());
	    ce.setReturnCode(1);
	    throw ce;
	} catch  (TimeoutException e) {
	    cMsgException ce = new cMsgException(e.toString());
	    ce.setReturnCode(1);
	    throw ce;
	}


	// put value
	try {
	    channel.put(msg.getText());
	    context.pendIO(putPend);
	} catch  (CAException e) {
	    cMsgException ce = new cMsgException(e.toString());
	    ce.setReturnCode(1);
	    throw ce;
	} catch  (TimeoutException e) {
	    cMsgException ce = new cMsgException(e.toString());
	    ce.setReturnCode(1);
	    throw ce;
	}

	
	// disconnect channel
	try {
	    channel.destroy();
	} catch  (CAException e) {
	    e.printStackTrace();
	    System.err.println(e);
	}
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle message sent by domain client in synchronous mode.
     * It requries an integer response from the subdomain handler.
     *
     * @param msg message from sender
     * @return response from subdomain handler
     * @throws cMsgException
     */
    public int handleSyncSendRequest(cMsgMessage msg) throws cMsgException {
	handleSendRequest(msg);
        return 0;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to synchronously get a single message from the server for a given
     * subject and type -- perhaps from a specified receiver.
     *
     * @param message message requesting what sort of message to get
     */
    public void handleGetRequest(cMsgMessage message) {

	boolean failed = false;


	// create reply message
	cMsgMessage cmsg = new cMsgMessage();
	cmsg.setDomain("cMsg");                   // just use domain for now
	// 	    cmsg.setSysMsgId();
	// 	    cmsg.setSender("EPICS");
	// 	    cmsg.setSenderHost();
	// 	    cmsg.setSenderTime(new Date());
	// 	    cmsg.setSenderId();
	// 	    cmsg.setSenderMsgId();
	// 	    cmsg.setReceiver(myClientInfo.getName());
	// 	    cmsg.setReceiverHost(myClientInfo.getClientHost());
	// 	    cmsg.setReceiverTime(new Date());
	// 	    cmsg.setReceiverSubscribeId();
	// 	    cmsg.setSubject(message.getSubject());
	// 	    cmsg.setType(message.getType());


	// create channel 
	Channel channel = null;
	try {
	    channel = context.createChannel(message.getSubject());
	    context.pendIO(contextPend);
	} catch  (CAException e) {
	    cmsg.setText(null);
	} catch  (TimeoutException e) {
	    cmsg.setText(null);
	}


	// get value
	DBR dbr;
	if(channel!=null) {
	    try {
		dbr = channel.get();
		context.pendIO(getPend);
		cmsg.setText(null);
	    } catch  (CAException e) {
		cmsg.setText(null);
	    } catch  (TimeoutException e) {
		cmsg.setText(null);
	    }
	}

	
	// return message
	try {
	    deliverMessage(myClientInfo.getChannel(),buffer,cmsg,null,cMsgConstants.msgGetResponse);
	} catch (IOException e) {
	    e.printStackTrace();
	    System.err.println(e);
	}


	// disconnect channel
	try {
	    if(channel!=null)channel.destroy();
	} catch  (CAException e) {
	    e.printStackTrace();
	    System.err.println(e);
	}

    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle unget request sent by domain client (hidden from user).
     *
     * @param id message id refering to these specific subject and type values
     */
    public void handleUngetRequest(int id) {
        // do nothing
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle subscribe request sent by domain client.
     *
     * @param subject message subject to subscribe to
     * @param type message type to subscribe to
     * @param receiverSubscribeId message id refering to these specific subject and type values
     * @throws cMsgException if no client information is available or a subscription for this
     *                          subject and type already exists
     */
    public void handleSubscribeRequest(String subject, String type,
                                       int receiverSubscribeId) throws cMsgException {
        // monitor on
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle sunsubscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     */
    public void handleUnsubscribeRequest(String subject, String type, int receiverSubscribeId) {
        // monitor off
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle keepalive sent by domain client checking to see
     * if the domain server socket is still up. Normally nothing needs to
     * be done as the domain server simply returns an "OK" to all keepalives.
     * This method is run after all exchanges between domain server and client.
     */
    public void handleKeepAlive() {
        // do nothing...
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle a client shutdown.
     *
     * @throws cMsgException
     */
    public void handleClientShutdown() throws cMsgException {
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle a complete name server shutdown.
     * This method is run after all exchanges between domain server and client but
     * before the server is killed (since that is what is running this
     * method).
     */
    public void handleServerShutdown() throws cMsgException {
	try {
	    context.destroy();
	} catch (Exception e) {
	    System.err.println(e);
	}
    }


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
}


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


import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgDomain.cMsgHandleRequests;
import org.jlab.coda.cMsg.cMsgDomain.cMsgClientInfo;

import java.sql.*;
import java.util.regex.*;



/**
 * cMsg subdomain handler for database subdomain.
 *
 * Executes sql statement from msg payload.
 * Gets database parameters from UDL.
 *
 * @author Elliott Wolin
 * @version 1.0
 *
 */
public class database implements cMsgHandleRequests {


    // register params
    private String myName;
    private String myHost;
    private int myPort;


    /** UDL remainder for this subdomain handler. */
    private String myUDLRemainder;


    // database access objects
    Connection myCon         = null;
    Statement myStmt         = null;


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


    /**
     * Method to tell if the "get" cMsg API function is implemented
     * by this interface implementation in the {@link #handleGetRequest}
     * method.
     *
     * @return true if get implemented in {@link #handleGetRequest}
     */
    public boolean hasGet() {
        return false;
    };


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
    

    /**
     * Method to register domain client.
     * Creates separate database connection for each client connection.
     * UDL contains driver name, database JDBC URL, account, and password
     * Column names are fixed (domain, sender, subject, etc.).
     *
     * @param info information about client
     * @throws cMsgException upon error
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {

        myName = info.getName();


	myName=name;
	myHost=host;
	myPort=port;


	// db params
	String driver    = null;
	String URL       = null;
	String account   = null;
	String password  = null;
	
	
	// extract db params from UDL
	int ind= myUDLRemainder.indexOf("?");
	if(ind!=0) {
	    cMsgException ce = new cMsgException("illegal UDL");
	    ce.setReturnCode(1);
	    throw ce;
	} else {
	    String remainder=myUDLRemainder + "&";


	    //  extract params
	    Pattern p;
	    Matcher m;
	    try {
		// driver required
		p = Pattern.compile("[&\\?]driver=(.*?)&",Pattern.CASE_INSENSITIVE);
		m = p.matcher(remainder);
		m.find();
		driver = m.group(1);
		
		// URL required
		p = Pattern.compile("[&\\?]url=(.*?)&",Pattern.CASE_INSENSITIVE);
		m = p.matcher(remainder);
		m.find();
		URL= m.group(1);
		
		// account not required
		p = Pattern.compile("[&\\?]account=(.*?)&",Pattern.CASE_INSENSITIVE);
		m = p.matcher(remainder);
		if(m.find()) {
			account = m.group(1);
		}
		
		// password not required
		p = Pattern.compile("[&\\?]password=(.*?)&",Pattern.CASE_INSENSITIVE);
		m = p.matcher(remainder);
		if(m.find()) {
			password = m.group(1);
		}

	    } catch (Exception e) {
		e.printStackTrace();
		cMsgException ce = new cMsgException(e.getMessage());
		ce.setReturnCode(1);
		throw ce;
	    }
	}


	// load driver
	try {
	    Class.forName(driver);
	} catch (Exception e) {
	    System.out.println(e);
	    e.printStackTrace();
	    cMsgException ce = new cMsgException("registerClient: unable to load driver");
	    ce.setReturnCode(1);
	    throw ce;
	}


	// create connection
	try {
	    myCon  = DriverManager.getConnection(URL,account,password);
	} catch (Exception e) {
	    System.out.println(e);
	    e.printStackTrace();
	    cMsgException ce = new cMsgException("registerClient: unable to connect to database");
	    ce.setReturnCode(1);
	    throw ce;
	}


	// create statement
	try {
	    myStmt  = myCon.createStatement();
	} catch (Exception e) {
	    System.out.println(e);
	    e.printStackTrace();
	    cMsgException ce = new cMsgException("registerClient: unable to create statement");
	    ce.setReturnCode(1);
	    throw ce;
	}
    }
    

    /**
     * Executes sql insert or update statement from message payload.
     *
     * @param msg message from sender.
     * @throws cMsgException
     */
    public void handleSendRequest(cMsgMessage msg) throws cMsgException {

	String sql = msg.getText();

	Pattern p1 = Pattern.compile("^\\s*insert\\s+",Pattern.CASE_INSENSITIVE);
	Pattern p2 = Pattern.compile("^\\s*update\\s+",Pattern.CASE_INSENSITIVE);
	Pattern p3 = Pattern.compile("^\\s*delete\\s+",Pattern.CASE_INSENSITIVE);

	Matcher m1 = p1.matcher(sql);
	Matcher m2 = p2.matcher(sql);
	Matcher m3 = p3.matcher(sql);

	if(m1.find()||m2.find()||m3.find()) {
	    try {
		myStmt.executeUpdate(sql);
	    } catch (Exception e) {
		System.out.println(e);
		e.printStackTrace();
		throw new cMsgException("handleSendRequest: unable to execute: " + sql);
	    }
	} else {
	    cMsgException ce = new cMsgException("handleSendRequest: illegal sql: " + msg.getText());
	    ce.setReturnCode(1);
	    throw ce;
	}
    }
	

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
	return(0);
    }
    

    /**
     * Method to handle subscribe request sent by domain client.
     * Not implemented.
     *
     * @param subject message subject to subscribe to
     * @param type message type to subscribe to
     * @param receiverSubscribeId message id refering to these specific subject and type values
     * @throws cMsgException if no client information is available or a subscription for this
     *                          subject and type already exists
     */
    public void handleSubscribeRequest(String subject, String type,
                                       int receiverSubscribeId) throws cMsgException {
	// do nothing...
    }


    /**
     * Method to handle sunsubscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     * Not implemented.
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     * @param receiverSubscribeId message id refering to these specific subject and type values
     */
    public void handleUnsubscribeRequest(String subject, String type, int receiverSubscribeId) {
        // do nothing...
    }


    /**
     * Method to synchronously get a single message from the server for a given
     * subject and type -- perhaps from a specified receiver.
     *
     * @param message message requesting what sort of message to get
     */
    public void handleGetRequest(cMsgMessage message) {
        // do nothing...
    }


    /**
     * Method to handle unget request sent by domain client (hidden from user).
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     */
    public void handleUngetRequest(String subject, String type) {
        // do nothing
    }


    /**
     * Method to handle keepalive sent by domain client checking to see
     * if the domain server socket is still up. Normally nothing needs to
     * be done as the domain server simply returns an "OK" to all keepalives.
     * This method is run after all exchanges between domain server and client.
     */
    public void handleKeepAlive() {
        // do nothing...
    }


    /**
     * Method to handle a client shutdown.
     *
     * @throws cMsgException
     */
    public void handleClientShutdown() throws cMsgException {
	try {
	    myStmt.close();
	    myCon.close();
	} catch (Exception e) {
	    throw(new cMsgException("database sub-domain handler shutdown error"));
	}
    }


    /**
     * Method to handle a complete name server down.
     * This method is run after all exchanges between domain server and client but
     * before the server is killed (since that is what is running this
     * method).
     */
    public void handleServerShutdown() throws cMsgException {
    }

}

// still to do:
//   return code values
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


package org.jlab.coda.cMsg.plugins;


import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgHandleRequests;
import org.jlab.coda.cMsg.cMsgException;
import java.sql.*;
import java.util.regex.*;



/**
 * cMsg subdomain handler for LogTable subdomain.
 *
 * Logs cMsgMessage messages to SQL database.
 * Gets database parameters from UDL.
 *
 * @author Elliott Wolin
 * @version 1.0
 *
 */
public class LogTable implements cMsgHandleRequests {


    // name
    private String myName;


    /** UDL remainder for this subdomain handler. */
    private String myUDLRemainder;


    // database access objects
    Connection myCon         = null;
    PreparedStatement myStmt = null;


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
     * Method to see if domain client is registered.
     * Allows unlimited connections per client (why not...).
     *
     * @param name name of client
     * @return true if client registered, false otherwise
     */
    public boolean isRegistered(String name) {
        return false;
    }


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
     * UDL contains driver name, database JDBC URL, account, password, and table name to use.
     * Column names are fixed (domain, sender, subject, etc.).
     *
     * @param name name of client
     * @param host host client is running on
     * @param port port client is listening on
     * @throws cMsgException upon error
     */
    public void registerClient(String name, String host, int port) throws cMsgException {

	myName=name;


	// extract db params from UDL
// 	    String driver    = "com.mysql.jdbc.Driver";
// 	    String URL       = "jdbc:mysql://lucy/cmsg";
	String driver    = null;
	String URL       = null;
	String account   = null;
	String password  = null;
	String table     = null;
	
	
	// get table name, etc from UDLRemainder
	int ind= myUDLRemainder.indexOf("?");
	if(ind!=0) {
	    cMsgException ce = new cMsgException("illegal UDL");
	    ce.setReturnCode(1);
	    throw ce;
	} else {
	    String remainder=myUDLRemainder + "&";


	    //  extract database params
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

		// table required
		p = Pattern.compile("[&\\?]table=(.*?)&",Pattern.CASE_INSENSITIVE);
		m = p.matcher(remainder);
		m.find();
		table = m.group(1);

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
	    myStmt = myCon.prepareStatement("insert into " + table + " (" + 
					    "domain,sysMsgId,sender,senderHost,senderTime," +
					    "senderId,senderMsgId,senderToken,receiver,receiverHost,"+
					    "receiverTime,subject,type,text" +
					    ") values (" +
					    "?,?,?,?,?," +
					    "?,?,?,?,?," +
					    "?,?,?,?" +
					    ")");
	} catch (Exception e) {
	    System.out.println(e);
	    e.printStackTrace();
	    cMsgException ce = new cMsgException("registerClient: unable to create statement object");
	    ce.setReturnCode(1);
	    throw ce;
	}
    }
    

    /**
     * Method to unregister domain client.
     */
    public void unregisterClient() {
    }


    /**
     * Method to handle message sent by client.
     * Inserts message into SQL database table via JDBC.
     *
     * @param msg message from sender
     * @throws cMsgException if a channel to the client is closed, cannot be created,
     *                          or socket properties cannot be set
     */
    public void handleSendRequest(cMsgMessage msg) throws cMsgException {
	try {
	    msg.setReceiver("cMsg:LogTable");
	    myStmt.setString	(1,  msg.getDomain());
	    myStmt.setInt   	(2,  msg.getSysMsgId());
	    myStmt.setString	(3,  msg.getSender());
	    myStmt.setString	(4,  msg.getSenderHost());
	    myStmt.setTimestamp (5,  new java.sql.Timestamp(msg.getSenderTime().getTime()));
	    myStmt.setInt   	(6,  msg.getSenderId());
	    myStmt.setInt   	(7,  msg.getSenderMsgId());
	    myStmt.setInt   	(8,  msg.getSenderToken());
	    myStmt.setString	(9,  msg.getReceiver());
	    myStmt.setString	(10, msg.getReceiverHost());
	    myStmt.setTimestamp (11, new java.sql.Timestamp(msg.getReceiverTime().getTime()));
	    myStmt.setString	(12, msg.getSubject());
	    myStmt.setString	(13, msg.getType());
	    myStmt.setString	(14, msg.getText());
	    myStmt.executeUpdate();
	} catch (Exception e) {
	    System.out.println(e);
	    e.printStackTrace();
	    throw new cMsgException("handleSendRequest: unable to insert into database");
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
	try {
	    handleSendRequest(msg);
	    return(0);
	} catch (cMsgException e) {
	    throw e;
	}
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
     */
    public void handleUnsubscribeRequest(String subject, String type) {
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
     * Method to handle a disconnect request sent by domain client.
     * Normally nothing needs to be done as the domain server simply returns an
     * "OK" and closes the channel. This method is run after all exchanges between
     * domain server and client.
     */
    public void handleDisconnect() {
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
	    throw(new cMsgException("sub-domain handler shutdown error"));
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

// still to do:
//   need db open parameters
//   need to feed back "not implemented" to caller



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
      public boolean hasSend() {return true;};


      /**
       * Method to tell if the "get" cMsg API function is implemented
       * by this interface implementation in the {@link #handleGetRequest}
       * method.
       *
       * @return true if get implemented in {@link #handleGetRequest}
       */
      public boolean hasGet() {return false;};


      /**
       * Method to tell if the "subscribe" cMsg API function is implemented
       * by this interface implementation in the {@link #handleSubscribeRequest}
       * method.
       *
       * @return true if subscribe implemented in {@link #handleSubscribeRequest}
       */
      public boolean hasSubscribe() {return false;};


      /**
       * Method to tell if the "unsubscribe" cMsg API function is implemented
       * by this interface implementation in the {@link #handleUnsubscribeRequest}
       * method.
       *
       * @return true if unsubscribe implemented in {@link #handleUnsubscribeRequest}
       */
      public boolean hasUnsubscribe() {return false;};


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


	// debug...will extract from UDL
	String driver    = "com.mysql.jdbc.Driver";
	String URL       = "jdbc:mysql://lucy/cmsg";
	String account   = "wolin";
	String password  = "";
	String tableName = "cmsg";


	// create database connection for each client connection
	try {
	    Class.forName(driver);
	    myCon  = DriverManager.getConnection(URL,account,password);
	    myStmt = myCon.prepareStatement("insert into " + tableName + " (" + 
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
	    throw new cMsgException("registerClient: unable to connect to database");
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
        return 0;
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
     * Method to get a single message from the server for a given
     * subject and type.
     *
     * @param subject subject of message to get
     * @param type type of message to get
     * @return cMsgMessage message obtained by this get
     */
    public cMsgMessage handleGetRequest(String subject, String type) {
        // do nothing...
        return null;
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


    public void handleClientShutdown() throws cMsgException {
	// do nothing...
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

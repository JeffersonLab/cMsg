// still to do:
//   what if exiting table incompatible with current message format?



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


package org.jlab.coda.cMsg.subdomains;

import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgSubdomainAbstract;
import org.jlab.coda.cMsg.cMsgClientInfo;
import org.jlab.coda.cMsg.cMsgConstants;

import java.nio.ByteBuffer;
import java.io.IOException;
import java.sql.*;
import java.util.regex.*;
import java.util.Date;



//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * cMsg subdomain handler for queue subdomain.
 *
 * UDL:  cMsg:cMsg://host:port/queue/myQueueName?driver=myDriver&url=myURL&account=muAccount&password=myPassword
 *
 * e.g. cMsg:cMsg://ollie/queue/myQueue?driver=com.mysql.jdbc.Driver&url=jdbc:mysql://myHost/myDatabase
 *
 * stores/retrieves cMsgMessage messages from SQL database.
 * Gets database parameters from UDL.
 *
 * Checked using mySQL.
 *
 * @author Elliott Wolin
 * @version 1.0
 *
 */
public class queue extends cMsgSubdomainAbstract {


    /** registration params. */
    private cMsgClientInfo myClientInfo;


    /** UDL remainder for this subdomain handler. */
    private String myUDLRemainder;


    /** direct buffer needed for nio socket IO. */
    private ByteBuffer myBuffer = ByteBuffer.allocateDirect(2048);


    // database access objects
    String myQueueName        = null;
    String myTableName        = null;
    String myDBType           = null;
    Connection myCon          = null;
    Statement myStmt          = null;
    PreparedStatement myPStmt = null;


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
     * Method to tell if the "subscribeAndGet" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSubscribeAndGetRequest}
     * method.
     *
     * @return true if subscribeAndGet implemented in {@link #handleSubscribeAndGetRequest}
     */
    public boolean hasSubscribeAndGet() {
        return false;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to tell if the "sendAndGet" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSendAndGetRequest}
     * method.
     *
     * @return true if sendAndGet implemented in {@link #handleSendAndGetRequest}
     */
    public boolean hasSendAndGet() {
        return true;
    }


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
     * Creates separate database connection for each client connection.
     * UDL contains driver name, database JDBC URL, account, password, and table name to use.
     * Column names are fixed (domain, sender, subject, etc.).
     *
     * @param info information about client
     * @throws cMsgException upon error
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {

        Pattern p;
        Matcher m;
        String remainder = null;
        String sql;


        // db parameters
        String driver = null;
        String URL = null;
        String account = null;
        String password = null;


        // extract queue name from UDL remainder
        if(myUDLRemainder.indexOf("?")>0) {
            p = Pattern.compile("^(.+?)(\\?.*)$");
            m = p.matcher(myUDLRemainder);
            if(m.find()) {
                myQueueName = m.group(1);
                remainder   = m.group(2);
            } else {
                cMsgException ce = new cMsgException("?illegal UDL");
                ce.setReturnCode(1);
                throw ce;
            }
        } else {
            cMsgException ce = new cMsgException("?illegal UDL...no remainder");
            ce.setReturnCode(1);
            throw ce;
        }


        //  extract db params from remainder...driver and url required
        remainder = remainder + "&";


        // driver
        p = Pattern.compile("[&\\?]driver=(.*?)&", Pattern.CASE_INSENSITIVE);
        m = p.matcher(remainder);
        try {
            m.find();
            driver = m.group(1);
        } catch (IllegalStateException e) {
            cMsgException ce = new cMsgException("?illegal UDL...no driver");
            ce.setReturnCode(1);
            throw ce;
        }


        // URL
        p = Pattern.compile("[&\\?]url=(.*?)&", Pattern.CASE_INSENSITIVE);
        m = p.matcher(remainder);
        try {
            m.find();
            URL = m.group(1);
        } catch (IllegalStateException e) {
            cMsgException ce = new cMsgException("?illegal UDL...no URL");
            ce.setReturnCode(1);
            throw ce;
        }


        // account not required
        p = Pattern.compile("[&\\?]account=(.*?)&", Pattern.CASE_INSENSITIVE);
        m = p.matcher(remainder);
        if (m.find()) account = m.group(1);


        // password not required
        p = Pattern.compile("[&\\?]password=(.*?)&", Pattern.CASE_INSENSITIVE);
        m = p.matcher(remainder);
        if (m.find()) password = m.group(1);



        // load driver
        try {
            Class.forName(driver);
        } catch (ClassNotFoundException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException("?registerClient: unable to load driver");
            ce.setReturnCode(1);
            throw ce;
        }


        // create connection
        try {
            myCon = DriverManager.getConnection(URL, account, password);
        } catch (SQLException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException("?registerClient: unable to connect to database");
            ce.setReturnCode(1);
            throw ce;
        }


        // create statement object, get db type, and check if table exists
        boolean tableExists = false;
        myTableName="cMsgQueue_" + myQueueName;
        try {
            myStmt=myCon.createStatement();

            DatabaseMetaData dbmeta = myCon.getMetaData();
            myDBType=dbmeta.getDatabaseProductName();

            ResultSet dbrs = dbmeta.getTables(null,null,myTableName,new String [] {"TABLE"});
            if(dbrs.next()) tableExists = dbrs.getString(3).equalsIgnoreCase(myTableName);

        } catch (SQLException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException("?registerClient: unable to get db metadata");
            ce.setReturnCode(1);
            throw ce;
        }


        // create table if it doesn't exist
        if(!tableExists) {
            if(myDBType.equalsIgnoreCase("mysql")) {
                sql = cMsgMessage.createTableString(myTableName,"auto_increment primary key","datetime","text","");
            } else {
                sql = cMsgMessage.createTableString(myTableName,"","time","clob","");
            }

            try {
                myStmt.executeUpdate(sql);
            } catch (SQLException e) {
                e.printStackTrace();
                cMsgException ce = new cMsgException("?registerClient: unable to create table " + myTableName);
                ce.setReturnCode(1);
                throw ce;
            }
        }


        // create prepared statement
        if(myDBType.equalsIgnoreCase("mysql")) {
            sql = cMsgMessage.createPreparedStatementString(myTableName,"delayed",false);
        } else {
            sql = cMsgMessage.createPreparedStatementString(myTableName,"",true);
        }
        try {
            myCon.prepareStatement(sql);
        } catch (SQLException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException("?registerClient: unable to create prepared statement for " + myTableName);
            ce.setReturnCode(1);
            throw ce;
        }

    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle message sent by client.
     * Inserts message into SQL database table via JDBC.
     *
     * @param msg message from sender
     * @throws cMsgException if a channel to the client is closed, cannot be created,
     *                       or socket properties cannot be set
     */
    public void handleSendRequest(cMsgMessage msg) throws cMsgException {

//         try {
//             msg.fillPreparedStatement(myPStmt,???);
//             myPStmt.executeUpdate();
//         } catch (SQLException e) {
//             e.printStackTrace();
//             throw new cMsgException("?handleSendRequest: unable to insert into queue");
//         }
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
        try {
            handleSendRequest(msg);
            return(0);
        } catch (cMsgException e) {
            return(1);
        }
    }


//-----------------------------------------------------------------------------


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


//-----------------------------------------------------------------------------


    /**
     * Method to handle sunsubscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     * Not implemented.
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     * @param receiverSubscribeId message id refering to these specific subject and type values
     */
    public void handleUnsubscribeRequest(String subject, String type, int receiverSubscribeId) throws cMsgException {
        // do nothing...
    }


//-----------------------------------------------------------------------------

    /**
     * Method to synchronously get a single message from a receiver by sending out a
     * message to be responded to.
     *
     * Currently just returns message at head of queue.
     *
     * @param message message requesting what sort of message to get
     */
    public void handleSendAndGetRequest(cMsgMessage message) throws cMsgException {

        // create msg
        cMsgMessage response = message.response();


        // retrieve oldest entry and fill message, send nulls if queue empty
        String sql = "select * from " + myTableName + " order by senderTime limit 1";
        try {
            ResultSet rs = myStmt.executeQuery(sql);
            rs.next();
            response.fillFromResultSet(rs);

        } catch (SQLException e) {
            cMsgException ce = new cMsgException("?unable to select from table " + myTableName);
            ce.setReturnCode(1);
            throw ce;
        }


        // send message
        try {
            deliverMessage(myClientInfo.getChannel(),myBuffer,response,null,cMsgConstants.msgGetResponse);

        } catch (IOException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }
    }


//-----------------------------------------------------------------------------


    /**
     * to synchronously get a single message from the server for a one-time
     * subscription of a subject and type.
     *
     * @param subject message subject subscribed to
     * @param type    message type subscribed to
     * @param id      message id refering to these specific subject and type values
     */
    public void handleSubscribeAndGetRequest(String subject, String type, int id) throws cMsgException {
        // no nothing
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle unget request sent by domain client (hidden from user).
     *
     * @param id message id refering to these specific subject and type values
     */
    public void handleUngetRequest(int id) throws cMsgException {
        // do mothing
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle keepalive sent by domain client checking to see
     * if the domain server socket is still up. Normally nothing needs to
     * be done as the domain server simply returns an "OK" to all keepalives.
     * This method is run after all exchanges between domain server and client.
     */
    public void handleKeepAlive() throws cMsgException {
        // do nothing...
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle a client shutdown.
     *
     * @throws cMsgException
     */
    public void handleClientShutdown() throws cMsgException {
        try {
            myStmt.close();
            myPStmt.close();
            myCon.close();
        } catch (SQLException e) {
            throw(new cMsgException("?queue sub-domain handler shutdown error"));
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle a complete name server down.
     * This method is run after all exchanges between domain server and client but
     * before the server is killed (since that is what is running this
     * method).
     */
    public void handleServerShutdown() throws cMsgException {
    }


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
}


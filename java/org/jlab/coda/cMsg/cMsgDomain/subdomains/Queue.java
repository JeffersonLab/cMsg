// still to do:


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

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgSubdomainAdapter;
import org.jlab.coda.cMsg.common.cMsgClientInfo;
import org.jlab.coda.cMsg.common.cMsgDeliverMessageInterface;
import org.jlab.coda.cMsg.common.cMsgMessageFull;

import java.io.*;
import java.sql.*;
import java.util.regex.*;
import java.net.*;
import java.nio.channels.*;



//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * cMsg subdomain handler for Queue subdomain.<p>
 *
 * UDL:  cMsg://host:port/Queue/myQueueName?driver=myDriver&amp;url=myURL&amp;account=myAccount&amp;password=myPassword<p>
 *
 * e.g. cMsg://localhost/Queue/ejw?driver=com.mysql.jdbc.Driver&amp;url=jdbc:mysql://halldweb1/test&amp;account=wolin<p>
 *
 * Stores/retrieves cMsgMessageFull messages from SQL database.
 * Gets database parameters from UDL.
 * Supported databases so far:  mySQL, PostgreSQL (not tested yet...).
 *
 * @author Elliiott Wolin
 * @version 1.0
 *
 */
public class Queue extends cMsgSubdomainAdapter {


    /** registration params. */
    private cMsgClientInfo myClientInfo;


    /** UDL remainder for this subdomain handler. */
    private String myUDLRemainder;


    /** Object used to deliver messages to the client. */
    private cMsgDeliverMessageInterface myDeliverer;


    // database access objects
    private String myQueueName        = null;
    private String myTableName        = null;
    private Connection myCon          = null;
    private Statement myStmt          = null;
    private PreparedStatement myPStmt = null;


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     * @return true
     */
    public boolean hasSend() {
        return true;
    };


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     * @return true
     */
    public boolean hasSendAndGet() {
        return true;
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     * @return true
     */
    public boolean hasSyncSend() {
        return true;
    };


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @param UDLRemainder {@inheritDoc}
     * @throws cMsgException never
     */
    public void setUDLRemainder(String UDLRemainder) throws cMsgException {
        myUDLRemainder=UDLRemainder;
    }


//-----------------------------------------------------------------------------


    /**
     * Creates separate database connection for each client connection.
     * UDL contains driver name, database JDBC URL, account, password, and table name to use.
     *
     * @param info information about client
     * @throws cMsgException if improper UDL, cannot connect to database,
     *                       cannot get database metadata
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {

        Pattern p;
        Matcher m;
        String remainder;


        // db parameters
        String driver;
        String URL;
        String account = null;
        String password = null;


        // set myClientInfo
        myClientInfo=info;


        // Get an object enabling this handler to communicate
        // with only this client in this cMsg subdomain.
        myDeliverer = info.getDeliverer();


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
        String myDBType;
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
        if(!tableExists) createTable(myDBType);


        // create prepared statement
        createPreparedStatement(myDBType);

    }


//-----------------------------------------------------------------------------


    /**
     * Inserts message into SQL database table via JDBC.
     *
     * @param msg {@inheritDoc}
     * @throws cMsgException if sql error
     */
    synchronized public void handleSendRequest(cMsgMessageFull msg) throws cMsgException {

        // get creator before payload compression
        String creator = null;
        try {
            cMsgPayloadItem creatorItem = msg.getPayloadItem("cMsgCreator");
            if(creatorItem!=null)creator=creatorItem.getString();
        } catch (cMsgException e) {
            System.err.println("?Queue domain...message has no creator!");
        }


        // compress payload
        msg.compressPayload();


        // serialize compressed message, then send to database
        try {
            ByteArrayOutputStream baos;
            ObjectOutputStream oos;

            baos = new ByteArrayOutputStream();
            oos = new ObjectOutputStream(baos);
            oos.writeObject(msg);
            oos.close();

            int i = 1;
            myPStmt.setTimestamp(i++, new java.sql.Timestamp(msg.getReceiverTime().getTime()));
            myPStmt.setString(i++,    creator);
            myPStmt.setString(i++,    msg.getSubject());
            myPStmt.setString(i++,    msg.getType());
            myPStmt.setTimestamp(i++, new java.sql.Timestamp(msg.getUserTime().getTime()));
            myPStmt.setInt(i++,       msg.getUserInt());
            myPStmt.setBytes(i++,     baos.toByteArray());
            myPStmt.executeUpdate();

        } catch (SQLException e) {
            e.printStackTrace();
            throw new cMsgException("?handleSendRequest: unable to insert into queue");
        } catch (IOException e) {
            e.printStackTrace();
            System.exit(-1);
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Inserts message into SQL database table via JDBC.
     *
     * @param msg {@inheritDoc}
     * @return 0 if successful, else 1
     * @throws cMsgException never
     */
    public int handleSyncSendRequest(cMsgMessageFull msg) throws cMsgException {
        try {
            handleSendRequest(msg);
            return(0);
        } catch (cMsgException e) {
            return(1);
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Returns message at head of queue.
     *
     * @param msg message to generate sendAndGet response to (contents ignored)
     * @throws cMsgException if error reading database or delivering message to client
     */
    synchronized public void handleSendAndGetRequest(cMsgMessageFull msg) throws cMsgException {

        cMsgMessageFull response = null;


        // retrieve oldest entry and delete
        try {

            // lock table
            myStmt.execute("lock tables " + myTableName + " write");


            // get oldest row
            ResultSet rs = myStmt.executeQuery("select * from " + myTableName + " order by id limit 1");
            if(rs.next()) {
                byte[] buf = rs.getBytes("message");
                if(buf!=null) {
                    try {
                        ObjectInputStream ois= new ObjectInputStream(new ByteArrayInputStream(buf));
                        response = (cMsgMessageFull) ois.readObject();
                        response.expandPayload();
                        response.makeResponse(msg);
                        myStmt.execute("delete from " + myTableName + " where id=" + rs.getInt("id"));

                    } catch (ClassNotFoundException e) {
                        e.printStackTrace();
                        System.exit(-1);

                    } catch (IOException e) {
                        e.printStackTrace();
                        System.exit(-1);
                    }
                }
            }

            // close result and unlock table
            rs.close();
            myStmt.execute("unlock tables");


        } catch (SQLException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }


        // create null response if needed
        if(response==null) {
            response = cMsgMessageFull.createDeliverableMessage();
            response.makeNullResponse(msg);
        }


        // deliver message
        try {
            myDeliverer.deliverMessage(response, cMsgConstants.msgGetResponse);
        } catch (IOException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Close connection to database.
     *
     * @throws cMsgException if close fails.
     */
    synchronized public void handleClientShutdown() throws cMsgException {
        try {
            myStmt.close();
            myPStmt.close();
            myCon.close();
        } catch (SQLException e) {
            throw(new cMsgException("?queue sub-domain handler shutdown error"));
        }
    }


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

//  misc functions

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


    private void createTable(String type) throws cMsgException {

        System.out.println("Creating new table for queue " + myQueueName);

        String sql;
        try {
            if(type.equalsIgnoreCase("mysql")) {
                sql="create table " + myTableName + " (id int not null primary key auto_increment," +
                    "msgTime dateTime, creator varchar(255), subject varchar(255), type varchar(128), " +
                    " userTime datetime, userInt int, message blob" +
                    ")";
                myStmt.executeUpdate(sql);

            } else if(type.equalsIgnoreCase("postgresql")) {
                String seq = "cMsgQueueSeq_" + myQueueName;
                myStmt.executeUpdate("create sequence " + seq);
                sql="create table " + myTableName + " (id int not null primary key default nextval('" + seq + "')," +
                    "msgTime dateTime, creator varchar(255), subject varchar(255), type varchar(128), " +
                    " userTime datetime, userInt int, message blob"  +
                    ")";
                myStmt.executeUpdate(sql);

            } else {
                cMsgException ce = new cMsgException("?createTable: unknown database type " + type);
                ce.setReturnCode(1);
                throw ce;
            }
        } catch (SQLException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException("?createTable: unable to create table " + myTableName);
            ce.setReturnCode(1);
            throw ce;
        }
    }


//-----------------------------------------------------------------------------


    private void createPreparedStatement(String type) throws cMsgException {

        String sql = "insert into " + myTableName + " (" +
            "msgTime,creator,subject,type," +
            "userTime,userInt,message" +
            ") values (" +
            "?,?,?,?," +
            "?,?,?" +
            ")";
        try {
            myPStmt = myCon.prepareStatement(sql);
        } catch (SQLException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException("?createPreparedStatement: unable to create prepared statement for " + myTableName);
            ce.setReturnCode(1);
            throw ce;
        }
    }

}


//-----------------------------------------------------------------------------

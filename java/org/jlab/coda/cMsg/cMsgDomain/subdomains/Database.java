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


import org.jlab.coda.cMsg.common.cMsgMessageFull;
import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.common.cMsgSubdomainAdapter;
import org.jlab.coda.cMsg.common.cMsgClientInfo;

import java.sql.*;
import java.util.regex.*;


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * cMsg subdomain handler for database subdomain.
 *
 * Executes sql statement from msg payload.
 * Gets database parameters from UDL.
 *
 *  UDL:<p>
 *    cMsg:cMsg://host:port/Database?driver=myDriver&url=myURL&account=myAccount&password=myPassword<p>
 *
 * @author Elliott Wolin
 * @version 1.0
 *
 */
public class Database extends cMsgSubdomainAdapter {


    /** UDL remainder for this subdomain handler. */
    private String myUDLRemainder;


    /** database access objects. */
    Connection myCon = null;
    Statement myStmt = null;


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @return true
     */
    public boolean hasSend() {
        return true;
    };


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
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
     * UDL contains driver name, database JDBC URL, account, and password
     * Column names are fixed (domain, sender, subject, etc.).
     *
     * @param info {@inheritDoc}
     * @throws cMsgException upon error
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {

        // db params
        String driver = null;
        String URL = null;
        String account = null;
        String password = null;


        // extract db params from UDL
        int ind = myUDLRemainder.indexOf("?");
        if (ind != 0) {
            cMsgException ce = new cMsgException("illegal UDL");
            ce.setReturnCode(1);
            throw ce;
        }
        else {
            String remainder = myUDLRemainder + "&";


            //  extract params
            Pattern p;
            Matcher m;

            // driver required
            p = Pattern.compile("[&\\?]driver=(.*?)&", Pattern.CASE_INSENSITIVE);
            m = p.matcher(remainder);
            m.find();
            driver = m.group(1);

            // URL required
            p = Pattern.compile("[&\\?]url=(.*?)&", Pattern.CASE_INSENSITIVE);
            m = p.matcher(remainder);
            m.find();
            URL = m.group(1);

            // account not required
            p = Pattern.compile("[&\\?]account=(.*?)&", Pattern.CASE_INSENSITIVE);
            m = p.matcher(remainder);
            if (m.find()) {
                account = m.group(1);
            }

            // password not required
            p = Pattern.compile("[&\\?]password=(.*?)&", Pattern.CASE_INSENSITIVE);
            m = p.matcher(remainder);
            if (m.find()) {
                password = m.group(1);
            }
        }


        // load driver
        try {
            Class.forName(driver);
        }
        catch (ClassNotFoundException e) {
            System.out.println(e);
            e.printStackTrace();
            cMsgException ce = new cMsgException("registerClient: unable to load driver");
            ce.setReturnCode(1);
            throw ce;
        }


        // create connection
        try {
            myCon = DriverManager.getConnection(URL, account, password);
        } catch (SQLException e) {
            System.out.println(e);
            e.printStackTrace();
            cMsgException ce = new cMsgException("registerClient: unable to connect to database");
            ce.setReturnCode(1);
            throw ce;
        }


        // create statement
        try {
            myStmt = myCon.createStatement();
        }
        catch (SQLException e) {
            System.out.println(e);
            e.printStackTrace();
            cMsgException ce = new cMsgException("registerClient: unable to create statement");
            ce.setReturnCode(1);
            throw ce;
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Executes sql insert or update statement from message payload.
     *
     * @param msg {@inheritDoc}
     * @throws cMsgException for sql error
     */
    synchronized public void handleSendRequest(cMsgMessageFull msg) throws cMsgException {

        String sql = msg.getText();

        Pattern p1 = Pattern.compile("^\\s*insert\\s+", Pattern.CASE_INSENSITIVE);
        Pattern p2 = Pattern.compile("^\\s*update\\s+", Pattern.CASE_INSENSITIVE);
        Pattern p3 = Pattern.compile("^\\s*delete\\s+", Pattern.CASE_INSENSITIVE);

        Matcher m1 = p1.matcher(sql);
        Matcher m2 = p2.matcher(sql);
        Matcher m3 = p3.matcher(sql);

        if (m1.find() || m2.find() || m3.find()) {
            try {
                myStmt.executeUpdate(sql);
            }
            catch (SQLException e) {
                System.out.println(e);
                e.printStackTrace();
                throw new cMsgException("handleSendRequest: unable to execute: " + sql);
            }
        }
        else {
            cMsgException ce = new cMsgException("handleSendRequest: illegal sql: " + msg.getText());
            ce.setReturnCode(1);
            throw ce;
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Executes sql insert or update statement from message payload.
     *
     * @param msg {@inheritDoc}
     * @return 0
     * @throws cMsgException for sql error
     */
    public int handleSyncSendRequest(cMsgMessageFull msg) throws cMsgException {
        handleSendRequest(msg);
        return (0);
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @throws cMsgException if sql error
     */
    synchronized public void handleClientShutdown() throws cMsgException {
        try {
            myStmt.close();
            myCon.close();
        }
        catch (SQLException e) {
            throw(new cMsgException("database sub-domain handler shutdown error"));
        }
    }


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
}



// to do:

//  check for nulls
//  exceptions, synchronized?



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
import org.jlab.coda.cMsg.common.*;

import java.io.*;
import java.sql.*;
import java.util.regex.*;
import java.util.*;


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * cMsg subdomain handler for database subdomain.
 *
 * Executes insert,update,delete sql statement from msg payload.
 * Select not supported as there is no mechanism to respond.
 *
 * Gets database parameters from UDL.
 *
 * <p>UDL:</p>
 *    <p>cMsg:cMsg://host:port/Database?driver=myDriver&amp;url=myURL&amp;account=myAccount&amp;password=myPassword</p>
 *
 * @author Elliott Wolin
 * @version 1.0
 *
 */


public class Database extends cMsgSubdomainAdapter {


    /** Object used to deliver messages to the client. */
    private cMsgDeliverMessageInterface myDeliverer;


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
    public boolean hasSyncSend() {
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


        // Get an object enabling this handler to communicate
        // with only this client in this cMsg subdomain.
        myDeliverer = info.getDeliverer();


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
     * Executes sql statement in message text field.
     *
     * @param msg {@inheritDoc}
     * @return zero on success, non-zero on error
     */
    synchronized public int handleSyncSendRequest(cMsgMessageFull msg) throws cMsgException {

        String sql = msg.getText();

        try {
            myStmt.executeUpdate(sql);

        } catch (SQLException e) {
            System.out.println("?Database::handleSyncSendRequest: error executing: " + sql);
            System.out.println(e);
            return(1);
        }

        // success
        return(0);
    }


//-----------------------------------------------------------------------------


    /**
     * Executes sql statement in message text field, returns resultset as payload arrays.
     *
     * @param msg Message to respond to
     * @throws cMsgException if error reading database or delivering message to client
     */
    synchronized public void handleSendAndGetRequest(cMsgMessageFull msg) throws cMsgException {

        int i;
        cMsgMessageFull response = null;

        int maxRow = msg.getUserInt();
        String sql = msg.getText();

        try {

            // query and get metadata, fill arrays
            ResultSet rs = myStmt.executeQuery(sql);
            if(rs!=null) {

                int ncol = 0;
                int nrow = 0;
                String [] colNames   = null;
                int [] colTypes      = null;
                ArrayList[] colData  = null;

                // loop over query results
                while(rs.next()) {

                    // get column info
                    if(ncol==0) {
                        ResultSetMetaData rsmd = rs.getMetaData();
                        ncol     = rsmd.getColumnCount();
                        colNames = new String[ncol];
                        colTypes = new int[ncol];
                        colData  = new ArrayList[ncol];
                        for(i=0; i<ncol; i++) {
                            colNames[i] = rsmd.getColumnName(i+1);
                            colTypes[i] = rsmd.getColumnType(i+1);
                            colData[i]  = new ArrayList();
                        }
                    }


                    // too many rows?
                    if((maxRow>0)&&(nrow>=maxRow))break;


                    // fill array of ArrayList with query results
                    nrow++;
                    for(i=0; i<ncol; i++) addDataToArrayList(rs,i+1,colTypes[i],colData[i]);

                }
                rs.close();


                // create and fill response message, store query results in payload
                if(nrow>0) {
                    response = cMsgMessageFull.createDeliverableMessage();
                    response.setUserInt(nrow);
                    response.makeResponse(msg);
                    for(i=0; i<ncol; i++) addArrayListToPayload(colNames[i],colTypes[i],colData[i],response);
                }
            }


        } catch (SQLException e) {
            System.out.println("?Database::handleSendAndGetRequest: illegal sql: " + sql);
            System.out.println(e);
        }


        // create null response if needed
        if(response==null) {
            response = cMsgMessageFull.createDeliverableMessage();
            response.setUserInt(0);
            response.makeNullResponse(msg);
        }


        // send response
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
     * Adds query result data to ArrayList.
     *
     * @param rs ResultSet containing results
     * @param col Column number
     * @param type JDBC data type
     * @param al ArrayList to add to
     */
    public void addDataToArrayList(ResultSet rs, int col, int type, ArrayList al) {

        if(rs==null)return;

        try {

            switch (type) {

            case Types.TINYINT:
                al.add(rs.getByte(col));
                break;

            case Types.SMALLINT:
                al.add(rs.getShort(col));
                break;

            case Types.INTEGER:
                al.add(rs.getInt(col));
                break;

            case Types.BIGINT:
                al.add(rs.getLong(col));
                break;

            case Types.FLOAT:
            case Types.REAL:
                al.add(rs.getFloat(col));
                break;

            case Types.DOUBLE:
                al.add(rs.getDouble(col));
                break;

            case Types.CHAR:
            case Types.VARCHAR:
            case Types.LONGVARCHAR:
                String s = rs.getString(col);
                al.add(rs.wasNull()?"null":s);
                break;

            case Types.BINARY:
            case Types.VARBINARY:
            case Types.LONGVARBINARY:
                al.add(rs.getBytes(col));
                break;

            case Types.DATE:
                java.sql.Date d = rs.getDate(col);
                al.add(rs.wasNull()?"null":d);
                break;

            case Types.TIME:
                java.sql.Time t = rs.getTime(col);
                al.add(rs.wasNull()?"null":t);
                break;

            case Types.TIMESTAMP:
                java.sql.Timestamp ts = rs.getTimestamp(col);
                al.add(rs.wasNull()?"null":ts);
                break;

            default:
                System.out.println("?Database::addDataToArrayList...illegal type: " + type);
                return;
            }

        } catch (SQLException e) {
            System.out.println(e);
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Adds ArrayList to message payload.
     *
     * @param name Column name
     * @param type JDBC data type
     * @param al ArrayList containing results
     * @param msg Message
     */
    public void addArrayListToPayload(String name, int type, ArrayList al, cMsgMessageFull msg) {

        int nrow     = al.size();
        Object[] ala = al.toArray();

        try {

            switch (type) {

            case Types.TINYINT:
                {
                    byte[] a = new byte[nrow];
                    for(int i=0; i<nrow; i++) a[i]=(Byte)(ala[i]);
                    msg.addPayloadItem(new cMsgPayloadItem(name,a));
                }
                break;

            case Types.SMALLINT:
                {
                    short[] a = new short[nrow];
                    for(int i=0; i<nrow; i++) a[i]=(Short)(ala[i]);
                    msg.addPayloadItem(new cMsgPayloadItem(name,a));
                }
                break;

            case Types.INTEGER:
                {
                    int[] a = new int[nrow];
                    for(int i=0; i<nrow; i++) a[i]=(Integer)(ala[i]);
                    msg.addPayloadItem(new cMsgPayloadItem(name,a));
                }
                break;

            case Types.BIGINT:
                {
                    long[] a = new long[nrow];
                    for(int i=0; i<nrow; i++) a[i]=(Long)(ala[i]);
                    msg.addPayloadItem(new cMsgPayloadItem(name,a));
                }
                break;

            case Types.FLOAT:
            case Types.REAL:
                {
                    float[] a = new float[nrow];
                    for(int i=0; i<nrow; i++) a[i]=(Float)(ala[i]);
                    msg.addPayloadItem(new cMsgPayloadItem(name,a));
                }
                break;

            case Types.DOUBLE:
                {
                    double[] a = new double[nrow];
                    for(int i=0; i<nrow; i++) a[i]=(Double)(ala[i]);
                    msg.addPayloadItem(new cMsgPayloadItem(name,a));
                }
                break;

            case Types.CHAR:
            case Types.VARCHAR:
            case Types.LONGVARCHAR:
                {
                    String[] a = new String[nrow];
                    for(int i=0; i<nrow; i++) a[i]=(String)(ala[i]);
                    msg.addPayloadItem(new cMsgPayloadItem(name,a));
                }
                break;

            case Types.DATE:
            case Types.TIME:
            case Types.TIMESTAMP:
                {
                    String[] a = new String[nrow];
                    for(int i=0; i<nrow; i++) a[i]=(ala[i]).toString();
                    msg.addPayloadItem(new cMsgPayloadItem(name,a));
                }
                break;

            case Types.BINARY:
            case Types.VARBINARY:
            case Types.LONGVARBINARY:
                {
                    byte[][] a = new byte[nrow][];
                    for(int i=0; i<nrow; i++) a[i]=(byte[])(ala[i]);
                    msg.addPayloadItem(new cMsgPayloadItem(name,a));
                }
                break;

            default:
                System.out.println("?Database::addArrayListToPayload...illegal type: " + type);
                return;
            }

        } catch (cMsgException e) {
            System.out.println(e);
        }

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



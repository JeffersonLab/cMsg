/*----------------------------------------------------------------------------*
*  Copyright (c) 2004        Southeastern Universities Research Association, *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    E.Wolin, 25-Jun-2004, Jefferson Lab                                     *
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

package org.jlab.coda.cMsg.apps;

import java.lang.*;
import java.sql.*;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.cMsg.cMsgCoda;

/**
 * Class to log cMsg message to a database.
 * @version 1.0
 */
public class cMsgLogger {

    // for cMsg
    /** UDL or Universal Domain Locator of cMsg name server. */
    private String domain = "coda://aslan:3456/coda";

    /** Name of this cMsg client. */
    private String name = "cMsgLogger";

    /** Description of this cMsg client. */
    private String description = "CODA cMsg Message Logger";

    /** Subject of messages being subscribed to. */
    private String subject = "cmsglog";

    /** Callback method to be run upon receipt of a message. */
    private cb jdbcCallback = new cb();

    // for jdbc
    /** Java JDBC database driver. */
    private String driver = "com.mysql.jdbc.Driver";

    /** URL of database server. */
    private String url = "jdbc:mysql://dubhe:3306/";

    /** Name of particular database or "catalog" in JDBC terms. */
    private String database = "coda";

    /** Database account name. */
    private String account = "wolin";

    /** Database account password. */
    private String password = "";

    /** JDBC database connection object. */
    private Connection con = null;

    /** Database prepared statement. */
    private PreparedStatement s = null;

    // misc
    /** Boolean indicating whether this application is done or not. */
    private boolean done = false;

    /** Boolean indicating whether debug output is turned on or not. */
    private boolean debug = false;
    
    
//-----------------------------------------------------------------------------

    //public cMsgLogger() {

    // }
    
    
//-----------------------------------------------------------------------------

    /** Class that implements the callback interface. */
    class cb implements cMsgCallback {
        /**
         * Callback method.
         * @param msg cMsg message
         * @param userObject object given by user to callback as argument.
         */
        public void callback(cMsgMessage msg, Object userObject) {

            try {
                if (debug) {
                    s.setString(1, msg.getSender());
                    s.setString(2, msg.getSenderHost());
                    s.setString(3, msg.getSenderTime().toString());
                    s.setString(4, msg.getDomain());
                    s.setString(5, msg.getSubject().substring(subject.length() + 1));
                    s.setString(6, msg.getType());
                    s.setString(7, msg.getText());
                    s.execute();
                }
                else {
                    System.out.println("Sending:\n\n" + msg);
                }
            }
            catch (SQLException e) {
                System.err.println("?sql error in callback\n" + e);
                System.exit(-1);
            }
        }
        public boolean maySkipMessages() {return false;}

        public boolean mustSerializeMessages() {return false;}
    }


//-----------------------------------------------------------------------------

    /** Main application. */
    static public void main(String[] args) {

        cMsgLogger logger = new cMsgLogger();

        // decode command line
        decode_command_line(args, logger);

        // connect to cMsg server
        cMsgCoda cmsg = null;
        try {
            cmsg = new cMsgCoda(logger.domain, logger.name, logger.description);
        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }

        // subscribe and provide callback
        try {
            cmsg.subscribe(logger.subject + "/*", null, logger.jdbcCallback, null);
        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }

        // load jdbc driver
        try {
            Class.forName(logger.driver);
        }
        catch (Exception e) {
            System.err.println("?unable to load driver: " + logger.driver + "\n" + e);
            System.exit(-1);
        }

        // connect to database
        try {
            logger.con = DriverManager.getConnection(logger.url + logger.database,
                                                     logger.account, logger.password);
        }
        catch (Exception e) {
            System.err.println("?unable to connect to database: " + logger.url + logger.database + "\n" + e);
            System.exit(-1);
        }

        // get prepared statement object
        try {
            String sql = "insert into " + logger.subject + " (" +
                    "sender,host,time,domain,topic,type,text" +
                    ") values (" +
                    "?,?,?,?,?, ?,?" +
                    ")";
            logger.s = logger.con.prepareStatement(sql);
        }
        catch (SQLException e) {
            System.err.println("?unable to prepare statement\n" + e);
            System.exit(-1);
        }

        // enable receipt of messages and delivery to callback
        cmsg.start();

        // wait for messages
        while (!logger.done) {
            try {
                Thread.sleep(1);
            }
            catch (InterruptedException e) {
            }
        }

        // done
        try {
            logger.con.close();
        }
        catch (SQLException e) {
        }
        cmsg.disconnect();
        System.exit(0);

    }  // main


//-----------------------------------------------------------------------------

    /**
     * Method to decode the command line used to start this application.
     * @param args command line arguments
     * @param logger logging object - object of this class
     */
    static public void decode_command_line(String[] args, cMsgLogger logger) {

        String help = "\nUsage:\n\n" +
                "   java cMsgLogger [-domain domain] [-name name] [-subject subject]\n" +
                "                   [-driver driver] [-url url] [-db database]\n" +
                "                   [-account account] [-pwd password] [-debug]\n\n";


        // loop over all args
        for (int i = 0; i < args.length; i++) {

            if (args[i].equalsIgnoreCase("-h")) {
                System.out.println(help);
                System.exit(-1);

            }
            else if (args[i].equalsIgnoreCase("-domain")) {
                logger.domain = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-subject")) {
                logger.subject = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-driver")) {
                logger.driver = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-url")) {
                logger.url = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-db")) {
                logger.database = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-account")) {
                logger.account = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-pwd")) {
                logger.password = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-debug")) {
                logger.debug = true;
            }
        }

        return;
    }
  

//-----------------------------------------------------------------------------


    /**
     * Formats string for sql.
     * @param s string to be formatted
     * @return formatted string
     */
    static public String sqlfmt(String s) {

        if (s == null) return ("''");

        StringBuffer sb = new StringBuffer();

        sb.append("'");
        for (int i = 0; i < s.length(); i++) {
            sb.append(s.charAt(i));
            if (s.charAt(i) == '\'') sb.append("'");
        }
        sb.append("'");

        return (sb.toString());
    }


//-----------------------------------------------------------------------------
}        //  end class definition:  cMsgLogger

//-----------------------------------------------------------------------------

//  general purpose cMsg logger


/*----------------------------------------------------------------------------*
*  Copyright (c) 2004        Southeastern Universities Research Association, *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    E.Wolin, 6-Jan-2005, Jefferson Lab                                      *
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

import org.jlab.coda.cMsg.*;

import java.lang.*;
import java.io.*;
import java.sql.*;



//-----------------------------------------------------------------------------


/**
 * Logs cMsg messages to screen, file, and/or database.
 * subject/type specified on command line.
 *
 * @version 1.0
 */
public class cMsgLogger {


    /** Universal Domain Locator. */
    private static String UDL = "cMsg:cMsg://ollie/cMsg";


    /** Name of this client, generally must be unique. */
    private static String name = "cMsgLogger";


    /** Description of this client. */
    private static String description = "Generic cMsg Logger";


    /** Subject and type of messages being logged. */
    private static String subject = "*";
    private static String type    = "*";


    /** toScreen true to log to screen. */
    private static boolean toScreen = false;


    /** filename not null to log to file. */
    private static String filename = null;


    /** url not null to log to database. */
    private static String url              = null;
    private static String table            = "cMsgLog";
    private static String driver           = "com.mysql.jdbc.Driver";
    private static String account          = "";
    private static String password         = "";
    private static Connection con          = null;
    private static PreparedStatement pStmt = null;


    // misc
    private static boolean done  = false;
    private static boolean debug = false;


    /** Class to implement the callback interface. */
    static class cb extends cMsgCallbackAdapter {
        /**
         * Called when message arrives, logs to screen, file, and/or database.
         *
         * @param msg cMsg message
         * @param userObject object given by user to callback as argument.
         */
        public void callback(cMsgMessage msg, Object userObject) {

            if(toScreen) {
            }

            if(filename!=null) {
            }

            if(url!=null) {
                try {
                    pStmt.setString(1, msg.getSender());
                    pStmt.setString(2, msg.getSenderHost());
                    pStmt.setString(3, msg.getSenderTime().toString());
                    pStmt.setString(4, msg.getDomain());
                    pStmt.setString(5, msg.getSubject().substring(subject.length() + 1));
                    pStmt.setString(6, msg.getType());
                    pStmt.setString(7, msg.getText());
                    pStmt.execute();
                } catch (SQLException e) {
                    System.err.println("?sql error in callback\n" + e);
                    System.exit(-1);
                }
            }
        }

    }


//-----------------------------------------------------------------------------


    static public void main(String[] args) {


        // decode command line
        decode_command_line(args);


        // connect to cMsg system
        cMsg cmsg = null;
        try {
            cmsg = new cMsg(UDL, name, description);
            cmsg.connect();
        } catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }


        // subscribe and provide callback
        try {
            cmsg.subscribe(subject, type, new cb(), null);
        } catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }


        // enable screen logging if nothing else enabled
        if((filename==null)&&(url==null)) toScreen=true;


        // init file logging
        if(filename!=null) {
        }


        // init database logging
        if(url!=null) {

            // load jdbc driver
            try {
                Class.forName(driver);
            } catch (Exception e) {
                System.err.println("?unable to load driver: " + driver + "\n" + e);
                System.exit(-1);
            }

            // connect to database
            try {
                con = DriverManager.getConnection(url, account, password);
            } catch (SQLException e) {
                System.err.println("?unable to connect to database url: " + url + "\n" + e);
                System.exit(-1);
            }

            // get prepared statement object
            try {
                String sql = "insert into " + table + " (" + "???";
                pStmt = con.prepareStatement(sql);
            } catch (SQLException e) {
                System.err.println("?unable to prepare statement\n" + e);
                System.exit(-1);
            }
        }


        // enable receipt of messages and delivery to callback
        cmsg.start();


        // wait for messages (forever at the moment...)
        while (!done) {
            try {
                Thread.sleep(1);
            } catch (InterruptedException e) {
            }
        }


        // done
        try {
            con.close();
        } catch (SQLException e) {
        }

        try {
            cmsg.disconnect();
        } catch (cMsgException e) {
        }

        System.exit(0);

    }


//-----------------------------------------------------------------------------

    /**
     * Method to decode the command line used to start this application.
     * @param args command line arguments
     */
    static public void decode_command_line(String[] args) {

        String help = "\nUsage:\n\n" +
            "   java cMsgLogger [-name name] [-udl domain] [-subject subject] [-type type]\n" +
            "                   [-screen] [-file filename]\n" +
            "                   [-url url] [-table table] [-driver driver] [-account account] [-pwd password]\n" +
            "                   [-debug]\n\n";


        // loop over all args
        for (int i = 0; i < args.length; i++) {

            if (args[i].equalsIgnoreCase("-h")) {
                System.out.println(help);
                System.exit(-1);

            }
            else if (args[i].equalsIgnoreCase("-udl")) {
                UDL= args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-subject")) {
                subject = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-type")) {
                type= args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-screen")) {
                toScreen=true;

            }
            else if (args[i].equalsIgnoreCase("-filename")) {
                filename= args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-url")) {
                url = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-table")) {
               table = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-driver")) {
                driver = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-account")) {
                account = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-pwd")) {
                password = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-debug")) {
                debug = true;
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

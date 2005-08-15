//  general purpose cMsg alarm server

//      -url jdbc:mysql://xdaq/test -driver com.mysql.jdbc.Driver -account fred



/*----------------------------------------------------------------------------*
*  Copyright (c) 2004        Southeastern Universities Research Association, *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    E.Wolin, 9-Aug-2005, Jefferson Lab                                      *
*                                                                            *
*    Authors: Elliott Wolin                                                  *
*             wolin@jlab.org                    Jefferson Lab, MS-6B         *
*             Phone: (757) 269-7365             12000 Jefferson Ave.         *
*             Fax:   (757) 269-5800             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/


//package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;

import java.lang.*;
import java.io.*;
import java.sql.*;
import java.util.Date;
import java.net.*;


//-----------------------------------------------------------------------------


/**
 * Logs special cMsg alarm messages to screen, file, and/or database.
 * Logging to database can be to history and/or update tables.
 *
 * @version 1.0
 */
public class cMsgAlarmServer {


    /** Universal Domain Locator and cMsg system object. */
    private static String udl = "cMsg:cMsg://ollie/cMsg";
    private static cMsg cmsg  = null;


    /** name,description of client, generally must be unique within domain. */
    private static String name = null;
    private static String description = "cMsg Alarm Server";


    /** alarm subject. */
    private static String alarmSubject = "cMsgAlarm";


    /** toScreen true to log to screen. */
    private static boolean toScreen = false;
    private static String format = "%-8d   %-24s   %25s   %2d   %s";


    /** filename not null to log to file. */
    private static boolean noAppend    = false;
    private static String filename     = null;
    private static PrintWriter pWriter = null;


    /** url not null to log to database. */
    private static String url                = null;
    private static String driver             = "com.mysql.jdbc.Driver";
    private static String account            = "";
    private static String password           = "";
    private static Connection con            = null;
    private static ResultSet rs              = null;
    private static PreparedStatement ps      = null;

    private static String historyTable            = null;
    private static PreparedStatement historyPStmt = null;

    private static String updateTable             = null;
    private static PreparedStatement updatePStmt1 = null;
    private static PreparedStatement updatePStmt2 = null;
    private static PreparedStatement updatePStmt3 = null;


    // misc
    private static int count           = 0;
    private static boolean forceUpdate = false;
    private static boolean done        = false;
    private static boolean debug       = false;


    /** Class to implement the callback. */
    static class cb extends cMsgCallbackAdapter {
        /**
         * Called when message arrives, logs to screen, file, and/or database.
         *
         * @param msg cMsg message
         * @param userObject object given by user to callback as argument.
         */
        public void callback(cMsgMessage msg, Object userObject) {

            String sql;
            int i;
            count++;

            // output to screen
            if(toScreen) {
                System.out.println(String.format(format,count,
                                                 msg.getType(),
                                                 new java.sql.Timestamp(msg.getSenderTime().getTime()),
                                                 msg.getUserInt(),
                                                 msg.getText()));
            }


            // output to file
            if(filename!=null) {
                pWriter.println(String.format(format,count,
                                              msg.getType(),
                                              new java.sql.Timestamp(msg.getSenderTime().getTime()),
                                              msg.getUserInt(),
                                              msg.getText()));
                pWriter.flush();
            }


            // output to database
            if(url!=null) {
                try {

                    // update...check if channel exists
                    if(updateTable!=null) {
                        updatePStmt1.setString(1,msg.getType());
                        rs = updatePStmt1.executeQuery();
                        if((rs==null) || (!rs.next()) || (rs.getInt(1)<=0)) {
                            i=1;
                            updatePStmt2.setTimestamp(i++, new java.sql.Timestamp(msg.getSenderTime().getTime()));
                            updatePStmt2.setInt(i++,       msg.getUserInt());
                            updatePStmt2.setString(i++,    msg.getText());
                            updatePStmt2.setString(i++,    msg.getType());
                            updatePStmt2.execute();
                        } else {
                            i=1;
                            updatePStmt3.setTimestamp(i++, new java.sql.Timestamp(msg.getSenderTime().getTime()));
                            updatePStmt3.setInt(i++,       msg.getUserInt());
                            updatePStmt3.setString(i++,    msg.getText());
                            updatePStmt3.setString(i++,    msg.getType());
                            updatePStmt3.execute();
                        }
                    }

                    // history
                    if(historyTable!=null) {
                        i=1;
                        historyPStmt.setTimestamp(i++, new java.sql.Timestamp(msg.getSenderTime().getTime()));
                        historyPStmt.setInt(i++,       msg.getUserInt());
                        historyPStmt.setString(i++,    msg.getText());
                        historyPStmt.setString(i++,    msg.getType());
                        historyPStmt.execute();
                    }

                } catch (SQLException e) {
                    System.err.println("?sql error in callback\n" + e);
                    e.printStackTrace();
                    System.exit(-1);
                }
            }
        }

    }


//-----------------------------------------------------------------------------


    static public void main(String[] args) {


        // decode command line
        decode_command_line(args);


        // generate name if not set
        if(name==null) {
            String host="";
            try {
                host=InetAddress.getLocalHost().getHostName();
            } catch (UnknownHostException e) {
                System.err.println("?unknown host exception");
            }
            name = "cMsgAlarmServer-" + host + "-" + (new Date()).getTime();
        }


        // connect to cMsg system
        try {
            cmsg = new cMsg(udl, name, description);
            cmsg.connect();
        } catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }


        // subscribe and provide callback
        try {
            cmsg.subscribe(alarmSubject, "*", new cb(), null);
        } catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }


        // init file logging in append mode
        if(filename!=null) {
            try {
                pWriter = new PrintWriter(new BufferedWriter(new FileWriter(filename,!noAppend)));
            } catch (IOException e) {
                System.err.println("?unable to open file " + filename);
                filename=null;
            }
        }


        // init database logging
        if(url!=null) {
            init_database();
        }


        // enable receipt of messages and delivery to callback
        cmsg.start();


        // wait for messages
        try {
            while (!done&&(cmsg.isConnected())) {
                Thread.sleep(1);
            }
        } catch (Exception e) {
            System.err.println(e);
        }


        // done
        cmsg.stop();
        try {
            if(filename!=null) {
                pWriter.flush();
                pWriter.close();
            }
            if(url!=null)con.close();
            cmsg.disconnect();
        } catch (Exception e) {
            System.exit(-1);
        }
        System.exit(0);

    }


//-----------------------------------------------------------------------------


    static public void init_database() {

        String sql;


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


        // check if tables exist, create if needed
        // if update table exists make sure it doesn't look like a history table
        try {

            DatabaseMetaData dbmeta = con.getMetaData();
            ResultSet dbrs;

            if(updateTable!=null) {
                dbrs = dbmeta.getTables(null,null,updateTable,new String [] {"TABLE"});
                if((!dbrs.next())||(!dbrs.getString(3).equalsIgnoreCase(updateTable))) {
                    sql="create table " + updateTable +
                        "(channel varchar(128), time datetime, severity int, text varchar(256))";
                    con.createStatement().executeUpdate(sql);
                } else {
                    sql="select channel,count(channel) from " + updateTable + " group by channel";
                    dbrs = con.createStatement().executeQuery(sql);
                    while(dbrs.next()) {
                        if(dbrs.getInt(2)>1) {
                            System.out.print("\n\n   *** Existing update table " + updateTable +
                                             " contains multiple entries for some channels ***\n" +
                                             "       This table looks like a history table!\n\n");
                            if(!forceUpdate) {
                                System.out.print("       Specify -forceUpdate on command line to ignore this\n\n\n" );
                                System.exit(-1);
                            } else {
                                System.out.print("       -forceUpdate specified...continuing...\n\n" );
                                break;
                            }
                        }
                    }
                }
            }


            if(historyTable!=null) {
                dbrs = dbmeta.getTables(null,null,historyTable,new String [] {"TABLE"});
                if((!dbrs.next())||(!dbrs.getString(3).equalsIgnoreCase(historyTable))) {
                    sql="create table " + historyTable +
                        "(channel varchar(128), time datetime, severity int, text varchar(256))";
                    con.createStatement().executeUpdate(sql);
                }
            }

        } catch (SQLException e) {
            e.printStackTrace();
            System.exit(-1);
        }



        // get various statement objects, etc.
        try {
            // history
            sql = "insert into " + historyTable + " (" +
                "time,severity,text,channel" +
                ") values (" +
                "?,?,?,?" + ")";
            historyPStmt = con.prepareStatement(sql);

            // update mode
            sql = "select count(channel) from " + updateTable + " where channel=?";
            updatePStmt1 = con.prepareStatement(sql);
            sql = "insert into " + updateTable + " (" +
                "time,severity,text,channel" +
                ") values (" +
                "?,?,?,?" + ")";
            updatePStmt2 = con.prepareStatement(sql);
            sql = "update " + updateTable + " set time=?, severity=?, text=? where channel=?";
            updatePStmt3 = con.prepareStatement(sql);



        } catch (SQLException e) {
            System.err.println("?unable to prepare statements\n" + e);
            System.exit(-1);
        }

    }


//-----------------------------------------------------------------------------


    /**
     * decodes command line parameters
     * @param args command line arguments
     */
    static public void decode_command_line(String[] args) {

        String help = "\nUsage:\n\n" +
            "   java cMsgAlarmServer [-name name] [-udl udl] [-descr description]\n" +
            "                   [-subject alarmSubject] [-file filename] [-noAppend]\n" +
            "                   [-screen] [-file filename] \n" +
            "                   [-update updateTable] [-history historyTable]\n" +
            "                   [-url url] [-table table] [-driver driver] [-account account] [-pwd password]\n" +
            "                   [-forceUpdate] [-debug]\n\n";


        // loop over all args
        for (int i = 0; i < args.length; i++) {

            if (args[i].equalsIgnoreCase("-h")) {
                System.out.println(help);
                System.exit(-1);
            }
            else if (args[i].equalsIgnoreCase("-name")) {
                name = args[i + 1];
                i++;

            } else if (args[i].equalsIgnoreCase("-descr")) {
                description = args[i + 1];
                i++;

            } else if (args[i].equalsIgnoreCase("-udl")) {
                udl = args[i + 1];
                i++;

            } else if (args[i].equalsIgnoreCase("-screen")) {
                toScreen = true;

            } else if (args[i].equalsIgnoreCase("-file")) {
                filename = args[i + 1];
                i++;

            } else if (args[i].equalsIgnoreCase("-noappend")) {
                noAppend = true;

            } else if (args[i].equalsIgnoreCase("-update")) {
                updateTable = args[i + 1];
                i++;

            } else if (args[i].equalsIgnoreCase("-history")) {
                historyTable = args[i + 1];
                i++;

            } else if (args[i].equalsIgnoreCase("-url")) {
                url = args[i + 1];
                i++;

            } else if (args[i].equalsIgnoreCase("-driver")) {
                driver = args[i + 1];
                i++;

            } else if (args[i].equalsIgnoreCase("-account")) {
                account = args[i + 1];
                i++;

            } else if (args[i].equalsIgnoreCase("-pwd")) {
                password = args[i + 1];
                i++;

            } else if (args[i].equalsIgnoreCase("-debug")) {
                debug = true;

            } else if (args[i].equalsIgnoreCase("-forceUpdate")) {
                forceUpdate= true;
            }
        }

        return;
    }


//-----------------------------------------------------------------------------
//  end class definition:  cMsgAlarmServer
//-----------------------------------------------------------------------------
}

//  general purpose cMsg queue

//  lots to do...
//   distinguish between subscribe for queue and for sendAndGet()



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
import java.util.Date;
import java.net.*;


//-----------------------------------------------------------------------------


/**
 * Queues messages to file or database.
 *
 * @version 1.0
 */
public class cMsgQueue {


    /** Universal Domain Locator and cMsg system object. */
    private static String UDL = "cMsg:cMsg://ollie/cMsg";
    private static cMsg cmsg  = null;


    /** Name of this client, generally must be unique within domain. */
    private static String name = null;


    /** Host client is running on. */
    private static String host = null;


    /** Description of this client. */
    private static String description = null;


    /** Subject and type of messages being queued. */
    private static String queueName;
    private static String subject = "*";
    private static String type    = "*";


    /** Subject and type for sendAndGet messages. */
    private static String getSubject = null;
    private static String getType    = "*";


    /** filename not null use file queue. */
    private static String filename     = null;


    /** url not null to use database queue. */
    private static String url              = null;
    private static String table            = "cMsgQueue";
    private static String driver           = "com.mysql.jdbc.Driver";
    private static String account          = "";
    private static String password         = "";
    private static Connection con          = null;
    private static PreparedStatement pStmt = null;


    // misc
    private static int recvCount    = 0;
    private static int getCount     = 0;
    private static boolean done  = false;
    private static boolean debug = false;


    /** Class to implement subscribe callback. */
    static class subscribeCB extends cMsgCallbackAdapter {
        /**
         *  Queues to file or database.
         */
        public void callback(cMsgMessage msg, Object userObject) {

            recvCount++;

            // queue to file
            if(filename!=null) {
            }


            // queue to database
            if(url!=null) {
                try {
                    int i = 1;
                    pStmt.setInt(i++,       msg.getVersion());
                    pStmt.setString(i++,    msg.getDomain());
                    pStmt.setInt(i++,       msg.getSysMsgId());

                    pStmt.setInt(i++,       (msg.isGetRequest()?1:0));
                    pStmt.setInt(i++,       (msg.isGetResponse()?1:0));

                    pStmt.setString(i++,    msg.getSender());
                    pStmt.setString(i++,    msg.getSenderHost());
                    pStmt.setTimestamp(i++, new java.sql.Timestamp(msg.getSenderTime().getTime()));
                    pStmt.setInt(i++,       msg.getSenderToken());

                    pStmt.setInt(i++,       msg.getUserInt());
                    pStmt.setTimestamp(i++, new java.sql.Timestamp(msg.getUserTime().getTime()));
                    pStmt.setInt(i++,       msg.getPriority());

                    pStmt.setString(i++,    msg.getReceiver());
                    pStmt.setString(i++,    msg.getReceiverHost());
                    pStmt.setTimestamp(i++, new java.sql.Timestamp(msg.getReceiverTime().getTime()));
                    pStmt.setInt(i++,       msg.getReceiverSubscribeId());

                    pStmt.setString(i++,    msg.getSubject());
                    pStmt.setString(i++,    msg.getType());
                    pStmt.setString(i++,    msg.getText());

                    pStmt.execute();
                } catch (SQLException e) {
                    System.err.println("?\n" + e);
                    System.exit(-1);
                }
            }
        }
    }


    /** Class to implement sendAndGet() callback. */
    static class getCB extends cMsgCallbackAdapter {
        /**
         *  retrieve from file or database queue
         */
        public void callback(cMsgMessage msg, Object userObject) {

            // is this a get request?
            if(!msg.isGetRequest()) return;


            getCount++;


            // get response message
            cMsgMessage response = null;
            try {
                response = msg.response();
            } catch (cMsgException e) {
            }


            // retrieve from file
            if(filename!=null) {
            }


            // retrieve from database
            if(url!=null) {
                try {
                    pStmt.execute("");
                } catch (SQLException e) {
                    System.err.println("?sql error in callback\n" + e);
                    System.exit(-1);
                }
            }


            // send off response
            try {
                cmsg.send(response);
                cmsg.flush();
            } catch (cMsgException e) {
            }

        }
    }


//-----------------------------------------------------------------------------


    static public void main(String[] args) {


        // decode command line
        decode_command_line(args);


        // check command args
        if((filename==null)&&(url==null)) {
            System.err.println("?cMsgQueue...must specify file OR database");
            System.exit(-1);
        }
        if((filename!=null)&&(url!=null)) {
            System.err.println("?cMsgQueue...can only queue to file OR database");
            System.exit(-1);
        }
        if(queueName==null) {
            System.err.println("?cMsgQueue...must specify queue name");
            System.exit(-1);
        }


        // get host
        try {
            host=InetAddress.getLocalHost().getHostName();
        } catch (UnknownHostException e) {
            System.err.println("?unknown host exception");
        }


        // generate name if not set
        if(name==null) {
            name = "cMsgQueue:" + queueName;
        }


        // generate description if not set
        if(description==null) {
            description = "Generic cMsg queue on " + host + " for queue " + queueName;
        }


        // set getSubject to name if not set
        if(getSubject==null) {
            getSubject = name;
        }


        // connect to cMsg system
        try {
            cmsg = new cMsg(UDL, name, description);
            cmsg.connect();
        } catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }


        // subscribe and provide callback
        try {
            cmsg.subscribe(subject,    type,    new subscribeCB(), null);
            cmsg.subscribe(getSubject, getType, new getCB(), null);
        } catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }


        // init queue files
        if(filename!=null) {
        }


        // init database
        if(url!=null) {

            // form table name if not set
            if(table==null) {
                table="cMsgQueue_" + queueName;
            }

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

            // check if table exists, create if needed
            try {
                DatabaseMetaData dbmeta = con.getMetaData();
                ResultSet dbrs = dbmeta.getTables(null,null,table,new String [] {"TABLE"});
                if((!dbrs.next())||(!dbrs.getString(3).equalsIgnoreCase(table))) {
                    String sql="create table " + table + " (" +
                        "version int, domain varchar(255), sysMsgId int," +
                        "getRequest int, getResponse int," +
                        "sender varchar(128), senderHost varchar(128),senderTime datetime, senderToken int," +
                        "userInt int, userTime datetime, priority int," +
                        "receiver varchar(128), receiverHost varchar(128), receiverTime datetime, receiverSubscribeId int," +
                        "subject  varchar(255), type varchar(128), text text)";
                    con.createStatement().executeUpdate(sql);
                }
            } catch (SQLException e) {
                e.printStackTrace();
                System.exit(-1);
            }

            // get prepared statement object
            try {
                String sql = "insert into " + table + " (" +
                    "version,domain,sysMsgId," +
                    "getRequest,getResponse," +
                    "sender,senderHost,senderTime,senderToken," +
                    "userInt,userTime,priority," +
                    "receiver,receiverHost,receiverTime,receiverSubscribeId," +
                    "subject,type,text" +
                    ") values (" +
                    "?,?,?," + "?,?," + "?,?,?,?," + "?,?,?," + "?,?,?,?," + "?,?,?" + ")";
                pStmt = con.prepareStatement(sql);
            } catch (SQLException e) {
                System.err.println("?unable to prepare statement\n" + e);
                System.exit(-1);
            }
        }


        // enable receipt of messages and delivery to callback
        cmsg.start();


        // wait for messages (forever at the moment...)
        try {
            while (!done) {
                Thread.sleep(1);
            }
        } catch (Exception e) {
            System.err.println(e);
        }


        // done
        try {
            if(url!=null)con.close();
            cmsg.disconnect();
        } catch (Exception e) {
            System.exit(-1);
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
            "   java cMsgQueue [-name name] [-descr description] [-udl domain] [-subject subject] [-type type]\n" +
            "                  [-queue queueName] [-getSubject getSubject] [-getType getType]" +
            "                  [-file filename]\n" +
            "                  [-url url] [-table table] [-driver driver] [-account account] [-pwd password]\n";


        // loop over all args
        for (int i = 0; i < args.length; i++) {

            if (args[i].equalsIgnoreCase("-h")) {
                System.out.println(help);
                System.exit(-1);

            }
            else if (args[i].equalsIgnoreCase("-name")) {
                name = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-descr")) {
                description = args[i + 1];
                i++;

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
            else if (args[i].equalsIgnoreCase("-queue")) {
                queueName = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-getSubject")) {
                getSubject = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-getType")) {
                getType= args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-file")) {
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
//  end class definition:  cMsgQueue
//-----------------------------------------------------------------------------
}

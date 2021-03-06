// still to do:

//   sender and history?
//   broadcast?


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
*             Fax:   (757) 269-6248             Newport News, VA 23606       *
*                                                                            *
*             Carl Timmer                                                    *
*             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
*             Phone: (757) 269-5130             12000 Jefferson Ave.         *
*             Fax:   (757) 269-6248             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/


package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.*;


import java.lang.*;
import java.io.*;
import java.sql.*;
import java.net.*;
import java.nio.channels.*;


//-----------------------------------------------------------------------------


/**
 * This class is a general purpose cMsg queue utility that queues messages to
 *  a file OR a database.  It stores the entire message in serialized binary,
 *  after payload compression.  Payload is uncompressed when retrieving messages.
 *
 * For databases it additionally stores receipt time, creator, subject, type, userTime and userInt,
 *  allowing for selection criteria at some future date.
 *
 * To use with a database queue:
 *   java cMsgQueue -udl cMsg:cMsg://localhost/cMsg  -name myQueue
 *                  -url jdbc:mysql://halldweb1/test  -driver com.mysql.jdbc.Driver  -account fred
 *
 * To use with a file-based queue:
 *   java cMsgQueue -udl cMsg:cMsg://localhost/cMsg  -name myQueue
 *                  -dir myDir  -base myFileBaseName
 *
 *
 * @version 1.0
 */
public class cMsgQueue {


    /** Universal Domain Locator and cMsg system object. */
    private static String UDL = "cMsg://localhost/cMsg";
    private static cMsg cmsg  = null;


    /** Name of this client, generally must be unique within domain. */
    private static String name = null;


    /** Host client is running on. */
    private static String host = null;


    /** Description of this client. */
    private static String description = null;


    /** queue name, and subject and type of messages being queued. */
    private static String queueName    = "default";
    private static String subject      = "*";
    private static String type         = "*";


    /** Subject and type to use for receiving sendAndGet() messages. */
    private static String getSubject = null;
    private static String getType    = "*";


    /** dir not null to use file queue. */
    private static String dir             = null;
    private static String base            = null;
    private static String fileBase        = null;
    private static String hiSeqFile       = null;
    private static String loSeqFile       = null;



    /** url not null to use database queue. */
    private static String url                  = null;
    private static String table                = null;
    private static String driver               = "com.mysql.jdbc.Driver";
    private static String account              = "";
    private static String password             = "";
    private static Connection con              = null;
    private static Statement stmt              = null;
    private static PreparedStatement pStmt     = null;


    // misc
    private static int recvCount     = 0;
    private static int getCount      = 0;
    private static boolean done      = false;
    private static boolean broadcast = false;
    private static boolean debug     = false;



    /** Inner class to implement subscribe callback. */
    static class subscribeCB extends cMsgCallbackAdapter {
        /**
         *  Queues message to file or database.
         */
        public void callback(cMsgMessage m, Object userObject) {

            cMsgMessageFull msg = (cMsgMessageFull)m;


            // do not queue sendAndGet() traffic
            if(msg.isGetRequest()) return;
            if(msg.isGetResponse()) return;


            // do not queue if this program is the sender
            if(msg.getSender().equals(name)) return;


            recvCount++;


            // get creator before payload compression
            String creator = null;
            try {
                cMsgPayloadItem creatorItem = msg.getPayloadItem("cMsgCreator");
                if(creatorItem!=null)creator=creatorItem.getString();
            } catch (cMsgException e) {
                System.err.println("?cMsgQueue...message has no creator!");
            }


            // compress payload
            msg.compressPayload();


            // queue to file
            if(dir!=null) {

                try {
                    // lock hi file
                    RandomAccessFile r = new RandomAccessFile(hiSeqFile,"rw");
                    FileChannel c = r.getChannel();
                    FileLock l = c.lock();

                    // read hi file, increment, write out new hi value
                    long hi = Long.parseLong(r.readLine());
                    hi++;
                    r.seek(0);
                    r.writeBytes(hi+"\n");

                    // serialize compressed message, then write to queue file
                    FileOutputStream fos = null;
                    ObjectOutputStream oos = null;
                    fos = new FileOutputStream(String.format("%s%08d",fileBase,hi));
                    oos = new ObjectOutputStream(fos);
                    oos.writeObject(msg);
                    oos.close();

                    // unlock and close hi file
                    l.release();
                    r.close();

                } catch (IOException e) {
                    e.printStackTrace();
                    System.exit(-1);
                }
            }  // queue to file


            // queue to database
            if(url!=null) {

                // serialize compressed message, then send to database
                ByteArrayOutputStream baos;
                ObjectOutputStream oos;
                try {
                    baos = new ByteArrayOutputStream();
                    oos = new ObjectOutputStream(baos);
                    oos.writeObject(msg);
                    oos.close();

                    int i = 1;
                    pStmt.setTimestamp(i++, new java.sql.Timestamp(msg.getReceiverTime().getTime()));
                    pStmt.setString(i++,    creator);
                    pStmt.setString(i++,    msg.getSubject());
                    pStmt.setString(i++,    msg.getType());
                    pStmt.setTimestamp(i++, new java.sql.Timestamp(msg.getUserTime().getTime()));
                    pStmt.setInt(i++,       msg.getUserInt());
                    pStmt.setBytes(i++,     baos.toByteArray());
                    pStmt.executeUpdate();

                } catch (SQLException e) {
                    e.printStackTrace();
                    System.exit(-1);
                } catch (IOException e) {
                    e.printStackTrace();
                    System.exit(-1);
                }
            }  // queue to database

        }
    }


//-----------------------------------------------------------------------------


    /** Inner class to implement sendAndGet() callback. */
    static class getCB extends cMsgCallbackAdapter {
        /**
         *  Retrieves oldest entry in file or database queue and returns as getResponse.
         *  If broadcast==true then also broadcasts the message.
         */
        public void callback(cMsgMessage m, Object userObject) {

            cMsgMessageFull msg = (cMsgMessageFull)m;

            // only handle sendAndGet() requests
            if(!msg.isGetRequest()) return;


            getCount++;
            cMsgMessageFull response = null;


            // retrieve from files
            if(dir!=null) {
                try {

                    // lock hi file
                    RandomAccessFile rHi = new RandomAccessFile(hiSeqFile,"rw");
                    FileChannel cHi = rHi.getChannel();
                    FileLock lHi = cHi.lock();

                    // lock lo file
                    RandomAccessFile rLo = new RandomAccessFile(loSeqFile,"rw");
                    FileChannel cLo = rLo.getChannel();
                    FileLock lLo = cLo.lock();

                    // read files
                    long hi = Long.parseLong(rHi.readLine());
                    long lo = Long.parseLong(rLo.readLine());


                    // message file exists only if hi>lo
                    if(hi>lo) {
                        lo++;
                        rLo.seek(0);
                        rLo.writeBytes(lo+"\n");

                        // create response from file if it exists
                        FileInputStream fis = null;
                        ObjectInputStream oin = null;
                        try {
                            fis = new FileInputStream(String.format("%s%08d",fileBase,lo));
                            oin = new ObjectInputStream(fis);
                            response = (cMsgMessageFull) oin.readObject();
                            oin.close();
                            fis.close();
                            new File(String.format("%s%08d",fileBase,lo)).delete();
                            response.expandPayload();
                            response.setNoHistoryAdditions(true);   // ??? sender too
                            response.makeResponse(msg);


                        } catch (FileNotFoundException e) {
                            System.err.println("?missing message file " + lo + " in queue " + queueName);

                        } catch (ClassNotFoundException e) {
                            e.printStackTrace();
                            System.exit(-1);

                        } catch (IOException e) {
                            e.printStackTrace();
                            System.exit(-1);
                        }
                    }


                    // unlock and close files
                    lLo.release();
                    lHi.release();
                    rHi.close();
                    rLo.close();

                } catch (IOException e) {
                    e.printStackTrace();
                    System.exit(-1);
                }

            }  // retrieve from file




            // retrieve from database
            if(url!=null) {
                try {

                    // lock table
                    stmt.execute("lock tables " + table + " write");

                    // get oldest row, then delete
                    ResultSet rs = stmt.executeQuery("select * from " + table + " order by id limit 1");
                    if(rs.next()) {
                        byte[] buf = rs.getBytes("message");
                        if(buf!=null) {
                            try {
                                ObjectInputStream ois= new ObjectInputStream(new ByteArrayInputStream(buf));
                                response = (cMsgMessageFull) ois.readObject();
                                response.expandPayload();
                                response.setNoHistoryAdditions(true);
                                response.makeResponse(msg);
                                stmt.execute("delete from " + table + " where id=" + rs.getInt("id"));

                            } catch (ClassNotFoundException e) {
                                e.printStackTrace();
                                System.exit(-1);

                            } catch (IOException e) {
                                e.printStackTrace();
                                System.exit(-1);
                            }
                        }
                    }

                    // close result set and unlock table
                    rs.close();
                    stmt.execute("unlock tables");

                } catch (SQLException e) {
                    e.printStackTrace();
                    System.exit(-1);
                }

            }   // retrieve from database



            // create null response if needed
            if(response==null) {
                response = cMsgMessageFull.createDeliverableMessage();
                response.makeNullResponse(msg);
            }


            // send response and broadcast if requested
            try {
                cmsg.send(response);
                if(broadcast) {
                    cmsg.send(response);  // send second copy, what about isGetResponse???
                }
                cmsg.flush(0);
            } catch (cMsgException e) {
                e.printStackTrace();
                System.exit(-1);
            }

        }
    }


//-----------------------------------------------------------------------------


    public static void main(String[] args) {


        // decode command line
        decode_command_line(args);


        // check command args
        if((dir==null)&&(url==null)) {
            System.err.println("?cMsgQueue...must specify either dir OR url");
            System.exit(-1);
        }
        if((dir!=null)&&(url!=null)) {
            System.err.println("?cMsgQueue...cannot specify both dir AND url");
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
            name = "cMsgQueue/" + queueName;
        }


        // generate description if not set
        if(description==null) {
            if(url!=null) {
                description = "cMsgQueue (database): " + queueName;
            } else {
                description = "cMsgQueue (file): " + queueName;
            }
        }


        // generate getSubject if not set
        if(getSubject==null) {
            getSubject = name;
        }


        // init queue files
        if(dir!=null) {
            if(base==null)base = "cMsgQueue_" + queueName + "_";
            fileBase  = dir + "/" + base;
            hiSeqFile = fileBase + "Hi";
            loSeqFile = fileBase + "Lo";

            File hi = new File(hiSeqFile);
            File lo = new File(loSeqFile);
            if(!hi.exists()||!lo.exists()) {
                try {
                    FileWriter h = new FileWriter(hi);  h.write("0\n");  h.close();
                    FileWriter l = new FileWriter(lo);  l.write("0\n");  l.close();
                } catch (IOException e) {
                    e.printStackTrace();
                    System.exit(-1);
                }
            }
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

            // check if table exists, create if needed (MySQL only, postgres is different)
            try {
                DatabaseMetaData dbmeta = con.getMetaData();
                ResultSet dbrs = dbmeta.getTables(null,null,table,new String [] {"TABLE"});
                if((!dbrs.next())||(!dbrs.getString(3).equalsIgnoreCase(table))) {
                    String sql = "create table " + table +
                        "(id int not null primary key auto_increment, " +
                        "msgTime datetime, creator varchar(255), subject varchar(255), type varchar(128), " +
                        " userTime datetime, userInt int, message " + getBlobName(con) +
                        ")";
                    con.createStatement().executeUpdate(sql);
                }
            } catch (SQLException e) {
                e.printStackTrace();
                System.exit(-1);
            }

            // get various statement objects
            try {
                stmt = con.createStatement();

                String sql = "insert into " + table + " (" +
                    "msgTime,creator,subject,type," +
                    "userTime,userInt,message" +
                    ") values (" +
                    "?,?,?,?," +
                    "?,?,?" +
                    ")";
                pStmt = con.prepareStatement(sql);
            } catch (SQLException e) {
                System.err.println("?unable to prepare statement\n" + e);
                System.exit(-1);
            }
        }


        // connect to cMsg system
        try {
            cmsg = new cMsg(UDL, name, description);
            cmsg.connect();
        } catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }


        // subscribe and provide callbacks
        try {
            cmsg.subscribe(subject,    type,    new subscribeCB(), null);
            cmsg.subscribe(getSubject, getType, new getCB(),       null);
        } catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }


        // enable receipt of messages and delivery to callbacks
        cmsg.start();


        // wait for messages (forever at the moment...)
        try {
            while (!done&&cmsg.isConnected()) {
                Thread.sleep(1);
            }
        } catch (Exception e) {
            System.err.println(e);
        }


        // disable message delivery
        cmsg.stop();


        // done
        try {
            if(url!=null)con.close();
            cmsg.disconnect();
        } catch (Exception e) {
            e.printStackTrace();
            System.exit(-1);
        }
        System.exit(0);

    }


//-----------------------------------------------------------------------------


    /** Method to print out correct program command line usage. */
    static private void usage() {
        System.out.println("\nUsage:\n\n" +
                "   java cMsgQueue\n" +
                "        [-name <name>]             name of this cmsg client\n" +
                "        [-udl <udl>]               UDL for cmsg connection\n" +
                "        [-descr <description>]     string describing this cmsg client\n" +
                "        [-subject <subject>]       subject of messages being queued\n" +
                "        [-type <type>]             type of messages being queued\n" +
                "        [-getSubject <subject>]    subject of sendAndGet msgs for retrieving a msg from queue\n" +
                "        [-getType <type>]          type of sendAndGet msgs for retrieving a msg from queue\n" +
                "        [-queue <name>]            used to generate name, description, getSubject, base, and table\n" +
                "                                   if any not given, (default = \"default\"\n" +
                "        [-dir <dir>]               directory of queue files\n" +
                "        [-base <name>]             base of queue file names\n" +
                "        [-broadcast]               when oldest queue msg is sent in response to sendAndGet,\n" +
                "                                   same message sent to all subscribed to getSubject, getType\n" +
                "        [-table <table>]           db table storing messages\n" +
                "        [-url <url>]               database url (for connection to db)\n" +
                "        [-driver <driver>]         database driver (for connection to db)\n" +
                "        [-account <account>]       database account (for connection to db)\n" +
                "        [-pwd <password>]          database password (for connection to db)\n" +
                "        [-debug]                   enable debug output\n" +
                "        [-h]                       print this help\n");
    }


//-----------------------------------------------------------------------------


    /** Gets blob name for this particular database.
     * @param  conn connection
     * @return String containing blob name
     */
    static String getBlobName(Connection conn) {

        String type;

        try {
            DatabaseMetaData dbmeta = conn.getMetaData();
            type = dbmeta.getDatabaseProductName();
            if(type.equalsIgnoreCase("mysql")) {
                return("blob");
            } else if(type.equalsIgnoreCase("oracle")) {
                return("blob");
            } else if(type.equalsIgnoreCase("postgresql")) {
                return("bytea");
            } else {
                System.out.println("?getBlobName...unknown database type " + type + ", trying blob");
                return("blob");
            }
        } catch (Exception e) {
            System.out.println("?getBlobName...unable to get database product name, trying blob");
            return("blob");
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to decode the command line used to start this application.
     * @param args command line arguments
     */
    static private void decode_command_line(String[] args) {


        // loop over all args
        for (int i = 0; i < args.length; i++) {

            if (args[i].equalsIgnoreCase("-h")) {
                usage();
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
            else if (args[i].equalsIgnoreCase("-broadcast")) {
                broadcast = true;

            }
            else if (args[i].equalsIgnoreCase("-dir")) {
                dir = args[i + 1];
                i++;

            }
            else if (args[i].equalsIgnoreCase("-base")) {
                base = args[i + 1];
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
            else {
                usage();
                System.exit(-1);
            }
        }

        return;
    }

}


//-----------------------------------------------------------------------------

//  cMsgLogger.java

//  receives cMsg messages and logs them to a database

//  subscribes to subject/* where subject is also the logging table name
//  messages are published to subject/topic


//  still to do:


//  E.Wolin, 25-jun-2004

package org.jlab.coda.cMsg.coda;

import java.io.*;
import java.lang.*;
import java.sql.*;
import org.jlab.coda.cMsg.*;


public class cMsgLogger {

    // for cMsg
    private String domain        = "coda://dubhe/coda";
    private String name          = "cMsgLogger";
    private String description   = "CODA cMsg Message Logger";
    private String subject       = "cmsglog";
    private cb     jdbcCallback  = new cb();


    // for jdbc
    private String driver           = "com.mysql.jdbc.Driver";
    private String url              = "jdbc:mysql://dubhe:3306/";
    private String database         = "coda";
    private String account          = "wolin";
    private String password         = "";
    private Connection con          = null;
    private PreparedStatement s     = null;
    
    
    // misc
    private boolean done           = false;
    private boolean debug          = false;
    
    
//-----------------------------------------------------------------------------

    //public cMsgLogger() {
        
   // }
    
    
//-----------------------------------------------------------------------------


    class cb implements cMsgCallback {
    
	public void callback(cMsgMessage msg, Object userObject) {
	    
	    try {
		if (debug) {
		    s.setString(1, msg.getSender());
		    s.setString(2, msg.getSenderHost());
		    s.setString(3, msg.getSenderTime().toString());
		    s.setString(4, msg.getDomain());
		    s.setString(5, msg.getSubject().substring(subject.length()+1));
		    s.setString(6, msg.getType());
		    s.setString(7, msg.getText());
		    s.execute();
		} else {
		    System.out.println("Sending:\n\n" + msg);
		}
	    } catch (SQLException e) {
		System.err.println("?sql error in callback\n" + e);
		System.exit(-1);
	    }
	}
    }    


//-----------------------------------------------------------------------------


    static public void main (String[] args) {
    
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
	int status = cmsg.subscribe(logger.subject+"/*", null, logger.jdbcCallback, null);
	if(status != cMsgConstants.ok) {
	}


	// load jdbc driver
	try {
	    Class.forName(logger.driver);
	}
	catch(Exception e) {
	    System.err.println("?unable to load driver: " + logger.driver +"\n" + e);
	    System.exit(-1);
	}
    

	// connect to database
	try  {
	    logger.con = DriverManager.getConnection(logger.url+logger.database,
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
	} catch (SQLException e) {
	    System.err.println("?unable to prepare statement\n" + e);
	    System.exit(-1);
	}


	// enable receipt of messages and delivery to callback
	cmsg.start();


	// wait for messages
	while(!logger.done) {
	    try {Thread.sleep(1);} catch (InterruptedException e) {}
	}


	// done
        try {logger.con.close();} catch (SQLException e) {}
	cmsg.done();
	System.exit(0);


    }  // main


//-----------------------------------------------------------------------------


    static public void decode_command_line(String[] args, cMsgLogger logger) {
  
	String help = "\nUsage:\n\n" + 
	    "   java cMsgLogger [-domain domain] [-name name] [-subject subject]\n" + 
	    "                   [-driver driver] [-url url] [-db database]\n" +
	    "                   [-account account] [-pwd password] [-debug]\n\n";
    

	// loop over all args
	for (int i=0; i<args.length; i++) {

	    if (args[i].equalsIgnoreCase("-h")) {
		System.out.println(help);
		System.exit(-1);
	
	    } else if(args[i].equalsIgnoreCase("-domain")) {
		logger.domain = args[i+1];
		i++;

	    } else if(args[i].equalsIgnoreCase("-subject")) {
		logger.subject = args[i+1];
		i++;

	    } else if(args[i].equalsIgnoreCase("-driver")) {
		logger.driver = args[i+1];
		i++;

	    } else if(args[i].equalsIgnoreCase("-url")) {
		logger.url = args[i+1];
		i++;

	    } else if(args[i].equalsIgnoreCase("-db")) {
		logger.database = args[i+1];
		i++;

	    } else if(args[i].equalsIgnoreCase("-account")) {
		logger.account = args[i+1];
		i++;

	    } else if(args[i].equalsIgnoreCase("-pwd")) {
		logger.password = args[i+1];
		i++;

	    } else if(args[i].equalsIgnoreCase("-debug")) {
		logger.debug = true;
	    }
	}    
    
	return;
    }
  

//-----------------------------------------------------------------------------


    // formats string for sql
    static public String sqlfmt(String s) {
	
	if(s==null)return("''");
	
	StringBuffer sb = new StringBuffer();
	
	sb.append("'");
	for(int i=0; i<s.length(); i++)  {
	    sb.append(s.charAt(i));
	    if(s.charAt(i)=='\'')sb.append("'");
	}
	sb.append("'");
    
	return(sb.toString());
    }


//-----------------------------------------------------------------------------
}        //  end class definition:  cMsgLogger
//-----------------------------------------------------------------------------

//  cMsgLogger.java

//  receives cMsg messages and logs them to a database

//  subscribes to subject/* where subject is also the logging table name
//  messages are published to subject/topic


//  still to do:


//  E.Wolin, 25-jun-2004



import java.io.*;
import java.lang.*;
import java.sql.*;
import cMsg.*;


public class cMsgLogger {


    // for cMsg
    private static String domain        = "coda://dubhe/coda";
    private static String name          = "cMsgLogger";
    private static String description   = "CODA cMsg Message Logger";
    private static String subject       = "cmsglog";


    // for jdbc
    private static String driver           = "com.mysql.jdbc.Driver";
    private static String url              = "jdbc:mysql://dubhe:3306/";
    private static String database         = "coda";
    private static String account          = "wolin";
    private static String password         = "";
    private static Connection con          = null;
    private static PreparedStatement s     = null;
    
    
    // misc
    private static bool done             = false;
    private static bool debug            = false;
    
    
//-----------------------------------------------------------------------------


    private class cb implements cMsgCallback {
    
	public void callback(cMsgMessage msg, Object userObject) {
	    
	    try {
		if(debug) {
		    s.setString(1,msg.sender);
		    s.setString(2,msg.senderHost);
		    s.setString(3,msg.senderTime);
		    s.setString(4,msg.domain);
		    s.setString(5,msg.subject.substring(subject.length()+1));
		    s.setString(6,msg.type);
		    s.setString(7,msg.text);
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
    
	int status;
	

	// decode command line
	decode_command_line(args);


	// connect to cMsg server
	cMsg cmsg = new cMsg(domain,name,description);
	if(!cmsg) {
	}


	// subscribe and provide callback
	status=cmsg.Subscribe(subject+"/*",null,new cb(),null);
	if(status!=CMSG_OK) {
	}


	// load jdbc driver
	try {
	    Class.forName(driver);
	}
	catch(Exception e) {
	    System.err.println("?unable to load driver: " + driver +"\n" + e);
	    System.exit(-1);
	}
    

	// connect to database
	try  {
	    con = DriverManager.getConnection(url+database,account,password);
	}
	catch (Exception e) {
	    System.err.println("?unable to connect to database: " + url + database + "\n" + e);
	    System.exit(-1);
	}
    
	
	// get prepared statement object
	try {
	    String sql = "insert into " + subject + " (" + 
		"sender,host,time,domain,topic,type,text" + 
		") values (" +
		"?,?,?,?,?, ?,?" +
		")";
	    s = con.prepareStatement(sql);
	} catch (SQLException e) {
	    System.err.println("?unable to prepare statement\n" + e);
	    System.exit(-1);
	}


	// enable receipt of messages and delivery to callback
	cmsg.Start();


	// wait for messages
	while(!done) {
	    Thread.sleep(1);
	}


	// done
	con.close();
	cmsg.Done();
	System.exit(0);


    }  // main


//-----------------------------------------------------------------------------


    static public void decode_command_line(String[] args) {
  
	String help = "\nUsage:\n\n" + 
	    "   java cMsgLogger [-domain domain] [-name name] [-subject subject]\n" + 
	    "                   [-driver driver] [-url url] [-db database]\n"
	    "                   [-account account] [-pwd password] [-debug]\n\n";
    

	// loop over all args
	for(int i=0; i<args.length; i++) {

	    if(args[i].equalsIgnoreCase("-h")) {
		System.out.println(help);
		System.exit(-1);
	
	    } else if(args[i].equalsIgnoreCase("-domain")) {
		domain=args[i+1];
		i++;

	    } else if(args[i].equalsIgnoreCase("-subject")) {
		subject=args[i+1];
		i++;

	    } else if(args[i].equalsIgnoreCase("-driver")) {
		driver=args[i+1];
		i++;

	    } else if(args[i].equalsIgnoreCase("-url")) {
		url=args[i+1];
		i++;

	    } else if(args[i].equalsIgnoreCase("-db")) {
		database=args[i+1];
		i++;

	    } else if(args[i].equalsIgnoreCase("-account")) {
		account=args[i+1];
		i++;

	    } else if(args[i].equalsIgnoreCase("-pwd")) {
		password=args[i+1];
		i++;

	    } else if(args[i].equalsIgnoreCase("-debug")) {
		debug=true;
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

// still to do:
//   server shutdown?



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


import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgDomain.cMsgHandleRequests;
import java.util.*;
import java.util.concurrent.atomic.*;
import java.io.*;
import java.util.regex.*;



/**
 * cMsg subdomain handler for LogFile subdomain.
 *
 * Current implementation uses a PrintWriter.
 *
 * @author Elliott Wolin
 * @version 1.0
 */
public class LogFile implements cMsgHandleRequests {


    /** Hash table to store all client info.  Canonical name is key. */
    private static HashMap openFiles = new HashMap(100);


    /** Inner class to hold log file information. */
    private static class LogFileObject {
        Object printHandle;
	AtomicInteger count;

        LogFileObject(Object handle) {
            printHandle = handle;
            count       = new AtomicInteger(1);
        }
    }


    /** Name of client using this subdomain handler. */
    private String myName;
    private String myHost;
    private int myPort;


    /** File name for this client. */
    private String myFileName;


    /** Canonical file name for this client. */
    private String myCanonicalName;


    /** print handle for this client. */
    private Object myPrintHandle = null;


    /** UDL remainder for this client. */
    private String myUDLRemainder;




    /**
     * Method to tell if the "send" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSendRequest}
     * method.
     *
     * @return true if get implemented in {@link #handleSendRequest}
     */
    public boolean hasSend() {
        return true;
    };


    /**
     * Method to tell if the "syncSend" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSyncSendRequest}
     * method.
     *
     * @return true if send implemented in {@link #handleSyncSendRequest}
     */
    public boolean hasSyncSend() {
        return true;
    };


    /**
     * Method to tell if the "get" cMsg API function is implemented
     * by this interface implementation in the {@link #handleGetRequest}
     * method.
     *
     * @return true if get implemented in {@link #handleGetRequest}
     */
    public boolean hasGet() {
        return false;
    };


    /**
     * Method to tell if the "subscribe" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSubscribeRequest}
     * method.
     *
     * @return true if subscribe implemented in {@link #handleSubscribeRequest}
     */
    public boolean hasSubscribe() {
        return false;
    };


    /**
     * Method to tell if the "unsubscribe" cMsg API function is implemented
     * by this interface implementation in the {@link #handleUnsubscribeRequest}
     * method.
     *
     * @return true if unsubscribe implemented in {@link #handleUnsubscribeRequest}
     */
    public boolean hasUnsubscribe() {
        return false;
    };


    /**
     * Method to give the subdomain handler the appropriate part
     * of the UDL the client used to talk to the domain server.
     *
     * @param UDLRemainder last part of the UDL appropriate to the subdomain handler
     * @throws cMsgException
     */
    public void setUDLRemainder(String UDLRemainder) throws cMsgException {
        myUDLRemainder=UDLRemainder;
    }
    

    /**
     * Method to see if domain client is registered.
     *
     * @param name name of client
     * @return true if client registered, false otherwise
     */
    public boolean isRegistered(String name) {
	return false;  // no limit on how many registrations per client
    }


    /**
     * Method to register domain client.
     *
     * @param name name of client
     * @param host host client is running on
     * @param port port client is listening on
     * @throws cMsgException if unable to register
     */
    public void registerClient(String name, String host, int port) throws cMsgException {

	String remainder = null;  // not used yet...could hold file open flags..
	LogFileObject l;
	PrintWriter pw;

        myName = name;  // not used for anything
	myHost = host;  //         "
	myPort = port;  //         "


	//  extract file name from UDL remainder
	try {
	    if(myUDLRemainder.indexOf("?")>0) {
		Pattern p = Pattern.compile("^(.+?)(\\?)(.*)$");
		Matcher m = p.matcher(myUDLRemainder);
		m.find();
		myFileName = m.group(1);
		remainder  = m.group(2);
	    } else {
		myFileName = myUDLRemainder;
	    }
	} catch (Exception e) {
	    e.printStackTrace();
	    cMsgException ce = new cMsgException(e.getMessage());
	    ce.setReturnCode(1);
	    throw ce;
	}


	// get canonical name
	try {
	    File f = new File(myFileName);
	    if(f.exists())myCanonicalName=f.getCanonicalPath();
	} catch (Exception e) {
	    e.printStackTrace();
	    cMsgException ce = new cMsgException("Unable to get canonical name");
	    ce.setReturnCode(1);
	    throw ce;
	}


	// check if file already open
	synchronized(openFiles) {
	    if(openFiles.containsKey(myCanonicalName)) {
		l=(LogFileObject)openFiles.get(myCanonicalName);
		myPrintHandle=l.printHandle;
		l.count.incrementAndGet();
		
		
 	    // file not open...open file in append mode, create print object and hash entry, write initial XML stuff, etc.
	    } else {
		try {
		    pw = new PrintWriter(new BufferedWriter(new FileWriter(myFileName,true)));
		    myPrintHandle = (Object)pw;
		    openFiles.put(myCanonicalName,new LogFileObject(myPrintHandle));
		    pw.println("<cMsgLogFile  name=\"" + myFileName + "\"" + "  date=\"" + (new Date()) + "\">\n\n");
		} catch (Exception e) {
		    System.out.println(e);
		    e.printStackTrace();
		    cMsgException ce = new cMsgException("registerClient: unable to open file");
		    ce.setReturnCode(1);
		    throw ce;
		}
	    }
	}
    }
    
    
    /**
     * Method to handle message sent by client.
     *
     * @param msg message from sender
     * @throws cMsgException if a channel to the client is closed, cannot be created,
     *                          or socket properties cannot be set
     */
    public void handleSendRequest(cMsgMessage msg) throws cMsgException {
	msg.setReceiver("cMsg:LogFile");
	try {
	    ((PrintWriter)myPrintHandle).println(msg);
	} catch (Exception e) {
	    System.out.println(e);
	    e.printStackTrace();
	    cMsgException ce = new cMsgException("handleSendRequest: unable to write");
	    ce.setReturnCode(1);
	    throw ce;
	}
    }


    /**
     * Method to handle message sent by domain client in synchronous mode.
     * It requries an integer response from the subdomain handler.
     *
     * @param msg message from sender
     * @return response from subdomain handler
     * @throws cMsgException
     */
    public int handleSyncSendRequest(cMsgMessage msg) throws cMsgException {
	handleSendRequest(msg);
        return 0;
    }


    /**
     * Method to synchronously get a single message from the server for a given
     * subject and type -- perhaps from a specified receiver.
     *
     * @param message message requesting what sort of message to get
     */
    public void handleGetRequest(cMsgMessage message) {
        // do nothing...
    }


    /**
     * Method to handle unget request sent by domain client (hidden from user).
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     */
    public void handleUngetRequest(String subject, String type) {
        // do mothing
    }


    /**
     * Method to handle subscribe request sent by domain client.
     *
     * @param subject message subject to subscribe to
     * @param type message type to subscribe to
     * @param receiverSubscribeId message id refering to these specific subject and type values
     * @throws cMsgException if no client information is available or a subscription for this
     *                          subject and type already exists
     */
    public void handleSubscribeRequest(String subject, String type,
                                       int receiverSubscribeId) throws cMsgException {
        // do nothing...
    }


    /**
     * Method to handle sunsubscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     */
    public void handleUnsubscribeRequest(String subject, String type) {
        // do nothing...
    }


    /**
     * Method to handle keepalive sent by domain client checking to see
     * if the domain server socket is still up. Normally nothing needs to
     * be done as the domain server simply returns an "OK" to all keepalives.
     * This method is run after all exchanges between domain server and client.
     */
    public void handleKeepAlive() {
        // do nothing...
    }


    /**
     * Method to handle a client shutdown.
     *
     * @throws cMsgException
     */
    public void handleClientShutdown() throws cMsgException {
	synchronized(openFiles) {
	    LogFileObject l = (LogFileObject)openFiles.get(myCanonicalName);
	    if(l.count.decrementAndGet()<=0) {
		((PrintWriter)myPrintHandle).println("</cMsgLogFile>\n");
		((PrintWriter)myPrintHandle).println("\n\n\n<!--===========================================================================================-->\n\n\n");
		((PrintWriter)myPrintHandle).close();
		openFiles.remove(myCanonicalName);
	    }
	}
    }


    /**
     * Method to handle a complete name server shutdown.
     * This method is run after all exchanges between domain server and client but
     * before the server is killed (since that is what is running this
     * method).
     */
    public void handleServerShutdown() throws cMsgException {
	// not working yet...
    }

}

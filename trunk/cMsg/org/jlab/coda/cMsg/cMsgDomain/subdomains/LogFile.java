// still to do:
//   need file open parameters: new/append, etc.
//   does object go away if already registered?
//   return code values?



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


    /** Hash table to store all client info.  Name is key and file object is value. */
    private static HashMap clients = new HashMap(100);



    /** Class to hold log file name and handle. */
    private static class LogFileObject {
        String logFileName;
        Object logFileHandle;

        LogFileObject(String name, Object handle) {
            logFileName   = name;
            logFileHandle = handle;
        }

    }


    /**  file object for this client. */
    private LogFileObject myLogFileObject = null;


    /** Name of client using this subdomain handler. */
    private String myName;


    /** UDL remainder for this subdomain handler. */
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
     * @param name name of client
     * @return true if client registered, false otherwise
     */
    public boolean isRegistered(String name) {
	synchronized (clients) {
	    if (clients.containsKey(name)) return true;
	    return false;
	}
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

        myName = name;
	String fname;


	//  extract file name from UDL remainder
	try {
	    if(myUDLRemainder.indexOf("?")>0) {
		Pattern p = Pattern.compile("^(.+)(\\?)(.*)$");
		Matcher m = p.matcher(myUDLRemainder);
		m.find();
		fname = m.group(1);
	    } else {
		fname=myUDLRemainder;
	    }
	} catch (Exception e) {
	    e.printStackTrace();
	    cMsgException ce = new cMsgException(e.getMessage());
	    ce.setReturnCode(1);
	    throw ce;
	}


	// check if this file already open
	synchronized (clients) {
	    LogFileObject o;
	    for(Iterator i = clients.values().iterator(); i.hasNext();) {
		o=(LogFileObject)i.next();
		if((o.logFileName).equals(fname)) {
		    myLogFileObject=o;
		    break;
		}
	    }
	    
	    
	    // file not open...open new file, create LogFileObject, write initial XML stuff to file
	    if(myLogFileObject==null) {
		try {
		    myLogFileObject = new LogFileObject(fname,
							new PrintWriter(new BufferedWriter(new FileWriter(fname))));
		    ((PrintWriter)(myLogFileObject.logFileHandle)).println("<cMsgLogFile  name=\"" + fname + "\""
									   + "  date=\"" + (new Date()) + "\"\n\n");
		} catch (Exception e) {
		    System.out.println(e);
		    e.printStackTrace();
		    cMsgException ce = new cMsgException("registerClient: unable to open file");
		    ce.setReturnCode(1);
		    throw ce;
		}
	    }
	    
	    // register file name and object
	    clients.put(myName,myLogFileObject);
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
        ((PrintWriter)myLogFileObject.logFileHandle).println(msg);
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
	msg.setReceiver("cMsg:LogFile");
        ((PrintWriter)myLogFileObject.logFileHandle).println(msg);
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
     * Method to handle a client or domain server shutdown.
     * This method is run after all exchanges between domain server and client but
     * before the domain server thread is killed (since that is what is running this
     * method).
     */
    public void handleClientShutdown() throws cMsgException {
        synchronized (clients) {
            clients.remove(myName);
        }
    }


    /**
     * Method to handle a complete name server down.
     * This method is run after all exchanges between domain server and client but
     * before the server is killed (since that is what is running this
     * method).
     */
    public void handleServerShutdown() throws cMsgException {
	PrintWriter pw;
	for(Iterator i = clients.values().iterator(); i.hasNext();) {
	    pw=(PrintWriter)(((LogFileObject)i.next()).logFileHandle);
	    try {
		pw.println("\n\n<cMsgLogFile>");
		pw.close();
	    } catch (Exception e) {
		// ignore errors
	    }
	}
    }

}

// still to do:
//    need file open parameters: name, new/append, etc.
//    need to feed back "not implemented" to caller
//    handleShutdown not called!
//    handleGetRequest for replaying file...what about simultaneous reading/writing?


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


package org.jlab.coda.cMsg.plugins;


import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgHandleRequests;
import org.jlab.coda.cMsg.cMsgException;
import java.util.*;
import java.io.*;



/**
 * cMsg subdomain handler for Logfile subdomain.
 *
 * Current implementation uses a PrintWriter.
 *
 * @author Elliott Wolin
 * @version 1.0
 */
public class Logfile implements cMsgHandleRequests {

    // debug...
    static String myLogFileName = "Logfile.log";


    /** Hash table to store all client info.  Name is key and file object is value. */
    static HashMap clients = new HashMap(100);


    /** Object to hold log file name and handle */
    class LogFileObject {
	String logFileName;
	Object logFileHandle;

	LogFileObject(String name, Object handle) {
	    logFileName=name;
	    logFileHandle=handle;
	}

    }


    //  file object for this client
    LogFileObject myLogFileObject = null;



    /**
     * Method to see if domain client is registered.
     * @param name name of client
     * @return true if client registered, false otherwise
     */
    public boolean isRegistered(String name) {
        if (clients.containsKey(name)) return true;
        return false;
    }


    /**
     * Method to register domain client.
     *
     * @param name name of client
     * @param host host client is running on
     * @param port port client is listening on
     * @throws cMsgException if client already exists
     */
    public void registerClient(String name, String host, int port) throws cMsgException {

        if (clients.containsKey(name)) {
            throw new cMsgException("registerClient: client already exists");
        }


	// check if this file already open
	LogFileObject f;
	for(Iterator i = clients.values().iterator(); i.hasNext();) {
	    f=(LogFileObject)i.next();
	    if((f.logFileName).equals(myLogFileName)) {
		myLogFileObject=f;
		break;
	    }
	}

	// file not open...open and create new LogFileObject
	if(myLogFileObject==null) {
	    try {
		myLogFileObject = new LogFileObject(myLogFileName,
					      new PrintWriter(new BufferedWriter(new FileWriter(myLogFileName))));
		((PrintWriter)(myLogFileObject.logFileHandle)).println("<cMsgLogFile  name=\"" + myLogFileName + "\""
								 + "  date=\"" + (new Date()) + "\"\n\n");
	    } catch (Exception e) {
		System.out.println(e);
		e.printStackTrace();
		System.exit(-1);
	    }
	}


	// register file name and handle
        synchronized (clients) {
            clients.put(name,myLogFileObject);
        }
    }


    /**
     * Method to unregister domain client.
     * @param name name of client
     */
    public void unregisterClient(String name) {
        synchronized (clients) {
            clients.remove(name);
	}
    }


    /**
     * Method to handle message sent by client.
     *
     * @param name name of client
     * @param msg message from sender
     * @throws cMsgException if a channel to the client is closed, cannot be created,
     *                          or socket properties cannot be set
     */
    public void handleSendRequest(String name, cMsgMessage msg) throws cMsgException {
	((PrintWriter)myLogFileObject.logFileHandle).println(msg);
    }


    /**
     * Method to handle subscribe request sent by domain client.
     *
     * @param name name of client
     * @param subject message subject to subscribe to
     * @param type message type to subscribe to
     * @param receiverSubscribeId message id refering to these specific subject and type values
     * @throws cMsgException if no client information is available or a subscription for this
     *                          subject and type already exists
     */
    public void handleSubscribeRequest(String name, String subject, String type,
                                       int receiverSubscribeId) throws cMsgException {
	// do nothing...
    }


    /**
     * Method to handle sunsubscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     *
     * @param name name of client
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     */
    public void handleUnsubscribeRequest(String name, String subject, String type) {
	// do nothing...
    }


    /**
     * Method to handle keepalive sent by domain client checking to see
     * if the domain server socket is still up. Normally nothing needs to
     * be done as the domain server simply returns an "OK" to all keepalives.
     * This method is run after all exchanges between domain server and client.
     *
     * @param name name of client
     */
    public void handleKeepAlive(String name) {
	// do nothing...
    }


    /**
     * Method to handle a disconnect request sent by domain client.
     * Normally nothing needs to be done as the domain server simply returns an
     * "OK" and closes the channel. This method is run after all exchanges between
     * domain server and client.
     *
     * @param name name of client
     */
    public void handleDisconnect(String name) {
	// do nothing...
    }


    /**
     * Method to handle a request sent by domain client to shut the domain server down.
     * This method is run after all exchanges between domain server and client but
     * before the domain server thread is killed (since that is what is running this
     * method).
     *
     * @param name name of client
     */
    public void handleShutdown(String name) {
	// close out all files
	System.out.println("closing log files...");  // debug...
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

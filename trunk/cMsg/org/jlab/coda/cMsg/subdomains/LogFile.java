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


package org.jlab.coda.cMsg.subdomains;


import org.jlab.coda.cMsg.cMsgMessageFull;
import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgSubdomainInterface;
import org.jlab.coda.cMsg.cMsgClientInfo;

import java.util.*;
import java.util.concurrent.atomic.*;
import java.io.*;
import java.util.regex.*;


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * cMsg subdomain handler for LogFile subdomain.
 *
 * Current implementation uses a PrintWriter.
 *
 * @author Elliott Wolin
 * @version 1.0
 */
public class LogFile implements cMsgSubdomainInterface {


    /** Hash table to store all client info.  Canonical name is key. */
    private static HashMap openFiles = new HashMap(100);


    /** Inner class to hold log file information. */
    private static class LogFileObject {
	PrintWriter printHandle;
	AtomicInteger count;

        LogFileObject(PrintWriter handle) {
            printHandle = handle;
            count       = new AtomicInteger(1);
        }
    }


    /** File name for this client. */
    private String myFileName;


    /** Canonical file name for this client. */
    private String myCanonicalName;


    /** print handle for this client. */
    private PrintWriter myPrintHandle = null;


    /** UDL remainder for this client. */
    private String myUDLRemainder;




//-----------------------------------------------------------------------------


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


//-----------------------------------------------------------------------------


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


//-----------------------------------------------------------------------------


    /**
     * Method to tell if the "subscribeAndGet" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSubscribeAndGetRequest}
     * method.
     *
     * @return true if subscribeAndGet implemented in {@link #handleSubscribeAndGetRequest}
     */
    public boolean hasSubscribeAndGet() {
        return false;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to tell if the "sendAndGet" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSendAndGetRequest}
     * method.
     *
     * @return true if sendAndGet implemented in {@link #handleSendAndGetRequest}
     */
    public boolean hasSendAndGet() {
        return false;
    }


//-----------------------------------------------------------------------------


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


//-----------------------------------------------------------------------------


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


//-----------------------------------------------------------------------------


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


//-----------------------------------------------------------------------------


    /**
     * Method to register domain client.
     *
     * @param info information about client
     * @throws cMsgException if unable to register
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {

        String remainder = null;  // not used yet...could hold file open flags..
        LogFileObject l;


        //  extract file name from UDL remainder
	if (myUDLRemainder.indexOf("?") > 0) {
	    Pattern p = Pattern.compile("^(.+?)(\\?)(.*)$");
	    Matcher m = p.matcher(myUDLRemainder);
	    m.find();
	    myFileName = m.group(1);
	    remainder = m.group(2);
	}
	else {
	    myFileName = myUDLRemainder;
	}


        // get canonical name
        try {
            File f = new File(myFileName);
            if (f.exists()) myCanonicalName = f.getCanonicalPath();
        } catch (IOException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException("Unable to get canonical name");
            ce.setReturnCode(1);
            throw ce;
        }


        // check if file already open
        synchronized (openFiles) {
            if (openFiles.containsKey(myCanonicalName)) {
                l = (LogFileObject) openFiles.get(myCanonicalName);
                myPrintHandle = l.printHandle;
                l.count.incrementAndGet();


                // file not open...open file in append mode, create print object and hash entry, write initial XML stuff, etc.
            }
            else {
                try {
                    myPrintHandle = new PrintWriter(new BufferedWriter(new FileWriter(myFileName, true)));
                    openFiles.put(myCanonicalName, new LogFileObject(myPrintHandle));
                    myPrintHandle.println("<cMsgLogFile  name=\"" + myFileName + "\"" + "  date=\"" + (new Date()) + "\">\n\n");
                } catch (IOException e) {
                    System.out.println(e);
                    e.printStackTrace();
                    cMsgException ce = new cMsgException("registerClient: unable to open file");
                    ce.setReturnCode(1);
                    throw ce;
                }
            }
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle message sent by client.
     *
     * @param msg message from sender
     * @throws cMsgException if a channel to the client is closed, cannot be created,
     *                       or socket properties cannot be set
     */
    public void handleSendRequest(cMsgMessageFull msg) throws cMsgException {
        msg.setReceiver("cMsg:LogFile");
	myPrintHandle.println(msg);
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle message sent by domain client in synchronous mode.
     * It requries an integer response from the subdomain handler.
     *
     * @param msg message from sender
     * @return response from subdomain handler
     * @throws cMsgException
     */
    public int handleSyncSendRequest(cMsgMessageFull msg) throws cMsgException {
        handleSendRequest(msg);
        return 0;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to synchronously get a single message from a receiver by sending out a
     * message to be responded to.
     *
     * @param message message requesting what sort of message to get
     */
    public void handleSendAndGetRequest(cMsgMessageFull message) {
        // do nothing
    }


//-----------------------------------------------------------------------------


    /**
     * Method to synchronously get a single message from the server for a one-time
     * subscription of a subject and type.
     *
     * @param subject message subject subscribed to
     * @param type    message type subscribed to
     * @param id      message id refering to these specific subject and type values
     */
    public void handleSubscribeAndGetRequest(String subject, String type, int id) {
        // no nothing
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle remove sendAndGet request sent by domain client
     * (hidden from user).
     *
     * @param id message id refering to these specific subject and type values
     */
    public void handleUnSendAndGetRequest(int id) {
        // do nothing
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle remove subscribeAndGet request sent by domain client
     * (hidden from user).
     *
     * @param id message id refering to these specific subject and type values
     */
    public void handleUnSubscribeAndGetRequest(int id) {
        // do nothing
    }


//-----------------------------------------------------------------------------


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


//-----------------------------------------------------------------------------


    /**
     * Method to handle sunsubscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     * @param receiverSubscribeId message id refering to these specific subject and type values
     */
    public void handleUnsubscribeRequest(String subject, String type, int receiverSubscribeId) {
        // do nothing...
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle keepalive sent by domain client checking to see
     * if the domain server socket is still up. Normally nothing needs to
     * be done as the domain server simply returns an "OK" to all keepalives.
     * This method is run after all exchanges between domain server and client.
     */
    public void handleKeepAlive() {
        // do nothing...
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle a client shutdown.
     *
     * @throws cMsgException
     */
    public void handleClientShutdown() throws cMsgException {
        synchronized (openFiles) {
            LogFileObject l = (LogFileObject) openFiles.get(myCanonicalName);
            if (l.count.decrementAndGet() <= 0) {
                myPrintHandle.println("</cMsgLogFile>\n");
                myPrintHandle.println("\n\n\n<!--===========================================================================================-->\n\n\n");
                myPrintHandle.close();
                openFiles.remove(myCanonicalName);
            }
        }
    }


//-----------------------------------------------------------------------------


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


//-----------------------------------------------------------------------------


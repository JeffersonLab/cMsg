// still to do:


/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    E. Wolin, 12-Nov-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Elliott Wolin                                                  *
 *             wolin@jlab.org                    Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-7365             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.FileDomain;

import org.jlab.coda.cMsg.*;

import java.io.*;
import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.net.*;


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * This class implements a client in the cMsg File domain.
 *
 * @author Elliott Wolin
 * @version 1.0
 */
public class File extends cMsgAdapter {

    private String myFileName;
    private PrintWriter myPrintHandle;
    private boolean textOnly = false;


//-----------------------------------------------------------------------------


    /**
     * Constructor for File domain.
     *
     * Default is to print entire message to file.
     * set textOnly=true in UDL to only print timestamp and message text.
     *
     * @param UDL Uniform Domain Locator holds file name
     * @param name does not need to be unique
     * @param description description of this client
     * @throws cMsgException if domain in not implemented or there are problems
     */
    public File(String UDL, String name, String description) throws cMsgException {

	Pattern p;
	Matcher m;
	String remainder = null;

	// save params
        this.UDL         = UDL;
        this.name        = name;
        this.description = description;

	try {
	    this.host = InetAddress.getLocalHost().getHostName();
	} catch (UnknownHostException e) {
	    System.err.println(e);
	    this.host="unknown";
	}


	// parse file name
	p = Pattern.compile("^\\s*(cMsg:)?File://(.+)$",Pattern.CASE_INSENSITIVE);
	int ind = UDL.indexOf('?');
	if(ind<=0) {
	    domain = UDL;
	} else {
	    domain    = UDL.substring(0,ind);
	    remainder = UDL.substring(ind+1);
	}
	m = p.matcher(domain);
	m.find();
	myFileName = m.group(2);
	

	// parse remainder
	if(remainder!=null) {
	    p = Pattern.compile("textOnly=(\\w+)",Pattern.CASE_INSENSITIVE);
	    m = p.matcher(remainder);
	    m.find();
	    textOnly = m.group(1).equals("true");
	}
    }


//-----------------------------------------------------------------------------


    /**
     * Opens file.
     *
     * @throws cMsgException if there are communication problems
     */
    public void connect() throws cMsgException {

	try{
	    myPrintHandle = new PrintWriter(new BufferedWriter(new FileWriter(myFileName, true)));
	    myPrintHandle.println("<cMsgFile  name=\"" + myFileName + "\"" + "  date=\"" + (new Date()) + "\">\n\n");
	} catch (IOException e) {
	    System.out.println(e);
	    e.printStackTrace();
	    cMsgException ce = new cMsgException("connect: unable to open file");
	    ce.setReturnCode(1);
	    throw ce;
	}
    }


//-----------------------------------------------------------------------------


    /**
     * Closes file.
     *
     */
    public void disconnect() {
	
	myPrintHandle.println("\n\n</cMsgFile>\n");
	myPrintHandle.println("\n\n<!--===========================================================================================-->\n\n\n");
	myPrintHandle.close();
    }


//-----------------------------------------------------------------------------


    /**
     * Writes to file.
     *
     * @param message message to send
     * @throws cMsgException (not thrown)
     */
    public void send(cMsgMessage msg) throws cMsgException {
	Date now = new Date();
	if(textOnly) {
	    myPrintHandle.println(now + ":    " + msg.getText());
	} else {
	    msg.setDomain(domain);
	    msg.setSender(name);
	    msg.setSenderHost(host);
	    msg.setSenderTime(now);
	    msg.setReceiver(domain);
	    msg.setReceiverTime(now);
	    msg.setReceiverHost(host);
	    myPrintHandle.println(msg);
	}
    }


//-----------------------------------------------------------------------------


    /**
     * Calls send to write to file.
     *
     * @param message message
     * @return response from subdomain handler
     * @throws cMsgException
     */
    public int syncSend(cMsgMessage message) throws cMsgException {
	send(message);
	return(0);
    }


//-----------------------------------------------------------------------------


    /**
     * Flushes output.
     */
    public void flush() {
	myPrintHandle.flush();
        return;
    }


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
}

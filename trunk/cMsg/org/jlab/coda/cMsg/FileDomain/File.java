// still to do:
//   what to log?  message?  text only?


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


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * This class implements a client in the cMsg File domain.
 *
 * @author Elliott Wolin
 * @version 1.0
 */
public class File extends cMsgAdapter {

    String myFileName;
    PrintWriter myPrintHandle;



//-----------------------------------------------------------------------------


    /**
     * Constructor for File domain.
     *
     * @param UDL Uniform Domain Locator holds file name
     * @param name does not need to be unique
     * @param description description of this client
     * @throws cMsgException if domain in not implemented or there are problems
     */
    public File(String UDL, String name, String description) throws cMsgException {

	// save params
        this.UDL         = UDL;
        this.name        = name;
        this.description = description;


	// parse file name
        Pattern p = Pattern.compile("^\\s*(cMsg:)?File://(.+)$",Pattern.CASE_INSENSITIVE);
        Matcher m;
	int ind = UDL.indexOf('?');
	if(ind<=0) {
	    m = p.matcher(UDL);
	} else {
	    m = p.matcher(UDL.substring(0,ind));
	}
        try {
	    m.find();
	    myFileName = m.group(2);
        } catch (Exception e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException(e.getMessage());
            ce.setReturnCode(1);
            throw ce;
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
	    PrintWriter pw = new PrintWriter(new BufferedWriter(new FileWriter(myFileName, true)));
	    myPrintHandle = pw;
	    pw.println("<cMsgFile  name=\"" + myFileName + "\"" + "  date=\"" + (new Date()) + "\">\n\n");
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
     * Writes to file
     *
     * @param message message to send
     * @throws cMsgException (not thrown)
     */
    public void send(cMsgMessage message) throws cMsgException {
	//	myPrintHandle.println(message);
	myPrintHandle.println(message.getText());  // ???
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
        return;
    }


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
}

// still to do:


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

import org.jlab.coda.cMsg.cMsgConstants;
import org.jlab.coda.cMsg.cMsgMessageFull;
import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgClientInfo;
import org.jlab.coda.cMsg.cMsgSubdomainAdapter;

import java.io.*;
import java.nio.ByteBuffer;


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * cMsg subdomain handler to access tcpserver processes
 *
 * @author Elliott Wolin
 * @version 1.0
 */
public class tcpserver extends cMsgSubdomainAdapter{


    /** registration params. */
    private cMsgClientInfo myClientInfo;

    /** direct buffer needed for nio socket IO. */
    private ByteBuffer myBuffer = ByteBuffer.allocateDirect(2048);

    /** UDL remainder. */
    private String myUDLRemainder;

    /** default tcpserver port. */
    private int defaultTcpServerPort = 12345;

    // tcpserver connection handle
    private int tcpServerHandle = 0;


//-----------------------------------------------------------------------------


    /**
     * Method to tell if the "sendAndGet" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSendAndGetRequest}
     * method.
     *
     * @return true if sendAndGet implemented in {@link #handleSendAndGetRequest}
     */
    public boolean hasSendAndGet() {
        return true;
    }


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
     * Connects to tcpserver.
     *
     * @param info information about client
     * @throws cMsgException if unable to register
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {

        String srv     = null;
        String srvHost = null;
        int srvPort    = 0;


        // save client info
        myClientInfo = info;


        // extract tcpserver host and port from UDL remainder
        int ind = myUDLRemainder.indexOf("?");
        if(ind>0) {
            srv=myUDLRemainder.substring(0,ind);
        } else {
            srv=myUDLRemainder;
        }

        ind=srv.indexOf(":");
        if(ind>0) {
            srvHost=srv.substring(0,ind);
            srvPort=Integer.parseInt(srv.substring(ind));
        } else {
            srvHost=srv;
            srvPort=defaultTcpServerPort;
        }


        // establish connection to server
        if(true) {
            cMsgException ce = new cMsgException("");
            ce.setReturnCode(1);
            throw ce;
        }

    }


//-----------------------------------------------------------------------------


    /**
     * Sends text string to server to execute, returns result.
     */
    public void handleSendAndGetRequest(cMsgMessageFull msg) {

        boolean null_response = false;

        // extract command string from text field and send to tcpserver
        msg.getText();

        // return result
        try {
            cMsgMessageFull response = msg.response();
            response.setText("");
            if(null_response) {
                deliverMessage(myClientInfo.getChannel(),myBuffer,response,null,cMsgConstants.msgGetResponseIsNull);
            } else {
                deliverMessage(myClientInfo.getChannel(),myBuffer,response,null,cMsgConstants.msgGetResponse);
            }
        } catch (cMsgException ce) {
        } catch (IOException e) {
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle a client shutdown.
     *
     * @throws cMsgException
     */
    public void handleClientShutdown() throws cMsgException {

    }


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
}


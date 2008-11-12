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
 *             wolin@jlab.org                    Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-7365             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/


package org.jlab.coda.cMsg.cMsgDomain.subdomains;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgSubdomainAdapter;
import org.jlab.coda.cMsg.common.cMsgClientInfo;
import org.jlab.coda.cMsg.common.cMsgDeliverMessageInterface;

import java.io.*;
import java.net.*;
import java.net.Socket;



//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * cMsg subdomain handler to access tcpserver processes<p>
 *
 *  UDL:   cMsg://host:port/TcpServer/srvHost:srvPort<p>
 *
 * @author Elliott Wolin
 * @version 1.0
 */
public class TcpServer extends cMsgSubdomainAdapter {


    /** registration params. */
    private cMsgClientInfo myClientInfo;


    /** UDL remainder. */
    private String myUDLRemainder;


    /** Object used to deliver messages to the client. */
    private cMsgDeliverMessageInterface myDeliverer;


    // for tcpserver connection
    private String mySrvHost            = null;
    private int mySrvPort               = 5001;   // default port
    private static final int srvFlag    = 1;      // special flag


    // misc
    private String myName               = "tcpserver";
    private String myHost               = null;


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     * @return true
     */
    public boolean hasSendAndGet() {
        return true;
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @param UDLRemainder {@inheritDoc}
     * @throws cMsgException never
     */
    public void setUDLRemainder(String UDLRemainder) throws cMsgException {
        myUDLRemainder=UDLRemainder;
    }


//-----------------------------------------------------------------------------


    /**
     * Connects to tcpserver.
     *
     * @param info {@inheritDoc}
     * @throws cMsgException never
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {


        // save client info
        myClientInfo = info;


        // Get an object enabling this handler to communicate
        // with only this client in this cMsg subdomain.
        myDeliverer = info.getDeliverer();


        // extract tcpserver host and port from UDL remainder
        String s;
        int ind = myUDLRemainder.indexOf("?");
        if(ind>0) {
            s=myUDLRemainder.substring(0,ind);
        } else {
            s=myUDLRemainder;
        }

        ind=s.indexOf(":");
        if(ind>0) {
            mySrvHost=s.substring(0,ind);
            mySrvPort=Integer.parseInt(s.substring(ind));
        } else {
            mySrvHost=s;
        }


        // set host
        try {
            myHost = InetAddress.getLocalHost().getHostName();
        } catch (UnknownHostException e) {
            System.err.println(e);
            myHost = "unknown";
        }

    }


//-----------------------------------------------------------------------------


    /**
     * Sends text string to server to execute, returns result.
     * Uses stateless transaction.
     *
     * @param msg {@inheritDoc}
     * @throws cMsgException if cannot connect to server, cannot send/receive to/from server,
     *                       cannot deliver msg to client
     */
    public void handleSendAndGetRequest(cMsgMessageFull msg) throws cMsgException {

        Socket           socket;
        DataOutputStream request;
        BufferedReader   response;


        // establish connection to server
        try {
            socket = new Socket(mySrvHost,mySrvPort);
            socket.setTcpNoDelay(true);

            request = new DataOutputStream(new BufferedOutputStream(socket.getOutputStream()));
            response = new BufferedReader(new InputStreamReader(socket.getInputStream()));

        } catch (IOException e) {
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }


        // send request to server
        try {
            request.writeInt(srvFlag);
            request.writeInt(msg.getText().length());
            request.write(msg.getText().getBytes("ASCII"));
            request.writeByte('\0');
            request.flush();
            socket.shutdownOutput();
        } catch (IOException e) {
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }


        // get response
        String s;
        StringBuffer sb = new StringBuffer();
        try {
            while(((s=response.readLine())!=null) && (s.indexOf("value =")<0)) {
                sb.append(s+"\n");
            }
        } catch (IOException e) {
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        } finally {
            try {
                request.close();
                response.close();
                socket.close();
            } catch (Exception e) {
            }
        }


        // return result
        try {
            cMsgMessageFull responseMsg = new cMsgMessageFull();
            responseMsg.setSubject(msg.getSubject());
            responseMsg.setType(msg.getType());
            responseMsg.setText(sb.toString());
            responseMsg.setSender(myName);
            responseMsg.setSenderHost(myHost);

            if(msg.getText().length()<=0) {
                responseMsg.makeNullResponse(msg);
            } else {
                responseMsg.makeResponse(msg);
            }
            myDeliverer.deliverMessage(responseMsg, cMsgConstants.msgGetResponse);

        } catch (IOException e) {
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }
    }

}


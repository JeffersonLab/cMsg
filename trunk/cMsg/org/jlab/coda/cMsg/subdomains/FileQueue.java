// still to do:
//  lock queue and files


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

import org.jlab.coda.cMsg.*;

import java.net.*;
import java.io.*;
import java.nio.*;
import java.nio.channels.*;
import java.util.*;
import java.util.regex.*;
import java.util.Date;



//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * cMsg subdomain handler for FileQueue subdomain.
 *
 * UDL:  cMsg:cMsg://host:port/FileQueue/myQueueName?dir=myDirName.
 *
 * e.g. cMsg:cMsg://ollie/FileQueue/ejw?dir=/home/wolin/qdir.
 *
 * stores/retrieves cMsgMessageFull messages from file-based queue.
 *
 * @author Elliiott Wolin
 * @version 1.0
 */
public class FileQueue extends cMsgSubdomainAbstract {


    /** registration params. */
    private cMsgClientInfo myClientInfo;


    /** UDL remainder for this subdomain handler. */
    private String myUDLRemainder;


    /** nio buffers. */
    private ByteBuffer myBuffer  = ByteBuffer.allocateDirect(2048);


    // database access objects
    String myQueueName        = null;
    String myDirectory        = ".";
    String myFileNameBase     = null;
    String myLoSeqFile        = null;
    String myHiSeqFile        = null;
    String myHost             = null;


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
        return true;
    }


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
     * Creates separate database connection for each client connection.
     * UDL contains driver name, database JDBC URL, account, password, and table name to use.
     * Column names are fixed (domain, sender, subject, etc.).
     *
     * @param info information about client
     * @throws cMsgException upon error
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {

        Pattern p;
        Matcher m;
        String remainder = null;


        // set host
        try {
            myHost = InetAddress.getLocalHost().getHostName();
        } catch (UnknownHostException e) {
            System.err.println(e);
            myHost = "unknown";
        }


        // set myClientInfo
        myClientInfo=info;


        // extract queue name from UDL remainder
        if(myUDLRemainder.indexOf("?")>0) {
            p = Pattern.compile("^(.+?)(\\?.*)$");
            m = p.matcher(myUDLRemainder);
            if(m.find()) {
                myQueueName = m.group(1);
                remainder   = m.group(2) + "&";
            } else {
                cMsgException ce = new cMsgException("?illegal UDL");
                ce.setReturnCode(1);
                throw ce;
            }
        } else {
            myQueueName=myUDLRemainder;
        }


        // extract directory from remainder
        if(remainder!=null) {
            p = Pattern.compile("[&\\?]dir=(.*?)&", Pattern.CASE_INSENSITIVE);
            m = p.matcher(remainder);
            if(m.find()) myDirectory = m.group(1);
        }


        // form various file names
        myFileNameBase = myDirectory + "/cMsgFileQueue_" + myQueueName + "_";
        myHiSeqFile    = myFileNameBase + "Hi";
        myLoSeqFile    = myFileNameBase + "Lo";


        // init hi,lo files if they don't exist  ??? tricky to get locks straight
        File hi = new File(myHiSeqFile);
        File lo = new File(myLoSeqFile);
        if(!hi.exists()||!lo.exists()) {
            try {
                FileWriter h = new FileWriter(hi);  h.write("0\n");  h.close();
                FileWriter l = new FileWriter(lo);  l.write("0\n");  l.close();
            } catch (IOException e) {
                e.printStackTrace();
                cMsgException ce = new cMsgException(e.toString());
                ce.setReturnCode(1);
                throw ce;
            }
        }

    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle message sent by client.
     * Inserts message into SQL database table via JDBC.
     *
     * @param msg message from sender
     * @throws cMsgException if a channel to the client is closed, cannot be created,
     *                       or socket properties cannot be set
     */
    public void handleSendRequest(cMsgMessageFull msg) throws cMsgException {

        long hi;

        // write message to queue
        try {

            // lock queue and hi file

            // read hi file, increment, write new hi value to file
            RandomAccessFile r = new RandomAccessFile(myHiSeqFile,"rw");
            hi=Long.parseLong(r.readLine());
            hi++;
            r.seek(0);
            r.writeBytes(hi+"\n");
            r.close();

            // write message to queue
            FileWriter fw = new FileWriter(myFileNameBase + hi);
            fw.write(msg.toString());
            fw.close();

            // unlock queue and hi file


        } catch (IOException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }
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
        try {
            handleSendRequest(msg);
            return(0);
        } catch (cMsgException e) {
            return(1);
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle subscribe request sent by domain client.
     * Not implemented.
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
     * Not implemented.
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     * @param receiverSubscribeId message id refering to these specific subject and type values
     */
    public void handleUnsubscribeRequest(String subject, String type, int receiverSubscribeId) throws cMsgException {
        // do nothing...
    }


//-----------------------------------------------------------------------------

    /**
     * Method to synchronously get a single message from a receiver by sending out a
     * message to be responded to.
     *
     * Currently just returns message at head of queue.
     *
     * @param message message requesting what sort of message to get
     */
    public void handleSendAndGetRequest(cMsgMessageFull message) throws cMsgException {

        cMsgMessageFull response;
        RandomAccessFile r;
        long hi,lo;
        boolean null_response = false;

        try {
            // lock queue and both hi and lo files

            // read and close hi file
            r = new RandomAccessFile(myHiSeqFile,"r");
            hi=Long.parseLong(r.readLine());
            r.close();
            // unlock hi file

            // read lo file
            r = new RandomAccessFile(myLoSeqFile,"rw");
            lo=Long.parseLong(r.readLine());

            // message file exists only if hi>lo
            if(hi>lo) {
                lo++;
                r.seek(0);
                r.writeBytes(lo+"\n");

                // create response from file
                File f = new File(myFileNameBase + lo);
                if(f.exists()) {
                    response = new cMsgMessageFull(f);
                    f.delete();
                } else {
                    null_response=true;
                    response = new cMsgMessageFull();
                }

            } else {
                null_response=true;
                response = new cMsgMessageFull();
            }

            // close lo file
            r.close();

            // unlock queue and lo file


        } catch (IOException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }


        // mark as response to message
        response.makeResponse(message);


        // send response to client
        try {
            if(null_response) {
                deliverMessage(myClientInfo.getChannel(),myBuffer,response,null,cMsgConstants.msgGetResponse);
            } else {
                deliverMessage(myClientInfo.getChannel(),myBuffer,response,null,cMsgConstants.msgGetResponse);
            }
        } catch (IOException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }
    }


//-----------------------------------------------------------------------------


    /**
     * to synchronously get a single message from the server for a one-time
     * subscription of a subject and type.
     *
     * @param subject message subject subscribed to
     * @param type    message type subscribed to
     * @param id      message id refering to these specific subject and type values
     */
    public void handleSubscribeAndGetRequest(String subject, String type, int id) throws cMsgException {
        // do nothing
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle unget request sent by domain client (hidden from user).
     *
     * @param id message id refering to these specific subject and type values
     */
    public void handleUngetRequest(int id) throws cMsgException {
        // do nothing
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle keepalive sent by domain client checking to see
     * if the domain server socket is still up. Normally nothing needs to
     * be done as the domain server simply returns an "OK" to all keepalives.
     * This method is run after all exchanges between domain server and client.
     */
    public void handleKeepAlive() throws cMsgException {
        // do nothing...
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle a client shutdown.
     *
     * @throws cMsgException
     */
    public void handleClientShutdown() throws cMsgException {
        // do nothing
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle a complete name server down.
     * This method is run after all exchanges between domain server and client but
     * before the server is killed (since that is what is running this
     * method).
     */
    public void handleServerShutdown() throws cMsgException {
        // do nothing
    }


//-----------------------------------------------------------------------------
//  end class
//-----------------------------------------------------------------------------
}


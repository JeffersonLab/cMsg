// still to do:
//   server shutdown
//   C-c in client doesn't kill connection?
//   cMsgException error codes


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
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgClientInfo;
import org.jlab.coda.cMsg.cMsgSubdomainAbstract;

import java.util.*;
import java.util.regex.*;
import java.io.IOException;
import java.nio.ByteBuffer;

import com.smartsockets.*;


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * cMsg subdomain handler for smartsockets subdomain.
 *
 * Implements proxy smartsockets server.
 *
 * @author Elliott Wolin
 * @version 1.0
 *
 */
public class smartsockets extends cMsgSubdomainAbstract {


    /** static collections. */
    private static Map subjects  = Collections.synchronizedMap(new HashMap(100));
    private static Map callbacks = Collections.synchronizedMap(new HashMap(100));


    /** client registration info. */
    private cMsgClientInfo myClientInfo;


    /** direct buffer needed for nio socket IO. */
    private ByteBuffer myBuffer = ByteBuffer.allocateDirect(2048);


    /** UDL remainder. */
    private String myUDLRemainder = null;


    /** for smartsockets. */
    private TipcSrv mySrv     = null;
    private String myProject;


    /** misc */
    private boolean done      = false;


    /** smartsockets callback delivers message to client. */
    private class ProcessCb implements TipcProcessCb {
        public void process(TipcMsg msg, Object arg) {
            ArrayList subscribeList = new ArrayList(1);
            try {
                msg.setCurrent(0);
                cMsgMessage cmsg = new cMsgMessage();

                cmsg.setDomain("cMsg");                   // just use domain for now
                cmsg.setSysMsgId(msg.getSeqNum());
                cmsg.setSender(msg.getSender());
                cmsg.setSenderHost("unknown");            // no easy way to get this
                cmsg.setSenderTime(new Date((long)msg.getSenderTimestamp()*1000));
                //  cmsg.setSenderId(msg.getStreamID());  // no field matches
                cmsg.setSenderMsgId(msg.getSeqNum());
                cmsg.setReceiver(myClientInfo.getName());
                cmsg.setReceiverHost(myClientInfo.getClientHost());
                cmsg.setReceiverTime(new Date());
                cmsg.setReceiverSubscribeId(((Integer)arg).intValue());
                cmsg.setSubject(msg.getDest());
                cmsg.setType(msg.getType().getName());
                cmsg.setText(msg.nextStr());

                subscribeList.add((Integer)arg);  // add receiver subscribe ID to list
                deliverMessage(myClientInfo.getChannel(),myBuffer,cmsg,subscribeList,cMsgConstants.msgSubscribeResponse);

            } catch (TipcException e) {
                System.err.println(e);

            } catch (IOException e) {
                System.err.println(e);
            }
        }
    }


    /** for smartsockets main loop. */
    private class MainLoop extends Thread {
        public void run() {
            try {
                while(!done) {
                    mySrv.mainLoop(0.5);
                }
            } catch (TipcException e) {
                System.err.println(e);
            }
            System.out.println("...main loop done");
        }
    }


    // for counting subscriptions in subject hash
    private class MyInt {
        int count;
    }



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
        return false;
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
        return false;
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
        return true;
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
        return true;
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
     * @param info contains all client info
     * @throws cMsgException upon error
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {

        myClientInfo = info;


        // extract project from UDL remainder
        if(myUDLRemainder.indexOf("?")>0) {
            Pattern p = Pattern.compile("^(.+?)(\\?)(.*)$");
            Matcher m = p.matcher(myUDLRemainder);
            m.find();
            myProject = m.group(1);
        } else {
            myProject = myUDLRemainder;
        }


        // connect to server
        try {
            mySrv=TipcSvc.createSrv();
            mySrv.setOption("ss.project",         myProject);
            mySrv.setOption("ss.unique_subject",  myClientInfo.getName());
            mySrv.setOption("ss.monitor_ident",   myClientInfo.getDescription());
            if(!mySrv.create())throw new cMsgException("?unable to connect to server");

        } catch (TipcException e) {
            e.printStackTrace();
            System.err.println(e);
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }


        // launch server main loop in its own thread
        (new MainLoop()).start();
    }



//-----------------------------------------------------------------------------


    /**
     * Forwards message to smartsockets system.
     *
     * @param msg message from sender.
     * @throws cMsgException
     */
    public void handleSendRequest(cMsgMessage msg) throws cMsgException {

        String type = msg.getType();
        TipcMt mt   = null;


        // form smartsockets message type
        if(type.matches("\\d+")) {
            mt=TipcSvc.lookupMt(Integer.parseInt(type));
            if(mt==null) {
                try {
                    mt=TipcSvc.createMt(type,Integer.parseInt(type),"verbose");
                } catch (TipcException e) {
                    cMsgException ce = new cMsgException("?unable to create message type: " + type);
                    ce.setReturnCode(1);
                    throw ce;
                }
            }
        } else {
            mt=TipcSvc.lookupMt(type);
            if(mt==null) {
                cMsgException ce = new cMsgException("?unknown message type: " + type);
                ce.setReturnCode(1);
                throw ce;
            }
        }


        // create, fill, send, and flush message
        try {
            TipcMsg ssMsg = TipcSvc.createMsg(mt);
            ssMsg.setDest(msg.getSubject());
            ssMsg.setSenderTimestamp(msg.getSenderTime().getTime()/1000);
            ssMsg.appendStr(msg.getText());
            mySrv.send(ssMsg);
            mySrv.flush();
        } catch (TipcException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException(e.getMessage());
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
    public int handleSyncSendRequest(cMsgMessage msg) throws cMsgException {
        return(0);  // do nothing
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

        TipcMt mt = null;
        TipcCb cb = null;
        MyInt m;


        if(type.matches("\\d+")) {
            mt=TipcSvc.lookupMt(Integer.parseInt(type));
            if(mt==null) {
                try {
                    mt=TipcSvc.createMt(type,Integer.parseInt(type),"verbose");
                } catch (TipcException e) {
                    cMsgException ce = new cMsgException("?unable to create message type: " + type);
                    ce.setReturnCode(1);
                    throw ce;
                }
            }
        } else {
            mt=TipcSvc.lookupMt(type);
        }
        if(mt==null) {
            cMsgException ce = new cMsgException("?unknown message type: " + type);
            ce.setReturnCode(1);
            throw ce;
        }


        // create and register callback
        cb=mySrv.addProcessCb(new ProcessCb(),mt,subject,receiverSubscribeId);
        if(cb==null) {
            cMsgException ce = new cMsgException("?unable to create callback");
            ce.setReturnCode(1);
            throw ce;
        }


        // subscribe
        try {
            mySrv.setSubjectSubscribe(subject, true);
        } catch (TipcException e) {
            cMsgException ce = new cMsgException("?unable to subscribe to: " + subject);
            ce.setReturnCode(1);
            throw ce;
        }


        // update subject count for new subscription
        if(subjects.containsKey(subject)) {
            m = (MyInt)subjects.get(subject);
            m.count++;
            subjects.put(subject,m);
        } else {
            m = new MyInt();
            m.count=1;
            subjects.put(subject,m);
        }


        // hash callbacks by id
        callbacks.put(receiverSubscribeId,cb);

        return;
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle sunsubscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     * @param receiverSubscribeId receiver subscribe id
     */
    public void handleUnsubscribeRequest(String subject, String type, int receiverSubscribeId) {
        try {
            if(callbacks.containsKey(receiverSubscribeId)) {
                mySrv.removeProcessCb((TipcCb)callbacks.get(receiverSubscribeId));
                callbacks.remove(receiverSubscribeId);
            }
        } catch (TipcException e) {
            System.err.println("?unable to unsubscribe from subject " + subject);
        }


        // update subject table
        if(subjects.containsKey(subject)) {
            MyInt m = (MyInt)subjects.get(subject);
            m.count--;
            if(m.count>=1) {
                subjects.put(subject,m);
            } else {
                subjects.remove(subject);
                try {
                    mySrv.setSubjectSubscribe(subject,false);
                } catch (TipcException e) {
                    System.err.println("?unable to unsubscribe from subject " + subject);
                }
            }
        }
    }


//-----------------------------------------------------------------------------

    /**
     * Method to synchronously get a single message from a receiver by sending out a
     * message to be responded to.
     *
     * @param message message requesting what sort of message to get
     */
    public void handleSendAndGetRequest(cMsgMessage message) {
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
     * Method to handle unget request sent by domain client (hidden from user).
     *
     * @param id message id refering to these specific subject and type values
     */
    public void handleUngetRequest(int id) {
        // do mothing
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
        done=true;
        System.out.println("...shutdown for client " + myClientInfo.getName());
        try {
            mySrv.destroy(TipcSrv.CONN_NONE);
        } catch (TipcException e) {
            System.err.println(e);
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Method to handle a complete name server down.
     * This method is run after all exchanges between domain server and client but
     * before the server is killed (since that is what is running this
     * method).
     */
    public void handleServerShutdown() throws cMsgException {
    }

}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

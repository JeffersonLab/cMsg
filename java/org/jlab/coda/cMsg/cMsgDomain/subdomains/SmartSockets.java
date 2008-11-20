// still to do:
//   cMsgException error codes
//   specify server list


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

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgSubdomainAdapter;
import org.jlab.coda.cMsg.common.cMsgClientInfo;
import org.jlab.coda.cMsg.common.cMsgDeliverMessageInterface;
import org.jlab.coda.cMsg.common.cMsgMessageFull;

import java.util.*;
import java.util.regex.*;
import java.io.IOException;

import com.smartsockets.*;


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * cMsg subdomain handler for smartsockets subdomain.
 *
 * Implements proxy smartsockets server.<p>
 *
 *  UDL:    cMsg:cMsg://host:port/SmartSockets/projectName<p>
 *
 * @author Elliott Wolin
 * @version 1.0
 *
 */
public class SmartSockets extends cMsgSubdomainAdapter {


    /** static collections. */
    private static Map<String,MyInt> subjects    = Collections.synchronizedMap(new HashMap<String,MyInt>(100));
    private static Map<Integer,TipcCb> callbacks = Collections.synchronizedMap(new HashMap<Integer,TipcCb>(100));


    /** client registration info. */
    private cMsgClientInfo myClientInfo;


    /** UDL remainder. */
    private String myUDLRemainder = null;


    /** Object used to deliver messages to the client. */
    private cMsgDeliverMessageInterface myDeliverer;


    /** for smartsockets. */
    private TipcSrv mySrv = null;


    /** misc */
    private boolean done = false;


    /** smartsockets callback delivers message to client. */
    private class ProcessCb implements TipcProcessCb {
        public void process(TipcMsg msg, Object arg) {
            try {
                msg.setCurrent(0);
                cMsgMessageFull cmsg = new cMsgMessageFull();

                cmsg.setDomain("cMsg");                   // just use domain for now
                cmsg.setSysMsgId(msg.getSeqNum());
                cmsg.setSender(msg.getSender());
                cmsg.setSenderHost("unknown");            // no easy way to get this
                cmsg.setSenderTime(new Date((long)msg.getSenderTimestamp()*1000));
                cmsg.setReceiver(myClientInfo.getName());
                cmsg.setReceiverHost(myClientInfo.getClientHost());
                cmsg.setReceiverTime(new Date());
                cmsg.setSenderToken((Integer)arg); // change receiverSubscribeId to senderToken

                cmsg.setSubject(msg.getDest());
                cmsg.setType(msg.getType().getName());
                cmsg.setText(msg.nextStr());
                cmsg.setUserInt(msg.getUserProp());

                myDeliverer.deliverMessage(cmsg, cMsgConstants.msgSubscribeResponse);

            } catch (TipcException e) {
                System.err.println(e);

            } catch (cMsgException e) {
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


    /** For counting subscriptions in subject hash. */
    private class MyInt {
        int count;
    }



//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     * @return true
     */
    public boolean hasSend() {
        return true;
    };


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     * @return true
     */
    public boolean hasSubscribe() {
        return true;
    };


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     * @return true
     */
    public boolean hasUnsubscribe() {
        return true;
    };


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
     * Connect to smartsockets server.
     *
     * @param info contains all client info
     * @throws cMsgException if cannot connect to smartsockets server
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {

        myClientInfo = info;


        // Get an object enabling this handler to communicate
        // with only this client in this cMsg subdomain.
        myDeliverer = info.getDeliverer();


        // extract project from UDL remainder
        String myProject;
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
     * @param msg {@inheritDoc}
     * @throws cMsgException if cannot forward message
     */
    public void handleSendRequest(cMsgMessageFull msg) throws cMsgException {

        String type = msg.getType();
        TipcMt mt;


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
            ssMsg.setUserProp(msg.getUserInt());
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
     * Subscribe to receive smartsockets messages.
     *
     * @param subject {@inheritDoc}
     * @param type {@inheritDoc}
     * @param id {@inheritDoc}
     * @throws cMsgException if cannot subscribe
     */
    public void handleSubscribeRequest(String subject, String type, int id)
                    throws cMsgException {

        TipcMt mt;
        TipcCb cb;
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
        cb=mySrv.addProcessCb(new ProcessCb(),mt,subject,id);
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
            m = subjects.get(subject);
            m.count++;
            subjects.put(subject,m);
        } else {
            m = new MyInt();
            m.count=1;
            subjects.put(subject,m);
        }


        // hash callbacks by id
        callbacks.put(id,cb);

        return;
    }


//-----------------------------------------------------------------------------


    /**
     * Unsubscribe smartsockets subscription.
     *
     * @param subject {@inheritDoc}
     * @param type {@inheritDoc}
     * @param id {@inheritDoc}
     */
    public void handleUnsubscribeRequest(String subject, String type, int id) {
        try {
            if(callbacks.containsKey(id)) {
                mySrv.removeProcessCb(callbacks.get(id));
                callbacks.remove(id);
            }
        } catch (TipcException e) {
            System.err.println("?unable to unsubscribe from subject " + subject);
        }


        // update subject table
        if(subjects.containsKey(subject)) {
            MyInt m = subjects.get(subject);
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
     * Close connection to smartsockets server.
     *
     * @throws cMsgException never
     */
    public void handleClientShutdown() throws cMsgException {
        done=true;
        System.out.println("...shutdown for client " + myClientInfo.getName());
        try {
            mySrv.setOption("ss.server_auto_connect", "false");
            mySrv.destroy(TipcSrv.CONN_NONE);
        } catch (TipcException e) {
            System.err.println(e);
        }
    }


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
}

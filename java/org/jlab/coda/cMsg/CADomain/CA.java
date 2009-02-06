// still to do:
//    other CA datatypes besides double



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

package org.jlab.coda.cMsg.CADomain;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgCallbackInterface;
import org.jlab.coda.cMsg.common.cMsgDomainAdapter;
import org.jlab.coda.cMsg.common.cMsgMessageFull;

import gov.aps.jca.*;
import gov.aps.jca.dbr.DOUBLE;
import gov.aps.jca.dbr.DBR;
import gov.aps.jca.configuration.DefaultConfiguration;
import gov.aps.jca.Monitor;
import gov.aps.jca.event.MonitorListener;
import gov.aps.jca.event.MonitorEvent;
import gov.aps.jca.CAStatus;

import java.util.*;
import java.net.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * This class implements a client in the cMsg Channel Access (CA) domain.<p>
 *
 * UDL:  cMsg:CA://channelName?addr_list=list.<p>
 *
 * where addr_list specifies the UDP broadcast address list.
 *
 * @author Elliott Wolin
 * @version 1.0
 */
public class CA extends cMsgDomainAdapter {


    /** JCALibrary and context. */
    private JCALibrary myJCA  = null;
    private Context myContext = null;


    /** channel info. */
    private String myChannelName = null;
    private Channel myChannel    = null;
    private String myAddrList    = "none";


    /** for subscriptions and monitoring */
    private Monitor myMonitor    = null;
    private ArrayList<SubInfo> mySubList  = new ArrayList<SubInfo>(10);


    /** misc params. */
    private double myContextPend = 3.0;
    private double myPutPend     = 3.0;


    /** for monitoring */
    private class MonitorListenerImpl implements MonitorListener {

        public void monitorChanged(MonitorEvent event) {

            if(event.getStatus()==CAStatus.NORMAL) {

                //  get cMsg and fill common fields
                cMsgMessageFull cmsg = new cMsgMessageFull();
                cmsg.setDomain(domain);
                cmsg.setSender(myChannelName);
                cmsg.setSenderHost(myAddrList);
                cmsg.setSenderTime(new Date());
                cmsg.setReceiver(name);
                cmsg.setReceiverHost(host);
                cmsg.setReceiverTime(new Date());
                cmsg.setText("" + (((DOUBLE)event.getDBR()).getDoubleValue())[0]);


                // dispatch cMsg to all registered callbacks
                synchronized (mySubList) {
                    for(SubInfo s : mySubList) {
                        cmsg.setSubject(s.subject);
                        cmsg.setType(s.type);
                        new Thread(new DispatchCB(s,cmsg)).start();
                    }
                }


            } else {
                System.err.println("Monitor error: " + event.getStatus());
            }
        }
    }

    
    /** for dispatching callbacks */
    private class DispatchCB extends Thread {

        private SubInfo s;
        private cMsgMessage c;

        DispatchCB(SubInfo s, cMsgMessage c) {
            this.s=s;
            this.c=c;
        }

        public void run() {
            s.cb.callback(c,s.userObj);
        }

    }


    /** holds subscription info */
    private class SubInfo implements cMsgSubscriptionHandle {

        String subject;
        String type;
        cMsgCallbackInterface cb;
        Object userObj;

        SubInfo(String s, String t, cMsgCallbackInterface c, Object o) {
            subject = s;
            type    = t;
            cb      = c;
            userObj = o;
        }

        // BUG BUG some of the first 4 following methods need to be implemented, Timmer, 2/6/09

        /**
         * Gets the number of messages in the cue.
         * Not relevant in this domain.
         * @return number of messages in the cue
         */
        public int getCueSize() {return 0;}

        /**
         * Returns true if cue is full.
         * Not relevant in this domain.
         * @return true if cue is full
         */
        public boolean cueIsFull() {return false;}

        /**
         * Clears the cue of all messages.
         * Not relevant in this domain.
         */
        public void clearCue() {return;}

        /**
         * Gets the total number of messages passed to the callback.
         * Not relevant in this domain.
         * @return total number of messages passed to the callback
         */
        public long getMsgCount() {return 0L;}

        /**
         * Gets the domain in which this subscription lives.
         * @return the domain in which this subscription lives
         */
        public String getDomain() {return domain;}

        /**
          * Gets the subject of this subscription.
          * @return the subject of this subscription
          */
        public String getSubject() {return subject;}

        /**
         * Gets the type of this subscription.
         * @return the type of this subscription
         */
        public String getType() {return type;}

        /**
         * Gets the callback object.
         * @return user callback object
         */
        public cMsgCallbackInterface getCallback() {return cb;}

        /**
         * Gets the subscription's user argument.
         * @return subscription's user argument object
         */
        public Object getUserObject() {return userObj;}
    }


//-----------------------------------------------------------------------------


    /** Constructor for CA domain. */
    public CA() {
        domain = "CA";
        try {
            host = InetAddress.getLocalHost().getHostName();
        }
        catch (UnknownHostException e) {
            System.err.println(e);
            host = "unknown";
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Connects to CA channel after parsing UDL.
     *
     * @throws cMsgException if there are communication problems
     */
    synchronized public void connect() throws cMsgException {

        Pattern p;
        Matcher m;
        String remainder = null;

        if (connected) return;


        if(UDLremainder==null) throw new cMsgException("?invalid UDL");


        // get channel name
        int ind = UDLremainder.indexOf('?');
        if (ind > 0) {
            myChannelName = UDLremainder.substring(0,ind);
            remainder     = UDLremainder.substring(ind) + "&";
        } else {
            myChannelName = UDLremainder;
        }


        // get JCA library
        myJCA = JCALibrary.getInstance();


        // parse remainder and set JCA context options
        DefaultConfiguration conf = new DefaultConfiguration("myContext");
        conf.setAttribute("class", JCALibrary.CHANNEL_ACCESS_JAVA);
        conf.setAttribute("auto_addr_list","false");
        if(remainder!=null) {
            p = Pattern.compile("[&\\?]addr_list=(.*?)&", Pattern.CASE_INSENSITIVE);
            m = p.matcher(remainder);
            if(m.find()) {
                myAddrList=m.group(1);
                conf.setAttribute("addr_list",myAddrList);
            }
        }


        // create context
        try {
            myContext = myJCA.createContext(conf);
        } catch (CAException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }


        // create channel object
        try {
            myChannel = myContext.createChannel(myChannelName);
            myContext.pendIO(myContextPend);
        } catch  (CAException e) {
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        } catch  (TimeoutException e) {
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }

        connected = true;
    }


//-----------------------------------------------------------------------------


    /**
     * Disconnects from CA channel.
     *
     * @throws cMsgException if destroying channel failed
     */
    synchronized public void disconnect() throws cMsgException {
        if (!connected) return;

        try {
            connected = false;
            myChannel.destroy();
            myContext.flushIO();
            myContext.destroy();
        } catch (CAException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Set double value of channel from message's text field.
     *
     * @param msg message containing the text form of a double value
     * @throws cMsgException if not connected to or cannot write to channel
     */
    synchronized public void send(cMsgMessage msg) throws cMsgException {
        if (!connected) {
            throw new cMsgException("Not connected, Call \"connect\" first");
        }

        try {
            myChannel.put(Double.parseDouble(msg.getText()));
            myContext.pendIO(myPutPend);
        } catch  (CAException e) {
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        } catch  (TimeoutException e) {
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Complete what the "send" method started -- write the double value to channel
     * for real.
     *
     * @param timeout {@inheritDoc}
     * @throws cMsgException if error in CA flushIO or not connected to channel
     */
    synchronized public void flush(int timeout) throws cMsgException {
        if (!connected) {
            throw new cMsgException("Not connected, Call \"connect\" first");
        }

        try {
            myContext.flushIO();
        } catch (CAException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Get the channel's value and place it in the return message's text field.
     *
     * @param subject subject of return msg
     * @param type    type of return msg
     * @param timeout {@inheritDoc}
     * @return message with new double value in text field
     * @throws cMsgException if not connected to channel or cannot get channel value
     */
    public cMsgMessage subscribeAndGet(String subject, String type, int timeout) throws cMsgException {


        cMsgMessageFull response = new cMsgMessageFull();
        response.setDomain(domain);
        response.setSender(myChannelName);
        response.setSenderHost(myAddrList);
        response.setSenderTime(new Date());
        response.setReceiver(name);
        response.setReceiverHost(host);
        response.setReceiverTime(new Date());
        response.setSubject(subject);
        response.setType(type);

        DBR dbr;

        synchronized (this) {
            if (!connected) {
                throw new cMsgException("Not connected, Call \"connect\" first");
            }
            // get channel data
            try {
                dbr = myChannel.get();
            }
            catch (CAException e) {
                response.setText(null);
                e.printStackTrace();
                return response;
            }
        }

        // Waiting for answer so don't synchronized this
        try {
            myContext.pendIO(timeout/1000.0);
            response.setText("" + (((DOUBLE)dbr).getDoubleValue())[0]);
        } catch  (CAException e) {
            response.setText(null);
            e.printStackTrace();
        } catch  (TimeoutException e) {
            response.setText(null);
            e.printStackTrace();
        }

        return response;
    }


//-----------------------------------------------------------------------------


    /**
     * Does a CA "monitor on" for this channel. Runs the given callback when the channel's
     * value changes. Subject, type, and userObj are passed right through in message/args sent to
     * callback.
     *
     * @param subject {@inheritDoc}
     * @param type    {@inheritDoc}
     * @param cb      {@inheritDoc}
     * @param userObj {@inheritDoc}
     * @return {@inheritDoc}
     * @throws cMsgException if channel not connected or "monitor on" failed
     */
    synchronized public cMsgSubscriptionHandle subscribe(String subject, String type, cMsgCallbackInterface cb, Object userObj)
            throws cMsgException {

        if (!connected) {
            throw new cMsgException("Not connected, Call \"connect\" first");
        }

        SubInfo info = new SubInfo(subject,type,cb,userObj);

        synchronized (mySubList) {
            mySubList.add(info);
        }

        if(myMonitor==null) {
            try {
                myMonitor = myChannel.addMonitor(Monitor.VALUE, new MonitorListenerImpl());
                myContext.flushIO();

            } catch (CAException e) {
                e.printStackTrace();
                cMsgException ce = new cMsgException(e.toString());
                ce.setReturnCode(1);
                throw ce;
            }
        }

        return info;
    }


//-----------------------------------------------------------------------------


    /**
     * Does a CA "monitor off" for this channel.
     *
     * @param obj {@inheritDoc}
     * @throws cMsgException if channel not connected or "monitor off" failed
     */
    synchronized public void unsubscribe(Object obj)
            throws cMsgException {

        if (!connected) {
            throw new cMsgException("Not connected, Call \"connect\" first");
        }

        int cnt;
        SubInfo info = (SubInfo)obj;

        // remove subscrition from list
        synchronized (mySubList) {
            mySubList.remove(info);
            cnt = mySubList.size();
        }


        // clear monitor if no subscriptions left
        if(cnt<=0) {
            try {
                myMonitor.clear();
                myMonitor=null;
            } catch (CAException e) {
                e.printStackTrace();
                cMsgException ce = new cMsgException(e.toString());
                ce.setReturnCode(1);
                throw ce;
            }
        }

    }

}

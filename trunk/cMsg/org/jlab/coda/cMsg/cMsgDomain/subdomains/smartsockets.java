// still to do:
//   isRegistered vs registerClient
//   server shutdown
//   redo all errors, catches, etc.



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

import org.jlab.coda.cMsg.cMsgConstants;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgDomain.cMsgHandleRequests;
import java.util.*;
import java.util.regex.*;

import com.smartsockets.*;

// may not be needed
import java.nio.channels.SocketChannel;
import java.net.Socket;
import java.net.InetSocketAddress;
import java.nio.ByteBuffer;
import java.io.IOException;
import java.io.UnsupportedEncodingException;




/**
 * cMsg subdomain handler for smartsockets subdomain.
 *
 * Implements proxy smartsockets server.
 *
 * @author Elliott Wolin
 * @version 1.0
 *
 */
public class smartsockets implements cMsgHandleRequests {


    /// static collections
    private static Map subjects  = Collections.synchronizedMap(new HashMap(100));
    private static Map callbacks = Collections.synchronizedMap(new HashMap(100));


    /// client registration parameters
    private String myDomain = "mydomain";  // ???
    private String myName;
    private String myHost;
    private int myPort;
    private SocketChannel myChannel = null;


    // may not be needed
    /** A direct buffer is necessary for nio socket IO. */
    private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);


    /// UDL remainder
    private String myUDLRemainder = null;
    

    /// for smartsockets
    private TipcSrv mySrv     = null;
    private String myProject;
    private boolean done      = false;


    /// smartsockets callback delivers message to client
    private class ProcessCb implements TipcProcessCb {
	public void process(TipcMsg msg, Object arg) { 	
	    try {
		cMsgMessage cmsg = new cMsgMessage();
		msg.setCurrent(0);
		cmsg.setDomain(myDomain);
		cmsg.setSysMsgId(msg.getMessageId());
		cmsg.setSender(msg.getSender());
		cmsg.setSenderHost("unknown");
		cmsg.setSenderTime(msg.get());
		cmsg.setSenderId(msg.get());
		cmsg.setSenderMsgId(msg.get());
		cmsg.setSenderToken(0);
		cmsg.setReceiver(myName);
		cmsg.setReceiverHost(myHost);
		cmsg.setReceiverTime(new Date());
		cmsg.setReceiverSubscribeId(((Integer)arg).intValue());
		cmsg.setSubject(msg.getDest());
		cmsg.setType(msg.getType().getName());
		cmsg.setText(msg.nextStr());
		System.out.println("...delivering msg: \n" + cmsg);
		deliverMessage(myChannel,((Integer)arg).intValue(),cMsgConstants.msgSubscribeResponse,cmsg);

	    } catch (Exception e) {
		System.out.println(e);
	    }
	}
    }


    /// for smartsockets main loop
    private class MainLoop extends Thread {
	public void run() {
 	    try {
		while(!done) {
		    mySrv.mainLoop(0.5);
 		}
	    } catch (Exception e) {
		System.out.println(e);
	    }
	}
    }


    // object counts subject subscriptions
    private class MyInt {
	int count;
    }



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


    /**
     * Method to tell if the "get" cMsg API function is implemented
     * by this interface implementation in the {@link #handleGetRequest}
     * method.
     *
     * @return true if get implemented in {@link #handleGetRequest}
     */
    public boolean hasGet() {
        return false;
    };


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
    

    /**
     * Method to see if domain client is registered.
     *
     * @param name name of client
     * @return true if client registered, false otherwise
     */
    public boolean isRegistered(String name) {

	myName=name;

	// must attempt to register to see if name is unique
	// extract ss params from UDL
	try {
	    if(myUDLRemainder.indexOf("?")>0) {
		Pattern p = Pattern.compile("^(.+?)(\\?)(.*)$");
		Matcher m = p.matcher(myUDLRemainder);
		m.find();
		myProject   = m.group(1);
	    } else {
		myProject   = myUDLRemainder;
	    }
	} catch (Exception e) {
	    e.printStackTrace();
	    return(false);
	}


	// connect to server
	try {
	    mySrv=TipcSvc.createSrv();
	    mySrv.setOption("ss.project",         myProject);
	    mySrv.setOption("ss.unique_subject",  myName);
	    return(!mySrv.create());

	} catch (Exception e) {
	    e.printStackTrace();
	    return(false);
	}
    }


    /**
     * Method to register domain client.
     *
     * @param name name of client
     * @param host host client is running on
     * @param port port client is listening on
     * @throws cMsgException upon error
     */
    public void registerClient(String name, String host, int port) throws cMsgException {
	myHost=host;
	myPort=port;

	// may not be needed...
	try {
	    myChannel = SocketChannel.open(new InetSocketAddress(myHost,myPort));
	    Socket socket = myChannel.socket();
	    socket.setTcpNoDelay(true);
	    socket.setReceiveBufferSize(65535);
	    socket.setSendBufferSize(65535);
	} catch (Exception e) {
	}


	// launch server main loop
	(new MainLoop()).start();

	return;
    }
    


    /**
     * Forwards message to smartsockets system.
     *
     * @param msg message from sender.
     * @throws cMsgException
     */
    public void handleSendRequest(cMsgMessage msg) throws cMsgException {

	String type = msg.getType();
	TipcMt mt   = null;


	// form ss message type
	if(type.matches("\\d+")) {
	    mt=TipcSvc.lookupMt(Integer.parseInt(type));
	    if(mt==null) {
		try {
		    mt=TipcSvc.createMt(type,Integer.parseInt(type),"verbose");
		} catch (Exception e) {
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
	

	// create, fill, send, and flush message
	TipcMsg ssMsg = TipcSvc.createMsg(mt);
	ssMsg.setDest(msg.getSubject()); 
	ssMsg.appendStr(msg.getText()); 
	try { 
	    mySrv.send(ssMsg); 
	    mySrv.flush(); 
	} catch (Exception e) { 
	    e.printStackTrace();
	    cMsgException ce = new cMsgException(e.getMessage());
	    ce.setReturnCode(1);
	    throw ce;
	}

    }
	

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
		} catch (Exception e) {
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


	// tell SS server about new subscription
	try {
	    mySrv.setSubjectSubscribe(subject, true);
	} catch (Exception e) {
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


	// create and register callback
	cb=mySrv.addProcessCb(new ProcessCb(),mt,subject,new Integer(receiverSubscribeId));
	if(cb==null) {
	    cMsgException ce = new cMsgException("?unable to create callback");
	    ce.setReturnCode(1);
	    throw ce;
	}


	// hash callbacks by id
	callbacks.put(new Integer(receiverSubscribeId),cb);

	return;
    }


    /**
     * Method to handle sunsubscribe request sent by domain client.
     * This method is run after all exchanges between domain server and client.
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     */
    public void handleUnsubscribeRequest(String subject, String type) {
	try {
	    //	    Integer I = new Integer(receiverSubscribeId);  ???
	    Integer I = new Integer(1);
	    if(callbacks.containsKey(I)) {
		mySrv.removeProcessCb((TipcCb)callbacks.get(I));
		callbacks.remove(I);
	    }		
	} catch (Exception e) {
	    System.out.println("?unable to unsubscribe from subject " + subject);
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
		} catch (Exception e) {
		    System.out.println("?unable to unsubscribe from subject " + subject);
		}
	    }
	}
    }


    /**
     * Method to synchronously get a single message from the server for a given
     * subject and type -- perhaps from a specified receiver.
     *
     * @param message message requesting what sort of message to get
     */
    public void handleGetRequest(cMsgMessage message) {
        // do nothing...
    }


    /**
     * Method to handle unget request sent by domain client (hidden from user).
     *
     * @param subject message subject subscribed to
     * @param type message type subscribed to
     */
    public void handleUngetRequest(String subject, String type) {
        // do nothing
    }


    /**
     * Method to handle keepalive sent by domain client checking to see
     * if the domain server socket is still up. Normally nothing needs to
     * be done as the domain server simply returns an "OK" to all keepalives.
     * This method is run after all exchanges between domain server and client.
     */
    public void handleKeepAlive() {
        // do nothing...
    }


    /**
     * Method to handle a client shutdown.
     *
     * @throws cMsgException
     */
    public void handleClientShutdown() throws cMsgException {
	done=true;
	try {
	    mySrv.destroy(TipcSrv.CONN_NONE);
	} catch (Exception e) {
	}
    }


    /**
     * Method to handle a complete name server down.
     * This method is run after all exchanges between domain server and client but
     * before the server is killed (since that is what is running this
     * method).
     */
    public void handleServerShutdown() throws cMsgException {
    }



// may not be needed -----------------------------------
    /**
     * Method to deliver a message to a client that is
     * subscribed to the message's subject and type.
     *
     * @param channel communication channel to client
     * @param id message id refering to message's subject and type
     * @param msgType type of communication with the client
     * @param msg message to be sent
     * @throws IOException if the message cannot be sent over the channel
     *                          or client returns an error
     */
    private void deliverMessage(SocketChannel channel, int id, int msgType, cMsgMessage msg) throws IOException {
        // get ready to write
        buffer.clear();

        // write 14 ints
        int outGoing[] = new int[14];
        outGoing[0]  = msgType;
        outGoing[1]  = msg.getSysMsgId();
        outGoing[2]  = msg.isGetRequest()  ? 1 : 0;
        outGoing[3]  = msg.isGetResponse() ? 1 : 0;
        outGoing[4]  = id;  // receiverSubscribeId, 0 for specific gets
        outGoing[5]  = msg.getSenderId();
        outGoing[6]  = (int) (msg.getSenderTime().getTime()/1000L);
        outGoing[7]  = msg.getSenderMsgId();
        outGoing[8]  = msg.getSenderToken();
        outGoing[9]  = msg.getSender().length();
        outGoing[10] = msg.getSenderHost().length();
        outGoing[11] = msg.getSubject().length();
        outGoing[12] = msg.getType().length();
        outGoing[13] = msg.getText().length();

        // send ints over together using view buffer
        buffer.asIntBuffer().put(outGoing);

        // position original buffer at position of view buffer
        buffer.position(56);

        // write strings
        try {
            buffer.put(msg.getSender().getBytes("US-ASCII"));
            buffer.put(msg.getSenderHost().getBytes("US-ASCII"));
            buffer.put(msg.getSubject().getBytes("US-ASCII"));
            buffer.put(msg.getType().getBytes("US-ASCII"));
            buffer.put(msg.getText().getBytes("US-ASCII"));
        }
        catch (UnsupportedEncodingException e) {
            e.printStackTrace();
        }

        // send buffer over the socket
        buffer.flip();
        while (buffer.hasRemaining()) {
            channel.write(buffer);
        }

        return;
    }

}

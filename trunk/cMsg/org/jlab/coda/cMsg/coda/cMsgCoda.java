package org.jlab.coda.cMsg.coda;

import org.jlab.coda.cMsg.*;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Jul 14, 2004
 * Time: 11:27:05 AM
 * To change this template use File | Settings | File Templates.
 */
public class cMsgCoda extends cMsgAdapter {


//-----------------------------------------------------------------------------


     public cMsgCoda(String myDomain, String myName, String myDescription)
                                                        throws cMsgException {
	     super(myDomain, myName, myDescription);
     }


//-----------------------------------------------------------------------------


    public int send(String subject, String type, String text) {
        return cMsgConstants.ok;
    }


//-----------------------------------------------------------------------------


    public int flush() {
        return cMsgConstants.ok;
    }


//-----------------------------------------------------------------------------


    public int get(String subject, String type, int timeout, cMsgMessage msg) {
        return cMsgConstants.ok;
    }


//-----------------------------------------------------------------------------


    public int subscribe(String subject, String type, cMsgCallback cb, Object userObj) {
        return cMsgConstants.ok;
    }


//-----------------------------------------------------------------------------


    public int subscribe(String subject, String type, cMsgCallback cb) {
        return cMsgConstants.ok;
    }


//-----------------------------------------------------------------------------


    public int done() {
        return cMsgConstants.ok;
    }


//-----------------------------------------------------------------------------


    public int start() {
	   receiveState = true;
       return cMsgConstants.ok;
    }


//-----------------------------------------------------------------------------


    public int stop() {
        receiveState = false;
        return cMsgConstants.ok;
    }
}


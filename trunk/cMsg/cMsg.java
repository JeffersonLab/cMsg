//  cMsg.java
//
//
//   *----------------------------------------------------------------------------*
//   *  Copyright (c) 2004        Southeastern Universities Research Association, *
//   *                            Thomas Jefferson National Accelerator Facility  *
//   *                                                                            *
//   *    This software was developed under a United States Government license    *
//   *    described in the NOTICE file included as part of this distribution.     *
//   *                                                                            *
//   *    E.Wolin, 29-Jun-2004, Jefferson Lab                                     *
//   *                                                                            *
//   *    Authors: Elliott Wolin                                                  *
//   *             wolin@jlab.org                    Jefferson Lab, MS-6B         *
//   *             Phone: (757) 269-7365             12000 Jefferson Ave.         *
//   *             Fax:   (757) 269-5800             Newport News, VA 23606       *
//   *                                                                            *
//   *             Carl Timmer                                                    *
//   *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
//   *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
//   *             Fax:   (757) 269-5800             Newport News, VA 23606       *
//   *                                                                            *
//   *----------------------------------------------------------------------------*
//
//
//  implements cMsg API for CODA domain
//
//
//  still to do:
//



import java.io.*;
import java.lang.*;



public class cMsg {


    // instance variables (is private needed?)
    private String initialized   = false;
    private String domain        = null;
    private String name          = null;
    private String description   = null;
    private int sendSocket       = 0;
    private int receiveSocket    = 0;
    private int pendThread       = 0;
    private String host          = null;
    private bool receiveState    = false;



//-----------------------------------------------------------------------------


     public int cMsg(String myDomain, String myName, String myDescription) {

	 if(domain???) {
	     domain=myDomain;
	     name=myName;
	     description=myDescription;

	     sendSocket=;
	     receiveSocket=;
	     pendThread=;
	     host=;

	 } else {
	     return(CMSG_NOT_IMPLEMENTED);
	 }
	 
	 initialized=true;
	 return(CMSG_OK);
    }


//-----------------------------------------------------------------------------


     public int cMsgSend(String subject, String type, String text) {
    }


//-----------------------------------------------------------------------------


     public int cMsgFlush(void) {
    }


//-----------------------------------------------------------------------------


     public int cMsgGet(String subject, String type, int timeout, cMsgMessage msg) {
    }


//-----------------------------------------------------------------------------


     public int cMsgSubscribe(String subject, String type, cMsgCallback cb, Object userObj) {
    }


//-----------------------------------------------------------------------------


     public int cMsgSubscribe(String subject, String type, cMsgCallback cb) {
    }


//-----------------------------------------------------------------------------


     public int cMsgStart(void) {
	 receiveState=true;
    }


//-----------------------------------------------------------------------------


     public int cMsgStop(void) {
	 receiveState=false;
    }


//-----------------------------------------------------------------------------


     public String cMsgGetDomain() {
	 return(domain);
    }


//-----------------------------------------------------------------------------


     public String cMsgGetName() {
	 return(name);
    }


//-----------------------------------------------------------------------------


     public String cMsgGetDescription() {
	 return(description);
    }


//-----------------------------------------------------------------------------


     public String cMsgGetHost() {
	 return(host);
     }


//-----------------------------------------------------------------------------


     public int cMsgGetSendSocket() {
	 return(sendSocket);
    }


//-----------------------------------------------------------------------------


     public int cMsgGetReceiveSocket() {
	 return(receiveSocket);
    }


//-----------------------------------------------------------------------------


     public int cMsgGetPendThread() {
	 return(pendThread);
    }


//-----------------------------------------------------------------------------


     public bool cMsgGetReceiveState() {
	 return(receiveState;
    }


//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
}        //  end class definition:  cMsg
//-----------------------------------------------------------------------------

package org.jlab.coda.cMsg;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Jul 14, 2004
 * Time: 11:35:31 AM
 * To change this template use File | Settings | File Templates.
 */
public class cMsgImpl implements cMsg {
    // instance variables (is private needed?)
    protected boolean initialized  = false;
    protected String domain        = null;
    protected String name          = null;
    protected String description   = null;
    protected int sendSocket       = 0;
    protected int receiveSocket    = 0;
    protected int pendThread       = 0;
    protected String host          = null;
    protected boolean receiveState = false;



//-----------------------------------------------------------------------------


    public cMsgImpl() throws cMsgException {
        this(null, null, null);
    }


//-----------------------------------------------------------------------------


    public cMsgImpl(String myDomain, String myName, String myDescription) throws cMsgException {

        if (domain.equalsIgnoreCase("CODA")) {
            domain = myDomain;
            name = myName;
            description = myDescription;
        }
        else {
            throw new cMsgException("cMsg is not implemented yet");
        }

        initialized = true;
    }


//-----------------------------------------------------------------------------


    public int send(String subject, String type, String text) {
        return cMsgConstants.errorNotImplemented;
    }


//-----------------------------------------------------------------------------


    public int flush() {
        return cMsgConstants.errorNotImplemented;
    }


//-----------------------------------------------------------------------------


    public int get(String subject, String type, int timeout, cMsgMessage msg) {
        return cMsgConstants.errorNotImplemented;
    }


//-----------------------------------------------------------------------------


    public int subscribe(String subject, String type, cMsgCallback cb, Object userObj) {
        return cMsgConstants.errorNotImplemented;
    }


//-----------------------------------------------------------------------------


    public int subscribe(String subject, String type, cMsgCallback cb) {
        return cMsgConstants.errorNotImplemented;
    }


//-----------------------------------------------------------------------------


    public int done() {
        return cMsgConstants.errorNotImplemented;
    }


//-----------------------------------------------------------------------------


    public int start() {
	 receiveState=true;
        return cMsgConstants.errorNotImplemented;
    }


//-----------------------------------------------------------------------------


    public int stop() {
        receiveState=false;
        return cMsgConstants.errorNotImplemented;
    }


//-----------------------------------------------------------------------------
    public String getDomain() {return(domain);}
    public void setDomain(String s) {domain = s;}


//-----------------------------------------------------------------------------


    public String getName() {return(name);}
    public void setName(String s) {name = s;}


//-----------------------------------------------------------------------------


    public String getDescription() {return(description);}
    public void setDescription(String s) {description = s;}


//-----------------------------------------------------------------------------


    public String getHost() {return(host);}


//-----------------------------------------------------------------------------


    public int getSendSocket() {return(sendSocket);}


//-----------------------------------------------------------------------------


    public int getReceiveSocket() {return(receiveSocket);}


//-----------------------------------------------------------------------------


    public int getPendThread() {return(pendThread);}


//-----------------------------------------------------------------------------


    public boolean getReceiveState() {return(receiveState);}


}

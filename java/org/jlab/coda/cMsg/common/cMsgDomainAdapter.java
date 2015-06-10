/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 14-Jul-2004, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.common;

import org.jlab.coda.cMsg.*;

import java.util.concurrent.TimeoutException;

/**
 * This class provides a very basic (non-functional, dummy) implementation
 * of the cMsgDomainInterface interface. Its non-getter/setter methods throw a
 * cMsgException saying that the method is not implemented. It is like the
 * swing Adapter classes.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgDomainAdapter implements cMsgDomainInterface {

    /** Boolean indicating whether this client is connected to the domain or not. */
    protected volatile boolean connected;

    /** Boolean indicating whether this client's callbacks are active or not. */
    protected volatile boolean receiving;

    /**
     * The Uniform Domain Locator which tells the location of a domain. It is of the
     * form cMsg:&lt;domainType&gt;://&lt;domain dependent remainder&gt;
     */
    protected String  UDL;

    /** String containing the remainder part of the UDL. */
    protected String  UDLremainder;

    /** Domain being connected to. */
    protected String  domain;

    /** Name of this client. */
    protected String  name;

    /** Description of the client. */
    protected String  description;

    /** Host the client is running on. */
    protected String  host;

    /** Handler for client shutdown command sent by server. */
    protected cMsgShutdownHandlerInterface shutdownHandler;

    /** Level of debug output for this class. */
    protected int debug = cMsgConstants.debugNone;
//-----------------------------------------------------------------------------


    /** Constructor which gives a default shutdown handler to this client. */
    public cMsgDomainAdapter() {
        setShutdownHandler(new cMsgShutdownHandlerDefault());
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @return {@inheritDoc}
     */
    public boolean isConnected() {
        return connected;
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @throws org.jlab.coda.cMsg.cMsgException always throws an exception since this is a dummy implementation
     */
    public void connect() throws cMsgException {
        throw new cMsgException("connect is not implemented");
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void disconnect() throws cMsgException {
        throw new cMsgException("disconnect is not implemented");
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @param message {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void send(cMsgMessage message) throws cMsgException {
        throw new cMsgException("send is not implemented");
    }


//-----------------------------------------------------------------------------

    /**
     * {@inheritDoc}
     *
     * @param message {@inheritDoc}
     * @param timeout {@inheritDoc}
     * @return {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public int syncSend(cMsgMessage message, int timeout) throws cMsgException {
        throw new cMsgException("syncSend is not implemented");
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @param timeout {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void flush(int timeout) throws cMsgException {
        throw new cMsgException("flush is not implemented");
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @param subject {@inheritDoc}
     * @param type {@inheritDoc}
     * @param timeout {@inheritDoc}
     * @return {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     * @throws TimeoutException {@inheritDoc}
     */
    public cMsgMessage subscribeAndGet(String subject, String type, int timeout)
            throws cMsgException, TimeoutException {
        throw new cMsgException("subscribeAndGet is not implemented");
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @param message {@inheritDoc}
     * @param timeout {@inheritDoc}
     * @return {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     * @throws TimeoutException {@inheritDoc}
     */
    public cMsgMessage sendAndGet(cMsgMessage message, int timeout)
            throws cMsgException, TimeoutException {
        throw new cMsgException("sendAndGet is not implemented");
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @param subject {@inheritDoc}
     * @param type    {@inheritDoc}
     * @param cb      {@inheritDoc}
     * @param userObj {@inheritDoc}
     * @return {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public cMsgSubscriptionHandle subscribe(String subject, String type, cMsgCallbackInterface cb, Object userObj)
            throws cMsgException {
        throw new cMsgException("subscribe is not implemented");
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @param handle {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void unsubscribe(cMsgSubscriptionHandle handle)
            throws cMsgException {
        throw new cMsgException("unsubscribe is not implemented");
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @param  command {@inheritDoc}
     * @return {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public cMsgMessage monitor(String command)
            throws cMsgException {
        throw new cMsgException("monitor is not implemented");
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public void start() {
	    receiving = true;
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public void stop() {
        receiving = false;
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @param client {@inheritDoc}
     * @param includeMe {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void shutdownClients(String client, boolean includeMe) throws cMsgException {
        throw new cMsgException("shutdownClients is not implemented");
    }


    /**
     * {@inheritDoc}
     *
     * @param server {@inheritDoc}
     * @param includeMyServer  {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void shutdownServers(String server, boolean includeMyServer) throws cMsgException {
        throw new cMsgException("shutdownServers is not implemented");
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public void setShutdownHandler(cMsgShutdownHandlerInterface handler) {
        shutdownHandler = handler;
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public cMsgShutdownHandlerInterface getShutdownHandler() {
        return shutdownHandler;
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public String getDomain() {return(domain);}


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public String getName() {return(name);}


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public void setName(String name) {this.name = name;}


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public String getDescription() {return(description);}


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public void setDescription(String description) {this.description = description;}


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public String getUDLRemainder() {return(UDLremainder);}


//-----------------------------------------------------------------------------


   /**
     * {@inheritDoc}
     */
    public void setUDLRemainder(String UDLremainder) {this.UDLremainder = UDLremainder;}


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public String getUDL() {return(UDL);}


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     * @param UDL {@inheritDoc}
     * @throws cMsgException never in this adapter
     */
    public void setUDL(String UDL) throws cMsgException {this.UDL = UDL;}


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public String getCurrentUDL() {return(UDL);}


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public String getHost() {return(host);}


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public String getInfo(String s) {return null;}

//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     */
    public boolean isReceiving() {return receiving;}


//-----------------------------------------------------------------------------

    
    /**
     * {@inheritDoc}
     */
    public void setDebug(int debug) {this.debug = debug;}


    /**
     * {@inheritDoc}
     */
    public int getDebug() {return debug;}

}

/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 10-Nov-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.common;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.common.cMsgMessageFull;


/**
 * <p>This class provides a very basic (non-functional/dummy) implementation
 * of the cMsgSubdomainInterface interface. This class is used
 * by a cMsg domain server to respond to client demands. It contains
 * some methods that hide the details of communication
 * with the client. A fully implementated subclass of this
 * class must handle all communication with a particular subdomain
 * (such as SmartSockets or JADE agents).
 * </p>
 * Understand that each client using cMsg will have its own handler object
 * from either an implemenation of the cMsgSubdomainInterface interface or a
 * subclass of this class. One client may concurrently use the same
 * cMsgHandleRequest object; thus, implementations must be thread-safe.
 * Furthermore, when the name server shuts dowm, the method handleServerShutdown
 * may be executed more than once for the same reason.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgSubdomainAdapter implements cMsgSubdomainInterface {
    /**
     * {@inheritDoc}
     *
     * @param UDLRemainder {@inheritDoc}
     * @throws org.jlab.coda.cMsg.cMsgException always throws an exception since this is a dummy implementation
     *
     */
    public void setUDLRemainder(String UDLRemainder) throws cMsgException {
        throw new cMsgException("setUDLRemainder is not implemented");
    }



    /**
     * {@inheritDoc}
     *
     * @param info {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {
        throw new cMsgException("registerClient is not implemented");
    }


    /**
     * {@inheritDoc}
     *
     * @param message {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void handleSendRequest(cMsgMessageFull message) throws cMsgException {
        throw new cMsgException("handleSendRequest is not implemented");
    }


    /**
     * {@inheritDoc}
     *
     * @param message {@inheritDoc}
     * @return {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public int handleSyncSendRequest(cMsgMessageFull message) throws cMsgException {
        throw new cMsgException("handleSyncSendRequest is not implemented");
    }


    /**
     * {@inheritDoc}
     *
     * @param subject {@inheritDoc}
     * @param type    {@inheritDoc}
     * @param id      {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void handleSubscribeAndGetRequest(String subject, String type, int id)
            throws cMsgException {
        throw new cMsgException("handleSubscribeAndGetRequest is not implemented");
    }


    /**
     * {@inheritDoc}
     *
     * @param message {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void handleSendAndGetRequest(cMsgMessageFull message) throws cMsgException {
        throw new cMsgException("handleSendAndGetRequest is not implemented");
    }


    /**
     * {@inheritDoc}
     *
     * @param id {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void handleUnSendAndGetRequest(int id) throws cMsgException {
        throw new cMsgException("handleUnSendAndGetRequest is not implemented");
    }


    /**
     * {@inheritDoc}
     *
     * @param subject {@inheritDoc}
     * @param type    {@inheritDoc}
     * @param id      {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void handleUnsubscribeAndGetRequest(String subject, String type, int id)
            throws cMsgException {
        throw new cMsgException("handleUnSubscribeAndGetRequest is not implemented");
    }


    /**
     * {@inheritDoc}
     *
     * @param subject  {@inheritDoc}
     * @param type     {@inheritDoc}
     * @param id       {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void handleSubscribeRequest(String subject, String type, int id)
            throws cMsgException {
        throw new cMsgException("handleSubscribeRequest is not implemented");
    }

    /**
     * {@inheritDoc}
     *
     * @param subject  {@inheritDoc}
     * @param type     {@inheritDoc}
     * @param id       {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void handleUnsubscribeRequest(String subject, String type, int id)
            throws cMsgException {
        throw new cMsgException("handleUnsubscribeRequest is not implemented");
    }


    /**
     * {@inheritDoc}
     *
     * @param client     {@inheritDoc}
     * @param includeMe  {@inheritDoc}
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void handleShutdownClientsRequest(String client, boolean includeMe)
            throws cMsgException {
        throw new cMsgException("handleShutdownClientsRequest is not implemented");
    }


    /**
     * {@inheritDoc}
     *
     * @throws cMsgException always throws an exception since this is a dummy implementation
     */
    public void handleClientShutdown() throws cMsgException {
    }


    /**
     * {@inheritDoc}
     *
     * @return {@inheritDoc}
     */
    public boolean hasSend() {
        return false;
    }


    /**
     * {@inheritDoc}
     *
     * @return {@inheritDoc}
     */
    public boolean hasSyncSend() {
        return false;
    }


    /**
     * {@inheritDoc}
     *
     * @return {@inheritDoc}
     */
    public boolean hasSubscribeAndGet() {
        return false;
    }


    /**
     * {@inheritDoc}
     *
     * @return {@inheritDoc}
     */
    public boolean hasSendAndGet() {
        return false;
    }


    /**
     * {@inheritDoc}
     *
     * @return {@inheritDoc}
     */
    public boolean hasSubscribe() {
        return false;
    }


    /**
     * {@inheritDoc}
     *
     * @return {@inheritDoc}
     */
    public boolean hasUnsubscribe() {
        return false;
    }


    /**
     * {@inheritDoc}
     *
     * @return {@inheritDoc}
     */
    public boolean hasShutdown() {
        return false;
    }



}

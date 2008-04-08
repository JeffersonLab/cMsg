/*----------------------------------------------------------------------------*
 *  Copyright (c) 2008        Jefferson Science Associates,                   *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *     C. Timmer, 1-apr-2008                                                  *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.subdomains;

import org.jlab.coda.cMsg.*;


/**
 *
 */
public class Et extends cMsgSubdomainAdapter {
    /** UDL remainder for this subdomain handler. */
    private String myUDLRemainder;




    /**
     * Method to tell if the "send" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSendRequest}
     * method.
     *
     * @return true
     */
    public boolean hasSend() {
        return true;
    };



    /**
     * Method to tell if the "syncSend" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSyncSendRequest}
     * method.
     *
     * @return true
     */
    public boolean hasSyncSend() {
        return true;
    };


    /**
     * Method to give the subdomain handler the appropriate part
     * of the UDL the client used to talk to the domain server.
     *
     * @param UDLRemainder last part of the UDL appropriate to the subdomain handler
     * @throws org.jlab.coda.cMsg.cMsgException
     */
    public void setUDLRemainder(String UDLRemainder) throws cMsgException {
        myUDLRemainder=UDLRemainder;
        System.out.println("UDL remainder = " + myUDLRemainder);

    }


    /**
     * Method to register domain client.
     *
     * @param info information about client
     * @throws cMsgException upon error
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {
        System.out.println("Registering client");
    }


    /**
     * Executes sql insert or update statement from message payload.
     *
     * @param msg message from sender.
     * @throws cMsgException
     */
    synchronized public void handleSendRequest(cMsgMessageFull msg) throws cMsgException {
        System.out.println("Putting new event into ET system");
        try {
            Thread.sleep(500);
        }
        catch (InterruptedException e) {
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
    public int handleSyncSendRequest(cMsgMessageFull msg) throws cMsgException {
        handleSendRequest(msg);
        return (0);
    }


    /**
     * Method to handle a client shutdown.
     *
     * @throws cMsgException
     */
    synchronized public void handleClientShutdown() throws cMsgException {
        System.out.println("Shutting down client");
    }

//-----------------------------------------------------------------------------

    /**
     * Method to give the subdomain handler on object able to deliver messages
     * to the client.
     *
     * @param deliverer object able to deliver messages to the client
     * @throws cMsgException
     */
    public void setMessageDeliverer(cMsgDeliverMessageInterface deliverer) throws cMsgException {
        // don't need a deliverer for this domain
        System.out.println("DONT NEED DELIVERER !!!!!");
    }



}

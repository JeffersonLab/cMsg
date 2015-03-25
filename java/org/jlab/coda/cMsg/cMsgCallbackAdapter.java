/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 15-Oct-2004, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;


/**
 * This class is an adapter which implements the cMsgCallbackInterface.
 * It implements the methods so extending this adapter is simpler than
 * implementing the full interface.
 */
public class cMsgCallbackAdapter implements cMsgCallbackInterface {

    /**
     * {@inheritDoc}
     *
     * @param msg {@inheritDoc}
     * @param userObject {@inheritDoc}
     */
    public void callback(cMsgMessage msg, Object userObject) {
        System.out.println("Called adapter's default callback, did not properly extend cMsgCallbackAdapter class!!!");
        return;
    }

    /**
     * {@inheritDoc}
     * @return {@inheritDoc}
     */
    public boolean maySkipMessages() { return false; }

    /**
     * {@inheritDoc}
     * @return {@inheritDoc}
     */
    public boolean mustSerializeMessages() { return true; }

    /**
     * {@inheritDoc}
     * @return {@inheritDoc}
     */
    public int getMaximumQueueSize() { return 1000; }

    /**
     * {@inheritDoc}
     * @return {@inheritDoc}
     */
    public int getSkipSize() { return 200; }

    /**
     * {@inheritDoc}
     * @return {@inheritDoc}
     */
    public int getMaximumThreads() { return 100; }

    /**
     * {@inheritDoc}
     * @return {@inheritDoc}
     */
    public int getMessagesPerThread() { return 50; }

}

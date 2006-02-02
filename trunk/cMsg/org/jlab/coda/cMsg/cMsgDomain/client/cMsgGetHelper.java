/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 2-Jan-2006, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.client;

import org.jlab.coda.cMsg.cMsgMessageFull;

/**
 * This class is used to help in implementing a client's {@link cMsg#subscribeAndGet}
 * and {@link cMsg#sendAndGet} methods.
 * An object of this class stores a msg from the server to the method's caller and
 * is used to synchronize/wait/notify on. It also indicates whether the call timed
 * out or not.
 */
public class cMsgGetHelper {
    /**  Message object. */
    cMsgMessageFull message;

    /** Has the "subscribeAndGet" or "sendAndGet" call timed out? */
    boolean timedOut = true;

    /** Subject. */
    String subject;

    /** Type. */
    String type;

    /** Constructor used in sendndGet. */
    public cMsgGetHelper() {
    }

    /**
     * Constructor used in subscribeAndGet.
     * @param subject subject of subscription
     * @param type type of subscription
     */
    public cMsgGetHelper(String subject, String type) {
        this.subject = subject;
        this.type = type;
    }
}

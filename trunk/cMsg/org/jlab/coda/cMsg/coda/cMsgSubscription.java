/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 7-Jul-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.coda;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Aug 11, 2004
 * Time: 11:24:58 AM
 * To change this template use File | Settings | File Templates.
 */
public class cMsgSubscription {
    /** Subject subscribed to. */
    String subject;
    /** Type subscribed to. */
    String type;
    /**
     * Id which eliminates the need to parse subject and type
     * strings upon client's receipt of a message.
     */
    int id;

    cMsgSubscription(String subject, String type, int id) {
        this.subject = subject;
        this.type = type;
        this.id = id;
    }
}

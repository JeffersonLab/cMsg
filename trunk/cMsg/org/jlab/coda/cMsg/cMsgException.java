/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Author:  Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12H        *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;

/**
 * Signals that an error occurred while attempting to execute a cMsg method.
 *
 * @author Carl Timmer
 */
public class cMsgException extends Exception {

    /**
     * Constructs a new exception with the specified message.
     * @param msg the error message
     */
    public cMsgException(String msg) {
        super(msg);
    }

    /** Constructs a new exception with no message. */
     public cMsgException() {
    }
}


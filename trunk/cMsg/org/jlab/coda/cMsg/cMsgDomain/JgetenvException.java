/*---------------------------------------------------------------------------*
 *  Copyright (c) 2003        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    V. Gyurjyan, 12-Jan-2004, Jefferson Lab                                 *
 *                                                                            *
 *    Author:  Vardan Gyurjyan                                                *
 *             gurjyan@jlab.org                  Jefferson Lab, MS-12H        *
 *             Phone: (757) 269-5879             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain;

/**
 * Signals that an error occurred while attempting
 * to read the enviromental variables from a
 * UNIX environment.
 * @author Vardan Gyurjyan
 */
public class JgetenvException extends Exception {

    /**
     * Constructs a new exception with the specified message.
     * @param msg the error message
     */
    public JgetenvException(String msg) {
        super(msg);
    }

    /** Constructs a new exception with no message. */
    public JgetenvException() {
    }
}


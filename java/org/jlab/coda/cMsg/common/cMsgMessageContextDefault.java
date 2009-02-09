/*----------------------------------------------------------------------------*
 *  Copyright (c) 2006        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 17-Nov-2004, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.common;

/**
 * The class defines the default context that a message starts with when it's created.
 */
public class cMsgMessageContextDefault implements cMsgMessageContextInterface {

    /**
     * Gets the domain this callback is running in.
     * @return domain this callback is running in.
     */
    public String getDomain() {return null;}


    /**
     * Gets the subject of this callback's subscription.
     * @return subject of this callback's subscription.
     */
    public String getSubject() {return null;}


    /**
     * Gets the type of this callback's subscription.
     * @return type of this callback's subscription.
     */
    public String getType() {return null;}


    /**
     * Gets the value of this callback's queue size.
     * @return value of this callback's queue size, -1 if no info available
     */
    public int getQueueSize() {return -1;}

}

/*----------------------------------------------------------------------------*
 *  Copyright (c) 2005        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 21-Jun-2005, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.server;

import org.jlab.coda.cMsg.cMsgDomain.cMsgSubscription;

/**
 * This class is a place to store a subscription object along with how
 * many clients in the cMsg subdomain of a cMsg server are subscribed
 * to that subject and type. This is used when a bridge subscribes to
 * clients' subjects and types with another cMsg server. Since only 1
 * subscription to a particular subject and type is allowed per client
 * in the cMsg subdomain, this class helps in admininstering that.
 */
public class cMsgSubscriptionHolder {
    cMsgSubscription sub;
    int numberOfSubscribers;
}

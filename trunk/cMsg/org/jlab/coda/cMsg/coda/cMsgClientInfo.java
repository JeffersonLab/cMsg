/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 8-Jul-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.coda;

import java.lang.*;
import java.util.*;
import java.nio.channels.Channel;

/**
 * Class in which to store a domain's client information.
 *
 * @author Carl Timmer
 * @version 1.0
 */
class cMsgClientInfo {
    /** Client's name. */
    String clientName;
    /** Client's port. */
    int    clientPort;
    /** Client's host. */
    String clientHost;
    /** Domain server's port. */
    int    domainPort;
    /** Domain server's host. */
    String domainHost;

    /**
     * Communication channel used by domain server (or clientHandler)
     * to talk to client (keepAlive).
     */
    Channel channel;

    /** Collection of all messages subscriptions. */
    HashSet subscriptions = new HashSet(20);

    cMsgClientInfo() {}
    
    cMsgClientInfo(String name, int port, String host) {
        clientName = name;
        clientPort = port;
        clientHost = host;
    }
}


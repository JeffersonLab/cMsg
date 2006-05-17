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
 * This interface defines some useful constants. These constants correspond
 * to identical constants defined in the C implementation of cMsg.
 *
 * @author Carl Timmer
 * @version 1.0
 */

public class cMsgNetworkConstants {
  
    private cMsgNetworkConstants() {}

    // constants from cMsgNetwork.h

    /** TCP port at which a cMsg domain name server starts looking for an unused listening port. */
    public static final int    nameServerStartingPort   = 3456;
    /** TCP port at which a cMsg domain, domain server starts looking for an unused listening port. */
    public static final int    domainServerStartingPort = 4567;  
    /** TCP port at which a cMsg domain client starts looking for an unused listening port. */
    public static final int    clientServerStartingPort = 2345;
    /** TCP port at which a TCPServer server listens for connections. */
    public static final int    tcpServerPort = 5432;
    /** UDP port at which a RunControl server listens for broadcasts. */
    public static final int    rcBroadcastPort = 6543;
    /** TCP port at which a RunControl server assumes a client is waiting for connections on. */
    public static final int    rcClientPort = 6543;

}

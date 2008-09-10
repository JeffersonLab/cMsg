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

    /** Ints representing ascii for "cMsg is cool", used to filter out portscanning software. */
    public static final int[] magicNumbers = {0x634d7367, 0x20697320, 0x636f6f6c};

    // There are very few officially used port #s in the 40,000s (only 5),
    // so we'll chose ports in the 45,000 range to avoid potential conflicts.

    /** Default TCP port at which a cMsg domain name server listens for client connections. */
    public static final int    nameServerPort = 45000;
    /** Default UDP port at which a cMsg name server listens for broadcasts. */
    public static final int    nameServerBroadcastPort = 45000;
    /** Default TCP port at which a cMsg domain, domain server listens for 2 client connections
      * (after that client has connected to name server). */
    public static final int    domainServerPort = 45100;
    /** UDP port at which a cMsg domain, domain server starts looking for an unused listening port. */
    public static final int    domainServerUdpStartingPort = 45100;
    /** Default UDP port at which a run control broadcast server listens for broadcasts. */
    public static final int    rcBroadcastPort = 45200;
    /** TCP port at which a run control server starts looking for a port to listen
     *  for a client connection on. */
    public static final int    rcServerPort = 45300;
    /** TCP port at which a run control client starts looking for a port to listen on and the port
      * that a run control server assumes a client is waiting for connections on. */
    public static final int    rcClientPort = 45400;

    /** TCP port at which a TCPServer server listens for connections. */
    public static final int    tcpServerPort = 45600;

    /** First int to send in UDP broadcast to server if cMsg domain. */
    public static final int    cMsgDomainBroadcast = 1;
    /** First int to send in UDP broadcast to server if RC domain and sender is client. */
    public static final int    rcDomainBroadcastClient = 2;
    /** First int to send in UDP broadcast to server if RC domain and sernder is server. */
    public static final int    rcDomainBroadcastServer = 4;
    /** Tell RCBroadcast server to kill himself since server at that port & expid already exists. */
    public static final int    rcDomainBroadcastKillSelf = 8;

    
    /** The biggest single UDP packet size is 2^16 - IP(v6) 40 byte header - 8 byte UDP header. */
    public static final int    biggestUdpPacketSize = 65487;

    /** The size (in bytes) of biggest buffers used to send UDP data from client to server. */
    public static final int    biggestUdpBufferSize = 65536;

    /** The size (in bytes) of big buffers used to send data from client to server. */
    public static final int    bigBufferSize = 131072;

}

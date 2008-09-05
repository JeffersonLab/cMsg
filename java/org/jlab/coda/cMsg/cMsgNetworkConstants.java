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

    /** TCP port at which a cMsg domain name server starts looking for an unused listening port. */
    public static final int    nameServerPort = 3456;
    /** TCP port at which a cMsg domain, domain server listens. */
    public static final int    domainServerPort = 4567;
    /** TCP port at which a cMsg domain, domain server starts looking for an unused UDP listening port. */
    public static final int    domainServerUdpStartingPort = 4567;
    /** TCP port at which a cMsg domain client starts looking for an unused listening port. */
    public static final int    clientServerStartingPort = 23945;
    /** TCP port at which a TCPServer server listens for connections. */
    public static final int    tcpServerPort = 5432;
    /** UDP port at which a RunControl server listens for broadcasts. */
    public static final int    rcBroadcastPort = 6543;
    /** TCP port at which a RunControl server listens for a client connection on. */
    public static final int    rcServerPort = 6543;
    /** TCP port at which a RunControl server assumes a client is waiting for connections on. */
    public static final int    rcClientPort = 6543;
    /** UDP port at which a cMsg name server listens for broadcasts. */
    public static final int    nameServerBroadcastPort = 7654;


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

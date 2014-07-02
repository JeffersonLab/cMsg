/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Author:  Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
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

    /** Ints representing ascii for "cMsg is cool", used to filter out port-scanning software. */
    public static final int[] magicNumbers = {0x634d7367, 0x20697320, 0x636f6f6c};

    /** Multicast address used in runcontrol domain = {@value} */
    public static final String rcMulticast = "239.210.0.0";
    
    /** Multicast address used in cMsg domain = {@value} */
    public static final String cMsgMulticast = "239.220.0.0";

    /** Multicast address used in Emu domain = {@value} */
    public static final String emuMulticast = "239.230.0.0";

    // There are very few officially used port #s in the 40,000s (only 5),
    // so we'll chose ports in the 45,000 range to avoid potential conflicts.

    /** Default TCP port at which an Emu domain server listens for client connections = {@value} */
    public static final int    emuTcpPort = 46100;
    /** Default TCP port at which a cMsg domain name server listens for client connections = {@value} */
    public static final int    nameServerTcpPort = 45000;
    /** Default UDP port at which a cMsg name server listens for multicasts = {@value} */
    public static final int    nameServerUdpPort = 45000;
    /** Default TCP port at which a cMsg domain, domain server listens for 2 client connections
      * (after that client has connected to name server) = {@value} */
    public static final int    domainServerPort = 45001;
    /** Default UDP port at which a run control multicast server listens for multicasts = {@value} */
    public static final int    rcMulticastPort = 45200;
    /** TCP port at which a run control server starts looking for a port to listen
     *  for a client connection on = {@value} */
    public static final int    rcServerPort = 45300;
    /** TCP port at which a run control client starts looking for a port to listen on and the port
      * that a run control server assumes a client is waiting for connections on = {@value} */
    public static final int    rcClientPort = 45800;

    /** TCP port at which a TCPServer server listens for connections = {@value} */
    public static final int    tcpServerPort = 45600;

    /** Int to send in UDP packet to cMsg name server if cMsg domain. */
    public static final int    cMsgDomainMulticast = 1;

    /** Int to send in UDP packet to server if RC domain and sender is client. */
    public static final int    rcDomainMulticastClient = 2;
    /** Int to send in UDP packet to rc multicast server if RC domain and sender is server. */
    public static final int    rcDomainMulticastServer = 4;
    /** Tell RCMulticast server to kill himself since server at that port & expid already exists. */
    public static final int    rcDomainMulticastKillSelf = 8;
    /** Client is just trying to find RCMulticast servers. */
    public static final int    rcDomainMulticastProbe = 16;

    /** Int to send in UDP packet to server if Emu domain and sender is client. */
    public static final int    emuDomainMulticastClient = 2;
    /** Int to send in UDP packet to Emu multicast server if Emu domain and sender is server. */
    public static final int    emuDomainMulticastServer = 4;
    /** Tell Emu multicast server to kill himself since server at that port & expid already exists. */
    public static final int    emuDomainMulticastKillSelf = 8;
    /** Client is just trying to find Emu multicast servers. */
    public static final int    emuDomainMulticastProbe = 16;


    /** The biggest single UDP packet size is 2^16 - IP(v6) 40 byte header - 8 byte UDP header. */
    public static final int    biggestUdpPacketSize = 65487;

    /** The size (in bytes) of biggest buffers used to send UDP data from client to server. */
    public static final int    biggestUdpBufferSize = 65536;

    /** The size (in bytes) of big buffers used to send data from client to server. */
    public static final int    bigBufferSize = 131072;

}

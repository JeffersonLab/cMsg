/*----------------------------------------------------------------------------*
 *  Copyright (c) 2008        Jefferson Science Associates,                   *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 7-May-2008, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.server;

import org.jlab.coda.cMsg.cMsgClientInfo;
import org.jlab.coda.cMsg.cMsgSubdomainInterface;

import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;
import java.io.DataOutputStream;
import java.net.DatagramSocket;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Need to store a little extra info not covered in the super class.
 */
public class cMsgClientData extends cMsgClientInfo {


    /** Socket to receive UDP sends from the client. */
    DatagramSocket udpSocket;
    
    /** Socket channel used by this server to get monitor data from client and
     * to send monitor data to client (dual functions as keepAlive). */
    SocketChannel keepAliveChannel;

    /** Output stream from this server to client. */
    DataOutputStream streamToClient;

    /** Reference to subdomain handler object. */
    cMsgSubdomainInterface subdomainHandler;

    /** Reference to cMsg subdomain handler object if appropriate. */
    org.jlab.coda.cMsg.cMsgDomain.subdomains.cMsg cMsgSubdomainHandler;

    /**
     * Keep track of whether the handleShutdown method of the subdomain
     * handler object has already been called.
     */
    AtomicBoolean calledSubdomainShutdown = new AtomicBoolean();

    /** Is this client in the cMsg domain? */
    boolean inCmsgSubdomain;

    boolean readingSize = true;
    
    /**
     * Size of data in bytes (not including int representing size which
     * is the first data sent).
     */
    int size;
    int bytesRead;

    /** Direct buffer for reading TCP nonblocking IO. */
    ByteBuffer buffer = ByteBuffer.allocateDirect(16384);

//    /** Reference to byte array used for reading TCP and UDP nonblocking IO. */
//    byte[] bArray;



    /** Convenience class to store all monitoring quantities. */
    static class monitorData {
        // Quantities measured by the domain server
        long birthday;
        long tcpSends, udpSends, syncSends, sendAndGets,
                subAndGets, subscribes, unsubscribes;
        // Quantities obtained from the client
        long clientTcpSends, clientUdpSends, clientSyncSends, clientSendAndGets,
                clientSubAndGets, clientSubscribes, clientUnsubscribes;
        boolean isJava;
        int pendingSendAndGets;
        int pendingSubAndGets;
        String monXML;
    }

    monitorData monData = new monitorData();

    long updateTime;




    /**
     * Constructor specifing client's name, port, host, subdomain, and UDL remainder.
     * Used in nameServer for a connecting regular client.
     *
     * @param name  client's name
     * @param nsPort name server's listening port
     * @param port  client's listening port
     * @param host  client's host
     * @param dotDec  client's address in dotted decimal form
     * @param subdomain    client's subdomain
     * @param UDLRemainder client's UDL's remainder
     * @param UDL          client's UDL
     * @param description  client's description
     */
    public cMsgClientData(String name, int nsPort, int port, String host,
                          String dotDec, String subdomain,                          
                          String UDLRemainder, String UDL, String description) {
        super(name, nsPort, port, host, dotDec, subdomain, UDLRemainder, UDL, description);
    }

    /**
     * Constructor used when cMsg server acts as a client and connects a to cMsg server.
     * Used in nameServer for a connecting server client.
     *
     * @param name  client's name
     * @param nsPort name server's listening port
     * @param host  client's host
     */
    public cMsgClientData(String name, int nsPort, String host) {
        super(name, nsPort, host);
    }
}
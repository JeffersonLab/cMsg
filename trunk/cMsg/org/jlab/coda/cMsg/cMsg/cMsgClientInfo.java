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

package org.jlab.coda.cMsg.cMsg;

import java.lang.*;
import java.util.*;
import java.nio.channels.SocketChannel;

/**
 * Class in which to store a domain's client information.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgClientInfo {
    /** Client's name. */
    String name;
    /** Subdomain client wishes to use. */
    String subdomain;
    /** Remainder from client's UDL. */
    String UDLRemainder;
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
    SocketChannel channel;

    /** Collection of all message subscriptions. */
    HashSet subscriptions = new HashSet(20);


    cMsgClientInfo() {}
    
    /**
     * Constructor specifing client's name, port, host, subdomain, and UDL remainder.
     *
     * @param name  client's name
     * @param port  client's listening port
     * @param host  client's host
     * @param subdomain  client's subdomain
     * @param UDLRemainder  client UDL's remainder
     */
    public cMsgClientInfo(String name, int port, String host, String subdomain, String UDLRemainder) {
        this.name = name;
        clientPort = port;
        clientHost = host;
        this.subdomain = subdomain;
        this.UDLRemainder = UDLRemainder;
    }
    /**
     * Constructor specifing client's name, port, host,.
     *
     * @param name  client's name
     * @param port  client's listening port
     * @param host  client's host
     */
    public cMsgClientInfo(String name, int port, String host) {
        this(name, port, host, null, null);
    }


    /**
     * Gets HashSet collection of all message subscriptions.
     * @return HashSet of all message subscriptions
     */
    public HashSet getSubscriptions() {
        return subscriptions;
    }

    /**
     * Gets communication channel used by server to talk to client.
     * @return communication channel
     */
    public SocketChannel getChannel() {
        return channel;
    }

    /**
     * Sets communication channel used by server to talk to client.
     * @param channel communication channel used by server to talk to client
     */
    public void setChannel(SocketChannel channel) {
        this.channel = channel;
    }

    /**
     * Gets host client is running on.
     * @return host client is running on
     */
    public String getClientHost() {
        return clientHost;
    }

    /**
     * Gets TCP port client is listening on.
     * @return TCP port client is listening on
     */
    public int getClientPort() {
        return clientPort;
    }

    /**
     * Gets host domain server is running on.
     * @return host domain server is running on
     */
    public String getDomainHost() {
        return domainHost;
    }

    /**
     * Gets TCP port domain server is listening on.
     * @return TCP port domain server is listening on
     */
    public int getDomainPort() {
        return domainPort;
    }

    /**
     * Gets client's name.
     * @return client's name
     */
    public String getName() {
        return name;
    }

    /**
     * Gets subdomain client is using.
     * @return subdomain client is using
     */
    public String getSubdomain() {
        return subdomain;
    }

    /**
     * Gets remainder of the UDL the client used to
     * connect to the domain server.
     * @return remainder of the UDL
     */
    public String getUDLRemainder() {
        return UDLRemainder;
    }
}


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

package org.jlab.coda.cMsg;

import java.lang.*;
import java.util.*;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.concurrent.locks.Lock;
import java.nio.channels.SocketChannel;

/**
 * Class in which to store a domain client's information.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgClientInfo {
    /** Client's name. */
    private String name;
    /** Client supplied description. */
    private String description;
    /** Client supplied UDL. */
    private String UDL;
    /** Remainder from client's UDL. */
    private String UDLremainder;
    /** Subdomain client wishes to use. */
    private String subdomain;
    /** Client's host. */
    private String clientHost;
    /** Client's port. */
    private int    clientPort;
    /** Domain server's host. */
    private String domainHost;
    /** Domain server's port. */
    private int    domainPort;

    /**
     * Communication channel used by domain server (or subdomainHandler)
     * to talk to client.
     */
    private SocketChannel channel;

    /** Collection of all message subscriptions. */
    private HashSet subscriptions = new HashSet(20);

    /** Collection of all message 1-shot get "subscriptions". */
    private HashSet gets = new HashSet(20);

    /**
     * This lock is for controlling access to the {@link #subscriptions} and
     * {@link #gets} hashsets. It is inherently more flexible than synchronizing
     * code, as most accesses of the hashsets are only reads. Using a readwrite
     * lock will prevent the mutual exclusion guaranteed by using synchronization.
     */
    private final ReentrantReadWriteLock lock = new ReentrantReadWriteLock();

    /** Read lock for {@link #subscriptions} and {@link #gets} hashmaps. */
    private Lock readLock = lock.readLock();

    /** Write lock for {@link #subscriptions} and {@link #gets} hashmaps. */
    private Lock writeLock = lock.writeLock();


    /** No arg constructor. */
    cMsgClientInfo() {}
    
    /**
     * Constructor specifing client's name, port, host, subdomain, and UDL remainder.
     *
     * @param name  client's name
     * @param port  client's listening port
     * @param host  client's host
     * @param subdomain    client's subdomain
     * @param UDLRemainder client's UDL's remainder
     * @param UDL          client's UDL
     * @param description  client's description
     */
    public cMsgClientInfo(String name, int port, String host, String subdomain,
                          String UDLRemainder, String UDL, String description) {
        this.name = name;
        clientPort = port;
        clientHost = host;
        this.subdomain = subdomain;
        this.UDLremainder = UDLRemainder;
        this.UDL = UDL;
        this.description = description;
    }
    /**
     * Constructor specifing client's name, port, host.
     *
     * @param name  client's name
     * @param port  client's listening port
     * @param host  client's host
     */
    public cMsgClientInfo(String name, int port, String host) {
        this(name, port, host, null, null, null, null);
    }

    //-----------------------------------------------------------------------------------

    /**
     * Gets client's name.
     * @return client's name
     */
    public String getName() {return name;}

    /**
     * Sets client's name.
     * @param name client's name
     */
    public void setName(String name) {this.name = name;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets client's description.
     * @return client's description
     */
    public String getDescription() {return description;}

    /**
     * Sets client's description.
     * @param description client's description
     */
    public void setDescription(String description) {this.description = description;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets client's UDL.
     * @return client's UDL
     */
    public String getUDL() {return UDL;}

    /**
     * Sets client's UDL.
     * @param UDL client's UDL
     */
    public void setUDL(String UDL) {this.UDL = UDL;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets remainder of the UDL the client used to connect to the domain server.
     * @return remainder of the UDL
     */
    public String getUDLremainder() {return UDLremainder;}

    /**
     * Sets remainder of the UDL the client used to connect to the domain server.
     * @param UDLremainder emainder of the UDL the client used to connect to the domain server
     */
    public void setUDLremainder(String UDLremainder) {this.UDLremainder = UDLremainder;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets subdomain client is using.
     * @return subdomain client is using
     */
    public String getSubdomain() {return subdomain;}

    /**
     * Sets subdomain client is using.
     * @param subdomain subdomain client is using
     */
    public void setSubdomain(String subdomain) {this.subdomain = subdomain;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets host client is running on.
     * @return host client is running on
     */
    public String getClientHost() {return clientHost;}

    /**
     * Sets host client is runnng on.
     * @param clientHost host client is runnng on
     */
    public void setClientHost(String clientHost) {this.clientHost = clientHost;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets TCP port client is listening on.
     * @return TCP port client is listening on
     */
    public int getClientPort() {return clientPort;}

    /**
     * Sets TCP port client is listening on.
     * @param clientPort TCP port client is listening on
     */
    public void setClientPort(int clientPort) {this.clientPort = clientPort;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets host domain server is running on.
     * @return host domain server is running on
     */
    public String getDomainHost() {return domainHost;}

    /**
     * Sets host domain server is running on.
     * @param domainHost host domain server is running on
     */
    public void setDomainHost(String domainHost) {this.domainHost = domainHost;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets TCP port domain server is listening on.
     * @return TCP port domain server is listening on
     */
    public int getDomainPort() {return domainPort;}

    /**
     * Sets TCP port domain server is listening on.
     * @param domainPort TCP port domain server is listening on
     */
    public void setDomainPort(int domainPort) {this.domainPort = domainPort;}

    //-----------------------------------------------------------------------------------

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

    //-----------------------------------------------------------------------------------

    /**
     * Gets HashSet collection of all message subscriptions.
     * @return HashSet of all message subscriptions
     */
    public HashSet getSubscriptions() {
        return subscriptions;
    }

    /**
     * Gets HashSet collection of all message gets.
     * @return HashSet of all message gets
     */
    public HashSet getGets() {
        return gets;
    }

    //-----------------------------------------------------------------------------------

    /**
     * Lock for reading {@link #subscriptions} and {@link #gets} hashmaps.
     * @return reading lock object
     */
    public Lock getReadLock() {return readLock;}

    /**
     * Lock for writing to {@link #subscriptions} and {@link #gets} hashmaps.
     * @return writing lock object
     */
    public Lock getWriteLock() {return writeLock;}

}


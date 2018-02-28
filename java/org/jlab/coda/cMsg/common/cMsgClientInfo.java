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

package org.jlab.coda.cMsg.common;

import org.jlab.coda.cMsg.cMsgDomain.subdomains.cMsgMessageDeliverer;

import java.lang.*;
import java.nio.channels.SocketChannel;
import java.net.InetSocketAddress;


/**
 * This class stores a cMsg client's information.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgClientInfo {
    /** Client's name. */
    protected String  name;
    /** Client's address in dotted decimal form. */
    protected String  dottedDecimalAddr;
    /** Client supplied description. */
    protected String  description;
    /** Client supplied UDL. */
    protected String  UDL;
    /** Remainder from client's UDL. */
    protected String  UDLremainder;
    /** Subdomain client wishes to use. */
    protected String  subdomain;
    /** cMsg subdomain namespace client is using. */
    protected String  namespace;
    /** Client's host. */
    protected String  clientHost;

    /** Domain server's host. */
    protected String  domainHost;
    /** Domain server's TCP port. */
    protected int     domainPort;
    /** Domain server's UDP port. */
    protected int     domainUdpPort;

    /** Is this client another cMsg server? */
    protected boolean isServer;

    /**
     * If this client is a cMsg server, this quantity is the TCP listening port
     * of the server which has just become a client of cMsg server where this
     * object lives. (I hope you got that).
     */
    protected int serverPort;

    /**
     * If this client is a cMsg server, this quantity is the UDP multicast port
     * of the server which has just become a client of cMsg server where this
     * object lives. (I hope you got that).
     */
    protected int multicastPort;

    /** Socket channel used to get messages to and receive messages/requests from client. */
    protected SocketChannel messageChannel;

    /** Object for delivering messages to this client. */
    protected cMsgMessageDeliverer deliverer;


    /**
     * Default constructor.
     */
    public cMsgClientInfo() {
    }

    /**
     * Constructor specifing client's name, port, host, subdomain, and UDL remainder.
     * Used in nameServer for a connecting regular client.
     *
     * @param name  client's name
     * @param dotDec  client's address in dotted decimal form
     * @param nsPort name server's listening port
     * @param dPort  domain server's listening port
     * @param host  client's host
     * @param subdomain    client's subdomain
     * @param UDLRemainder client's UDL's remainder
     * @param UDL          client's UDL
     * @param description  client's description
     */
    public cMsgClientInfo(String name, int nsPort, int dPort, String host,
                          String dotDec, String subdomain,
                          String UDLRemainder, String UDL, String description) {
        this.name = name;
        dottedDecimalAddr = dotDec;
        serverPort = nsPort;
        domainPort = dPort;
        clientHost = host;
        this.subdomain = subdomain;
        this.UDLremainder = UDLRemainder;
        this.UDL = UDL;
        this.description = description;
    }

    /**
     * Constructor used when cMsg server acts as a client and connects a to cMsg server.
     * Used in nameServer for a connecting server client.
     *
     * @param name  client's name
     * @param nsPort name server's TCP listening port
     * @param mPort name server's UDP multicast listening port
     * @param host  client's host
     */
    public cMsgClientInfo(String name, int nsPort, int mPort, String host) {
        this.name     = name;
        serverPort    = nsPort;
        multicastPort = mPort;
        clientHost    = host;
        isServer      = true;
    }


    //-----------------------------------------------------------------------------------

    /**
     * Gets client's name.
     * @return client's name
     */
    public String getName() {return name;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets client's address in dotted decimal form.
     * @return client's address in dotted decimal form
     */
    public String getDottedDecimalAddr() {return dottedDecimalAddr;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets client's description.
     * @return client's description
     */
    public String getDescription() {return description;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets client's UDL.
     * @return client's UDL
     */
    public String getUDL() {return UDL;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets remainder of the UDL the client used to connect to the domain server.
     * @return remainder of the UDL
     */
    public String getUDLremainder() {return UDLremainder;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets subdomain client is using.
     * @return subdomain client is using
     */
    public String getSubdomain() {return subdomain;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets the namespace of client's cMsg subdomain.
     * @return namespace of client's cMsg subdomain
     */
    public String getNamespace() {return namespace;}

    /**
     * Sets the namespace of client's cMsg subdomain.
     * @param namespace namespace of client's cMsg subdomain
     */
    public void setNamespace(String namespace) {this.namespace = namespace;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets host client is running on.
     * @return host client is running on
     */
    public String getClientHost() {return clientHost;}

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
     * Gets UDP port domain server is listening on.
     * Meaningful only if client is sending by UDP.
     * @return UDP port domain server is listening on
     */
    public int getDomainUdpPort() {return domainUdpPort;}

    /**
     * Sets UDP port domain server is listening on.
     * Meaningful only if client is sending by UDP.
     * @param domainUdpPort TCP port domain server is listening on
     */
    public void setDomainUdpPort(int domainUdpPort) {this.domainUdpPort = domainUdpPort;}

    //-----------------------------------------------------------------------------------

    /**
     * Gets host connecting name server (client of this server) is running on.
     * @return host connecting name server is running on
     */
    public String getServerHost() {return clientHost;}

    /**
     * Gets the TCP port the connecting name server (client of this server)
     * is listening on.
     * @return TCP port connecting name server is listening on
     */
    public int getServerPort() {return serverPort;}

    /**
     * Gets the UDP multicasting port the connecting name server (client of this server)
     * is listening on.
     * @return TCP port connecting name server is listening on
     */
    public int getServerMulticastPort() {return multicastPort;}

    //-----------------------------------------------------------------------------------

    /**
     * States whether this client is a cMsg server or not.
     * @return true if this client is a cMsg server
     */
    public boolean isServer() {
        return isServer;
    }

    //-----------------------------------------------------------------------------------

    /**
     * Gets the SocketChannel used to send messages to and receives messages/requests from this client.
     * @return the SocketChannel used to send messages to and receives messages/requests from this client
     */
    public SocketChannel getMessageChannel() {
        return messageChannel;
    }

    /**
     * Sets the SocketChannel used to send messages to and receives messages/requests from this client.
     * @param messageChannel the SocketChannel used to send messages to and receives
     *                       messages/requests from this client
     */
    public void setMessageChannel(SocketChannel messageChannel) {
        this.messageChannel = messageChannel;
    }

    /**
     * Gets the InetAddress object of the client end of the socket the client uses for
     * sending/receiving messages to/from server.
     * @return InetAddress object of the client end of the socket the client uses for
     *         sending/receiving messages to/from server
     */
    public InetSocketAddress getMessageChannelRemoteSocketAddress() {
        return (InetSocketAddress)messageChannel.socket().getRemoteSocketAddress();
    }

    //-----------------------------------------------------------------------------------
    /**
     * Gets the object used to deliver messages to this client.
     * @return object used to deliver messages to this client
     */
    public cMsgMessageDeliverer getDeliverer() {
        return deliverer;
    }

    /**
     * Sets the object used to deliver messages to this client.
     * @param deliverer object used to deliver messages to this client
     */
    public void setDeliverer(cMsgMessageDeliverer deliverer) {
        this.deliverer = deliverer;
    }


 }


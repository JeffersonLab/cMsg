/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 7-Jul-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain;

import java.io.*;
import java.net.*;
import java.lang.*;
import java.util.*;
import java.nio.channels.Selector;
import java.nio.channels.SelectionKey;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;

import org.jlab.coda.cMsg.*;

/**
 * This class implements a cMsg name server in the cMsg domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgNameServer extends Thread {

    /** Type of domain this is. */
    private String domain = "cMsg";

    /** Port number to listen on. */
    private int port;
  
    /** Port number from which to start looking for a suitable listening port. */
    private int startingPort;

    /** Server channel (contains socket). */
    private ServerSocketChannel serverChannel;

    /** A direct buffer is necessary for nio socket IO. */
    private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

    /**
     * Keep all domain server objects in a weak hashmap (if the
     * only reference to the server object is in this hashmap,
     * it is still garbage collected). Thus, the only domain servers
     * left in the hashmap will be those still active. This map
     * will then be used to call the active domain servers'
     * handleServerShutdown methods when this name server is being
     * shutdown.
     */
    private WeakHashMap domainServers;

    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugInfo;
  
    /** Tell the server to kill spawned threads. */
    private boolean killAllThreads;
  
    /**
     * Sets boolean to kill this and all spawned threads.
     * @param b setting to true will kill this and all spawned threads
     */
    public void setKillAllThreads(boolean b) {killAllThreads = b;}
  
    /** Gets boolean value specifying whether to kill this and all spawned threads. */
    public boolean getKillAllThreads() {return killAllThreads;}
  
    /** Gets type of domain this object serves. */
    public String getDomain() {return domain;}


    /**
     * Constructor which reads environmental variables and opens listening socket.
     *
     * @throws cMsgException If a port to listen on could not be found or
     *                          no handler class has been specified
     *                          (command line or in env var CMSG_HANDLER)
     */
    public cMsgNameServer() throws cMsgException {
        domainServers = new WeakHashMap(20);

        // read env variable for starting (desired) port number
        try {
            String env = System.getenv("CMSG_PORT");
            if (env != null) {
                startingPort = Integer.parseInt(env);
            }
        }
        catch (NumberFormatException ex) {
        }

        // port #'s < 1024 are reserved
        if (startingPort < 1024) {
            startingPort = cMsgNetworkConstants.nameServerStartingPort;
        }

        // At this point, find a port to bind to. If that isn't possible, throw
        // an exception.
        try {
            serverChannel = ServerSocketChannel.open();
        }
        catch (IOException ex) {
            ex.printStackTrace();
            throw new cMsgException("Exiting Server: cannot open a listening socket");
        }

        port = startingPort;
        ServerSocket listeningSocket = serverChannel.socket();

        while (true) {
            try {
                listeningSocket.bind(new InetSocketAddress(port));
                break;
            }
            catch (IOException ex) {
                // try another port by adding one
                if (port < 65536) {
                    port++;
                }
                else {
                    ex.printStackTrace();
                    throw new cMsgException("Exiting Server: cannot find port to listen on");
                }
            }
        }
    }
  
  
    /** Run as a stand-alone application. */
    public static void main(String[] args) {
        try {
            cMsgNameServer server = new cMsgNameServer();
            server.start();
        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }
    }


    /**
     * Method to be run when this server is unreachable and all its threads are killed.
     *
     * Finalize methods are run after an object has become unreachable and
     * before the garbage collector is run;
     */
    public void finalize() throws cMsgException {
        cMsgDomainServer server;
        for (Iterator i = domainServers.keySet().iterator(); i.hasNext(); ) {
            server = (cMsgDomainServer)i.next();
            server.getClientHandler().handleServerShutdown();
        }
    }


    static private cMsgSubdomainHandler createClientHandler(String subdomain, String UDLRemainder) throws cMsgException {
        /** Object to handle clients' inputs */
        cMsgSubdomainHandler clientHandler = null;

         // First check to see if handler class name was set on the command line.
        String clientHandlerClass = System.getProperty(subdomain);

        // If it wasn't given on the command line,
        // check the appropriate environmental variable.
        if (clientHandlerClass == null) {
            clientHandlerClass = System.getenv("CMSG_HANDLER");
        }

        // If there is still no handler class and if the
        // cMsg subdomain is desired, look for the
        // org.jlab.coda.cMsg.cMsgDomain.subdomains.cMsg class.
        if (clientHandlerClass == null) {
            if (subdomain.equalsIgnoreCase("cMsg")) {
                clientHandlerClass = "org.jlab.coda.cMsg.subdomains.cMsg";
            }
            else if (subdomain.equalsIgnoreCase("CA")) {
                clientHandlerClass = "org.jlab.coda.cMsg.subdomains.CA";
            }
            else if (subdomain.equalsIgnoreCase("database")) {
                clientHandlerClass = "org.jlab.coda.cMsg.subdomains.database";
            }
            else if (subdomain.equalsIgnoreCase("LogFile")) {
                clientHandlerClass = "org.jlab.coda.cMsg.subdomains.LogFile";
            }
            else if (subdomain.equalsIgnoreCase("LogTable")) {
                clientHandlerClass = "org.jlab.coda.cMsg.subdomains.LogTable";
            }
            else if (subdomain.equalsIgnoreCase("smartsockets")) {
                clientHandlerClass = "org.jlab.coda.cMsg.subdomains.smartsockets";
            }
        }

        // all options are exhaused, throw error
        if (clientHandlerClass == null) {
            cMsgException ex = new cMsgException("no handler class found");
            ex.setReturnCode(cMsgConstants.errorNoClassFound);
            throw ex;
        }


        // Get handler class name and create handler object
        try {
            clientHandler = (cMsgSubdomainHandler) (Class.forName(clientHandlerClass).newInstance());
        }
        catch (InstantiationException e) {
            cMsgException ex = new cMsgException("cannot instantiate "+ clientHandlerClass +
                                                 " class");
            ex.setReturnCode(cMsgConstants.error);
            throw ex;
        }
        catch (IllegalAccessException e) {
            cMsgException ex = new cMsgException("cannot access "+ clientHandlerClass +
                                                 " class");
            ex.setReturnCode(cMsgConstants.error);
            throw ex;
        }
        catch (ClassNotFoundException e) {
            cMsgException ex = new cMsgException("no handler class found");
            ex.setReturnCode(cMsgConstants.errorNoClassFound);
            throw ex;
        }

        // The first thing we do is pass the UDL remainder to the handler
        clientHandler.setUDLRemainder(UDLRemainder);

        return clientHandler;
    }


    /**
     * Method to register a client with this name server. This method passes on the
     * registration function to the client handler object. Part of the information
     * in the cMsgClientInfo object is the subdomain which specifies the type of
     * client handler object needed. This handler object gets the UDL remainder
     * (also part of the cMsgClientInfo object) which it can parse as it sees fit.
     *
     * The subdomain should have a class by that name that can be loaded and used
     * as the client handler object. The classes corresponding to these handlers
     * must be passed to the name server on the command line as in the following:
     *     java cMsgNameServer -DcMsg=myCmsgClientHandlerClass
     *
     * @param info object containing information about the client
     * @throws cMsgException If a domain server could not be started for the client
     */
    synchronized public cMsgSubdomainHandler registerClient(cMsgClientInfo info) throws cMsgException {
        cMsgSubdomainHandler clientHandler = createClientHandler(info.getSubdomain(),
                                                               info.getUDLremainder());
        // If clientHandler is a subclass of cMsgHandlerRequestAbstract, it has methods
        // to connect to the client, so do it now. The socket channel is stored in "info".
        if (clientHandler instanceof cMsgSubdomainAbstract) {
            try {
                ((cMsgSubdomainAbstract)clientHandler).createChannel(info);
            }
            catch (IOException e) {
                cMsgException ex = new cMsgException("socket communication error");
                ex.setReturnCode(cMsgConstants.errorNetwork);
                throw ex;
            }
        }

        // pass registration on to handler object
        clientHandler.registerClient(info);

        // Create a domain server thread, and get back its host & port
        cMsgDomainServer server = new cMsgDomainServer(clientHandler, info,
                                                       cMsgNetworkConstants.domainServerStartingPort);
        // kill this thread too if name server thread quits
        server.setDaemon(true);
        server.start();
        domainServers.put(server, null);

        return clientHandler;
    }


    /** This method is executed as a thread. */
    public void run() {
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("Running Name Server");
        }

        try {
            // get things ready for a select call
            Selector selector = Selector.open();

            // set nonblocking mode for the listening socket
            serverChannel.configureBlocking(false);

            // register the channel with the selector for accepts
            serverChannel.register(selector, SelectionKey.OP_ACCEPT);

            while (true) {
                // 3 second timeout
                int n = selector.select(3000);

                // if no channels (sockets) are ready, listen some more
                if (n == 0) {
                    // but first check to see if we've been commanded to die
                    if (getKillAllThreads()) {
                        return;
                    }
                    continue;
                }

                // get an iterator of selected keys (ready sockets)
                Iterator it = selector.selectedKeys().iterator();

                // look at each key
                while (it.hasNext()) {
                    SelectionKey key = (SelectionKey) it.next();

                    // is this a new connection coming in?
                    if (key.isValid() && key.isAcceptable()) {
                        ServerSocketChannel server = (ServerSocketChannel) key.channel();
                        // accept the connection from the client
                        SocketChannel channel = server.accept();
                        // let us know (in the next select call) if this socket is ready to read
                        cMsgUtilities.registerChannel(selector, channel, SelectionKey.OP_READ);

                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("\ncMsgNameServer: registered client");
                        }
                    }

                    // is there data to read on this channel?
                    if (key.isValid() && key.isReadable()) {
                        SocketChannel channel = (SocketChannel) key.channel();
                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("cMsgNameServer: client request");
                        }
                        handleClient(channel);
                    }

                    // remove key from selected set since it's been handled
                    it.remove();
                }
            }
        }
        catch (IOException ex) {
            if (debug >= cMsgConstants.debugError) {
                //ex.printStackTrace();
            }
        }

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("\n\nQuitting Name Server");
        }

        return;
    }


    /**
     * This method handles all communication between a cMsg user
     * and this name server for that domain.
     *
     * @param channel nio socket communication channel
     */
    private void handleClient(SocketChannel channel) {

        try {
            // keep reading until we have 7 ints of data
            cMsgUtilities.readSocketBytes(buffer, channel, 36, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // read 7 ints
            int[] inComing = new int[9];
            buffer.asIntBuffer().get(inComing);

            // message id
            int msgId = inComing[0];
            // listening port of client
            int clientListeningPort = inComing[1];
            // length of domain type client is expecting to connect to
            int lengthDomainType = inComing[2];
            // length of subdomain type client is expecting to use
            int lengthSubdomainType = inComing[3];
            // length of UDL remainder to pass to subdomain handler
            int lengthUDLRemainder = inComing[4];
            // length of client's host name
            int lengthHost = inComing[5];
            // length of client's name
            int lengthName = inComing[6];
            // length of client's UDL
            int lengthUDL = inComing[7];
            // length of client's description
            int lengthDescription = inComing[8];

            // bytes expected
            int bytesToRead = lengthDomainType + lengthSubdomainType +
                              lengthUDLRemainder + lengthHost + lengthName +
                              lengthUDL + lengthDescription;

            // read in all remaining bytes
            cMsgUtilities.readSocketBytes(buffer, channel, bytesToRead, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // allocate byte array big enough for everything
            byte[] buf = new byte[bytesToRead];

            // read domain
            buffer.get(buf, 0, lengthDomainType);
            String domainType = new String(buf, 0, lengthDomainType, "US-ASCII");
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  domain = " + domainType);
            }

            // read subdomain
            buffer.get(buf, 0, lengthSubdomainType);
            String subdomainType = new String(buf, 0, lengthSubdomainType, "US-ASCII");
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  subdomain = " + subdomainType);
            }

            // read UDL remainder
            buffer.get(buf, 0, lengthUDLRemainder);
            String UDLRemainder = new String(buf, 0, lengthUDLRemainder, "US-ASCII");
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  remainder = " + UDLRemainder);
            }

            // read host
            buffer.get(buf, 0, lengthHost);
            String host = new String(buf, 0, lengthHost, "US-ASCII");
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  host = " + host);
            }

            // read name
            buffer.get(buf, 0, lengthName);
            String name = new String(buf, 0, lengthName, "US-ASCII");
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  port = " + clientListeningPort);
                System.out.println("  name = " + name);
            }

            // read UDL
            buffer.get(buf, 0, lengthUDL);
            String UDL = new String(buf, 0, lengthUDL, "US-ASCII");
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  UDL = " + UDL);
            }

            // read description
            buffer.get(buf, 0, lengthDescription);
            String description = new String(buf, 0, lengthDescription, "US-ASCII");
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  description = " + description);
            }

            // if this is not the domain of server the client is expecting, return an error
            if (!domainType.equalsIgnoreCase(this.domain)) {
                // send error to client
                buffer.clear();
                buffer.putInt(cMsgConstants.errorWrongDomainType).flip();
                while (buffer.hasRemaining()) {
                    channel.write(buffer);
                }
                return;
            }


            // Try to register this client. If the cMsg system already has a
            // client by this name, it will fail.
            try {
                cMsgClientInfo info = new cMsgClientInfo(name, clientListeningPort, host,
                                                         subdomainType, UDLRemainder,
                                                         UDL, description);
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("name server to register " + name);
                }

                cMsgSubdomainHandler handler = registerClient(info);

                buffer.clear();

                // send ok back as acknowledgment
                buffer.putInt(cMsgConstants.ok);

                // send back attributes of clientHandler class/object
                // 1 = has, 0 = don't have: send, subscribeAndGet, sendAndGet, subscribe, unsubscribe
                byte[] atts = new byte[6];
                atts[0] = handler.hasSend()            ? (byte)1 : (byte)0;
                atts[1] = handler.hasSyncSend()        ? (byte)1 : (byte)0;
                atts[2] = handler.hasSubscribeAndGet() ? (byte)1 : (byte)0;
                atts[3] = handler.hasSendAndGet()      ? (byte)1 : (byte)0;
                atts[4] = handler.hasSubscribe()       ? (byte)1 : (byte)0;
                atts[5] = handler.hasUnsubscribe()     ? (byte)1 : (byte)0;
                buffer.put(atts);

                // send cMsg domain host & port contact info back to client
                buffer.putInt(info.getDomainPort());
                buffer.putInt(info.getDomainHost().length());
                buffer.put(info.getDomainHost().getBytes("US-ASCII")).flip();

                while (buffer.hasRemaining()) {
                    channel.write(buffer);
                }

            }
            catch (cMsgException ex) {
                // send error to client
                buffer.clear();
                buffer.putInt(ex.getReturnCode()).flip();
                while (buffer.hasRemaining()) {
                    channel.write(buffer);
                }
            }

            return;
        }
        catch (IOException ex) {
            if (debug >= cMsgConstants.debugError) {
                System.out.println("cMsgNameServer's Client thread: IO error in talking to client");
            }
        }
        finally {
            // we are done with the channel
            try {
                channel.close();
            }
            catch (IOException ex) {
            }
        }
    }

}

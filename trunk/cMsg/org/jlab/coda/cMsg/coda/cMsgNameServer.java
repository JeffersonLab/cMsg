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

package org.jlab.coda.cMsg.coda;

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
 * This class implements a cMsg name server for a particular cMsg domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgNameServer extends Thread {

    /** Type of domain this is. */
    private String type = "CODA";

    /** Object to handle clients' inputs */
    private cMsgHandleRequests clientHandler;

    /** Port number to listen on. */
    private int port;
  
    /** Port number from which to start looking for a suitable listening port. */
    private int startingPort;
  
    /**
     * HashMap which stores all client information. Client name
     * is the key and cMsgClientInfo objects are the value.
     */
    private HashMap clients = new HashMap(100);

    /** Server channel (contains socket). */
    private ServerSocketChannel serverChannel;

    /** A direct buffer is necessary for nio socket IO. */
    private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugInfo;
  
    /** Tell the server to kill spawned threads. */
    private boolean killAllThreads;
  
    /**
     * Sets boolean to kill all spawned threads.
     * @param b setting to true will kill spawned threads
     */
    public void setKillAllThreads(boolean b) {killAllThreads = b;}
  
    /** Gets boolean value specifying whether to kill all spawned threads. */
    public boolean getKillAllThreads() {return killAllThreads;}
  
    /** Gets type of domain this object serves. */
    public String getType() {return type;}

    /** Gets object which handles client requests. */
    public cMsgHandleRequests getClientHandler() {
        return clientHandler;
    }

    /**
     * Constructor which reads environmental variables and starts threads.
     */
    public cMsgNameServer() throws cMsgException {
        // First check to see if handler class name was set on the command line.
        String clientHandlerClass = System.getProperty("handler");
        Jgetenv env = null;

        // read env variable for starting (desired) port number
        try {
            env = new Jgetenv();
            startingPort = Integer.parseInt(env.echo("CMSG_PORT"));
        }
        catch (NumberFormatException ex) {
        }
        catch (JgetenvException ex) {
        }

        try {
            if (clientHandlerClass == null) {
                clientHandlerClass = env.echo("CMSG_HANDLER");
            }
        }
        catch (JgetenvException e) {
            throw new cMsgException(e.getMessage());
        }

        // port #'s < 1024 are reserved
        if (startingPort < 1024) {
            startingPort = cMsgNetworkConstants.nameServerStartingPort;
        }

        // Get handler class name and create handler object
        try {
            clientHandler = (cMsgHandleRequests) Class.forName(clientHandlerClass).newInstance();
        }
        catch (InstantiationException e) {
            e.printStackTrace();
        }
        catch (IllegalAccessException e) {
            e.printStackTrace();
        }
        catch (ClassNotFoundException e) {
            e.printStackTrace();
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
     * Method to register a client with this name server.
     *
     * @param name unique client name
     * @param info object containing information about the client
     * @throws cMsgException If a domain server could not be started for the client
     */
    synchronized public void registerClient(String name, cMsgClientInfo info)
            throws cMsgException {
        // Check to see if name is taken already
        if (clients.containsKey(name)) {
            throw new cMsgException("client already exists");
        }

        clients.put(name, info);

        // Create either a domain server thread or process, and get back its host & port
        startDomainServer(info);

    }


    /**
     * Method to start a thread or process which handles all the sent
     * messages from the client and forwards subscribed messages to the client.
     *
     * @param info object containing information about the client with whom the
     *             domain server will be communicating
     * @throws cMsgException If a domain server could not be started due to
     *                       not finding a port to listen on, or trouble
     *                       starting a separate process for the server
     */
    private void startDomainServer(cMsgClientInfo info) throws cMsgException {
        cMsgDomainServer server = new cMsgDomainServer(clientHandler, info, cMsgNetworkConstants.domainServerStartingPort);
        server.start();

        return;
    }


    /**
     * This method is executed as a thread.
     */
    public void run() {
         System.out.println("Running Name Server");

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
                         registerChannel(selector, channel, SelectionKey.OP_READ);

                         if (debug >= cMsgConstants.debugInfo) {
                             System.out.println("\ncMsgDomainServer: registered client\n");
                         }
                     }

                     // is there data to read on this channel?
                     if (key.isValid() && key.isReadable()) {
                         SocketChannel channel = (SocketChannel) key.channel();
                         if (debug >= cMsgConstants.debugInfo) {
                             System.out.println("\n\ncMsgDomainServer: client request");
                         }
                         handleClient(channel);
                     }

                     // remove key from selected set since it's been handled
                     it.remove();
                 }
             }
         }
         catch (IOException ex) {
         }
         System.out.println("Quitting Name Server");
         return;
     }


     private void registerChannel(Selector selector, SocketChannel channel, int ops) {
          if (channel == null) {
              return;
          }

          try {
              // set socket options, first make socket nonblocking
              channel.configureBlocking(false);

              // get socket
              Socket socket = channel.socket();
              // Set tcpNoDelay so no packets are delayed
              socket.setTcpNoDelay(true);
              // set buffer sizes
              socket.setReceiveBufferSize(65535);
              socket.setSendBufferSize(65535);

              channel.register(selector, ops);
          }
          catch (IOException e) {
              e.printStackTrace();
          }
      }


     /**
      * This method handles all communication between a cMsg user who has
      * connected to a domain and this server for that domain.
      */
     private void handleClient(SocketChannel channel) {

         try {
             // create a message
             cMsgMessage msg = new cMsgMessage();

             // keep reading until we have 5 ints of data
             cMsgUtilities.readSocketBytes(buffer, channel, 20, debug);

             // go back to reading-from-buffer mode
             buffer.flip();

             // read 5 ints
             int[] inComing = new int[5];
             buffer.asIntBuffer().get(inComing);

             // message id
             int msgId = inComing[0];

             // listening port of client
             int clientListeningPort = inComing[1];

             // length of domain domainType client is expecting to connect to
             int lengthType = inComing[2];

             // length of client's host name
             int lengthHost = inComing[3];

             // length of client's name
             int lengthName = inComing[4];

             if (debug >= cMsgConstants.debugInfo) {
                 System.out.println("msgId = " + msgId +
                                    ", port = " + clientListeningPort +
                                    ", lenType = " + lengthType +
                                    ", lenHost = " + lengthHost +
                                    ", lenName = " + lengthName);
             }

             // bytes expected
             int bytesToRead = lengthType + lengthHost + lengthName;

             // read in all remaining bytes
             cMsgUtilities.readSocketBytes(buffer, channel, bytesToRead, debug);

             // go back to reading-from-buffer mode
             buffer.flip();

             // allocate byte array
             int lengthBuf = lengthHost > lengthType ? lengthHost : lengthType;
             lengthBuf = lengthBuf > lengthName ? lengthBuf : lengthName;
             byte[] buf = new byte[lengthBuf];

             // read domainType
             buffer.get(buf, 0, lengthType);
             String domainType = new String(buf, 0, lengthType, "US-ASCII");
             if (debug >= cMsgConstants.debugInfo) {
                 System.out.println("  domainType = " + domainType);
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
                 System.out.println("  name = " + name);
             }

             // if this is not the domainType of server the client is expecting, return an error
             if (!domainType.equalsIgnoreCase(type)) {
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
                 if (debug >= cMsgConstants.debugInfo) {
                     System.out.println("register client");
                 }
                 cMsgClientInfo info = new cMsgClientInfo(name, clientListeningPort, host);
                 registerClient(name, info);

                 // send ok back as acknowledgment
                 buffer.clear();
                 buffer.putInt(cMsgConstants.ok).flip();
                 while (buffer.hasRemaining()) {
                     channel.write(buffer);
                 }

                 // send cMsg domain host & port contact info back to client
                 buffer.clear();
                 buffer.putInt(info.domainPort);
                 buffer.putInt(info.domainHost.length());
                 buffer.put(info.domainHost.getBytes("ASCII")).flip();
                 while (buffer.hasRemaining()) {
                     channel.write(buffer);
                 }

             }
             catch (cMsgException ex) {
                 // send error to client
                 buffer.clear();
                 buffer.putInt(cMsgConstants.errorNameExists).flip();
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

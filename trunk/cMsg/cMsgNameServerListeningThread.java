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


// package org.jlab.coda.cMsg;

import java.io.*;
import java.net.*;
import java.lang.*;

/**
 * This class implements a thread which listens for connections to a cMsg
 * name server for a particular cMsg domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgNameServerListeningThread extends Thread {
  
  /** cMsgNameServer object which spawned this thread. */
  cMsgNameServer server;
  
  /** Port number to listen on. */
  private int port;
  
  /** Host this is running on. */
  private String host;
  
  /** Level of debug output. */
  private int debug;
  

  /**
   * Creates a new cMsgNameServerListeningThread object.
   *
   * @param server cMsg name server which is starting this thread
   * @param startingPort suggested port on which to starting listening for connections
   * @param debug level of debug output for this object
   */
  cMsgNameServerListeningThread(cMsgNameServer server, int startingPort, int debug) {
      port = startingPort;
      this.server = server;
      this.debug  = debug;
  }

  
  /** This method is executed as a thread. */
  public void run() {
    System.out.println("Running Server");
    ServerSocket listeningSocket;
    
    // find a port to listen on
    while (true) {
      try {
        listeningSocket = new ServerSocket(port);
        break;
      }
      catch (IOException ex) {
        // try another port by adding one
        if (port < 65536) {
          port++;
        }
        else {
         System.out.println("Exiting Server: cannot find port to listen on");
         return;
        }
      }
    }

    try {
      while (true) {
	// socket to client created
	Socket sock;
        
        // 3 second accept timeout
        listeningSocket.setSoTimeout(3000);
        
	while (true) {
	  try {
	    sock = listeningSocket.accept();
	    break;
	  }
          // server socket accept timeout
	  catch (InterruptedIOException ex) {
	    // check to see if we've been commanded to die
	    if (server.getKillAllThreads()) {
	      return;
	    }
	  }
	}
	// Set reading timeout to 1/2 second so dead clients
	// can be found by reading on a socket.
	// sock.setSoTimeout(500);
        
        // Set tcpNoDelay so no packets are delayed
        sock.setTcpNoDelay(true);
        // set buffer size
        sock.setReceiveBufferSize(65535);
        sock.setSendBufferSize(65535);
	// create thread to deal with client
	cMsgNameServerClientThread connection = new cMsgNameServerClientThread(server, sock, debug);
	connection.start();
      }
    }
    catch (SocketException ex) {
    }
    catch (IOException ex) {
    }
    return;
  }

}


/**
 * This class handles all communication between a cMsg user who is
 * just connecting to a domain and this name server for that domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
class cMsgNameServerClientThread extends Thread {

  /** Name server object. */
  private cMsgNameServer server;
  
  /** Level of debug output. */
  private int debug;

  /** Tcp socket. */
  private Socket sock;
  /** Input stream from the socket. */
  private InputStream sockIn;
  /** Output stream from the socket. */
  private OutputStream sockOut;
  /** Data input stream built on top of the socket's input stream (with an
   *  intervening buffered input stream). */
  private DataInputStream  in;
  /** Data output stream built on top of the socket's output stream (with an
   *  intervening buffered output stream). */
  private DataOutputStream out;

  /**
   * Create a new cMsgNameServerClientThread object.
   *
   * @param server name server object.
   * @param sock Tcp socket.
   * @param debug level of debug output for this object
   */
  cMsgNameServerClientThread(cMsgNameServer server, Socket sock, int debug) {
    this.server = server;
    this.sock   = sock;
    this.debug  = debug;
  }

  /** Start thread to handle communications with user. */
  public void run() {

    try {
      // buffered communication streams for efficiency
      sockIn  = sock.getInputStream();
      sockOut = sock.getOutputStream();
      in  = new DataInputStream(new BufferedInputStream(sock.getInputStream(), 65535));
      out = new DataOutputStream(new BufferedOutputStream(sock.getOutputStream(), 65535));
      
      // read message id
      int msgId = in.readInt();
      
      // read listening port of client
      int clientListeningPort = in.readInt();
      
      // read length of client's host name
      int lengthHost = in.readInt();
      
      // read length of client's name
      int lengthName = in.readInt();
if (debug >= cMsgConstants.debugInfo) {
  System.out.println("msgId = " + msgId + ", port = " + clientListeningPort +
                      ", lenHost = " + lengthHost + ", lenName = " + lengthName);
}      
      // allocate buffer
      int lengthBuf = lengthHost > lengthName ? lengthHost : lengthName;
      byte[] buf = new byte[lengthBuf];
      
      // read host name
      in.readFully(buf, 0, lengthHost);
      String host = new String(buf, 0, lengthHost, "ASCII");

if (debug >= cMsgConstants.debugInfo) {
  System.out.println("host = " + host);
}      
      // read client's name
      in.readFully(buf, 0, lengthName);
      String name = new String(buf, 0, lengthName, "ASCII");

if (debug >= cMsgConstants.debugInfo) {
  System.out.println("name = " + name);
}      
      // Try to register this client. If the cMsg system already has a
      // client by this name, it will fail.
      try {
if (debug >= cMsgConstants.debugInfo) {
  System.out.println("register client");
}      
        cMsgClientInfo info = new cMsgClientInfo(clientListeningPort, host);
        server.registerClient(name, info);
      
	// send ok to client
        out.writeInt(cMsgConstants.ok);

        // send cMsg domain host & port contact info back to client
        out.writeInt(info.domainPort);
        out.writeInt(info.domainHost.length());
        out.write(info.domainHost.getBytes("ASCII"));
        out.flush();

      }
      catch (cMsgException ex) {
	// send error to client
        out.writeInt(cMsgConstants.errorNameExists);
        out.flush();
      }

      return;
    }
    catch (IOException ex) {
      if (debug >= cMsgConstants.debugError) {
	System.out.println("cMsgNameServer's Client thread: IO error in talking to domain client");
      }
    }
    finally {
      // we are done with the socket
      try {sock.close();}
      catch (IOException ex) {}
    }
  }
}

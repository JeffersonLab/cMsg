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
import org.jlab.coda.cMsg.*;

/**
 * This class implements a thread which listens for connections to a cMsg
 * name server for a particular cMsg domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgDomainServerListeningThread extends Thread {
  
  /** cMsgDomainServer object which spawned this thread. */
  cMsgDomainServer server;
  
  /**
   * Object containing information about the domain client.
   * Certain members of info can only be filled in by this thread,
   * such as the listening port & host.
   */
  cMsgClientInfo info;
  
  /** Listening socket. */
  ServerSocket listeningSocket;
  
  /** Port number to listen on. */
  private int port;
  
  /** Host this is running on. */
  private String host;
      
  /** Level of debug output for this class. */
  private int debug;
  
  /** Get listening port of this object. */
  public int    getPort() {return port;}
  
  /** Get local host running this object. */
  public String getHost() {return host;}
  
  /** Print the listening port of this object to std out. */
  public void printPort() {System.out.println("" + port);}
  
  /** Print the host running this object to std out. */
  public void printHost() {System.out.println(host);}
  

  /**
   * Creates a new cMsgDomainServer object.
   *
   * @param server domain server object which started this thread
   * @param info object containing information about the doman server's client
   * @param startingPort suggested port on which to starting listening for connections
   * @param debug level of debug output for this object
   * @exception cMsgException If a port to listen on could not be found
   */
  cMsgDomainServerListeningThread(cMsgDomainServer server,
                                  cMsgClientInfo info,
                                  int startingPort, int debug) throws cMsgException {
      // Port number to listen on
      port = startingPort;
      this.info = info;
      this.server = server;
      this.debug = debug;

      // At this point, find a port to bind to. If that isn't possible, throw
      // an exception. We want to do this in the constructor, because it's much
      // harder to do it in a separate thread and still report back the results.

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
                  throw new cMsgException("Exiting Server: cannot find port to listen on");
              }
          }
      }

      // fill in info members
      info.domainPort = port;
      try {
          host = InetAddress.getLocalHost().getHostName();
      }
      catch (UnknownHostException ex) {
      }
      info.domainHost = host;

      // If this is a Runtime.exec'ed process, stdout will be sent back
      // to the Java object which spawned this one.
      printHost();
      printPort();

  }

    /**
     * This method is executed as a thread.
     */
    public void run() {
        System.out.println("Running Server");

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
                // Set reading timeout to 1 second so dead clients
                // can be found by reading on a socket.
                sock.setSoTimeout(1000);

                // Set tcpNoDelay so no packets are delayed
                sock.setTcpNoDelay(true);
                // set buffer size
                sock.setReceiveBufferSize(65535);
                sock.setSendBufferSize(65535);
                // create thread to deal with client
                cMsgDomainServerClientThread connection =
                        new cMsgDomainServerClientThread(server, sock, debug);
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
class cMsgDomainServerClientThread extends Thread {

    /** Name server object. */
    private cMsgDomainServer server;

    private int debug;

    /** Tcp socket. */
    private Socket sock;
    /** Input stream from the socket. */
    private InputStream sockIn;
    /** Output stream from the socket. */
    private OutputStream sockOut;
    /**
     * Data input stream built on top of the socket's input stream (with an
     * intervening buffered input stream).
     */
    private DataInputStream in;
    /**
     * Data output stream built on top of the socket's output stream (with an
     * intervening buffered output stream).
     */
    private DataOutputStream out;

    /**
     * Create a new cMsgDomainServerClientThread object.
     *
     * @param server name server object.
     * @param sock   Tcp socket.
     * @param debug  level of debug output for this object
     */
    cMsgDomainServerClientThread(cMsgDomainServer server, Socket sock, int debug) {
        this.server = server;
        this.sock = sock;
        this.debug = debug;
    }

    /**
     * Start thread to handle communications with user.
     */
    public void run() {

        try {
            // buffered communication streams for efficiency
            sockIn = sock.getInputStream();
            sockOut = sock.getOutputStream();
            in = new DataInputStream(new BufferedInputStream(sock.getInputStream(), 65535));
            out = new DataOutputStream(new BufferedOutputStream(sock.getOutputStream(), 65535));

            /* wait for and process client requests */
            commandLoop();

            return;
        }
        catch (IOException ex) {
            if (debug >= cMsgConstants.debugError) {
                System.out.println("Tcp Server: IO error in client etOpen");
            }
        }
        finally {
            // we are done with the socket
            try {
                sock.close();
            }
            catch (IOException ex) {
            }
        }
    }


    /**
     * Wait for and implement commands from the user.
     */
    private void commandLoop() throws IOException {
        int msgId;

        // The Command Loop ...
        main: while (true) {
            // First, read the remote command. Remember, the
            // socket has a read timeout of 1 second.
            while (true) {
                try {
                    msgId = in.readInt();
                    break;
                }
                        // socket read timeout
                catch (InterruptedIOException ex) {
                    // check to see if we've been commanded to die
                    if (server.getKillAllThreads()) {
                        return;
                    }
                }
            }

            switch (msgId) {

                case cMsgConstants.msgSendRequest: // receiving a message
                    handleIncomingMessage();
                    break;

                case cMsgConstants.msgSubscribeRequest: // receiving a message
                    handleSubscribeRequest();
                    break;

                case cMsgConstants.msgUnsubscribeRequest: // receiving a message
                    handleUnsubscribeRequest();
                    break;


                case cMsgConstants.msgShutdown: // told to shutdown
                    break main;

                default:
                    break;

            }

        } // while(true)

        // We only end up down here if the client has quit or crashed.

        if (debug >= cMsgConstants.debugWarn) {
            System.out.println("commandLoop: remote client connection broken");
        }

        return;
    }


    private void handleIncomingMessage() throws IOException {

        // create a message
        cMsgMessage msg = new cMsgMessage();

        // read listening port of message subject
        int lengthSubject = in.readInt();

        // read length of message type
        int lengthType = in.readInt();

        // read length of message text
        int lengthText = in.readInt();

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("sendRequest:" + " lenSubject = " + lengthSubject +
                               ", lenType = " + lengthType + ", lenText = " + lengthText);
        }

        // allocate buffer
        int lengthBuf = lengthSubject > lengthType ? lengthSubject : lengthType;
        lengthBuf = lengthBuf > lengthText ? lengthBuf : lengthText;
        byte[] buf = new byte[lengthBuf];

        // read subject
        in.readFully(buf, 0, lengthSubject);
        msg.setSubject(new String(buf, 0, lengthSubject, "ASCII"));

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("subject = " + msg.getSubject());
        }
        // read type
        in.readFully(buf, 0, lengthType);
        msg.setType(new String(buf, 0, lengthType, "ASCII"));

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("type = " + msg.getType());
        }
        // read text
        in.readFully(buf, 0, lengthText);
        msg.setText(new String(buf, 0, lengthText, "ASCII"));

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("text = " + msg.getText());
        }

        // send ok to client
        out.writeInt(cMsgConstants.ok);
        out.flush();

        return;
    }


    private void handleSubscribeRequest() {
    }


    private void handleUnsubscribeRequest() {
    }
  
  
}

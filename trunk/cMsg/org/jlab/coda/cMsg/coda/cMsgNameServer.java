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
import org.jlab.coda.cMsg.*;

/**
 * This class implements a cMsg name server for a particular cMsg domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgNameServer {

  /** Type of domain this is. */
  private String type = "CODA";
  
  /** Port number to listen on. */
  private int port;
  
  /** Port number from which to start looking for a suitable listening port. */
  private int startingPort;
  
  /** HashMap which stores all client information. */
  private HashMap clients = new HashMap(100);
  
  /** Thread which listens for name server clients to connect. */
  cMsgNameServerListeningThread listeningThread;
  
  /** Level of debug output for this class. */
  private int debug = cMsgConstants.debugInfo;
  
  /** Tell the server to kill spawned threads. */
  boolean killAllThreads;
  
  /**
   * Sets boolean to kill all spawned threads.
   * @param b setting to true will kill spawned threads
   */
  public void setKillAllThreads(boolean b) {killAllThreads = b;}
  
  /** Gets boolean value specifying whether to kill all spawned threads. */
  public boolean getKillAllThreads() {return killAllThreads;}
  
  /** Gets type of domain this object serves. */
  public String getType() {return type;}
  
  

  /** Constructor which reads environmental variables and starts threads. */
  public cMsgNameServer() {
    // read env variable for starting (desired) port number
    try {
      Jgetenv env = new Jgetenv();
      startingPort = Integer.parseInt(env.echo("CMSG_PORT"));
    }
    catch (NumberFormatException ex) {}
    catch (JgetenvException ex) {}
    
    // port #'s < 1024 are reserved
    if (startingPort < 1024) {
      startingPort = cMsgNetworkConstants.nameServerStartingPort;
    }
    
    // start up a listening thread
    listeningThread = new cMsgNameServerListeningThread(this, startingPort, debug);
    listeningThread.start();
  }
  
  
  /** Run as a stand-alone application. */
  public static void main(String[] args) {
    cMsgNameServer server = new cMsgNameServer();
  }
  

  /**
   * Method to register a client with this name server.
   *
   * @param name unique client name
   * @param info object containing information about the client
   * @exception cMsgException If a domain server could not be started for the client
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
   * @exception cMsgException If a domain server could not be started due to
   *                          not finding a port to listen on, or trouble
   *                          starting a separate process for the server
   */
  private void startDomainServer(cMsgClientInfo info)
                                                           throws cMsgException {
    cMsgDomainServer server = new cMsgDomainServer(info, cMsgNetworkConstants.domainServerStartingPort);
    info.server = server;

    return;
  }
  
}

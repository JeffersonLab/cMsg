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
import java.util.*;

/**
 * This class implements a cMsg name server for a particular cMsg domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgDomainServer {

  /** Port number to listen on. */
  private int port;
  
  /** Port number from which to start looking for a suitable listening port. */
  private int startingPort;
      
  /** Thread which listens for name server clients to connect. */
  cMsgDomainServerListeningThread listeningThread;
  
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
  
  
  
  /**
   * Constructor which reads environmental variables and starts threads.
   *
   * @param info object containing information about the client for which this
   *                    domain server was started
   * @param startingPort suggested port on which to starting listening for connections
   * @exception cMsgException If a port to listen on could not be found
   */
  public cMsgDomainServer(cMsgClientInfo info, int startingPort) throws cMsgException {
    this.startingPort = startingPort;
    
    // start up a listening thread
    listeningThread = new cMsgDomainServerListeningThread(this, info, startingPort, debug);
    listeningThread.start();
  }
  
  /** Run as a stand-alone application. */
  public static void main(String[] args) {
    cMsgClientInfo info = new cMsgClientInfo();
    try {
      cMsgDomainServer server = new cMsgDomainServer(info, cMsgConstants.domainServerStartingPort);
    }
    catch (cMsgException ex) {
    }
  }
  
  
}

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

package org.jlab.coda.cMsg.coda;

import java.io.*;
import java.net.*;
import java.lang.*;
import java.util.*;

/**
 * Class in which to store a domain's client information.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgClientInfo {
    int    clientPort;
    String clientHost;
    int    domainPort;
    String domainHost;
    cMsgDomainServer server;
    
    cMsgClientInfo() {}
    
    cMsgClientInfo(int port, String host) {
      clientPort = port;
      clientHost = host;
    }
}


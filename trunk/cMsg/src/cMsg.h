/*----------------------------------------------------------------------------*
 *
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    E.Wolin, 14-Jul-2004, Jefferson Lab                                     *
 *                                                                            *
 *    Authors: Elliott Wolin                                                  *
 *             wolin@jlab.org                    Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-7365             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *             Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 * Description:                                                               *
 *                                                                            *
 *  Defines cMsg API and return codes                                         *
 *                                                                            *
 *                                                                            *
 *----------------------------------------------------------------------------*/
 
/**
 * @file
 * This is the one necessary header file for all cMsg users.
 
 * <b>INTRODUCTION</b>
 *
 * cMsg is a simple, abstract API to an arbitrary underlying message service. It 
 * is powerful enough to support synchronous and asynchronous point-to-point and 
 * publish/subscribe communication, and network-accessible message queues.  Note 
 * that a given underlying implementation may not necessarily implement all these 
 * features.  
 *
 *
 * <b>DOMAINS</b>
 *
 * The abstraction relies on the important concept of a "domain", specified via a 
 * "Universal Domain Locator" (UDL) of the form:
 * 
 * <code><b>cMsg:domainType://domainInfo</b></code>
 *
 * The domain type refers to an underlying messaging software implementation, 
 * and the domain info is interpreted by the implementation. Generally domains with
 * different UDL's are isolated from each other, but this is not necessarily the 
 * case.  For example, users can easily create gateways between different domains,
 * or different domain servers may serve into the same messaging namespace.
 * 
 * The full domain specifier for the full cMsg domain looks like:
 *
 * <code><b>cMsg:cMsg://node:port/cMsg/namespace?param1=val1(&param2=val2)</b></code>
 *
 * where node:port correspond to the node and port of a cMsg nameserver, and 
 * namespace allows for multiple namespaces on the same server.  If the port is missing 
 * a default port is used.  Parameters are optional and not specified at this time.
 * Currently different cMsg domains are completely isolated from each other. A
 * process can connect to multiple domains if desired. 
 *
 *
 * <b>MESSAGES</b>
 *
 * Messages are sent via cMsgSend() and related functions.  Messages have a type and are 
 * sent to a subject, and both are arbitrary strings.  The payload consists of
 * a single text string.  Users must call cMsgFlush() to initiate delivery of messages 
 * in the outbound send queues, although the implementation may deliver messages 
 * before cMsgFlush() is called.  Additional message meta-data may be set by the user
 * (see below), although much of it is set by the system.
 *
 * Message consumers ask the system to deliver messages to them that match various 
 * subject/type combinations (each may be NULL).  The messages are delivered 
 * asynchronously to callbacks (via cMsgSubscribe()).  cMsgFreeMessage() must be 
 * called when the user is done processing the message.  Synchronous or RPC-like 
 * messaging is also possible via cMsgSendAndGet().
 *
 * cMsgReceiveStart() must be called to start delivery of messages to callbacks.  
 *
 * In the cMsg domain perl-like subject wildcard characters are supported, multiple 
 * callbacks for the same subject/type are allowed, and each callback executes in 
 * its own thread.
 *
 *

 * <b>ADDITIONAL INFORMATION</b>
 *
 * See the cMsg User's Guide and the cMsg Developer's Guide for more information.
 * See the cMsg Doxygen and Java docs for the full API specification.
 *
 */


#ifndef _cMsg_h
#define _cMsg_h


/* This file may be compiled by either the C or C++ compiler.
 * It implements the C version of cMsgCallback (function pointer).
 *
 * EJW, 21-Mar-2005
 */

typedef void (cMsgCallback) (void *msg, void *userArg);


/** remaining definitions found here */
#include <cMsgBase.h>


#endif /* _cMsg_h */

/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Author:  Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12H        *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;

/**
 * This interface defines some useful constants. These constants correspond
 * to identical constants defined in the C implementation of cMsg.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgConstants {

  private cMsgConstants() {}

  /** Major cMsg version number. */
  public static final int    version      = 1;
  /** Minor cMsg version number. */
  public static final int    minorVersion = 0;

  // constants from cMsgPrivate.h

  /** Print out no status messages. */
  public static final int    debugNone           = 0;
  /** Print out only severe error messages. */
  public static final int    debugSevere         = 1;
  /** Print out severe and normal error messages. */
  public static final int    debugError          = 2;
  /** Print out all error and warning messages. */
  public static final int    debugWarn           = 3;
  /** Print out all error, warning, and informational messages. */
  public static final int    debugInfo           = 4;

  // C language cMsg error codes from cMsg.h

  /** No error. */
  public static final int    ok                      =  0;
  /** General error. */
  public static final int    error                   =  1;
  /** Error specifying a time out. */
  public static final int    errorTimeout            =  2;
  /** Specifying a feature not implemented. */
  public static final int    errorNotImplemented     =  3;
  /** Specifying a bad argument. */
  public static final int    errorBadArgument        =  4;
  /** Specifying a bad format. */
  public static final int    errorBadFormat          =  5;
  /** Specifying a domain type that does not exist or is not supported. */
  public static final int    errorBadDomainType      =  6;
  /** Specifying that a name already exists. */
  public static final int    errorNameExists         =  7;
  /** Error since not initialized. */
  public static final int    errorNotInitialized     =  8;
  /** Error since already initialized. */
  public static final int    errorAlreadyInitialized =  9;
  /** Error since lost network connection. */
  public static final int    errorLostConnection     = 10;
  /** Error due to network communication problem. */
  public static final int    errorNetwork            = 11;
  /** Error due to bad socket specification. */
  public static final int    errorSocket             = 12;
  /** Error waiting for messages to arrive. */
  public static final int    errorPend               = 13;
  /** Specifying illegal message type. */
  public static final int    errorIllegalMessageType = 14;
  /** Error due to no more computer memory. */
  public static final int    errorNoMemory           = 15;
  /** Specifying out-of-range parameter. */
  public static final int    errorOutOfRange         = 16;
  /** Error due to a limit that was exceeded. */
  public static final int    errorLimitExceeded      = 17;
  /** Error due to not matching any existing domain. */
  public static final int    errorBadDomainId        = 18;
  /** Error due to message not being in the correct form. */
  public static final int    errorBadMessage         = 19;
  /** Specifying a different domain type than expected. */
  public static final int    errorWrongDomainType    = 20;
  /** Error due to no Java class found for specified subdomain. */
  public static final int    errorNoClassFound       = 21;

  // codes sent over the network to identify cMsg messages

  /** Connect to the server. */
  public static final int    msgConnectRequest     =  0;
  /** Disconnect from the server. */
  public static final int    msgDisconnectRequest  =  1;
  /** Respond to the server. */
  public static final int    msgServerResponse     =  2;
  /** See if the other end of the socket is still open. */
  public static final int    msgKeepAlive          =  3;
  /** Exit your cMsg-related methods and threads. */
  public static final int    msgShutdown           =  4;
  /** Get a message. */
  public static final int    msgGetRequest         =  5;
  /** Respond to a "get message" command. */
  public static final int    msgGetResponse        =  6;
  /** Send a message. */
  public static final int    msgSendRequest        =  7;
  /** Respond to a "send message" command. */
  public static final int    msgSendResponse       =  8;
  /** Subscribe to messages. */
  public static final int    msgSubscribeRequest   =  9;
  /** Unsubscribe to messages. */
  public static final int    msgUnsubscribeRequest = 10;
  /** Respond to the subscribe/unsubscribe commands. */
  public static final int    msgSubscribeResponse  = 11;

}

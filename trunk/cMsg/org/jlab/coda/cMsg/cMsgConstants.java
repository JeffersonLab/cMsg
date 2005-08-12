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
    public static final int    version      = 0;
    /** Minor cMsg version number. */
    public static final int    minorVersion = 9;

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

    // endianness constants in cMsgBase.h

    /** Data is big endian. */
    public static final int    endianBig           = 0;
    /** Data is little endian. */
    public static final int    endianLittle        = 1;
    /** Data's endian is the same as the local host's. */
    public static final int    endianLocal         = 2;
    /** Data's endian is opposite of the local host's. */
    public static final int    endianNotLocal      = 3;
    /** Switch recorded value of data's endianness. */
    public static final int    endianSwitch        = 4;


    // C language cMsg error codes from cMsgBase.h

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
    /** Specifying that a unique item already exists. */
    public static final int    errorAlreadyExists      =  7;
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
    /** Error due to being different version. */
    public static final int    errorDifferentVersion   = 22;

    // Codes sent by the client over the network to
    // specify a particular request.

    /** Connect to the server from client. */
    public static final int    msgConnectRequest               =  0;
    /** Disconnect client from the server. */
    public static final int    msgDisconnectRequest            =  1;
    /** See if the process on the other end of the socket is still alive. */
    public static final int    msgKeepAlive                    =  2;
    /** Shutdown various clients and servers. */
    public static final int    msgShutdown                     =  3;
    /** Send a message. */
    public static final int    msgSendRequest                  =  4;
    /** Send a message with synchronous response. */
    public static final int    msgSyncSendRequest              =  5;
    /** Subscribe to messages. */
    public static final int    msgSubscribeRequest             =  6;
    /** Unsubscribe to messages. */
    public static final int    msgUnsubscribeRequest           =  7;
    /** Get a message with 1-shot subscribe. */
    public static final int    msgSubscribeAndGetRequest       =  8;
    /** Remove "subscribeAndGet" request. */
    public static final int    msgUnSubscribeAndGetRequest     =  9;
    /** Get a message from a responder of a sent message. */
    public static final int    msgSendAndGetRequest            = 10;
    /** Remove "sendAndGet" request. */
    public static final int    msgUnSendAndGetRequest          = 11;

    // Codes sent by the server to a client over the
    // network to specify a particular response.

    /** Respond to a "sendAndGet" request. */
    public static final int    msgGetResponse              =  20;
    /** Respond to a "sendAndGet" command is null. */
    public static final int    msgGetResponseIsNull        =  21;
    /** Respond with message to the subscribe command. */
    public static final int    msgSubscribeResponse        =  22;

    // Codes sent by a server's "client" connection to another server
    // in the cMsg subdomain only

    /** Subscribe to messages by another server. */
    public static final int    msgServerSubscribeRequest       = 30;
    /** Get a message with 1-shot subscribe by another server. */
    public static final int    msgServerSubscribeAndGetRequest = 31;
    /** Connect to the server from server. */
    public static final int    msgServerConnectRequest         = 32;
    /** Disconnect server from the server. */
    public static final int    msgServerDisconnectRequest      = 33;
    /** Register client from another server. */
    public static final int    msgServerRegisterClient         = 34;
    /** Unregister client from another server. */
    public static final int    msgServerUnRegisterClient       = 35;
    /** Send names of local clients. */
    public static final int    msgServerSendClientNames        = 36;
    /** Lock server from doing other registrations. */
    public static final int    msgServerRegistrationLock       = 37;
    /** Unlock server for doing other registrations. */
    public static final int    msgServerRegistrationUnlock     = 38;
    /** Lock server from joining server cloud or accepting registrations. */
    public static final int    msgServerCloudLock              = 39;
    /** Unlock server for joining server cloud or accepting registrations. */
    public static final int    msgServerCloudUnlock            = 40;
    /** Unlock server for joining server cloud or accepting registrations. */
    public static final int    msgServerCloudJoin              = 41;

    // Flags for client's shutdown method.

    /** Do NOT exclude the client calling shutdown from being shutdown. */
    public static final int    includeMe = 1;

}

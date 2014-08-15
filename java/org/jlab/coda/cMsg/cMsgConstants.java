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

    // constants from cMsgPrivate.h

    /** Major cMsg version number = {@value} */
    public static final int    version      = 3;
    /** Minor cMsg version number = {@value} */
    public static final int    minorVersion = 6;

    // constants from cMsgConstants.h

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


    /** Client has medium data/message throughput (default). */
    public static final int    regimeMedium        = 0;
    /** Client has low data/message throughput. */
    public static final int    regimeLow           = 1;
    /** Client has high data/message throughput. */
    public static final int    regimeHigh          = 2;
    /** Default maximum number of clients per cMsgDomainServerSelect object in regimeLow mode. */
    public static final int    regimeLowMaxClients = 10;


    /** Failover of client can go to any of the UDLs given in connect (default). */
    public static final int    failoverAny         = 0;
    /** Failover of client will go to servers in the cloud first, if none are available,
     *  the client can go to any of the UDLs given in connect. */
    public static final int    failoverCloud       = 1;
    /** Failover of client will only go to servers in the cloud. */
    public static final int    failoverCloudOnly   = 2;


    /** Failover of client to a server in the cloud will go to any of the cloud servers (default). */
    public static final int    cloudAny            = 0;
    /** Failover of client to a server in the cloud will go to a local cloud server first
     *  before others are considered. */
    public static final int    cloudLocal          = 1;


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
    /** Error due to wrong password. */
    public static final int    errorWrongPassword      = 23;
    /** Error due to server dying. */
    public static final int    errorServerDied         = 24;
    /** Error due to aborted procedure. */
    public static final int    errorAbort              = 25;

    // Codes sent by the client over the network to
    // specify a particular request from cMsgPrivate.h .

    /** Connect to the server from client. */
    public static final int    msgConnectRequest               =  0;
    /** Disconnect client from the server. */
    public static final int    msgDisconnectRequest            =  1;
    /** See if the process on the other end of the socket is still alive. */
    public static final int    msgKeepAlive                    =  2;
    /** Shutdown various clients. */
    public static final int    msgShutdownClients              =  3;
    /** Shutdown various servers. */
    public static final int    msgShutdownServers              =  4;
    /** Send a message. */
    public static final int    msgSendRequest                  =  5;
    /** Send a message with synchronous response. */
    public static final int    msgSyncSendRequest              =  6;
    /** Subscribe to messages. */
    public static final int    msgSubscribeRequest             =  7;
    /** Unsubscribe to messages. */
    public static final int    msgUnsubscribeRequest           =  8;
    /** Get a message with 1-shot subscribe. */
    public static final int    msgSubscribeAndGetRequest       =  9;
    /** Remove "subscribeAndGet" request. */
    public static final int    msgUnsubscribeAndGetRequest     = 10;
    /** Get a message from a responder of a sent message. */
    public static final int    msgSendAndGetRequest            = 11;
    /** Remove "sendAndGet" request. */
    public static final int    msgUnSendAndGetRequest          = 12;
    /** Send "monitor" request. */
    public static final int    msgMonitorRequest               = 13;

    // Codes sent by the server to a client over the
    // network to specify a particular response from cMsgPrivate.h .

    /** Respond to a "sendAndGet" request. */
    public static final int    msgGetResponse                    =  20;
    /** Respond with message to the subscribe command. */
    public static final int    msgSubscribeResponse              =  21;
    /** Respond to a server's "sendAndGet" request. */
    public static final int    msgServerGetResponse              =  22;
    /** Respond to a runcontrol server's "connect" request. */
    public static final int    msgRcConnect                      =  23;
    /** Respond to a runcontrol server's "connect" request. */
    public static final int    msgRcAbortConnect                 =  24;
    /** Send a response to a syncSend request. */
    public static final int    msgSyncSendResponse               =  25;
    /** Send a response to a server client's "sendClientNames" request. */
    public static final int    msgServerSendClientNamesResponse  =  26;
    /** Send a response to a server client's "cloud lock" request. */
    public static final int    msgServerCloudLockResponse        =  27;
    /** Send a response to a server client's "registration lock" request. */
    public static final int    msgServerRegistrationLockResponse =  28;

    // Codes sent by a server's "client" connection to another server
    // in the cMsg subdomain only (no C counterpart).

    /** Subscribe to messages by another server. */
    public static final int    msgServerSubscribeRequest         = 30;
    /** Unsubscribe to messages by another server. */
    public static final int    msgServerUnsubscribeRequest       = 31;
    /** SubscribeAndGet a message by another server. */
    public static final int    msgServerSubscribeAndGetRequest   = 32;
    /** UnsubscribeAndGet to a message by another server. */
    public static final int    msgServerUnsubscribeAndGetRequest = 33;
    /** Connect to the server from server. */
    public static final int    msgServerConnectRequest           = 34;
    /** Disconnect server from the server. */
    public static final int    msgServerDisconnectRequest        = 35;
    /** Register client from another server. */
    public static final int    msgServerRegisterClient           = 36;
    /** Unregister client from another server. */
    public static final int    msgServerUnRegisterClient         = 37;
    /** Send names of local clients. */
    public static final int    msgServerSendClientNames          = 38;
    /** Lock server from doing other registrations. */
    public static final int    msgServerRegistrationLock         = 39;
    /** Unlock server for doing other registrations. */
    public static final int    msgServerRegistrationUnlock       = 40;
    /** Lock server from joining server cloud or accepting registrations. */
    public static final int    msgServerCloudLock                = 41;
    /** Unlock server for joining server cloud or accepting registrations. */
    public static final int    msgServerCloudUnlock              = 42;
    /** Unlock server for joining server cloud or accepting registrations. */
    public static final int    msgServerCloudSetStatus           = 43;
    /** SendAndGet a message by another server. */
    public static final int    msgServerSendAndGetRequest        = 44;
    /** UnSendAndGet to a message by another server. */
    public static final int    msgServerUnSendAndGetRequest      = 45;
    /** Shutdown various clients. */
    public static final int    msgServerShutdownClients          = 46;
    /** Shutdown various servers. */
    public static final int    msgServerShutdownSelf             = 47;

    // Flags for client's shutdown methods in cmsgConstants.h
    
    /**
     * For shutdownClients,
     * do NOT exclude the client calling shutdown from being shutdown.
     */
    public static final int    includeMe = 1;
    /**
     * For shutdownServers,
     * do NOT exclude the server that the calling client is connected to
     * from being shutdown.
     */
    public static final int    includeMyServer = 2;

    // Types of data contained in a message's payload in cMsgConstants.h

    /** String item in cMsg message payload. */
    public static final int payloadStr    = 10;
    /** 4-byte float item in cMsg message payload. */
    public static final int payloadFlt    = 11;
    /** 8 byte double item in cMsg message payload. */
    public static final int payloadDbl    = 12;
    /** 1 byte signed integer item in cMsg message payload. */
    public static final int payloadInt8   = 13;
    /** 2 byte signed integer item in cMsg message payload. */
    public static final int payloadInt16  = 14;
    /** 4 byte signed integer item in cMsg message payload. */
    public static final int payloadInt32  = 15;
    /** 8 byte signed integer item in cMsg message payload. */
    public static final int payloadInt64  = 16;
    /** 1 byte unsigned integer item in cMsg message payload. */
    public static final int payloadUint8  = 17;
    /** 2 byte unsigned integer item in cMsg message payload. */
    public static final int payloadUint16 = 18;
    /** 4 byte unsigned integer item in cMsg message payload. */
    public static final int payloadUint32 = 19;
    /** 8 byte unsigned integer item in cMsg message payload. */
    public static final int payloadUint64 = 20;
    /** cMsg message in cMsg message payload. */
    public static final int payloadMsg    = 21;
    /** Binary array in cMsg message payload. */
    public static final int payloadBin    = 22;

    // arrays ...
    /** String array item in cMsg message payload. */
    public static final int payloadStrA    = 23;
    /** 4-byte float array item in cMsg message payload. */
    public static final int payloadFltA    = 24;
    /** 8 byte double array item in cMsg message payload. */
    public static final int payloadDblA    = 25;
    /** 1 byte signed integer array item in cMsg message payload. */
    public static final int payloadInt8A   = 26;
    /** 2 byte signed integer array item in cMsg message payload. */
    public static final int payloadInt16A  = 27;
    /** 4 byte signed integer array item in cMsg message payload. */
    public static final int payloadInt32A  = 28;
    /** 8 byte signed integer array item in cMsg message payload. */
    public static final int payloadInt64A  = 29;
    /** 1 byte unsigned integer array item in cMsg message payload. */
    public static final int payloadUint8A  = 30;
    /** 2 byte unsigned integer array item in cMsg message payload. */
    public static final int payloadUint16A = 31;
    /** 4 byte unsigned integer array item in cMsg message payload. */
    public static final int payloadUint32A = 32;
    /** 8 byte unsigned integer array item in cMsg message payload. */
    public static final int payloadUint64A = 33;
    /** cMsg message array in cMsg message payload. */
    public static final int payloadMsgA    = 34;
    /** Array of binary arrays in cMsg message payload. */
    public static final int payloadBinA    = 35;


    // Commands in Emu domain
    public static final int    emuEvioFileFormat = 1;
    /** Will shutdown socket on other end. */
    public static final int    emuEnd            = 2;

    /** Must be same as org.jlab.coda.emu.support.data.EventType.ROC_RAW. */
    public static final int    emuEventTypeRoc  = 0;
    /** Must be same as org.jlab.coda.emu.support.data.EventType.PARTIAL_PHYSICS. */
    public static final int    emuEventTypePP   = 2;
    /** Must be same as org.jlab.coda.emu.support.data.EventType.CONTROL. */
    public static final int    emuEventTypeControl = 5;




}

/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 20-Aug-2004, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.client;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgSubscription;
import org.jlab.coda.cMsg.common.cMsgCallbackThread;
import org.jlab.coda.cMsg.common.cMsgGetHelper;
import org.jlab.coda.cMsg.common.cMsgMessageFull;

import java.io.*;
import java.util.*;
import java.net.Socket;

/**
 * <p>
 * This class implements a cMsg client's thread which listens for
 * communications from the domain server. The server sends it keep alives,
 * messages to which the client has subscribed, and other directives.
 * </p><p>
 * Note that the class org.jlab.cMsg.RCDomain.rcListeningThread is largely
 * the same as this one. If there are any changes made here the identical
 * changes should be made there as well.
 * </p>
 * @author Carl Timmer
 * @version 1.0
 */
class cMsgClientListeningThread extends Thread {

    /** Type of domain this is. */
    private String domainType = "cMsg";

    /** cMsg client that created this object. */
    private cMsg client;

    /** cMsg server client that created this object. */
    private cMsgServerClient serverClient;

    /** Server channel (contains socket). */
    private Socket socket;

    /** Socket input stream associated with channel. */
    DataInputStream in;

    /** Allocate byte array once (used for reading in data) for efficiency's sake. */
    byte[] bytes = new byte[65536];

    /** Level of debug output for this class. */
    private int debug;

    /** Setting this to true will kill this thread. */
    private boolean killThread;

    /** Kills this thread. */
    public void killThread() {
        killThread = true;
    }


    /**
     * Constructor for regular clients.
     *
     * @param myClient cMsg client that created this object
     * @param sock main communication socket with server
     */
    public cMsgClientListeningThread(cMsg myClient, Socket sock) throws IOException {

        client = myClient;
        socket = sock;
        debug = client.getDebug();

        // buffered communication streams for efficiency
        in = new DataInputStream(new BufferedInputStream(socket.getInputStream(), 65536));
        // die if no more non-daemon thds running
        setDaemon(true);
        setName("cMsg domain client listener");
    }


    /**
     * Constructor for server clients.
     *
     * @param myClient cMsg server client that created this object
     * @param sock main communication socket with server
     */
    public cMsgClientListeningThread(cMsgServerClient myClient, Socket sock) throws IOException {
        this((cMsg)myClient, sock);
        this.serverClient = myClient;
    }

    /**
     * If reconnecting to another server as part of a failover, we must change to
     * another channel.
     * TODO: How do we call this while waiting on read???
     *
     * @param sock main communication socket with server
     * @throws IOException if channel is closed
     */
    synchronized public void changeSockets(Socket sock) throws IOException {
        socket = sock;
        in = new DataInputStream(new BufferedInputStream(socket.getInputStream()));
    }




    /** This method is executed as a thread. */
    public void run() {

        try {
            while (true) {
                if (this.isInterrupted()) {
                    return;
                }

                // read first int -- total size in bytes
//System.out.println("cMsgClientListeningThread: Try reading size");
                int size = in.readInt();
//System.out.println("cMsgClientListeningThread: size = " + size);

                // read client's request
                int msgId = in.readInt();
//System.out.println("cMsgClientListeningThread: msgId = " + msgId);

                cMsgMessageFull msg;

                switch (msgId) {

                    case cMsgConstants.msgSubscribeResponse: // receiving a message
//System.out.println("cMsgClientListeningThread: got msg from server");
                        // read the message here
                        msg = readIncomingMessage();

                        // run callbacks for this message
                        runCallbacks(msg);

                        break;

                    case cMsgConstants.msgGetResponse:       // receiving a message for sendAndGet
                    case cMsgConstants.msgServerGetResponse: // server receiving a message for sendAndGet
//System.out.println("cMsgClientListeningThread: got sendAndGet response from server");
//                        if (debug >= cMsgConstants.debugInfo) {
//                            System.out.println("cMsgClientListeningThread: got sendAndGet response from server");
//                        }
                        // read the message here
                        msg = readIncomingMessage();
                        msg.setGetResponse(true);

                        // wakeup caller with this message
                        if (msgId == cMsgConstants.msgGetResponse) {
                            wakeGets(msg);
                        }
                        else {
                            runServerCallbacks(msg);
                        }

                        break;

                    case cMsgConstants.msgShutdownClients: // server told this client to shutdown
//System.out.println("cMsgClientListeningThread: got shutdown client response from server");
                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("cMsgClientListeningThread: got shutdown from server");
                        }

                        if (client.getShutdownHandler() != null) {
//System.out.println("cMsgClientListeningThread: run client's shutdown handler");
                            client.getShutdownHandler().handleShutdown();
                        }
                        break;

                    case cMsgConstants.msgSyncSendResponse: // receiving a couple ints for syncSend
                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("cMsgClientListeningThread: got syncSend response from server");
                        }
                        int response = in.readInt();
                        int ssid = in.readInt();
                        // notify waiter that sync send response is here
                        wakeSyncSends(response, ssid);
                        break;

                    case cMsgConstants.msgServerSendClientNamesResponse: // server told this server client its list of client names
                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("cMsgClientListeningThread: got getClientNamesAndNamespaces response from server");
                        }
                        String[] names = readClientNamesAndNamespaces();
                        // notify waiter that response is here
                        wakeGetClientNames(names);
                        break;

                    case cMsgConstants.msgServerCloudLockResponse: // server told this server client about grabbing cloud lock
                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("cMsgClientListeningThread: got clouldLock response from server");
                        }
                        response = in.readInt();
                        in.readInt();  // junk
                        // notify waiter that response is here
                        wakeCloudLock(response);
                        break;

                    case cMsgConstants.msgServerRegistrationLockResponse: // server told this server client about grabbing registration lock
                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("cMsgClientListeningThread: got registrationLock response from server");
                        }
                        response = in.readInt();
                        in.readInt();  // junk
                        // notify waiter that response is here
                        wakeRegistrationLock(response);
                        break;

                    default:
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("cMsgClientListeningThread: can't understand server message = " + msgId);
                        }
                        break;
                }
            }
        }
        catch (InterruptedIOException e) {
            if (debug >= cMsgConstants.debugError) {
                System.out.println("cMsgClientListeningThread: I/O interrupted reading from server");
            }
        }
        catch (IOException e) {
            if (debug >= cMsgConstants.debugError) {
                System.out.println("cMsgClientListeningThread: I/O ERROR reading from server");
            }
        }
        finally {
            // We're here if there is an IO error.
            // Hopefully the keepalive thread will take care of all difficulties from here
//            try {in.close();}      catch (IOException e1) {}
//            try {out.close();}     catch (IOException e1) {}
//            try {channel.close();} catch (IOException e1) {}
        }

        return;
    }


    /**
     * This method reads an incoming message from the server.
     * Currently not used.
     *
     * @param array array of data to parse for message
     * @return message read from channel
     * @throws IOException if error parsing array
     */
    private cMsgMessageFull readIncomingMessage(byte[] array) throws IOException {

        // create a message
        cMsgMessageFull msg = new cMsgMessageFull();

        // already read msgId
        int index = 4;

        msg.setVersion(cMsgUtilities.bytesToInt(array, index));     index += 4;
                                                                    index += 4;  // skip 4 bytes
        msg.setUserInt(cMsgUtilities.bytesToInt(array, index));     index += 4;
        // mark the message as having been sent over the wire & having expanded payload
        msg.setInfo(cMsgUtilities.bytesToInt(array, index) | cMsgMessage.wasSent | cMsgMessage.expandedPayload); index += 4;

        // time message was sent = 2 ints (hightest byte first)
        // in milliseconds since midnight GMT, Jan 1, 1970
        long time = ((long) cMsgUtilities.bytesToInt(array, index) << 32) |
                    ((long) cMsgUtilities.bytesToInt(array, index+4) & 0x00000000FFFFFFFFL);
        msg.setSenderTime(new Date(time));
        index += 8;

        // user time
        time = ((long) cMsgUtilities.bytesToInt(array, index) << 32) |
               ((long) cMsgUtilities.bytesToInt(array, index+4) & 0x00000000FFFFFFFFL);
        msg.setUserTime(new Date(time));
        index += 8;

        msg.setSysMsgId(cMsgUtilities.bytesToInt(array, index));    index += 4;
        msg.setSenderToken(cMsgUtilities.bytesToInt(array, index)); index += 4;

        // String lengths
        int lengthSender      = cMsgUtilities.bytesToInt(array, index);    index += 4;
        int lengthSenderHost  = cMsgUtilities.bytesToInt(array, index);    index += 4;
        int lengthSubject     = cMsgUtilities.bytesToInt(array, index);    index += 4;
        int lengthType        = cMsgUtilities.bytesToInt(array, index);    index += 4;
        int lengthPayloadTxt  = cMsgUtilities.bytesToInt(array, index);    index += 4;
        int lengthText        = cMsgUtilities.bytesToInt(array, index);    index += 4;
        int lengthBinary      = cMsgUtilities.bytesToInt(array, index);    index += 4;

        // read sender
        msg.setSender(new String(array, index, lengthSender, "US-ASCII"));
        //System.out.println("sender = " + msg.getSender());
        index += lengthSender;

        // read senderHost
        msg.setSenderHost(new String(array, index, lengthSenderHost, "US-ASCII"));
        //System.out.println("senderHost = " + msg.getSenderHost());
        index += lengthSenderHost;

        // read subject
        msg.setSubject(new String(array, index, lengthSubject, "US-ASCII"));
        //System.out.println("subject = " + msg.getSubject());
        index += lengthSubject;

        // read type
        msg.setType(new String(array, index, lengthType, "US-ASCII"));
        //System.out.println("type = " + msg.getType());
        index += lengthType;

        // read payload text
        if (lengthPayloadTxt > 0) {
            String s = new String(array, index, lengthPayloadTxt, "US-ASCII");
            // setting the payload text is done by setFieldsFromText
//System.out.println("payload text = " + s);
            index += lengthPayloadTxt;
            try {
                msg.setFieldsFromText(s, cMsgMessage.allFields);
            }
            catch (cMsgException e) {
                System.out.println("msg payload is in the wrong format: " + e.getMessage());
            }
        }

        // read text
        if (lengthText > 0) {
            msg.setText(new String(array, index, lengthText, "US-ASCII"));
            index += lengthText;
            //System.out.println("text = " + msg.getText());
        }

        // read binary array
        if (lengthBinary > 0) {
            try {
                msg.setByteArray(array, index, lengthBinary);
            }
            catch (cMsgException e) {
            }
        }

        // fill in message object's members
        msg.setDomain(domainType);
        msg.setReceiver(client.getName());
        msg.setReceiverHost(client.getHost());
        msg.setReceiverTime(new Date()); // current time
//System.out.println("MESSAGE RECEIVED");

        return msg;
    }


    /**
     * This method reads an incoming message from the server.
     *
     * @return message read from channel
     * @throws IOException if socket read or write error
     */
    private cMsgMessageFull readIncomingMessage() throws IOException {

        // create a message
        cMsgMessageFull msg = new cMsgMessageFull();
        msg.setVersion(in.readInt());

        // second incoming integer is for future use
        in.skipBytes(4);
        msg.setUserInt(in.readInt());
        // mark the message as having been sent over the wire & having expanded payload
        msg.setInfo(in.readInt() | cMsgMessage.wasSent | cMsgMessage.expandedPayload);

        // time message was sent = 2 ints (hightest byte first)
        // in milliseconds since midnight GMT, Jan 1, 1970
        long time = ((long) in.readInt() << 32) | ((long) in.readInt() & 0x00000000FFFFFFFFL);
        msg.setSenderTime(new Date(time));
        // user time
        time = ((long) in.readInt() << 32) | ((long) in.readInt() & 0x00000000FFFFFFFFL);
        msg.setUserTime(new Date(time));
        msg.setSysMsgId(in.readInt());
        msg.setSenderToken(in.readInt());
        // String lengths
        int lengthSender      = in.readInt();
        int lengthSenderHost  = in.readInt();
        int lengthSubject     = in.readInt();
        int lengthType        = in.readInt();
        int lengthPayloadText = in.readInt();
        int lengthText        = in.readInt();
        int lengthBinary      = in.readInt();

        // bytes expected
        int stringBytesToRead = lengthSender + lengthSenderHost + lengthSubject +
                lengthType + lengthPayloadText + lengthText;
        int offset = 0;

        // read all string bytes
        if (stringBytesToRead > bytes.length) {
            bytes = new byte[stringBytesToRead];
        }
        in.readFully(bytes, 0, stringBytesToRead);

        // read sender
        msg.setSender(new String(bytes, offset, lengthSender, "US-ASCII"));
        //System.out.println("sender = " + msg.getSender());
        offset += lengthSender;

        // read senderHost
        msg.setSenderHost(new String(bytes, offset, lengthSenderHost, "US-ASCII"));
        //System.out.println("senderHost = " + msg.getSenderHost());
        offset += lengthSenderHost;

        // read subject
        msg.setSubject(new String(bytes, offset, lengthSubject, "US-ASCII"));
        //System.out.println("subject = " + msg.getSubject());
        offset += lengthSubject;

        // read type
        msg.setType(new String(bytes, offset, lengthType, "US-ASCII"));
        //System.out.println("type = " + msg.getType());
        offset += lengthType;

        // read payload text
        if (lengthPayloadText > 0) {
            String s = new String(bytes, offset, lengthPayloadText, "US-ASCII");
            // setting the payload text is done by setFieldsFromText
//System.out.println("payload text = " + s);
            offset += lengthPayloadText;
            try {
                msg.setFieldsFromText(s, cMsgMessage.allFields);
            }
            catch (cMsgException e) {
                System.out.println("msg payload is in the wrong format: " + e.getMessage());
            }
        }

        // read text
        if (lengthText > 0) {
            msg.setText(new String(bytes, offset, lengthText, "US-ASCII"));
            offset += lengthText;
            //System.out.println("text = " + msg.getText());
        }

        // read binary array
        if (lengthBinary > 0) {
            byte[] b = new byte[lengthBinary];

            // read all binary bytes
            in.readFully(b, 0, lengthBinary);

            try {
                msg.setByteArrayNoCopy(b, 0, lengthBinary);
            }
            catch (cMsgException e) {
            }
        }

        // fill in message object's members
        msg.setDomain(domainType);
        msg.setReceiver(client.getName());
        msg.setReceiverHost(client.getHost());
        msg.setReceiverTime(new Date()); // current time
//System.out.println("MESSAGE RECEIVED");
        return msg;
    }

    /**
     * This method reads the names and namespaces of all the local clients (not servers)
     * of another cMsg domain server.
     *
     * @return array of client names and namespaces
     * @throws IOException if communication error with server
     */
    private String[] readClientNamesAndNamespaces() throws IOException {

        String[] names;

        int offset = 0;
        int stringBytesToRead = 0;

        // read how many strings are coming
        int numberOfStrings = in.readInt();

        int[] lengths = new int[numberOfStrings];
        names = new String[numberOfStrings];

        // read lengths of all names being sent
        for (int i=0; i < numberOfStrings; i++) {
            lengths[i] = in.readInt();
            stringBytesToRead += lengths[i];
        }

        // read all string bytes
        byte[] bytes = new byte[stringBytesToRead];
        in.readFully(bytes, 0, stringBytesToRead);

        // change bytes to strings
        for (int i=0; i < numberOfStrings; i++) {
            names[i] = new String(bytes, offset, lengths[i], "US-ASCII");
            offset += lengths[i];
        }

        return names;
    }



    /**
     * This method runs all appropriate callbacks - each in their own thread -
     * for client subscribe and subscribeAndGet calls.
     *
     * @param msg incoming message
     */
    private void runCallbacks(cMsgMessageFull msg)  {

//System.out.println("subAndGets size = " + client.subscribeAndGets.size());
        if (client.subscribeAndGets.size() > 0) {
            // for each subscribeAndGet called by this client ...
            cMsgSubscription sub;
            for (Iterator i = client.subscribeAndGets.values().iterator(); i.hasNext();) {
                sub = (cMsgSubscription) i.next();
                if (sub.matches(msg.getSubject(), msg.getType())) {
//System.out.println("runCallbacks: tell sub&Get to wake");

                    sub.setTimedOut(false);
                    sub.setMessage(msg.copy());
                    // Tell the subscribeAndGet-calling thread to wakeup
                    // and retrieve the held msg
                    synchronized (sub) {
                        sub.notify();
                    }
                }
                i.remove();
            }
        }

        // handle subscriptions, map not modified here
        Map<cMsgSubscription, String> map = client.subscriptions;

        if (map.size() > 0) {
            // if callbacks have been stopped, return
            if (!client.isReceiving()) {
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("runCallbacks: all subscription callbacks have been stopped");
                }
                return;
            }

            // for each subscription of this client ...
            for (cMsgSubscription sub : map.keySet()) {
                // if subject & type of incoming message match those in subscription ...
                if (sub.matches(msg.getSubject(), msg.getType())) {
                    // run through all callbacks
                    for (cMsgCallbackThread cbThread : sub.getCallbacks()) {
                        // The callback thread copies the message given
                        // to it before it runs the callback method on it.
//System.out.println("runCallbacks: send msg to " + cbThread.getName());
                        cbThread.sendMessage(msg);
                    }
                }
            }
        }
//System.out.println("runCallbacks: End");
    }


    /**
     * This method wakes up a thread in a server client waiting in the sendAndGet method
     * and delivers a message to it.
     *
     * @param msg incoming message
     */
    private void runServerCallbacks(cMsgMessageFull msg)  {

        // Get thread waiting on sendAndGet response from another cMsg server.
        // Remove it from table.
        cMsgSendAndGetCallbackThread cbThread =
                serverClient.serverSendAndGets.remove(msg.getSenderToken());

        // Remove future object for thread waiting on sendAndGet response from
        // another cMsg server.  Remove it from table.
        serverClient.serverSendAndGetCancel.remove(msg.getSenderToken());

        if (cbThread == null) {
            return;
        }

        cbThread.sendMessage(msg);
    }


    /**
     * This method wakes up a thread in a regular client waiting in the sendAndGet method
     * and delivers a message to it.
     *
     * @param msg incoming message
     */
    private void wakeGets(cMsgMessageFull msg) {

//System.out.println("sendAndGets size = " + client.sendAndGets.size());
        cMsgGetHelper helper = client.sendAndGets.remove(msg.getSenderToken());

        if (helper == null) {
            return;
        }
        helper.setTimedOut(false);
        // Do NOT need to copy msg as only 1 receiver gets it
        helper.setMessage(msg);

        // Tell the sendAndGet-calling thread to wakeup and retrieve the held msg
        synchronized (helper) {
            helper.notify();
        }
    }

    /**
     * This method wakes up a thread in a regular client waiting in the syncSend method
     * and delivers an integer to it.
     *
     * @param response returned int from subdomain handler
     * @param ssid syncSend id
     */
    private void wakeSyncSends(int response, int ssid) {

//System.out.println("syncSends size = " + client.syncSends.size());
        cMsgGetHelper helper = client.syncSends.remove(ssid);
        if (helper == null) {
//System.out.println("wakeSyncSends: helper is null");
            return;
        }
        // Tell the syncSend-calling thread to wakeup and retrieve the held int
        synchronized (helper) {
            helper.setTimedOut(false);
            helper.setIntVal(response);
            helper.notify();
        }
    }

    /**
     * This method wakes up a thread in a server client waiting in the getClientNamesAndNamespaces method
     * and delivers a String array to it.
     *
     * @param names returned client names and namespaces from subdomain handler
     */
    private void wakeGetClientNames(String[] names) {

        serverClient.clientNamesAndNamespaces = names;
        cMsgGetHelper helper = serverClient.clientNamesHelper;
        if (helper == null) {
            return;
        }

        // Tell the getClientNamesAndNamesapces-calling thread to wakeup and retrieve info
        synchronized (helper) {
            helper.notify();
        }
    }

    /**
     * This method wakes up a thread in a server client waiting in the cloudLock method
     * and delivers an integer to it.
     *
     * @param response returned response from cloudLock
     */
    private void wakeCloudLock(int response) {

        serverClient.gotCloudLock.set(response == 1);
        cMsgGetHelper helper = serverClient.cloudLockHelper;
        if (helper == null) {
            return;
        }

        // Tell the cloudLock-calling thread to wakeup and retrieve info
        synchronized (helper) {
            helper.notify();
        }
    }

    /**
     * This method wakes up a thread in a server client waiting in the registrationLock method
     * and delivers an integer to it.
     *
     * @param response returned response from registrationLock
     */
    private void wakeRegistrationLock(int response) {

        serverClient.gotRegistrationLock.set(response == 1);
        cMsgGetHelper helper = serverClient.registrationLockHelper;
        if (helper == null) {
            return;
        }

        // Tell the cloudLock-calling thread to wakeup and retrieve info
        synchronized (helper) {
            helper.notify();
        }
    }

}



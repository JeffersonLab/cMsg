/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 10-Nov-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;

import java.util.List;
import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.net.InetSocketAddress;
import java.net.Socket;

/**
 * This abstract class is essentially an interface for an object
 * that a domain server uses to respond to client demands, with a
 * couple of methods implemented to hide the details of communication
 * with the client. An fully implementated subclass of this abstract
 * class must handle all communication with a particular subdomain
 * (such as SmartSockets or JADE agents).
 * <p/>
 * Understand that each client using cMsg will have its own handler object
 * from either an implemenation of the cMsgSubdomainHandler interface or a
 * subclass of this class. One client may concurrently use the same
 * cMsgHandleRequest object; thus, implementations must be thread-safe.
 * Furthermore, when the name server shuts dowm, the method handleServerShutdown
 * may be executed more than once for the same reason.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public abstract class cMsgSubdomainAbstract implements cMsgSubdomainHandler {
    /**
     * Method to give the subdomain handler the appropriate part
     * of the UDL the client used to talk to the domain server.
     *
     * @param UDLRemainder last part of the UDL appropriate to the subdomain handler
     * @throws org.jlab.coda.cMsg.cMsgException
     *
     */
    public abstract void setUDLRemainder(String UDLRemainder) throws cMsgException;


    /**
     * Method to register domain client.
     *
     * @param info information about client
     * @throws cMsgException
     */
    public abstract void registerClient(cMsgClientInfo info) throws cMsgException;


    /**
     * Method to handle message sent by domain client.
     *
     * @param message message from sender
     * @throws cMsgException
     */
    public abstract void handleSendRequest(cMsgMessageFull message) throws cMsgException;


    /**
     * Method to handle message sent by domain client in synchronous mode.
     * It requries an integer response from the subdomain handler.
     *
     * @param message message from sender
     * @return response from subdomain handler
     * @throws cMsgException
     */
    public abstract int handleSyncSendRequest(cMsgMessageFull message) throws cMsgException;


    /**
     * Method to synchronously get a single message from the server for a one-time
     * subscription of a subject and type.
     *
     * @param subject message subject subscribed to
     * @param type    message type subscribed to
     * @param id      message id refering to these specific subject and type values
     * @throws cMsgException
     */
    public abstract void handleSubscribeAndGetRequest(String subject, String type,
                                                      int id) throws cMsgException;


    /**
     * Method to synchronously get a single message from a receiver by sending out a
     * message to be responded to.
     *
     * @param message message requesting what sort of message to get
     * @throws cMsgException
     */
    public abstract void handleSendAndGetRequest(cMsgMessageFull message) throws cMsgException;


    /**
     * Method to handle unget request sent by domain client (hidden from user).
     *
     * @param id message id refering to these specific subject and type values
     * @throws cMsgException
     */
    public abstract void handleUngetRequest(int id) throws cMsgException;


    /**
     * Method to handle subscribe request sent by domain client.
     *
     * @param subject  message subject to subscribe to
     * @param type     message type to subscribe to
     * @param id       message id refering to these specific subject and type values
     * @throws cMsgException
     */
    public abstract void handleSubscribeRequest(String subject, String type,
                                                int id) throws cMsgException;

    /**
     * Method to handle unsubscribe request sent by domain client.
     *
     * @param subject  message subject to subscribe to
     * @param type     message type to subscribe to
     * @param id       message id refering to these specific subject and type values
     * @throws cMsgException
     */
    public abstract void handleUnsubscribeRequest(String subject, String type,
                                                  int id) throws cMsgException;


    /**
     * Method to handle keepalive sent by domain client checking to see
     * if the domain server socket is still up.
     *
     * @throws cMsgException
     */
    public abstract void handleKeepAlive() throws cMsgException;


    /**
     * Method to handle a client or domain server down.
     *
     * @throws cMsgException
     */
    public abstract void handleClientShutdown() throws cMsgException;

    /**
     * Method to handle a complete name server down.
     *
     * @throws cMsgException
     */
    public abstract void handleServerShutdown() throws cMsgException;


    /**
     * Method to tell if the "send" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSendRequest}
     * method.
     *
     * @return true if send implemented in {@link #handleSendRequest}
     */
    public abstract boolean hasSend();


    /**
     * Method to tell if the "syncSend" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSyncSendRequest}
     * method.
     *
     * @return true if send implemented in {@link #handleSyncSendRequest}
     */
    public abstract boolean hasSyncSend();


    /**
     * Method to tell if the "subscribeAndGet" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSubscribeAndGetRequest}
     * method.
     *
     * @return true if subscribeAndGet implemented in {@link #handleSubscribeAndGetRequest}
     */
    public abstract boolean hasSubscribeAndGet();


    /**
     * Method to tell if the "sendAndGet" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSendAndGetRequest}
     * method.
     *
     * @return true if sendAndGet implemented in {@link #handleSendAndGetRequest}
     */
    public abstract boolean hasSendAndGet();


    /**
     * Method to tell if the "subscribe" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSubscribeRequest}
     * method.
     *
     * @return true if subscribe implemented in {@link #handleSubscribeRequest}
     */
    public abstract boolean hasSubscribe();


    /**
     * Method to tell if the "unsubscribe" cMsg API function is implemented
     * by this interface implementation in the {@link #handleUnsubscribeRequest}
     * method.
     *
     * @return true if unsubscribe implemented in {@link #handleUnsubscribeRequest}
     */
    public abstract boolean hasUnsubscribe();


    /**
      * Creates a socket communication channel to a client.
      * @param info client information object
      * @throws IOException if socket cannot be created
      */
    public static void createChannel(cMsgClientInfo info) throws IOException {
         SocketChannel channel = SocketChannel.open(new InetSocketAddress(info.getClientHost(),
                                                            info.getClientPort()));
         // set socket options
         Socket socket = channel.socket();
         // Set tcpNoDelay so no packets are delayed
         socket.setTcpNoDelay(true);
         // set buffer sizes
         socket.setReceiveBufferSize(65535);
         socket.setSendBufferSize(65535);
         info.setChannel(channel);
     }


    /**
     * Creates a socket communication channel to a client.
     *
     * @param host host client resides on
     * @param port port client listens on
     * @return SocketChannel object for communicating with client
     * @throws IOException if socket cannot be created
     */
    public static SocketChannel createChannel(String host, int port) throws IOException {
        SocketChannel channel = SocketChannel.open(new InetSocketAddress(host, port));
        // set socket options
        Socket socket = channel.socket();
        // Set tcpNoDelay so no packets are delayed
        socket.setTcpNoDelay(true);
        // set buffer sizes
        socket.setReceiveBufferSize(65535);
        socket.setSendBufferSize(65535);
        return channel;
    }


    /**
     * Method to deliver a message to a client that is
     * subscribed to the message's subject and type.
     *
     * @param channel communication channel to client
     * @param buffer byte buffer needed for channel communication
     * @param msg message to be sent
     * @param idList list of receiverSubscribeIds matching the message's subject and type
     * @param msgType type of communication with the client
     * @throws java.io.IOException if the message cannot be sent over the channel
     *                             or client returns an error
     */
    public static void deliverMessage(SocketChannel channel, ByteBuffer buffer,
                                  cMsgMessageFull msg, List<Integer> idList, int msgType) throws IOException {
        int size = 0;
        if (idList != null) {
            size = idList.size();
        }

        // get ready to write
        buffer.clear();

        // write 14 ints
        int outGoing[] = new int[15];
        outGoing[0]  = msgType;
        outGoing[1]  = msg.getVersion();
        outGoing[2]  = msg.getPriority();
        outGoing[3]  = msg.getUserInt();
        outGoing[4]  = msg.isGetRequest() ? 1 : 0;
        outGoing[5]  = (int) (msg.getSenderTime().getTime() / 1000L);
        outGoing[6]  = (int) (msg.getUserTime().getTime() / 1000L);
        outGoing[7]  = msg.getSysMsgId();
        outGoing[8]  = msg.getSenderToken();
        outGoing[9]  = msg.getSender().length();
        outGoing[10] = msg.getSenderHost().length();
        outGoing[11] = msg.getSubject().length();
        outGoing[12] = msg.getType().length();
        outGoing[13] = msg.getText().length();
        outGoing[14] = size;  // number of receiverSubscribeIds to be sent

        // send ints over together using view buffer
        buffer.asIntBuffer().put(outGoing);

        // position original buffer at position of view buffer
        buffer.position(60);

        // now send ids
        if (idList != null) {
            for (Integer i : idList) {
                buffer.putInt(i.intValue());
            }
        }

        // write strings
        try {
            buffer.put(msg.getSender().getBytes("US-ASCII"));
            buffer.put(msg.getSenderHost().getBytes("US-ASCII"));
            buffer.put(msg.getSubject().getBytes("US-ASCII"));
            buffer.put(msg.getType().getBytes("US-ASCII"));
            buffer.put(msg.getText().getBytes("US-ASCII"));
        }
        catch (UnsupportedEncodingException e) {
            e.printStackTrace();
        }

        // send buffer over the socket
        buffer.flip();
        while (buffer.hasRemaining()) {
            channel.write(buffer);
        }

        return;
    }


}

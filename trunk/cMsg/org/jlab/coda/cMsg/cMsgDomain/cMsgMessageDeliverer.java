/*----------------------------------------------------------------------------*
 *  Copyright (c) 2005        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 8-Feb-2005, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain;

import org.jlab.coda.cMsg.*;

import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;
import java.net.InetSocketAddress;
import java.net.Socket;

/**
 * This class delivers messages from the subdomain handler object in the cMsg
 * domain to its client.<p>
 * Various types of messages may be sent. These are defined to be: <p>
 * <ul>
 * <li>{@link org.jlab.coda.cMsg.cMsgConstants#msgGetResponse} for a message sent in
 * response to a {@link org.jlab.coda.cMsg.cMsg#sendAndGet}<p>
 * <li>{@link org.jlab.coda.cMsg.cMsgConstants#msgGetResponseIsNull} for a null sent in
 * response to a {@link org.jlab.coda.cMsg.cMsg#sendAndGet}<p>
 * with a return acknowlegment<p>
 * <li>{@link org.jlab.coda.cMsg.cMsgConstants#msgSubscribeResponse} for a message sent in
 * response to a {@link org.jlab.coda.cMsg.cMsg#subscribe}<p>
 * with a return acknowlegment<p>
 * </ul>
 */
public class cMsgMessageDeliverer implements cMsgDeliverMessageInterface {

    /** A direct buffer is necessary for nio socket IO. */
    private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

    /** Constructor. */
    public cMsgMessageDeliverer() {}

    /**
     * Method to deliver a message from a domain server's subdomain handler to a client.
     *
     * @param msg     message to sent to client
     * @param clientInfo  information about client
     * @param msgType type of communication with the client
     * @throws java.io.IOException
     */
    public void deliverMessage(cMsgMessageFull msg, cMsgClientInfo clientInfo, int msgType)
            throws IOException {
        deliverMessageReal(msg, clientInfo, msgType, false);
    }

    /**
     * Method to deliver a message from a domain server's subdomain handler to a client
     * and receive acknowledgment that the message was received.
     *
     * @param msg     message to sent to client
     * @param clientInfo  information about client
     * @param msgType type of communication with the client
     * @return true if message acknowledged by receiver or acknowledgment undesired, otherwise false
     * @throws java.io.IOException
     */
    public boolean deliverMessageAndAcknowledge(cMsgMessageFull msg, cMsgClientInfo clientInfo, int msgType)
            throws IOException {
        return deliverMessageReal(msg, clientInfo, msgType, true);
    }

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
     * Method to deliver a message to a client.
     *
     * @param msg     message to be sent
     * @param info information about the client
     * @param msgType type of communication with the client
     * @param acknowledge if acknowledgement of message is desired, set to true
     * @return true if message acknowledged by receiver or acknowledgment undesired, otherwise false
     * @throws java.io.IOException if the message cannot be sent over the channel
     *                             or client returns an error
     */
    private boolean deliverMessageReal(cMsgMessageFull msg, cMsgClientInfo info,
                                       int msgType, boolean acknowledge)
            throws IOException {

        SocketChannel channel = info.getChannel();
        if (channel == null) {
            createChannel(info);
        }

        // if a get has a null response ...
        boolean nullResponse = false;
        if (msgType == cMsgConstants.msgGetResponseIsNull) {
            nullResponse = true;
        }

        // get ready to write
        buffer.clear();

        if (msgType == cMsgConstants.msgShutdown) {
            buffer.putInt(msgType);
        }
        else if (nullResponse) {
            buffer.putInt(msgType);
            // send senderToken
            buffer.putInt(msg.getSenderToken());
            // want an acknowledgment?
            buffer.putInt(acknowledge ? 1 : 0);
        }
        else {
            // write 15 ints
            int outGoing[] = new int[16];
            outGoing[0]  = msgType;
            outGoing[1]  = msg.getVersion();
            outGoing[2]  = 0; // reserved for future use
            outGoing[3]  = msg.getUserInt();
            outGoing[4]  = msg.getInfo();
            outGoing[5]  = (int) (msg.getSenderTime().getTime() / 1000L);
            outGoing[6]  = (int) (msg.getUserTime().getTime() / 1000L);
            outGoing[7]  = msg.getSysMsgId();
            outGoing[8]  = msg.getSenderToken();
            outGoing[9]  = msg.getSender().length();
            outGoing[10] = msg.getSenderHost().length();
            outGoing[11] = msg.getSubject().length();
            outGoing[12] = msg.getType().length();
            outGoing[13] = msg.getText().length();
            outGoing[14] = msg.getCreator().length();
            outGoing[15] = acknowledge ? 1 : 0;

            // send ints over together using view buffer
            buffer.asIntBuffer().put(outGoing);

            // position original buffer at position of view buffer
            buffer.position(64);

            // write strings
            try {
                buffer.put(msg.getSender().getBytes("US-ASCII"));
                buffer.put(msg.getSenderHost().getBytes("US-ASCII"));
                buffer.put(msg.getSubject().getBytes("US-ASCII"));
                buffer.put(msg.getType().getBytes("US-ASCII"));
                buffer.put(msg.getText().getBytes("US-ASCII"));
                buffer.put(msg.getCreator().getBytes("US-ASCII"));
            }
            catch (UnsupportedEncodingException e) {
                e.printStackTrace();
            }

        }

        // send buffer over the socket
        buffer.flip();
        while (buffer.hasRemaining()) {
            channel.write(buffer);
        }

        // If no acknowledgment required, return
        if (!acknowledge) {
            return true;
        }

        // read acknowledgment - 1 int of data
        cMsgUtilities.readSocketBytes(buffer, channel, 4, cMsgConstants.debugNone);
        buffer.flip();

        if (buffer.getInt() != cMsgConstants.ok) return false;
        return true;
    }


}

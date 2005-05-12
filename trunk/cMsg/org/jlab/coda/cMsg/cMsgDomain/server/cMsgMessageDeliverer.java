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

package org.jlab.coda.cMsg.cMsgDomain.server;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.cMsgDomain.cMsgUtilities;

import java.io.*;
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
    private ByteBuffer buffer = ByteBuffer.allocateDirect(4096);

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
    public void createChannel(cMsgClientInfo info) throws IOException {
        SocketChannel channel = SocketChannel.open(new InetSocketAddress(info.getClientHost(),
                                                                         info.getClientPort()));
        // set socket options
        Socket socket = channel.socket();
        // Set tcpNoDelay so no packets are delayed
        socket.setTcpNoDelay(true);
        // set buffer sizes
        socket.setReceiveBufferSize(65535);
        socket.setSendBufferSize(65535);

        // store connection in info object
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
    public SocketChannel createChannel(String host, int port) throws IOException {
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
                                       int msgType, boolean acknowledge) throws IOException {

        DataInputStream in = info.getInputStream();
        DataOutputStream out = info.getOutputStream();

        if (in == null || out == null) {
            createChannel(info);
            in  = info.getInputStream();
            out = info.getOutputStream();
        }

        // if a get has a null response ...
        boolean nullResponse = false;
        if (msgType == cMsgConstants.msgGetResponseIsNull) {
            nullResponse = true;
        }

        if (msgType == cMsgConstants.msgShutdown) {
            // size
            out.writeInt(8);
            // msg type
            out.writeInt(msgType);
            // want an acknowledgment?
            out.writeInt(acknowledge ? 1 : 0);
        }
        else if (nullResponse) {
            // size
            out.writeInt(12);
            // msg type
            out.writeInt(msgType);
            // senderToken
            out.writeInt(msg.getSenderToken());
            // want an acknowledgment?
            out.writeInt(acknowledge ? 1 : 0);
        }
        else {
            // write 20 ints

            int len1 = msg.getSender().length();
            int len2 = msg.getSenderHost().length();
            int len3 = msg.getSubject().length();
            int len4 = msg.getType().length();
            int len5 = msg.getCreator().length();
            int len6 = msg.getText().length();
            int binLength = msg.getByteArrayLength();
            // size of everything sent (except "size" itself which is first integer)
            int size = len1 + len2 + len3 + len4 + len5 + len6 +
                       binLength + 4*(19);

            out.writeInt(size);
            out.writeInt(msgType);
            out.writeInt(msg.getVersion());
            out.writeInt(0); // reserved for future use
            out.writeInt(msg.getUserInt());
            out.writeInt(msg.getInfo());

            // send the time in milliseconds as 2, 32 bit integers
            out.writeInt((int) (msg.getSenderTime().getTime() >>> 32)); // higher 32 bits
            out.writeInt((int) (msg.getSenderTime().getTime() & 0x00000000FFFFFFFFL)); // lower 32 bits
            out.writeInt((int) (msg.getUserTime().getTime() >>> 32));
            out.writeInt((int) (msg.getUserTime().getTime() & 0x00000000FFFFFFFFL));

            out.writeInt(msg.getSysMsgId());
            out.writeInt(msg.getSenderToken());
            out.writeInt(len1);
            out.writeInt(len2);
            out.writeInt(len3);
            out.writeInt(len4);
            out.writeInt(len5);
            out.writeInt(len6);
            out.writeInt(binLength);
            out.writeInt(acknowledge ? 1 : 0);

            // write strings
            try {
                out.write(msg.getSender().getBytes("US-ASCII"));
                out.write(msg.getSenderHost().getBytes("US-ASCII"));
                out.write(msg.getSubject().getBytes("US-ASCII"));
                out.write(msg.getType().getBytes("US-ASCII"));
                out.write(msg.getCreator().getBytes("US-ASCII"));
                out.write(msg.getText().getBytes("US-ASCII"));
                if (binLength > 0) {
                    out.write(msg.getByteArray(),
                               msg.getByteArrayOffset(),
                               binLength);
                }
            }
            catch (UnsupportedEncodingException e) {
                e.printStackTrace();
            }
            out.flush();

        }

        // If no acknowledgment required, return
        if (!acknowledge) {
            return true;
        }

        if (in.readInt() != cMsgConstants.ok) return false;
        return true;
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
    private boolean deliverMessageRealNIO(cMsgMessageFull msg, cMsgClientInfo info,
                                       int msgType, boolean acknowledge) throws IOException {

        int binaryLength = 0;

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
            // size
            buffer.putInt(8);
            // msg type
            buffer.putInt(msgType);
            // want an acknowledgment?
            buffer.putInt(acknowledge ? 1 : 0);
        }
        else if (nullResponse) {
            // size
            buffer.putInt(12);
            // msg type
            buffer.putInt(msgType);
            // senderToken
            buffer.putInt(msg.getSenderToken());
            // want an acknowledgment?
            buffer.putInt(acknowledge ? 1 : 0);
        }
        else {
            // write 20 ints
            int outGoing[] = new int[20];
            outGoing[1]  = msgType;
            outGoing[2]  = msg.getVersion();
            outGoing[3]  = 0; // reserved for future use
            outGoing[4]  = msg.getUserInt();
            outGoing[5]  = msg.getInfo();

            // send the time in milliseconds as 2, 32 bit integers
            outGoing[6]  = (int) (msg.getSenderTime().getTime() >>> 32); // higher 32 bits
            outGoing[7]  = (int) (msg.getSenderTime().getTime() & 0x00000000FFFFFFFFL); // lower 32 bits
            outGoing[8]  = (int) (msg.getUserTime().getTime() >>> 32);
            outGoing[9]  = (int) (msg.getUserTime().getTime() & 0x00000000FFFFFFFFL);

            outGoing[10] = msg.getSysMsgId();
            outGoing[11] = msg.getSenderToken();
            outGoing[12] = msg.getSender().length();
            outGoing[13] = msg.getSenderHost().length();
            outGoing[14] = msg.getSubject().length();
            outGoing[15] = msg.getType().length();
            outGoing[16] = msg.getCreator().length();
            outGoing[17] = msg.getText().length();
            binaryLength = msg.getByteArrayLength();
            outGoing[18] = binaryLength;
            outGoing[19] = acknowledge ? 1 : 0;

            // make sure there's enough space in the buffer
            outGoing[0]  = outGoing[12] + outGoing[13] + outGoing[14] +
                           outGoing[15] + outGoing[16] + outGoing[17] +
                           outGoing[18] + 4*(outGoing.length - 1);

            if (outGoing[0] + 4 > buffer.capacity()) {
                buffer = ByteBuffer.allocateDirect(outGoing[0] + 1004);
            }

            // send ints over together using view buffer
            buffer.asIntBuffer().put(outGoing);

            // position original buffer at position of view buffer
            buffer.position(80);

            // write strings
            try {
                buffer.put(msg.getSender().getBytes("US-ASCII"));
                buffer.put(msg.getSenderHost().getBytes("US-ASCII"));
                buffer.put(msg.getSubject().getBytes("US-ASCII"));
                buffer.put(msg.getType().getBytes("US-ASCII"));
                buffer.put(msg.getCreator().getBytes("US-ASCII"));
                buffer.put(msg.getText().getBytes("US-ASCII"));
                if (binaryLength > 0) {
                    buffer.put(msg.getByteArray(),
                               msg.getByteArrayOffset(),
                               binaryLength);
                }
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

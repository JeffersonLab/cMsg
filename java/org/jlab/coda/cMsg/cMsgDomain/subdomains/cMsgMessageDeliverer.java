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

package org.jlab.coda.cMsg.cMsgDomain.subdomains;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgClientInfo;
import org.jlab.coda.cMsg.common.cMsgDeliverMessageInterface;

import java.io.*;
import java.nio.channels.SocketChannel;
import java.nio.channels.ByteChannel;
import java.nio.channels.Channels;
import java.nio.ByteBuffer;

/**
 * <p>This class delivers messages from the subdomain handler objects in the cMsg
 * domain to a particular client.</p>
 * <p>Various types of messages may be sent. These are defined to be:</p>
 * <ul>
 * <li><p>{@link org.jlab.coda.cMsg.cMsgConstants#msgGetResponse} for a message sent in
 * response to a {@link org.jlab.coda.cMsg.cMsg#sendAndGet}</p>
 * <li><p>{@link org.jlab.coda.cMsg.cMsgConstants#msgServerGetResponse} for a message sent in
 * response to a {@link org.jlab.coda.cMsg.cMsgDomain.client.cMsgServerClient#serverSendAndGet}
 * with a return acknowlegment</p>
 * <li><p>{@link org.jlab.coda.cMsg.cMsgConstants#msgSubscribeResponse} for a message sent in
 * response to a {@link org.jlab.coda.cMsg.cMsg#subscribe}
 * with a return acknowlegment</p>
 * <li><p>{@link org.jlab.coda.cMsg.cMsgConstants#msgShutdownClients} for a message to shutdown
 * the receiving client</p>
 * <li><p>{@link org.jlab.coda.cMsg.cMsgConstants#msgSyncSendResponse} for an int sent in
 * response to a {@link org.jlab.coda.cMsg.cMsg#syncSend}</p>
 * <li><p>{@link org.jlab.coda.cMsg.cMsgConstants#msgServerRegistrationLockResponse} for an int sent in
 * response to a {@link org.jlab.coda.cMsg.cMsgDomain.client.cMsgServerClient#registrationLock}</p>
 * <li><p>{@link org.jlab.coda.cMsg.cMsgConstants#msgServerCloudLockResponse} for an int sent in
 * response to a {@link org.jlab.coda.cMsg.cMsgDomain.client.cMsgServerClient#cloudLock}</p>
 * <li><p>{@link org.jlab.coda.cMsg.cMsgConstants#msgServerSendClientNamesResponse} for an int sent in
 * response to a {@link org.jlab.coda.cMsg.cMsgDomain.client.cMsgServerClient#getClientNamesAndNamespaces}</p>
 * </ul>
 */
public class cMsgMessageDeliverer implements cMsgDeliverMessageInterface {

    /** Communication channel used by subdomainHandler to talk to client. */
    private SocketChannel channel;

    /** Buffered data output stream associated with channel socket. */
    private DataOutputStream out;

    private cMsgClientInfo info;

    boolean blocking;

    /** Direct buffer for writing nonblocking IO. */
    ByteBuffer buffer;

    //-----------------------------------------------------------------------------------

    /**
     * Create a message delivering object for use with one specific client.
     * @throws IOException if cannot establish communication with client
     */
    public cMsgMessageDeliverer() throws IOException {
        // Do not create the connection yet.
        // Now it's done after client connects to domain server.
    }

    /**
     * Method to close all streams and sockets.
     */
    synchronized public void close() {
        if (out != null) {
            try {out.close();}
            catch (IOException e) {}
        }
        if (channel != null) {
            try {channel.close();}
            catch (IOException e) {}
        }
    }

    /**
     * Method to deliver a message from a domain server's subdomain handler to a client.
     *
     * @param msg message to sent to client
     * @param msgType type of communication with the client
     * @throws IOException if the message cannot be sent over the channel
     *                     or client returns an error
     * @throws cMsgException if connection to client has not been established
     */
    synchronized public void deliverMessage(cMsgMessage msg, int msgType)
            throws IOException, cMsgException {
        if (blocking) {
            deliverMessageReal(msg, msgType);
        }
        else {
            deliverMessageRealNonblocking(msg, msgType);
        }
    }

    /**
     * Method to deliver an integer from a domain server's subdomain handler to a client.
     *
     * @param i integer to sent to client
     * @param j integer to sent to client
     * @param msgType type of communication with the client
     * @throws IOException if the message cannot be sent over the channel
     *                     or client returns an error
     * @throws cMsgException if connection to client has not been established
     */
    synchronized public void deliverMessage(int i, int j, int msgType)
            throws IOException, cMsgException {
        if (blocking) {
            deliverMessageReal(i, j, msgType);
        }
        else {
            deliverMessageRealNonblocking(i, j, msgType);
        }
    }

    /**
     * Method to deliver an array of string from a domain server's subdomain handler to a client.
     *
     * @param strs array of strings to sent to client
     * @param msgType type of communication with the client
     * @throws IOException if the message cannot be sent over the channel
     *                     or client returns an error
     * @throws cMsgException if connection to client has not been established
     */
    synchronized public void deliverMessage(String[] strs, int msgType)
            throws IOException, cMsgException {
        if (blocking) {
            deliverMessageReal(strs, msgType);
        }
        else {
            deliverMessageRealNonblocking(strs, msgType);
        }
    }


    /**
     * Creates a socket communication channel to a client.
     * @param channel channel to client
     * @param blocking is socket blocking (true) or nonblocking (false) ?
     * @throws IOException if socket cannot be created
     */
    synchronized public void createClientConnection(SocketChannel channel, boolean blocking) throws IOException {
        this.blocking = blocking;
        this.channel  = channel;
        buffer = ByteBuffer.allocateDirect(16384);
        // set socket options (most set in cMsgDomainServerSelect)
        // Set tcpNoDelay so no packets are delayed
        channel.socket().setTcpNoDelay(true);
        // set buffer size
        channel.socket().setSendBufferSize(131072);

        if (blocking) {
            channel.configureBlocking(true);
            ByteChannel bc = cMsgUtilities.wrapChannel(channel);
            out = new DataOutputStream(new BufferedOutputStream(Channels.newOutputStream(bc), 131072));
        }
    }



    /**
     * Method to deliver a integer to a client.
     *
     * @param i integer to be sent
     * @param j integer to be sent
     * @param msgType type of communication with the client
     * @throws cMsgException if connection to client has not been established
     * @throws IOException if the message cannot be sent over the channel
     *                     or client returns an error
     */
    private void deliverMessageReal(int i, int j, int msgType)
            throws IOException, cMsgException {

        if (out == null || !channel.isOpen()) {
            throw new cMsgException("Channel to client is closed");
        }

        if (msgType == cMsgConstants.msgShutdownClients) {
            // size
            out.writeInt(4);
            // msg type
            out.writeInt(msgType);
            out.flush();
        }
        else {
            // size
            out.writeInt(12);
            // msg type
            out.writeInt(msgType);
            // sync send response
            out.writeInt(i);
            // sync send id
            out.writeInt(j);
            out.flush();
        }
        return;
    }


    /**
     * Method to deliver a integer to a client.
     *
     * @param i integer to be sent
     * @param j integer to be sent
     * @param msgType type of communication with the client
     * @throws cMsgException if connection to client has not been established
     * @throws IOException if the message cannot be sent over the channel
     *                     or client returns an error
     */
    private void deliverMessageRealNonblocking(int i, int j, int msgType)
            throws IOException, cMsgException {

        if (!channel.isOpen()) {
            throw new cMsgException("Channel to client is closed");
        }

        if (msgType == cMsgConstants.msgShutdownClients) {
            buffer.clear();
            // size
            buffer.putInt(4);
            // msg type
            buffer.putInt(msgType);
            buffer.flip();
        }
        else {
            buffer.clear();
            buffer.putInt(12);
            buffer.putInt(msgType);
            // sync send response
            buffer.putInt(i);
            // sync send id
            buffer.putInt(j);
            buffer.flip();
        }

        // Write info to client.
        // NOTE: If vxworks client, its death will not be detected
        // when writing, so be careful not to get stuck in infinite
        // loop.
        int bytesWritten = 0, totalBytesWritten = 0, tries = 0;

        while (buffer.hasRemaining()) {
            bytesWritten = channel.write(buffer);
            totalBytesWritten += bytesWritten;
            // Client may be dead or TCP socket buffer may be
            // full due to overwhelmed & backed up client.
            if (totalBytesWritten < buffer.limit()) {
                if (!channel.isOpen()) {
                    throw new cMsgException("Channel to client is closed");
                }
                try { Thread.sleep(10); }
                catch (InterruptedException e) {}
            }
        }

        return;
    }


    /**
     * Method to deliver an array of strings to a client.
     *
     * @param strs array of strings to be sent
     * @param msgType type of communication with the client
     * @throws cMsgException if connection to client has not been established
     * @throws IOException if the message cannot be sent over the channel
     *                     or client returns an error
     */
    private void deliverMessageReal(String[] strs, int msgType)
            throws IOException, cMsgException {

        if (out == null || !channel.isOpen()) {
            throw new cMsgException("Channel to client is closed");
        }

        // lengths of ints to send
        int size = 4*(2+strs.length);
        // lengths of strings to send
        for (String s : strs) {
            size += s.length();
        }

        // size in bytes
        out.writeInt(size);
        // msg type
        out.writeInt(msgType);
        // number of strings to come
        out.writeInt(strs.length);
        // lengths of strings
        for (String s : strs) {
            out.writeInt(s.length());
        }
        // strings
        try {
            for (String s : strs) {
                out.write(s.getBytes("US-ASCII"));
            }
        }
        catch (UnsupportedEncodingException e) {/* never happen */}
        out.flush();

        return;
    }

    /**
     * Method to deliver an array of string to a client.
     *
     * @param strs array of strings to be sent
     * @param msgType type of communication with the client
     * @throws cMsgException if connection to client has not been established
     * @throws IOException if the message cannot be sent over the channel
     *                     or client returns an error
     */
    private void deliverMessageRealNonblocking(String[] strs, int msgType)
            throws IOException, cMsgException {

        if (!channel.isOpen()) {
            throw new cMsgException("Channel to client is closed");
        }

        // lengths of ints to send
        int size = 4*(2+strs.length);
        // lengths of strings to send
        for (String s : strs) {
            size += s.length();
        }

        if (buffer.capacity() < size) {
            buffer = ByteBuffer.allocateDirect(size + 1024);
        }

        buffer.clear();
        buffer.putInt(size);
        buffer.putInt(msgType);
        buffer.putInt(strs.length);
        // lengths of strings
        for (String s : strs) {
            buffer.putInt(s.length());
        }
        // strings
        try {
            for (String s : strs) {
                buffer.put(s.getBytes("US-ASCII"));
            }
        }
        catch (UnsupportedEncodingException e) {/* never happen */}
        buffer.flip();

        // Write info to client.
        // NOTE: If vxworks client, its death will not be detected
        // when writing, so be careful not to get stuck in infinite
        // loop.
        int bytesWritten = 0, totalBytesWritten = 0, tries = 0;

        while (buffer.hasRemaining()) {
            bytesWritten = channel.write(buffer);
            totalBytesWritten += bytesWritten;
            // Client may be dead or TCP socket buffer may be
            // full due to overwhelmed & backed up client.
            if (totalBytesWritten < buffer.limit()) {
                if (!channel.isOpen()) {
                    throw new cMsgException("Channel to client is closed");
                }
                try { Thread.sleep(10); }
                catch (InterruptedException e) {}
            }
        }

        return;
    }



    /**
     * Method to deliver a message to a client.
     *
     * @param msg     message to be sent
     * @param msgType type of communication with the client
     * @throws cMsgException if connection to client has not been established
     * @throws IOException if the message cannot be sent over the channel
     *                     or client returns an error
     */
    private void deliverMessageRealNonblocking(cMsgMessage msg, int msgType)
            throws IOException, cMsgException {

        if (!channel.isOpen()) {
            throw new cMsgException("Channel to client is closed");
        }

// CHANGED the protocol by getting rid of acknowledges & adding syncSend response
        if (msgType == cMsgConstants.msgSyncSendResponse ||
            msgType == cMsgConstants.msgServerRegistrationLock ||
            msgType == cMsgConstants.msgServerCloudLock) {
            throw new cMsgException("Call deliverMessage(int,int) to send msg");
        }
        
        if (msgType == cMsgConstants.msgShutdownClients) {
            buffer.clear();
            // size
            buffer.putInt(4);
            // msg type
            buffer.putInt(msgType);
            buffer.flip();
        }
        else {
            // write 19 ints
//System.out.println("deliverer (NIO) : Normal message actually being sent");
            int len1 = msg.getSender().length();
            int len2 = msg.getSenderHost().length();
            int len3 = msg.getSubject().length();
            int len4 = msg.getType().length();
            int len5 = 0;
            if (msg.getPayloadText() != null) {
                len5 = msg.getPayloadText().length();
            }
            int len6 = msg.getText().length();
            int binLength;
            if (msg.getByteArray() == null) {
                binLength = 0;
            }
            else {
                binLength = msg.getByteArrayLength();
            }
            // size of everything sent (except "size" itself which is first integer)
            int size = len1 + len2 + len3 + len4 + len5 + len6 +
                       binLength + 4*19;

            if (buffer.capacity() < size) {
                buffer = ByteBuffer.allocateDirect(size + 1024);
            }

            buffer.clear();
            buffer.putInt(size);
            buffer.putInt(msgType);
            buffer.putInt(msg.getVersion());
            buffer.putInt(0);   // reserved for future use
            buffer.putInt(msg.getUserInt());
            buffer.putInt(msg.getInfo());

            // send the time in milliseconds as 2, 64 bit integers
            buffer.putLong(msg.getSenderTime().getTime());
            buffer.putLong(msg.getUserTime().getTime());
//            buffer.putInt((int) (msg.getSenderTime().getTime() >>> 32)); // higher 32 bits
//            buffer.putInt((int) (msg.getSenderTime().getTime() & 0x00000000FFFFFFFFL)); // lower 32 bits
//            buffer.putInt((int) (msg.getUserTime().getTime() >>> 32));
//            buffer.putInt((int) (msg.getUserTime().getTime() & 0x00000000FFFFFFFFL));

            buffer.putInt(msg.getSysMsgId());
            buffer.putInt(msg.getSenderToken());
            buffer.putInt(len1);
            buffer.putInt(len2);
            buffer.putInt(len3);
            buffer.putInt(len4);
            buffer.putInt(len5);
            buffer.putInt(len6);
            buffer.putInt(binLength);

            // write strings
            try {
                buffer.put(msg.getSender().getBytes("US-ASCII"));
                buffer.put(msg.getSenderHost().getBytes("US-ASCII"));
                buffer.put(msg.getSubject().getBytes("US-ASCII"));
                buffer.put(msg.getType().getBytes("US-ASCII"));
                if (len5 > 0) {
                    buffer.put(msg.getPayloadText().getBytes("US-ASCII"));
                }
                buffer.put(msg.getText().getBytes("US-ASCII"));
                if (binLength > 0) {
                    buffer.put(msg.getByteArray(),
                               msg.getByteArrayOffset(),
                               binLength);
                }
            }
            catch (UnsupportedEncodingException e) {
                e.printStackTrace();
            }
            buffer.flip();

        }

        // Write info to client.
        // NOTE: If vxworks client, its death will not be detected
        // when writing, so be careful not to get stuck in infinite
        // loop.
        int bytesWritten = 0, totalBytesWritten = 0, tries = 0;

        while (buffer.hasRemaining()) {
//System.out.println("deliverer (NIO) : try writing buffer, chan open (" + channel.isOpen() +
//                    "), connected (" + channel.isConnected() + ")");
            bytesWritten = channel.write(buffer);
            totalBytesWritten += bytesWritten;

            // Client may be dead or TCP socket buffer may be
            // full due to overwhelmed & backed up client.
            if (totalBytesWritten < buffer.limit()) {
                if (!channel.isOpen()) {
                    throw new cMsgException("Channel to client is closed");
                }
                try { Thread.sleep(10); }
                catch (InterruptedException e) {}
            }
// OLD WAY OF DOING THIS ...
//            // client may be dead so bail
//            if (bytesWritten < 1 && totalBytesWritten < 1) {
//                throw new IOException("Client is presumed dead");
//            }
//            // Keep trying to write. Throwing an error before finishing
//            // the write will cause massive chaos in the client. It's
//            // better to be deadlocked then cause exceptions and seg faults.
//            else if (totalBytesWritten < buffer.limit()) {
////                if  (++tries > 9) {
////System.out.println("deliverer (NIO) : Cannot deliver full MSG");
////                    throw new IOException("Client is dead or deadlocked");
////                }
//                try { Thread.sleep(10); }
//                catch (InterruptedException e) {}
//            }
        }
//System.out.println("deliverer (NIO) : done sending msg");
     
        return;
    }

    /**
     * Method to deliver a message to a client.
     *
     * @param msg     message to be sent
     * @param msgType type of communication with the client
     * @throws cMsgException if connection to client has not been established
     * @throws IOException if the message cannot be sent over the channel
     *                     or client returns an error
     */
    private void deliverMessageReal(cMsgMessage msg, int msgType)
            throws IOException, cMsgException {

        //if (in == null || out == null || !channel.isOpen()) {
        if (out == null || !channel.isOpen()) {
            throw new cMsgException("Channel to client is closed");
        }

// CHANGED the protocol by getting rid of acknowledges & adding syncSend reponse

        if (msgType == cMsgConstants.msgShutdownClients) {
            // size
            out.writeInt(4);
            // msg type
            out.writeInt(msgType);
            out.flush();
        }
        else if (msgType == cMsgConstants.msgSyncSendResponse) {
            // size
            out.writeInt(8);
            // msg type
            out.writeInt(msgType);
            // sync send response
            out.writeInt(0);
            out.flush();
        }
        else {
            // write 20 ints
//System.out.println("deliverer (streams) : Normal message actually being sent");
            int len1 = msg.getSender().length();
            int len2 = msg.getSenderHost().length();
            int len3 = msg.getSubject().length();
            int len4 = msg.getType().length();
            int len5 = 0;
            if (msg.getPayloadText() != null) {
                len5 = msg.getPayloadText().length();
            }
            int len6 = msg.getText().length();
            int binLength;
            if (msg.getByteArray() == null) {
                binLength = 0;
            }
            else {
                binLength = msg.getByteArrayLength();
            }
            // size of everything sent (except "size" itself which is first integer)
            int size = len1 + len2 + len3 + len4 + len5 + len6 + binLength + 4*18;

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

            // write strings
            try {
                out.write(msg.getSender().getBytes("US-ASCII"));
                out.write(msg.getSenderHost().getBytes("US-ASCII"));
                out.write(msg.getSubject().getBytes("US-ASCII"));
                out.write(msg.getType().getBytes("US-ASCII"));
                if (len5 > 0) {
                    out.write(msg.getPayloadText().getBytes("US-ASCII"));
                }
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
//System.out.println("deliverer (streams) : try flushing");
            out.flush();

        }
//System.out.println("deliverer (streams) : done");

       return;
    }


}

/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 20-Aug-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.coda;

import org.jlab.coda.cMsg.cMsgConstants;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgException;

import java.nio.channels.ServerSocketChannel;
import java.nio.channels.Selector;
import java.nio.channels.SelectionKey;
import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;
import java.io.IOException;
import java.util.Iterator;
import java.util.Date;
import java.util.HashSet;
import java.net.Socket;

/**
 * This class implements a cMsg client's thread which listens for
 * communications from the domain server. The server sends it messages
 * to which the client has subscribed.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgClientListeningThread extends Thread {

    /** Type of domain this is. */
    private String domainType = "CODA";

    /** cMsg client that created this object. */
    private cMsgCoda client;

    /** Server channel (contains socket). */
    private ServerSocketChannel serverChannel;

    /** A direct buffer is necessary for nio socket IO. */
    private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

    /** Allocate int array once (used for reading in data) for efficiency's sake. */
    private int[] inComing = new int[11];
    //private int lastOdd=1,lastEven=0;

    /** Allocate byte array once (used for reading in data) for efficiency's sake. */
    byte[] bytes = new byte[5000];

    /** Level of debug output for this class. */
    private int debug;

    /** Setting this to true will kill this thread. */
    private boolean killThread;

    /** Kills this thread. */
    public void killThread() {
        killThread = true;
    }


    /**
     * Constructor which starts threads.
     *
     * @param myClient cMsg client that created this object
     * @param channel suggested port on which to starting listening for connections
     */
    public cMsgClientListeningThread(cMsgCoda myClient, ServerSocketChannel channel) {

        client = myClient;
        serverChannel = channel;
        debug = client.debug;
    }



    /** This method is executed as a thread. */
    public void run() {
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("Running Client Listening Thread");
        }

        try {
            // get things ready for a select call
            Selector selector = Selector.open();

            // set nonblocking mode for the listening socket
            serverChannel.configureBlocking(false);

            // register the channel with the selector for accepts
            serverChannel.register(selector, SelectionKey.OP_ACCEPT);

            // cMsgCoda object is waiting for this thread to start in connect method
            synchronized(this) {
                notifyAll();
            }

            while (true) {
                // 3 second timeout
                int n = selector.select(3000);

                // if no channels (sockets) are ready, listen some more
                if (n == 0) {
                    // but first check to see if we've been commanded to die
                    if (killThread) {
                        serverChannel.close();
                        selector.close();
                        return;
                    }
                    continue;
                }

                if (killThread) {
                    serverChannel.close();
                    selector.close();
                    return;
                }

                // get an iterator of selected keys (ready sockets)
                Iterator it = selector.selectedKeys().iterator();

                // look at each key
                while (it.hasNext()) {
                    SelectionKey key = (SelectionKey) it.next();

                    // is this a new connection coming in?
                    if (key.isValid() && key.isAcceptable()) {
                        ServerSocketChannel server = (ServerSocketChannel) key.channel();
                        // accept the connection from the client
                        SocketChannel channel = server.accept();
                        // let us know (in the next select call) if this socket is ready to read
                        cMsgUtilities.registerChannel(selector, channel, SelectionKey.OP_READ);

                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("cMsgClientListeningThread: new connection from server");
                        }
                    }

                    // is there data to read on this channel?
                    if (key.isValid() && key.isReadable()) {
                        SocketChannel channel = (SocketChannel) key.channel();
                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("cMsgClientListeningThread: request from server");
                        }
                        handleClient(channel);
                    }

                    // remove key from selected set since it's been handled
                    it.remove();
                }
            }
        }
        catch (IOException ex) {
            if (debug >= cMsgConstants.debugError) {
                ex.printStackTrace();
            }
        }

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("Quitting Client Listening Thread");
        }

        return;
    }


    /**
     * This method handles all communication between a domain server and this
     * cMsg user who has connected to that domain server.
     *
     * @param channel nio socket communication channel
     */
     private void handleClient(SocketChannel channel) {

         try {
             // keep reading until we have an int (4 bytes) of data
             if (cMsgUtilities.readSocketBytes(buffer, channel, 4, debug) < 0) {
                 return;
             }

             // make buffer readable
             buffer.flip();

             // read client's request
             int msgId = buffer.getInt();

             switch (msgId) {

                 case cMsgConstants.msgSubscribeResponse: // receiving a message
                     // read the message here
                     if (debug >= cMsgConstants.debugInfo) {
                         System.out.println("handleClient: got send request from server");
                     }
                     cMsgMessage msg = readIncomingMessage(channel);

                     // run callbacks for this message
                     runCallbacks(msg);

                     break;

                 case cMsgConstants.msgKeepAlive: // see if this end is still here
                     if (debug >= cMsgConstants.debugInfo) {
                         System.out.println("handleClient: got keep alive from server");
                     }
                     // read int
                     cMsgUtilities.readSocketBytes(buffer, channel, 4, debug);
                     // send ok back as acknowledgment
                     buffer.clear();
                     buffer.putInt(cMsgConstants.ok).flip();
                     while (buffer.hasRemaining()) {
                         channel.write(buffer);
                     }
                     break;

                 case cMsgConstants.msgShutdown: // told this server to shutdown
                     if (debug >= cMsgConstants.debugInfo) {
                         System.out.println("handleClient: got shutdown from server");
                     }
                     // send ok back as acknowledgment
                     buffer.clear();
                     buffer.putInt(cMsgConstants.ok).flip();
                     while (buffer.hasRemaining()) {
                         channel.write(buffer);
                     }
                     // close channel and unregister from selector
                     channel.close();
                     // need to shutdown this server now
                     killThread();
                     break;

                 default:
                     if (debug >= cMsgConstants.debugWarn) {
                         System.out.println("handleClient: can't understand server message = " + msgId);
                     }
                     break;
             }

             return;
         }
         catch (IOException e) {
             //e.printStackTrace();
             if (debug >= cMsgConstants.debugError) {
                 System.out.println("handleClient: I/O ERROR in cMsg client");
             }
             try {channel.close();}
             catch (IOException e1) {
                 e1.printStackTrace();
             }
         }
         catch (cMsgException e) {
             // We're here if too many messages are sent to a callback.
             // Disconnect the client (kill listening (this) thread and keepAlive thread).
             System.out.println(e.getMessage());
             client.disconnect();
             return;
         }
     }



    /**
     * This method reads an incoming cMsgMessage from a clientHandler object.
     *
     * @param channel nio socket communication channel
     * @throws IOException if socket read or write error
     * @return message read from channel
     */
    private cMsgMessage readIncomingMessage(SocketChannel channel) throws IOException {

        // create a message
        cMsgMessage msg = new cMsgMessage();

        // keep reading until we have 11 ints of data
        cMsgUtilities.readSocketBytes(buffer, channel, 44, debug);

        // go back to reading-from-buffer mode
        buffer.flip();

        // read 11 ints
        buffer.asIntBuffer().get(inComing);

        // system message id
        msg.setSysMsgId(inComing[0]);
        // receiverSubscribe id
        msg.setReceiverSubscribeId(inComing[1]);
        // sender id
        msg.setSenderId(inComing[2]);
        // time message sent in seconds since midnight GMT, Jan 1, 1970
        msg.setSenderTime(new Date(((long)inComing[3])*1000));
        // sender message id
        msg.setSenderMsgId(inComing[4]);
        // sender token
        msg.setSenderToken(inComing[5]);

        // length of message sender
        int lengthSender = inComing[6];
        // length of message senderHost
        int lengthSenderHost = inComing[7];
        // length of message subject
        int lengthSubject = inComing[8];
        // length of message type
        int lengthType = inComing[9];
        // length of message text
        int lengthText = inComing[10];

        // bytes expected
        int bytesToRead = lengthSender + lengthSenderHost + lengthSubject + lengthType + lengthText;

        // read in all remaining bytes
        cMsgUtilities.readSocketBytes(buffer, channel, bytesToRead, debug);

        // go back to reading-from-buffer mode
        buffer.flip();

        // allocate bigger byte array if necessary
        // (allocate more than needed for speed's sake)
        if (bytesToRead > bytes.length) {
            bytes = new byte[bytesToRead];
        }

        buffer.get(bytes, 0, bytesToRead);
        // read sender
        msg.setSender(new String(bytes, 0, lengthSender, "US-ASCII"));

        // read senderHost
        msg.setSenderHost(new String(bytes, lengthSender, lengthSenderHost, "US-ASCII"));

        // read subject
        msg.setSubject(new String(bytes, lengthSender+lengthSenderHost, lengthSubject, "US-ASCII"));

        // read type
        msg.setType(new String(bytes, lengthSender+lengthSenderHost+lengthSubject, lengthType, "US-ASCII"));

        // read text
        msg.setText(new String(bytes, bytesToRead-lengthText, lengthText, "US-ASCII"));
        /*
        int num = Integer.parseInt(msg.getText());
        if (num%2 > 0) {
            if (num - lastOdd != 2) {
                System.out.println("         " + lastOdd + " -> " + msg.getText());
            }
            lastOdd = num;
        }
        else {
            if (num - lastEven != 2) {
                System.out.println(lastEven + " -> " + msg.getText());
            }
            lastEven = num;
        }
        */

        /*
        // send ok back as acknowledgment
        buffer.clear();
        buffer.putInt(cMsgConstants.ok).flip();
        while (buffer.hasRemaining()) {
            channel.write(buffer);
        }
        */

        // fill in message object's members
        msg.setDomain(domainType);
        msg.setReceiver(client.getName());
        msg.setReceiverHost(client.getHost());
        msg.setReceiverTime(new Date()); // current time

        return msg;
    }


    /**
     * This method runs all appropriate callbacks - each in their own thread.
     * Different callbacks are run depending on the subject and type of the
     * incoming message.
     *
     * @param msg incoming message
     */
    private void runCallbacks(cMsgMessage msg) throws cMsgException {

        // if callbacks have been stopped, return
        if (!client.isReceiving()) {
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("runCallbacks: all callbacks have been stopped");
            }
            return;
        }

        HashSet subSet = client.subscriptions;
        Iterator iter = subSet.iterator();

        for (; iter.hasNext(); ) {
            cMsgSubscription sub = (cMsgSubscription) iter.next();

            // if the subject/type id's match, run callbacks for this sub/type
            if (sub.id == msg.getReceiverSubscribeId()) {
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("runCallbacks: found subscription to match receiveSubscriberId = " + sub.id);
                }

                // run through all callbacks
                Iterator iter2 = sub.callbacks.iterator();
                for (; iter2.hasNext();) {
                    cMsgCallbackThread cbThread = (cMsgCallbackThread) iter2.next();
                    // Tell the callback thread to wakeup and run the callback.
                    cbThread.sendMessage(msg);
                    synchronized (cbThread) {
                        cbThread.notify();
                    }
                }

                break;
            }
        }
    }

}


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

package org.jlab.coda.cMsg.cMsgDomain;

import org.jlab.coda.cMsg.cMsgConstants;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgException;

import java.nio.channels.ServerSocketChannel;
import java.nio.channels.Selector;
import java.nio.channels.SelectionKey;
import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;
import java.io.IOException;
import java.util.*;
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
    private String domainType = "cMsg";

    /** cMsg client that created this object. */
    private cMsg client;

    /** Server channel (contains socket). */
    private ServerSocketChannel serverChannel;

    /** A direct buffer is necessary for nio socket IO. */
    private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

    /** Allocate int array once (used for reading in data) for efficiency's sake. */
    private int[] inComing = new int[14];

    /** List of all receiverSubscribeIds that match the incoming message. */
    private int[] rsIds = new int[20];
    private int rsIdCount = 0;

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
    public cMsgClientListeningThread(cMsg myClient, ServerSocketChannel channel) {

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

            // cMsg object is waiting for this thread to start in connect method
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
//BUG BUG not the best way to handle an error
                 return;
             }

             // make buffer readable
             buffer.flip();

             // read client's request
             int msgId = buffer.getInt();

             cMsgMessage msg;

             switch (msgId) {

                 case cMsgConstants.msgSubscribeResponse: // receiving a message
                     // read the message here
                     msg = readIncomingMessage(channel);

                     // run callbacks for this message
                     runCallbacks(msg);

                     break;

                 case cMsgConstants.msgGetResponse: // receiving a message for specific get
                     // read the message here
                     msg = readIncomingMessage(channel);

                     // run callbacks for this message
                     wakeGets(msg);

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

        // keep reading until we have 14 ints of data
        cMsgUtilities.readSocketBytes(buffer, channel, 56, debug);

        // go back to reading-from-buffer mode
        buffer.flip();

        // read 14 ints
        buffer.asIntBuffer().get(inComing, 0, 14);

        msg.setVersion(inComing[0]);
        msg.setPriority(inComing[1]);
        msg.setUserInt(inComing[2]);
        msg.setGetRequest(inComing[3] == 0 ? false : true);
        // time sent in seconds since midnight GMT, Jan 1, 1970
        msg.setSenderTime(new Date(((long)inComing[4])*1000));
        msg.setUserTime(new Date(((long)inComing[5])*1000));
        msg.setSysMsgId(inComing[6]);
        msg.setSenderToken(inComing[7]);
        // String lengths
        int lengthSender     = inComing[8];
        int lengthSenderHost = inComing[9];
        int lengthSubject    = inComing[10];
        int lengthType       = inComing[11];
        int lengthText       = inComing[12];

        // number of receiverSubscribe ids to come
        rsIdCount = inComing[13];

        if (rsIdCount > 0) {
            // keep reading until we have "rsIdCount" ints of data
            cMsgUtilities.readSocketBytes(buffer, channel, rsIdCount*4, debug);

            // go back to reading-from-buffer mode
            buffer.flip();

            // make sure we have enough space in the int array
            if (rsIdCount > rsIds.length) {
                rsIds = new int[rsIdCount];
            }

            // read ints into array for future use
            buffer.asIntBuffer().get(rsIds, 0, rsIdCount);
        }

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
     * incoming message. It also wakes up all active general-get methods.
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

        cMsgHolder holder;


        cMsgCallbackThread cbThread;

        // handle subscriptions
        Set set = client.subscriptions;
        Iterator iter, iter2;

        if (client.subscriptions.size() > 0) {
            // set is NOT modified here
            synchronized (set) {
                iter = set.iterator();

                // for each subscription of this client ...
                while (iter.hasNext()) {
                    cMsgSubscription sub = (cMsgSubscription) iter.next();

                    // run through list of receiverSubscribeIds that msg matches
                    for (int i = 0; i < rsIdCount; i++) {

                        // if the subject/type id's match, run callbacks for this sub/type
                        if (sub.getId() == rsIds[i]) {

                            // run through all callbacks
                            iter2 = sub.getCallbacks().iterator();
                            while (iter2.hasNext()) {
                                cbThread = (cMsgCallbackThread) iter2.next();
                                cbThread.sendMessage(msg);
//System.out.println("Sending wakeup for SUBSCRIBE");
                                // Tell one callback thread to wakeup and run the callback.
                                cbThread.wakeup();
                            }

                            // look at next subscription
                            break;
                        }
                    }
                }
            }
        }

        if (client.generalGets.size() < 1) return;

        // run through list of receiverSubscribeIds that msg matches
        for (int i = 0; i < rsIdCount; i++) {
            // take care of any general gets first
            holder = (cMsgHolder) (client.generalGets.remove(rsIds[i]));

            if (holder != null) {
// BUG BUG copy message??

                holder.message = msg;
//System.out.println("Sending notify for subscribeAndGet");
                // Tell the get-calling thread to wakeup and retrieved the held msg
                synchronized (holder) {
                    holder.notify();
                }
            }
        }
    }


    /**
     * This method wakes up all active specific-get methods.
     *
     * @param msg incoming message
     */
    private void wakeGets(cMsgMessage msg) {

        // if gets have been stopped, return
        if (!client.isReceiving()) {
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("wakeSpecificGet: all gets have been stopped");
            }
            return;
        }

        // handle specific get (aimed at a receiver)
        cMsgHolder holder = (cMsgHolder) client.specificGets.remove(msg.getSenderToken());

        if (holder == null) {
            return;
        }
// BUG BUG copy message??
        holder.message = msg;
        // Tell the get-calling thread to wakeup and retrieved the held msg
        synchronized (holder) {
            holder.notify();
        }

        return;
    }


}


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

import org.jlab.coda.cMsg.*;

import java.nio.channels.ServerSocketChannel;
import java.nio.channels.Selector;
import java.nio.channels.SelectionKey;
import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;
import java.io.IOException;
import java.util.*;

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
    private int[] inComing = new int[17];

    /** List of all receiverSubscribeIds that match the incoming message. */
    private int[] rsIds = new int[20];
    private int rsIdCount = 0;

    /** Does the server want an acknowledgment returned? */
    private boolean acknowledge;

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
        // die if no more non-daemon thds running
        setDaemon(true);
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

             cMsgMessageFull msg;

             switch (msgId) {

                 case cMsgConstants.msgSubscribeResponse: // receiving a message
                     // read the message here
                     msg = readIncomingMessage(channel);

                     // if server wants an acknowledgment, send one back
                     if (acknowledge) {
                         // send ok back as acknowledgment
                         buffer.clear();
                         buffer.putInt(cMsgConstants.ok).flip();
                         while (buffer.hasRemaining()) {
                             channel.write(buffer);
                         }
                     }

                     // run callbacks for this message
                     runCallbacks(msg);

                     break;

                 case cMsgConstants.msgGetResponse: // receiving a message for sendAndGet
                     // read the message here
                     msg = readIncomingMessage(channel);
                     msg.setGetResponse(true);

                     // if server wants an acknowledgment, send one back
                     if (acknowledge) {
                         // send ok back as acknowledgment
                         buffer.clear();
                         buffer.putInt(cMsgConstants.ok).flip();
                         while (buffer.hasRemaining()) {
                             channel.write(buffer);
                         }
                     }

                     // wakeup caller with this message
                     wakeGets(msg);

                     break;

                 case cMsgConstants.msgGetResponseIsNull: // receiving null for sendAndGet
                     // read the id to be notified
                     int token = readSenderToken(channel);

                     // if server wants an acknowledgment, send one back
                     if (acknowledge) {
                         // send ok back as acknowledgment
                         buffer.clear();
                         buffer.putInt(cMsgConstants.ok).flip();
                         while (buffer.hasRemaining()) {
                             channel.write(buffer);
                         }
                     }

                     // wakeup caller with null
                     wakeGets(token);

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

                     // If server wants an acknowledgment, send one back.
                     // Do this BEFORE running shutdown.
                     if (acknowledge) {
                         // send ok back as acknowledgment
                         buffer.clear();
                         buffer.putInt(cMsgConstants.ok).flip();
                         while (buffer.hasRemaining()) {
                             channel.write(buffer);
                         }
                     }

                     if (client.getShutdownHandler() != null) {
                         client.getShutdownHandler().handleShutdown();
                     }
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
     * This method reads an incoming cMsgMessageFull from a clientHandler object.
     *
     * @param channel nio socket communication channel
     * @throws IOException if socket read or write error
     * @return message read from channel
     */
    private cMsgMessageFull readIncomingMessage(SocketChannel channel) throws IOException {

        // create a message
        cMsgMessageFull msg = new cMsgMessageFull();

        // keep reading until we have 17 ints of data
        cMsgUtilities.readSocketBytes(buffer, channel, 68, debug);

        // go back to reading-from-buffer mode
        buffer.flip();

        // read ints
        buffer.asIntBuffer().get(inComing, 0, 17);

        msg.setVersion(inComing[0]);
        // inComing[1] is for future use
        msg.setUserInt(inComing[2]);
        msg.setInfo(inComing[3]);
        // time message was sent = 2 ints (hightest byte first)
        // in milliseconds since midnight GMT, Jan 1, 1970
        long time = ((long)inComing[4] << 32) | inComing[5];
        msg.setSenderTime(new Date(time));
        // user time
        time = ((long)inComing[6] << 32) | inComing[7];
        msg.setUserTime(new Date(time));
        msg.setSysMsgId(inComing[8]);
        msg.setSenderToken(inComing[9]);
        // String lengths
        int lengthSender     = inComing[10];
        int lengthSenderHost = inComing[11];
        int lengthSubject    = inComing[12];
        int lengthType       = inComing[13];
        int lengthText       = inComing[14];
        int lengthCreator    = inComing[15];
        acknowledge          = inComing[16] == 1 ? true : false;

        // bytes expected
        int bytesToRead = lengthSender + lengthSenderHost + lengthSubject +
                          lengthType + lengthText + lengthCreator;

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
        msg.setText(new String(bytes, bytesToRead-lengthText-lengthCreator, lengthText, "US-ASCII"));

        // read creator
        msg.setCreator(new String(bytes, bytesToRead-lengthCreator, lengthCreator, "US-ASCII"));

        // fill in message object's members
        msg.setDomain(domainType);
        msg.setReceiver(client.getName());
        msg.setReceiverHost(client.getHost());
        msg.setReceiverTime(new Date()); // current time

        return msg;
    }


    /**
     * This method reads incoming receiver-subscribe ids from a clientHandler object.
     *
     * @param channel nio socket communication channel
     * @return senderToken
     * @throws IOException
     */
    private int readSenderToken(SocketChannel channel) throws IOException {
        // keep reading until we have 2 ints of data
        cMsgUtilities.readSocketBytes(buffer, channel, 8, debug);

        // go back to reading-from-buffer mode
        buffer.flip();

        int token   = buffer.getInt();
        acknowledge = buffer.getInt() == 1 ? true : false;

        return token;
    }


    /**
     * This method runs all appropriate callbacks - each in their own thread.
     * Different callbacks are run depending on the subject and type of the
     * incoming message. It also wakes up all active general-get methods.
     *
     * @param msg incoming message
     */
    private void runCallbacks(cMsgMessageFull msg) throws cMsgException {

        // if callbacks have been stopped, return
        if (!client.isReceiving()) {
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("runCallbacks: all callbacks have been stopped");
            }
            return;
        }

        // handle subscriptions
        Set<cMsgSubscription> set = client.subscriptions;

        if (set.size() > 0) {

            // set is NOT modified here
            synchronized (set) {

                // for each subscription of this client ...
                for (cMsgSubscription sub : set) {

                    // if subject & type of incoming message equal those in subscription ...
                    if (msg.getSubject().matches(sub.getSubjectRegexp()) &&
                            msg.getType().matches(sub.getTypeRegexp())) {
//System.out.println(" handle send msg");

                        // run through all callbacks
                        for (cMsgCallbackThread cbThread : sub.getCallbacks()) {
                            cbThread.sendMessage(msg);
//System.out.println(" sent wakeup for SUBSCRIBE");
                        }
                    }
                }
            }
        }

        if (client.subscribeAndGets.size() < 1) return;

        // for each subscription of this client ...
        cMsgHolder holder;
        for (Iterator i = client.subscribeAndGets.values().iterator(); i.hasNext();) {
            holder = (cMsgHolder)i.next();
            if (cMsgMessageMatcher.matches(holder.subject, msg.getSubject()) &&
                cMsgMessageMatcher.matches(holder.type, msg.getType())) {
//System.out.println(" handle subscribeAndGet msg");

                holder.timedOut = false;
                holder.message  = msg.copy();
//System.out.println(" sending notify for subscribeAndGet");
                // Tell the get-calling thread to wakeup and retrieved the held msg
                synchronized (holder) {
                    holder.notify();
                }
            }
            i.remove();
        }
    }


    /**
     * This method wakes up an active sendAndGet method and delivers a message to it.
     *
     * @param msg incoming message
     */
    private void wakeGets(cMsgMessageFull msg) {

        // if gets have been stopped, return
        if (!client.isReceiving()) {
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("wakeGets: all gets have been stopped");
            }
            return;
        }

        cMsgHolder holder = client.sendAndGets.remove(msg.getSenderToken());

        if (holder == null) {
            return;
        }
        holder.timedOut = false;
        // Do NOT need to copy msg as only 1 receiver gets it
        holder.message = msg;

        // Tell the get-calling thread to wakeup and retrieve the held msg
        synchronized (holder) {
            holder.notify();
        }

        return;
    }


    /**
     * This method wakes up an active sendAndGet method and supplies
     * a null message to the client associated with senderToken.
     *
     * @param senderToken sendAndGet caller to wake up
     */
    private void wakeGets(int senderToken) {

        // if gets have been stopped, return
        if (!client.isReceiving()) {
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("wakeGets: all gets have been stopped");
            }
            return;
        }

        cMsgHolder holder = client.sendAndGets.remove(senderToken);

        if (holder == null) {
            return;
        }
        holder.timedOut = false;
        holder.message  = null;

        // Tell the get-calling thread to wakeup
        synchronized (holder) {
            holder.notify();
        }

        return;
    }


}


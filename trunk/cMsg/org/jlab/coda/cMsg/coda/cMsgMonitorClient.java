/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 8-Jul-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.coda;

import org.jlab.coda.cMsg.cMsgConstants;

import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.io.IOException;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Aug 10, 2004
 * Time: 2:34:11 PM
 * To change this template use File | Settings | File Templates.
 */
public class cMsgMonitorClient extends Thread {
    /** Client information object. */
    cMsgClientInfo   info;
    /** Domain server object. */
    cMsgDomainServer domainServer;
    /** Communication channel to client from domain server. */
    SocketChannel    channel;
    /** A direct buffer is necessary for nio socket IO. */
    ByteBuffer buffer = ByteBuffer.allocateDirect(2048);
    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugInfo;


    public cMsgMonitorClient(cMsgClientInfo info, cMsgDomainServer server) {
        this.info   = info;
        this.domainServer = server;

        try {
            channel = SocketChannel.open(new InetSocketAddress(info.clientHost, info.clientPort));
            // set socket options
            Socket socket = channel.socket();
            // Set tcpNoDelay so no packets are delayed
            socket.setTcpNoDelay(true);
            // set buffer sizes
            socket.setReceiveBufferSize(65535);
            socket.setSendBufferSize(65535);
        }
        catch (IOException e) {
            e.printStackTrace();
            // client has died, time to bail.
            if (debug >= cMsgConstants.debugError) {
                System.out.println("\ncMsgMonitorClient: cannot create socket to client " +
                                   info.clientName + "\n");
            }
        }

        info.channel = channel;

    }

    /** This method is executed as a thread. */
    public void run() {
        System.out.println("Running Client Monitor");

        while (true) {
            // get ready to write
            buffer.clear();

            // write 2 ints
            buffer.putInt(cMsgConstants.msgKeepAlive);
            buffer.putInt(1);

            try {
                // send buffer over the socket
                buffer.flip();
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("\ncMsgMonitorClient: will send keep alive to " +
                                       info.clientName + "\n");
                }
                while (buffer.hasRemaining()) {
                    channel.write(buffer);
                }
                // read acknowledgment & keep reading until we have 1 int of data
                cMsgUtilities.readSocketBytes(buffer, channel, 4, debug);
            }
            catch (IOException e) {
                e.printStackTrace();
                // client has died, time to bail.
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("\ncMsgMonitorClient: cannot communicate with client " +
                                       info.clientName + "\n");
                }
                domainServer.getClientHandler().unregisterClient(info.clientName);
                domainServer.setKillAllThreads(true);
                return;
            }

            // go back to reading-from-buffer mode
            buffer.flip();

            int error = buffer.getInt();

            if (error != cMsgConstants.ok) {
                // something wrong with the client, time to bail
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("\ncMsgMonitorClient: keep alive returns error from client " +
                                       info.clientName + "\n");
                }
                domainServer.getClientHandler().unregisterClient(info.clientName);
                domainServer.setKillAllThreads(true);
                return;
            }

            try {Thread.sleep(3000);}
            catch (InterruptedException e) {}
        }

    }

}

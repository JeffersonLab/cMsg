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

package org.jlab.coda.cMsg.cMsgDomain;

import org.jlab.coda.cMsg.cMsgConstants;
import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgClientInfo;

import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.io.IOException;

/**
 * This class implements an object to monitor the health of a cMsg client.
 * It does this by periodically sending a keepAlive command to the listening
 * thread of the client. If there is an error reading the response, the
 * client is assumed dead. All resources associated with that client are
 * recovered for reuse.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgMonitorClient extends Thread {
    /** Client information object. */
    private cMsgClientInfo info;

    /** Domain server object. */
    private cMsgDomainServer domainServer;

    /** Communication channel to client from domain server. */
    private SocketChannel channel;

    /** A direct buffer is necessary for nio socket IO. */
    private ByteBuffer buffer = ByteBuffer.allocateDirect(2048);

    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugNone;


    /**
     * Constructor. Creates a socket channel with the client.
     *
     * @param info object containing information about the client
     * @param server domain server which created this monitor
     */
    public cMsgMonitorClient(cMsgClientInfo info, cMsgDomainServer server) {
        this.info   = info;
        this.domainServer = server;

        try {
            channel = SocketChannel.open(new InetSocketAddress(info.getClientHost(),
                                                               info.getClientPort()));
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
                                   info.getName() + "\n");
            }
        }

        info.setChannel(channel);
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
                while (buffer.hasRemaining()) {
                    channel.write(buffer);
                }
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("cMsgMonitorClient: wrote keepAlive & 1 to client " +
                                       info.getName() + "\n");
                }
                // read acknowledgment & keep reading until we have 1 int of data
                cMsgUtilities.readSocketBytes(buffer, channel, 4, debug);
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("cMsgMonitorClient: read keepAlive client (" +
                                       info.getName() + ") response\n");
                }
            }
            catch (IOException e) {
                // client has died, time to bail.
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("cMsgMonitorClient: CANNOT COMMUNICATE with client " +
                                       info.getName() + "\n");
                }

                domainServer.killAllThreads();
                domainServer.requestCue.clear();

                try {
                    if (!domainServer.calledShutdown) {
                        domainServer.calledShutdown = true;
                        domainServer.getClientHandler().handleClientShutdown();
                    }
                }
                catch (cMsgException e1) {}

                return;
            }

            // go back to reading-from-buffer mode
            buffer.flip();

            int error = buffer.getInt();

            if (error != cMsgConstants.ok) {
                // something wrong with the client, time to bail
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("cMsgMonitorClient: keep alive returns ERROR from client " +
                                       info.getName() + "\n");
                }

                domainServer.killAllThreads();
                domainServer.requestCue.clear();
                
                try {
                    if (!domainServer.calledShutdown) {
                        domainServer.calledShutdown = true;
                        domainServer.getClientHandler().handleClientShutdown();
                    }
                }
                catch (cMsgException e1) {}

                return;
            }

            try {Thread.sleep(1000);}
            catch (InterruptedException e) {}
        }

    }

}

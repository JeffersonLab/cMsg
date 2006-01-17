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

package org.jlab.coda.cMsg.cMsgDomain.server;

import org.jlab.coda.cMsg.cMsgConstants;
import org.jlab.coda.cMsg.cMsgClientInfo;
import org.jlab.coda.cMsg.cMsgDomain.cMsgUtilities;

import java.nio.channels.SocketChannel;
import java.nio.channels.Selector;
import java.nio.channels.SelectionKey;
import java.nio.ByteBuffer;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.io.IOException;
import java.util.Iterator;

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
    private int debug;
    
    /**
     * Do a select before reading a keepalive response. This will catch
     * dead vxWorks tasks.
     */
    private Selector selector;


    /**
     * Constructor. Creates a socket channel with the client.
     *
     * @param info object containing information about the client
     * @param server domain server which created this monitor
     * @param debug level of debug output
     */
    public cMsgMonitorClient(cMsgClientInfo info, cMsgDomainServer server, int debug) {
        this.info   = info;
        this.debug  = debug;
        this.domainServer = server;

        try {
            // There is a problem with vxWorks clients in that its sockets are
            // global and do not close when the client disappears. In fact, the
            // board can reboot and the other end of the socket will know nothing
            // about this since apparently no "reset" is sent on that socket.
            // Since sockets have no timeout on Solaris, the only solution is to
            // do a select on the read and remove client when it times out.
            selector = Selector.open();

            channel = SocketChannel.open(new InetSocketAddress(info.getClientHost(),
                                                               info.getClientPort()));
            channel.configureBlocking(false);
            // set socket options
            Socket socket = channel.socket();
            // Set tcpNoDelay so no packets are delayed
            socket.setTcpNoDelay(true);
            // no need to set buffer sizes

            // register the channel with the selector for reads
            channel.register(selector, SelectionKey.OP_READ);

        }
        catch (IOException e) {
            e.printStackTrace();
            // client has died, time to bail.
            if (debug >= cMsgConstants.debugError) {
                System.out.println("\ncMsgMonitorClient: cannot create socket to client " +
                                   info.getName() + "\n");
            }
        }
    }

    /** This method is executed as a thread. */
    public void run() {

        while (true) {
            // get ready to write
            buffer.clear();

            // write 2 ints
            buffer.putInt(4); // # bytes to follow
            buffer.putInt(cMsgConstants.msgKeepAlive);

            try {
                // check to see if domain server is shutting down and we must die too
                if (domainServer.killSpawnedThreads) {
                    return;
                }

                // send buffer over the socket
                buffer.flip();
                while (buffer.hasRemaining()) {
                    channel.write(buffer);
                }
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("cMsgMonitorClient: wrote keepAlive & 1 to " +
                                       info.getName());
                }

                // wait 2 seconds for client to answer keepalive
                int n = selector.select(2000);

                // if no answer, this client is dead so remove it
                if (n == 0) {
                    try {
                        selector.close();
                        channel.close();
                    }
                    catch (IOException ex) {}

                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("cMsgMonitorClient: CANNOT COMMUNICATE with " +
                                           info.getName() + "\n");
                    }

                    if (domainServer.calledShutdown.compareAndSet(false,true)) {
                        //System.out.println("SHUTDOWN TO BE RUN BY monitor client thd");
                        domainServer.shutdown();
                    }
                    return;
                }

                // get an iterator of selected keys (ready sockets)
                Iterator it = selector.selectedKeys().iterator();

                // look at each key
                while (it.hasNext()) {
                    SelectionKey key = (SelectionKey) it.next();

                    // is a readable socket?
                    if (key.isValid() && key.isReadable()) {

                        // read acknowledgment & keep reading until we have 1 int of data
                        cMsgUtilities.readSocketBytes(buffer, channel, 4, debug);
                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println("  cMsgMonitorClient: read keepAlive response for " +
                                               info.getName() + "\n\n");
                        }

                    }
                    // remove key from selected set since it's been handled
                    it.remove();
                }

            }
            catch (IOException e) {
                // client has died, time to bail.
                try {
                    selector.close();
                    channel.close();
                }
                catch (IOException ex) {}

                if (debug >= cMsgConstants.debugError) {
                    System.out.println("cMsgMonitorClient: CANNOT COMMUNICATE with client " +
                                       info.getName() + "\n");
                }

                if (domainServer.calledShutdown.compareAndSet(false,true)) {
                    //System.out.println("SHUTDOWN TO BE RUN BY monitor client thd");
                    domainServer.shutdown();
                }
                return;
            }

            try {Thread.sleep(2000);}
            catch (InterruptedException e) {}
        }

    }

}

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
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.server;

import org.jlab.coda.cMsg.cMsgConstants;
import org.jlab.coda.cMsg.cMsgException;

import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.text.DateFormat;
import java.util.Date;

/**
 * This class implements an object to monitor the health of all cMsg clients connected
 * to this server and to provide these clients with evidence of this server's health.
 * It does this by doing 2 things: 1) it periodically sends monitoring data about
 * the whole cmsg system to all the clients, and 2) it reads monitoring data from
 * each client. If there is an error in reading, the client is assumed dead.
 * All resources associated with that client are recovered for reuse.
 *
 * @author Carl Timmer
 */
class cMsgMonitorClient extends Thread {

    /** Domain server object. */
    private cMsgNameServer server;

    /** A direct outBuffer is necessary for nio socket IO. */
    private ByteBuffer outBuffer;

    /** A direct outBuffer is necessary for nio socket IO. */
    private ByteBuffer outBuffer2;

    /** A direct outBuffer is necessary for nio socket IO. */
    private ByteBuffer inBuffer;

    /** Level of debug output for this class. */
    private int debug;

    /** Time in milliseconds to write keepalive to, and read keepalive from clients. */
    private final long updatePeriod = 2100;

    /** Time in milliseconds elapsed between client keepalives received before calling client dead. */
    private final long deadTime = 7000;

    /** Kill this thread if true. */
    volatile boolean killThisThread;

    private StringBuilder xml;


    /** Kills this thread. */
    void killThread() {
        killThisThread = true;
        this.interrupt();
    }
    
    /**
     * Copies an integer value into 4 bytes of a byte array.
     * @param intVal integer value
     * @param b byte array
     * @param off offset into the byte array
     */
    public static final void intToBytes(int intVal, byte[] b, int off) {
      b[off]   = (byte) ((intVal & 0xff000000) >>> 24);
      b[off+1] = (byte) ((intVal & 0x00ff0000) >>> 16);
      b[off+2] = (byte) ((intVal & 0x0000ff00) >>>  8);
      b[off+3] = (byte)  (intVal & 0x000000ff);
    }


    /**
     * Constructor. Creates a socket channel with the client.
     *
     * @param server domain server which created this monitor
     * @param debug level of debug output
     */
    public cMsgMonitorClient(cMsgNameServer server, int debug) {
        this.debug  = debug;
        this.server = server;

        outBuffer  = ByteBuffer.allocateDirect(4096);
        outBuffer2 = ByteBuffer.allocateDirect(1024);
        inBuffer   = ByteBuffer.allocateDirect(2048);
        xml        = new StringBuilder(1000);
    }


    public void run() {

        // There is a problem with vxWorks clients in that its sockets are
        // global and do not close when the client disappears. In fact, the
        // board can reboot and the other end of the socket will know nothing
        // about this since apparently no "reset" is sent on that socket.
        // Since sockets have no timeout on Solaris, the only solution is to
        // store times between receiving monitor data on a socket, and dealing
        // with those that take too long or timeout.

        while (true) {
            // update the XML string we're sending
            updateMonitorXML();

            // Prepare 2 buffers. One is for regular clients and the other is
            // for server clients.
            ByteBuffer outBuf;
            String passwd;
            int dataLength = server.fullMonitorXML.length();

            // Create outBuffer with data to be sent to regular clients.
            // Send:
            //      1) len of xml
            //      2) xml (state of system)
            //      3) how many other types of items to follow.
            //         This allows us to expand the info sent in future
            //         without messing up protocol (making it backwards compatible).
            //      4) how many cloud hosts
            //         for each cloud host send:
            //            5) tcpPort
            //            6) multicastPort
            //            7) len of host
            //            8) len of password
            //            9) host
            //           10) password
            while (true) {
                int totalLength = dataLength + 3*8;
                for (cMsgClientData cd : server.nameServers.values()) {
                    passwd = cd.getPassword();
                    if (passwd == null) passwd = "";
                    totalLength += 16 + cd.getServerHost().length() + passwd.length();
                }

                if (totalLength > outBuffer.capacity()) {
                    outBuffer = ByteBuffer.allocateDirect(totalLength + 1024);
                }
                outBuffer.clear();

//System.out.println("cMsgMonitorClient 1: xml data length = " + dataLength + ", total length = " + totalLength);
                try {
                    // put xml size first
                    outBuffer.putInt(dataLength);
                    // xml string comes next
                    outBuffer.put(server.fullMonitorXML.getBytes("US-ASCII"));
                    int size = server.nameServers.size();
                    // how many types of info to follow (just one - cloud servers)
                    outBuffer.putInt(1);
                    // how many servers
                    outBuffer.putInt(size);
//System.out.println("cMsgMonitorClient 1: buffer pos (after len,xml,#servers) = " + outBuffer.position());
                    if (size > 0) {
                        int count=0;
                        for (cMsgClientData cd : server.nameServers.values()) {
                            outBuffer.putInt(cd.getServerPort());
                            outBuffer.putInt(cd.getServerMulticastPort());
                            outBuffer.putInt(cd.getServerHost().length());
                            passwd = cd.getPassword();
                            if (passwd == null) passwd = "";
                            outBuffer.putInt(passwd.length());
                            outBuffer.put(cd.getServerHost().getBytes("US-ASCII"));
                            outBuffer.put(passwd.getBytes("US-ASCII"));
//System.out.println("cMsgMonitorClient 1: buffer pos = " + outBuffer.position());
                            count++;
                        }
                        if (count != size) {
                            // another thread messed us up, do it again
//System.out.println("cMsgMonitorClient 1: messed up try again");
                            continue;
                        }
                    }
                }
                catch (UnsupportedEncodingException e) {/* never get here */}
                break;
            }

            // create outBuffer with data to be sent to server clients
            dataLength = server.nsMonitorXML.length();
            if (dataLength + 8 > outBuffer2.capacity()) {
                // give outBuffer 1k more space than needed
                outBuffer2 = ByteBuffer.allocateDirect(dataLength + 1024);
            }
            outBuffer2.clear();
//System.out.println("Buffer2 capacity = " + outBuffer2.capacity());
            // put size first
            outBuffer2.putInt(dataLength);

            // string comes next
            try { outBuffer2.put(server.nsMonitorXML.getBytes("US-ASCII")); }
            catch (UnsupportedEncodingException e) {/* never get here */}

            // how types of info to follow - nothing
            outBuffer2.putInt(0);

            // get outBuffer ready for writing
            outBuffer.flip();
            outBuffer2.flip();

            for (cMsgDomainServerSelect dss : server.domainServersSelect.keySet()) {

                if (dss.clientIsServer()) {
                    outBuf = outBuffer2;
                }
                else {
                    outBuf = outBuffer;
                }

                // concurrent hashmap so no sync required
                for (cMsgClientData cd : dss.clients.keySet()) {
                    // send monitor info to client
                    // BUGBUG don't send time first as before

                    // get outBuffer ready for writing
                    outBuf.rewind();
                    try {
                        // write monitor info to client
                        while (outBuf.hasRemaining()) {
//System.out.println("Write KA info to " + cd.getName());
                            cd.keepAliveChannel.write(outBuf);
                        }

                        // read monitor info from client
                        readMonitorInfo(cd);

                        // check for non-responding client
                        if (clientDead(cd)) {
                            // do something
                            if (debug >= cMsgConstants.debugError) {
                                System.out.println("cMsgMonitorClient: client " + cd.getName() +
                                        " has not responded for " + (deadTime/1000) + " seconds at " + (new Date()) + ", so consider it dead");
                            }
                            dss.deleteClient(cd);
                        }
                    }
                    catch (cMsgException e) {
                        // during read: internal cMsg protocol error, or too many tries to read data
                        if (debug >= cMsgConstants.debugError) {
                            System.out.println("cMsgMonitorClient: client " + cd.getName() +
                                    " has keepalive protocol error or too many tries to read data");
                        }
                        dss.deleteClient(cd);
                    }
                    catch (IOException e) {
                        // socket & therefore client dead
                        if (debug >= cMsgConstants.debugError) {
                            System.out.println("cMsgMonitorClient: IO error, client " + cd.getName() + " is dead");
                        }
                        dss.deleteClient(cd);
                    }
                }
            }

            for (cMsgDomainServer ds : server.domainServers.keySet()) {
                // send monitor info to client
                // BUGBUG don't send time first as before
                if (ds.info.isServer()) {
                    outBuf = outBuffer2;
                }
                else {
                    outBuf = outBuffer;
                }

                // get outBuffer ready for writing
                outBuf.rewind();
                try {
                    // write monitor info to client
                    while (outBuf.hasRemaining()) {
                        ds.info.keepAliveChannel.write(outBuf);
                    }

                    // read monitor info from client
                    readMonitorInfo(ds.info);

                    // check for non-responding client
                    if (clientDead(ds.info)) {
                        // do something
                        if (debug >= cMsgConstants.debugError) {
                            System.out.println("cMsgMonitorClient: (ds) client " + ds.info.getName() +
                                    " has not responded for " + (deadTime/1000) + " seconds, so consider it dead");
                        }
                        ds.shutdown();
                    }
                }
                catch (cMsgException e) {
                    // during read: internal cMsg protocol error, or too many tries to read data
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("cMsgMonitorClient: (ds) client " + ds.info.getName() +
                                " has keepalive protocol error or too many tries to read data");
                    }
                    ds.shutdown();
                }
                catch (IOException e) {
                    // socket & therefore client dead
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("cMsgMonitorClient: IO error, (ds) client " + ds.info.getName() + " is dead");
                    }
                    ds.shutdown();
                }
            }

            // wait 2 seconds between rounds
            try { Thread.sleep(updatePeriod); }
            catch (InterruptedException e) {}

            if (killThisThread) return;
        }

    }


    /**
     * This method checks to see if the client has died (its time has not been updated).
     * 
     * @param cd client data object
     * @return true if client is dead, else false
     */
    private boolean clientDead(cMsgClientData cd) {
        return (System.currentTimeMillis() - cd.updateTime) > deadTime;
    }


    /**
     * This method reads a given of number of bytes from a nonblocking channel into a buffer.
     * If there is nothing to read, zero can be returned. If not, the full amount is read.
     * If it takes too many tries to read all the data, an exception is thrown.
     *
     * @param buffer       a byte buffer which channel data is read into
     * @param channel      nio socket communication channel
     * @param bytes        minimum number of bytes to read from channel
     * @param returnOnZero return immediately if read returns 0 bytes
     *
     * @return number of bytes read
     * @throws cMsgException If too many tries used to read all the data
     * @throws IOException   If channel is closed or cannot be read from
     */
    private int readSocketBytes(ByteBuffer buffer, SocketChannel channel, int bytes, boolean returnOnZero)
            throws cMsgException, IOException {

        if (bytes <= 0) return 0;

        int n, tries = 0, count = 0, maxTries=200;
        
        buffer.clear();
        buffer.limit(bytes);

        // Keep reading until we have exactly "bytes" number of bytes,
        // or have tried "tries" number of times to read.

        while (count < bytes) {
//System.out.println("readSocketBytes: try reading, channel is blocking = " + channel.isBlocking());
            if ((n = channel.read(buffer)) < 0) {
System.out.println("readSocketBytes: client's socket is dead");
                throw new IOException("readSocketBytes: client's socket is dead");
            }
//System.out.println("readSocketBytes: done reading");

            count += n;
            if (count >= bytes) {
                break;
            }
            else if (count == 0 && returnOnZero) {
                return 0;
            }

            tries++;
            if (tries >= maxTries) {
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("readSocketBytes: called read " + tries + " times, read " + count + " bytes");
                }
                throw new cMsgException("readSocketBytes: too many tries to read " + bytes + " bytes, read only " + count);
            }

            // wait minimum amount
            try { Thread.sleep(1); }
            catch (InterruptedException e) {}
//System.out.println("readSocketBytes: called read again");
        }
        return count;
    }


    /**
     * This method reads a nonblocking socket for monitor data from a cMsg client.
     *
     * @param cd client data object
     * @throws cMsgException If too many tries used to read all the data or internal protocol error
     * @throws IOException   If channel is closed or cannot be read from
     */
    private void readMonitorInfo(cMsgClientData cd) throws cMsgException, IOException {
        // read as much as possible, since we don't want keepalive signals to pile up
        while (true) {
            // read 1 int of data
//System.out.println("Try reading KA int from " + cd.getName());
            int n = readSocketBytes(inBuffer, cd.keepAliveChannel, 4, true);
            if (n == 0) {
                // if brand new client, pretend things are OK
                if (cd.updateTime == 0L) {
                    cd.updateTime = System.currentTimeMillis();
                }
//                if (debug >= cMsgConstants.debugWarn) {
//                    System.out.println("cMsgMonitorClient: nothing to read from client " + cd.getName() + " at " +
//                            (new Date()).toString() + ", go to next client");
//                }
                return;
            }
//System.out.println("Read KA int from " + cd.getName());
            inBuffer.flip();
            int size = inBuffer.getInt();          // size of buffer data in bytes
            if (size > inBuffer.capacity()) {
                inBuffer = ByteBuffer.allocateDirect(size + 512);
            }
            else if (size < 0) {
                throw new cMsgException("Internal cMsg keepalive protocol error");
            }

            if (size > 0) {
                // read size bytes of data
                inBuffer.clear();
                readSocketBytes(inBuffer, cd.keepAliveChannel, size, false);
                inBuffer.flip();
                size                          = inBuffer.getInt();   // xml length in chars/bytes
                cd.monData.isJava             = inBuffer.getInt() == 1;
                cd.monData.pendingSubAndGets  = inBuffer.getInt();
                cd.monData.pendingSendAndGets = inBuffer.getInt();

                cd.monData.clientTcpSends     = inBuffer.getLong();
                cd.monData.clientUdpSends     = inBuffer.getLong();
                cd.monData.clientSyncSends    = inBuffer.getLong();
                cd.monData.clientSendAndGets  = inBuffer.getLong();
                cd.monData.clientSubAndGets   = inBuffer.getLong();
                cd.monData.clientSubscribes   = inBuffer.getLong();
                cd.monData.clientUnsubscribes = inBuffer.getLong();

                byte[] buf = new byte[size];
                inBuffer.get(buf, 0, size);
                cd.monData.monXML = new String(buf, 0, size, "US-ASCII");
                // record when keepalive info received
                cd.updateTime = System.currentTimeMillis();
                // System.out.println("Print XML:\n" + server.monData.monXML);
//System.out.println("Read KA XML from " + cd.getName());
            }
        }
    }


    /**
     * This method updates the XML string representing the state of this server and the
     * XML string representing the state of the complete cMsg system - cMsg subdomain.
     */
    public void updateMonitorXML() {

        DateFormat dateFormat = DateFormat.getDateTimeInstance(DateFormat.FULL,DateFormat.FULL);

        xml.delete(0, xml.capacity());

        // Gather all the xml monitor data into 1 place for final
        // distribution to clients asking for it in XML format.
        xml.append("\n  <server  name=\"");
        xml.append(server.serverName);
        xml.append("\"  host=\"");
        xml.append(server.getHost());
        xml.append("\"  tcpPort=\"");
        xml.append(server.getPort());
        xml.append("\"  domainPort=\"");
        xml.append(server.getDomainPort());
        xml.append("\"  multicastPort=\"");
        xml.append(server.getMulticastPort());
        xml.append("\" >\n");
        String indent1 = "      ";

        for (cMsgDomainServer ds : server.domainServers.keySet()) {
//System.out.println("       >>XML: ns looking at client " + ds.info.getName());
            // Skip other servers' bridges to us,
            // they're not real clients.
            if (ds.info.isServer()) {
//System.out.println("       >>XML: Skipping other server's bridge client");
                continue;
            }

            // subdomain
            String sd = ds.info.getSubdomain();

            xml.append("\n    <client  name=\"");
            xml.append(ds.info.getName());
            xml.append("\"  subdomain=\"");
            xml.append(sd);
            xml.append("\">\n");

            // time created
            xml.append(indent1);
            xml.append("<timeConnected>");
            xml.append(dateFormat.format(ds.info.monData.birthday));
            xml.append("</timeConnected>\n");

            // namespace
            String ns = ds.info.getNamespace();
            if (ns != null) {
                // get rid of beginning slash (add by subdomain)
                ns = ns.substring(1, ns.length());
                xml.append(indent1);
                xml.append("<namespace>");
                xml.append(ns);
                xml.append("</namespace>\n");
            }

            // list subscriptions sent from client (cmsg subdomain only)
            if (sd != null && sd.equalsIgnoreCase("cmsg") && ds.info.monData.monXML != null) {
                xml.append(indent1);
                xml.append("<sendStats");

                xml.append("  tcpSends=\"");
                xml.append(ds.info.monData.clientTcpSends);

                xml.append("\"  udpSends=\"");
                xml.append(ds.info.monData.clientUdpSends);

                xml.append("\"  syncSends=\"");
                xml.append(ds.info.monData.clientSyncSends);

                xml.append("\"  sendAndGets=\"");
                xml.append(ds.info.monData.clientSendAndGets);
                xml.append("\" />\n");

                xml.append(indent1);
                xml.append("<subStats");

                xml.append("   subscribes=\"");
                xml.append(ds.info.monData.clientSubscribes);

                xml.append("\"  unsubscribes=\"");
                xml.append(ds.info.monData.clientUnsubscribes);

                xml.append("\"  subAndGets=\"");
                xml.append(ds.info.monData.clientSubAndGets);
                xml.append("\" />\n");

                // add subscription & callback stuff here (from client)
//System.out.println("       >>XML: ns adding from client: \n" + ds.info.monData.monXML);
                xml.append(ds.info.monData.monXML);
            }
            else {
                xml.append(indent1);
                xml.append("<sendStats");

                xml.append("  tcpSends=\"");
                xml.append(ds.info.monData.tcpSends);

                xml.append("\"  udpSends=\"");
                xml.append(ds.info.monData.udpSends);

                xml.append("\"  syncSends=\"");
                xml.append(ds.info.monData.syncSends);

                xml.append("\"  sendAndGets=\"");
                xml.append(ds.info.monData.sendAndGets);
                xml.append("\" />\n");

                xml.append(indent1);
                xml.append("<subStats");

                xml.append("  subscribes=\"");
                xml.append(ds.info.monData.subscribes);

                xml.append("\"  unsubscribes=\"");
                xml.append(ds.info.monData.unsubscribes);

                xml.append("\"  subAndGets=\"");
                xml.append(ds.info.monData.subAndGets);
                xml.append("\" />\n");
            }
            xml.append("    </client>\n");
        }

        for (cMsgDomainServerSelect dss : server.domainServersSelect.keySet()) {
            for (cMsgClientData cd : dss.clients.keySet()) {
//System.out.println("       >>XML: ns looking at client " + cd.getName());
                // Skip other servers' bridges to us,
                // they're not real clients.
                if (cd.isServer()) {
//System.out.println("       >>XML: skipping other server's bridge client");
                    continue;
                }

                // subdomain
                String sd = cd.getSubdomain();

                xml.append("\n    <client  name=\"");
                xml.append(cd.getName());
                xml.append("\"  subdomain=\"");
                xml.append(sd);
                xml.append("\">\n");

                // time created
                xml.append(indent1);
                xml.append("<timeConnected>");
                xml.append(dateFormat.format(cd.monData.birthday));
                xml.append("</timeConnected>\n");

                // namespace
                String ns = cd.getNamespace();
                if (ns != null) {
                    // get rid of beginning slash (add by subdomain)
                    ns = ns.substring(1, ns.length());
                    xml.append(indent1);
                    xml.append("<namespace>");
                    xml.append(ns);
                    xml.append("</namespace>\n");
                }

                // list subscriptions sent from client (cmsg subdomain only)
                if (sd != null && sd.equalsIgnoreCase("cmsg") && cd.monData.monXML != null) {
                    xml.append(indent1);
                    xml.append("<sendStats");

                    xml.append("  tcpSends=\"");
                    xml.append(cd.monData.clientTcpSends);

                    xml.append("\"  udpSends=\"");
                    xml.append(cd.monData.clientUdpSends);

                    xml.append("\"  syncSends=\"");
                    xml.append(cd.monData.clientSyncSends);

                    xml.append("\"  sendAndGets=\"");
                    xml.append(cd.monData.clientSendAndGets);
                    xml.append("\" />\n");

                    xml.append(indent1);
                    xml.append("<subStats");

                    xml.append("   subscribes=\"");
                    xml.append(cd.monData.clientSubscribes);

                    xml.append("\"  unsubscribes=\"");
                    xml.append(cd.monData.clientUnsubscribes);

                    xml.append("\"  subAndGets=\"");
                    xml.append(cd.monData.clientSubAndGets);
                    xml.append("\" />\n");

                    // add subscription & callback stuff here (from client)
//System.out.println("       >>XML: ns adding from client: \n" + dss.monData.monXML);
                    xml.append(cd.monData.monXML);
                }
                else {
                    xml.append(indent1);
                    xml.append("<sendStats");

                    xml.append("  tcpSends=\"");
                    xml.append(cd.monData.tcpSends);

                    xml.append("\"  udpSends=\"");
                    xml.append(cd.monData.udpSends);

                    xml.append("\"  syncSends=\"");
                    xml.append(cd.monData.syncSends);

                    xml.append("\"  sendAndGets=\"");
                    xml.append(cd.monData.sendAndGets);
                    xml.append("\" />\n");

                    xml.append(indent1);
                    xml.append("<subStats");

                    xml.append("  subscribes=\"");
                    xml.append(cd.monData.subscribes);

                    xml.append("\"  unsubscribes=\"");
                    xml.append(cd.monData.unsubscribes);

                    xml.append("\"  subAndGets=\"");
                    xml.append(cd.monData.subAndGets);
                    xml.append("\" />\n");
                }
                xml.append("    </client>\n");
            }
        }

        xml.append("\n  </server>\n\n");

        // store this as an xml string describing local server only
        server.nsMonitorXML = xml.toString();
//System.out.println("       >>XML: NS xml size = " + xml.length());

        xml.insert(0,"<cMsgMonitorData  domain=\"cmsg\">\n\n");

        // allow no changes to "bridges" while iterating
        synchronized (server.bridges) {
            // foreach bridge ...
            for (cMsgServerBridge b : server.bridges.values()) {
                xml.append(b.client.monitorXML);
//                if ( b.client.monitorXML != null)
//System.out.println("       >>XML: client's monitorXML size = " + b.client.monitorXML.length());
            }
        }
        xml.append("</cMsgMonitorData>\n");

        // store this as an xml string describing all monitor data
        server.fullMonitorXML = xml.toString();
//System.out.println("       >>XML: full xml size = " + xml.length());
//System.out.println("       >>XML: fullMonitorXML = \n" + fullMonitorXML);

    }


}

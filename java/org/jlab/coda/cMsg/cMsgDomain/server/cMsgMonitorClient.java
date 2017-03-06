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

import java.nio.channels.*;
import java.nio.ByteBuffer;
import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.text.DateFormat;
import java.util.Date;
import java.util.Iterator;
import java.util.concurrent.*;

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

    /** Create XML string for output. */
    private StringBuilder xml;

    /** A direct output buffer is necessary for nio socket IO. */
    private ByteBuffer outBuffer;

    /** A direct output buffer is necessary for nio socket IO. */
    private ByteBuffer outBuffer2;

    /** A direct input buffer is necessary for nio socket IO. */
    private ByteBuffer inBuffer;

    /** Level of debug output for this class. */
    private int debug;

    /** Time in milliseconds to attempt to read keepalive data from clients. */
    private final int readTimeout = 3000;

    /** Time in milliseconds between writes of monitoring data to clients. */
    private final long updatePeriod = 2000;

    /** Time in milliseconds elapsed between client keepalives received before calling client dead. */
    private final long deadTime = 120000;

    /**
     * Do we send and read meaningful monitoring data (false)?
     * Or do we send the minimum and ignore what we read (true)?
     */
    private boolean monitoringOff;

    /**
     * Set of all clients waiting to be registered with the selector of this domain server.
     * The value is just a dummy so the concurrent hashmap could be used.
     */
    private ConcurrentHashMap<cMsgClientData, String> clients2register;

    /** Set of all clients. The value is cMsgDomainServer(Select) object. */
    private ConcurrentHashMap<cMsgClientData, Object> clients;

    /** Selector object each client's channel is registered with. Used for reading. */
    private Selector selector;

    /**
     * This member is a pool of threads for reading all the monitoring data coming in.
     * The reason a pool is desirable is because if a single thread
     * is used, one uncooperative client can block the reading of all
     * clients. It limits how much time we can spend trying to read
     * one guy and put all the rest on hold.
     *
     * Run up to 3 threads with infinite queue. Wait 1 min before terminating
     * extra (more than 1) unused threads.
     */
    private ThreadPoolExecutor readingThreadPool;

    /** Kill these threads if true. */
    volatile private boolean killThreads;


    /** Shuts down all client monitoring threads. */
    void shutdown() {
        readingThreadPool.shutdownNow();
        killThreads = true;
        this.interrupt();
    }


    /**
     * Constructor. Creates a socket channel with the client.
     *
     * @param server domain server which created this monitor
     * @param debug level of debug output
     * @throws IOException if selector cannot be opened
     */
    public cMsgMonitorClient(cMsgNameServer server, int debug) throws IOException {
        this.debug  = debug;
        this.server = server;

        monitoringOff = server.monitoringOff;
        outBuffer  = ByteBuffer.allocateDirect(4096);
        outBuffer2 = ByteBuffer.allocateDirect(1024);
        inBuffer   = ByteBuffer.allocateDirect(2048);
        xml        = new StringBuilder(1000);
        clients          = new ConcurrentHashMap<cMsgClientData, Object>(500);
        clients2register = new ConcurrentHashMap<cMsgClientData, String>(100);

        // Create thread pool. Run up to 3 threads with infinite queue.
        // Wait 1 min before terminating extra (more than 1) unused threads.
        readingThreadPool = new ThreadPoolExecutor(1, 3, 60L, TimeUnit.SECONDS,
                                                   new LinkedBlockingQueue<Runnable>());

        // allow clients to be registered with the selector
        selector = Selector.open();

        // start thread for reading
        ReadMonitorData rd = new ReadMonitorData();
        rd.start();

        // start thread for writing
        WriteMonitorData wr = new WriteMonitorData();
        wr.start();
    }


    /**
     * Method to add another client for monitoring.
     * @param info information on client
     */
    void addClient(cMsgClientData info, cMsgDomainServerSelect dss) throws IOException {
        // Put client's channel in list to be registered with the selector for reading
        // (once selector is woken up).
        clients2register.put(info, "");
        clients.put(info, dss);
        selector.wakeup();
        // assume client is alive even though it hasn't sent anything yet
        info.updateTime = System.currentTimeMillis();
    }


    /**
     * Method to add another client for monitoring.
     * @param info information on client
     */
    void addClient(cMsgClientData info, cMsgDomainServer ds) throws IOException {
        // Put client's channel in list to be registered with the selector for reading
        // (once selector is woken up).
        clients2register.put(info, "");
        clients.put(info, ds);
        selector.wakeup();
        // assume client is alive even though it hasn't sent anything yet
        info.updateTime = System.currentTimeMillis();
    }

    
    /**
     * Method to remove a client from being monitored.
     * @param info information on client
     */
    void removeClient(cMsgClientData info) {
        // Remove client from list of clients the selector uses for reading
        clients2register.remove(info);
        clients.remove(info);
    }


    /**
     * Class used to read monitoring info from clients by being run in a thread pool.
     */
    private class DataReader implements Runnable {

        cMsgClientData cd;

        /** Constructor. */
        public DataReader(cMsgClientData cd) {
            this.cd = cd;
        }


        public void run() {

            Object dssObject;
            cMsgDomainServer ds;
            cMsgDomainServerSelect dss;

            try {
                // read monitor info from client
                if (monitoringOff) {
//System.out.println("Read&Dump from " + cd.getName());
                    readAndDumpMonitorInfo(cd);
                }
                else {
                    // timeout
//System.out.println("Read from " + cd.getName());
                    readMonitorInfo(cd, readTimeout);
                }
//System.out.println("Done reading");
            }
            catch (TimeoutException e) {
                // Let the WriteMonitorData thread kill clients if there's been too long
                // between reading. Since we cannot control whether a client writes,
                // checking times from read to read may not work. Do this in write
                // thread which is scheduled at a regular period.
            }
            catch (cMsgException e) {
                // during read: internal cMsg protocol error
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("cMsgMonitorClient: client " + cd.getName() +
                                       " internal protocol error");
                }

                dssObject = clients.get(cd);
                // Writing thread may delete this client first, so check for null.
                if (dssObject != null) {
                    if (dssObject instanceof cMsgDomainServerSelect) {
                        dss = (cMsgDomainServerSelect) dssObject;
                        dss.deleteClient(cd);
                    }
                    else {
                        ds = (cMsgDomainServer) dssObject;
                        ds.shutdown();
                    }

                    removeClient(cd);

                    if (debug >= cMsgConstants.debugSevere) {
                        System.out.println("Client " + cd.getName() + " error reading during keepalive, remove");
                    }
                }
            }
            catch (IOException e) {
                // socket & therefore client dead
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("cMsgMonitorClient: IO error, client " +
                            cd.getName() + " protocol error or is dead");
                }

                dssObject = clients.get(cd);
                if (dssObject != null) {
                    if (dssObject instanceof cMsgDomainServerSelect) {
                        dss = (cMsgDomainServerSelect) dssObject;
                        dss.deleteClient(cd);
                    }
                    else {
                        ds = (cMsgDomainServer) dssObject;
                        ds.shutdown();
                    }

                    removeClient(cd);

                    if (debug >= cMsgConstants.debugSevere) {
                        System.out.println("Client " + cd.getName() + " I/O error in keepalive, remove");
                    }
                }
            }
        }
    }


    /**
     * This Class uses select to read monitoring data from clients.
     */
    private class ReadMonitorData extends Thread {

        public void run() {

            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  cMsgMonitorClient(read): Running Reader Thread");
            }

            // tell startServer that this thread has started
            server.preConnectionThreadsStartedSignal.countDown();

            int n;
            cMsgClientData clientData = null;

            try {

                while (true) {

                    // 1 second timeout
                    n = selector.select(1000);

                    // register any clients waiting for it
                    if (clients2register.size() > 0) {
                        for (Iterator it = clients2register.keySet().iterator(); it.hasNext();) {
                            clientData = (cMsgClientData)it.next();
                            if (debug >= cMsgConstants.debugInfo) {
                                System.out.println("  cMsgMonitorClient(read): Registering client " + clientData.getName() + " with selector");
                            }

                            try {
                                clientData.keepAliveChannel.register(selector, SelectionKey.OP_READ, clientData);
                            }
                            catch (ClosedChannelException e) { /* if we can't register it, client is dead already */ }
                            catch (IllegalArgumentException e) { /* never happen */ }
                            catch (IllegalBlockingModeException e) { /* never happen */}

                            it.remove();
                        }
                    }

                    // first check to see if we've been commanded to die
                    if (killThreads) {
                        System.out.println("    cMsgMonitorClient(read): ending main thread 1");
                        return;
                    }

                    // if no channels (sockets) are ready, listen some more
                    if (n < 1) {
//System.out.println("    cMsgMonitorClient(read): selector woke up with no ready channels");
                        selector.selectedKeys().clear();
                        continue;
                    }

                    // get an iterator of selected keys (ready sockets)
                    Iterator it = selector.selectedKeys().iterator();
                    DataReader reader;

                    // look at each key
                    while (it.hasNext()) {
                        SelectionKey key = (SelectionKey) it.next();

                        // channel ready to read?
                        if (key.isValid() && key.isReadable()) {

                            // cMsgClientData object of TCP channel being read
                            clientData = (cMsgClientData) key.attachment();
                            if (clientData == null) {
//                                key.cancel();
                                it.remove();
                                continue;
                            }

                            // have client's monitor data read by a thread in the thread pool
                            reader = new DataReader(clientData);
                            readingThreadPool.execute(reader);

                        } // if valid key

                        // remove key from selected set since it's been handled
                        it.remove();
                    }

                    // delay next round of reading for 1 second
                    try {Thread.sleep(1000);}
                    catch (InterruptedException e) { }
                }
            }
            catch (IOException ex) {
                ex.printStackTrace();
            }
            finally {
                try {selector.close();}
                catch (IOException e) { }
            }
//System.out.println("    cMsgMonitorClient(read): ending thread");

            return;
        }
    }


    /**
     * This method reads a given of number of bytes from a nonblocking channel into a buffer.
     * If there is nothing to read, zero can be returned. If not, the full amount is read.
     * If it takes too many tries to read all the data, an exception is thrown.
     *
     * @param buffer        a byte buffer which channel data is read into
     * @param channel       nio socket communication channel
     * @param bytes         minimum number of bytes to read from channel
     * @param timeout       time to wait (milliseconds) for read to complete before throwing exception
     * @param returnOnZero  return immediately if read returns 0 bytes
     *
     * @return number of bytes read
     * @throws TimeoutException if timed out trying to read all the data
     * @throws IOException      if channel is closed or cannot be read from
     */
    private int readSocketBytes(ByteBuffer buffer, SocketChannel channel, int bytes,
                                int timeout, boolean returnOnZero)
            throws TimeoutException, IOException {

        if (bytes <= 0) return 0;

        int n, count = 0;
        long t2, t1 = System.currentTimeMillis();

        buffer.clear();
        buffer.limit(bytes);

        // Keep reading until we have exactly "bytes" number of bytes,
        // or have tried "tries" number of times to read.
        while (count < bytes) {
            if ((n = channel.read(buffer)) < 0) {
                throw new IOException("readSocketBytes: client's socket is dead");
            }

            count += n;
            if (count >= bytes) {
                break;
            }
            else if (count == 0 && returnOnZero) {
                return 0;
            }

            t2 = System.currentTimeMillis();
            if (t2 - t1 >= timeout) {
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("    readSocketBytes: timed out, read " + count +
                            " out of " + bytes + " bytes");
                }
                throw new TimeoutException("readSocketBytes: timed out trying to read " + bytes +
                        " bytes, read only " + count);
            }

            // wait small amount
            try { Thread.sleep(10); }
            catch (InterruptedException e) {}
        }
        return count;
    }


    /**
     * This method reads a nonblocking socket for monitor data from a cMsg client.
     * It reads as much as it can and then discards the data. This is used when
     * monitoring is turned off in the cMsgNameServer.
     *
     * @param cd client data object
     * @throws IOException  if channel is closed or cannot be read from
     */
    private void readAndDumpMonitorInfo(cMsgClientData cd) throws IOException {
        // read as much as possible, since we don't want keepalive signals to pile up
        int n;

        do {
            inBuffer.clear();
            n = cd.keepAliveChannel.read(inBuffer);
            if (n < 1) {
                return;
            }
            else {
                // record when some keepalive info read
                cd.updateTime = System.currentTimeMillis();
            }
        } while (n >= inBuffer.capacity());
    }


    /**
     * This method reads a nonblocking socket for monitor data from a cMsg client.
     *
     * @param cd             client data object
     * @param timeout        time to wait (milliseconds) for read to complete before throwing exception
     * @throws TimeoutException if timeout while trying to read all the data
     * @throws cMsgException    if internal protocol error
     * @throws IOException      if channel is closed or cannot be read from
     */
    private void readMonitorInfo(cMsgClientData cd, int timeout)
            throws TimeoutException, cMsgException, IOException {

        // read as much as possible, since we don't want keepalive signals to pile up
        while (true) {
            // read 1 int of data
            int n = readSocketBytes(inBuffer, cd.keepAliveChannel, 4, timeout, true);
            if (n == 0) {
                return;
            }

            inBuffer.flip();
            // size of buffer data in bytes
            int size = inBuffer.getInt();
            if (size > inBuffer.capacity()) {
                inBuffer = ByteBuffer.allocateDirect(size + 512);
            }
            else if (size < 0) {
                throw new cMsgException("Internal cMsg keepalive protocol error from " + cd.getName());
            }

            if (size > 0) {
                // read size bytes of data
                inBuffer.clear();
                // wait until everything is read
                readSocketBytes(inBuffer, cd.keepAliveChannel, size, timeout, false);
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
            }
        }
    }


    /**
     * Writes this server's monitor data to a single client.
     *
     * @param cd client to write data to
     * @param isServer is this client a cMsg server?
     * @throws IOException if error writing data or channel is closed
     */
    private void writeMonitorInfo(cMsgClientData cd, boolean isServer) throws IOException {

        ByteBuffer outBuf;

        if (isServer) {
            outBuf = outBuffer2;
        }
        else {
            outBuf = outBuffer;
        }

        // get outBuffer ready for writing
        outBuf.rewind();

        // Write monitor info to client.
        // NOTE: If vxworks client, its death will not be detected when
        // writing, so be careful not to get stuck in infinite loop.
        // todo: writing should always work unless socket buffer full -
        // todo: which should not be the case for monitoring communications

        int bytesWritten = 0, totalBytesWritten = 0, tries = 0;

        while (outBuf.hasRemaining()) {
//System.out.println("Write " + outBuf.remaining() + " bytes of KA info to " + cd.getName());
            bytesWritten = cd.keepAliveChannel.write(outBuf);
            totalBytesWritten += bytesWritten;

            if (totalBytesWritten < outBuf.limit()) {
                if (!cd.keepAliveChannel.isOpen()) {
                    throw new IOException("Keepalive channel to client is closed");
                }

                // print warning every 10 second or so
                if  (++tries%1000 == 0) {
                    System.out.println("writeMonitorInfo: monitor data write not finished" );
                }

                try { Thread.sleep(10); }
                catch (InterruptedException e) {}
            }
//System.out.println("cMsgMonitorClient: wrote " + totalBytesWritten + " bytes for " + cd.getName() );
        }
    }


    /**
     * This class is used to write this server's monitoring data to all clients.
     */
    private class WriteMonitorData extends Thread {

        public void run() {

            // tell startServer that this thread has started
            server.preConnectionThreadsStartedSignal.countDown();

            // There is a problem with vxWorks clients in that its sockets are
            // global and do not close when the client disappears. In fact, the
            // board can reboot and the other end of the socket will know nothing
            // about this since apparently no "reset" is sent on that socket.
            // Since sockets have no timeout on Solaris, the only solution is to
            // store times between receiving monitor data on a socket, and dealing
            // with those that take too long or timeout.

            emptySendBuffers();

            Object dssObject;
            cMsgDomainServer ds;
            cMsgDomainServerSelect dss;

            while (true) {

                // for each go-round, send updated monitoring info
                if (!monitoringOff) {
//System.out.println("Filling the send buffers");
                    fillSendBuffers();
                }

                // look at each client ...
                cMsgClientData cd;
                for (Iterator it = clients.keySet().iterator(); it.hasNext();) {

                    cd = (cMsgClientData)it.next();

                    try {
                        // send monitor info to client
//System.out.println("Write to " + cd.getName());
                        writeMonitorInfo(cd, cd.isServer());

                        // check for non-responding client
                        if (clientDead(cd)) {
                            // do something
                            if (debug >= cMsgConstants.debugError) {
                                System.out.println("cMsgMonitorClient: client " + cd.getName() +
                                        " has not responded for " + (deadTime/1000) + " seconds at " + (new Date()) +
                                        ", consider it dead");
                            }

                            dssObject = clients.get(cd);
                            // Reading thread may delete this client first, so check for null.
                            if (dssObject != null) {
                                if (dssObject instanceof cMsgDomainServerSelect) {
                                    dss = (cMsgDomainServerSelect) dssObject;
                                    dss.deleteClient(cd);
                                }
                                else {
                                    ds = (cMsgDomainServer) dssObject;
                                    ds.shutdown();
                                }

                                it.remove();
                            }
                        }
                    }
                    catch (IOException e) {
                        // socket & therefore client dead
                        if (debug >= cMsgConstants.debugError) {
                            System.out.println("cMsgMonitorClient: IO error, client " + cd.getName() + " is dead at " +
                                    (new Date()));
                        }

                        dssObject = clients.get(cd);
                        // Reading thread may delete this client first, so check for null.
                        if (dssObject != null) {
                            if (dssObject instanceof cMsgDomainServerSelect) {
                                dss = (cMsgDomainServerSelect) dssObject;
                                dss.deleteClient(cd);
                            }
                            else {
                                ds = (cMsgDomainServer) dssObject;
                                ds.shutdown();
                            }

                            it.remove();
                        }
                    }
                }

                // wait between rounds
                try { Thread.sleep(updatePeriod); }
                catch (InterruptedException e) {}

                if (killThreads) {
//System.out.println("    cMsgMonitorClient(write): ending thread");
                    return;
                }
            }
        }
    }


    /**
     * Fill the output buffers with monitoring data to send clients, one for server clients
     * and the other for regular clients.
     */
    private void emptySendBuffers() {
        outBuffer.clear();
        outBuffer.putInt(0); // zero length xml to follow
        outBuffer.putInt(0); // no other types of items to follow

        outBuffer2.clear();
        outBuffer2.putInt(0);
        outBuffer2.putInt(0);

        outBuffer.flip();
        outBuffer2.flip();
    }


    /**
     * Fill the output buffers with monitoring data to send clients, one for server clients
     * and the other for regular clients.
     */
    private void fillSendBuffers() {
        int count;
        int size1;
        int size2;// update the XML string we're sending
        updateMonitorXML();

        // Prepare 2 buffers. One is for regular clients and the other is
        // for server clients.
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
// changed 3*8 to 3*4, watch for problems 9/28/2010
            int totalLength = dataLength + 3*4; // xml len + 1) + 3) + 4)
            count = 0;
            for (cMsgClientData cd : server.nameServers.values()) {
                passwd = cd.getPassword();
                if (passwd == null) passwd = "";
                // 5) + 6) + 7) + 8) + host len + pswd len
                totalLength += 4*4 + cd.getServerHost().length() + passwd.length();
                count++;
            }
            size1 = count;

            if (totalLength > outBuffer.capacity()) {
                outBuffer = ByteBuffer.allocateDirect(totalLength + 2048);
            }
            outBuffer.clear();

//System.out.println("cMsgMonitorClient: reg client: xml data length = " + dataLength + ", total length = " + totalLength);
            try {
                // put xml size first
                outBuffer.putInt(dataLength);
                // xml string comes next
                outBuffer.put(server.fullMonitorXML.getBytes("US-ASCII"));
                // how many types of info to follow (just one - cloud servers)
                outBuffer.putInt(1);
                // how many servers (cloud hosts)
                size2 = server.nameServers.size();
                outBuffer.putInt(size2);
//System.out.println("cMsgMonitorClient: buffer pos (after len,xml,#servers) = " + outBuffer.position());
                if (size2 > 0) {
                    count=0;
                    for (cMsgClientData cd : server.nameServers.values()) {
                        outBuffer.putInt(cd.getServerPort());
                        outBuffer.putInt(cd.getServerMulticastPort());
                        outBuffer.putInt(cd.getServerHost().length());
                        passwd = cd.getPassword();
                        if (passwd == null) passwd = "";
                        outBuffer.putInt(passwd.length());
                        outBuffer.put(cd.getServerHost().getBytes("US-ASCII"));
                        outBuffer.put(passwd.getBytes("US-ASCII"));
//System.out.println("cMsgMonitorClient: buffer pos = " + outBuffer.position());
                        count++;
                    }

                    if (count != size2 || size1 != size2) {
                        // another thread messed us up, do it again
System.out.println("\n !!! cMsgMonitorClient: inconsistency!!! try again");
                        continue;
                    }
                }
            }
            catch (UnsupportedEncodingException e) {/* never get here */}
            break;
        }

        // create outBuffer with data to be sent to server clients
        dataLength = server.nsMonitorXML.length();
        if (dataLength + 2*4 > outBuffer2.capacity()) {
            // give outBuffer 1k more space than needed
            outBuffer2 = ByteBuffer.allocateDirect(dataLength + 1024);
        }
        outBuffer2.clear();
//System.out.println("cMsgMonitorClient: Buffer2 capacity = " + outBuffer2.capacity());
        // put size first
        outBuffer2.putInt(dataLength);

        // string comes next
        try { outBuffer2.put(server.nsMonitorXML.getBytes("US-ASCII")); }
        catch (UnsupportedEncodingException e) {/* never get here */}

        // how types of info to follow - nothing
        outBuffer2.putInt(0);

//System.out.println("cMsgMonitorClient: server client: xml data length = " + dataLength + ", total length = " + (dataLength + 2*4));
        // get outBuffer ready for writing
        outBuffer.flip();
        outBuffer2.flip();
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
//System.out.println("       >>XML: only my NS xml size = " + xml.length());

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
//System.out.println("       >>XML: fullMonitorXML = \n" + server.fullMonitorXML);

    }


}

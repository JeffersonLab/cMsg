/*----------------------------------------------------------------------------*
 *  Copyright (c) 2006        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 31-Mar-2006, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.RCDomain;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.cMsgDomain.client.cMsgClientListeningThread;

import java.io.*;
import java.util.concurrent.locks.ReentrantReadWriteLock;
import java.util.concurrent.locks.Lock;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.net.*;
import java.nio.channels.ServerSocketChannel;

/**
 * This class implements a cMsg client in the RunControl (or RC) domain.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class RunControl extends cMsgDomainAdapter {
    /** Port number to listen on. */
    int port;

    /** Port number from which to start looking for a suitable listening port. */
    int startingPort;

    /** Server channel (contains socket). */
     ServerSocketChannel serverChannel;

    /** Thread listening for TCP connections and responding to RC domain server commands. */
    cMsgClientListeningThread listeningThread;

    /** Name of local host. */
    String localHost;

    /** Coda experiment id under which this is running. */
    String expid;

    /** Timeout in seconds to wait for server to respond to broadcasts. */
    int broadcastTimeout;

    /**
     * Timeout in seconds to wait for RC server to finish connection
     * once RC broadcast server responds.
     */
    int connectTimeout;

    /** RunControl server's net address obtained from broadcast resonse. */
    volatile InetAddress rcServerAddress;

    /** RunControl server's UDP listening port obtained from broadcast resonse. */
    volatile int rcServerPort;

    /** RunControl server's net address obtained from UDL. */
    InetAddress rcServerBroadcastAddress;

    /** RunControl server's broadcast listening port obtained from UDL. */
    int rcServerBroadcastPort;

    /** Socket over which to UDP broadcast and receive UDP packets. */
    DatagramSocket udpSocket;

    /**
     * This lock is for controlling access to the methods of this class.
     * It is inherently more flexible than synchronizing code. The {@link #connect}
     * and {@link #disconnect} methods of this object cannot be called simultaneously
     * with each other or any other method. However, the {@link #send} method is
     * thread-safe and may be called simultaneously from multiple threads.
     */
    private final ReentrantReadWriteLock methodLock = new ReentrantReadWriteLock();

    /** Lock for calling {@link #connect} or {@link #disconnect}. */
    Lock connectLock = methodLock.writeLock();

    /** Lock for calling methods other than {@link #connect} or {@link #disconnect}. */
    Lock notConnectLock = methodLock.readLock();

    /** Boolean telling if there has been a response to the initial UDP broadcast. */
    volatile boolean noResponse = true;

    /** Level of debug output for this class. */
    int debug = cMsgConstants.debugInfo;


    /**
     * Converts 4 bytes of a byte array into an integer.
     *
     * @param b byte array
     * @param off offset into the byte array (0 = start at first element)
     * @return integer value
     */
    private static final int bytesToInt(byte[] b, int off) {
      int result = ((b[off]  &0xff) << 24) |
                   ((b[off+1]&0xff) << 16) |
                   ((b[off+2]&0xff) <<  8) |
                    (b[off+3]&0xff);
      return result;
    }



    /** Constructor. */
    public RunControl() throws cMsgException {
        domain = "RC";

        try {
            localHost = InetAddress.getLocalHost().getCanonicalHostName();
        }
        catch (UnknownHostException e) {
            throw new cMsgException(e.getMessage());
        }
    }


    /**
     * Method to connect to the codaComponent server from this client.
     *
     * @throws org.jlab.coda.cMsg.cMsgException if there are problems parsing the UDL or
     *                       communication problems with the server(s)
     */
    public void connect() throws cMsgException {
        parseUDL(UDLremainder);

        // cannot run this simultaneously with any other public method
        connectLock.lock();

        try {
            if (connected) return;

            // read env variable for starting port number
            try {
                String env = System.getenv("CMSG_CLIENT_PORT");
                if (env != null) {
                    startingPort = Integer.parseInt(env);
                }
            }
            catch (NumberFormatException ex) {
            }

            // port #'s < 1024 are reserved
            if (startingPort < 1024) {
                startingPort = cMsgNetworkConstants.clientServerStartingPort;
            }

            // At this point, find a port to bind to. If that isn't possible, throw
            // an exception.
            try {
                serverChannel = ServerSocketChannel.open();
            }
            catch (IOException ex) {
                ex.printStackTrace();
                throw new cMsgException("connect: cannot open a listening socket");
            }

            port = startingPort;
            ServerSocket listeningSocket = serverChannel.socket();

            while (true) {
                try {
                    listeningSocket.bind(new InetSocketAddress(port));
                    break;
                }
                catch (IOException ex) {
                    // try another port by adding one
                    if (port < 65535) {
                        port++;
                        try { Thread.sleep(100);  }
                        catch (InterruptedException e) {}
                    }
                    else {
                        // close channel
                        try { serverChannel.close(); }
                        catch (IOException e) { }

                        ex.printStackTrace();
                        throw new cMsgException("connect: cannot find port to listen on");
                    }
                }
            }

            // launch thread and start listening on receive socket
 //           listeningThread = new cMsgClientListeningThread(this, serverChannel);
 //           listeningThread.start();

            // Wait for indication thread is actually running before
            // continuing on. This thread must be running before we talk to
            // the name server since the server tries to communicate with
            // the listening thread.
            /*
            synchronized (listeningThread) {
                if (!listeningThread.isAlive()) {
                    try {
                        listeningThread.wait();
                    }
                    catch (InterruptedException e) {
                        e.printStackTrace();
                    }
                }
            }
            */


            //-------------------------------------------------------
            // broadcast on local subnet to find RunControl server
            //-------------------------------------------------------
            DatagramPacket udpPacket = null;

            // create byte array for broadcast
            ByteArrayOutputStream baos = new ByteArrayOutputStream(1024);
            DataOutputStream out = new DataOutputStream(baos);

            try {
                // Put EXPID (experiment id string) into byte array.
                // The host and port are automatically sent by UDP
                // to the recipient.
                out.writeInt(6666); // port
                out.writeInt(name.length());
                out.writeInt(expid.length());
                try {
                    out.write(name.getBytes("US-ASCII"));
                    out.write(expid.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {
                }
                out.flush();
                out.close();

                // create socket to receive at anonymous port & all interfaces
                udpSocket = new DatagramSocket();
                udpSocket.setReceiveBufferSize(2048);

                // create packet to broadcast from the byte array
                byte[] buf = baos.toByteArray();
                udpPacket = new DatagramPacket(buf, buf.length,
                                               rcServerBroadcastAddress,
                                               rcServerBroadcastPort);
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("I/O Error: " + e);
                }
                throw new cMsgException(e.getMessage());
            }

            // create a thread which will receive any responses to our broadcast
            BroadcastReceiver receiver = new BroadcastReceiver();
            receiver.start();

            int numLoops = 5;
            try {
                while (noResponse && numLoops > 0) {
                    // broadcast
                    udpSocket.send(udpPacket);
System.out.println("Sent out broadcast");

                    // wait with 2 sec timeout
                    synchronized (udpSocket) {
                        try {
System.out.println("Will wait 2 sec for response");
                            udpSocket.wait(2000);
System.out.println("wait for response timed out");
                        }
                        catch (InterruptedException e) {}
                    }
                    numLoops--;
                }
            }
            catch (IOException e) {
            }

            if (noResponse) {
System.out.println("Got no response");
                throw new cMsgException("No response to UDP broadcast received");
            }
            else {
System.out.println("Got a response!");
            }

            // Create a UDP "connection". This means security check is done only once
            // and communication with any other host/port is not allowed.
            udpSocket.connect(rcServerAddress, rcServerPort);

            // create request sending (to domain) channel (This takes longest so do last)
            connected = true;
        }
        finally {
            connectLock.unlock();
        }

        return;
    }




    /**
     * Method to parse the Universal Domain Locator (UDL) into its various components.
     *
     * @param udlRemainder partial UDL to parse
     * @throws cMsgException if udlRemainder is null
     */
     void parseUDL(String udlRemainder) throws cMsgException {

        if (udlRemainder == null) {
            throw new cMsgException("invalid UDL");
        }

        /* Runcontrol domain UDL is of the form:
         *        cMsg:rc://<host>:<port>/?expid=<expid>&broadcastTO=<timeout>&connectTO=<timeout>
         *
         * Remember that for this domain:
         * 1) port is optional with a default of 6543 (cMsgNetworkConstants.rcBroadcastPort)
         * 2) host is optional with a default of 255.255.255.255 (broadcast)
         *    and may be "localhost" or in dotted decimal form
         * 3) the experiment id or expid is optional, it is taken from the
         *    environmental variable EXPID
         * 4) broadcastTO is the time to wait in seconds before connect returns a
         *    timeout when a rc broadcast server does not answer
         * 5) connectTO is the time to wait in seconds before connect returns a
         *    timeout while waiting for the rc server to send a special (tcp)
         *    concluding connect message
         */

        Pattern pattern = Pattern.compile("((?:[a-zA-Z]+[\\w\\.\\-]*)|(?:[\\d]+\\.[\\d\\.]+))?:?(\\d+)?/?(.*)");
        Matcher matcher = pattern.matcher(udlRemainder);

        String udlHost=null, udlPort=null, remainder =null;

        if (matcher.find()) {
            // host
            udlHost = matcher.group(1);
            // port
            udlPort = matcher.group(2);
            // remainder
            remainder = matcher.group(3);

           // if (debug >= cMsgConstants.debugInfo) {
                System.out.println("\nparseUDL: " +
                                   "\n  host      = " + udlHost +
                                   "\n  port      = " + udlPort +
                                   "\n  remainder = " + remainder);
           // }
        }
        else {
            throw new cMsgException("invalid UDL");
        }

        // if host given ...
        if (udlHost != null) {
            // if the host is "localhost", find the actual, fully qualified  host name
            if (udlHost.equalsIgnoreCase("localhost")) {
                try {
                    udlHost = InetAddress.getLocalHost().getCanonicalHostName();
                }
                catch (UnknownHostException e) {
                    udlHost = null;
                }

                if (debug >= cMsgConstants.debugWarn) {
                    System.out.println("parseUDL: codaComponent host given as \"localhost\", substituting " +
                                       udlHost);
                }
            }
            else {
                try {
                    udlHost = InetAddress.getByName(udlHost).getCanonicalHostName();
                }
                catch (UnknownHostException e) {
                    udlHost = null;
                }
            }

            // If the host is NOT given we broadcast on local subnet.
            // If the host is     given we unicast to this particular host.
            if (udlHost != null) {
                // Note that a null arg to getByName gives the loopback address
                // so we need to rule that out.
                try { rcServerBroadcastAddress = InetAddress.getByName(udlHost); }
                catch (UnknownHostException e) {}
            }
            System.out.println("WIll unicast");
        }
        else {
            System.out.println("WIll broadcast");
            try {rcServerBroadcastAddress = InetAddress.getByName("255.255.255.255"); }
            catch (UnknownHostException e) {}
        }

        // get codaComponent port or guess if it's not given
        if (udlPort != null && udlPort.length() > 0) {
            try { rcServerBroadcastPort = Integer.parseInt(udlPort); }
            catch (NumberFormatException e) {
                rcServerBroadcastPort = cMsgNetworkConstants.rcBroadcastPort;
                if (debug >= cMsgConstants.debugWarn) {
                    System.out.println("parseUDL: non-integer port, guessing codaComponent port is " + rcServerBroadcastPort);
                }
            }
        }
        else {
            rcServerBroadcastPort = cMsgNetworkConstants.rcBroadcastPort;
            if (debug >= cMsgConstants.debugWarn) {
                System.out.println("parseUDL: guessing codaComponent port is " + rcServerBroadcastPort);
            }
        }

        if (rcServerBroadcastPort < 1024 || rcServerBroadcastPort > 65535) {
            throw new cMsgException("parseUDL: illegal port number");
        }

        // if no remaining UDL to parse, return
        if (remainder == null) {
            return;
        }

        // look for ?expid=value& or &expid=value&
        pattern = Pattern.compile("[\\?&]expid=([\\w\\-]+)");
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            expid = matcher.group(1);
System.out.println("parsed expid = " + expid);
        }
        else {
            expid = System.getenv("EXPID");
            if (expid == null) {
             throw new cMsgException("Experiment ID is unknown");
            }
System.out.println("env expid = " + expid);
        }

        // now look for ?broadcastTO=value& or &broadcastTO=value&
        pattern = Pattern.compile("[\\?&]broadcastTO=([0-9]+)");
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                broadcastTimeout = 1000 * Integer.parseInt(matcher.group(1));
System.out.println("broadcast TO = " + broadcastTimeout);
            }
            catch (NumberFormatException e) {
                // ignore error and keep value of 0
            }
        }

        // now look for ?connectTO=value& or &connectTO=value&
        pattern = Pattern.compile("[\\?&]connectTO=([0-9]+)");
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            try {
                connectTimeout = 1000 * Integer.parseInt(matcher.group(1));
System.out.println("broadcast TO = " + connectTimeout);
            }
            catch (NumberFormatException e) {
                // ignore error and keep value of 0
            }
        }

    }


    /**
     * Method to close the connection to the codaComponent. This method results in this object
     * becoming functionally useless.
     */
    public void disconnect() {
        // cannot run this simultaneously with connect or send
        connectLock.lock();
        connected = false;
        udpSocket.close();
        connectLock.unlock();
    }


    /**
     * Method to send a message/command to the codaComponent. The command is sent as a
     * string in the message's text field.
     *
     * @param message message to send
     * @throws cMsgException if there are communication problems with the server;
     *                       text is null or blank
     */
    public void send(cMsgMessage message) throws cMsgException {

        String text = message.getText();
        int textLen = text.length();

        // check message fields first
        if (text == null || text.length() < 1) {
            throw new cMsgException("send: need to send a command in text field of message");
        }

        // cannot run this simultaneously with connect or disconnect
        notConnectLock.lock();

        try {
            if (!connected) {
                throw new cMsgException("not connected to server");
            }

            // create byte array for sending message
            ByteArrayOutputStream baos = new ByteArrayOutputStream(textLen + 100);
            DataOutputStream out = new DataOutputStream(baos);

            try {
                // put flag and message text into byte array
                out.writeInt(1);    // some flag
                out.writeInt(textLen);
                try {
                    out.write(text.getBytes("US-ASCII"));
                    out.writeChar(0);  // ending null
                }
                catch (UnsupportedEncodingException e) {
                }
                out.flush();
                out.close();

                // create packet to send from the byte array
                byte[] buf = baos.toByteArray();
                udpSocket.send(new DatagramPacket(buf, buf.length, rcServerAddress, rcServerPort));
            }
            catch (IOException e) {
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("send: " + e);
                }
                throw new cMsgException(e.getMessage());
            }
        }
        finally {
            notConnectLock.unlock();
        }

    }



    /**
     * This class gets any response to our UDP broadcast. A response will
     * stop the broadcast and give some host & port of where to direct
     * future UDP packets.
     */
    class BroadcastReceiver extends Thread {

        public void run() {

            byte[] buf = new byte[1024];
            DatagramPacket packet = new DatagramPacket(buf, 1024);

            try {
                udpSocket.receive(packet);
            }
            catch (IOException e) {
            }

            // pick apart byte array received
            rcServerPort = packet.getPort();  // port to send future udp packets to
System.out.println("Got rc broadcast server port = " + rcServerPort);

            // host to send future udp packets to
            rcServerAddress = packet.getAddress();
System.out.println("Got rc broadcast server host = " + rcServerAddress.getCanonicalHostName());

            noResponse = false;

            // notify waiter that we have a response
            synchronized (udpSocket) {
                udpSocket.notify();
            }
        }
    }


}


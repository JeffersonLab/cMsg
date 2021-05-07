/*----------------------------------------------------------------------------*
 *  Copyright (c) 2008        Southeastern Universities Research Association, *
 *                            Jefferson Science Associates                    *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C.Timmer, 10-Sep-2008, Jefferson Lab                                    *
 *                                                                            *
 *    Authors: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.common;

import org.jlab.coda.cMsg.*;

import java.net.*;
import java.io.*;
import java.nio.ByteBuffer;
import java.nio.IntBuffer;
import java.util.*;

/**
 * This class finds cMsg domain name servers and rc domain multicast servers
 * which are listening on UDP sockets.
 * By convention, all cMsg name servers should have a multicast port starting at
 * 45000 and not exceeding 45099. All these ports will be probed for servers.
 * By convention, all rc multicast servers should have a multicast port starting at
 * 45200 and not exceeding 45299. Twenty of these ports will be probed for servers.
 * Additional ports may be specified.
 */
public class cMsgServerFinder {

    /** Port numbers provided by caller to probe in cmsg domain. */
    private HashSet<Integer> cmsgPorts;

    /** Port numbers provided by caller to probe in rc domain. */
    private HashSet<Integer> rcPorts;

    /** Default list of port numbers to probe in cmsg domain. */
    private final int[] defaultCmsgPorts;

    /** Default list of port numbers to probe in rc domain. */
    private final int[] defaultRcPorts;

    /** Optional password included in UDL for connection to server requiring one. */
    private String password = "";

    /** Set of all cMsg domain responders' hosts and ports in a "host:tcpPort:udpPort" string format. */
    private HashSet<ResponderInfo> cMsgResponders;

    /** Set of all rc domain responders' hosts, ports, and expids. */
    private HashSet<ResponderInfo> rcResponders;

    /** Time in milliseconds waiting for a response to the multicasts. */
    private int sleepTime = 3000;

    /** Do changes to the expid or added ports necessitate finding rc multicast servers again? */
    private volatile boolean needToUpdateRc = true;

    /** Do added ports necessitate finding cmsg name servers again? */
    private volatile boolean needToUpdateCmsg = true;

    /** Level of debug output for this class. */
    private int debug;


    /** Class to store info from a multicast responder. */
    class ResponderInfo {
        int version;
        int tcpPort;
        int udpPort;
        String expid;
        String host;
        String[] ipAddrs;
        String[] broadcastAddrs;
    }




    /** Constructor. */
    public cMsgServerFinder() {
        this(cMsgConstants.debugNone);
    }

    /**
     * Constructor.
     * @param debug level of debug output
     */
    public cMsgServerFinder(int debug) {
        this.debug = debug;

        rcResponders   = new HashSet<ResponderInfo>();
        cMsgResponders = new HashSet<ResponderInfo>();

        rcPorts   = new HashSet<Integer>();
        cmsgPorts = new HashSet<Integer>();

        defaultRcPorts   = new int[20];
        defaultCmsgPorts = new int[20];

        // set default ports to scan
        for (int i=0; i<20; i++) {
            defaultRcPorts[i]   = cMsgNetworkConstants.rcMulticastPort   + i;
            defaultCmsgPorts[i] = cMsgNetworkConstants.nameServerUdpPort + i;
        }
    }                                                                           


    /**
     * Get the time to wait for server responses in milliseconds.
     * Defaults to 3 seconds.
     * @return time to wait for server responses in milliseconds.
     */
    public int getSleepTime() {
        return sleepTime;
    }


    /**
     * Set the time to wait for server responses in milliseconds.
     * @param sleepTime time to wait for server responses in milliseconds.
     *                  Negative value resets to default (3 sec).
     */
    public void setSleepTime(int sleepTime) {
        if (sleepTime < 0) {
            this.sleepTime = 3000;
        }
        else {
            this.sleepTime = sleepTime;
        }
    }


    /**
     * Set level of debug output.
     * Argument may be one of:
     * <ul>
     * <li><p>{@link cMsgConstants#debugNone} for no outuput</p>
     * <li><p>{@link cMsgConstants#debugSevere} for severe error output</p>
     * <li><p>{@link cMsgConstants#debugError} for all error output</p>
     * <li><p>{@link cMsgConstants#debugWarn} for warning and error output</p>
     * <li><p>{@link cMsgConstants#debugInfo} for information, warning, and error output</p>
     * </ul>
     *
     * @param debug level of debug output
     */
    void setDebug(int debug) {
        if (debug != cMsgConstants.debugError &&
            debug != cMsgConstants.debugInfo &&
            debug != cMsgConstants.debugNone &&
            debug != cMsgConstants.debugSevere &&
            debug != cMsgConstants.debugWarn) {
            return;
        }
        this.debug = debug;
    }

    /**
     * Get the password for connecting to a cmsg name server.
     * This is necessary only for a server that requires a password.
     * @return value for password
     */
    public String getPassword() {
        return password;
    }


    /**
     * Set the password for connecting to a cmsg name server.
     * Use a null string as the arg for servers requiring no password.
     *
     * @param password value to set password with
     */
    synchronized public void setPassword(String password) {
        if (password == null) {
            if (this.password == null) return;
        }
        else if (this.password == null) {
        }
        else if (this.password.equals(password)) {
            return;
        }
        this.password = password;
        needToUpdateCmsg = true;
    }


    /**
     * Add a UDP port to the list of ports to be probed for rc multicast servers.
     * @param port UDP port to be probed for rc multicast servers
     */
    synchronized public void addRcPort(int port) {
        if (port < 1024 || port > 65535) {
            return;
        }
        if (rcPorts.contains(port)) {
            return;
        }
        rcPorts.add(port);
        needToUpdateRc = true;
    }


    /**
     * Add a collection of UDP ports to the list of ports to be probed for rc multicast servers.
     * @param col collection of UDP ports to be probed for rc multicast servers
     */
    synchronized public void addRcPorts(Collection<Integer> col) {
        for (Integer port : col) {
            addRcPort(port);
        }
    }


    /**
     * Remove a UDP port from the list of user-specified ports to be probed
     * for rc multicast servers. The list of 20 default ports will still be
     * probed regardless.
     * @param port UDP port to be removed from probing for rc multicast servers
     */
    synchronized public void removeRcPort(int port) {rcPorts.remove(port);}


    /** Remove all user-specified UDP ports from list of ports to be
     *  probed for rc multicast servers. The list of 20 default ports will
     *  still be probed regardless. */
    synchronized public void removeRcPorts() {rcPorts.clear();}


    /**
     * Add a UDP port to the user-specified list of ports to be probed
     * for cmsg name servers.
     * @param port UDP port to be probed for cmsg name servers
     */
    synchronized public void addCmsgPort(int port) {
        if (port < 1024 || port > 65535) {
            return;
        }
        if (cmsgPorts.contains(port)) {
            return;
        }
        cmsgPorts.add(port);
        needToUpdateCmsg = true;
    }


    /**
     * Add a collection of UDP ports to the list of user-specified ports to be
     * probed for cmsg name servers.
     * @param col collection of UDP ports to be probed for cmsg name servers
     */
    synchronized public void addCmsgPorts(Collection<Integer> col) {
        for (Integer port : col) {
            addCmsgPort(port);
        }
    }


    /**
     * Remove a user-specified UDP port from the list of ports to be probed
     * for cmsg name servers. The list of 20 default ports will still be
     * probed regardless.
     * @param port UDP port to be removed from probing for cmsg name servers
     */
    synchronized public void removeCmsgPort(int port) {
        cmsgPorts.remove(port);
    }


    /** Remove all user-specified UDP ports from list of ports to be probed
     * for cmsg name servers. The list of 20 default ports will
     *  still be probed regardless. */
    synchronized public void removeCmsgPorts() { cmsgPorts.clear(); }


    /**
     * Tells caller if {@link #find} needs to be called since the
     * server search parameters (eg. expid, ports) have been changed or added to.
     * @return true if "find" needs to be called again, else false
     */
    synchronized public boolean needsUpdate() {
        return (needToUpdateCmsg || needToUpdateRc);
    }


    /**
     * Find all reachable cmsg name servers by multicasting.
     * Follow the calling of this method with {@link #toString},
     * {@link #getCmsgServers}, or {@link #getCmsgServersXML}
     * in order to see the results of the search.
     */
    synchronized public void findCmsgServers() {
        cMsgResponders.clear();

        // start thread to find cMsg name servers
        cMsgFinder cFinder = new cMsgFinder();
        cFinder.start();

        // give receiving threads some time to get responses
        try { Thread.sleep(sleepTime + 200); }
        catch (InterruptedException e) { }

        needToUpdateCmsg = false;
    }


    /**
     * Find all reachable rc multicast servers by multicasting.
     * Follow the calling of this method with {@link #toString} or
     * {@link #getRcServers}, or {@link #getRcServersXML}
     * in order to see the results of the search.
     */
    synchronized public void findRcServers() {
        rcResponders.clear();

        // start thread to find rc multicast servers
        rcFinder rFinder = new rcFinder();
        rFinder.start();

        // give receiving threads some time to get responses
        try { Thread.sleep(sleepTime + 200); }
        catch (InterruptedException e) { }

        needToUpdateRc = false;
    }


    /**
     * Find all reachable cmsg name servers and rc multicast servers by multicasting.
     * Follow the calling of this method with {@link #toString} in order to see the
     * results of the search.
     */
    synchronized public void find() {
         // start thread to find cMsg name servers
        cMsgResponders.clear();
        cMsgFinder cFinder = new cMsgFinder();
        cFinder.start();

        // start thread to find rc multicast servers
        rcResponders.clear();
        rcFinder rFinder = new rcFinder();
        rFinder.start();

        // give receiving threads some time to get responses
        try { Thread.sleep(sleepTime + 200); }
        catch (InterruptedException e) { }

        needToUpdateCmsg = needToUpdateRc = false;
    }


    /**
     * Print out the cmsg name servers and rc multicast servers found in the last search.
     */
    synchronized public void print() {

        String[] parts;

        if (cMsgResponders.size() > 0) {
            System.out.println("\ncMsg name servers:");
        }

        for (ResponderInfo info : cMsgResponders) {
            if (info.host == null) info.host = "<unknown>";
            System.out.println("host = " + info.host +
                               ",  UDP port = " + info.udpPort +
                               ",  TCP port = " + info.tcpPort +
                               "\n  IP addresses:");
            for (String s : info.ipAddrs) {
                System.out.println("    " + s);
            }
        }

        if (rcResponders.size() > 0) {
            System.out.println("\nrc multicast servers:");
        }

        for (ResponderInfo info : rcResponders) {
            if (info.host == null) info.host = "<unknown>";
            System.out.println("host = " + info.host +
                               ",  UDP port = " + info.udpPort +
                               ",  expid = " + info.expid +
                               "\n  IP addresses:");
            for (String s : info.ipAddrs) {
                System.out.println("    " + s);
            }
        }

        System.out.println();
    }


    /**
     * Return a string in XML format of the cmsg name servers and rc multicast servers
     * found in the last search.
     *
     * @return XML format string listing all servers found
     */
    public String toString() {
        return toXML(true,true);
    }


    /**
     * Return a string in XML format of the cmsg name servers
     * found in the last search.
     *
     * @return XML format string listing all cmsg name servers found
     */
    public String getCmsgServersXML() {
        return toXML(false,true);
    }


    /**
     * Return a string in XML format of the rc multicast servers
     * found in the last search.
     *
     * @return XML format string listing all rc multicast servers found
     */
    public String getRcServersXML() {
        return toXML(true,false);
    }


    /**
     * Return a string in XML format of the cmsg name servers and rc multicast servers
     * found in the last search.
     *
     * @param rc if true, look for rc multicast servers
     * @param cmsg rc if true, look for cmsg name servers
     * @return XML format string listing all servers found
     */
    synchronized private String toXML(boolean rc, boolean cmsg) {

        String[] parts;
        StringBuilder buffer = new StringBuilder(1024);

        if (cmsg) {
            for (ResponderInfo info : cMsgResponders) {
                if (info.host == null) info.host = "unknown";
                buffer.append("<cMsgNameServer");
                buffer.append("  host=\"");    buffer.append(info.host);
                buffer.append("\"  udpPort=\""); buffer.append(info.udpPort);
                buffer.append("\"  tcpPort=\""); buffer.append(info.tcpPort);
                buffer.append("\" >\n");

                String[] ips    = info.ipAddrs;
                String[] bcasts = info.broadcastAddrs;

                for (int i=0; i < ips.length; i++) {
                    buffer.append("  <interface ip=\""); buffer.append(ips[i]);
                    buffer.append("\" bcast=\""); buffer.append(bcasts[i]);
                    buffer.append("\" />\n");
                }

                buffer.append("</cMsgNameServer>\n");
            }
        }

        if (rc) {
            for (ResponderInfo info : rcResponders) {
                if (info.host == null) info.host = "unknown";
                buffer.append("<rcMulticastServer");
                buffer.append("  host=\"");    buffer.append(info.host);
                buffer.append("\"  udpPort=\""); buffer.append(info.udpPort);
                buffer.append("\"  expid=\""); buffer.append(info.expid);
                buffer.append("\" >\n");

                String[] ips    = info.ipAddrs;
                String[] bcasts = info.broadcastAddrs;

                for (int i=0; i < ips.length; i++) {
                    buffer.append("  <interface ip=\""); buffer.append(ips[i]);
                    buffer.append("\" bcast=\""); buffer.append(bcasts[i]);
                    buffer.append("\" />\n");
                }

                buffer.append("</rcMulticastServer>\n");
            }
        }

        return buffer.toString();
    }

    /**
     * Return an array of cMsg messages contains payload items of the cmsg name servers
     * information found in the last search. One message for each server.
     *
     * @return array of cMsg messages contains payload items of the cmsg name servers
     *         information, one message for each server
     */
    synchronized public cMsgMessage[] getCmsgServers() {

        if (cMsgResponders.size() < 1) return null;

        int i=0;
        String[] parts;
        cMsgMessage[] msgs = new cMsgMessage[cMsgResponders.size()];

        for (ResponderInfo info : cMsgResponders) {

            if (info.host == null) info.host = "unknown";
            cMsgMessage msg = new cMsgMessage();

            try {
                cMsgPayloadItem item1 = new cMsgPayloadItem("host", info.host);
                cMsgPayloadItem item2 = new cMsgPayloadItem("udpPort", info.udpPort);
                cMsgPayloadItem item3 = new cMsgPayloadItem("tcpPort", info.tcpPort);
                cMsgPayloadItem item4 = new cMsgPayloadItem("addresses", info.ipAddrs);
                cMsgPayloadItem item5 = new cMsgPayloadItem("bcastAddresses", info.broadcastAddrs);

                msg.addPayloadItem(item1);
                msg.addPayloadItem(item2);
                msg.addPayloadItem(item3);
                msg.addPayloadItem(item4);
                msg.addPayloadItem(item5);
            }
            catch (cMsgException e) { /* never happen */ }

            msgs[i++] = msg;
        }

        return msgs;
    }


    /**
     * Return an array of cMsg messages contains payload items of the rc multicast servers
     * information found in the last search. One message for each server.
     *
     * @return array of cMsg messages contains payload items of the rc multicast servers
     *         information, one message for each server
     */
    synchronized public cMsgMessage[] getRcServers() {

        if (rcResponders.size() < 1) return null;

        int i=0;
        String[] parts;
        cMsgMessage[] msgs = new cMsgMessage[rcResponders.size()];

        for (ResponderInfo info : rcResponders) {

            if (info.host == null) info.host = "unknown";
            cMsgMessage msg = new cMsgMessage();

            try {
                cMsgPayloadItem item1 = new cMsgPayloadItem("host", info.host);
                cMsgPayloadItem item2 = new cMsgPayloadItem("udpPort", info.udpPort);
                cMsgPayloadItem item3 = new cMsgPayloadItem("expid", info.expid);
                cMsgPayloadItem item4 = new cMsgPayloadItem("addresses", info.ipAddrs);
                cMsgPayloadItem item5 = new cMsgPayloadItem("bcastAddresses", info.broadcastAddrs);

                msg.addPayloadItem(item1);
                msg.addPayloadItem(item2);
                msg.addPayloadItem(item3);
                msg.addPayloadItem(item4);
                msg.addPayloadItem(item5);
            }
            catch (cMsgException e) { /* never happen */ }

            msgs[i++] = msg;
        }

        return msgs;
    }



    /**
     * Class to find cmsg name servers.
     */
    class cMsgFinder extends Thread {

        public void run() {

            //-------------------------------------------------------
            // multicast on local subnet to find cMsg server
            //-------------------------------------------------------
            byte[] buffer;
            DatagramSocket socket = null;

            // create byte array for multicast
            ByteArrayOutputStream baos = new ByteArrayOutputStream(1024);
            DataOutputStream out = new DataOutputStream(baos);

            try {
                // send our magic ints
                out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
                out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
                out.writeInt(cMsgNetworkConstants.magicNumbers[2]);
                out.writeInt(cMsgConstants.version);
                // int describing our message type: multicast is from cMsg domain client
                out.writeInt(cMsgNetworkConstants.cMsgDomainMulticast);
                out.writeInt(password.length());
                try {out.write(password.getBytes("US-ASCII"));}
                catch (UnsupportedEncodingException e) { }
                out.flush();
                out.close();

                // create socket to receive at anonymous port & all interfaces
                socket = new DatagramSocket();

                // Avoid local port for socket to which others may be multicasting to
                int tries = 20;
                while (socket.getLocalPort() > cMsgNetworkConstants.UdpClientPortMin &&
                       socket.getLocalPort() < cMsgNetworkConstants.UdpClientPortMax) {
                    socket = new DatagramSocket();
                    if (--tries < 0) break;
                }

                socket.setReceiveBufferSize(1024);
                socket.setSoTimeout(sleepTime);

                // create multicast packet from the byte array
                buffer = baos.toByteArray();
                baos.close();
            }
            catch (IOException e) {
                try { out.close();} catch (IOException e1) {}
                try {baos.close();} catch (IOException e1) {}
                if (socket != null) socket.close();
                System.out.println("Cannot create cmsg multicast packet");
                return;
            }

            // create a thread which will receive any responses to our multicast
            cMsgMulticastReceiver receiver = new cMsgMulticastReceiver(socket);
            receiver.start();

            // give receiver time to get started before we starting sending out packets
            try { Thread.sleep(200); }
            catch (InterruptedException e) { }

            // create a thread which will send our multicast
            cMsgMulticaster sender = new cMsgMulticaster(buffer, socket);
            sender.start();

            // wait for responses
            try { Thread.sleep(sleepTime); }
            catch (InterruptedException e) { }

            receiver.interrupt();
            socket.close();
            return;
        }
    }


    /**
     * This class gets any response to our UDP multicast to cmsg name servers.
     */
    class cMsgMulticastReceiver extends Thread {

        DatagramSocket socket;

        cMsgMulticastReceiver(DatagramSocket socket) {
            this.socket = socket;
        }

        public void run() {
            int nameServerTcpPort, nameServerUdpPort;
            StringBuilder id = new StringBuilder(1024);
            byte[] buf = new byte[1024];
            DatagramPacket packet = new DatagramPacket(buf, 1024);

            nextPacket:
            while (true) {
                try {
                    packet.setLength(1024);
                    socket.receive(packet);

                    // if packet is smaller than 6 ints
                    if (packet.getLength() < 24) {
                        continue;
                    }

//System.out.println("RECEIVED CMSG MULTICAST RESPONSE PACKET");
                    // pick apart byte array received
                    int magicInt1  = cMsgUtilities.bytesToInt(buf, 0); // magic password
                    int magicInt2  = cMsgUtilities.bytesToInt(buf, 4); // magic password
                    int magicInt3  = cMsgUtilities.bytesToInt(buf, 8); // magic password

                    if ( (magicInt1 != cMsgNetworkConstants.magicNumbers[0]) ||
                         (magicInt2 != cMsgNetworkConstants.magicNumbers[1]) ||
                         (magicInt3 != cMsgNetworkConstants.magicNumbers[2]))  {
//System.out.println("  Bad magic numbers for multicast response packet");
                        continue;
                    }

                    // cMsg name server port
                    nameServerTcpPort = cMsgUtilities.bytesToInt(buf, 12); // port to do a direct connection to
                    nameServerUdpPort = cMsgUtilities.bytesToInt(buf, 16); // port to do a direct connection to
                    int addrCount     = cMsgUtilities.bytesToInt(buf, 20); // host to do a direct connection to

                    if ((nameServerTcpPort < 1024 || nameServerTcpPort > 65535) ||
                            (addrCount < 0 || addrCount > 50)) {
//System.out.println("  Wrong TCP port # (" + nameServerTcpPort + ") or address count (" +
//                           addrCount + ") for multicast response packet");
                        continue;
                    }

                    int pos = 24;
                    String ss;
                    int stringLen;
                    ResponderInfo info = new ResponderInfo();
                    info.ipAddrs = new String[addrCount];
                    info.broadcastAddrs = new String[addrCount];

                    for (int i=0; i < addrCount; i++) {
                        try {
                            stringLen = cMsgUtilities.bytesToInt(buf, pos); pos += 4;
//System.out.println("     ip len = " + stringLen);
                            ss = new String(buf, pos, stringLen, "US-ASCII");
//System.out.println("     ip = " + ss);
                            info.ipAddrs[i] = ss;
                            pos += stringLen;

                            // Get a host name to go with the IP addresses
                            if (info.host == null) {
                                try {
                                    info.host = InetAddress.getByName(ss).getCanonicalHostName();
                                }
                                catch (UnknownHostException e) {}
                            }

                            stringLen = cMsgUtilities.bytesToInt(buf, pos); pos += 4;
//System.out.println("     broad len = " + stringLen);
                            ss = new String(buf, pos, stringLen, "US-ASCII");
//System.out.println("     broad = " + ss);
                            info.broadcastAddrs[i] = ss;
                            pos += stringLen;
                        }
                        catch (UnsupportedEncodingException e) {/*never happen */}
                    }

                    if (info.host == null) info.host = "unknown";
                    info.udpPort = nameServerUdpPort;
                    info.tcpPort = nameServerTcpPort;
//System.out.println("cmsgMulticastReceiver host = " + info.host + "\n");

                    // Do not add this if it is a duplicate
                    for (ResponderInfo rInfo : cMsgResponders) {
                        if ( (info.host.equals(rInfo.host) && info.udpPort == rInfo.udpPort) ) {
                            continue nextPacket;
                        }
                    }

                    cMsgResponders.add(info);

                }
                catch (InterruptedIOException e) {
//System.out.println("  Interrupted receiving thread so return");
                    return;
                }
                catch (java.net.SocketException e) {
                    // time's up, socket was closed
                    return;
                }
                catch (IOException e) {
//System.out.println("  IO exception in cmsg receiving thread so return\n");
                    e.printStackTrace();
                    return;
                }
                // Possibly get this with unknown packet, so ignore packet
                catch (Exception e) {
                }
            }
        }
    }


    /**
     * This class defines a thread to multicast a single UDP packet to
     * cmsg name servers.
     */
    class cMsgMulticaster extends Thread {

        byte[] buffer;
        DatagramSocket socket;

        cMsgMulticaster(byte[] buffer, DatagramSocket socket) {
            this.socket = socket;
            this.buffer = buffer;
        }

        public void run() {
            DatagramPacket packet;
            InetAddress addr = null;

            try {
                /** Multicast address. */
                addr = InetAddress.getByName(cMsgNetworkConstants.cMsgMulticast);
            }
            catch (UnknownHostException e) {
                e.printStackTrace();
            }

            try {
                for (int port : cmsgPorts) {
//System.out.println("Send multicast packets on port " + port);
                    packet = new DatagramPacket(buffer, buffer.length,
                                                addr, port);
                    socket.send(packet);
                }
                for (int port : defaultCmsgPorts) {
//System.out.println("Send multicast packets on port " + port);
                    packet = new DatagramPacket(buffer, buffer.length,
                                                addr, port);
                    socket.send(packet);
                }
            }
            catch (IOException e) {
                e.printStackTrace();
            }
        }
    }


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


    /**
     * Class to find rc multicast servers.
     */
    class rcFinder extends Thread {

        public void run() {

            //--------------------------------------------------------------
            // multicast on local subnet to find RunControl Multicast server
            //--------------------------------------------------------------
            byte[] buffer;
            String name = "serverFinder";
            String myExpid = "expid";
            MulticastSocket socket = null;

            // create byte array for multicast
            ByteArrayOutputStream baos = new ByteArrayOutputStream(1024);
            DataOutputStream out = new DataOutputStream(baos);

            try {
                // Put our magic #s, TCP listening port, name, and
                // the EXPID (experiment id string) into byte array.
                out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
                out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
                out.writeInt(cMsgNetworkConstants.magicNumbers[2]);
                out.writeInt(cMsgConstants.version);
                out.writeInt(cMsgNetworkConstants.rcDomainMulticastProbe); // multicast is from rc domain prober
                out.writeInt(1);                // port = 1 identifies us as being from cMsgServerFinder
                out.writeInt(0);                // id # not used
                out.writeInt(name.length());    // use any client name just to get a response
                out.writeInt(myExpid.length()); // use any expid name just to get a response
                try {
                    out.write(name.getBytes("US-ASCII"));
                    out.write(myExpid.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {
                }
                out.flush();
                out.close();

                // create socket to receive at anonymous port & all interfaces
                socket = new MulticastSocket();

                // Avoid local port for socket to which others may be multicasting to
                int tries = 20;
                while (socket.getLocalPort() > cMsgNetworkConstants.UdpClientPortMin &&
                       socket.getLocalPort() < cMsgNetworkConstants.UdpClientPortMax) {
                    socket = new MulticastSocket();
                    if (--tries < 0) break;
                 }

                socket.setReceiveBufferSize(1024);
                socket.setSoTimeout(sleepTime);

                // create multicast packet from the byte array
                baos.close();
                buffer = baos.toByteArray();
            }
            catch (IOException e) {
                try { out.close();} catch (IOException e1) {}
                try {baos.close();} catch (IOException e1) {}
                if (socket != null) socket.close();
                System.out.println("Cannot create rc multicast packet");
                return;
            }

            // create a thread which will receive any responses to our multicast
            rcMulticastReceiver receiver = new rcMulticastReceiver(socket);
            receiver.start();

            // create a thread which will send our multicast
            rcMulticaster sender = new rcMulticaster(buffer, socket);
            sender.start();

            // wait up to multicast timeout seconds
            // wait for responses
            try { Thread.sleep(sleepTime); }
            catch (InterruptedException e) { }

            receiver.interrupt();
            socket.close();

            return;
        }
    }


    /**
     * This class gets any response to our UDP multicast to rc multicast servers.
     */
    class rcMulticastReceiver extends Thread {

        DatagramSocket socket;

        rcMulticastReceiver(DatagramSocket socket) {
            this.socket = socket;
        }

        /**
         * This method takes a byte buffer and prints out the desired number of words
         * from the given position.
         *
         * @param buf            buffer to print out
         * @param position       position of data (bytes) in buffer to start printing
         * @param words          number of 32 bit words to print in hex
         * @param label          a label to print as header
         */
        public void printBuffer(ByteBuffer buf, int position, int words, String label) {

            if (buf == null) {
                System.out.println("printBuffer: buf arg is null");
                return;
            }

            int origPos = buf.position();
            buf.position(position);

            if (label != null) System.out.println(label + ":");

            IntBuffer ibuf = buf.asIntBuffer();
            words = words > ibuf.capacity() ? ibuf.capacity() : words;
            for (int i=0; i < words; i++) {
                System.out.println("  Buf(" + i + ") = 0x" + Integer.toHexString(ibuf.get(i)));
            }
            System.out.println();

            buf.position(origPos);
       }


        public void run() {

            int index;
            byte[] buf = new byte[1024];
            DatagramPacket packet = new DatagramPacket(buf, 1024);

            nextPacket:
            while (true) {
                // reset for each round
                packet.setLength(1024);

                try {
                    socket.receive(packet);

//System.out.println("rcMulticastReceiver: RECEIVED RC MULTICAST RESPONSE PACKET");

                    // if we get too small of a packet, reject it
                    if (packet.getLength() < 6*4) {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("rcMulticastReceiver: got packet that's too small");
                        }
                        continue;
                    }
                    int magic1 = cMsgUtilities.bytesToInt(buf, 0);
                    int magic2 = cMsgUtilities.bytesToInt(buf, 4);
                    int magic3 = cMsgUtilities.bytesToInt(buf, 8);
                    if (magic1 != cMsgNetworkConstants.magicNumbers[0] ||
                        magic2 != cMsgNetworkConstants.magicNumbers[1] ||
                        magic3 != cMsgNetworkConstants.magicNumbers[2])  {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("rcMulticastReceiver: got bad magic # response to multicast");
                        }
                        continue;
                    }

                    int version  = cMsgUtilities.bytesToInt(buf, 12);
                    int port     = cMsgUtilities.bytesToInt(buf, 16);
                    int hostLen  = cMsgUtilities.bytesToInt(buf, 20);
                    int expidLen = cMsgUtilities.bytesToInt(buf, 24);
//System.out.println("rcMulticastReceiver: version # = " + version + ", port = " + port +
//", host len = " + hostLen + ", expid len = " + expidLen);

                    if (version != cMsgConstants.version) {
//System.out.println("rcMulticastReceiver: got bad version # = " + version + ", probably from older-cMsg based platforms, ignore");
                        continue;
                    }

                    ResponderInfo info = new ResponderInfo();
                    info.version = version;
                    info.udpPort = port;

                    // get host
                    index = 28;
                    if (hostLen > 0) {
                        // Set host below
                        //info.host = new String(buf, index, hostLen, "US-ASCII");
                        index += hostLen;
                    }

                    // get expid
                    if (expidLen > 0) {
                        info.expid = new String(buf, index, expidLen, "US-ASCII");
                        index += expidLen;
                    }
                    else {
                        info.expid = "expid";
                    }
//System.out.println("rcMulticastReceiver expid = " + info.expid);

                    int addrCount = cMsgUtilities.bytesToInt(buf, index); index += 4;
//System.out.println("rcMulticastReceiver count = " + addrCount);
                    String ss;
                    int stringLen;
                    info.ipAddrs = new String[addrCount];
                    info.broadcastAddrs = new String[addrCount];

                    for (int i=0; i < addrCount; i++) {
                        try {
                            stringLen = cMsgUtilities.bytesToInt(buf, index); index += 4;
//System.out.println("     ip len = " + stringLen + ", index = " + (index - 4));

                            try {
                                ss = new String(buf, index, stringLen, "US-ASCII");
                            }
                            catch (Exception e) {
//System.out.println("  Exception in rc receiving thread, bad packet, this socket is uses udp port = \n" +
//socket.getLocalPort());
//System.out.println("rcMulticastReceiver: version # = " + version + ", port = " + port +
//", host len = " + hostLen + ", expid len = " + expidLen + ", expid = " + info.expid +
//                           ", addrCount = " + addrCount + ", stringLen = " + stringLen);
//                                printBuffer(ByteBuffer.wrap(buf), 0, 20, "Bad packet bytes");
                                return;
                            }

//System.out.println("     ip = " + ss);
                            info.ipAddrs[i] = ss;
                            index += stringLen;

                            // Get a host name to go with the IP addresses
                            if (info.host == null) {
                                try {
                                    info.host = InetAddress.getByName(ss).getCanonicalHostName();
                                }
                                catch (UnknownHostException e) {}
                            }

                            stringLen = cMsgUtilities.bytesToInt(buf, index); index += 4;
//System.out.println("     broad len = " + stringLen + ", index = " + (index - 4));
                            ss = new String(buf, index, stringLen, "US-ASCII");
//System.out.println("     broad = " + ss);
                            info.broadcastAddrs[i] = ss;
                            index += stringLen;
                        }
                        catch (UnsupportedEncodingException e) {/*never happen */}
                    }

                    if (info.host == null) info.host = "unknown";
//System.out.println("rcMulticastReceiver host = " + info.host + "\n");

                    // Do not add this if it is a duplicate
                    for (ResponderInfo rInfo : rcResponders) {
                        if ( (info.host.equals(rInfo.host) && info.udpPort == rInfo.udpPort) ) {
                            continue nextPacket;
                        }
                    }

                    rcResponders.add(info);
//System.out.println("rcMulticastReceiver end packet parsing");
                }
                catch (InterruptedIOException e) {
//System.out.println("  Interrupted rc receiving thread so return");
                    return;
                }
                catch (java.net.SocketException e) {
//System.out.println("  time's up, socket closed\n");
                    return;
                }
                catch (IOException e) {
//System.out.println("  cMsgServerFinder: IO exception in rc receiving thread so return\n");
                    e.printStackTrace();
                    return;
                }
                // Possibly get this with unknown packet, ignore it
                catch (Exception e) {
                }
            }
        }
    }



    /**
     * This class defines a thread to multicast a single UDP packet to
     * rc multicast servers.
     */
    class rcMulticaster extends Thread {

        byte[] buffer;
        MulticastSocket socket;

        rcMulticaster(byte[] buffer, MulticastSocket socket) {
            this.socket = socket;
            this.buffer = buffer;
        }

        public void run() {
            DatagramPacket packet;
            InetAddress addr = null;

            try {
                addr = InetAddress.getByName(cMsgNetworkConstants.rcMulticast);
            }
            catch (UnknownHostException e) { /* never thrown */ }


            try {
                // Send a packet over each network interface.
                Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();

                while (enumer.hasMoreElements()) {
                    NetworkInterface ni = enumer.nextElement();
                    if (ni.isUp()) {
//System.out.println("cMSgServerFinder: sending mcast packet over " + ni.getName());
                        for (int port : rcPorts) {
                            //System.out.println("Send multicast packets on port " + port);
                            packet = new DatagramPacket(buffer, buffer.length,
                                                        addr, port);
                            socket.setNetworkInterface(ni);
                            socket.send(packet);
                        }
                        for (int port : defaultRcPorts) {
                            //System.out.println("Send multicast packets on port " + port);
                            packet = new DatagramPacket(buffer, buffer.length,
                                                        addr, port);
                            socket.send(packet);
                        }
                    }
                }
            }
            catch (IOException e) {
                e.printStackTrace();
            }
        }
    }



}

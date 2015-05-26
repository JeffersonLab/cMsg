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
import java.util.*;

/**
 * This class finds cMsg domain name servers and rc domain multicast servers
 * which are listening on UDP sockets.
 * By convention, all cMsg name servers should have a multicast port starting at
 * 45000 and not exceeding 45099. All these ports will be probed for servers.
 * By convention, all rc multicast servers should have a multicast port starting at
 * 45200 and not exceeding 45299. All these ports will be probed for servers.
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
    private HashSet<String> cMsgResponders;

    /** Map of all rc domain responders' hosts, ports, and expids
     *  with key = "host:udpPort" and value = expid. */
    private HashMap<String, String> rcResponders;

    /** Time in milliseconds waiting for a response to the multicasts. */
    private int sleepTime = 3000;

    /** Do changes to the expid or added ports necessitate finding rc multicast servers again? */
    private volatile boolean needToUpdateRc = true;

    /** Do added ports necessitate finding cmsg name servers again? */
    private volatile boolean needToUpdateCmsg = true;

    /** Level of debug output for this class. */
    private int debug;




    /** Constructor. */
    public cMsgServerFinder() {
        this(cMsgConstants.debugNone);
    }

    /** Constructor. */
    public cMsgServerFinder(int debug) {
        this.debug = debug;

        rcResponders   = new HashMap<String, String>(100);
        cMsgResponders = new HashSet<String>(100);

        rcPorts   = new HashSet<Integer>(100);
        cmsgPorts = new HashSet<Integer>(100);

        defaultRcPorts   = new int[100];
        defaultCmsgPorts = new int[100];

        // set default ports to scan
        for (int i=0; i<100; i++) {
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
     * <li>{@link cMsgConstants#debugNone} for no outuput<p>
     * <li>{@link cMsgConstants#debugSevere} for severe error output<p>
     * <li>{@link cMsgConstants#debugError} for all error output<p>
     * <li>{@link cMsgConstants#debugWarn} for warning and error output<p>
     * <li>{@link cMsgConstants#debugInfo} for information, warning, and error output<p>
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
    synchronized public void addRcPort(Collection<Integer> col) {
        for (Integer port : col) {
            addRcPort(port);
        }
    }


    /**
     * Remove a UDP port from the list of ports to be probed for rc multicast servers.
     * @param port UDP port to be removed from probing for rc multicast servers
     */
    synchronized public void removeRcPort(int port) {
        rcPorts.remove(port);
    }


    /**
     * Add a UDP port to the list of ports to be probed for cmsg name servers.
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
     * Add a collection of UDP ports to the list of ports to be probed for cmsg name servers.
     * @param col collection of UDP ports to be probed for cmsg name servers
     */
    synchronized public void addCmsgPort(Collection<Integer> col) {
        for (Integer port : col) {
            addCmsgPort(port);
        }
    }


    /**
     * Remove a UDP port from the list of ports to be probed for cmsg name servers.
     * @param port UDP port to be removed from probing for cmsg name servers
     */
    synchronized public void removeCmsgPort(int port) {
        cmsgPorts.remove(port);
    }


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

        for (String s : cMsgResponders) {
            String host = "unknown";
            parts = s.split(":");
            try { host = InetAddress.getByName(parts[0]).getHostName(); }
            catch (UnknownHostException e) { }
            System.out.println("host = " + host + ",  addr = " + parts[0] +
                    ",  UDP port = " + parts[2] +
                    ",  TCP port = " + parts[1]);
        }

        if (rcResponders.size() > 0) {
            System.out.println("\nrc multicast servers:");
        }

        for (Map.Entry<String,String> entry :  rcResponders.entrySet()) {
            String host = "unknown";
            parts = entry.getKey().split(":");
            try { host = InetAddress.getByName(parts[0]).getHostName(); }
            catch (UnknownHostException e) { }
            System.out.println("host = " + host + ",  addr = " + parts[0] +
                    ",  UDP port = " + parts[1] + ", expid = " + entry.getValue());
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
            for (String s : cMsgResponders) {
                String host = "unknown";
                parts = s.split(":");
                try { host = InetAddress.getByName(parts[0]).getHostName(); }
                catch (UnknownHostException e) { }
                buffer.append("<cMsgNameServer");
                buffer.append("  host=\"");    buffer.append(host);
                buffer.append("\"  addr=\"");    buffer.append(parts[0]);
                buffer.append("\"  udpPort=\""); buffer.append(parts[1]);
                buffer.append("\"  tcpPort=\""); buffer.append(parts[2]);
                buffer.append("\" />\n");
            }
        }

        if (rc) {
            for (Map.Entry<String,String> entry :  rcResponders.entrySet()) {
                String host = "unknown";
                parts = entry.getKey().split(":");
                try { host = InetAddress.getByName(parts[0]).getHostName(); }
                catch (UnknownHostException e) { }
                buffer.append("<rcMulticastServer");
                buffer.append("\"  host=\"");    buffer.append(host);
                buffer.append("\"  addr=\"");    buffer.append(parts[0]);
                buffer.append("\"  udpPort=\""); buffer.append(parts[1]);
                buffer.append("\"  expid=\"");   buffer.append(entry.getValue());
                buffer.append("\" />\n");
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

        for (String s : cMsgResponders) {

            String host = "unknown";
            parts = s.split(":");
            try { host = InetAddress.getByName(parts[0]).getHostName(); }
            catch (UnknownHostException e) { }

            cMsgMessage msg = new cMsgMessage();

            try {
                cMsgPayloadItem item1 = new cMsgPayloadItem("host",    host);
                cMsgPayloadItem item2 = new cMsgPayloadItem("address", parts[0]);
                cMsgPayloadItem item3 = new cMsgPayloadItem("udpPort", Integer.parseInt(parts[1]));
                cMsgPayloadItem item4 = new cMsgPayloadItem("tcpPort", Integer.parseInt(parts[2]));

                msg.addPayloadItem(item1);
                msg.addPayloadItem(item2);
                msg.addPayloadItem(item3);
                msg.addPayloadItem(item4);
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

        for (Map.Entry<String,String> entry :  rcResponders.entrySet()) {

            String host = "unknown";
            parts = entry.getKey().split(":");
            try { host = InetAddress.getByName(parts[0]).getHostName(); }
            catch (UnknownHostException e) { }

            cMsgMessage msg = new cMsgMessage();

            try {
                cMsgPayloadItem item1 = new cMsgPayloadItem("host",    host);
                cMsgPayloadItem item2 = new cMsgPayloadItem("address", parts[0]);
                cMsgPayloadItem item3 = new cMsgPayloadItem("udpPort", Integer.parseInt(parts[1]));
                cMsgPayloadItem item4 = new cMsgPayloadItem("expid",   entry.getValue());

                msg.addPayloadItem(item1);
                msg.addPayloadItem(item2);
                msg.addPayloadItem(item3);
                msg.addPayloadItem(item4);
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
                // int describing our message type: multicast is from cMsg domain client
                out.writeInt(cMsgNetworkConstants.cMsgDomainMulticast);
                out.writeInt(password.length());
                try {out.write(password.getBytes("US-ASCII"));}
                catch (UnsupportedEncodingException e) { }
                out.flush();
                out.close();

                // create socket to receive at anonymous port & all interfaces
                socket = new DatagramSocket();
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

            sender.interrupt();

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
            String nameServerHost;
            int nameServerTcpPort, nameServerUdpPort;
            StringBuffer id = new StringBuffer(1024);
            byte[] buf = new byte[1024];
            DatagramPacket packet = new DatagramPacket(buf, 1024);

            while (true) {
                try {
                    nameServerHost = "";
                    packet.setLength(1024);
//System.out.println("Waiting to receive a packet");
                    socket.receive(packet);

                    // if packet is smaller than 6 ints
                    if (packet.getLength() < 24) {
                        continue;
                    }

//System.out.println("RECEIVED MULTICAST RESPONSE PACKET !!!");
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
                    int hostLength    = cMsgUtilities.bytesToInt(buf, 20); // host to do a direct connection to

                    if ((nameServerTcpPort < 1024 || nameServerTcpPort > 65535) ||
                            (hostLength < 0 || hostLength > 1024 - 24)) {
//System.out.println("  Wrong port # or host length for multicast response packet");
                        continue;
                    }

                    if (packet.getLength() != 4*6 + hostLength) {
//System.out.println("  Wrong length for multicast response packet");
                        continue;
                    }

                    // cMsg name server host
                    try { nameServerHost = new String(buf, 24, hostLength, "US-ASCII"); }
                    catch (UnsupportedEncodingException e) {}
//System.out.println("  Got port = " + nameServerTcpPort + ", host = " + nameServerHost);

                    // put in a unique item: "host:udpPort:tcpPort"
                    if (nameServerHost.length() > 0) {
                        id.delete(0,1023);
                        id.append(nameServerHost);
                        id.append(":");
                        id.append(nameServerUdpPort);
                        id.append(":");
                        id.append(nameServerTcpPort);
                        cMsgResponders.add(id.toString());
                    }
                }
                catch (InterruptedIOException e) {
//System.out.println("  Interrupted receiving thread so return");
                    return;
                }
                catch (IOException e) {
//System.out.println("  IO exception in receiving thread so return");
                    return;
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
            DatagramSocket socket = null;

            // create byte array for multicast
            ByteArrayOutputStream baos = new ByteArrayOutputStream(1024);
            DataOutputStream out = new DataOutputStream(baos);

            try {
                // Put our magic #s, TCP listening port, name, and
                // the EXPID (experiment id string) into byte array.
                out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
                out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
                out.writeInt(cMsgNetworkConstants.magicNumbers[2]);
                out.writeInt(cMsgNetworkConstants.rcDomainMulticastProbe); // multicast is from rc domain prober
                out.writeInt(1);                // port = 1 identifies us as being from cMsgServerFinder
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
                socket = new DatagramSocket();
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

            sender.interrupt();

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

        public void run() {

            int index;
            byte[] buf = new byte[1024];
            DatagramPacket packet = new DatagramPacket(buf, 1024);
            StringBuffer id = new StringBuffer(1024);

            while (true) {
                // reset for each round
                packet.setLength(1024);

                try {
                    socket.receive(packet);
//System.out.println("received UDP packet");
                    // if we get too small of a packet, reject it
                    if (packet.getLength() < 6*4) {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("rc Multicast receiver: got packet that's too small");
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
                            System.out.println("rc Multicast receiver: got bad magic # response to multicast");
                        }
                        continue;
                    }

                    int port     = cMsgUtilities.bytesToInt(buf, 12);
                    int hostLen  = cMsgUtilities.bytesToInt(buf, 16);
                    int expidLen = cMsgUtilities.bytesToInt(buf, 20);

                    if (packet.getLength() < 4*6 + hostLen + expidLen) {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("rc Multicast receiver: got packet that's too small");
                        }
                        continue;
                    }

                    // get host
                    index = 24;
                    String host = "";
                    if (hostLen > 0) {
                        host = new String(buf, index, hostLen, "US-ASCII");
//System.out.println("host = " + host);
                        index += hostLen;
                    }

                    // get expid
                    String serverExpid="expid";
                    if (expidLen > 0) {
                        serverExpid = new String(buf, index, expidLen, "US-ASCII");
//System.out.println("expid = " + serverExpid);
//                        if (!expid.equals(serverExpid)) {
//                            if (debug >= cMsgConstants.debugWarn) {
//                                System.out.println("rc Multicast receiver: got bad expid response to multicast (" + serverExpid + ")");
//                            }
//                            continue;
//                        }
                    }

                    // put in a unique item: "host:udpPort"
                    if (host.length() > 0) {
                        id.delete(0,1023);
                        id.append(host);
                        id.append(":");
                        id.append(port);
                        rcResponders.put(id.toString(), serverExpid);
                    }

                }
                catch (InterruptedIOException e) {
//System.out.println("  Interrupted receiving thread so return");
                    return;
                }
                catch (IOException e) {
//System.out.println("  IO exception in receiving thread so return");
                    return;
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
        DatagramSocket socket;

        rcMulticaster(byte[] buffer, DatagramSocket socket) {
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
                for (int port : rcPorts) {
//System.out.println("Send multicast packets on port " + port);
                    packet = new DatagramPacket(buffer, buffer.length,
                                                addr, port);
                    socket.send(packet);
                }
                for (int port : defaultRcPorts) {
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



}

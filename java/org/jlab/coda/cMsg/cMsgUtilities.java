/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 7-Jul-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;

import java.net.*;
import java.nio.channels.SocketChannel;
import java.nio.channels.ByteChannel;
import java.nio.ByteBuffer;
import java.io.IOException;
import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * This class stores methods which are neatly self-contained and
 * may be used in more that one place.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgUtilities {

    /**
     * Converts 4 bytes of a byte array into an integer.
     *
     * @param b byte array
     * @param off offset into the byte array (0 = start at first element)
     * @return integer value
     */
    static public final int bytesToInt(byte[] b, int off) {
        return (((b[off]  &0xff) << 24) |
                ((b[off+1]&0xff) << 16) |
                ((b[off+2]&0xff) <<  8) |
                 (b[off+3]&0xff));
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
     * Converts 2 bytes of a byte array into a short.
     *
     * @param b   byte array
     * @param off offset into the byte array (0 = start at first element)
     * @return integer value
     */
    public static final short bytesToShort(byte[] b, int off) {
        return  (short) ( ((b[off+1] & 0xff) << 8)  | (b[off] & 0xff) );
    }


    /**
     * Copies a short value into 2 bytes of a byte array.
     * @param val short value
     * @param b byte array
     * @param off offset into the byte array
     */
    public static final void shortToBytes(short val, byte[] b, int off) {
      b[off]   = (byte) ((val & 0x0000ff00) >>>  8);
      b[off+1] = (byte)  (val & 0x000000ff);
    }


    /**
     * Converts 8 bytes of a byte array into a long.
     *
     * @param b byte array
     * @param off offset into the byte array (0 = start at first element)
     * @return ling value
     */
    static public final long bytesToLong(byte[] b, int off) {
        return (((b[off]  &0xffL) << 56) |
                ((b[off+1]&0xffL) << 48) |
                ((b[off+2]&0xffL) << 40) |
                ((b[off+3]&0xffL) << 32) |
                ((b[off+4]&0xffL) << 24) |
                ((b[off+5]&0xffL) << 16) |
                ((b[off+6]&0xffL) <<  8) |
                 (b[off+7]&0xffL));
    }


    /**
     * Get all local IP addresses in a list in dotted-decimal form.
     * The first IP address in the list is the one associated with
     * the canonical host name.
     * @return list of all local IP addresses in dotted-decimal form.
     */
    public static Collection<String> getAllIpAddresses() {
        // Set of our IP addresses in order
        LinkedHashSet<String> set = new LinkedHashSet<String>();

        try {
            // Start with IP addr associated with canonical host name (if any)
            String canonicalIP = InetAddress.getByName(InetAddress.getLocalHost().
                                                getCanonicalHostName()).getHostAddress();

            if (canonicalIP != null) {
//System.out.println("getAllIpAddresses: add canonical IP = " + canonicalIP);
                set.add(canonicalIP);
            }
        }
        catch (UnknownHostException e) {}

        try {
            Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();
            while (enumer.hasMoreElements()) {
                NetworkInterface ni = enumer.nextElement();
                if (ni.isUp() && !ni.isLoopback()) {
                    List<InterfaceAddress> inAddrs = ni.getInterfaceAddresses();
                    for (InterfaceAddress ifAddr : inAddrs) {
                        InetAddress addr = ifAddr.getAddress();

                        // skip IPv6 addresses (IPv4 addr has 4 bytes)
                        if (addr.getAddress().length != 4) continue;

                        set.add(addr.getHostAddress());
//System.out.println("getAllIpAddresses: add ip address = " + addr.getHostAddress());
                    }
                }
            }
        }
        catch (SocketException e) {
            e.printStackTrace();
        }

        return set;
    }


    /**
     * Get all local IP broadcast addresses in a list in dotted-decimal form.
     * This only makes sense for IPv4.
     * @return list of all local IP broadcast addresses in dotted-decimal form.
     */
    public static Collection<String> getAllBroadcastAddresses() {

        // Set of our IP addresses
        LinkedHashSet<String> set = new LinkedHashSet<String>();

        try {
            Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();
            while (enumer.hasMoreElements()) {
                NetworkInterface ni = enumer.nextElement();
                if (ni.isUp() && !ni.isLoopback()) {
                    List<InterfaceAddress> inAddrs = ni.getInterfaceAddresses();
                    for (InterfaceAddress ifAddr : inAddrs) {
                        Inet4Address bAddr;
                        try { bAddr = (Inet4Address)ifAddr.getBroadcast(); }
                        catch (ClassCastException e) {
                            // probably IPv6 so ignore
                            continue;
                        }

                        set.add(bAddr.getHostAddress());
                    }
                }
            }
        }
        catch (SocketException e) {
            e.printStackTrace();
        }

        // Try this if nothing else works
        if (set.size() < 1) {
            set.add("255.255.255.255");
        }

        return set;
    }



    /**
     * Get all local IP addresses, broadcast addresses and subnet masks in a
     * single list of InterfaceAddress objects. This only returns IPv4 info
     * from interfaces which are up and are NOT the loopback.
     *
     * @return lists of all InterfaceAddress objects which are up and not loopback.
     */
    public static List<InterfaceAddress> getAllIpInfo() {

        // List of our IP addresses
        LinkedList<InterfaceAddress> ipList = new LinkedList<InterfaceAddress>();

        try {
            Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();

            while (enumer.hasMoreElements()) {
                NetworkInterface ni = enumer.nextElement();

                if (ni.isUp() && !ni.isLoopback()) {
                    List<InterfaceAddress> inAddrs = ni.getInterfaceAddresses();

                    for (InterfaceAddress ifAddr : inAddrs) {
                        // Regular IP address
                        InetAddress addr = ifAddr.getAddress();
                        // Skip IPv6 addresses (IPv4 addr has 4 bytes)
                        if (addr.getAddress().length != 4) continue;
                        ipList.add(ifAddr);
                    }
                }
            }
        }
        catch (SocketException e) {
            e.printStackTrace();
        }

        return ipList;
    }


    /**
     * Get the network or subnet address (32-bit integer format)
     * of the given IP address (byte array format)
     * for the given netmask (# of bits format).
     *
     * @param addr IP address (byte array format)
     * @param netPrefixLen netmask (# of bits format)
     * @return network or subnet address (32-bit integer format);
     *         -1 if bad arg(s)
     */
    public static int getNetworkAddressInt(byte[] addr, short netPrefixLen) {
        if (addr == null || netPrefixLen < 0 || netPrefixLen > 32) return -1;

        // Convert 4 byte addr (network byte order) into 32 bit int
        int intAddr = 0;
        for (int i = 0; i < 4; ++i) {
            intAddr |= ((addr[i] & 0xff) << 8*(3-i));
        }

        // Turn net prefix len into integer mask
        int intNetMask = 0;
        for (int j = 0; j < netPrefixLen; ++j) {
            intNetMask |= (1 << 31-j);
        }

        // Find network (subnet) address as int
        return (intAddr & intNetMask);
    }


    /**
     * Get the network or subnet address as a dotted-decimal string
     * of the given IP address (byte array format)
     * for the given netmask (# of bits format).
     *
     * @param addr IP address (byte array format)
     * @param netPrefixLen netmask (# of bits format)
     * @return network or subnet address (dotted-decimal string format);
     *         null if bad arg(s)
     */
    public static String getNetworkAddressString(byte[] addr, short netPrefixLen) {
        if (addr == null || netPrefixLen < 0 || netPrefixLen > 32) return null;

        // Find network (subnet) address as int
        int network = getNetworkAddressInt(addr, netPrefixLen);

        // Turn network int into 4 bytes of network byte order
        // (highest order byte = ret[0]).
        byte ret[] = new byte[4];
        for (int j = 3; j >= 0; --j) {
            ret[j] |= (network >>> 8*(3-j));
        }

        // Convert 4 byte array into dotted-decimal format
        StringBuilder strBuilder = new StringBuilder();
        for (int i = 0; i < ret.length; ++i) {
            int bite = ((int) ret[i]) & 0xff;
            strBuilder.append(bite);
            if (i != ret.length - 1) {
                strBuilder.append(".");
            }
        }

        return strBuilder.toString();
    }


    /**
     * Get the network or subnet address as a dotted-decimal string
     * of the given IP address (dotted-decimal string format)
     * for the given netmask (# of bits format).
     *
     * @param hostDottedDecimal IP address (dotted-decimal string format)
     * @param netPrefixLen netmask (# of bits format)
     * @return network or subnet address (dotted-decimal string format);
     *         null if bad arg(s)
     */
    public static String getNetworkAddressString(String hostDottedDecimal, short netPrefixLen) {

        if (hostDottedDecimal == null || netPrefixLen < 0 || netPrefixLen > 32) return null;

        String IP_ADDRESS = "(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})";
        Pattern pattern = Pattern.compile(IP_ADDRESS);
        Matcher matcher = pattern.matcher(hostDottedDecimal);
//System.out.println("getNetworkAddressString: host arg = " + hostDottedDecimal);
        if (!matcher.matches()) return null;

        try {
            int[] hostInts = new int[4];
            for (int i = 1; i <= 4; ++i) {
// System.out.println("   group(" + i + ") = " + matcher.group(i));
                hostInts[i-1] = Integer.parseInt(matcher.group(i));
                if (hostInts[i-1] > 255) return null;
            }

            // Change ints to bytes
            byte[] hostBytes = new byte[4];
            hostBytes[0] = (byte) hostInts[0];
            hostBytes[1] = (byte) hostInts[1];
            hostBytes[2] = (byte) hostInts[2];
            hostBytes[3] = (byte) hostInts[3];

            return getNetworkAddressString(hostBytes, netPrefixLen);
        }
        catch (IndexOutOfBoundsException e) {}
        catch (NumberFormatException e) {}

        return null;
    }


    /**
      * Determine whether a given host name refers to the local host.
      * @param hostName host name that is checked to see if its local or not
      */
     public static final boolean isHostLocal(String hostName) {
        if (hostName == null || hostName.length() < 1) return false;

        try {
            String canonicalHost = InetAddress.getLocalHost().getCanonicalHostName();

            // quick check
            if (canonicalHost.equalsIgnoreCase(hostName)) {
                return true;
            }

            // get all local IP addresses
            InetAddress[] localAddrs = InetAddress.getAllByName(canonicalHost);
            // get all hostName's IP addresses
            InetAddress[] hostAddrs  = InetAddress.getAllByName(hostName);

            // see if any 2 addresses are identical
            for (InetAddress lAddr : localAddrs) {
                for (InetAddress hAddr : hostAddrs) {
                    if (lAddr.equals(hAddr)) return true;
                }
            }
        }
        catch (UnknownHostException e) {}

        return false;
     }

    
    /**
     * Determine whether two given host names refers to the same host.
     * @param hostName1 host name that is checked to see if it is the same as the other arg or not.
     * @param hostName2 host name that is checked to see if it is the same as the other arg or not.
     */
     public static final boolean isHostSame(String hostName1, String hostName2) {
        // arg check
        if (hostName1 == null || hostName1.length() < 1) return false;
        if (hostName2 == null || hostName2.length() < 1) return false;

        try {
            // quick check
            String canonicalHost1 = InetAddress.getByName(hostName1).getCanonicalHostName();
            String canonicalHost2 = InetAddress.getByName(hostName2).getCanonicalHostName();
            if (canonicalHost1.equalsIgnoreCase(canonicalHost2)) {
                return true;
            }

            // otherwise compare all know IP addresses against each other

            // get all hostName1's IP addresses
            InetAddress[] hostAddrs1  = InetAddress.getAllByName(hostName1);

            // get all hostName2's IP addresses
            InetAddress[] hostAddrs2  = InetAddress.getAllByName(hostName2);

            // see if any 2 addresses are identical
            for (InetAddress lAddr : hostAddrs1) {
                for (InetAddress hAddr : hostAddrs2) {
                    if (lAddr.equals(hAddr)) return true;
                }
            }
        }
        catch (UnknownHostException e) {}

        return false;
     }


    static public final ByteChannel wrapChannel2(final ByteChannel channel)
    {
        return channel;
    }


    /**
     * This method wraps a SocketChannel so that the input and output streams
     * derived from that channel do not block each other (SUN java bug id 4774871).
     * The problem is that a SocketChannel is also a SelectableChannel which has this
     * synchronization issue, and that is eliminated by making it a ByteChannel.
     *
     * @param channel the SelectableChannel which has sync problems
     * @return a ByteChannel which does not have sync problems
     */
     static public final ByteChannel wrapChannel(final ByteChannel channel) {
        return new ByteChannel() {
            public int write(ByteBuffer src) throws IOException {
                return channel.write(src);
            }

            public int read(ByteBuffer dst) throws IOException {
                return channel.read(dst);
            }

            public boolean isOpen() {
                return channel.isOpen();
            }

            public void close() throws IOException {
                channel.close();
            }
        };
    }


    /**
      * This methods reads a minimum of number of bytes from a channel into a buffer.
      *
      * @param buffer   a byte buffer which channel data is read into
      * @param channel  nio socket communication channel
      * @param bytes    minimum number of bytes to read from channel
      * @return number of bytes read
      * @throws IOException If channel is closed or cannot be read from
      */
     static public final int readSocketBytesPlain(ByteBuffer buffer, SocketChannel channel, int bytes)
             throws IOException {

         int n, count = 0;

         buffer.clear();
         buffer.limit(bytes);

         // Keep reading until we have exactly "bytes" number of bytes,

         while (count < bytes) {
             if ((n = channel.read(buffer)) < 0) {
                 throw new IOException("readSocketBytes: client's socket is dead");
             }
             count += n;
         }
         return count;
     }


    /**
      * This methods reads a minimum of number of bytes from a channel into a buffer.
      *
      * @param buffer   a byte buffer which channel data is read into
      * @param channel  nio socket communication channel
      * @param bytes    minimum number of bytes to read from channel
      * @param debug    level of debug output
      * @return number of bytes read
      * @throws IOException If channel is closed or cannot be read from
      */
     static public final int readSocketBytes(ByteBuffer buffer, SocketChannel channel, int bytes, int debug)
             throws IOException {

         int n, tries = 0, count = 0, maxTries=200;

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

             if (tries > maxTries) {
                 throw new IOException("readSocketBytes: too many tries to read " + n + " bytes");
             }
             tries++;
             if (debug >= cMsgConstants.debugInfo && tries==maxTries) {
                 System.out.println("readSocketBytes: called read " + tries + " times, read " + n + " bytes");
             }
             try {
                 Thread.sleep(1);
             }
             catch (InterruptedException e) {}
         }
         return count;
     }



    /**
     * Method that returns and/or prints an error explanation.
     *
     * @param error error number
     * @param debug level of debug output
     * @return error explanation
     */
    static public final String printError(int error, int debug) {
      String reason;

      switch (error) {

          case cMsgConstants.ok:
              reason = "action completed successfully";
              if (debug > cMsgConstants.debugError)
                  System.out.println("ok: " + reason);
              return(reason);

          case cMsgConstants.error:
              reason = "generic error return";
              break;

          case cMsgConstants.errorTimeout:
              reason = "no response from cMsg server within timeout period";
              break;

          case cMsgConstants.errorNotImplemented:
              reason = "function not implemented";
              break;

          case cMsgConstants.errorBadArgument:
              reason = "one or more arguments bad";
              break;

          case cMsgConstants.errorBadFormat:
              reason = "one or more arguments in the wrong format";
              break;

          case cMsgConstants.errorBadDomainType:
              reason = "domain type not supported";
              break;

          case cMsgConstants.errorAlreadyExists:
              reason = "another process in this domain is using this name";
              break;

          case cMsgConstants.errorNotInitialized:
              reason = "connection to server needs to be made";
              break;

          case cMsgConstants.errorAlreadyInitialized:
              reason = "connection to server already exists";
              break;

          case cMsgConstants.errorLostConnection:
              reason = "connection to cMsg server lost";
              break;

          case cMsgConstants.errorNetwork:
              reason = "error talking to cMsg server";
              break;

          case cMsgConstants.errorSocket:
              reason = "error setting socket options";
              break;

          case cMsgConstants.errorPend:
              reason = "error waiting for messages to arrive";
              break;

          case cMsgConstants.errorIllegalMessageType:
              reason = "pend received illegal message type";
              break;

          case cMsgConstants.errorNoMemory:
              reason = "out of memory";
              break;

          case cMsgConstants.errorOutOfRange:
              reason = "argument is out of range";
              break;

          case cMsgConstants.errorLimitExceeded:
              reason = "trying to create too many of something";
              break;

          case cMsgConstants.errorBadDomainId:
              reason = "intVal does not match any existing domain";
              break;

          case cMsgConstants.errorBadMessage:
              reason = "message is not in the correct form";
              break;

          case cMsgConstants.errorWrongDomainType:
              reason = "UDL does not match the server type";
              break;

          case cMsgConstants.errorNoClassFound:
              reason = "java class cannot be found";
              break;

          case cMsgConstants.errorDifferentVersion:
              reason = "error due to being different version";
              break;

          case cMsgConstants.errorWrongPassword:
              reason = "error due to wrong password";
              break
                      ;
          case cMsgConstants.errorServerDied:
              reason = "error due to server dying";
              break;

          case cMsgConstants.errorAbort:
              reason = "error due to aborted procedure";
              break;

          default:
              reason = "no such error (" + error + ")";
              if (debug > cMsgConstants.debugError)
                  System.out.println("error: "+ reason);
              return(reason);
      }

      if (debug > cMsgConstants.debugError)
          System.out.println("error: " + reason);

      return(reason);
    }

    /**
     * This method tests its input argument to see if it is in the proper format
     * for a server; namely, "host:port". If it is, it makes sure the host is in
     * its canonical form.
     *
     * @param s input string of a possible server name
     * @return an acceptable server name with the canonical form of the host name
     * @throws org.jlab.coda.cMsg.cMsgException if input string is not in the proper form (host:port)
     *                       or the host is unknown
     */
    static public final String constructServerName(String s) throws cMsgException {

        // Separate the server name from the server port.
        // First check for ":"
        int index = s.indexOf(":");
        if (index == -1) {
            throw new cMsgException("arg is not in the \"host:port\" format");
        }

        String sName = s.substring(0, index);
        String sPort = s.substring(index + 1);
        int port;

        // translate the port from string to int
        try {
            port = Integer.parseInt(sPort);
        }
        catch (NumberFormatException e) {
            throw new cMsgException("port needs to be an integer");
        }

        if (port < 1024 || port > 65535) {
            throw new cMsgException("port needs to be an integer between 1024 & 65535");
        }

        // See if this host is recognizable. To do that
        boolean doingMulticast = false;
        InetAddress address = null;
        try {
            if (sName.equalsIgnoreCase("localhost")) {
                address = InetAddress.getLocalHost();
            }
            else if (sName.equalsIgnoreCase("multicast")) {
                doingMulticast = true;
            }
            else {
                address = InetAddress.getByName(sName);
            }
        }
        catch (UnknownHostException e) {
            throw new cMsgException("specified server is unknown");
        }

        // put everything in canonical form if possible
        if (doingMulticast) {
            s = "multicast" + ":" + sPort;
        }
        else {
            s = address.getCanonicalHostName() + ":" + sPort;
        }

        return s;
    }
}

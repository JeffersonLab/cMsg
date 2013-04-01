package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.cMsgNetworkConstants;

import java.io.IOException;
import java.net.*;
import java.util.Enumeration;
import java.util.List;

/**
 * Print out all network info used by the rc multicast server.
 *
 * @author timmer
 * Date: 3/29/13
 */
public class rcServerNetworkTest {


    private static void printNI(NetworkInterface ni) {
        System.out.println("\n\nInterface name = " + ni.getName());
        System.out.println("Interface display name = " + ni.getDisplayName());

        int counter = 1;

        List<InterfaceAddress> inAddrs  = ni.getInterfaceAddresses();
        for (InterfaceAddress ifAddr : inAddrs) {
            System.out.println("\n  interface address #" + counter++ + ":");
            InetAddress addr = ifAddr.getAddress();
            System.out.println("    host address = " + addr.getHostAddress());
            System.out.println("    canonical host name = " + addr.getCanonicalHostName());
            System.out.println("    host name = " + addr.getHostName());
            System.out.println("    toString() = " + addr.toString());

            InetAddress baddr = ifAddr.getBroadcast();
            if (baddr != null)
                System.out.println("    broadcast addr = " + baddr.getHostAddress());
        }
    }


    /** This method is executed as a thread. */
    public static void main(String[] args) {
        try {

            int udpPort = cMsgNetworkConstants.rcMulticastPort;

            System.out.println("Rc multicast server listens on port " + udpPort + " and address " +
                                       cMsgNetworkConstants.rcMulticast);

            // send dotted-decimal if possible
            String host = null;
            try {
                host = InetAddress.getLocalHost().getHostAddress();
            }
            catch (UnknownHostException e) {
                try {
                    host = InetAddress.getLocalHost().getCanonicalHostName();
                }
                catch (UnknownHostException e1) {
                }
            }
            System.out.println("\nPacket sent from rc multicast server to rc client has server host = " + host);
            System.out.println("  (but that is NOT used by the rc client).");



            System.out.println("\nPacket sent from rc server to rc client has server host (canonical) = " +
                                       InetAddress.getLocalHost().getCanonicalHostName());
            // send list of our IP addresses (skipping IPv6)
            System.out.println("  send list of IP addresses:");
                try {
                    Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();
                    while (enumer.hasMoreElements()) {
                        NetworkInterface ni = enumer.nextElement();
                        if (ni.isUp() && !ni.isLoopback()) {
                            List<InterfaceAddress> inAddrs  = ni.getInterfaceAddresses();
                            for (InterfaceAddress ifAddr : inAddrs) {
                                InetAddress addr = ifAddr.getAddress();
                                byte b[] = addr.getAddress();
                                // skip IPv6 addresses
                                if (b.length != 4) continue;
System.out.println("    " + addr.getHostAddress());
                            }
                        }
                    }

                }
                catch (SocketException e) {
                    e.printStackTrace();
                }

System.out.println("Rc client uses IP addresses, in given order, to connect to rc server");



            // Be sure to join the multicast address group of all network interfaces
            // (something not mentioned in any javadocs or books!).
            Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();
            while (enumer.hasMoreElements()) {
                NetworkInterface ni = enumer.nextElement();
                if (ni.isUp() && ni.supportsMulticast() && !ni.isLoopback()) {
                    System.out.println("\nJoin multicast addr group of net interface w/ addrs: ");
                    printNI(ni);
                }
            }

        }
        catch (IOException e) {
            e.printStackTrace();
        }





    }

}

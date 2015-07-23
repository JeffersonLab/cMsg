/*----------------------------------------------------------------------------*
 *  Copyright (c) 2008        Southeastern Universities Research Association, *
 *                            Jefferson Science Associates                    *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C.Timmer, 17-Sep-2008, Jefferson Lab                                    *
 *                                                                            *
 *    Authors: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.cMsgConstants;
import org.jlab.coda.cMsg.common.cMsgServerFinder;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgException;

import java.util.HashSet;
import java.util.Set;

/**
 * This is an example class which creates a cMsg server finder. It is useful in and of itself.
 * This class will find cMsg domain servers and rc multicast servers -- each of which may be
 * operating at different ports. By default, the cMsgServerFinder class (which this
 * class uses) probes ports starting from
 * {@link org.jlab.coda.cMsg.cMsgNetworkConstants#rcMulticastPort}
 * and the next 99 values for rc multicast server, and it also probes ports starting from
 * {@link org.jlab.coda.cMsg.cMsgNetworkConstants#nameServerUdpPort} and the next 99 values
 * for cmsg domain servers. Additional ports to probe can be included on the command line.
 */
public class cMsgFindServers {

    /** Object to find servers with. */
    private cMsgServerFinder finder;

    /** Port numbers provided by caller to probe in cmsg domain. */
    private HashSet<Integer> cmsgPorts;

    /** Port numbers provided by caller to probe in rc domain. */
    private HashSet<Integer> rcPorts;

    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugInfo;

    /** Time in seconds to wait for server responses.
     *  Negative values mean use the default (3 sec). */
    private int waitTime = -1;

    /** Level of debug output for this class. */
    private boolean inXML;


    /** Constructor. */
    cMsgFindServers(String[] args) {
        rcPorts   = new HashSet<Integer>(100);
        cmsgPorts = new HashSet<Integer>(100);
        finder    = new cMsgServerFinder();
        decodeCommandLine(args);
    }


    /** Method to print out correct program command line usage. */
    private static void usage() {
        System.out.println("\nUsage:\n\n" +
                "   java cMsgServerFinder\n" +
                "        [-cmsg <UDP ports list>]   list of cMsg domain UDP ports to probe\n" +
                "        [-rc   <UDP ports list>]   list of rc domain UDP ports to probe\n" +
                "        [-pswd <password>]         password for connecting to cMsg domain server\n" +
                "        [-xml]                     output in XML\n" +
                "        [-t]                       time to wait for server response in seconds\n" +
                "        [-h]                       print this help\n");
        System.out.println("        A port list is a single string with ports separated by");
        System.out.println("        white space or punctuation with the exception of dashes.");
        System.out.println("        Two ports joined by a single dash are taken as a range of ports.");
        System.out.println("        No white space allowed in defining ranges.\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
            cMsgFindServers sfinder = new cMsgFindServers(args);
            sfinder.run();
    }


    public void run() {
        // start thread to find cMsg name servers
        finder.find();
        System.out.println();

        if (inXML) {
            System.out.println(finder.toString());
            return;
        }

        cMsgMessage[] rcServers   = finder.getRcServers();
        cMsgMessage[] cmsgServers = finder.getCmsgServers();
        int i=0;
        if (rcServers != null) {
            System.out.println("rcServers:");
            for (cMsgMessage msg : rcServers) {
                System.out.println("    server #" + (i+1));
                try {
                    System.out.println("        host    = " + msg.getPayloadItem("host").getString());
                    System.out.println("        udpPort = " + msg.getPayloadItem("udpPort").getInt());
                    System.out.println("        expid   = " + msg.getPayloadItem("expid").getString());
                    System.out.println("        addresses:");
                    String[] ip = msg.getPayloadItem("addresses").getStringArray();
                    String[] bcast = msg.getPayloadItem("bcastAddresses").getStringArray();
                    for (int j=0; j < ip.length; j++) {
                        System.out.println("           ip = " + ip[j] + ", bcast = " + bcast[j]);
                    }
                    System.out.println();
                }
                catch (cMsgException e) { /* never happen */ }

                i++;
            }
        }

        i=0;
        if (cmsgServers != null) {
            System.out.println("cMsgServers:");
            for (cMsgMessage msg : cmsgServers) {
                System.out.println("    server #" + (i+1));
                try {
                    System.out.println("        host      = " + msg.getPayloadItem("host").getString());
                    System.out.println("        udpPort   = " + msg.getPayloadItem("udpPort").getInt());
                    System.out.println("        tcpPort   = " + msg.getPayloadItem("tcpPort").getInt());
                    System.out.println("        addresses:");
                    String[] ip = msg.getPayloadItem("addresses").getStringArray();
                    String[] bcast = msg.getPayloadItem("bcastAddresses").getStringArray();
                    for (int j=0; j < ip.length; j++) {
                        System.out.println("           ip = " + ip[j] + ", bcast = " + bcast[j]);
                    }
                    System.out.println();
                }
                catch (cMsgException e) { /* never happen */ }

                i++;
            }
        }
    }


    /**
     * This method decodes the command line used to start this application.
     * @param args command line arguments
     */
    private void decodeCommandLine(String[] args) {

        // loop over all args
        for (int i = 0; i < args.length; i++) {

            if (args[i].equalsIgnoreCase("-h")) {
                usage();
                System.exit(-1);
            }
            else if (args[i].equalsIgnoreCase("-pswd")) {
                String password = args[i + 1];
                finder.setPassword(password);
                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("Setting password to " + password);
                }
                i++;
            }
            else if (args[i].equalsIgnoreCase("-rc")) {
                parsePortList(args[i + 1], "rc", rcPorts);
                if (rcPorts.size() > 0) {
                    finder.addRcPorts(rcPorts);
                }
                i++;
            }
            else if (args[i].equalsIgnoreCase("-t")) {
                try {
                    // seconds
                    waitTime = Integer.parseInt(args[i + 1]);
                    // milliseconds
                    finder.setSleepTime(1000*waitTime);
                }
                catch (NumberFormatException e) {
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("bad wait time, ignore");
                    }
                    continue;
                }
                i++;
            }
            else if (args[i].equalsIgnoreCase("-cmsg")) {
                parsePortList(args[i+1], "cmsg", cmsgPorts);
                if (cmsgPorts.size() > 0) {
                    finder.addCmsgPorts(cmsgPorts);
                }
                i++;
            }
            else if (args[i].equalsIgnoreCase("-xml")) {
                inXML = true;
            }
            else {
                usage();
                System.exit(-1);
            }
        }

        return;
    }


    /**
     * This method takes a (command line) string and parses it into a list
     * of port numbers. Ports may be separated by any white space or punctuation,
     * except for dashes. If 2 ports are joined by a dash, it is taken to be a
     * range of ports. All ports must be >1023 and <65536. All formatting errors
     * are ignored.
     *
     * @param s string to parse
     * @param type string describing type of port list (eg. cmsg or rc)
     * @param set set in which to store port numbers (ints)
     */
    private void parsePortList(String s, String type, Set<Integer> set) {
        int port, port1, port2;

        // split string at punctuation and spaces, except dashes
        String[] strs = s.split("[\\p{Punct}\\s&&[^-]]");
        if (strs.length < 1) {
            if (debug >= cMsgConstants.debugError) {
                System.out.println("no valid " + type + " ports specified");
            }
            return;
        }

        // look at each port or range of ports
        for (String str : strs) {

            // check for portA-portB (range) contstructs
            if (str.contains("-")) {
                String[] ports = str.split("-");
                if (ports.length != 2) {
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("wrong # of " + type + " ports defining range");
                    }
                    continue;
                }

                try {
                    port1 = Integer.parseInt(ports[0]);
                    port2 = Integer.parseInt(ports[1]);
                    if (port1 < 1024 || port1 > 65535 ||
                        port2 < 1024 || port2 > 65535) {
                        if (debug >= cMsgConstants.debugError) {
                            System.out.println(type + " ports must be > 1023 and < 65536");
                        }
                        continue;
                    }
                }
                catch (NumberFormatException e) {
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("bad " + type + " port format in range");
                    }
                    continue;
                }

                if (port1 > port2) {
                    port  = port1;
                    port1 = port2;
                    port2 = port;
                }
                for (int k = port1; k <= port2; k++) {
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println("adding " + type + " port " + k);
                    }
                    set.add(k);
                }
            }

            // else check for just plain port numbers
            else {
                try {
                    port = Integer.parseInt(str);
                }
                catch (NumberFormatException e) {
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("bad " + type + " port format");
                    }
                    continue;
                }

                if (port < 1024 || port > 65535) {
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println(type + " ports must be > 1023 and < 65536");
                    }
                    continue;
                }

                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("adding " + type + " port " + port);
                }
                set.add(port);
            }
        }

    }




}

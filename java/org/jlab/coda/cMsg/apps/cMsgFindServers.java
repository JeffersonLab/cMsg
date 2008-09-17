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
import org.jlab.coda.cMsg.cMsgServerFinder;

import java.util.HashSet;
import java.util.Set;

/**
 * 
 */
public class cMsgFindServers {

    /** Object to find servers with. */
    cMsgServerFinder finder;

    /** Port numbers provided by caller to probe in cmsg domain. */
    private HashSet<Integer> cmsgPorts;

    /** Port numbers provided by caller to probe in rc domain. */
    private HashSet<Integer> rcPorts;

    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugError;


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
                "   java cMsgServerFinder [-cmsg <list of cmsg UDP ports>]\n" +
                "                         [-rc <list of rc UDP ports>]\n" +
                "                         [-pswd <password>]\n" +
                "                         [-expid <experimental ID>]\n" +
                "                         [-debug]\n" +
                "                         [-h]\n");
        System.out.println("       A port list is a single string with ports separated by");
        System.out.println("       white space or punctuation with the exception of dashes.");
        System.out.println("       Two ports joined by a single dash are taken as a range of ports.");
        System.out.println("       No white space allowed in defining ranges.");
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
        System.out.println(finder.toString());
    }


   /**
    * This method decodes the command line used to start this application.
    * @param args command line arguments
    */
   public void decodeCommandLine(String[] args) {

       // loop over all args
       for (int i = 0; i < args.length; i++) {

           if (args[i].equalsIgnoreCase("-h")) {
               usage();
               System.exit(-1);
           }
           else if (args[i].equalsIgnoreCase("-pswd")) {
               String password = args[i + 1];
               finder.setPassword(password);
//System.out.println("Setting password to " + password);
               i++;
           }
           else if (args[i].equalsIgnoreCase("-expid")) {
               String expid = args[i + 1];
               finder.setExpid(expid);
//System.out.println("Setting expid to " + expid);
               i++;
           }
           else if (args[i].equalsIgnoreCase("-rc")) {
//System.out.println("Finding rc ports:");
               parsePortList(args[i+1], rcPorts);
               if (rcPorts.size() > 0) {
                   finder.addRcPort(rcPorts);
               }
               i++;
           }
           else if (args[i].equalsIgnoreCase("-cmsg")) {
//System.out.println("Finding cmsg ports:");
               parsePortList(args[i+1], cmsgPorts);
               if (cmsgPorts.size() > 0) {
                   finder.addCmsgPort(cmsgPorts);
               }
               i++;
           }
           else if (args[i].equalsIgnoreCase("-debug")) {
//System.out.println("Turning debug on");
               debug = cMsgConstants.debugInfo;
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
     * @param set set in which to store port numbers (ints)
     */
    private void parsePortList(String s, Set<Integer> set) {
        int port, port1, port2;

        // split string at punctuation and spaces, except dashes
        String[] strs = s.split("[\\p{Punct}\\s&&[^-]]");
        if (strs.length < 1) {
            if (debug >= cMsgConstants.debugError) {
                System.out.println("no valid ports specified");
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
                        System.out.println("wrong # of ports defining range");
                    }
                    continue;
                }

                try {
                    port1 = Integer.parseInt(ports[0]);
                    port2 = Integer.parseInt(ports[1]);
                    if (port1 < 1024 || port1 > 65535 ||
                        port2 < 1024 || port2 > 65535) {
                        if (debug >= cMsgConstants.debugError) {
                            System.out.println("ports must be > 1023 and < 65536");
                        }
                        continue;
                    }
                }
                catch (NumberFormatException e) {
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("bad port format in range");
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
                        System.out.println("adding port " + k);
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
                        System.out.println("bad port format");
                    }
                    continue;
                }

                if (port < 1024 || port > 65535) {
                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("ports must be > 1023 and < 65536");
                    }
                    continue;
                }

                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("adding port " + port);
                }
                set.add(port);
            }
        }

    }




}

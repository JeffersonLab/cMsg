/*----------------------------------------------------------------------------*
 *  Copyright (c) 2008        Southeastern Universities Research Association, *
 *                            Jefferson Science Associates                    *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C.Timmer, 23-Jul-2008, Jefferson Lab                                    *
 *                                                                            *
 *    Authors: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;

import java.util.Map;
import java.util.Set;

/**
 * This class prints out cMsg domain system monitoring information
 * obtained from a cMsg domain client.
 */
public class cMsgMonitor {

    private String  name = "monitor";
    private String  description = "java monitor";
    private String  UDL = "cMsg://localhost/cMsg/myNameSpace";
    private boolean debug;


    /** Constructor. */
    cMsgMonitor(String[] args) {
        decodeCommandLine(args);
    }


    /**
     * Method to decode the command line used to start this application.
     * @param args command line arguments
     */
    private void decodeCommandLine(String[] args) {

        // loop over all args
        for (int i = 0; i < args.length; i++) {

            if (args[i].equalsIgnoreCase("-h")) {
                usage();
                System.exit(-1);
            }
            else if (args[i].equalsIgnoreCase("-n")) {
                name = args[i + 1];
                i++;
            }
            else if (args[i].equalsIgnoreCase("-d")) {
                description = args[i + 1];
                i++;
            }
            else if (args[i].equalsIgnoreCase("-u")) {
                UDL= args[i + 1];
                i++;
            }
            else if (args[i].equalsIgnoreCase("-debug")) {
                debug = true;
            }
            else {
                usage();
                System.exit(-1);
            }
        }

        return;
    }


    /** Method to print out correct program command line usage. */
    private static void usage() {
        System.out.println("\nUsage:\n\n" +
            "   java cMsgMonitor\n" +
            "        [-n <name>]          set client name\n"+
            "        [-d <description>]   set description of client\n" +
            "        [-u <UDL>]           set UDL to connect to cMsg\n" +
            "        [-debug]             turn on printout\n" +
            "        [-h]                 print this help\n");
    }


    /** Run as a stand-alone application. */
    public static void main(String[] args) {
        try {
            cMsgMonitor mon = new cMsgMonitor(args);
            mon.run();
        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }
    }


    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {

        if (debug) {
            System.out.println("Running cMsg monitor");
        }

        // connect to cMsg server
        cMsg coda = new cMsg(UDL, name, description);
        coda.connect();

        long period = 3000; // 3 seconds

        cMsgMessage msg;

        while (true) {
            msg = coda.monitor(null);
            if (msg == null) continue;
//System.out.println("GOT MESSAGE: has payload = " + msg.hasPayload());
            Map<String, cMsgPayloadItem> map = msg.getPayloadItems();
            Set<Map.Entry<String, cMsgPayloadItem>> set = map.entrySet();
            for (Map.Entry<String, cMsgPayloadItem> entry : set) {
                System.out.println("Key = " + entry.getKey() + ", Val = " + entry.getValue());
            }

            cMsgPayloadItem item = msg.getPayloadItem("IpAddresses");
            if (item != null) {
                System.out.println("\nPlatform is on:");
                String[] ips = item.getStringArray();
                for (String ip : ips) {
                    System.out.println("  " + ip);
                }
            }
//            else {
//                System.out.println("\nPlatform is on unknown host");
//            }

            System.out.println("monitor message:\n" + msg.getText());

            System.out.println("********************************\n");
            // wait
            try { Thread.sleep(period); }
            catch (InterruptedException e) {}
        }
    }
}

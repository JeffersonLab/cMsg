/*----------------------------------------------------------------------------*
 *  Copyright (c) 2008        Southeastern Universities Research Association, *
 *                            Jefferson Science Associates                    *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C.Timmer, 6-Nov-2006, Jefferson Lab                                     *
 *                                                                            *
 *    Authors: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgShutdownHandlerInterface;

/**
 * This is an example class which creates a cMsg client that shuts down
 * other specified cMsg clients (possibly including itself).
 * In the cMsg domain the name of clients to be shutdown implements a
 * simple wildcard matching scheme where "*" means any or no characters,
 * "?" means exactly 1 character, and "#" means 1 or no positive integer.
 */
public class cMsgShutdowner {

    private String  name = "shutdowner";
    private String  description = "java shutdowner";
    private String  UDL = "cMsg://localhost/cMsg/myNameSpace";
    private String  client = "defaultClientNameHere";
    private boolean debug, shutMeDown;


    /** Constructor. */
    cMsgShutdowner(String[] args) {
        decodeCommandLine(args);
    }


    /**
     * Method to decode the command line used to start this application.
     * @param args command line arguments
     */
    public void decodeCommandLine(String[] args) {

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
            else if (args[i].equalsIgnoreCase("-c")) {
                client = args[i + 1];
                i++;
            }
            else if (args[i].equalsIgnoreCase("-me")) {
                shutMeDown = true;
            }
            else if (args[i].equalsIgnoreCase("-debug")) {
                debug = true;
            }
            else {
                usage();
                System.exit(-1);
            }
        }
    }


    /** Method to print out correct program command line usage. */
    private static void usage() {
        System.out.println("\nUsage:\n\n" +
                "   java cMsgShutdowner\n" +
                "        [-n <name>]          set client name\n"+
                "        [-d <description>]   set description of client\n" +
                "        [-u <UDL>]           set UDL to connect to cMsg\n" +
                "        [-c <client name>]   name of client(s) to shutdown (in cMsg domain, wildcards allowed where\n" +
                "                             * matches everything, ? matches 1 char, # matches 1 or 0 pos int)\n" +
                "        [-me]                allow this cmsg client to be shutdown\n" +
                "        [-debug]             turn on printout\n" +
                "        [-h]                 print this help\n");
    }


    /**
     * Run as a stand-alone application.
     * @param args arguments.
     */
    public static void main(String[] args) {
        try {
            cMsgShutdowner shutdowner = new cMsgShutdowner(args);
            shutdowner.run();
        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }
    }

    class myShutdownHandler implements cMsgShutdownHandlerInterface {
        public void handleShutdown() {
            System.out.println("RUNNING SHUTDOWN HANDLER");
            System.exit(-1);
        }
    }


    /**
     * Run as a stand-alone application.
     * @throws cMsgException if error.
     */
    public void run() throws cMsgException {
        if (debug) {
            System.out.println("Running cMsg shutdowner");
        }

        // connect to cMsg server
        cMsg coda = new cMsg(UDL, name, description);
        coda.connect();

        if (debug) {
            System.out.println("Shutting down " + client);
        }

        // add shutdown handler
        coda.setShutdownHandler(new myShutdownHandler());

        // wait 2 seconds
        try {
            if (debug) System.out.println("Wait 3 seconds, then shutdown " + client);
            Thread.sleep(3000);
        }
        catch (InterruptedException e) {}

        // shutdown specified client
        coda.shutdownClients(client, shutMeDown);

        try {
            if (debug) System.out.println("Wait 3 more seconds, then disconnect");
            Thread.sleep(3000);
        }
        catch (InterruptedException e) {}

        coda.disconnect();
    }

}

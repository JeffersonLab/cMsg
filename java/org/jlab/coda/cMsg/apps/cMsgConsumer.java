/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer,       2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.cMsgCallbackInterface;
import org.jlab.coda.cMsg.cMsgCallbackAdapter;


/**
 * This is an example class which creates a cMsg message consumer.
 */
public class cMsgConsumer {

    private String  subject = "SUBJECT";
    private String  type = "TYPE";
    private String  name = "consumer";
    private String  description = "java consumer";
    private String  UDL = "cMsg://localhost/cMsg/myNameSpace";
    //private String  UDL = "cMsg://multicast/cMsg/myNameSpace";
    private boolean debug;
    private long    count;


    /** Constructor. */
    cMsgConsumer(String[] args) {
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
            else if (args[i].equalsIgnoreCase("-s")) {
                subject = args[i + 1];
                i++;
            }
            else if (args[i].equalsIgnoreCase("-t")) {
                type= args[i + 1];
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
                "   java cMsgConsumer\n" +
                "        [-n <name>]          set client name\n"+
                "        [-d <description>]   set description of client\n" +
                "        [-u <UDL>]           set UDL to connect to cMsg\n" +
                "        [-s <subject>]       set subject of sent messages\n" +
                "        [-t <type>]          set type of sent messages\n" +
                "        [-debug]             turn on printout\n" +
                "        [-h]                 print this help\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            cMsgConsumer consumer = new cMsgConsumer(args);
            consumer.run();
        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }
    }


    /**
     * Method to convert a double to a string with a specified number of decimal places.
     *
     * @param d double to convert to a string
     * @param places number of decimal places
     * @return string representation of the double
     */
    private static String doubleToString(double d, int places) {
        if (places < 0) places = 0;

        double factor = Math.pow(10,places);
        String s = "" + (double) (Math.round(d * factor)) / factor;

        if (places == 0) {
            return s.substring(0, s.length()-2);
        }

        while (s.length() - s.indexOf(".") < places+1) {
            s += "0";
        }

        return s;
    }


    /**
     * This class defines the callback to be run when a message matching
     * our subscription arrives.
     */
    class myCallback extends cMsgCallbackAdapter {
        /**
         * Callback method definition.
         *
         * @param msg message received from domain server
         * @param userObject object passed as an argument which was set when the
         *                   client originally subscribed to a subject and type of
         *                   message.
         */
        public void callback(cMsgMessage msg, Object userObject) {
            // keep track of how many messages we receive
            count++;
            /*
            System.out.println("Received msg has subject = " + msg.getSubject());
            System.out.println("                    type = " + msg.getType());
            if (msg.hasPayload()) {
                System.out.println("                 payload = ");
                msg.payloadPrintout(0);
            }

            System.out.println("\n\nMESSAGE IN XML:\n" + msg);
            */
        }

        // Define behavior of callback by overriding methods of cMsgCallbackAdapter
        /*
        // Maximum number of unprocessed messages kept locally
        // before things "back up" (potentially slowing or stopping
        // senders of messages of this subject and type). Default = 1000.
        public int getMaximumCueSize() {return 10000;}

        // Oldest messages may be skipped if queue is full. Default = false.
        public boolean maySkipMessages() {return true;}

        // Messages to this callback must be processed by a single thread and in order.
        // Default = true;
        public boolean mustSerializeMessages() {return false;}

        // Allow up to 20 threads to process messages to this callback if the method
        // mustSerializeMessages returns false. Default = 100.
        public int getMaximumThreads() {return 20;}
        */
     }


    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {

        if (debug) {
            System.out.println("Running cMsg consumer subscribed to:\n" +
                               "    subject = " + subject +
                               "\n    type    = " + type);
        }

        // connect to cMsg server
        cMsg coda = new cMsg(UDL, name, description);
        coda.connect();

        // enable message reception
        coda.start();

        // subscribe to subject/type
        cMsgCallbackInterface cb = new myCallback();
        Object unsub  = coda.subscribe(subject, type, cb, null);

        // variables to track incoming message rate
        double freq, freqAvg;
        long   totalT=0, totalC=0, period = 5000; // millisec

        while (true) {
            count = 0;

            // wait
            try { Thread.sleep(period); }
            catch (InterruptedException e) {}

            freq    = (double)count/period*1000;
            totalT += period;
            totalC += count;
            freqAvg = (double)totalC/totalT*1000;

            if (debug) {
                System.out.println("count = " + count + ", " +
                                   doubleToString(freq, 1) + " Hz, Avg = " +
                                   doubleToString(freqAvg, 1) + " Hz");
            }

            if (!coda.isConnected()) {
                // Allow 2 seconds for failover to new cMsg server (in cMsg domain)
                // before declaring us dead-in-the-water.
                try { Thread.sleep(2000); }
                catch (InterruptedException e) {}

                // if still not connected, quit
                if (!coda.isConnected()) {
                    System.out.println("No longer connected to domain server, quitting");
                    System.exit(-1);
                }
            }
        }
    }
}

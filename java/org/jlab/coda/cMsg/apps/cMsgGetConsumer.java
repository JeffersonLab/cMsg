/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer,        2005, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;

import java.util.Arrays;
import java.util.concurrent.TimeoutException;

/**
 * This is an example class which creates a cMsg client that synchronously either
 * sends and receives messages by calling {@link cMsg#sendAndGet} or subscribes to a
 * subject & type and receives a response by calling {@link cMsg#subscribeAndGet}.
 * To receive a response to a sendAndGet's send, the {@link cMsgGetResponder} must also
 * be running.
 */
public class cMsgGetConsumer {

    private String  subject = "SUBJECT";
    private String  type = "TYPE";
    private String  name = "getConsumer";
    private String  description = "java getConsumer";
    private String  UDL = "cMsg://localhost/cMsg/myNameSpace";

    private String  text = "TEXT";
    private char[]  textChars;
    private int     textSize;

    private int     delay, count = 5000, timeout = 3000; // 3 second default timeout
    private boolean send, debug;


    /** Constructor. */
    cMsgGetConsumer(String[] args) {
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
            else if (args[i].equalsIgnoreCase("-c")) {
                count = Integer.parseInt(args[i + 1]);
                if (count < 1)
                    System.exit(-1);
                i++;
            }
            else if (args[i].equalsIgnoreCase("-text")) {
                text = args[i + 1];
                i++;
            }
            else if (args[i].equalsIgnoreCase("-textsize")) {
                textSize  = Integer.parseInt(args[i + 1]);
                textChars = new char[textSize];
                Arrays.fill(textChars, 'A');
                text = new String(textChars);
                System.out.println("text len = " + text.length());
                i++;
            }
            else if (args[i].equalsIgnoreCase("-to")) {
                timeout = Integer.parseInt(args[i + 1]);
                i++;
            }
            else if (args[i].equalsIgnoreCase("-delay")) {
                delay = Integer.parseInt(args[i + 1]);
                i++;
            }
            else if (args[i].equalsIgnoreCase("-debug")) {
                debug = true;
            }
            else if (args[i].equalsIgnoreCase("-send")) {
                send = true;
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
            "   java cMsgGetConsumer\n" +
            "        [-n <name>]          set client name\n"+
            "        [-d <description>]   set description of client\n" +
            "        [-u <UDL>]           set UDL to connect to cMsg\n" +
            "        [-s <subject>]       set subject of subscription/sent messages\n" +
            "        [-t <type>]          set type of subscription/sent messages\n" +
            "        [-c <count>]         set # of messages to get before printing output\n" +
            "        [-text <text>]       set text of sent messages\n" +
            "        [-textsize <size>]   set text to 'size' number of ASCII chars (bytes)\n" +
            "        [-to <time>]         set timeout\n" +
            "        [-delay <time>]      set time in millisec between sending of each message\n" +
            "        [-debug]             turn on printout\n" +
            "        [-send]              use sendAndGet instead of subscribeAndGet\n" +
            "        [-h]                 print this help\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            cMsgGetConsumer getConsumer = new cMsgGetConsumer(args);
            getConsumer.run();
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
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {

        if (debug) {
            System.out.println("Running cMsg getConsumer sending or subscribed to:\n" +
                               "    subject = " + subject +
                               "\n    type    = " + type);
        }

        // connect to cMsg server
        cMsg coda = new cMsg(UDL, name, description);
        coda.connect();

        // enable message reception
        coda.start();

        // create a message to send to a responder (for sendAndGet only)
        cMsgMessage msg;
        cMsgMessage sendMsg = new cMsgMessage();
        sendMsg.setSubject(subject);
        sendMsg.setType(type);
        sendMsg.setText(text);

        // variables to track incoming message rate
        double freq, freqAvg;
        long   t1, t2, deltaT, totalT=0, totalC=0;

        while (true) {

            t1 = System.currentTimeMillis();

            // do some gets
            for (int i=0; i < count; i++) {
                try {
                    if (send) {
                        msg = coda.sendAndGet(sendMsg, timeout);
                    }
                    else {
                        msg = coda.subscribeAndGet(subject, type, timeout);
                    }
                }
                catch (TimeoutException e) {
                    if (send) {
                        System.out.println("Timeout in sendAndGet");
                    }
                    else {
                        System.out.println("Timeout in subscribeAndGet");
                    }
                    continue;
                }

                if (msg == null) {
                    System.out.println("Got null message");
                }
//                else {
//                    System.out.println("Got message");
//                }

                // delay between messages sent
                if (delay != 0) {
                    try {Thread.sleep(delay);}
                    catch (InterruptedException e) {}
                }
            }

            t2 = System.currentTimeMillis();

            deltaT  = t2 - t1; // millisec
            freq    = (double)count/deltaT*1000;
            totalT += deltaT;
            totalC += count;
            freqAvg = (double)totalC/totalT*1000;

            if (debug) {
                System.out.println("count = " + count + ", " +
                                   doubleToString(freq, 0) + " Hz, Avg = " +
                                   doubleToString(freqAvg, 0) + " Hz");
            }

            if (!coda.isConnected()) {
                System.out.println("No longer connected to domain server, quitting");
                System.exit(-1);
            }
        }
    }
}

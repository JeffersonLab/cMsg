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
import org.jlab.coda.cMsg.cMsgCallbackAdapter;

import java.util.Arrays;

/**
 * This is an example class which creates a cMsg message producer.
 */
public class cMsgProducer {

    private String  subject = "SUBJECT";
    private String  type = "TYPE";
    private String  name = "producer";
    private String  description = "java producer";
   // private String  UDL = "cMsg://localhost/cMsg/myNameSpace";
    private String  UDL = "cMsg://multicast/cMsg/myNameSpace";

    private String  text;
    private char[]  textChars;
    private int     textSize;
    private boolean sendText;

    private byte[]  binArray;
    private int     binSize;
    private boolean sendBinary;

    private int     delay, count = 50000;
    private boolean debug, useSyncSend;


    /**
     * Constructor.
     * @param args arguments.
     */
    cMsgProducer(String[] args) {
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
            else if (args[i].equalsIgnoreCase("-ss")) {
                useSyncSend = true;
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
                type = args[i + 1];
                i++;
            }
            else if (args[i].equalsIgnoreCase("-text")) {
                text = args[i + 1];
                sendText = true;
                i++;
            }
            else if (args[i].equalsIgnoreCase("-textsize")) {
                textSize  = Integer.parseInt(args[i + 1]);
                textChars = new char[textSize];
                Arrays.fill(textChars, 'A');
                text = new String(textChars);
                System.out.println("text len = " + text.length());
                sendText = true;
                i++;
            }
            else if (args[i].equalsIgnoreCase("-binsize")) {
                binSize  = Integer.parseInt(args[i + 1]);
                binArray = new byte[binSize];
                for (int j=0; j < binSize; j++) {
                  binArray[j] = (byte)(j%255);
                }
                System.out.println("binary size = " + binSize);
                sendBinary = true;
                i++;
            }
            else if (args[i].equalsIgnoreCase("-c")) {
                count = Integer.parseInt(args[i + 1]);
                if (count < 1)
                    System.exit(-1);
                i++;
            }
            else if (args[i].equalsIgnoreCase("-delay")) {
                delay = Integer.parseInt(args[i + 1]);
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
    }


    /** Method to print out correct program command line usage. */
    private static void usage() {
        System.out.println("\nUsage:\n\n" +
            "   java cMsgProducer\n" +
            "        [-n <name>]          set client name\n"+
            "        [-d <description>]   set description of client\n" +
            "        [-u <UDL>]           set UDL to connect to cMsg\n" +
            "        [-s <subject>]       set subject of sent messages\n" +
            "        [-t <type>]          set type of sent messages\n" +
            "        [-c <count>]         set # of messages to send before printing output\n" +
            "        [-text <text>]       set text of sent messages\n" +
            "        [-textsize <size>]   set text to 'size' number of ASCII chars (bytes)\n" +
            "        [-binsize <size>]    set binary array to 'size' number of bytes\n" +
            "        [-delay <time>]      set time in millisec between sending of each message\n" +
            "        [-debug]             turn on printout\n" +
            "        [-ss]                use syncSend instead of send\n" +
            "        [-h]                 print this help\n");
    }


    /**
     * Run as a stand-alone application.
     * @param args arguments.
     */
    public static void main(String[] args) {
        try {
            cMsgProducer producer = new cMsgProducer(args);
            producer.run();
        }
        catch (cMsgException e) {
            System.out.println(e.toString());
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
        public void callback(cMsgMessage msg, Object userObject) {
            // keep track of how many messages we receive
            //count++;

            System.out.println("Received msg: ");
            System.out.println(msg.toString(true, false, true));
        }
     }



    /**
     * This method is executed as a thread.
     * @throws cMsgException if error.
     */
    public void run() throws cMsgException {

        if (debug) {
            System.out.println("Running cMsg producer sending to:\n" +
                                 "    subject = " + subject +
                               "\n    type    = " + type +
                               "\n    UDL     = " + UDL);
        }

        // connect to cMsg server
        cMsg coda = new cMsg(UDL, name, description);
        try {
            coda.connect();
        }
        catch (cMsgException e) {
            e.printStackTrace();
            return;
        }

        System.out.println("We are connected to host " + coda.getServerHost() +
                                   " on port " + coda.getServerPort());

        // enable message reception
        //cMsgCallbackInterface cb = new myCallback();
        //cMsgSubscriptionHandle handle  = coda.subscribe(subject, type, cb, null);
        //coda.start();

        // create a message
        cMsgMessage msg = new cMsgMessage();
        msg.setSubject(subject);
        msg.setType(type);
        msg.setHistoryLengthMax(0);

        if (sendText) {
          msg.setText(text);
        }
        if (sendBinary) {
          msg.setByteArrayNoCopy(binArray);
        }

        // send using UDP instead of TCP
        // msg.getContext().setReliableSend(false);

//        // Add 2 payload items
//        String[] ses = new String[]{"one", "two", "three"};
//        cMsgPayloadItem item1 = new cMsgPayloadItem("myStringArray", ses);
//        msg.addPayloadItem(item1);
//
//        int j = 123456789;
//        cMsgPayloadItem item2 = new cMsgPayloadItem("myInt", j);
//        msg.addPayloadItem(item2);
//

        // variables to track message rate
        double freq=0., freqAvg=0.;
        long t1, t2, deltaT, totalT=0, totalC=0;

        // Ignore the first N values found for freq in order
        // to get better avg statistics. Since the JIT compiler in java
        // takes some time to analyze & compile code, freq may initially be low.
        long ignore=0;
        
        while (true) {
            t1 = System.currentTimeMillis();
            for (int i = 0; i < count; i++) {
                if (useSyncSend) {
                    int j = coda.syncSend(msg, 1000);
                    //System.out.println("Got syncSend val = " + j);
                }
                else {
                    msg.setUserInt(i);
                    coda.send(msg);
                }
                coda.flush(0);

                // delay between messages sent
                if (delay != 0) {
                    try {Thread.sleep(delay);}
                    catch (InterruptedException e) {}
                }
            }
            t2 = System.currentTimeMillis();

            if (ignore == 0) {
                deltaT = t2 - t1; // millisec
                freq = (double) count / deltaT * 1000;
                totalT += deltaT;
                totalC += count;
                freqAvg = (double) totalC / totalT * 1000;

                if (debug) {
                    System.out.println(doubleToString(freq, 1) + " Hz, Avg = " +
                                       doubleToString(freqAvg, 1) + " Hz");
                }
            }
            else {
                ignore--;
            }
        }
    }

}

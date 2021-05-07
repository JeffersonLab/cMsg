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
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;

import java.math.BigInteger;

/**
 * An example class which creates a cMsg message producer whose messges
 * contain many and varied payload items.
 */
public class cMsgPayloadProducer {

    private String  subject = "SUBJECT";
    private String  type = "TYPE";
    private String  name = "producer";
    private String  description = "java producer";
    private String  UDL = "cMsg://localhost/cMsg/myNameSpace";
    //private String  UDL = "cMsg://multicast/cMsg/myNameSpace";

    private int delay, count = 50000;
    private boolean debug;


    /**
     * Constructor.
     * @param args arguments.
     */
    cMsgPayloadProducer(String[] args) {
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
                type = args[i + 1];
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
            "   java cMsgPayloadProducer\n" +
            "        [-n <name>]          set client name\n"+
            "        [-d <description>]   set description of client\n" +
            "        [-u <UDL>]           set UDL to connect to cMsg\n" +
            "        [-s <subject>]       set subject of sent messages\n" +
            "        [-t <type>]          set type of sent messages\n" +
            "        [-delay <time>]      set time in millisec between sending of each message\n" +
            "        [-debug]             turn on printout\n" +
            "        [-h]                 print this help\n");
    }



    /**
     * Run as a stand-alone application.
     * @param args arguments.
     */
    public static void main(String[] args) {
        try {
            cMsgPayloadProducer producer = new cMsgPayloadProducer(args);
            producer.run();
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
     * @throws cMsgException if error.
     */
    public void run() throws cMsgException {

        if (debug) {
            System.out.println("Running cMsg producer sending to:\n" +
                               "    subject = " + subject +
                               "\n    type    = " + type);
        }

        // connect to cMsg server
        cMsg coda = new cMsg(UDL, name, description);
        coda.connect();

        // Create a message
        cMsgMessage msg = new cMsgMessage();
        msg.setSubject(subject);
        msg.setType(type);

        // Get rid of sender history stored in msg by calling:
        // msg.setHistoryLengthMax(0);

        // Set for using UDP to send call:
        // msg.getContext().setReliableSend(false);

        //----------------------------------------
        // Add different types of items to payload
        //----------------------------------------

        // Integers
        String[] ses = new String[]{"one", "two", "three"};
        cMsgPayloadItem item1 = new cMsgPayloadItem("STR_ARRAY", ses);
        msg.addPayloadItem(item1);

        cMsgPayloadItem item2 = new cMsgPayloadItem("STR", "hey you");
        msg.addPayloadItem(item2);

        int ii = 123456789;
        cMsgPayloadItem item3 = new cMsgPayloadItem("INT", ii);
        msg.addPayloadItem(item3);

        int[] ia = {1,2,3};
        cMsgPayloadItem item4 = new cMsgPayloadItem("INT_ARRAY", ia);
        msg.addPayloadItem(item4);

        byte bt = 123;
        cMsgPayloadItem item5 = new cMsgPayloadItem("BYTE", bt);
        msg.addPayloadItem(item5);

        byte[] ba = {1,2,3};
        cMsgPayloadItem item6 = new cMsgPayloadItem("BYTE_ARRAY", ba);
        msg.addPayloadItem(item6);

        short st = 12345;
        cMsgPayloadItem item7 = new cMsgPayloadItem("SHORT", st);
        msg.addPayloadItem(item7);

        short[] sa = {1,2,3};
        cMsgPayloadItem item8 = new cMsgPayloadItem("SHORT_ARRAY", sa);
        msg.addPayloadItem(item8);

        long lt = 123456789123456789L;
        cMsgPayloadItem item9 = new cMsgPayloadItem("LONG", lt);
        msg.addPayloadItem(item9);

        long[] la = {1,2,3};
        cMsgPayloadItem item10 = new cMsgPayloadItem("LONG_ARRAY", la);
        msg.addPayloadItem(item10);


        // Send byte array as binary
        cMsgPayloadItem item11 = new cMsgPayloadItem("BIN", ba, cMsgConstants.endianLocal);
        msg.addPayloadItem(item11);


        // Test zero compression
        long[] lb = new long[30];
        lb[0] = 1; lb[15] = 2; lb[29] = 3;
        cMsgPayloadItem item20 = new cMsgPayloadItem("LONG_ARRAY_ZERO", lb);
        msg.addPayloadItem(item20);

        int[] ib = new int[30];
        ib[0] = 1; ib[15] = 2; ib[29] = 3;
        cMsgPayloadItem item21 = new cMsgPayloadItem("INT_ARRAY_ZERO", ib);
        msg.addPayloadItem(item21);

        short[] sb = new short[30];
        sb[0] = 1; sb[15] = 2; sb[29] = 3;
        cMsgPayloadItem item22 = new cMsgPayloadItem("SHORT_ARRAY_ZERO", sb);
        msg.addPayloadItem(item22);

        byte[] bb = new byte[30];
        bb[0] = 1; bb[15] = 2; bb[29] = 3;
        cMsgPayloadItem item23 = new cMsgPayloadItem("BYTE_ARRAY_ZERO", bb);
        msg.addPayloadItem(item23);


        // BigInteger class
        BigInteger bi = new BigInteger("18446744073709551614");
        cMsgPayloadItem item30 = new cMsgPayloadItem("BIGINT", bi);
        msg.addPayloadItem(item30);

        BigInteger[] big = new BigInteger[10];
        big[0] = BigInteger.ONE;
        big[1] = big[2] = big[3] = big[4] = BigInteger.ZERO;
        big[5] = BigInteger.TEN;
        big[6] = big[7] = big[8] = BigInteger.ZERO;
        big[9] = BigInteger.ONE;
        cMsgPayloadItem item31 = new cMsgPayloadItem("BIGINT_ARRAY", big);
        msg.addPayloadItem(item31);


        // Real Numbers
        float f = 12345.12345f;
        cMsgPayloadItem item40 = new cMsgPayloadItem("FLT", f);
        msg.addPayloadItem(item40);

        float[] ff = {1.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.f, 3.f};
        cMsgPayloadItem item41 = new cMsgPayloadItem("FLT_ARRAY", ff);
        msg.addPayloadItem(item41);

        double d = 123456789.123456789;
        cMsgPayloadItem item42 = new cMsgPayloadItem("DBL", d);
        msg.addPayloadItem(item42);

        double[] dd = {1., 0., 0., 2., 0., 0., 0., 3.};
        cMsgPayloadItem item43 = new cMsgPayloadItem("DBL_ARRAY", dd);
        msg.addPayloadItem(item43);

        //-------------------------------
        // cMsg messages as payload items
        //-------------------------------

        // Define array of cMsg messages
        cMsgMessage[] ma = new cMsgMessage[2];

        // In first message of array ...
        ma[0] = new cMsgMessage();
        ma[0].setSubject("sub1");
        ma[0].setType("type1");

        cMsgPayloadItem item50 = new cMsgPayloadItem("DBL", d);
        ma[0].addPayloadItem(item50);

        // In second message of array ...
        ma[1] = new cMsgMessage();
        ma[1].setSubject("sub2");
        ma[1].setType("type2");

        cMsgPayloadItem item51 = new cMsgPayloadItem("INT", ii);
        ma[1].addPayloadItem(item51);

        // Add array of cMsg messages to original message as a payload item
        cMsgPayloadItem item52 = new cMsgPayloadItem("MSG_ARRAY", ma);
        msg.addPayloadItem(item52);

        //-------------------------------
        // End of payload
        //-------------------------------


        // variables to track message rate
        double freq, freqAvg;
        long t1, t2, deltaT, totalT=0, totalC=0, ignore=0;

        while (true) {
            t1 = System.currentTimeMillis();
            for (int i = 0; i < count; i++) {
                coda.send(msg);
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


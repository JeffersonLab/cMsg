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

import java.util.Arrays;

/**
 * An example class which creates a cMsg message producer.
 */
public class payloadTest {
    String  name = "producer";
    String  description = "java producer";
    String  UDL = "cMsg:cMsg://localhost:3456/cMsg/test";
    //String  UDL = "cMsg:cmsg://broadcast/cMsg/test";
    String  subject = "SUBJECT";
    String  type = "TYPE";

    String  text;
    char[]  textChars;
    int     textSize;
    boolean sendText;

    byte[]  binArray;
    int     binSize;
    boolean sendBinary;

    int     delay;
    boolean debug;


    /** Constructor. */
    payloadTest(String[] args) {
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

        return;
    }


    /** Method to print out correct program command line usage. */
    private static void usage() {
        System.out.println("\nUsage:\n\n" +
            "   java payloadTest [-n name] [-d description] [-u UDL]\n" +
            "                     [-s subject] [-t type] [-text text]\n" +
            "                     [-textsize size in bytes]\n" +
            "                     [-delay millisec] [-debug]\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            payloadTest producer = new payloadTest(args);
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
     */
    public void run() throws cMsgException {

        if (debug) {
            System.out.println("Running cMsg producer sending to:\n" +
                               "    subject = " + subject +
                               "\n    type    = " + type);
        }

        // create a message
        cMsgMessage msg = new cMsgMessage();
        msg.setSubject(subject);
        msg.setType(type);

        // play with payload

        String[] ses = new String[]{"one", "two", "three"};
        cMsgPayloadItem item = new cMsgPayloadItem("STR_ARRAY", ses);
        msg.addPayloadItem(item);
/*
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


        byte[] bia = {1,2,3};
        cMsgPayloadItem item11 = new cMsgPayloadItem("BIN", bia, cMsgConstants.endianLocal);
        msg.addPayloadItem(item11);
*/
/*
        // test zero suppression
        long[] la = new long[30];
        la[0] = 1; la[15] = 2; la[29] = 3;
        cMsgPayloadItem item10 = new cMsgPayloadItem("LONG_ARRAY_ZERO", la);
        msg.addPayloadItem(item10);

        int[] ia = new int[30];
        ia[0] = 1; ia[15] = 2; ia[29] = 3;
        cMsgPayloadItem item12 = new cMsgPayloadItem("INT_ARRAY_ZERO", ia);
        msg.addPayloadItem(item12);

        short[] sa = new short[30];
        sa[0] = 1; sa[15] = 2; sa[29] = 3;
        cMsgPayloadItem item8 = new cMsgPayloadItem("SHORT_ARRAY_ZERO", sa);
        msg.addPayloadItem(item8);

        byte[] ba = new byte[30];
        ba[0] = 1; ba[15] = 2; ba[29] = 3;
        cMsgPayloadItem item6 = new cMsgPayloadItem("BYTE_ARRAY_ZERO", ba);
        msg.addPayloadItem(item6);
*/

/*
        Integer ii = 123456789;
        cMsgPayloadItem item3 = new cMsgPayloadItem("INT", ii);
        msg.addPayloadItem(item3);

        Byte bt = 123;
        cMsgPayloadItem item5 = new cMsgPayloadItem("BYTE", bt);
        msg.addPayloadItem(item5);

        Short st = 12345;
        cMsgPayloadItem item7 = new cMsgPayloadItem("SHORT", st);
        msg.addPayloadItem(item7);

        Long lt = 123456789123456789L;
        cMsgPayloadItem item9 = new cMsgPayloadItem("LONG", lt);
        msg.addPayloadItem(item9);
        
        BigInteger bi = new BigInteger("18446744073709551614");
        cMsgPayloadItem item1 = new cMsgPayloadItem("BIGINT", bi);
        msg.addPayloadItem(item1);

        BigInteger[] big = new BigInteger[10];
        big[0] = BigInteger.ONE;
        big[1] = big[2] = big[3] = big[4] = BigInteger.ZERO;
        big[5] = BigInteger.TEN;
        big[6] = big[7] = big[8] = BigInteger.ZERO;
        big[9] = BigInteger.ONE;
        cMsgPayloadItem item2 = new cMsgPayloadItem("BIGINT_ARRAY", big);
        msg.addPayloadItem(item2);
*/

        //short[] sa = new short[8];
        //byte[] sa = new byte[8];
        //int[] sa = new int[8];
        //long[] sa = new long[8];
        //sa[0] = 1; sa[3] = 2; sa[7] = 3;
/*
        BigInteger[] sa = new BigInteger[8];
        sa[0] = BigInteger.ONE;
        sa[1] = sa[2] = BigInteger.ZERO;
        sa[3] = new BigInteger("2");
        sa[4] = sa[5] = sa[6] = BigInteger.ZERO;
        sa[7] = new BigInteger("3");
        cMsgPayloadItem item8 = new cMsgPayloadItem("ARRAY_ZERO", sa);
        msg.addPayloadItem(item8);
*/

/*
        float f = 12345.12345f;
        cMsgPayloadItem item3 = new cMsgPayloadItem("FLT", f);
        msg.addPayloadItem(item3);

        float[] ff = {1.f, 0.f, 0.f, 2.f, 0.f, 0.f, 0.f, 3.f};
        cMsgPayloadItem item4 = new cMsgPayloadItem("FLT_ARRAY", ff);
        msg.addPayloadItem(item4);

        double d = 123456789.123456789;
        cMsgPayloadItem item5 = new cMsgPayloadItem("DBL", d);
        msg.addPayloadItem(item5);

        double[] dd = {1., 0., 0., 2., 0., 0., 0., 3.};
        cMsgPayloadItem item6 = new cMsgPayloadItem("DBL_ARRAY", dd);
        msg.addPayloadItem(item6);
*/
/*
        double d = 1.23e-123;
        cMsgPayloadItem item15 = new cMsgPayloadItem("DBL", d);
        msg.addPayloadItem(item15);
*/
        // get rid of history
        //msg.setHistoryLengthMax(0);

        cMsgMessage[] ma = new cMsgMessage[2];

        // create a message as payload
        ma[0] = new cMsgMessage();
        ma[0].setSubject(subject);
        ma[0].setType(type);

        ma[1] = new cMsgMessage();
        ma[1].setSubject("subbie");
        ma[1].setType("typie");

        int j = 123456789;
        cMsgPayloadItem item1 = new cMsgPayloadItem("INT", j);
        ma[1].addPayloadItem(item1);

        cMsgPayloadItem item66 = new cMsgPayloadItem("MSG_ARRAY", ma);

        cMsgMessage[] ma2 = new cMsgMessage[2];

        // create a message as payload
        ma2[0] = new cMsgMessage();
        ma2[0].setSubject(subject);
        ma2[0].setType(type);

        ma2[1] = new cMsgMessage();
        ma2[1].setSubject("subbie");
        ma2[1].setType("typie");
        ma2[1].addPayloadItem(item66);

        cMsgPayloadItem item666 = new cMsgPayloadItem("MSG_ARRAY", ma2);
        msg.addPayloadItem(item666);

        // set for UDP send
        //msg.getContext().setReliableSend(false);
        if (sendText) {
          System.out.println("Sending text\n");
          msg.setText(text);
        }
        if (sendBinary) {
          System.out.println("Sending byte array\n");
          msg.setByteArrayNoCopy(binArray);
        }

//        System.out.println("MSG:\n" + msg.toString(true, false));
//        if (true) throw new cMsgException("hey");

        System.out.println("PAYLOAD:\n" + msg.payloadToString(true, false));
        if (true) throw new cMsgException("hey");

        // connect to cMsg server
        cMsg coda = new cMsg(UDL, name, description);
        coda.connect();

        // variables to track message rate
        double freq=0., freqAvg=0.;
        long t1, t2, deltaT, totalT=0, totalC=0, count=50000, ignore=0;

        // delay between messages
        //if (delay != 0) rcCount = rcCount/(20 + delay);

        while (true) {
            t1 = System.currentTimeMillis();
            for (int i = 0; i < count; i++) {
                coda.send(msg);
                //int a = coda.syncSend(msg);
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


/*----------------------------------------------------------------------------*
 *  Copyright (c) 2008        Jefferson Science Associates,                   *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 24-Nov-2008, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.cMsgCallbackAdapter;
      import org.jlab.coda.cMsg.common.cMsgMessageFull;

import java.util.Arrays;

/**
 * Test the payload.
 */
public class cMsgTestPayload {
    private String  subject = "SUBJECT";
    private String  type = "TYPE";
    private String  name = "payload tester";
    private String  description = "java producer";
    private String  UDL = "cMsg://localhost/cMsg/myNameSpace";

    private String  text;
    private char[]  textChars;
    private int     textSize;
    private boolean sendText;

    private byte[]  binArray;
    private int     binSize;
    private boolean sendBinary;

    private int     delay, count = 50000;
    private boolean debug, useSyncSend;

    cMsg coda;

    /** Constructor. */
    cMsgTestPayload(String[] args) {
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
                  binArray[j] = (byte)(j%256);
                  //System.out.println("bin[" + j + "] = " + binArray[j]);
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

        return;
    }


    /** Method to print out correct program command line usage. */
    private static void usage() {
        System.out.println("\nUsage:\n\n" +
            "   java cMsgTestPayload\n" +
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
     */
    public static void main(String[] args) {
        try {
            cMsgTestPayload tp = new cMsgTestPayload(args);
            tp.run();
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
            count++;

            if (msg.hasPayload()) {
                System.out.println("Received msg has payload = \n" + msg.toString());

                byte[] byt = msg.getByteArray();
                System.out.println("CB Byte Array = " + byt);
                if (byt != null) {
                    for (int kk = 0; kk < byt.length; kk++) {
                        System.out.println("bin[" + kk + "] = " + byt[kk]);
                    }
                }
                System.out.println("CB Byte Array End");


                try {
                    byte[][] myBin = msg.getPayloadItem("BIN_ARRAY").getBinaryArray();
                    for (int k=0; k<myBin.length; k++) {
                        System.out.println("bin array #" + (k+1) + ":");
                        for (int kk=0; kk<myBin[k].length; kk++) {
                            System.out.println("bin[" + kk + "] = " + myBin[k][kk]);
                        }
                    }


//                    System.out.println("\n\n");
//                    byte[] biny = msg.getPayloadItem("BINNIE").getBinary();
//                    for (int kk=0; kk<biny.length; kk++) {
//                        System.out.println("bin[" + kk + "] = " + biny[kk]);
//                    }
                }
                catch (cMsgException e) {
                    e.printStackTrace();
                }
            }

            // delay between messages sent
            if (delay != 0) {
                try {Thread.sleep(delay);}
                catch (InterruptedException e) {}
            }

//            try { coda.send(msg); }
//            catch (cMsgException e) {
//                e.printStackTrace();
//            }
        }

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

        // connect to cMsg server
        coda = new cMsg(UDL, name, description);
        coda.connect();

        // enable message reception
        coda.start();

        // subscribe to subject/type
        cMsgCallbackAdapter cb = new myCallback();
        Object unsub  = coda.subscribe(subject, type, cb, null);

        // create a message
        cMsgMessageFull msg = new cMsgMessageFull();
        //msg.setSubject("\"NEW\"\" SUB\"JECT\"");
        msg.setSubject(subject);
        msg.setType(type);
        msg.setText("Some test text");
        //msg.setText("~!@#$%^&*()_-+=,.<>?/|\\;:[]{}`");
        //msg.setText("A<![CDATA[B]]><![CDATA[C]]>D");
        msg.setHistoryLengthMax(4);

//        if (sendText) {
//          msg.setText(text);
//        }
        if (sendBinary) {
            //msg.setByteArrayNoCopy(binArray);
            msg.setByteArray(binArray, 10, 10);
        }

        // send using UDP instead of TCP
        // msg.getContext().setReliableSend(false);

        // Get rid of sender history stored in msg by calling:
        // msg.setHistoryLengthMax(0);

        //----------------------------------------
        // Add different types of items to payload
        //----------------------------------------

//        // Integers
//        String[] ses = new String[]{"one\none", "two\ntwo", "three\nthree"};
//        cMsgPayloadItem item1 = new cMsgPayloadItem("STR_ARRAY", ses);
//        msg.addPayloadItem(item1);
//
//        cMsgPayloadItem item2 = new cMsgPayloadItem("STR", "hey you");
//        msg.addPayloadItem(item2);
//
//        int ii = Integer.MAX_VALUE;
//        cMsgPayloadItem item3 = new cMsgPayloadItem("INT", ii);
//        msg.addPayloadItem(item3);
//
//        int[] ia = {0, -1, 1};
//        cMsgPayloadItem item4 = new cMsgPayloadItem("INT_ARRAY", ia);
//        msg.addPayloadItem(item4);
//
//        byte bt = Byte.MAX_VALUE;
//        cMsgPayloadItem item5 = new cMsgPayloadItem("BYTE", bt);
//        msg.addPayloadItem(item5);
//
        byte[] ba = {Byte.MIN_VALUE, -1, Byte.MAX_VALUE};
        cMsgPayloadItem item6 = new cMsgPayloadItem("BYTE_ARRAY", ba);
        msg.addPayloadItem(item6);
//
//        short st = Short.MAX_VALUE;
//        cMsgPayloadItem item7 = new cMsgPayloadItem("SHORT", st);
//        msg.addPayloadItem(item7);
//
//        short[] sa = {Short.MIN_VALUE, -1, Short.MAX_VALUE};
//        cMsgPayloadItem item8 = new cMsgPayloadItem("SHORT_ARRAY", sa);
//        msg.addPayloadItem(item8);
//
//        long lt = Long.MAX_VALUE;
//        cMsgPayloadItem item9 = new cMsgPayloadItem("LONG", lt);
//        msg.addPayloadItem(item9);
//
//        long[] la = {Long.MIN_VALUE, -1, Long.MAX_VALUE};
//        cMsgPayloadItem item10 = new cMsgPayloadItem("LONG_ARRAY", la);
//        msg.addPayloadItem(item10);
//
//
//        // Send byte array as binary
//        cMsgPayloadItem item11 = new cMsgPayloadItem("BIN", ba, cMsgConstants.endianLocal);
//        msg.addPayloadItem(item11);
//
//
//
        byte[] binnie = new byte[256];
        for (int j=-128; j < 128; j++) {
          binnie[j+128] = (byte)j;
        }
        cMsgPayloadItem item12 = new cMsgPayloadItem("BINNIE", binnie, cMsgConstants.endianLocal);
        msg.addPayloadItem(item12);


        // array of byte arrays
        byte[] binArray1 = {(byte)-1, (byte)-2, (byte)-3};
        byte[] binArray2 = {(byte)10, (byte)20, (byte)30};
        byte[] binArray3 = {(byte)-7, (byte)-8, (byte)-9};
        byte[][] bb  = new byte[3][];
        byte[][] bb2 = new byte[3][];
        bb[0]  = binArray1;
        bb[1]  = binArray2;
        bb[2]  = binArray3;
        bb2[0] = binnie;
        bb2[1] = binnie;
        bb2[2] = binnie;
        cMsgPayloadItem item13 = new cMsgPayloadItem("BIN_ARRAY", bb);
        msg.addPayloadItem(item13);

//        cMsgPayloadItem item12 = new cMsgPayloadItem("BINNIE", binArray1, cMsgConstants.endianLocal);
//        msg.addPayloadItem(item12);

//
//        // Test zero compression
//        long[] lb = new long[30];
//        lb[0] = 1; lb[15] = 2; lb[29] = 3;
//        cMsgPayloadItem item20 = new cMsgPayloadItem("LONG_ARRAY_ZERO", lb);
//        msg.addPayloadItem(item20);
//
//        int[] ib = new int[30];
//        ib[0] = 1; ib[15] = 2; ib[29] = 3;
//        cMsgPayloadItem item21 = new cMsgPayloadItem("INT_ARRAY_ZERO", ib);
//        msg.addPayloadItem(item21);
//
//        short[] sb = new short[30];
//        sb[0] = 1; sb[15] = 2; sb[29] = 3;
//        cMsgPayloadItem item22 = new cMsgPayloadItem("SHORT_ARRAY_ZERO", sb);
//        msg.addPayloadItem(item22);
//
//        byte[] bb = new byte[30];
//        bb[0] = 1; bb[15] = 2; bb[29] = 3;
//        cMsgPayloadItem item23 = new cMsgPayloadItem("BYTE_ARRAY_ZERO", bb);
//        msg.addPayloadItem(item23);
//
//
//        // BigInteger class
//        BigInteger bi = new BigInteger("18446744073709551614");
//        cMsgPayloadItem item30 = new cMsgPayloadItem("BIGINT", bi);
//        msg.addPayloadItem(item30);
//
//        BigInteger[] big = new BigInteger[10];
//        big[0] = BigInteger.ONE;
//        big[1] = big[2] = big[3] = big[4] = BigInteger.ZERO;
//        big[5] = BigInteger.TEN;
//        big[6] = big[7] = big[8] = BigInteger.ZERO;
//        big[9] = BigInteger.ONE;
//        cMsgPayloadItem item31 = new cMsgPayloadItem("BIGINT_ARRAY", big);
//        msg.addPayloadItem(item31);
//
//
//        // Real Numbers
//        float f = 12345.12345f;
//        cMsgPayloadItem item40 = new cMsgPayloadItem("FLT", f);
//        msg.addPayloadItem(item40);
//
//        float[] ff = {Float.MIN_VALUE, 0.f, 0.f, -1.f, 0.f, 0.f, 0.f, Float.MAX_VALUE};
//        cMsgPayloadItem item41 = new cMsgPayloadItem("FLT_ARRAY", ff);
//        msg.addPayloadItem(item41);
//
//        double d = 123456789.123456789;
//        cMsgPayloadItem item42 = new cMsgPayloadItem("DBL", d);
//        msg.addPayloadItem(item42);
//
//        double[] dd = {Double.MIN_VALUE, 0., 0., -1., 0., 0., 0., Double.MAX_VALUE};
//        cMsgPayloadItem item43 = new cMsgPayloadItem("DBL_ARRAY", dd);
//        msg.addPayloadItem(item43);
//
        //-------------------------------
        // cMsg messages as payload items
        //-------------------------------
        // Define array of cMsg messages
//        cMsgMessage[] ma = new cMsgMessage[2];
//
//        cMsgPayloadItem item14 = new cMsgPayloadItem("SUB_BIN_ARRAY", bb2);
//        // In first message of array ...
//        ma[0] = new cMsgMessage();
//        ma[0].setSubject("sub1");
//        ma[0].setType("type1");
//        ma[0].addPayloadItem(item14);
//        ma[0].setByteArrayNoCopy(binnie, 0, 58);
//
//
//        // In second message of array ...
//        ma[1] = new cMsgMessage();
//        ma[1].setSubject("sub2");
//        ma[1].setType("type2");
//        ma[1].addPayloadItem(item12);
//        ma[1].setByteArrayNoCopy(binnie, 0, 58);
//
//        cMsgPayloadItem item50 = new cMsgPayloadItem("MSG", ma[0]);
//        msg.addPayloadItem(item50);
//
//        cMsgPayloadItem item3 = new cMsgPayloadItem("INT_0", 0);
//        ma[0].addPayloadItem(item3);
//        cMsgPayloadItem item4 = new cMsgPayloadItem("INT_1", 1);
//        ma[1].addPayloadItem(item4);
//
////        cMsgPayloadItem item51 = new cMsgPayloadItem("INT", ii);
////        ma[1].addPayloadItem(item51);
//
//        // Add array of cMsg messages to original message as a payload item
//        cMsgPayloadItem item52 = new cMsgPayloadItem("MSG_ARRAY", ma);
//        msg.addPayloadItem(item52);
//        msg.setByteArrayNoCopy(binnie, 0, 58);
//

        
//        // What happens when you change elements in payload item array outside of payload item
//        //Integer[] ia = {0, -1, 1};
//        int[] ia = {0, -1, 1};
//        cMsgPayloadItem item4 = new cMsgPayloadItem("INT_ARRAY", ia);
//        msg.addPayloadItem(item4);
//
//        cMsgMessage msgNew = (cMsgMessage)msg.clone();
//        cMsgPayloadItem itty =  msgNew.getPayloadItem("INT_ARRAY");
//        int[] intA = itty.getIntArray();
//        System.out.println("intA[0] = " + intA[0]);
//        System.out.println("intA[1] = " + intA[1]);
//        System.out.println("intA[2] = " + intA[2]);
//        System.out.println("Now change first element in original msg to -999");
//        ia[0] = -999;
//        System.out.println("intA[0] = " + intA[0]);
//        System.out.println("Now change first element in retrieved msg to -999 and retrieve again");
//        intA[0] = -999;
//        intA = itty.getIntArray();
//        System.out.println("intA[0] = " + intA[0]);
//        System.out.println("intA[1] = " + intA[1]);
//        System.out.println("intA[2] = " + intA[2]);
//
//        Double[] ia = {0., -1., 1.};
//        cMsgPayloadItem item4 = new cMsgPayloadItem("DBL_ARRAY", ia);
//        msg.addPayloadItem(item4);
//
//        cMsgMessage msgNew = (cMsgMessage)msg.clone();
//        cMsgPayloadItem itty =  msgNew.getPayloadItem("DBL_ARRAY");
//        double[] intA = itty.getDoubleArray();
//        System.out.println("dblA[0] = " + intA[0]);
//        System.out.println("dblA[1] = " + intA[1]);
//        System.out.println("dblA[2] = " + intA[2]);
//        System.out.println("Now change first element in original msg to -999.");
//        ia[0] = -999.;
//        System.out.println("dblA[0] = " + intA[0]);

        //-------------------------------
        // Send msg to our cb
        //-------------------------------
        coda.send(msg);

        //-------------------------------
        // End of payload
        //-------------------------------

        String XML = msg.toString();
        byte[] byt = msg.getByteArray();
        System.out.println("Byte Array = " + byt);

        System.out.println("Msg XML:\n" + XML);

        cMsgMessage newMsg = msg.parseXml(XML);
        System.out.println("\n\n\n****************************************************\n\n\n");
        System.out.println("newMsg reconstituted text = " + newMsg.getText());
        System.out.println("newMsg subject = " + newMsg.getSubject());
        System.out.println("\n\n\n****************************************************\n\n\n");


        byt = newMsg.getByteArray();
        System.out.println("Byte Array = " + byt);
        if (byt != null) {
            for (int kk = 0; kk < byt.length; kk++) {
                System.out.println("bin[" + kk + "] = " + byt[kk]);
            }
        }
        System.out.println("Byte Array End");


        byte[][] myBin = newMsg.getPayloadItem("BIN_ARRAY").getBinaryArray();
        for (int k=0; k<myBin.length; k++) {
            System.out.println("bin array #" + (k+1) + ":");
            for (int kk=0; kk<myBin[k].length; kk++) {
                System.out.println("bin[" + kk + "] = " + myBin[k][kk]);
            }
        }

//
//        System.out.println("\n\n");
//        byte[] biny = newMsg.getPayloadItem("BINNIE").getBinary();
//            for (int kk=0; kk<biny.length; kk++) {
//                System.out.println("bin[" + kk + "] = " + biny[kk]);
//            }



//        System.out.println("\n\n\nPayload XML:\n" +
//                msg.getPayloadText());
//
//        coda.send(msg);

//        // variables to track message rate
//        int j;
//        double freq=0., freqAvg=0.;
//        long t1, t2, deltaT, totalT=0, totalC=0;
//
//        // Ignore the first N values found for freq in order
//        // to get better avg statistics. Since the JIT compiler in java
//        // takes some time to analyze & compile code, freq may initially be low.
//        long ignore=0;
//
//        while (true) {
//            t1 = System.currentTimeMillis();
//            for (int i = 0; i < count; i++) {
//                if (useSyncSend) {
//                    j = coda.syncSend(msg, 1000);
//                    //System.out.println("Got syncSend val = " + j);
//                }
//                else {
//                    coda.send(msg);
//                }
//                coda.flush(0);
//
//                // delay between messages sent
//                if (delay != 0) {
//                    try {Thread.sleep(delay);}
//                    catch (InterruptedException e) {}
//                }
//            }
//            t2 = System.currentTimeMillis();
//
//            if (ignore == 0) {
//                deltaT = t2 - t1; // millisec
//                freq = (double) count / deltaT * 1000;
//                totalT += deltaT;
//                totalC += count;
//                freqAvg = (double) totalC / totalT * 1000;
//
//                if (debug) {
//                    System.out.println(doubleToString(freq, 1) + " Hz, Avg = " +
//                                       doubleToString(freqAvg, 1) + " Hz");
//                }
//            }
//            else {
//                ignore--;
//            }
//        }
                    try {Thread.sleep(16000);}
                    catch (InterruptedException e) {}
    }
}

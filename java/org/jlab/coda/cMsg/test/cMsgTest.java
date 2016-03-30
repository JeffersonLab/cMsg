/*----------------------------------------------------------------------------*
 *  Copyright (c) 2008        Jefferson Science Associates,                   *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 15-Dec-2008, Jefferson Lab                                   *
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
public class cMsgTest {
    private String  subject = "SUBJECT";
    private String  type = "TYPE";
    private String  name = "producer";
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
    cMsgTest(String[] args) {
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

        return;
    }


    /** Method to print out correct program command line usage. */
    private static void usage() {
        System.out.println("\nUsage:\n\n" +
            "   java cMsgTest\n" +
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
            cMsgTest tp = new cMsgTest(args);
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
                System.out.println("Received msg has payload = ");
                msg.payloadPrintout(0);
            }

            // delay between messages sent
//            if (delay != 0) {
//                try {Thread.sleep(delay);}
//                catch (InterruptedException e) {}
//            }
//
//            try { coda.send(msg); }
//            catch (cMsgException e) {
//                e.printStackTrace();
//            }
        }

     }


//"     <text><![CDATA[~!@#$%^&*()_-+=,.<>?/|\\;:[]{}`]]></text>\n" +


String XML =
"<cMsgMessage\n" +
"     version           = \"3\"\n" +
"     userInt           = \"0\"\n" +
"     getResponse       = \"false\"\n" +
"     domain            = \"(null)\"\n" +
"     sender            = \"(null)\"\n" +
"     senderHost        = \"(null)\"\n" +
"     senderTime        = \"Wed Jan 7 01:20:30 EST 2009\"\n" +
"     receiver          = \"(null)\"\n" +
"     receiverHost      = \"(null)\"\n" +
"     receiverTime      = \"Fri Jan 15 10:30:40 EST 2016\"\n" +
"     userTime          = \"Tue Aug 25 20:40:50 EST 2037\"\n" +
"     subject           = \"SUBJECT&lt;&#34;&gt;\"\n" +
"     type              = \"TYPE\"\n" +
"     payloadItemCount  = \"23\">\n" +
"     <text><![CDATA[A<![CDATA[B]]><![CDATA[]]]]><![CDATA[><![CDATA[C]]><![CDATA[]]]]><![CDATA[><![CDATA[D]]><![CDATA[]]]]><![CDATA[>E]]></text>\n" +
"     <binary name=\"BIN_ARAY\" endian=\"big\" nbytes=\"256\">\n" +
"AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc4\n" +
"OTo7PD0+P0BBQkNERUZHSElKS0xNTk9QUVJTVFVWV1hZWltcXV5fYGFiY2RlZmdoaWprbG1ub3Bx\n" +
"cnN0dXZ3eHl6e3x9fn+AgYKDhIWGh4iJiouMjY6PkJGSk5SVlpeYmZqbnJ2en6ChoqOkpaanqKmq\n" +
"q6ytrq+wsbKztLW2t7i5uru8vb6/wMHCw8TFxsfIycrLzM3Oz9DR0tPU1dbX2Nna29zd3t/g4eLj\n" +
"5OXm5+jp6uvs7e7v8PHy8/T19vf4+fr7/P3+/w==\n" +
"     </binary>\n" +
"     <payload compact=\"false\">\n" +
"          <double name=\">DBL\"> 1.2345678912345679E8 </double>\n" +
"          <string name=\"STR\"><![CDATA[hey you]]></string>\n" +
"          <int8_array name=\"BYTE_ARRAY_ZERO\" count=\"30\">\n" +
"                  1    0    0    0    0\n" +
"                  0    0    0    0    0\n" +
"                  0    0    0    0    0\n" +
"                  2    0    0    0    0\n" +
"                  0    0    0    0    0\n" +
"                  0    0    0    0    3\n" +
"          </int8_array>\n" +
"          <uint64 name=\"BIGINT\"> 18446744073709551614 </uint64>\n" +
"          <int64_array name=\"LONG_ARRAY\" count=\"3\">\n" +
"               -9223372036854775808 -1 9223372036854775807\n" +
"          </int64_array>\n" +
"          <cMsgMessage name=\"SingleMsg\"\n" +
"               version           = \"3\"\n" +
"               userInt           = \"72345\"\n" +
"               getResponse       = \"false\"\n" +
"               domain            = \"(null)\"\n" +
"               sender            = \"me\"\n" +
"               senderHost        = \"meHost\"\n" +
"               senderTime        = \"Wed Dec 31 19:00:00 EST 1969\"\n" +
"               receiver          = \"(null)\"\n" +
"               receiverHost      = \"(null)\"\n" +
"               receiverTime      = \"Wed Dec 31 19:00:00 EST 1969\"\n" +
"               userTime          = \"Wed Dec 31 19:00:00 EST 1969\"\n" +
"               subject           = \"subject*****\"\n" +
"               type              = \"type*****\"\n" +
"               payloadItemCount  = \"1\">\n" +
"               <payload compact=\"false\">\n" +
"                    <double name=\"DBL\"> 2.2250738585072014E-308 </double>\n" +
"               </payload>\n" +
"          </cMsgMessage>\n" +
"          <cMsgMessage_array name=\"MSG_ARRAY\" count=\"2\">\n" +
"               <cMsgMessage\n" +
"                    version           = \"3\"\n" +
"                    userInt           = \"0\"\n" +
"                    getResponse       = \"false\"\n" +
"                    domain            = \"(null)\"\n" +
"                    sender            = \"(null)\"\n" +
"                    senderHost        = \"(null)\"\n" +
"                    senderTime        = \"Wed Dec 31 19:00:00 EST 1969\"\n" +
"                    receiver          = \"(null)\"\n" +
"                    receiverHost      = \"(null)\"\n" +
"                    receiverTime      = \"Wed Dec 31 19:00:00 EST 1969\"\n" +
"                    userTime          = \"Wed Dec 31 19:00:00 EST 1969\"\n" +
"                    subject           = \"sub1\"\n" +
"                    type              = \"type1\"\n" +
"                    payloadItemCount  = \"1\">\n" +
"                    <payload compact=\"false\">\n" +
"                         <double name=\"DBL\"> 2.2250738585072014E-308 </double>\n" +
"                    </payload>\n" +
"               </cMsgMessage>\n" +
"               <cMsgMessage\n" +
"                    version           = \"3\"\n" +
"                    userInt           = \"0\"\n" +
"                    getResponse       = \"false\"\n" +
"                    domain            = \"(null)\"\n" +
"                    sender            = \"(null)\"\n" +
"                    senderHost        = \"(null)\"\n" +
"                    senderTime        = \"Wed Dec 31 19:00:00 EST 1969\"\n" +
"                    receiver          = \"(null)\"\n" +
"                    receiverHost      = \"(null)\"\n" +
"                    receiverTime      = \"Wed Dec 31 19:00:00 EST 1969\"\n" +
"                    userTime          = \"Wed Dec 31 19:00:00 EST 1969\"\n" +
"                    subject           = \"sub2\"\n" +
"                    type              = \"type2\"\n" +
"                    payloadItemCount  = \"1\">\n" +
"                    <payload compact=\"false\">\n" +
"                         <int32 name=\"INT\"> 2147483647 </int32>\n" +
"                    </payload>\n" +
"               </cMsgMessage>\n" +
"          </cMsgMessage_array>\n" +
"          <binary name=\"BIN\" endian=\"big\" nbytes=\"3\">gP9/</binary>\n" +
"          <float name=\"FLT\"> 12345.123 </float>\n" +
"          <int32 name=\"INT\"> 2147483647 </int32>\n" +
"          <int32_array name=\"INT_ARRAY\" count=\"3\">\n" +
"               -2147483648 -1 2147483647\n" +
"          </int32_array>\n" +
"          <int8_array name=\"BYTE_ARRAY\" count=\"3\">\n" +
"               -128   -1  127\n" +
"          </int8_array>\n" +
"          <int32_array name=\"INT_ARRAY_ZERO\" count=\"30\">\n" +
"               1 0 0 0 0\n" +
"               0 0 0 0 0\n" +
"               0 0 0 0 0\n" +
"               2 0 0 0 0\n" +
"               0 0 0 0 0\n" +
"               0 0 0 0 3\n" +
"          </int32_array>\n" +
"          <double_array name=\"DBL_ARRAY\" count=\"8\">\n" +
"               4.900000000000000e-324 0.000000000000000 0.000000000000000 -1.000000000000000 0.000000000000000\n" +
"               0.000000000000000 0.000000000000000 1.7976931348623157e+308\n" +
"          </double_array>\n" +
"          <int64 name=\"LONG\"> 9223372036854775807 </int64>\n" +
"          <uint64_array name=\"BIGINT_ARRAY\" count=\"10\">\n" +
"               1 0 0 0 0\n" +
"               10 0 0 0 1\n" +
"          </uint64_array>\n" +
"          <int16 name=\"SHORT\"> 32767 </int16>\n" +
"          <int8 name=\"BYTE\"> 127 </int8>\n" +
"          <float_array name=\"FLT_ARRAY\" count=\"8\">\n" +
"               1.401298e-45 0.000000 0.000000 -1.000000 0.000000\n" +
"               0.000000 0.000000 3.4028235e+38\n" +
"          </float_array>\n" +
"          <int16_array name=\"SHORT_ARRAY_ZERO\" count=\"30\">\n" +
"                    1      0      0      0      0\n" +
"                    0      0      0      0      0\n" +
"                    0      0      0      0      0\n" +
"                    2      0      0      0      0\n" +
"                    0      0      0      0      0\n" +
"                    0      0      0      0      3\n" +
"          </int16_array>\n" +
"          <int64_array name=\"LONG_ARRAY_ZERO\" count=\"30\">\n" +
"               1 0 0 0 0\n" +
"               0 0 0 0 0\n" +
"               0 0 0 0 0\n" +
"               2 0 0 0 0\n" +
"               0 0 0 0 0\n" +
"               0 0 0 0 3\n" +
"          </int64_array>\n" +
"          <int16_array name=\"SHORT_ARRAY\" count=\"3\">\n" +
"               -32768     -1  32767\n" +
"          </int16_array>\n" +
"          <string_array name=\"STR_ARRAY\" count=\"3\">\n" +
"               <string><![CDATA[one\n" +
"one]]></string>\n" +
"               <string><![CDATA[two\n" +
"two]]></string>\n" +
"               <string><![CDATA[three\n" +
"three]]></string>\n" +
"          </string_array>\n" +
"     </payload>\n" +
"</cMsgMessage>";


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
//        coda = new cMsg(UDL, name, description);
//        coda.connect();
//
//        // enable message reception
//        coda.start();
//
//        // subscribe to subject/type
////        cMsgCallbackInterface cb = new myCallback();
////        Object unsub  = coda.subscribe(subject, type, cb, null);
//
//        // create a message
        cMsgMessageFull msg = new cMsgMessageFull();
//        msg.setSubject(subject);
//        msg.setType(type);
//        msg.setText("~!@#$%^&*()_-+=,.<>?/|\\;:[]{}`");
//        msg.setHistoryLengthMax(4);
//
//        if (sendText) {
//          msg.setText(text);
//        }
//        if (sendBinary) {
//          msg.setByteArrayNoCopy(binArray);
//        }

        // send using UDP instead of TCP
        // msg.getContext().setReliableSend(false);

        // Get rid of sender history stored in msg by calling:
        // msg.setHistoryLengthMax(0);




        cMsgMessage newMsg = msg.parseXml(XML);
        System.out.println("\n\n\n****************************************************\n\n\n");
        System.out.println(newMsg.toString());
        System.out.println("\n\n\n****************************************************\n\n\n");
        System.out.println("Double max = " + Double.MAX_VALUE + ", min = " + Double.MIN_VALUE);
        System.out.println("Float max = " + Float.MAX_VALUE + ", min = " + Float.MIN_VALUE);


    }
}

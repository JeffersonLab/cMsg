package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;

import java.util.Date;
import java.util.Arrays;
import java.util.concurrent.TimeoutException;

/**
 * This is an example class which creates a cMsg client that sends and
 * receives messages by calling sendAndGet.
 */
public class cMsgGetConsumer {

    String  name = "getConsumer";
    String  description = "java getConsumer";
    String  UDL = "cMsg:cMsg://aslan:3456/cMsg/test";
    String  subject = "SUBJECT";
    String  type = "TYPE";

    String  text = "TEXT";
    char[]  textChars;
    int     textSize;

    int     timeout = 1000; // 1 second default timeout
    boolean debug;


    /** Constructor. */
    cMsgGetConsumer(String[] args) {
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
                type= args[i + 1];
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
            "   java cMsgGetConsumer [-n name] [-d description] [-u UDL]\n" +
            "                        [-s subject] [-t type] [-text text]\n" +
            "                        [-textsize size in bytes]\n" +
            "                        [-to timeout] [-debug]\n");
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
            System.out.println("Running cMsg getConsumer subscribed to:\n" +
                               "    subject = " + subject +
                               "\n    type    = " + type);
        }

        // connect to cMsg server
        cMsg coda = new cMsg(UDL, name, description);
        coda.connect();

        // enable message reception
        coda.start();

        // create a message to send to a responder
        cMsgMessage msg = null;
        cMsgMessage sendMsg = new cMsgMessage();
        sendMsg.setSubject(subject);
        sendMsg.setType(type);
        //sendmsg.setText(text);

        byte[] bin = new byte[10000];
        for (int i=0; i< bin.length; i++) {
            bin[i] = 1;
        }
        sendMsg.setByteArrayNoCopy(bin);

        // variables to track incoming message rate
        double freq=0., freqAvg=0.;
        long   t1, t2, deltaT, totalT=0, totalC=0, count;

        while (true) {
            count = 0;

            t1 = (new Date()).getTime();

            // do a bunch of gets
            for (int i=0; i < 400; i++) {
                // do the synchronous sendAndGet with timeout
                try {msg = coda.subscribeAndGet(subject, type, timeout);}
                //try {msg = coda.sendAndGet(sendMsg, timeout);}
                catch (TimeoutException e) {
                    System.out.println("Timeout in sendAndGet");
                    continue;
                }
                //if (msg == null) {
                //    System.out.println("Got null msg");
                //}
                //else {
                //    System.out.println("CLIENT GOT MESSAGE!!!");
                //}
                count++;
            }

            t2 = (new Date()).getTime();

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

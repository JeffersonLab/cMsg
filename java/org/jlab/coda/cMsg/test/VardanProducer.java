package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsg;
import org.jlab.coda.cMsg.cMsgConstants;

/**
 * 
 */
public class VardanProducer {
    private String  subject = "SUBJECT\"";
    private String  type = "TYPE";
    private String  name = "VardanProducer";
    private String  description = "java consumer";
    private String  UDL = "cMsg://localhost/cMsg/myNameSpace";
    //private String  UDL = "cMsg://multicast/cMsg/myNameSpace";
    private boolean debug;
    private long    count;
    private int     delay, loops=300;
    private long    time1, time2;

    /** Constructor. */
    VardanProducer(String[] args) {
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
            else if (args[i].equalsIgnoreCase("-delay")) {
                delay = Integer.parseInt(args[i + 1]);
                i++;
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
                "   java VardanProducer\n" +
                "        [-n <name>]          set client name\n"+
                "        [-d <description>]   set description of client\n" +
                "        [-u <UDL>]           set UDL to connect to cMsg\n" +
                "        [-s <subject>]       set subject of sent messages\n" +
                "        [-t <type>]          set type of sent messages\n" +
                "        [-debug]             turn on printout\n" +
                "        [-delay]             time in millisec to wait between sends\n" +
                "        [-h]                 print this help\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            VardanProducer consumer = new VardanProducer(args);
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
    private String doubleToString(double d, int places) {
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
            System.out.println("Running cMsg consumer subscribed to:\n" +
                               "    subject = " + subject +
                               "\n    type    = " + type);
        }

        // connect to cMsg server
        cMsg coda = new cMsg(UDL, name, description);
        coda.setDebug(cMsgConstants.debugError);

        // create a message
        cMsgMessage msg = new cMsgMessage();
        msg.setSubject(subject);
        msg.setType(type);


        // variables to track message rate
        double freq=0., freqAvg=0.;
        long t1, t2, deltaT, totalT=0, totalC=0;

        // Ignore the first N values found for freq in order
        // to get better avg statistics. Since the JIT compiler in java
        // takes some time to analyze & compile code, freq may initially be low.
        long ignore=0;

        while (true) {
            t1 = System.currentTimeMillis();
            for (int i = 0; i < loops; i++) {
                System.out.println("Will connect");
                coda.connect();
                System.out.println("Will send");
                coda.send(msg);
                System.out.println("Will disconnect");
                coda.disconnect();
                System.out.println("Will wait");
                // delay between messages sent
                if (delay != 0) {
                    try {Thread.sleep(delay);}
                    catch (InterruptedException e) {}
                }
            }
            t2 = System.currentTimeMillis();

            if (ignore == 0) {
                deltaT = t2 - t1; // millisec
                freq = (double) loops / deltaT * 1000;
                totalT += deltaT;
                totalC += loops;
                freqAvg = (double) totalC / totalT * 1000;

                if (debug) {
                    System.out.println("Sending at " + doubleToString(freq, 1) + " Hz, Avg = " +
                                       doubleToString(freqAvg, 1) + " Hz");
                }
            }
            else {
                ignore--;
            }
        }
    }


}

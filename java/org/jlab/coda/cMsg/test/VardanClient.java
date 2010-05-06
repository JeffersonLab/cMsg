package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.cMsgCallbackAdapter;
import org.jlab.coda.cMsg.cMsgCallbackInterface;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Jan 9, 2009
 * Time: 9:41:50 AM
 * To change this template use File | Settings | File Templates.
 */
public class VardanClient {
    private String  name = "Vardan_Client";
    private String  description = "vardan client";
    private String  UDL = "cMsg://localhost/cMsg/myNameSpace";
    //private String  UDL = "cMsg://multicast/cMsg/myNameSpace";
    private boolean debug;
    private long    countIn, countOut;
    private int     delay, loops=300;
    private long    time1, time2;
    private cMsg    coda;
    private cMsgSubscriptionHandle subHandle;


    /** Constructor. */
    VardanClient(String[] args) {
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
                "   java VardanClient\n" +
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
            VardanClient consumer = new VardanClient(args);
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


    class startCb extends cMsgCallbackAdapter {

        public void callback(cMsgMessage msg, Object userObject) {
System.out.println("\nReceived " + name + "/start message from server");

            try {
System.out.println("Subscribe to " + name + "/data");
                cMsgCallbackInterface cb = new receivingCb();
                subHandle  = coda.subscribe(name, "data", cb, null);

                // send output messages at some rate
                cMsgMessage message = new cMsgMessage();
                message.setSubject(name);
                message.setType("sendone");
                message.setText(name);

System.out.println("Send periodic msgs to " + name + "/sendone");
                while (true) {
                    coda.send(message);
                    try { Thread.sleep(100);  }
                    catch (InterruptedException e) {  }

                }
            }
            catch (cMsgException e) {
                e.printStackTrace();
            }
        }
    }


    class receivingCb extends cMsgCallbackAdapter {
        public int getMaximumQueueSize() { return 200; }

        public void callback(cMsgMessage msg, Object userObject) {
System.out.println("\nReceived " + name + "/data message from server");
            try {
                Thread.sleep(10000);
            }
            catch (InterruptedException e) {
            }

            if (countIn == 0) time1 = System.currentTimeMillis();

            // keep track of how many messages we receive
            countIn++;

            if (countIn%loops == 0) {
                time2 = System.currentTimeMillis();

                long deltaT = time2 - time1; // millisec
                double freq = (double) loops / deltaT * 1000;

                System.out.println("Receiving at " + doubleToString(freq, 1) + " Hz, count = " + countIn);
                time1 = time2;
            }
        }
    }


    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {

        if (debug) {
            System.out.println("Running Vardan client");
        }

        // connect to cMsg server
        coda = new cMsg(UDL, name, description);
        coda.setDebug(cMsgConstants.debugInfo);
        coda.connect();

        // enable message reception
        coda.start();

        // subscribe to subject/type
System.out.println("Subscribe to " + name + "/start");
        cMsgCallbackInterface cb = new startCb();
        cMsgSubscriptionHandle subHandle2  = coda.subscribe(name, "start", cb, null);

        // create a message
System.out.println("Send message to admin/send");
        cMsgMessage msg = new cMsgMessage();
        msg.setSubject("admin");
        msg.setType("send");
        msg.setText(name);

        coda.send(msg);

        // variables to track incoming message rate
        double freq, freqAvg;
        long   totalT=0, totalC=0, period = 5000; // millisec

        while (true) {
            countIn = 0;

            // wait
            try { Thread.sleep(period); }
            catch (InterruptedException e) {}

            freq    = (double)countIn/period*1000;
            totalT += period;
            totalC += countIn;
            freqAvg = (double)totalC/totalT*1000;

            if (debug) {
                System.out.println("count = " + countIn + ", " +
                                   doubleToString(freq, 1) + " Hz, Avg = " +
                                   doubleToString(freqAvg, 1) + " Hz");
            }

            System.out.println("The cue is full = " + subHandle.isQueueFull());
            System.out.println("Cue size = " + subHandle.getQueueSize());
            if (subHandle.isQueueFull()) {
                subHandle.clearQueue();
                System.out.println("Cleared cue");
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

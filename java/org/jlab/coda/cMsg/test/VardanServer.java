package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.cMsgCallbackInterface;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Jan 9, 2009
 * Time: 9:42:04 AM
 * To change this template use File | Settings | File Templates.
 */
public class VardanServer {
    private String  name = "Vardan_Server";
    private String  description = "vardan serverr";
    private String  UDL = "cMsg://localhost/cMsg/myNameSpace";
    //private String  UDL = "cMsg://multicast/cMsg/myNameSpace";
    private boolean debug;
    private long    count;
    cMsg coda;

    /** Constructor. */
    VardanServer(String[] args) {
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
            VardanServer consumer = new VardanServer(args);
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


    class adminCb extends cMsgCallbackAdapter {
        public boolean maySkipMessages() { return true; }

        public int getMaximumQueueSize() { return 1001; }

        public int getSkipSize() { return 100; }

        public void callback(cMsgMessage msg, Object userObject) {
System.out.println("\nReceived message to admin/send");
            try {
System.out.println("Subscribe to " + msg.getText() + "/sendone");
                cMsgCallbackInterface cb = new sendingCb();
                Object unsub  = coda.subscribe(msg.getText(), "sendone", cb, null);

                // send start message
                cMsgMessage message = new cMsgMessage();
                message.setSubject(msg.getText());
                message.setType("start");
System.out.println("Send msg to " + msg.getText() + "/start");

                coda.send(message);
            }
            catch (cMsgException e) {
                e.printStackTrace();
            }
        }

    }


    class sendingCb extends cMsgCallbackAdapter {
        public boolean maySkipMessages() { return true; }

        public int getMaximumQueueSize() { return 1002; }

        public int getSkipSize() { return 100; }

        public void callback(cMsgMessage msg, Object userObject) {
//System.out.println("Received msg to " + msg.getText() + "/sendone");
            count++;
            try {
                // send output messages at some rate
                cMsgMessage message = new cMsgMessage();
                message.setSubject(msg.getText());
                message.setType("data");
                message.setText("blah blah blah");

//System.out.println("Send msg to " + msg.getText() + "/data");
                coda.send(message);
            }
            catch (cMsgException e) {
                e.printStackTrace();
            }
        }

    }


    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {

        if (debug) {
            System.out.println("Running Vardan Server");
        }

        // connect to cMsg server
        coda = new cMsg(UDL, name, description);
        coda.connect();

        // enable message reception
        coda.start();

        // subscribe to subject/type
        cMsgCallbackInterface cb = new adminCb();
        Object unsub  = coda.subscribe("admin", "send", cb, null);

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

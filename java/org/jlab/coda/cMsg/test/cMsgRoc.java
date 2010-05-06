package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.*;

/**
 * Simulate a simple ROC.
 */
public class cMsgRoc {

    private String  description = "debugging Roc";
    private String  subject = "Roc1";
    private String  type = "*";
    private String  name = "Roc1";
    private String  text = "junk";
    private String  UDL  = "cMsg://multicast:41111/cMsg/vxtest";

    private int     delay, count = 5;
    private boolean debug;

    private boolean stopData = true;


    /** Constructor. */
    cMsgRoc(String[] args) {
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

        return;
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
            "        [-delay <time>]      set time in millisec between sending of each message\n" +
            "        [-debug]             turn on printout\n" +
            "        [-h]                 print this help\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            cMsgRoc producer = new cMsgRoc(args);
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

    
    /** This class defines the callback to be run when a command message arrives. */
    class myCmdCallback extends cMsgCallbackAdapter {

        public void callback(cMsgMessage msg, Object userObject) {
            String cmd = msg.getText();
            if (cmd == null) return;

            System.out.println("Got command to \"" + cmd + "\"");

            if (cmd.equals("go")) {
                stopData = false;
            }
            else if (cmd.equals("stop")) {
                stopData = true;
            }
            else if (cmd.equals("end")) {
                System.exit(-1);
            }
        }

     }


    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {

        if (debug) {
            System.out.println("Running " + name + ", UDL = " + UDL);
        }

        // 2 connections to cMsg server - 1 for commands, 1 for data
        cMsg codaCmds = new cMsg(UDL, name, description);
        cMsg codaData = new cMsg(UDL, name+"_data", description + " data");
        try {
            codaCmds.connect();
            codaData.connect();
        }
        catch (cMsgException e) {
            e.printStackTrace();
            return;
        }

        // enable message reception
        cMsgCallbackInterface rcCb = new myCmdCallback();
        cMsgSubscriptionHandle handle = codaCmds.subscribe(subject, type, rcCb, null);
        codaCmds.start();

        // create a message
        cMsgMessage msg = new cMsgMessage();
        msg.setSubject("data");
        msg.setType(name);
        msg.setText(text);

        while (true) {

            for (int i = 0; i < count; i++) {
                if (!stopData) {
                    System.out.println("Sending data now");
                    codaData.send(msg);
                    codaData.flush(0);
                }
                else {
                    System.out.println("Stopped sending data");
                }

                // delay between messages sent
                if (delay != 0) {
                    try {Thread.sleep(delay);}
                    catch (InterruptedException e) {}
                }
            }
        }
    }



}

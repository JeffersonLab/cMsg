package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.*;

/**
 * Simulate event builder and event recorder.
 */
public class cMsgEb_er {
    private String  name = "Eb_er";
    private String  description = "Eb_ER consumer";
    private String  UDL = "cMsg://localhost/cMsg/vxtest";
    private boolean debug;


    /** Constructor. */
    cMsgEb_er(String[] args) {
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
                "        [-debug]             turn on printout\n" +
                "        [-h]                 print this help\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            cMsgEb_er consumer = new cMsgEb_er(args);
            consumer.run();
        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }
    }


    /** This class defines the callback to be run when a command message arrives. */
    class myCmdCallback extends cMsgCallbackAdapter {
        public void callback(cMsgMessage msg, Object userObject) {
            String cmd = msg.getText();
            if (cmd == null) return;
            System.out.println("Got command to \"" + cmd + "\"");
            if (cmd.equals("end")) {
                System.exit(-1);
            }
        }
     }


    /** This class defines the callback to be run when a command message arrives. */
    class myDataCallback extends cMsgCallbackAdapter {
        public void callback(cMsgMessage msg, Object userObject) {
            System.out.println("Got data");
        }
     }


    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {

        if (debug) {
            System.out.println("Running Eb_er consumer");
        }

        // 2 connections to cMsg server - 1 for commands, 1 for data
        cMsg codaCmds = new cMsg(UDL, name, description);
        cMsg codaData = new cMsg(UDL, name+"_data", description + " data");
        codaCmds.connect();
        codaData.connect();

        // enable message reception
        codaCmds.start();
        codaData.start();

        // subscribe to commands
        cMsgCallbackInterface cb = new myCmdCallback();
        Object unsub = codaCmds.subscribe("Eb_er", "*", cb, null);

        // subscribe to data
        cMsgCallbackInterface cb_data = new myDataCallback();
        Object unsub_data = codaData.subscribe("data", "*", cb_data, null);

        // wait forever
        while (true) {
            try { Thread.sleep(1000); }
            catch (InterruptedException e) {}
         }
    }


}

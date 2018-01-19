package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsg;
import org.jlab.coda.cMsg.cMsgConstants;
import org.jlab.coda.cMsg.cMsgMessage;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Jan 23, 2009
 * Time: 11:12:41 AM
 * To change this template use File | Settings | File Templates.
 */
public class connectTest {

    private String  subject = "SUBJECT\"";
    private String  type = "TYPE";
    private String  name = "connectTest";
    private String  description = "java connect tester";
    //private String  UDL = "cMsg://localhost/cMsg/myNameSpace";
    private String  UDL = "cMsg://multicast:12345/cMsg/myNameSpace";
    private boolean debug;
    private long    count;
    private int     delay, loops=300;
    private long    time1, time2;

    /** Constructor. */
    connectTest(String[] args) {
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
                "   java connectTest\n" +
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
            connectTest consumer = new connectTest(args);
            consumer.runCycle();
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
            System.out.println("Running cMsg connectTest");
        }

        // connect to cMsg server
        cMsg coda = new cMsg(UDL, name, description);
        coda.setDebug(cMsgConstants.debugInfo);

        count = 0;

        coda.connect();
        System.out.print("Connected!, please kill server: ");
        while (coda.isConnected()) {
            try {Thread.sleep(1000);}
            catch (InterruptedException e) {}
            System.out.print((count++) + ", ");
        }
        System.out.println("\nDisconnected from server, try another connect()");

        while (!coda.isConnected()) {
            try {
                coda.connect();
            }
            catch (cMsgException e) {
                System.out.println("Failed reconnecting to server, again in 1 sec");
                try {Thread.sleep(1000);}
                catch (InterruptedException ex) {}
            }
        }

        if (coda.isConnected()) {
            System.out.println("Now we are connected to server, try sending a msg");
            // create a message
            cMsgMessage msg = new cMsgMessage();
            msg.setSubject(subject);
            msg.setType(type);
            coda.send(msg);
            System.out.println("Message was sent, so exit");
        }
        else {
            System.out.println("Now we are disconnected from server, exit");
        }
        return;
    }

    /**
     * This method is executed as a thread.
     */
    public void runCycle() throws cMsgException {

        if (debug) {
            System.out.println("Running cMsg connectTest");
        }

        count = 0;

        while (true) {
            // connect to cMsg server
            cMsg coda = new cMsg(UDL, name, description);
            coda.setDebug(cMsgConstants.debugInfo);

            coda.connect();
            if (coda.isConnected()) {
                // create a message
                cMsgMessage msg = new cMsgMessage();
                msg.setSubject(subject);
                msg.setType(type);
                coda.send(msg);
            }
            coda.disconnect();
        }
    }


}

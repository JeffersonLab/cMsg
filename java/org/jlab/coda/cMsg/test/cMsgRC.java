package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgMessageFull;

import java.io.BufferedReader;
import java.io.InputStreamReader;


/**
 * Simulate a runcontrol-like application by issuing "stop" and "go" messages from keyboard.
 */
public class cMsgRC {

    private String  name = "RC";
    private String  description = "little rc";
    private String  UDL = "cMsg://localhost/cMsg/vxtest";

    private boolean debug;

    cMsg coda;

    /** Constructor. */
    cMsgRC(String[] args) {
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
            "   java cMsgRC\n" +
            "        [-n <name>]          set client name\n"+
            "        [-d <description>]   set description of client\n" +
            "        [-u <UDL>]           set UDL to connect to cMsg\n" +
            "        [-c <count>]         set # of messages to send before printing output\n" +
            "        [-debug]             turn on printout\n" +
            "        [-h]                 print this help\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            cMsgRC tp = new cMsgRC(args);
            tp.run();
        }
        catch (cMsgException e) {
            System.out.println(e.toString());
            System.exit(-1);
        }
    }

    /**
     * Method to wait on string from keyboard.
     * @param s prompt string to print
     * @return string typed in keyboard
     */
    public String inputStr(String s) {
        String aLine = "";
        BufferedReader input =  new BufferedReader(new InputStreamReader(System.in));
        System.out.print(s);
        try {
            aLine = input.readLine();
        }
        catch (Exception e) {
            e.printStackTrace();
        }
        return aLine;
    }

 
    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {

        if (debug) {
            System.out.println("Running cMsgRC runcontrol simulator sending to: Roc1 & Eb_er");
        }

        // connect to cMsg server
        coda = new cMsg(UDL, name, description);
        coda.connect();

        // create stop & go messages
        cMsgMessageFull goMsg = new cMsgMessageFull();
        goMsg.setSubject("Roc1");
        goMsg.setType("anything");
        goMsg.setText("go");

        cMsgMessageFull stopMsg = new cMsgMessageFull();
        stopMsg.setSubject("Roc1");
        stopMsg.setType("anything");
        stopMsg.setText("stop");

        while (true) {
            inputStr("Enter to send GO");
            goMsg.setSubject("Roc1");
            coda.send(goMsg);
            goMsg.setSubject("Eb_er");
            coda.send(goMsg);

            inputStr("Enter to send STOP");
            stopMsg.setSubject("Roc1");
            coda.send(stopMsg);
            stopMsg.setSubject("Eb_er");
            coda.send(stopMsg);
        }
    }



}

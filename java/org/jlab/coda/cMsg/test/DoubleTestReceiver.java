package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgMessageFull;

import java.util.Map;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Mar 9, 2010
 * Time: 10:32:37 AM
 * To change this template use File | Settings | File Templates.
 */
public class DoubleTestReceiver {

    private String  subject1 = "doubleSender";
    private String  type1    = "type";

    private String  subject2 = "doubleReceiver";
    private String  type2    = "type";

    private String  name = "payload test receiver";
    private String  description = "java producer";
    private String  UDL = "cMsg://localhost/cMsg/myNameSpace";


    private int     delay = 2000;
    private boolean debug;

    cMsg coda;

    /** Constructor. */
    DoubleTestReceiver(String[] args) {
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
            "   java DoubleTest\n" +
            "        [-n <name>]          set client name\n"+
            "        [-d <description>]   set description of client\n" +
            "        [-u <UDL>]           set UDL to connect to cMsg\n" +
            "        [-delay <time>]      set time in millisec between sending of each message\n" +
            "        [-debug]             turn on printout\n" +
            "        [-h]                 print this help\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            DoubleTestReceiver dtr = new DoubleTestReceiver(args);
            dtr.run();
        }
        catch (cMsgException e) {
            System.out.println(e.toString());
            System.exit(-1);
        }
    }


    /**
      * This class defines the callback to be run when a double arrives.
      */
     class DoubleReceivingCallback extends cMsgCallbackAdapter {

         public void callback(cMsgMessage msg, Object userObject) {
             if (msg.hasPayload()) {
                 Map<String,cMsgPayloadItem> map = msg.getPayloadItems();
                 for (cMsgPayloadItem it : map.values()) {
                     System.out.println("p item name = " + it.getName());
                 }
                 cMsgPayloadItem item = msg.getPayloadItem("int");
                 //cMsgPayloadItem item = msg.getPayloadItem("double");
                 try {
                     //double d = item.getDouble();
                     int i = item.getInt();
                     System.out.println("q2 " + msg.getContext().getQueueSize());
                     //System.out.println("" + d);
//                     int i = item.getInt();
//                     System.out.println("" + i);
                 }
                 catch (cMsgException e) {
                     e.printStackTrace();
                 }
             }
             else {
                 //System.out.println("Got message with no payload");
                 int i = msg.getUserInt();
                 System.out.println("q2 " + msg.getContext().getQueueSize());
             }
         }

      }



    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {

        if (debug) {
            System.out.println("Running cMsg double sending test receiver");
        }

        // connect to cMsg server
        coda = new cMsg(UDL, name, description);
        coda.connect();

        // enable message reception
        coda.start();

        // subscription for receiving and printing double
        cMsgCallbackAdapter cb_r = new DoubleReceivingCallback();
        Object unsub_r  = coda.subscribe(subject2, type2, cb_r, null);

        // wait
        while (true) {
            try {Thread.sleep(10000);}
            catch (InterruptedException e) {}
        }
    }


}

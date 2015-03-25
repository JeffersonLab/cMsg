package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.*;


/**
@author timmer
 */
public class UnsubTest {

    private String subject = "subject1";
    private String type    = "type1";

    private String  name = "unsubscribe tester";
    private String  description = "java unsubscribe tester";
    private String  UDL = "cMsg://localhost/cMsg/myNameSpace";


    private int     delay = 2000;
    private boolean debug;

    cMsg coda;

    /** Constructor. */
    UnsubTest(String[] args) {
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
            else if (args[i].equalsIgnoreCase("-u")) {
                UDL= args[i + 1];
                i++;
            }
            else if (args[i].equalsIgnoreCase("-d")) {
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
            "        [-u <UDL>]           set UDL to connect to cMsg\n" +
            "        [-d <time>]          set time in millisec between sending of each message\n" +
            "        [-debug]             turn on printout\n" +
            "        [-h]                 print this help\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            UnsubTest dtr = new UnsubTest(args);
            dtr.run();
        }
        catch (cMsgException e) {
            System.out.println(e.toString());
            System.exit(-1);
        }
    }


    /** This class defines the callback to be run - sleeps for 1 sec. */
     class ReceivingCallback extends cMsgCallbackAdapter {
         public void callback(cMsgMessage msg, Object userObject) {
             System.out.println("Callback entering");
             try {Thread.sleep(8000);}
             catch (InterruptedException e) {}
             System.out.println("Callback exiting");
         }
      }


    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {

        if (debug) {
            System.out.println("Running unsubscribe tester");
        }

        cMsgMessage msg = new cMsgMessage();
        msg.setSubject("subject1");
        msg.setType("type1");

        // connect to cMsg server
        coda = new cMsg(UDL, name, description);
        coda.connect();

        // Enable message reception
        coda.start();

        // Subscription for msgs
        ReceivingCallback cb = new ReceivingCallback();
        cMsgSubscriptionHandle[] unsubHandles = new cMsgSubscriptionHandle[10];
        for (int i=0; i < 10; i++) {
            System.out.println("Subscribe " + (i+1));
            unsubHandles[i] = coda.subscribe(subject, type, cb, null);
            try {Thread.sleep(500);}
            catch (InterruptedException e) {}
        }

        // Send a msg to run callback
        System.out.println("Send msg");
        coda.send(msg);

        System.out.println("Wait 2 sec");
        try {Thread.sleep(2000);}
        catch (InterruptedException e) {}

        // Unsubscribe
        for (int i=0; i < 10; i++) {
            System.out.println("Un subscribe " + (i+1));
            coda.unsubscribe(unsubHandles[i]);
            //try {Thread.sleep(500);}
            //catch (InterruptedException e) {}
        }




        // Wait
        while (true) {
            try {Thread.sleep(10000);}
            catch (InterruptedException e) {}
        }
    }


}

package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.cMsgMessageFull;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Mar 9, 2010
 * Time: 1:33:03 PM
 * To change this template use File | Settings | File Templates.
 */
public class DoubleTestSender {

     private String  subject1 = "doubleSender";
     private String  type1    = "type";

     private String  subject2 = "doubleReceiver";
     private String  type2    = "type";

     private String  name = "payload test sender";
     private String  description = "java producer";
     private String  UDL = "cMsg://localhost/cMsg/myNameSpace";


     private int     delay = 2000;
     private boolean debug;

     cMsg coda;

     /** Constructor. */
     DoubleTestSender(String[] args) {
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
             DoubleTestSender dts = new DoubleTestSender(args);
             dts.run();
         }
         catch (cMsgException e) {
             System.out.println(e.toString());
             System.exit(-1);
         }
     }


     /**
      * This class defines the callback to be run when a message matching
      * our subscription arrives.
      */
     class DoubleSendingCallback extends cMsgCallbackAdapter {

         public void callback(cMsgMessage msg, Object userObject) {

             cMsgMessageFull message = new cMsgMessageFull();
             message.setSubject(subject2);
             message.setType(type2);

             double d = 1./3.;
             int i=2;
             try {
                 //cMsgPayloadItem item = new cMsgPayloadItem("double", d);
                 cMsgPayloadItem item = new cMsgPayloadItem("int", i);
                 //message.setUserInt(i);
                 message.addPayloadItem(item);
                 System.out.println("q1 " + msg.getContext().getQueueSize());
//                for (int j=0; j<1000000; j++) {
//                    int q = j*j;
//                }
                 //System.out.println("Sending double");
                 //System.out.println("Sending int");
                 //message.setNoHistoryAdditions(true);
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
             System.out.println("Running cMsg double sending test sender");
         }

         // connect to cMsg server
         coda = new cMsg(UDL, name, description);
         coda.connect();

         // enable message reception
         coda.start();

         // subscription for sending double
         cMsgCallbackAdapter cb_s = new DoubleSendingCallback();
         Object unsub_s  = coda.subscribe(subject1, type1, cb_s, null);

         // wait
         while (true) {
             try {Thread.sleep(10000);}
             catch (InterruptedException e) {}
         }
     }

}

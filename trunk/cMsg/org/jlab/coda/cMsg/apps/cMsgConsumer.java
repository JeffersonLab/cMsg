package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgCallback;
import org.jlab.coda.cMsg.cMsg.cMsg;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Sep 9, 2004
 * Time: 12:48:08 PM
 * To change this template use File | Settings | File Templates.
 */
public class cMsgConsumer {
    String name;
    long count;

    cMsgConsumer(String name) {
        this.name = name;
    }

    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            cMsgConsumer consumer = null;
            if (args.length > 0) {
                consumer = new cMsgConsumer(args[0]);
            }
            else {
                consumer = new cMsgConsumer("consumer");
                System.out.println("Name of this client is \"consumer\"");
            }
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


    class myCallback implements cMsgCallback {
         /**
          * Callback method definition.
          *
          * @param msg message received from domain server
          * @param userObject object passed as an argument which was set when the
          *                   client orginally subscribed to a subject and type of
          *                   message.
          */
         public void callback(cMsgMessage msg, Object userObject) {
             //try { Thread.sleep(1); }
             //catch (InterruptedException e) {}
             count++;
         }

        public boolean maySkipMessages() {return false;}

        public boolean mustSerializeMessages() {return true;}

     }


    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {
        String subject = "SUBJECT", type = "TYPE";

        System.out.println("Running Message Consumer\n");

        String UDL = "cMsg:cMsg://aslan:3456/cMsg";

        System.out.print("Try to connect ...");
        cMsg coda = new cMsg(UDL, name, "message consumer");
        System.out.println(" done");

        System.out.println("Enable message receiving");
        coda.start();

        System.out.println("Subscribe to subject = " + subject + ", type = " + type);
        cMsgCallback cb = new myCallback();
        coda.subscribe(subject, type, cb, null);


        double freq=0., freqAvg=0., freqTotal=0.;
        long   iterations=1;

        while (true) {
            count = 0;
            try { Thread.sleep(10000); }
            catch (InterruptedException e) {}

            freq = (double)count/10.;
            if (Double.MAX_VALUE - freqTotal < freq) {
                freqTotal  = 0.;
                iterations = 1;
            }
            freqTotal += freq;
            freqAvg = freqTotal/iterations;
            iterations++;
            System.out.println("count = " + count + ", " + doubleToString(freq, 0) + " Hz, Avg = " + doubleToString(freqAvg, 0) + " Hz");

            if (!coda.isConnected()) {
                System.out.println("No longer connected to domain server, quitting");
                try { Thread.sleep(20000); }
                catch (InterruptedException e) {}
                System.exit(-1);
            }
        }
    }
}

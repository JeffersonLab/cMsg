package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgCallback;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgCallbackImpl;
import org.jlab.coda.cMsg.cMsg.cMsg;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Sep 16, 2004
 * Time: 12:48:46 PM
 * To change this template use File | Settings | File Templates.
 */
public class cMsgConsumer2CBs {
    String name;
    long count1, count2;

    cMsgConsumer2CBs(String name) {
        this.name = name;
    }

    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            cMsgConsumer2CBs consumer = null;
            if (args.length > 0) {
                consumer = new cMsgConsumer2CBs(args[0]);
            }
            else {
                consumer = new cMsgConsumer2CBs("consumer");
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


    class myCallback1 extends cMsgCallbackImpl {
         /**
          * Callback method definition.
          *
          * @param msg message received from domain server
          * @param userObject object passed as an argument which was set when the
          *                   client orginally subscribed to a subject and type of
          *                   message.
          */
        public void callback(cMsgMessage msg, Object userObject) {
            //try { Thread.sleep(1000); }
            //catch (InterruptedException e) {}
            count1++;
        }

        public boolean mustSerializeMessages() {
            return false;
        }
     }


    class myCallback2 extends cMsgCallbackImpl {
        /**
         * Callback method definition.
         *
         * @param msg message received from domain server
         * @param userObject object passed as an argument which was set when the
         *                   client orginally subscribed to a subject and type of
         *                   message.
         */
        public void callback(cMsgMessage msg, Object userObject) {
            count2++;
        }

        public boolean mustSerializeMessages() {return false;}
     }


    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {
        String subject1 = "SUBJECT1", type1 = "TYPE1";
        String subject2 = "SUBJECT2", type2 = "TYPE2";

        System.out.println("Running Message Consumer\n");

        String UDL = "cMsg:cMsg://aslan:3456/cMsg";

        System.out.print("Try to connect ...");
        cMsg coda = new cMsg(UDL, name, "message consumer");
        System.out.println(" done");

        System.out.println("Enable message receiving");
        coda.start();

        System.out.println("Subscribe to subject = " + subject1 + ", type = " + type1);
        cMsgCallback cb1 = new cMsgConsumer2CBs.myCallback1();
        coda.subscribe(subject1, type1, cb1, null);
        System.out.println("Subscribe to subject = " + subject2 + ", type = " + type2);
        cMsgCallback cb2 = new cMsgConsumer2CBs.myCallback2();
        coda.subscribe(subject2, type2, cb2, null);


        double freq=0., freqAvg=0., freqTotal=0.;
        long   iterations=1;

        while (true) {
            count1 = count2 = 0;
            try { Thread.sleep(10000); }
            catch (InterruptedException e) {}

            freq = (double) (count1 + count2)/10.;
            if (Double.MAX_VALUE - freqTotal < freq) {
                freqTotal  = 0.;
                iterations = 1;
            }
            freqTotal += freq;
            freqAvg = freqTotal/iterations;
            iterations++;
            System.out.println("count = " + (count1+count2) + ", " + doubleToString(freq, 0) +
                               " Hz, Avg = " + doubleToString(freqAvg, 0) + " Hz");

        }
    }
}

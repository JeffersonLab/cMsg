package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;

import java.util.Date;
import java.util.concurrent.TimeoutException;

/**
 * An example class which creates a cMsg message producer/consumer
 * which both produces messages and waits synchronously for a response message
 * with a sendAndGet call.
 */
public class cMsgGetConsumer {
    String name;
    long count;

    cMsgGetConsumer(String name) {
        this.name = name;
    }

    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            cMsgGetConsumer consumer = null;
            if (args.length > 0) {
                consumer = new cMsgGetConsumer(args[0]);
            }
            else {
                consumer = new cMsgGetConsumer("consumer");
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


    class myCallback extends cMsgCallbackAdapter {
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
            System.out.println("*");
            count++;
        }

        public boolean maySkipMessages() {
            return true;
        }

        public boolean mustSerializeMessages() {
            return false;
        }

        public int  getMaximumThreads() {
            return 200;
        }

     }


    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {
        String subject = "responder", type = "TYPE";
        //String subject = "SUBJECT", type = "TYPE";

        System.out.println("Running Message GET Consumer\n");

        String UDL = "cMsg:cMsg://aslan:3456/cMsg/vx";

        cMsg coda = new cMsg(UDL, name, "getConsumer");
        coda.connect();
        coda.start();

        double freq=0., freqAvg=0., freqTotal=0.;
        long   iterations=1;
        long   t1, t2;
        cMsgMessage msg = null;
        cMsgMessage sendMsg = new cMsgMessage();
        sendMsg.setSubject(subject);
        sendMsg.setType(type);

        while (true) {
            count = 0;
            t1 = (new Date()).getTime();

            // do a bunch of gets
            for (int i=0; i < 1000; i++) {
                try {msg = coda.sendAndGet(sendMsg, 1000);}
                catch (TimeoutException e) {}

                //msg = coda.subscribeAndGet(subject, type, 1000);
                if (msg == null) {
                    System.out.println("TIMEOUT in GET");
                }
                else {
                    count++;
                }
                //try {Thread.sleep(2000);}
                //catch (InterruptedException e) { }
            }

            t2 = (new Date()).getTime();
            freq = (double)count/(t2-t1)*1000.;
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
                System.exit(-1);
            }
        }
    }
}

package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsg;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Sep 8, 2004
 * Time: 12:48:16 PM
 * To change this template use File | Settings | File Templates.
 */
public class cMsgProducer {
    String name, subject="SUBJECT", type="TYPE";

    cMsgProducer(String name) {
        this.name = name;
    }

    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            cMsgProducer producer = null;
            if (args.length > 0) {
                producer = new cMsgProducer(args[0]);

                if (args.length > 1) {
                    producer.subject = args[1];
                    System.out.println("  producing messages with subject = " + producer.subject);
                }
                if (args.length > 2) {
                    producer.type = args[2];
                    System.out.println("  producing messages with type = " + producer.type);
                }
            }
            else {
                producer = new cMsgProducer("producer");
                System.out.println("Name of this client is \"producer\"");
                System.out.println("  producing messages with subject = " + producer.subject);
                System.out.println("  producing messages with type = " + producer.type);
                System.out.println("  producing messages with \"JUNK\" as text");
            }
            producer.run();
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

    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {
        System.out.println("Running Message Producer\n");

        String UDL = "cMsg:cMsg://aslan:3456/cMsg";

        cMsg coda = new cMsg(UDL, name, "message producer");
        coda.connect();

        cMsgMessage msg = new cMsgMessage();
        msg.setSubject(subject);
        msg.setType(type);
        msg.setText("Junk");

        double freq=0., freqAvg=0., freqTotal=0.;
        long t1, t2, deltaT, count = 20000, iterations=1;

        System.out.println("Sending messages ...");
        int j=0;
        while (true) {
            t1 = System.currentTimeMillis();
            for (int i = 0; i < count; i++) {
                //try {Thread.sleep(1000);}
                //catch (InterruptedException e) {}
                coda.send(msg);
            }
            t2 = System.currentTimeMillis();

            deltaT = t2 - t1; // millisec
            freq = (double)count/deltaT*1000;
            if (Double.MAX_VALUE - freqTotal < freq) {
                freqTotal = 0.;
                iterations = 1;
            }
            freqTotal += freq;
            freqAvg = freqTotal/iterations;
            iterations++;
            System.out.println(doubleToString(freq, 0) + " Hz, Avg = " + doubleToString(freqAvg, 0) + " Hz");

        }
    }

}

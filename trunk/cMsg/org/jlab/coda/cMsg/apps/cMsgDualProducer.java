package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgDomain.cMsg;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Oct 28, 2004
 * Time: 5:12:27 PM
 * To change this template use File | Settings | File Templates.
 */
public class cMsgDualProducer {
    String name, subject="SUBJECT", type="TYPE";
    cMsg coda;


    cMsgDualProducer(String name) {
        this.name = name;
    }

    class SendThread extends Thread {
        String id;

        SendThread(String id) {
            this.id = id;
        }

        public void run() {
            cMsgMessage msg = new cMsgMessage();
            msg.setSubject(subject);
            msg.setType(type);
            msg.setText("Junk");

            double freq=0., freqAvg=0., freqTotal=0.;
            long t1, t2, deltaT, count = 20000, iterations=1;

            System.out.println(id + " sending messages ...");
            int j=0;
            while (true) {
                t1 = System.currentTimeMillis();
                for (int i = 0; i < count; i++) {
                    //try {Thread.sleep(1000);}
                    //catch (InterruptedException e) {}
                    try {
                        coda.send(msg);
                    }
                    catch (cMsgException e) {
                        e.printStackTrace();
                        System.exit(-1);
                    }
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
                System.out.println(id + " " + doubleToString(freq, 0) + " Hz, Avg = " + doubleToString(freqAvg, 0) + " Hz");
            }

        }
    }

    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            cMsgDualProducer producer = new cMsgDualProducer("producer");
                System.out.println("Name of this client is \"producer\"");
                System.out.println("  producing messages with subject = " + producer.subject);
                System.out.println("  producing messages with type = " + producer.type);
                System.out.println("  producing messages with \"JUNK\" as text");
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

        System.out.print("Try to connect ...");
        coda = new cMsg(UDL, name, "message producer");
        System.out.println(" done");

        SendThread sender1 = new SendThread("1");
        SendThread sender2 = new SendThread("2");

        sender1.start();
        sender2.start();
    }

}

package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsg;

/**
 * An example class which creates a cMsg message producer.
 */
public class cMsgProducer {
    String  name = "producer";
    String  description = "java producer";
    String  UDL = "cMsg:cMsg://aslan:3456/cMsg/test";
    String  subject = "SUBJECT";
    String  type = "TYPE";
    String  text = "TEXT";
    int     delay;
    boolean debug;
    long    count;


    /** Constructor. */
    cMsgProducer(String[] args) {
        decodeCommandLine(args);
    }


    /**
     * Method to decode the command line used to start this application.
     * @param args command line arguments
     */
    public void decodeCommandLine(String[] args) {

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
                type = args[i + 1];
                i++;
            }
            else if (args[i].equalsIgnoreCase("-text")) {
                text = args[i + 1];
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
            "   java cMsgProducer [-n name] [-d description] [-u UDL]\n" +
            "                     [-s subject] [-t type] [-text text]\n" +
            "                     [-delay millisec] [-debug]\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            cMsgProducer producer = new cMsgProducer(args);
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

        if (debug) {
            System.out.println("Running cMsg producer sending to:\n" +
                               "    subject = " + subject +
                               "\n    type    = " + type);
        }

        // connect to cMsg server
        cMsg coda = new cMsg(UDL, name, description);
        coda.connect();

        // create a message
        cMsgMessage msg = new cMsgMessage();
        msg.setSubject(subject);
        msg.setType(type);
        msg.setText(text);

        // variables to track message rate
        double freq=0., freqAvg=0.;
        long t1, t2, deltaT, totalT=0, totalC=0, count=10000;

        // delay between messages
        if (delay != 0) count = count/(20 + delay);

        while (true) {
            t1 = System.currentTimeMillis();
            for (int i = 0; i < count; i++) {
                // delay between messages sent
                if (delay != 0) {
                    try {Thread.sleep(delay);}
                    catch (InterruptedException e) {}
                }
                coda.send(msg);
                coda.flush();
            }
            t2 = System.currentTimeMillis();

            deltaT  = t2 - t1; // millisec
            freq    = (double)count/deltaT*1000;
            totalT += deltaT;
            totalC += count;
            freqAvg = (double)totalC/totalT*1000;

            System.out.println(doubleToString(freq, 1) + " Hz, Avg = " +
                               doubleToString(freqAvg, 1) + " Hz");

        }
    }

}

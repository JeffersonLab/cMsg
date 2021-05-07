package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgUtilities;
import org.jlab.coda.cMsg.common.cMsgServerFinder;

/**
 * Thread that sleeps for 10 sec and checks to see if
 * there is a platform running with the given EXPID
 *
 * @author gurjyan, timmer
 *         Date: 8/6/2015
 */
public class cMsgPlatformSpy  implements Runnable {

    String expid;
    String enviromentalExpid;
    cMsgServerFinder finder;
    boolean debug;


    /**
     * Constructor.
     * @param args arguments.
     */
    cMsgPlatformSpy(String[] args) {
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
            else if (args[i].equalsIgnoreCase("-x")) {
                expid = args[i + 1];
                i++;
            }
            else if (args[i].equalsIgnoreCase("-d")) {
                debug = true;
            }
            else {
                usage();
                System.exit(-1);
            }
        }
    }


    /** Method to print out correct program command line usage. */
    private static void usage() {
        System.out.println("\nUsage:\n\n" +
            "   java cMsgPlatformSpy\n" +
            "        [-x <expid>]         platform expid to search for\n"+
            "        [-d]                 turn on debug printout\n" +
            "        [-h]                 print this help\n");
    }


    /**
     * Run as a stand-alone application.
     * @param args arguments.
     */
    public static void main(String[] args) {
        cMsgPlatformSpy spy = new cMsgPlatformSpy(args);
        spy.run();
    }


    @Override
    public void run() {
        enviromentalExpid = System.getenv("EXPID");
        if (expid == null) {
            expid = enviromentalExpid;
        }
        finder = new cMsgServerFinder();
        finder.setPassword(expid);

        while(true){
            finder.findRcServers();
            cMsgMessage[] msgs = finder.getRcServers();

            if (msgs != null) {
                try {
                    for(cMsgMessage msg: msgs){
                        if (msg.getPayloadItem("host") != null && msg.getPayloadItem("expid") != null) {
                            String h = msg.getPayloadItem("host").getString();
                            String e = msg.getPayloadItem("expid").getString();

                            //System.out.println("Found rc server w/ host = "+ h + " and EXPID = " + e);

                            if (!cMsgUtilities.isHostLocal(h) && e.equals(expid)) {
                                System.out.println("Warning! Detected conflicting platform " +
                                        "running on the host = "+ h +" with EXPID = " + e);
                            }
                        }
                    }
                } catch (cMsgException e) {
                    e.printStackTrace();
                }
            }

            try {Thread.sleep(3000);}
            catch (InterruptedException e) {return;}
        }
    }
}
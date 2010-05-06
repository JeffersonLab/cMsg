package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.cMsgException;

import java.io.FileInputStream;
import java.io.IOException;
import java.nio.channels.FileChannel;
import java.nio.MappedByteBuffer;
import java.nio.ByteOrder;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Apr 1, 2010
 * Time: 10:31:35 AM
 * To change this template use File | Settings | File Templates.
 */
public class etTest {

    /** Constructor. */
    etTest(String[] args) {
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
            "   java etTest\n" +
            "        [-h]                 print this help\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            etTest tp = new etTest(args);
            tp.run();
        }
        catch (cMsgException e) {
            System.out.println(e.toString());
            System.exit(-1);
        }
    }



    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {

        try {
            FileInputStream fis = new FileInputStream("/tmp/javaCShareTest");
            FileChannel fc = fis.getChannel();
            MappedByteBuffer buffer = fc.map(FileChannel.MapMode.READ_ONLY, 0, fc.size());
            buffer.order(ByteOrder.LITTLE_ENDIAN);

            while (true) {
                for (int i=0; i < 10; i++) {
                    int val = buffer.getInt(i*4);
                    System.out.print(val + " ");
                }
                System.out.println("");
                Thread.sleep(1000);
            }


        }
        catch (InterruptedException e) {
            e.printStackTrace();
        }
        catch (IOException e) {
            e.printStackTrace();
        }

    }

}

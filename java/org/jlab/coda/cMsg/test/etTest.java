package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.cMsgException;

import java.nio.ByteBuffer;
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
            ByteBuffer buf = ByteBuffer.allocate(16);
            buf.putInt(1);
            buf.putInt(2);
            buf.putInt(3);
            buf.putInt(4);

            //buf.position(4).limit(12);
            buf.position(8).limit(16);

            ByteBuffer slice = buf.slice();
            slice.order(ByteOrder.LITTLE_ENDIAN);

            buf.position(0).limit(16);

            // Backing array of buf
            byte[] bufBack = buf.array();
            System.out.println("buf pos = " + buf.position() + ", lim = " + buf.limit());
            System.out.println("buf backing array, len = " + bufBack.length);
            System.out.println("Data is:");
            System.out.println("" + buf.getInt());
            System.out.println("" + buf.getInt());
            System.out.println("" + buf.getInt());
            System.out.println("" + buf.getInt());
            // There is where in the backing array the buf starts
            System.out.println("Backing array offset is " + buf.arrayOffset());

            // Backing array of slice
            byte[] sliceBack = slice.array();
            System.out.println("slice pos = " + slice.position() + ", lim = " + slice.limit());
            System.out.println("slice backing array, len = " + sliceBack.length);
            System.out.println("Data is:");
            System.out.println("" + slice.getInt());
            System.out.println("" + slice.getInt());
            // There is where in the backing array the slice starts
            System.out.println("Backing array offset is " + slice.arrayOffset());

            if (bufBack == sliceBack) {
                System.out.println("Both backing arrays are the same");
            }
            else {
                System.out.println("Both backing arrays are the diff");
            }

        }
        catch (Exception e) {
            e.printStackTrace();
        }

    }

}

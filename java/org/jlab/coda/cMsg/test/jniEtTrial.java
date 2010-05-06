package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.cMsgException;

import java.nio.ByteBuffer;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Apr 2, 2010
 * Time: 1:04:37 PM
 * To change this template use File | Settings | File Templates.
 */
public class jniEtTrial {

    static {
        try {
            System.loadLibrary("jniTest");
        }
        catch (Error e) {
            System.out.println("error loading libjniTest.so");
            System.exit(-1);
        }
    }

    private ByteBuffer buffer;
    int memSize;

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


    /** Run as a stand-alone application.  */
    public static void main(String[] args) {
        try {
            // variables to track data rate
            double freq=0., freqAvg=0.;
            long t1, t2, deltaT, totalT=0, totalC=0;
            int count= 1000000;
            // Ignore the first N values found for freq in order
            // to get better avg statistics. Since the JIT compiler in java
            // takes some time to analyze & compile code, freq may initially be low.
            long ignore=0;

            int size = 1024; // 10 bytes
            jniEtTrial trial = new jniEtTrial(size);
            trial.init("/tmp/jniEtTrial", size);
            System.out.println("DONE WITH INIT()");

            while(true) {
                t1 = System.currentTimeMillis();

                for (int i = 0; i < count; i++) {
                    trial.buffer.clear();
                    trial.getData(size, trial.buffer);

                    // print out data
                    //trial.printData();

                    // change the data
//                    trial.buffer.flip();
//                    for (int j=0; j < size; j++) {
//                        trial.buffer.put(j, (byte) (trial.buffer.get() + 1));
//                    }

                    trial.buffer.flip();
                    trial.putData(size, 0, trial.buffer);
                }

                t2 = System.currentTimeMillis();

                if (ignore == 0) {
                    deltaT = t2 - t1; // millisec
                    freq = ((double)size*count) / deltaT * 1000;
                    totalT += deltaT;
                    totalC += size*count;
                    freqAvg = (double) totalC / totalT * 1000;

                    System.out.println(freq + " Hz, Avg = " +
                                       freqAvg + " Hz");
                }
                else {
                    ignore--;
                }

               // Thread.sleep(3000);
            }
        }
//        catch (InterruptedException e) {
//            e.printStackTrace();
//            System.exit(-1);
//        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }
    }


    jniEtTrial(int memSize) {
        this.memSize = memSize;
        buffer = ByteBuffer.allocate(memSize);
        if (!buffer.hasArray()) {
            System.out.println("Buffer does NOT have backing array !!!");
            System.exit(-1);
        }
    }

    public void printData() {
        buffer.flip();
        for (int i=0; i < memSize; i++) {
            System.out.println(buffer.get()+" ");
        }
    }

    public native void init(String fileName, int memByteSize) throws cMsgException;

    public native void getData(int byteSize, ByteBuffer b) throws cMsgException;

    public native void putData(int byteSize, int offset, ByteBuffer b) throws cMsgException;
}

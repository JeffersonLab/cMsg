package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.cMsgNetworkConstants;

import java.io.*;
import java.net.Socket;
import java.net.InetAddress;
import java.net.UnknownHostException;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Apr 1, 2010
 * Time: 10:31:35 AM
 * To change this template use File | Settings | File Templates.
 */
public class etTest2 {
    int    size;
    int    port;
    String host;
    DataInputStream   in;
    DataOutputStream out;
    byte[] buffer;

    /** Constructor. */
    etTest2(String[] args) {
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
            else if (args[i].equalsIgnoreCase("-size")) {
                size = Integer.parseInt(args[i + 1]);
                if (size < 1) {
                    System.out.println("size must be in valid range");
                    usage();
                    System.exit(-1);
                }
                i++;
            }
            else if (args[i].equalsIgnoreCase("-port")) {
                port = Integer.parseInt(args[i + 1]);
                if (port < 1024 || port > 65535) {
                    System.out.println("port must be in valid range");
                    usage();
                    System.exit(-1);
                }
                i++;
            }
            else if (args[i].equalsIgnoreCase("-host")) {
                host = args[i + 1];
                i++;
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
            "        [-size <size>]      size (bytes) of amount of data to send server\n" +
            "        [-port <port>]      server port to connect to\n" +
            "        [-host <host>]      server host to connect to\n" +
            "        [-h]                print this help\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
            etTest2 tp = new etTest2(args);
            tp.run();
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

    private void readData() throws IOException {
        // send number of bytes we want to read
        out.writeInt(1);
        out.writeInt(size);
        out.flush();

        // read 'em in
        in.readFully(buffer);
    }

    private void writeData() throws IOException {
        out.writeInt(2);
        out.writeInt(size);
        out.write(buffer);
        out.flush();
    }

    /**
     * This method is executed as a thread.
     */
    public void run() {

        // variables to track message rate
        double freq=0., freqAvg=0.;
        long t1, t2, deltaT, totalT=0, totalC=0, count;

        // Ignore the first N values found for freq in order
        // to get better avg statistics. Since the JIT compiler in java
        // takes some time to analyze & compile code, freq may initially be low.
        long ignore=0;

        try {
            buffer = new byte[size];

            // default to local host
            if (host == null) host = InetAddress.getLocalHost().getCanonicalHostName();

            // create socket to server
System.out.println("Try to connect to server on " + host + " at port " + port + " with size " + size);
            Socket socket = new Socket(host, port);

            // set socket options
            socket.setTcpNoDelay(true);
            socket.setSendBufferSize(cMsgNetworkConstants.bigBufferSize);

            // streams for reading and writing
            in  = new DataInputStream(new BufferedInputStream(socket.getInputStream(),
                                                              cMsgNetworkConstants.bigBufferSize));
            out = new DataOutputStream(new BufferedOutputStream(socket.getOutputStream(),
                                                                cMsgNetworkConstants.bigBufferSize));

            while (true) {
                count = 0L;
                t1 = System.currentTimeMillis();

                while (true) {
                    readData();
                    writeData();

                    count += size;

                    if (count > 1000000000) break;
                }

                t2 = System.currentTimeMillis();

                if (ignore == 0) {
                    deltaT = t2 - t1; // millisec
                    freq = (double) count / deltaT * 1000;
                    totalT += deltaT;
                    totalC += count;
                    freqAvg = (double) totalC / totalT * 1000;

                    System.out.println(doubleToString(freq, 1) + " Hz, Avg = " +
                                           doubleToString(freqAvg, 1) + " Hz");
                }
                else {
                    ignore--;
                }
            }

        }
        catch (UnknownHostException e) {
            e.printStackTrace();
        }
        catch (IOException e) {
            e.printStackTrace();
        }

    }

}
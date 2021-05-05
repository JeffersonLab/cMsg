package org.jlab.coda.cMsg.apps;


import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;

/**
 * Created by timmer on 8/6/15.
 */
public class cMsgStartEt {


    String file;
    String cmd = "et_start -f /tmp/test -n 6 -s 32 -g 2";
    boolean debug;


    /** Constructor. */
    cMsgStartEt(String[] args) {
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
            else if (args[i].equalsIgnoreCase("-c")) {
                cmd = args[i + 1];
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
            "   java cMsgStartEt\n" +
            "        [-c <et cmd>]       command to start et system\n"+
            "        [-d]                turn on debug printout\n" +
            "        [-h]                print this help\n");
    }


    /**
     * Run as a stand-alone application.
     * @param args arguments.
     */
    public static void main(String[] args) {
        cMsgStartEt prog = new cMsgStartEt(args);
        prog.run();
    }


    /**
     * Get the output of a process - either error or regular output
     * depending on the input stream.
     *
     * @param inputStream get process output from this stream
     * @return String of process output
     */
    private String getProcessOutput(InputStream inputStream) {
        String line;
        StringBuilder sb = new StringBuilder(300);
        BufferedReader brErr = new BufferedReader(new InputStreamReader(inputStream));

        try {
            // read each line of output
            while ((line = brErr.readLine()) != null) {
                sb.append(line);
                sb.append("\n");
            }
        }
        catch (IOException e) {
            // probably best to ignore this error
        }

        if (sb.length() > 0) {
            // take off last \n we put in buffer
            sb.deleteCharAt(sb.length()-1);
            return sb.toString();
        }

        return null;
    }


    /**
     * Get regular output (if monitor true) and error output
     * of Process and return both as strings.
     *
     * @param monitor <code>true</code> if we store regular output, else <code>false</code>.
     * @return array with both regular output (first element) and error output (second).
     */
    private String[] gatherAllOutput(Process process, boolean monitor) {
        String output;
        String[] strs = new String[2];

        // Grab regular output if requested.
        if (monitor) {
            output = getProcessOutput(process.getInputStream());
            if (output != null) {
                strs[0] = output;
            }
        }

        // Always grab error output.
        output = getProcessOutput(process.getErrorStream());
        if (output != null) {
            strs[1] = output;
        }

        return strs;
    }




    public void run() {
        try {

//                etCmd += " > " + (etOpenConfig.getEtName() + ".log");

//                String[] cmd = new String[] {"/bin/sh", "-c", etCmd};
            System.out.println("    DataTransport Et: create ET system with cmd:\n" + cmd);
            Process processET = Runtime.getRuntime().exec(cmd);

            // Allow process a chance to run before testing if its terminated.
            Thread.yield();
            try {Thread.sleep(1000);}
            catch (InterruptedException e) {}

            // Figure out if process has already terminated.
            boolean terminated = true;
            try { processET.exitValue(); }
            catch (IllegalThreadStateException e) {
                terminated = false;
            }

            if (terminated) {
                String errorOut = null;
                // grab any output
                String[] retStrings = gatherAllOutput(processET, true);
                if (retStrings[0] != null) {
                    errorOut += retStrings[0];
                }
                if (retStrings[1] != null) {
                    errorOut += "\n" + retStrings[0];
                }

                System.out.println("    Et system terminated so exit: " + errorOut);
                return;
            }

       }
        catch (Exception e) {
            e.printStackTrace();
        }

    }

}


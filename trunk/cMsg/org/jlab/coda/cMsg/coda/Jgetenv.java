/*---------------------------------------------------------------------------*
 *  Copyright (c) 2003        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    V. Gyurjyan, 12-Jan-2004, Jefferson Lab                                 *
 *                                                                            *
 *    Author:  Vardan Gyurjyan                                                *
 *             gurjyan@jlab.org                  Jefferson Lab, MS-12H        *
 *             Phone: (757) 269-5879             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.coda;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.Hashtable;

/**
 * Class to read all UNIX environmental variables, store them in a hashtable,
 * and make them accessible to Java.
 *
 * @author Vardan Gyurjyan
 * @version 1.0
 */
public class Jgetenv {
    /** Handle on "getenv" shell command. */
    private Process process;
    /** Reads stream output of "getenv" shell command. */
    private BufferedReader reader;
    /** Hashtable storing environmental variables' names and values. */
    private Hashtable table;
    /** A line of output from the "getenv" command. */
    private String line;
    /** Value of an environmental variable. */
    private String value;
    /** Name of an environmental variable. */
    private String variable;

    /** Constructor */
    public Jgetenv() throws JgetenvException {
        updateInfo();
    }

    /**
     * Method to run the UNIX shell command, "printenv", take its output,
     * and put it into a hashtable.
     *
     * @throws JgetenvException if environmental variables cannot be read
     */
    public void updateInfo() throws JgetenvException {
        try {
            process = Runtime.getRuntime().exec("printenv");
            reader  = new BufferedReader(new InputStreamReader(process.getInputStream()));
            table   = new Hashtable();
            while ((line = reader.readLine()) != null) {
                value = (line.substring(line.indexOf('=') + 1));
                variable = line.substring(0, line.indexOf('='));
                table.put(variable, value);
            }
        }
        catch (java.io.IOException e) {
            throw (new JgetenvException(e.toString()));
        }
        finally {
            try {
                reader.close();
                process.destroy();
            }
            catch (java.io.IOException e) {
            }
        }
    }


    /**
     * Method returning an envirnomental variable's value.
     *
     * @param env name of environmental variable
     * @return environmental variable's value
     * @throws JgetenvException if environmental variable is not found
     */
    public String echo(String env) throws JgetenvException {
        if (table.containsKey(env))
            return (String) table.get(env);
        else {
            throw (new JgetenvException(env + " Environmental variable is not defined"));
        }
    }

    /**
     * Run this class as an executable which prints environmental variables
     * given on the command line.
     *
     * @param args environmental variables of interest
     */
    public static void main(String[] args) {
        try {
            Jgetenv myJgetenv = new Jgetenv();
            System.out.println(args[0]);
            System.out.println(myJgetenv.echo(args[0]));
        }
        catch (JgetenvException a) {
            System.err.println(a);
        }
    }
}

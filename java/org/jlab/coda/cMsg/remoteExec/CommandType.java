/*---------------------------------------------------------------------------*
*  Copyright (c) 2010        Jefferson Science Associates,                   *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    C.Timmer, 22-Nov-2010, Jefferson Lab                                    *
*                                                                            *
*    Authors: Carl Timmer                                                    *
*             timmer@jlab.org                   Jefferson Lab, #10           *
*             Phone: (757) 269-5130             12000 Jefferson Ave.         *
*             Fax:   (757) 269-6248             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.remoteExec;

/**
 * This enumerates the commands that can be sent to Executor.
 *
 * @author timmer
 * Date: Oct 8, 2010
 */
public enum CommandType {
    
    START_THREAD  ("start_thread"),   // Executor must start a thread in its own JVM
    START_PROCESS ("start_process"),  // Executor must start an external process
    STOP          ("stop"),           // stop thread or process that Executor started
    STOP_ALL      ("stop_all"),       // stop all threads and processes that Executor started
    DIE           ("die"),            // Executor must kill itself
    IDENTIFY      ("identify");       // Executor must send id information

    private String value;

    private CommandType(String value) {
        this.value = value;
    }

    /**
     * Get the enum's value.
     *
     * @return the value, e.g., "stop" for a STOP
     */
    public String getValue() {
        return value;
    }

    /**
     * Obtain the name from the value.
     *
     * @param value the value to match.
     * @return the name, or <code>null</code>.
     */
    public static String getName(String value) {
        CommandType commandTypes[] = CommandType.values();
        for (CommandType dt : commandTypes) {
            if (dt.value.equalsIgnoreCase(value)) {
                return dt.name();
            }
        }

        return null;
    }

    /**
     * Obtain the enum from the value.
     *
     * @param value the value to match.
     * @return the matching enum, or <code>null</code>.
     */
    public static CommandType getCommandType(String value) {
        CommandType commandTypes[] = CommandType.values();
        for (CommandType dt : commandTypes) {
            if (dt.value.equalsIgnoreCase(value)) {
                return dt;
            }
        }

        return null;
    }

}

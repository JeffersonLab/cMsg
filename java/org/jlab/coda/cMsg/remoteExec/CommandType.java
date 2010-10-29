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
    JOBS          ("jobs"),           // Executor must send a list of all currently
                                      // running jobs (processes and threads)
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

package org.jlab.coda.cMsg.remoteExec;

/**
 * @author timmer
 * Date: Oct 14, 2010
 */
public class CommandReturn {

    /** Id number generated by Commander. */
    private final int id;

    /** Associated id number generated by Executor. */
    private final int executorId;

    /** Output (if any) of error. */
    private String errorOutput;

    /** Output (if any) of the started process. */
    private String regularOutput;

    /** Was there an error in running the process/thread? */
    private boolean error;

    /** Has the process already terminated? */
    private boolean terminated;

    /** Callback object. Sorry, only one allowed. */
    private ProcessCallback processCallback;

    /** Object to pass to callback as argument. */
    private Object userObject;

    // TODO: anything volatile??

    /**
     * Constructor.
     * @param id id number generated by Commander.
     * @param processId associated id number generated by Executor.
     * @param error was there an error running the process/thread?
     * @param terminated has process terminated already?
     * @param regularOutput regular output (if any) of the started process
     * @param errorOutput error output (if any) of the started process
     */
    public CommandReturn(int id, int processId, boolean error, boolean terminated,
                         String regularOutput, String errorOutput) {
        // ensure consistency
        if (errorOutput != null) error = true;
        
        this.id = id;
        this.executorId = processId;
        this.error = error;
        this.terminated = terminated;
        this.regularOutput = regularOutput;
        this.errorOutput = errorOutput;
    }

    /**
     * Register a callback to be run when the process ends.
     * Calling this multiple times results in a replacement of the callback.
     *
     * @param processCallback callback to be run when the process ends.
     * @param userObject argument to be passed to callback.
     */
    public void registerCallback(ProcessCallback processCallback, Object userObject) {
        this.userObject = userObject;
        this.processCallback = processCallback;
    }

    /**
     * Unregister a callback so it does not get run.
     */
    public void unregisterCallback() {
        this.userObject = null;
        this.processCallback = null;
    }

    /**
     * Run the registered callback.
     */
    public void executeCallback() {
        if (processCallback == null) return;
        processCallback.callback(userObject, this);
    }

    /**
     * Get the Executor generated id number.
     * @return the Executor generated id number.
     */
    int getExecutorId() {
        return executorId;
    }

    /**
     * Get the Commander generated id number.
     * @return the Commander generated id number.
     */
    public int getId() {
        return id;
    }

    /**
     * Set whether the process has already terminated.
     * @param b <code>true</code> if process has terminated, else <code>false</code>.
     */
    void hasTerminated(boolean b) {
        terminated = b;
    }

    /**
     * Get whether the process has already terminated.
     * @return <code>true</code> if process has already terminated, else <code>false</code>.
     */
    public boolean hasTerminated() {
        return terminated;
    }

    /**
     * Get whether error occurred in the attempt to run the process or thread.
     * @return <code>true</code> if error occurred attempting to run process/thread,
     *         else <code>false</code>.
     */
    public boolean hasError() {
        return error;
    }

    /**
     * Get whether the process has any output.
     * @return <code>true</code> if the process has output, else <code>false</code>.
     */
    public boolean hasOutput() {
        return regularOutput != null;
    }

    /**
     * Get output (if any) generated by the started process or thread.
     * @return any output by the process or thread, or null if none
     */
    public String getOutput() {
        return regularOutput;
    }

    /**
     * Get error output (if any) generated by the started process or thread.
     * @return any error output by the process or thread, or null if none
     */
    public String getError() {
        return errorOutput;
    }

    /**
     * Set error output generated by the started process or thread.
     * @param error error output by the process or thread.
     */
    void setError(String error) {
        errorOutput = error;
        if (error != null) this.error = true;
    }

    /**
     * Set output generated by the started process or thread.
     * @param output output by the process or thread.
     */
    void setOutput(String output) {
        regularOutput = output;
    }

    public String toString() {
        return "Id = " + id + ", execId = " + executorId + ", error = " +
                error + ", term = " + terminated + ", output = " + (regularOutput != null)  +
                ", error output = " + (errorOutput != null);
    }

}
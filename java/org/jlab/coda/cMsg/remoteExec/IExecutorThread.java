package org.jlab.coda.cMsg.remoteExec;

/**
 * This interface allows the Executor to run, stop, and cleanup a
 * thread it has been told to run.
 *
 * @author timmer
 * Date: Oct 12, 2010
 */
public interface IExecutorThread extends Runnable {
    /**
     * Stop and clean up after a thread started by an Executor
     * (which includes stopping all spawned subthreads).
     */
    public void remove();
}

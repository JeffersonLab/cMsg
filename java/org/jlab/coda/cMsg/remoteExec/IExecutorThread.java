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
     * When a thread implementing this interface is run by an Executor,
     * eventually a Commander may want to stop it. In that case an
     * interrupt will be sent to the thread. Immediately after that,
     * this method will be run so things can be cleaned up.
     */
    public void cleanUp();
}

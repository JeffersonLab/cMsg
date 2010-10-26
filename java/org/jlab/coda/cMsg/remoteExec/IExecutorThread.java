package org.jlab.coda.cMsg.remoteExec;

/**
 * This interface allows the Executor to run, wait for, and shut down
 * an application it has been told to run.
 *
 * @author timmer
 * Date: Oct 12, 2010
 */
public interface IExecutorThread {

    /**
     * When a class implementing this interface is run by an Executor,
     * it calls this method to do so. This method does everything that
     * needs doing in order to get this application running.
     */
    public void startItUp();

    /**
     * When a class implementing this interface is run by an Executor,
     * eventually a Commander may want to stop it. In that case, this
     * method can be run so things can be shut down and cleaned up.
     */
    public void shutItDown();

    /**
     * When a class implementing this interface is run by an Executor,
     * a Commander may want to wait until it finished running. In that
     * case, this method can be run so things to wati for it to finish.
     */
    public void waitUntilDone() throws InterruptedException;
}

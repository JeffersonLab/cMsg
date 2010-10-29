package org.jlab.coda.cMsg.remoteExec;


/**
 * Interface for callback object to be run when process or thread ends.
 * @author timmer
 * Date: Oct 20, 2010
 */
public interface ProcessCallback {
    /**
     * Callback method definition.
     *
     * @param userObject user object passed as an argument to {@link Commander#startProcess}
     *                   or {@link Commander#startThread} with the purpose of being passed
     *                   on to this callback.
     * @param commandReturn object returned from call to startProcess or startThread
     *                      method (which registered this callback) which was updated
     *                      just before being passed to this method.
     */
    public void callback(Object userObject, CommandReturn commandReturn);
}

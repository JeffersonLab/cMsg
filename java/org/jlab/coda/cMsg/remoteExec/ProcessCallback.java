package org.jlab.coda.cMsg.remoteExec;


/**
 * Interface for callback object to be run when process ends.
 * @author timmer
 * Date: Oct 20, 2010
 */
public interface ProcessCallback {
    /**
     * Callback method definition.
     *
     * @param userObject object passed as an argument which was set when the
     *                   client orginally subscribed to a subject and type of
     *                   message.
     */
    public void callback(Object userObject);
}

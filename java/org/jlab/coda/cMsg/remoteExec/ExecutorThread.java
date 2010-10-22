package org.jlab.coda.cMsg.remoteExec;

/**
 * Wrapper class for java.lang.Thread so cleanUp() method can be called
 * in Commander's class to be instantiated and run as a thread.
 *
 * @author timmer
 * Date: Oct 22, 2010
 */
class ExecutorThread extends Thread implements IExecutorThread {
    IExecutorThread execThread;

    ExecutorThread(IExecutorThread execThread) {
        super(execThread);
        this.execThread = execThread;
    }

    public void cleanUp() {
        execThread.cleanUp();
    }
}

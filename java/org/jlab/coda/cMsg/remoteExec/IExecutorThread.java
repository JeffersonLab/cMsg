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
     * In a Thread object, this can be used to wrap start();
     */
    public void startItUp();

    /**
     * When a class implementing this interface is run by an Executor,
     * eventually a Commander may want to stop it. In that case, this
     * method can be run so things can be shut down and cleaned up.
     * In a Thread object, this can be used to wrap some user method
     * to gracefully shut the thread down;
     */
    public void shutItDown();

    /**
     * When a class implementing this interface is run by an Executor,
     * a Commander may want to wait until it finished running. In that
     * case, this method can be run so things to wait for it to finish.
     * In a Thread object, this can be used to wrap join();
     *
     * @throws InterruptedException if thread interrupted.
     */
    public void waitUntilDone() throws InterruptedException;
}

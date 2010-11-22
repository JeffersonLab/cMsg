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
 * Interface for callback object to be run when process or thread ends.
 * @author timmer
 * Date: Oct 20, 2010
 */
public interface CommandCallback {
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

/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 15-Oct-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Oct 15, 2004
 * Time: 12:29:21 PM
 * To change this template use File | Settings | File Templates.
 */
public class cMsgCallbackAdapter implements cMsgCallbackInterface {
    /**
     * Callback method definition.
     *
     * @param msg message received from domain server
     * @param userObject object passed as an argument which was set when the
     *                   client orginally subscribed to a subject and type of
     *                   message.
     */
    public void callback(cMsgMessage msg, Object userObject) {
        return;
    }

    /**
     * Method telling whether messages may be skipped or not.
     * @return true if messages can be skipped without error, false otherwise
     */
    public boolean maySkipMessages() {
        return false;
    }

    /**
     * Method telling whether messages must serialized -- processed in the order
     * received.
     * @return true if messages must be processed in the order received, false otherwise
     */
    public boolean mustSerializeMessages() {
        return true;
    }

    /**
     * Method to get the maximum number of messages to cue for the callback.
     * @return maximum number of messages to cue for the callback
     */
    public int getMaximumCueSize() {
        return 60000;
    }

    /**
     * Method to get the maximum number of messages to skip over (delete) from
     * the cue for the callback when the cue size has reached it limit.
     * This is only used when the {@link #maySkipMessages} method returns true;
     * @return maximum number of messages to skip over from the cue
     */
    public int getSkipSize() {
        return 10000;
    }

    /**
     * Method to get the maximum number of supplemental threads to use for running
     * the callback if {@link #mustSerializeMessages} returns false.
     * @return maximum number of supplemental threads to start
     */
    public int getMaximumThreads() {
        return 1000;
    }

    /**
     * Method to get the maximum number of unprocessed messages per supplemental thread.
     * This number is a target for dynamically adjusting server.
     * This is only used when the {@link #mustSerializeMessages} method returns false;
     * @return maximum number of messages per supplemental thread
     */
    public int getMessagesPerThread() {
        return 50;
    }

}

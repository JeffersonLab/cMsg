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
 * Enumerate the possible states of a callback.
 *
 * @author timmer
 * Date: Nov 2, 2010
 */
public enum CallbackState {

    /** No callback exists. */
    NONE      ("none"),
    /** Callback was already run. */
    RUN       ("run"),
    /** Callback cancelled because error getting process or thread to run. */
    ERROR     ("error"),
    /** Callback is scheduled to be run. */
    PENDING   ("pending"),
    /** Callback was cancelled by killing executor with calls to
     * {@link Commander#kill}  or {@link Commander#killAll(boolean)}. */
    KILLED   ("killed"),
    /** Callback was cancelled by stopping executor process or thread with
     *  calls to {@link Commander#stop}  or {@link Commander#stopAll}. */
    STOPPED   ("stopped"),
    /** Callback was cancelled by calling {@link Commander#cancelCallback(CommandReturn)}. */
    CANCELLED ("cancelled");

    private String value;

    private CallbackState(String value) {
        this.value = value;
    }

    /**
     * Get the enum's value.
     *
     * @return the value, e.g., "reporting" for an REPORTING.
     */
    public String getValue() {
        return value;
    }

    /**
     * Obtain the name from the value.
     *
     * @param value the value to match.
     * @return the name, or <code>null</code>.
     */
    public static String getName(String value) {
        CallbackState states[] = CallbackState.values();
        for (CallbackState state : states) {
            if (state.value.equalsIgnoreCase(value)) {
                return state.name();
            }
        }

        return null;
    }

    /**
     * Obtain the enum from the value.
     *
     * @param value the value to match.
     * @return the matching enum, or <code>null</code>.
     */
    public static CallbackState getCallbackState(String value) {
        CallbackState states[] = CallbackState.values();
        for (CallbackState state : states) {
            if (state.value.equalsIgnoreCase(value)) {
                return state;
            }
        }

        return null;
    }

}

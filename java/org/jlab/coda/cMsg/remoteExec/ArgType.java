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
 * This enumerates the different types of a constructor's arguments.
 * @author timmer
 * Date: Oct 26, 2010
 */
public enum ArgType {

    PRIMITIVE      (1), // Arg = primitive (except char) type, takes String as arg to constructor
    PRIMITIVE_CHAR (2), // Arg = char, takes char value as arg to constructor
    REFERENCE      (3), // Arg = reference type which takes a custom number and type of args to construct
    REFERENCE_NOARG(4), // Arg = reference type which uses no-arg constructor
    NULL           (5); // Arg = null

    private int value;

    private ArgType(int value) {
        this.value = value;
    }

    /**
     * Get the enum's value.
     *
     * @return the value, e.g., 1 for STRING.
     */
    public int getValue() {
        return value;
    }

    /**
     * Obtain the name from the value.
     *
     * @param value the value to match.
     * @return the name, or <code>null</code>.
     */
    public static String getName(int value) {
        ArgType argTypes[] = ArgType.values();
        for (ArgType dt : argTypes) {
            if (dt.value == value) {
                return dt.name();
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
    public static ArgType getArgType(int value) {
        ArgType argTypes[] = ArgType.values();
        for (ArgType dt : argTypes) {
            if (dt.value == value) {
                return dt;
            }
        }

        return null;
    }



}

/*----------------------------------------------------------------------------*
 *  Copyright (c) 2005        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 10-Feb-2005, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;

/**
 * This class contains the method used to determine whether a message's subject
 * and type match a subscription's subject and type.
 */
public class cMsgMessageMatcher {
    /**
     * Characters which need to be escaped to avoid special interpretation in
     * regular expressions.
     */
    static final private String escapeChars = "\\(){}[]+.|^$";

    /** Array of regular expressions to look for in a given string. */
    static final private String[] lookFor = {"\\\\", "\\(", "\\)", "\\{", "\\}", "\\[",
                                             "\\]", "\\+" ,"\\.", "\\|", "\\^", "\\$"};
    /** Array of strings to replace the found regular expressions in a given string. */
    static final private String[] replaceWith = {"\\\\\\\\", "\\\\(", "\\\\)", "\\\\{", "\\\\}", "\\\\[",
                                                 "\\\\]",  "\\\\+" ,"\\\\.", "\\\\|", "\\\\^", "\\\\\\$"};

    /**
     * This method implements a simple wildcard matching scheme where "*" means
     * any or no characters and "?" means exactly 1 character.
     *
     * @param regexp subscription string that can contain wildcards (* and ?)
     * @param s message string to be matched (can be blank which only matches *)
     * @return true if there is a match, false if there is not
     */
    static final public boolean matches(String regexp, String s) {
        // It's a match if regexp (subscription string) is null
        if (regexp == null) return true;

        // If the message's string is null, something's wrong
        if (s == null) return false;

        // The first order of business is to take the regexp arg and modify it so that it is
        // a regular expression that Java can understand. This means subbing all occurrences
        // of "*" and "?" with ".*" and ".{1}". And it means escaping other regular
        // expression special characters.
        regexp = escape(regexp);

        // Now see if there's a match with the string arg
        if (s.matches(regexp)) return true;
        return false;
    }

    /**
     * This method takes a string and escapes most special, regular expression characters.
     * The return string can allows only * and ? to be passed through in a way meaningful
     * to regular expressions (as .* and .{1} respectively).
     *
     * @param s string to be escaped
     * @return escaped string
     */
    static final public String escape(String s) {
        if (s == null) return null;

        for (int i=0; i < escapeChars.length(); i++) {
            s = s.replaceAll(lookFor[i], replaceWith[i]);
        }

        // translate from * and ? to Java regular expression language
        s = s.replaceAll("\\*", ".*");
        s = s.replaceAll("\\?", ".{1}");

        return s;
    }

}

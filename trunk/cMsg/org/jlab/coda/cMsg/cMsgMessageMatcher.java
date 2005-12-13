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

import java.util.regex.Pattern;
import java.util.regex.Matcher;

/**
 * This class contains the method used to determine whether a message's subject
 * and type match a subscription's subject and type.
 */
class cMsgMessageMatcherOrig {
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
     * @param escapeRegexp if true, the regexp argument is escaped, else not
     * @return true if there is a match, false if there is not
     */
    static final public boolean matches(String regexp, String s, boolean escapeRegexp) {
        // It's a match if regexp (subscription string) is null
        if (regexp == null) return true;

        // If the message's string is null, something's wrong
        if (s == null) return false;

        // The first order of business is to take the regexp arg and modify it so that it is
        // a regular expression that Java can understand. This means subbing all occurrences
        // of "*" and "?" with ".*" and ".{1}". And it means escaping other regular
        // expression special characters.
        // Sometimes regexp is already escaped, so no need to redo it.
        if (escapeRegexp) {
            regexp = escape(regexp);
        }

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
        System.out.println("s = " + s);
        return s;
    }

}


public class cMsgMessageMatcher {
    /**
     * Characters which need to be escaped to avoid special interpretation in
     * regular expressions.
     */
    static final private String escapeChars = "\\(){}[]+.|^$";

    /** Array of characters to look for in a given string. */
    static final private String[] lookFor = {"\\", "(", ")", "{", "}", "[",
                                             "]", "+" ,".", "|", "^", "$"};

    /** Length of each item in {@link #lookFor}. */
    static final private int lookForLen = 1;

    /** Array of regexp strings to replace the found special characters in a given string. */
    static final private String[] replaceWith = {"\\\\", "\\(", "\\)", "\\{", "\\}", "\\[",
                                                 "\\]",  "\\+" ,"\\.", "\\|", "\\^", "\\$"};

    /** Length of each item in {@link #replaceWith}. */
    static final private int replaceWithLen = 2;


    /**
     * This method implements a simple wildcard matching scheme where "*" means
     * any or no characters and "?" means exactly 1 character.
     *
     * @param regexp subscription string that can contain wildcards (* and ?)
     * @param s message string to be matched (can be blank which only matches *)
     * @param escapeRegexp if true, the regexp argument is escaped, else not
     * @return true if there is a match, false if there is not
     */
    static final public boolean matches(String regexp, String s, boolean escapeRegexp) {
        // It's a match if regexp (subscription string) is null
        if (regexp == null) return true;

        // If the message's string is null, something's wrong
        if (s == null) return false;

        // The first order of business is to take the regexp arg and modify it so that it is
        // a regular expression that Java can understand. This means subbing all occurrences
        // of "*" and "?" with ".*" and ".{1}". And it means escaping other regular
        // expression special characters.
        // Sometimes regexp is already escaped, so no need to redo it.
        if (escapeRegexp) {
            regexp = escape(regexp);
        }

        // Now see if there's a match with the string arg
        if (s.matches(regexp)) return true;
        return false;
    }

    /**
     * This method implements a simple wildcard matching scheme where "*" means
     * any or no characters and "?" means exactly 1 character.
     *
     * @param pattern compiled subscription regexp string that can contain wildcards (* and ?)
     * @param s message string to be matched (can be blank which only matches *)
     * @return true if there is a match, false if there is not
     */
    static final public boolean matches(Pattern pattern, String s) {
        // It's a match if regexp (subscription string) is null
        if (pattern == null) return true;

        // If the message's string is null, something's wrong
        if (s == null) return false;

        Matcher m = pattern.matcher(s);

        // Now see if there's a match with the string arg
        return m.matches();
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

        // StringBuilder is a nonsynchronized StringBuffer class.
        StringBuilder buf = new StringBuilder(s);
        int lastIndex = 0;

        // Check for characters which need to be escaped to prevent
        // special interpretation in regular expressions.
        for (int i=0; i < lookFor.length; i++) {
            lastIndex = 0;
            for (int j=0; j < buf.length(); j++) {
                lastIndex = buf.indexOf(lookFor[i], lastIndex);
                if (lastIndex < 0) {
                    break;
                }
                buf.replace(lastIndex, lastIndex+lookForLen, replaceWith[i]);
//System.out.println("buf1 = " + buf);
                lastIndex += replaceWithLen;
            }
        }

        // Translate from * and ? to Java regular expression language.

        // Check for "*" characters and replace with ".*"
        lastIndex = 0;
        for (int j=0; j < buf.length(); j++) {
            lastIndex = buf.indexOf("*", lastIndex);
            if (lastIndex < 0) {
                break;
            }
            buf.replace(lastIndex, lastIndex+1, ".*");
//System.out.println("buf2 = " + buf);
            lastIndex += 2;
        }

        // Check for "?" characters and replace with ".{1}"
        lastIndex = 0;
        for (int j=0; j < buf.length(); j++) {
            lastIndex = buf.indexOf("?", lastIndex);
            if (lastIndex < 0) {
                break;
            }
            buf.replace(lastIndex, lastIndex+1, ".{1}");
//System.out.println("buf3 = " + buf);
            lastIndex += 4;
        }

        return buf.toString();
    }

}

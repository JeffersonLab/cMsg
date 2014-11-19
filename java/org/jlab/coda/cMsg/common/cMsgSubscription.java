/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 11-Aug-2004, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.common;

import java.util.*;
import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.util.List;
import java.util.LinkedList;
import java.util.concurrent.ConcurrentHashMap;

/**
 * This class stores a client's subscription to a particular message subject and type.
 * Used by both the cMsg domain API and cMsg subdomain handler objects. Once the
 * domain server object is created, the only set method called is setNamespace.
 * Thus, this object is for all intents and purposes immutable during its lifetime.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgSubscription extends cMsgGetHelper {

    /** Subject subscribed to. */
    private String subject;

    /** Subject turned into regular expression that understands *, ?, #, and integer ranges. */
    private String subjectRegexp;

    /** Compiled regular expression given in {@link #subjectRegexp}. */
    private Pattern subjectPattern;

    /** Are there any *, ?, or # characters or integer ranges in the subject? */
    private boolean wildCardsInSub;

    /** Keep a set of strings that are known to match the subject for performance reasons. */
    private Set<String> subjectMatches = Collections.synchronizedSet(new HashSet<String>(65));

    /** Type subscribed to. */
    private String type;

    /** Type turned into regular expression that understands *, ?, #, and integer ranges. */
    private String typeRegexp;

    /** Compiled regular expression given in {@link #typeRegexp}. */
    private Pattern typePattern;

    /** Are there any *, ?, or # characters or integer ranges in the type? */
    private boolean wildCardsInType;

    /** Keep a set of strings that are known to match the type for performance reasons. */
    private Set<String> typeMatches = Collections.synchronizedSet(new HashSet<String>(65));

    /** Client generated intVal. */
    private int id;

    /**
     * This refers to a cMsg subdomain's namespace in which this subscription resides.
     * It is useful only when another server becomes a client for means of bridging
     * messages. In this case, this special client does not reside in 1 namespace but
     * represents subscriptions from different namespaces. This is used on the server
     * side.
     */
    private String namespace;

    /** Set of objects used to notify servers that their subscribeAndGet is complete. */
    private HashSet<cMsgNotifier> notifiers;

    /**
     * This map contains all of the callback thread objects
     * {@link cMsgCallbackThread} used on the client side.
     * The string values are dummies and not used.
     */
    private ConcurrentHashMap<cMsgCallbackThread, String> callbacks;

    /**
     * This set contains all clients (regular and bridge) subscribed to this exact subject, type,
     * and namespace and is used on the server side.
     */
    private HashSet<cMsgClientInfo> allSubscribers;

    /**
     * This map contains all regular clients (servers do not call
     * subscribeAndGet but use subscribe to implement it) who have
     * called {@link org.jlab.coda.cMsg.cMsg#subscribeAndGet}
     * with this exact subject, type, and namespace. A count is
     * keep of how many times subscribeAndGet for a particular
     * client has been called. The client info object is the key
     * and count is the value. This is used on the server side.
     */
    private HashMap<cMsgClientInfo, Integer> clientSubAndGetters;

    /**
     * This set contains only regular clients subscribed to this exact subject, type,
     * and namespace and is used on the server side.
     */
    private HashSet<cMsgClientInfo> clientSubscribers;

    //---------------------------------------------------------------------------------------
    // Members associated with regular expressions
    //---------------------------------------------------------------------------------------

    /**
     * Characters which need to be escaped to avoid special interpretation in
     * regular expressions.
     */
    static final private String escapeChars = "\\(){}[]+.|^$";

    /** Array of characters to look for in a given string. */
    static final private String[] lookFor = {"\\", "(", ")", "{", "}", "[",
                                             "]", "+" ,".", "|", "^", "$"};

    /** Array of bar characters to look for in a given string. */
    static final private String[] lookForBar = {"|"};

    /** Array of characters to look for in a given string excluding bar (OR). */
    static final private String[] lookForNoBar = {"\\", "(", ")", "{", "}", "[",
                                                   "]", "+" ,".", "^", "$"};

    /** Length of each item in {@link #lookFor}. */
    static final private int lookForLen = 1;

    /** Array of regexp strings to replace the found special characters in a given string. */
    static final private String[] replaceWith = {"\\\\", "\\(", "\\)", "\\{", "\\}", "\\[",
                                                 "\\]", "\\+" ,"\\.", "\\|", "\\^", "\\$"};

    /** Array of regexp strings to replace the bar in a given string. */
    static final private String[] replaceWithBar = {"\\|"};

    /** Array of regexp strings to replace the found special characters in a given string
     *  excluding bar (OR). */
    static final private String[] replaceWithNoBar = {"\\\\", "\\(", "\\)", "\\{", "\\}", "\\[",
                                                      "\\]", "\\+" ,"\\.", "\\^", "\\$"};

    /** Length of each item in {@link #replaceWith}. */
    static final private int replaceWithLen = 2;

    //---------------------------------------------------------------------------------------
    // Members associated with parsing ranges of numbers in subscriptions' subjects and types
    //---------------------------------------------------------------------------------------

    /** Int representation of < sign. */
    private static final int LT  = 0;
    /** Int representation of > sign. */
    private static final int GT  = 1;
    /** Int representation of = sign. */
    private static final int EQ  = 2;
    /** Int representation of || sign. */
    private static final int OR  = 3;
    /** Int representation of && sign. */
    private static final int AND = 4;

    /**
     * A regular expression string that matches a single part of a subject or type
     * that describes a range of positive integers in a special syntax (described in the
     * javadoc that's a part of this class). The part is captured for further
     * regular expression analysis.
     */
    private static final String exprRange = "\\\\\\{([\\x20i<>=&|[0-9]+]*)\\\\\\}";
    /**
     * A regular expression string that completely matches one part of a subject or type
     * describing a range of positive integers in a special syntax (described in the
     * javadoc that's a part of this class). This determines if the part's format is correct.
     */
    private static final String exprFull = "(?:i|[0-9]+)[<>=](?:i|[0-9]+)(?:[|&](?:i|[0-9]+)[<>=](?:i|[0-9]+))*";
    /**
     * A regular expression string that matches sections of one part of a subject or type
     * describing a range of positive integers in a special syntax (described in the
     * javadoc that's a part of this class). This is designed to capture all information in
     * each section which is stored for use when matching a message's subject and type.
     */
    private static final String exprSec  = "(i|[0-9]+)([<>=])(i|[0-9]+)([|&])*";

    /** Compiled regular expression object of {@link #exprRange}. */
    private static final Pattern patRange = Pattern.compile(exprRange);
    /** Compiled regular expression object of {@link #exprFull}. */
    private static final Pattern patFull = Pattern.compile(exprFull);
    /** Compiled regular expression object of {@link #exprSec}. */
    private static final Pattern patSec  = Pattern.compile(exprSec);

    /** List of lists each of which stores numbers, operators, and conjunctions extracted
     * with {@link #exprSec} for a single section of range in the subject. */
    private List<ArrayList<Integer>> subNumbersList = new LinkedList<ArrayList<Integer>>();

    /** List of lists each of which stores numbers, operators, and conjunctions extracted
     * with {@link #exprSec} for a single section of range in the type. */
    private List<ArrayList<Integer>> typeNumbersList = new LinkedList<ArrayList<Integer>>();



    /**
     * Constructor used for cMsg and rcServer domain subscribeAndGets.
     * @param subject subscription subject
     * @param type subscription type
     */
    public cMsgSubscription(String subject, String type) {
        setSubject(subject);
        setType(type);
    }


    /**
     * Constructor used by cMsg subdomain handler.
     * @param subject subscription subject
     * @param type subscription type
     * @param namespace namespace subscription exists in
     */
    public cMsgSubscription(String subject, String type, String namespace) {
        this.namespace = namespace;

        setSubject(subject);
        setType(type);

        notifiers            = new HashSet<cMsgNotifier>(30);
        allSubscribers       = new HashSet<cMsgClientInfo>(30);
        clientSubAndGetters  = new HashMap<cMsgClientInfo, Integer>(30);
        clientSubscribers    = new HashSet<cMsgClientInfo>(30);
    }


    /**
     * Constructor used by cMsg domain API.
     * @param subject subscription subject
     * @param type subscription type
     * @param id unique intVal referring to subject and type combination
     * @param cbThread object containing callback, its argument, and the thread to run it
     */
    public cMsgSubscription(String subject, String type, int id, cMsgCallbackThread cbThread) {
        this.id = id;

        setSubject(subject);
        setType(type);

        notifiers            = new HashSet<cMsgNotifier>(30);
        clientSubAndGetters  = new HashMap<cMsgClientInfo, Integer>(30);
        allSubscribers       = new HashSet<cMsgClientInfo>(30);
        clientSubscribers    = new HashSet<cMsgClientInfo>(30);
        callbacks            = new ConcurrentHashMap<cMsgCallbackThread,String>(30);
        callbacks.put(cbThread,"");
    }


    /**
     * Sets the subject along with related members such that the wildcards,
     * *, ?, #, and {} are implemented.
     *
     * @param subject subject of subscription
     */
    private void setSubject(String subject) {
        this.subject = subject;
        if (subject == null) return;

        boolean doStar, doQuestion, doPound;

        // Escape regular expression characters, and
        // deal with potential wildcard number range instances.
        if (subject.contains("{") && subject.contains("}")) {
            // 1) This does nothing if no ranges found.
            // 2) Otherwise it sets subjectRegexp to the subject
            //    with escaped regular expression characters if any,
            //    and also substitutes for number range instances.
            //    This will set wildCardsinSub = true in this case.
            createRegexp(subject, true);
        }

        doStar = doQuestion = doPound = false;
        if (subject.contains("*")) doStar = true;
        if (subject.contains("?")) doQuestion = true;
        if (subject.contains("#")) doPound = true;

        // we only need to do the regexp stuff if there are pseudo wildcards chars in subj
        if (doStar || doQuestion || doPound) {
            if (!wildCardsInSub) {
                subjectRegexp = replaceWildcards(subject, doStar, doQuestion, doPound);
                wildCardsInSub = true;
            }
            // retain the parsing we did previously in getRegexp
            else {
                subjectRegexp = replaceWildcards(subjectRegexp, doStar, doQuestion, doPound);
            }
        }

        if (wildCardsInSub) subjectPattern = Pattern.compile(subjectRegexp);
    }

    
    /**
     * Sets the type along with related members such that the wildcards,
     * *, ?, #, and {} are implemented.
     *
     * @param type type of subscription
     */
    private void setType(String type) {
        this.type = type;
        if (type == null) return;

        boolean doStar, doQuestion, doPound;
        if (type.contains("{") && type.contains("}")) {
            createRegexp(type, false);
        }

        doStar = doQuestion = doPound = false;
        if (type.contains("*")) doStar = true;
        if (type.contains("?")) doQuestion = true;
        if (type.contains("#")) doPound = true;

        if (doStar || doQuestion || doPound) {
            if (!wildCardsInType) {
                typeRegexp = replaceWildcards(type, doStar, doQuestion, doPound);
                wildCardsInType = true;
            }
            else {
                typeRegexp = replaceWildcards(typeRegexp, doStar, doQuestion, doPound);
            }
        }

        if (wildCardsInType) typePattern = Pattern.compile(typeRegexp);
    }


    /**
     * This method creates a regular expression from the given string argument.
     * The created string escapes regular expression characters so they will be
     * treated as normal characters.
     * It also changes integer range constructs like {i>5 & i<10 | i=66} into the regular
     * expression ([0-9]+) and stores info allowing the given logic to be applied when this
     * subscription is eventually compared to a message's subject and type.
     * The member variable pairs {@link #subjectRegexp} and  {@link #wildCardsInSub} or
     * {@link #typeRegexp} and  {@link #wildCardsInType} are set if valid range
     * constructs are found.
     * If no valid ranges are found, these variable pairs are left untouched.
     *
     * @param subtyp given string (subject or type) to be transformed to a regular expression
     * @param isSubject true if string arg is subject, else false and string arg is type
     */
    private void createRegexp(String subtyp, boolean isSubject) {
        // Escape everything except the "bar" character,
        // since there may be bars in the range constructs.
        String subtypEscaped = escapeNoBar(subtyp);
//System.out.println("Escaped str = " + subtypEscaped);

        StringBuffer sb = new StringBuffer(subtyp.length()*2);
        Matcher m, matchExprs = patRange.matcher(subtypEscaped);
        boolean foundRangeMatch = false;

        // loop thru each range \{...\}
        mainWhile:
        while (matchExprs.find()) {
            // define list to store all range ints, oerators, conjunctions
            ArrayList<Integer> numbers = new ArrayList<Integer>(20);

            // remove all spaces
            String s = matchExprs.group(1).replaceAll("\\x20", "");
//System.out.println("Match = " + s);

            // is this range \{...\} in the proper format?
            m = patFull.matcher(s);

            // If it is NOT in the proper format, look for special cases
            // like \{\} and \{i\} which by definition mean match any number.
            if (!m.matches()) {
                // Check to see if it is blank or i which matches any number by definition.
                if (s.equals("") || s.equals("i")) {
//System.out.println("Declare a match for s = " + s);
                    numbers.add(1);
                    numbers.add(EQ);
                    numbers.add(1);
                    if (isSubject) {
                        // store list in list of lists
                        subNumbersList.add(numbers);
                    }
                    else {
                        typeNumbersList.add(numbers);
                    }
                    // Replace each range \{...\} in the subject with ([0-9]+)
                    // in order to match an int in messge's subject or type.
                    matchExprs.appendReplacement(sb, "([0-9]+)");
                    foundRangeMatch = true;
                    continue;
                }
//System.out.println("Bad expression, so just ignore it");
                // ignore non-matches so they get treated literally
                continue;
            }

            // examine what's in a properly formatted range \{...\}
            m = patSec.matcher(s);
            while (m.find()) {
                String s1,s2,s3,s4=null;
                s1 = m.group(1);
                s2 = m.group(2);
                s3 = m.group(3);
                if (m.groupCount() >3) s4 = m.group(4);

                // There is still the possibility that the range contains an improper
                // construct such as i>i or 20<10. Be sure to ignore those.
                boolean b1 = s1.equals("i");
                boolean b3 = s3.equals("i");
                if ((b1 && b3) || (!b1 && !b3)) {
                    continue mainWhile;
                }

                // sub range in format # op # (&|)*
                // Substitution will be done for i later (with number from
                // messge's sub/type). Save it for now as -1.
                if (b1) numbers.add(-1);
                else numbers.add(Integer.valueOf(s1));

                if (s2.equals("<")) numbers.add(LT);
                else if (s2.equals(">")) numbers.add(GT);
                else numbers.add(EQ);

                if (b3) numbers.add(-1);
                else numbers.add(Integer.valueOf(s3));

                if (s4 == null) break;
                if (s4.equals("|")) numbers.add(OR);
                else numbers.add(AND);
            }

            if (isSubject) {
                // store list in list of lists
                subNumbersList.add(numbers);
            }
            else {
                typeNumbersList.add(numbers);
            }
            // Replace each range \{...\} in the subject with ([0-9]+)
            // in order to match an int in messge's subject or type.
            matchExprs.appendReplacement(sb, "([0-9]+)");
            foundRangeMatch = true;
        }

        // Found no valid ranges so the variable pairs subjectRegexp and wildCardsInSub
        // or typeRegexp and wildCardsInType are left untouched.
        if (!foundRangeMatch) {
            return;
        }
        // found some valid ranges ...

        // our final regular expression string (bar in our little expression is replaced by this point)
        matchExprs.appendTail(sb);

        if (isSubject) {
            wildCardsInSub = true;
            // escape remaining "bar" chars
            subjectRegexp = escapeBar(sb.toString());
//System.out.println("string regexp = " + subjectRegexp);
        }
        else {
            wildCardsInType = true;
            // escape remaining "bar" chars
            typeRegexp = escapeBar(sb.toString());
//System.out.println("string regexp = " + typeRegexp);
        }
        
        return;
    }


    /**
     * This method takes a string and escapes regular expression characters
     * with the exception of * and ? which are untouched.
     *
     * @param s string to be escaped
     * @return escaped string
     */
    static public String escape(String s) {
        return escapeString(s, lookFor, replaceWith);
    }

    /**
     * This method takes a string and escapes regular expression characters
     * with the exception of *, ?, and | which are untouched.
     *
     * @param s string to be escaped
     * @return escaped string
     */
    static public String escapeNoBar(String s) {
        return escapeString(s, lookForNoBar, replaceWithNoBar);
    }

    /**
     * This method takes a string and escapes | (bar (OR)).
     *
     * @param s string to be escaped
     * @return escaped string
     */
    static public String escapeBar(String s) {
        return escapeString(s, lookForBar, replaceWithBar);
    }

    /**
     * This method takes a given String and replaces each occurrence of any of a set of
     * Strings that may be contained within the given String with a corresponding replacement
     * String.
     * Internally, using a StringBuilder object is 37% faster than using String objects.
     * A hidden assumption is that all looked-for strings are of length {@link #lookForLen} and
     * each replacement string is of length {@link #replaceWithLen}.
     *
     * @param s string to be modified
     * @param lookFor array of strings to look for
     * @param replaceWith array of strings to replace what was looked for (same index)
     * @return escaped string
     */
    static private String escapeString(String s, String[] lookFor, String[] replaceWith) {
        if (s == null) return null;

        // StringBuilder is a nonsynchronized StringBuffer class.
        StringBuilder buf = new StringBuilder(s.length()*2);   // allow some room for growth
        buf.append(s);
        int lastIndex;

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
                lastIndex += replaceWithLen;
            }
        }
        return buf.toString();
    }

    /**
     * This method replaces pseudo regular expression characters *, ?, and # with actual
     * regular expressions .*, .{1}, and [0-9]* respectively.
     * Internally, using a StringBuilder object is 37% faster than using String objects.
     *
     * @param s string to be modified
     * @param doStar      if true, turn * into .*
     * @param doQuestion  if true, turn ? into .{1}
     * @param doPound     if true, turn # into [0-9]*
     * @return regular expression string
     */
    static private String replaceWildcards(String s, boolean doStar, boolean doQuestion, boolean doPound) {
        // StringBuilder is a nonsynchronized StringBuffer class.
        StringBuilder buf = new StringBuilder(s.length()*2);   // allow some room for growth
        buf.append(s);
        int lastIndex;

        if (doStar) {
            // Check for "*" characters and replace with ".*"
            lastIndex = 0;
            for (int j = 0; j < buf.length(); j++) {
                lastIndex = buf.indexOf("*", lastIndex);
                if (lastIndex < 0) {
                    break;
                }
                buf.replace(lastIndex, lastIndex + 1, ".*");
                lastIndex += 2;
            }
        }

        if (doQuestion) {
            // Check for "?" characters and replace with ".{1}"
            lastIndex = 0;
            for (int j = 0; j < buf.length(); j++) {
                lastIndex = buf.indexOf("?", lastIndex);
                if (lastIndex < 0) {
                    break;
                }
                buf.replace(lastIndex, lastIndex + 1, ".{1}");
                lastIndex += 4;
            }
        }

        if (doPound) {
            // Check for "#" characters and replace with "[0-9]*"
            lastIndex = 0;
            for (int j = 0; j < buf.length(); j++) {
                lastIndex = buf.indexOf("#", lastIndex);
                if (lastIndex < 0) {
                    break;
                }
                buf.replace(lastIndex, lastIndex + 1, "[0-9]*");
                lastIndex += 6;
            }
        }

        return buf.toString();
    }


    /**
     * This method checks to see if there is a match between a given string
     * and this subscription's subject or type using regular expressions.
     * This subscription's subject and type may include wildcards where "*" means any or
     * no characters, "?" means exactly 1 character, "#" means 1 or no positive integer,
     * and a defined range (eg. {i>5 & i<10 | i=66}) matches 1 integer and applies the given
     * logic to see if there is a match with that integer.
     *
     * @param subtyp subject or type
     * @param isSubject true if subtyp is subject, else subtyp is type
     * @return true if subtyp matches the appropriate regular expression, false if it doesn't
     */
    private boolean matchesRegexp(String subtyp, boolean isSubject) {

        int numRanges;
        Matcher matcher = null;
        if (isSubject) {
            matcher   = subjectPattern.matcher(subtyp);
            numRanges = subNumbersList.size();
        }
        else {
            matcher   = typePattern.matcher(subtyp);
            numRanges = typeNumbersList.size();
        }

        // Does string match regular expression?
        if (!matcher.matches()) {
//            if (isSubject)
//                System.out.println("There is no match between regexp " + subjectRegexp + " and string " + subtyp);
//            else
//                System.out.println("There is no match between regexp " + typeRegexp + " and string " + subtyp);
            return false;
        }

//System.out.println("groupCount = " + matcher.groupCount() + ", numRanges = " + numRanges);
        if (matcher.groupCount() != numRanges) {
            System.out.println("Internal error: mismatch between # of integers grabbed and # of integer ranges");
            return false;
        }

        // If there is a match with the regular expression, there is one more test to apply.

        // If groupCount > 0, it means we have extracted numbers that must be run through
        // the logic given in the subscription's subject & type defined ranges in the form {i<5} etc.
        // So now run through that logic to see if there is a match.
        if (matcher.groupCount() > 0) {
            String s;
            Integer conj;
            int num1, num2, oper, myNum;
            boolean lastBool;

            // Each group is a just a single positive integer taken from the message's subject or type.
            // This int must be run through the previously parsed pseudo wildcard ranges of the form like
            // {i>5 & 10<i | i=66}, taken from the subscription's subject and type.
            for (int i = 1; i <= matcher.groupCount(); i++) {
                s = matcher.group(i);
                try {
                    // positive int parsed from message's subject or type
                    myNum = Integer.parseInt(s);

//System.out.println("Does " + s + " pass the #" + i + " test?  ");
                    conj = null;
                    lastBool = true;

                    int numIndex=0;
                    // all range ints, operators, conjunctions
                    ArrayList<Integer> numbers;

                    if (isSubject) {
                        numbers = subNumbersList.get(i-1);
                    }
                    else {
                        numbers = typeNumbersList.get(i-1);
                    }
//System.out.println("Size of numbers list = " + numbers.size());
                    while (numIndex < numbers.size()) {
                        boolean myBool;

                        num1 = numbers.get(numIndex++);
                        if (num1 == -1) num1 = myNum;

                        oper = numbers.get(numIndex++);

                        num2 = numbers.get(numIndex++);
                        if (num2 == -1) num2 = myNum;
//System.out.print("    num1 = " + num1 + ", num2 = " + num2);

                        if (oper == GT) {
                            myBool = (num1 > num2);
//System.out.println(", op is >");
                        }
                        else if (oper == LT) {
                            myBool = (num1 < num2);
//System.out.println(", op is <");
                        }
                        else {
                            myBool = (num1 == num2);
//System.out.println(", op is =");
                        }
//System.out.println("    Before conj myBool = " + myBool);

                        if (conj != null) {
                            if (conj == AND) {
                                myBool = myBool && lastBool;
//System.out.println("    conj = &&");
                            }
                            else {
                                myBool = myBool || lastBool;
//System.out.println("    conj = ||");
                            }
                        }
                        else {
//System.out.println("    no conj");
                        }
//System.out.println("    After  conj myBool = " + myBool);
                        lastBool = myBool;

                        if (numIndex < numbers.size()) {
                            conj = numbers.get(numIndex++);
                        }
                    }
//if (lastBool) System.out.println("YES\n");
//else System.out.println("NO\n");
                }
                catch (NumberFormatException e) {
//System.out.println("ERROR: " + e.getMessage());
                    return false;
                }

//System.out.println("Final BOOLEAN = " + lastBool);
                if (!lastBool) return false;
            } // for each captured group
        }

        return true;
    }


    /**
     * This method checks to see if there is a match between a given (message's) subject & type
     * pair and this subscription's subject and type. If this subscription's subject and
     * type has no wildcards characters, then a straight string compare is done.
     * This subscription's subject and type may include wildcards where "*" means any or
     * no characters, "?" means exactly 1 character, "#" means 1 or no positive integer,
     * and a defined range (eg. {i>5 & i<10 | i=66}) matches 1 integer and applies the given
     * logic to see if there is a match with that integer. If such wildcards exist,
     * a match using regular expressions is done.
     *
     * @param msgSubject subject
     * @param msgType type
     * @return true if there is a match of both subject and type, false if there is not
     */
    public boolean matches(String msgSubject, String msgType) {
        // first check for null subjects/types in subscription
        if (subject == null || type == null) return false;

        // first see if it's stored in the set of strings known to match
        if (!subjectMatches.contains(msgSubject)) {
            // else if there are no wildcards in the subscription's subject, just use string compare
            if (!wildCardsInSub) {
                if (!msgSubject.equals(subject)) return false;
            }
            // else if there are wildcards in the subscription's subject, use regexp matching
            else {
//System.out.println("Wildcards in subject, use regexps for matching");
                if (!matchesRegexp(msgSubject, true)) return false;
            }
//System.out.println("Msg subject (" + msgSubject + ") matches regexp (" + subjectRegexp + ")");

            // first check to see if our hashset is getting too big
            if (subjectMatches.size() > 50) {
                // clear things out
                subjectMatches.clear();
            }

            // add to set since it matches
            subjectMatches.add(msgSubject);
        }
//        else {
//            System.out.println("Msg subject (" + msgSubject + ") matches - in hashset");
//        }

        // first see if it's stored in the set of strings known to match
        if (!typeMatches.contains(msgType)) {
            // else if there are no wildcards in the subscription's subject, just use string compare
            if (!wildCardsInType) {
                if (!msgType.equals(type)) return false;
            }
            // else if there are wildcards in the subscription's type, use regexp matching
            else {
//System.out.println("Wildcards in type, use regexps for matching");
                if (!matchesRegexp(msgType, false)) return false;
            }
//System.out.println("Msg type (" + msgType + ") matches regexp (" + typeRegexp + ")");

            // first check to see if our hashset is getting too big
            if (typeMatches.size() > 50) {
                // clear things out
                typeMatches.clear();
            }

            // add to set since it matches
            typeMatches.add(msgType);
        }
//        else {
//            System.out.println("Msg type (" + msgType + ") matches - in hashset");
//        }

        return true;
    }


    /**
     * This method implements a simple wildcard matching scheme where "*" means
     * any or no characters, "?" means exactly 1 character, and "#" means
     * 1 or no positive integer.
     *
     * @param regexp subscription string that can contain wildcards (*, ?, #), null matches everything
     * @param s message string to be matched (blank only matches *, null matches nothing)
     * @param escapeRegexp if true, the regexp argument has regular expression chars escaped,
     *                     and *, ?, # chars translated to real java regular expression syntax, else not
     * @return true if there is a match, false if there is not
     */
    static public boolean matches(String regexp, String s, boolean escapeRegexp) {
        // It's a match if regexp (subscription string) is null
        if (regexp == null) return true;

        // If the message's string is null, something's wrong
        if (s == null) return false;

        // The first order of business is to take the regexp arg and modify it so that it is
        // a regular expression that Java can understand. This means subbing all occurrences
        // of "*", "?", and # with ".*", ".{1}", and "[0-9]*". And it means escaping other regular
        // expression special characters.
        // Sometimes regexp is already escaped, so no need to redo it.
        if (escapeRegexp) {
            // escape reg expression chars except *, ?
            regexp = cMsgSubscription.escape(regexp);
            // substitute real java regexps for psuedo wildcards
            regexp = replaceWildcards(regexp, true, true, true);
        }

        // Now see if there's a match with the string arg
        return s.matches(regexp);
    }


    /**
     * This method checks to see if there is a match between a given msgSubject & msgType
     * pair and this subscription's msgSubject and msgType. The subscription's msgSubject and
     * msgType may include wildcards where "*" means any or no characters and "?" means
     * exactly 1 character. There is a match only if both msgSubject and msgType strings match
     * their conterparts in this subscription.
     *
     * @param msgSubject msgSubject
     * @param msgType msgType
     * @return true if there is a match of both msgSubject and msgType, false if there is not
     */
    public boolean matchesOrig(String msgSubject, String msgType) {
        // first check for null subjects/types in this subscription
        if (subject == null || type == null) return false;

        // if there are no wildcards in this subscription's subject, just use string compare
        if (!wildCardsInSub) {
            if (!msgSubject.equals(subject)) return false;
        }
        // else if there are wildcards in the subscription's msgSubject, use regexp matching
        else {
            Matcher m = subjectPattern.matcher(msgSubject);
            if (!m.matches()) return false;
        }

        if (!wildCardsInType) {
            if (!msgType.equals(type)) return false;
        }
        else {
            Matcher m = typePattern.matcher(msgType);
            if (!m.matches()) return false;
        }
        return true;
    }


    /** This method prints the sizes of all sets and maps. */
    public void printSizes() {
        System.out.println("        notifiers           = " + notifiers.size());
        if (callbacks != null) {
            System.out.println("        callbacks           = " + callbacks.size());
        }
        System.out.println("        allSubscribers      = " + allSubscribers.size());
        System.out.println("        clientSubscribers   = " + clientSubscribers.size());
        System.out.println("        clientSubAndGetters = " + clientSubAndGetters.size());
    }

    /**
     * Returns true if there are * or ? characters in subject.
     * @return true if there are * or ? characters in subject.
     */
    public boolean areWildCardsInSub() {
        return wildCardsInSub;
    }


    /**
     * Returns true if there are * or ? characters in type.
     * @return true if there are * or ? characters in type.
     */
    public boolean areWildCardsInType() {
        return wildCardsInType;
    }


    /**
     * Gets subject subscribed to.
     * @return subject subscribed to
     */
    public String getSubject() {
        return subject;
    }

    /**
     * Gets subject turned into regular expression that understands * and ?.
     * @return subject subscribed to in regexp form
     */
    public String getSubjectRegexp() {
        return subjectRegexp;
    }

    /**
     * Gets subject turned into compiled regular expression pattern.
     * @return subject subscribed to in compiled regexp form
     */
    public Pattern getSubjectPattern() {
        return subjectPattern;
    }

    /**
     * Gets type subscribed to.
     * @return type subscribed to
     */
    public String getType() {
        return type;
    }

    /**
     * Gets type turned into regular expression that understands * and ?.
     * @return type subscribed to in regexp form
     */
     public String getTypeRegexp() {
         return typeRegexp;
     }

    /**
     * Gets type turned into compiled regular expression pattern.
     * @return type subscribed to in compiled regexp form
     */
    public Pattern getTypePattern() {
        return typePattern;
    }

    /**
     * Gets the intVal which client generates (receiverSubscribeId).
     * @return receiverSubscribeId
     */
    public int getIntVal() {
        return id;
    }

    /**
     * Sets the intVal which client generates (receiverSubscribeId).
     * @param id intVal which client generates (receiverSubscribeId)
     */
    public void setIntVal(int id) {
        this.id = id;
    }

    /**
     * Gets the namespace in the cMsg subdomain in which this subscription resides.
     * @return namespace subscription resides in
     */
    public String getNamespace() {
        return namespace;
    }

    /**
     * Sets the namespace in the cMsg subdomain in which this subscription resides.
     * @param namespace namespace subscription resides in
     */
    public void setNamespace(String namespace) {
        this.namespace = namespace;
    }

    //-----------------------------------------------------
    // Methods for dealing with callbacks
    //-----------------------------------------------------

    /**
     * Gets the set storing callback threads.
     * @return set storing callback threads
     */
    public Set<cMsgCallbackThread> getCallbacks() {
        return callbacks.keySet();
    }


    /**
     * Method to add a callback thread.
     * @param cbThread  object containing callback thread, its argument,
     *                  and the thread to run it
     */
    public void addCallback(cMsgCallbackThread cbThread) {
        callbacks.put(cbThread,"");
    }


    /**
     * Method to remove a callback thread.
     * @param cbThread  object containing callback thread to be removed
     */
    public void removeCallback(cMsgCallbackThread cbThread) {
        callbacks.remove(cbThread);
    }


    /**
     * Method to return the number of callback threads registered.
     * @return number of callback registered
     */
    public int numberOfCallbacks() {
        return callbacks.size();
    }


    //-------------------------------------------------------------------------
    // Methods for dealing with clients subscribed to the sub/type/ns
    //-------------------------------------------------------------------------

    /**
     * Gets the set containing only regular clients subscribed to this subject, type,
     * and namespace. Used only on the server side.
     *
     * @return set containing only regular clients subscribed to this subject, type,
     *         and namespace
     */
    public HashSet<cMsgClientInfo> getClientSubscribers() {
        return clientSubscribers;
    }

    /**
     * Adds a client to the set containing only regular clients subscribed to this subject, type,
     * and namespace. Used only on the server side.
     *
     * @param client regular client to be added to subscription
     * @return true if set did not already contain client, else false
     */
    public boolean addClientSubscriber(cMsgClientInfo client) {
        return clientSubscribers.add(client);
    }

    /**
     * Removes a client from the set containing only regular clients subscribed to this subject, type,
     * and namespace. Used only on the server side.
     *
     * @param client regular client to be removed from subscription
     * @return true if set contained client, else false
     */
    public boolean removeClientSubscriber(cMsgClientInfo client) {
        return clientSubscribers.remove(client);
    }


    //-------------------------------------------------------------------------
    // Methods for dealing with clients & servers subscribed to the sub/type/ns
    //-------------------------------------------------------------------------

    /**
     * Gets the set containing all clients (regular and bridge) subscribed to this
     * subject, type, and namespace. Used only on the server side.
     *
     * @return set containing all clients (regular and bridge) subscribed to this subject, type,
     *         and namespace
     */
    public HashSet<cMsgClientInfo> getAllSubscribers() {
        return allSubscribers;
    }

    /**
     * Is this a subscription of the given client (regular or bridge)?
     * Used only on the server side.
     *
     * @param client client
     * @return true if this is a subscription of the given client, else false
     */
    public boolean containsSubscriber(cMsgClientInfo client) {
        return allSubscribers.contains(client);
    }

    /**
     * Adds a client to the set containing all clients (regular and bridge) subscribed
     * to this subject, type, and namespace. Used only on the server side.
     *
     * @param client client to be added to subscription
     * @return true if set did not already contain client, else false
     */
    public boolean addSubscriber(cMsgClientInfo client) {
        return allSubscribers.add(client);
    }

    /**
     * Removes a client from the set containing all clients (regular and bridge) subscribed
     * to this subject, type, and namespace. Used only on the server side.
     *
     * @param client client to be removed from subscription
     * @return true if set contained client, else false
     */
    public boolean removeSubscriber(cMsgClientInfo client) {
        return allSubscribers.remove(client);
    }


    //-------------------------------------------------------------------------------
    // Methods for dealing with clients who subscribeAndGet to the sub/type/namespace
    //-------------------------------------------------------------------------------

    /**
     * Gets the hashmap containing all regular clients who have
     * called {@link org.jlab.coda.cMsg.cMsg#subscribeAndGet}
     * with this subject, type, and namespace. A count is
     * keep of how many times subscribeAndGet for a particular
     * client has been called. The client info object is the key
     * and count is the value. This is used on the server side.
     *
     * @return hashmap containing all regular clients who have
     *         called {@link org.jlab.coda.cMsg.cMsg#subscribeAndGet}
     */
    public HashMap<cMsgClientInfo, Integer> getSubAndGetters() {
        return clientSubAndGetters;
    }

    /**
     * Adds a client to the hashmap containing all regular clients who have
     * called {@link org.jlab.coda.cMsg.cMsg#subscribeAndGet}.
     * This is used on the server side.
     *
     * @param client client who called subscribeAndGet
     */
    public void addSubAndGetter(cMsgClientInfo client) {
//System.out.println("      SUB: addSub&Getter arg = " + client);
        Integer count = clientSubAndGetters.get(client);
        if (count == null) {
//System.out.println("      SUB: set sub&Getter cnt to 1");
            clientSubAndGetters.put(client, 1);
        }
        else {
//System.out.println("      SUB: set sub&Getter cnt to " + (count + 1));
            clientSubAndGetters.put(client, count + 1);
        }
    }

    /**
     * Clears the hashmap containing all regular clients who have
     * called {@link org.jlab.coda.cMsg.cMsg#subscribeAndGet}.
     * This is used on the server side.
     */
    public void clearSubAndGetters() {
        clientSubAndGetters.clear();
    }

    /**
     * Removes a client from the hashmap containing all regular clients who have
     * called {@link org.jlab.coda.cMsg.cMsg#subscribeAndGet}. If the client has
     * called subscribeAndGet more than once, it's entry in the table stays but
     * it's count of active subscribeAndGet calls is decremented.
     * This is used on the server side.
     *
     * @param client client who called subscribeAndGet
     */
    public void removeSubAndGetter(cMsgClientInfo client) {
        Integer count = clientSubAndGetters.get(client);
//System.out.println("      SUB: removeSub&Getter: count = " + count);
        if (count == null || count < 2) {
//System.out.println("      SUB: remove sub&Getter completely (count = 0)");
            clientSubAndGetters.remove(client);
        }
        else {
//System.out.println("      SUB: reduce sub&Getter cnt to " + (count - 1));
            clientSubAndGetters.put(client, count - 1);
        }
    }

    /**
     * Returns the total number of subscribers and subscribeAndGet callers
     * subscribed to this subject, type, and namespace.
     * @return the total number of subscribers and subscribeAndGet callers
     *         subscribed to this subject, type, and namespace
     */
    public int numberOfSubscribers() {
        return (allSubscribers.size() + clientSubAndGetters.size());
    }


    //--------------------------------------------------------------------------
    // Methods for dealing with servers' notifiers of subscribeAndGet completion
    //--------------------------------------------------------------------------

    /**
     * Gets the set of objects used to notify servers that their subscribeAndGet is complete.
     *
     * @return set of objects used to notify servers that their subscribeAndGet is complete
     */
    public Set<cMsgNotifier> getNotifiers() {
        return notifiers;
    }

    /**
     * Add a notifier object to the set of objects used to notify servers that their
     * subscribeAndGet is complete.
     *
     * @param notifier notifier object
     */
    public void addNotifier(cMsgNotifier notifier) {
        notifiers.add(notifier);
    }

    /**
     * Remove a notifier object from the set of objects used to notify servers that their
     * subscribeAndGet is complete.
     *
     * @param notifier  notifier object
     */
    public void removeNotifier(cMsgNotifier notifier) {
        notifiers.remove(notifier);
    }

    /**
     * Clear all notifier objects from the set of objects used to notify servers that their
     * subscribeAndGet is complete.
     */
    public void clearNotifiers() {
        notifiers.clear();
    }

 }

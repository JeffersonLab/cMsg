/*---------------------------------------------------------------------------*
*  Copyright (c) 2007        Jefferson Science Associates,                   *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    C.Timmer, 19-Dec-2007, Jefferson Lab                                    *
*                                                                            *
*    Authors: Carl Timmer                                                    *
*             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
*             Phone: (757) 269-5130             12000 Jefferson Ave.         *
*             Fax:   (757) 269-6248             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;

import org.jlab.coda.cMsg.common.Base64;

import java.io.Serializable;
import java.math.BigInteger;
import java.lang.Number;
import java.util.Collection;
import java.util.Arrays;

/**
 * This class represents an item in a cMsg message's payload.
 * The value of each item is stored in this class along with a text
 * representation of that item. Following is the text format of various
 * types of payload items where [nl] means newline.<p/>
 *
 * Note that there is only 1 space or newline between all entries. The only exception
 * to the 1 space spacing is between the last two entries on each "header" line (the line
 * that contains the item_name). There may be several spaces between the last 2
 * entries on these lines.<p/>
 *
 *<b><i>for (arrays of) string items:</i></b><p/>
 *<pre>    item_name   item_type   item_count   isSystemItem?   item_length[nl]
 *    string_length_1[nl]
 *    string_characters_1[nl]
 *     .
 *     .
 *     .
 *    string_length_N[nl]
 *    string_characters_N</pre><p/>
 *
 *<b><i>for (arrays of) binary (converted into text) items:</i></b><p>
 *<pre>    item_name   item_type   item_count   isSystemItem?   item_length[nl]
 *    string_length_1   original_binary_byte_length_1   endian_1[nl]
 *    string_characters_1[nl]</pre><p>
 *     .
 *     .
 *     .
 *    string_length_N   original_binary_byte_length_N   endian_N[nl]
 *    string_characters_N</pre><p>
 *
 *<b><i>for primitive type items:</i></b><p/>
 *<pre>    item_name   item_type   item_count   isSystemItem?   item_length[nl]
 *    value_1   value_2   ...   value_N[nl]</pre><p/>
 *
 *<b>A cMsg message is formatted as a compound payload. Each message has
 *   a number of fields (payload items).<p/>
 *
 *  <i>for message items:</i></b><p/>
 *<pre>                                                                            _
 *    item_name   item_type   item_count   isSystemItem?   item_length[nl]   /
 *    message_1_in_compound_payload_text_format[nl]                         <  field_count[nl]
 *        .                                                                  \ list_of_payload_format_items
 *        .                                                                   -
 *        .
 *    message_N_in_compound_payload_text_format[nl]</pre>
 *
 */
public final class cMsgPayloadItem implements Cloneable, Serializable {

    /** Name of this item. */
    String name;

    /** String representation for this item, containing name,
      * type, count, length, values, etc for wire protocol. */
    String text;

    /**
     * Type of item (number, bin, string, msg, ...) stored here.
     * The type may have the following values:
     * <UL>
     * <LI>{@link cMsgConstants#payloadStr}         for a   String
     * <LI>{@link cMsgConstants#payloadFlt}         for a   4 byte float
     * <LI>{@link cMsgConstants#payloadDbl}         for an  8 byte float
     * <LI>{@link cMsgConstants#payloadInt8}        for an  8 bit int
     * <LI>{@link cMsgConstants#payloadInt16}       for a  16 bit int
     * <LI>{@link cMsgConstants#payloadInt32}       for a  32 bit int
     * <LI>{@link cMsgConstants#payloadInt64}       for a  64 bit int
     * <LI>{@link cMsgConstants#payloadUint8}       for an unsigned  8 bit int
     * <LI>{@link cMsgConstants#payloadUint16}      for an unsigned 16 bit int
     * <LI>{@link cMsgConstants#payloadUint32}      for an unsigned 32 bit int
     * <LI>{@link cMsgConstants#payloadUint64}      for an unsigned 64 bit int
     * <LI>{@link cMsgConstants#payloadMsg}         for a  cMsg message
     * <LI>{@link cMsgConstants#payloadBin}         for    binary
     * <p/>
     * <LI>{@link cMsgConstants#payloadStrA}        for a   String array
     * <LI>{@link cMsgConstants#payloadFltA}        for a   4 byte float array
     * <LI>{@link cMsgConstants#payloadDblA}        for an  8 byte float array
     * <LI>{@link cMsgConstants#payloadInt8A}       for an  8 bit int array
     * <LI>{@link cMsgConstants#payloadInt16A}      for a  16 bit int array
     * <LI>{@link cMsgConstants#payloadInt32A}      for a  32 bit int array
     * <LI>{@link cMsgConstants#payloadInt64A}      for a  64 bit int array
     * <LI>{@link cMsgConstants#payloadUint8A}      for an unsigned  8 bit int array
     * <LI>{@link cMsgConstants#payloadUint16A}     for an unsigned 16 bit int array
     * <LI>{@link cMsgConstants#payloadUint32A}     for an unsigned 32 bit int array
     * <LI>{@link cMsgConstants#payloadUint64A}     for an unsigned 64 bit int array
     * <LI>{@link cMsgConstants#payloadMsgA}        for a  cMsg message array
     * <LI>{@link cMsgConstants#payloadBinA}        for an array of binary items
     * </UL>
     */
    private int type;

    /** If type was originally unsigned and then promoted in Java, this stores that unsigned type. */
    private int originalType;

    /** If type was originally unsigned and then promoted in Java, this stores the
      * original string representation for this item. */
    private String originalText;

    /** Number of items in array if array, else 1. */
    private int count = 1;

    /** Length of text in chars without header (first) line. */
    private int noHeaderLen;

    /** Endian value if item is binary. */
    private int endian = cMsgConstants.endianBig;

    /** Endian values if item is array of binary arrays. */
    private int[] endianArray;

    /** Is this item part of a hidden system field in the payload? */
    private boolean isSystem;

    /** Place to store the item passed to the constructor. */
    private Object item;

    /** Maximum length of the name. */
    private static final int CMSG_PAYLOAD_NAME_LEN_MAX = 128;

    /** String containing all characters not allowed in a name. */
    private static final String EXCLUDED_CHARS = " \t\n`\'\"";

    /** Map the value of a byte (index) to the hex string value it represents. */
    private static final String toASCII[] =
    {"00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "0a", "0b", "0c", "0d", "0e", "0f",
     "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "1a", "1b", "1c", "1d", "1e", "1f",
     "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "2a", "2b", "2c", "2d", "2e", "2f",
     "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "3a", "3b", "3c", "3d", "3e", "3f",
     "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "4a", "4b", "4c", "4d", "4e", "4f",
     "50", "51", "52", "53", "54", "55", "56", "57", "58", "59", "5a", "5b", "5c", "5d", "5e", "5f",
     "60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "6a", "6b", "6c", "6d", "6e", "6f",
     "70", "71", "72", "73", "74", "75", "76", "77", "78", "79", "7a", "7b", "7c", "7d", "7e", "7f",
     "80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "8a", "8b", "8c", "8d", "8e", "8f",
     "90", "91", "92", "93", "94", "95", "96", "97", "98", "99", "9a", "9b", "9c", "9d", "9e", "9f",
     "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9", "aa", "ab", "ac", "ad", "ae", "af",
     "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7", "b8", "b9", "ba", "bb", "bc", "bd", "be", "bf",
     "c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7", "c8", "c9", "ca", "cb", "cc", "cd", "ce", "cf",
     "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "da", "db", "dc", "dd", "de", "df",
     "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7", "e8", "e9", "ea", "eb", "ec", "ed", "ee", "ef",
     "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "fa", "fb", "fc", "fd", "fe", "ff"};


    /**
     * Clone this object.
     * @return a cMsgPayloadItem object which is a copy of this object
     */
    public Object clone() {
        try {
            cMsgPayloadItem result = (cMsgPayloadItem) super.clone();
            if (endianArray != null) {
                result.endianArray = endianArray.clone();
            }
            // The problem is how to clone the item object.
            // If it's a String, Byte, Short, Integer, Long, Float, Double,
            // or BigInteger, it's immutable and doesn't need explicit cloning.
            // All other types need to be cloned explicitly.
            if      (type == cMsgConstants.payloadBin)     { result.item = ((byte[])       item).clone(); }
            else if (type == cMsgConstants.payloadStrA)    { result.item = ((String[])     item).clone(); }
            else if (type == cMsgConstants.payloadDblA)    { result.item = ((double[])     item).clone(); }
            else if (type == cMsgConstants.payloadFltA)    { result.item = ((float[])      item).clone(); }
            else if (type == cMsgConstants.payloadInt8A)   { result.item = ((byte[])       item).clone(); }
            else if (type == cMsgConstants.payloadInt16A)  { result.item = ((short[])      item).clone(); }
            else if (type == cMsgConstants.payloadInt32A)  { result.item = ((int[])        item).clone(); }
            else if (type == cMsgConstants.payloadInt64A)  { result.item = ((long[])       item).clone(); }
            else if (type == cMsgConstants.payloadUint64A) { result.item = ((BigInteger[]) item).clone(); }
            else if (type == cMsgConstants.payloadMsg)     { result.item = ((cMsgMessage)  item).clone(); }
            else if (type == cMsgConstants.payloadMsgA)    {
                cMsgMessage[] msgs = (cMsgMessage[]) item;
                result.item = new cMsgMessage[count];
                for (int i = 0; i < msgs.length; i++) {
                    ((cMsgMessage[])result.item)[i] = (cMsgMessage) msgs[i].clone();
                }
            }
            else if (type == cMsgConstants.payloadBinA)    {
                byte[][] bb = (byte[][]) item;
                result.item = new byte[count][];
                for (int i = 0; i < bb.length; i++) {
                    ((byte[][])result.item)[i] = bb[i].clone();
                }
            }
            return result;
        }
        catch (CloneNotSupportedException e) {
            return null; // never invoked
        }
    }


    /**
     * Creates a complete copy of this object.
     *
     * @return copy of this object.
     */
    public cMsgPayloadItem copy() {
        return (cMsgPayloadItem) this.clone();
    }


    /**
     * This method returns the number of characters needed to represent an integer including a minus sign.
     *
     * @param number integer
     * @return number of characters needed to represent integer argument including minus sign
     */
    static int numDigits(long number) {
      int digits = 1;
      long step = 10;

      if (number < 0) {
        digits++;
        number *= -1;
      }

      while (step <= number) {
          digits++;
          step *= 10;
      }
      return digits;
    }


    /**
     * This method returns the number of characters needed to represent an integer
     * including a minus sign for large numbers including unsigned 64 bit.
     *
     * @param number integer
     * @return number of characters needed to represent integer argument including minus sign
     */
    static int numDigits(BigInteger number) {
      int digits = 1;
      BigInteger step = BigInteger.TEN;
      BigInteger minus1 = new BigInteger("-1");

      if (number.compareTo(BigInteger.ZERO) < 0) {
        digits++;
        number = number.multiply(minus1);
      }

      while ( !(step.compareTo(number) > 0) ) {
          digits++;
          step = step.multiply(BigInteger.TEN);
      }
      return digits;
    }


    /**
     * This method checks a string to see if it is a valid payload item name.
     * It throws an exeception if it is not. A check is made to see if
     * it contains any character from a list of excluded characters.
     * All names starting with "cmsg", independent of case,
     * are reserved for use by the cMsg system itself. Names may not be
     * longer than CMSG_PAYLOAD_NAME_LEN_MAX characters.
     *
     * @param name string to check
     * @param isSystem if true, allows names starting with "cmsg", else not
     *
     * @throws cMsgException if name is null, contains illegal characters, starts with
     *                       "cmsg" if not isSystem, or is too long
     */
    static private void validName(String name, boolean isSystem) throws cMsgException {
        if (name == null) {
            throw new cMsgException("name argument is null");
        }

        // check for excluded chars
        for (char c : EXCLUDED_CHARS.toCharArray()) {
            if (name.indexOf(c) > -1) {
                throw new cMsgException("name contains illegal character \"" + c + "\"");
            }
        }

        // check for length
        if (name.length() > CMSG_PAYLOAD_NAME_LEN_MAX) {
            throw new cMsgException("name too long, " + CMSG_PAYLOAD_NAME_LEN_MAX + " chars allowed" );
        }

        // check for starting with cmsg
        if (!isSystem && name.toLowerCase().startsWith("cmsg")) {
            throw new cMsgException("names may not start with \"cmsg\"");
        }
    }


    /**
     * This method checks a string to see if it is a valid payload item name starting with "cmsg",
     * independent of case, reserved for use by the cMsg system itself. Names may not be
     * longer than CMSG_PAYLOAD_NAME_LEN_MAX characters.
     *
     * @param name string to check
     * @return true if string is a valid system name, else false
     */
    static boolean validSystemName(String name) {
        if (name == null) {
            return false;
        }

        // check for excluded chars
        for (char c : EXCLUDED_CHARS.toCharArray()) {
            if (name.indexOf(c) > -1) {
                return false;
            }
        }

        // check for length
        if (name.length() > CMSG_PAYLOAD_NAME_LEN_MAX) {
            return false;
        }

        // check for starting with cmsg
        return name.toLowerCase().startsWith("cmsg");
    }


    /**
     * This method checks an integer to make sure it has the proper value for an endian argument --
     * {@link cMsgConstants#endianBig}, {@link cMsgConstants#endianLittle},
     * {@link cMsgConstants#endianLocal}, or {@link cMsgConstants#endianNotLocal}.
     * It returns the final endian value of either {@link cMsgConstants#endianBig} or
     * {@link cMsgConstants#endianLittle}. In other words, {@link cMsgConstants#endianLocal}, or
     * {@link cMsgConstants#endianNotLocal} are transformed to big or little.
     *
     * @param endian value to check
     *
     * @return either {@link cMsgConstants#endianBig} or {@link cMsgConstants#endianLittle}
     * @throws cMsgException if endian is an improper value
     */
    static private int checkEndian(int endian) throws cMsgException {
        if (endian != cMsgConstants.endianBig    &&
            endian != cMsgConstants.endianLittle &&
            endian != cMsgConstants.endianLocal  &&
            endian != cMsgConstants.endianNotLocal) {
            throw new cMsgException("endian value must be cMsgConstants.Big/Little/Local/NotLocal");
        }
        if (endian == cMsgConstants.endianLittle || endian == cMsgConstants.endianNotLocal) {
            return cMsgConstants.endianLittle;
        }
        return cMsgConstants.endianBig;
    }

    /**
     * This method changes an integer value into a string of 8 hex characters starting with
     * "Z" in order to represent a number of zeros in our simple zero-compression scheme.
     * This only makes sense for positive integers.
     *
     * @param sb StringBuilder object into which the characters are written
     * @param zeros the number of zeros to be encoded/compressed
     */
    static private void zerosToIntStr(StringBuilder sb, int zeros) {
        sb.append("Z");
        sb.append( toASCII[ zeros >> 24 & 0xff ].charAt(1) );
        sb.append( toASCII[ zeros >> 16 & 0xff ] );
        sb.append( toASCII[ zeros >>  8 & 0xff ] );
        sb.append( toASCII[ zeros       & 0xff ] );
    }

    /**
     * This method changes an integer value into a string of 16 hex characters starting with
     * "Z" in order to represent a number of zeros in our simple zero-compression scheme.
     * This only makes sense for positive integers.
     * 
     * @param sb StringBuilder object into which the characters are written
     * @param zeros the number of zeros to be encoded/compressed
     */
    static private void zerosToLongStr(StringBuilder sb, int zeros) {
        sb.append("Z00000000");
        sb.append( toASCII[ zeros >> 24 & 0xff ].charAt(1) );
        sb.append( toASCII[ zeros >> 16 & 0xff ] );
        sb.append( toASCII[ zeros >>  8 & 0xff ] );
        sb.append( toASCII[ zeros       & 0xff ] );
    }

    /**
     * This method changes a long value into a string of 16 hex characters in
     * order to represent its bit pattern.
     *
     * @param sb StringBuilder object into which the characters are written
     * @param l the number to transform
     */
    static private void longToStr(StringBuilder sb, long l) {
        sb.append( toASCII[ (int) (l>>56 & 0xffL) ] );
        sb.append( toASCII[ (int) (l>>48 & 0xffL) ] );
        sb.append( toASCII[ (int) (l>>40 & 0xffL) ] );
        sb.append( toASCII[ (int) (l>>32 & 0xffL) ] );
        sb.append( toASCII[ (int) (l>>24 & 0xffL) ] );
        sb.append( toASCII[ (int) (l>>16 & 0xffL) ] );
        sb.append( toASCII[ (int) (l>> 8 & 0xffL) ] );
        sb.append( toASCII[ (int) (l     & 0xffL) ] );
    }

    /**
     * This method changes an int value into a string of 8 hex characters in
     * order to represent its bit pattern.
     *
     * @param sb StringBuilder object into which the characters are written
     * @param i the number to transform
     */
    static private final void intToStr(StringBuilder sb, int i) {
        sb.append( toASCII[ i>>24 & 0xff ] );
        sb.append( toASCII[ i>>16 & 0xff ] );
        sb.append( toASCII[ i>> 8 & 0xff ] );
        sb.append( toASCII[ i     & 0xff ] );
    }


    //--------------------------
    // Constructors, String
    //--------------------------

    /**
     * Construct a payload item from a String object.
     *
     * @param name name of item
     * @param s string to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, String s) throws cMsgException {
        if (s == null) throw new cMsgException("cannot add null payload item");
        validName(name, false);
        addString(name, s, false);
    }

    /**
     * Constructor for hidden system fields.
     * Used internally when adding system fields to a payload
     * (e.g. cMsgCreator) just before sending.
     *
     * @param name name of item
     * @param s string to be part of the payload
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, String s, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addString(name, s, isSystem);
    }

    /**
     * Construct a payload item from a String object.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param s string to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, String s, String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addString(name, s, txt, isSystem, noHeadLen);
    }

    /**
     * Construct a payload item from a String array.
     *
     * @param name name of item
     * @param s string to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, String[] s) throws cMsgException {
        if (s == null || s.length < 1) throw new cMsgException("cannot add null/empty payload item");
        for (String val : s) {
            if (val == null) throw new cMsgException("string array contains null, cannot add payload item");
        }
        validName(name, false);
        addString(name, s, false);
    }

    /**
     * Constructor for hidden system fields like cMsgSenderHistory.
     * Used internally when adding system fields to a payload (e.g. sender
     * history) just before sending.
     *
     * @param name name of item
     * @param s string array to be part of the payload
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, String[] s, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addString(name, s, isSystem);
    }

    /**
     * Construct a payload item from a String array.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param s string array to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, String[] s, String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addString(name, s, txt, isSystem, noHeadLen);
    }


    //--------------------------
    // Constructors, Binary
    //--------------------------
    /**
     * Construct a payload item from a byte array containing binary data.
     *
     * @param name name of item
     * @param b byte array containing binary data to be part of the payload
     * @param end endian value of the binary data ({@link cMsgConstants#endianBig},
     *            {@link cMsgConstants#endianLittle}, {@link cMsgConstants#endianLocal}, or
     *            {@link cMsgConstants#endianNotLocal})
     * @throws cMsgException if invalid name or endian value
     */
    public cMsgPayloadItem(String name, byte[] b, int end) throws cMsgException {
        if (b == null) throw new cMsgException("cannot add null payload item");
        validName(name, false);
        endian = checkEndian(end);
        addBinary(name, b, false);
    }

    /**
     * Construct a payload item from a byte array containing binary data.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param b byte array containing binary data to be part of the payload
     * @param end endian value of the binary data ({@link cMsgConstants#endianBig},
     *            {@link cMsgConstants#endianLittle}, {@link cMsgConstants#endianLocal}, or
     *            {@link cMsgConstants#endianNotLocal})
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name or endian value
     */
    cMsgPayloadItem(String name, byte[] b, int end, String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        endian = checkEndian(end);
        addBinary(name, b, txt, isSystem, noHeadLen);
    }

    /**
     * Construct a payload item from an array of byte arrays containing binary data.
     *
     * @param name name of item
     * @param b array of byte arrays containing binary data to be part of the payload
     * @param end array of endian values of the binary data ({@link cMsgConstants#endianBig},
     *            {@link cMsgConstants#endianLittle}, {@link cMsgConstants#endianLocal}, or
     *            {@link cMsgConstants#endianNotLocal})
     * @throws cMsgException if invalid name or endian values, not enough endian values for bin arrays
     */
    public cMsgPayloadItem(String name, byte[][] b, int[] end) throws cMsgException {
        if (b == null || b.length < 1) throw new cMsgException("cannot add null/empty payload item");
        for (byte[] array : b) {
            if (array == null) throw new cMsgException("array of byte arrays contains null, cannot add payload item");
        }

        validName(name, false);
        if (b.length > end.length) {
            throw new cMsgException("need an endian value for each byte array");
        }
        endianArray = new int[b.length];
        for (int i=0; i<b.length; i++) {
            endianArray[i] = checkEndian(end[i]);
        }
        addBinary(name, b, false);
    }

    /**
     * Construct a payload item from an array of byte arrays containing binary data.
     * Big endian data is assumed.
     *
     * @param name name of item
     * @param b array of byte arrays containing binary data to be part of the payload
     *            {@link cMsgConstants#endianLittle}, {@link cMsgConstants#endianLocal}, or
     *            {@link cMsgConstants#endianNotLocal})
     * @throws cMsgException if invalid name or endian value
     */
    public cMsgPayloadItem(String name, byte[][] b) throws cMsgException {
        if (b == null || b.length < 1) throw new cMsgException("cannot add null/empty payload item");
        for (byte[] array : b) {
            if (array == null) throw new cMsgException("array of byte arrays contains null, cannot add payload item");
        }

        validName(name, false);
        endianArray = new int[b.length];
        for (int i=0; i<b.length; i++) {
            endianArray[i] = cMsgConstants.endianBig;
        }
        addBinary(name, b, false);
    }

    /**
     * Construct a payload item from an array of byte arrays containing binary data.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param b array of byte arrays containing binary data to be part of the payload
     * @param end array of endian values of the binary data ({@link cMsgConstants#endianBig},
     *            {@link cMsgConstants#endianLittle}, {@link cMsgConstants#endianLocal}, or
     *            {@link cMsgConstants#endianNotLocal})
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name or endian values, not enough endian values for bin arrays
     */
    cMsgPayloadItem(String name, byte[][] b, int[] end,
                    String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        if (b.length > end.length) {
            throw new cMsgException("need an endian value for each byte array");
        }
        endianArray = new int[b.length];
        for (int i=0; i<b.length; i++) {
            endianArray[i] = checkEndian(end[i]);
        }
        addBinary(name, b, txt, isSystem, noHeadLen);
    }


    //--------------------------
    // Constructors, Message
    //--------------------------
    /**
     * Construct a payload item from a cMsgMessage object.
     *
     * @param name name of item
     * @param msg cMsgMessage object to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, cMsgMessage msg) throws cMsgException {
        if (msg == null) throw new cMsgException("cannot add null payload item");
        validName(name, false);
        addMessage(name, msg, false);
    }

    /**
     * Construct a payload item from a cMsgMessage object.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param msg cMsgMessage object to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, cMsgMessage msg, String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addMessage(name, msg, txt, isSystem, noHeadLen);
    }

    /**
     * Construct a payload item from an array of cMsgMessage objects.
     *
     * @param name name of item
     * @param msgs array of cMsgMessage objects to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, cMsgMessage[] msgs) throws cMsgException {
        if (msgs == null || msgs.length < 1) throw new cMsgException("cannot add null/empty payload item");
        for (cMsgMessage msg : msgs) {
            if (msg == null) throw new cMsgException("array of messages contains null, cannot add payload item");
        }
        validName(name, false);
        addMessage(name, msgs, false);
    }

    /**
     * Construct a payload item from an array of cMsgMessage objects.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param msgs array of cMsgMessage objects to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, cMsgMessage[] msgs, String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addMessage(name, msgs, txt, isSystem, noHeadLen);
    }

    //-------------------
    // Constructors, Ints
    //-------------------

    // 8 bit

    /**
     * Construct a payload item from an 8-bit integer.
     *
     * @param name name of item
     * @param b byte (8-bit integer) to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, byte b) throws cMsgException {
        validName(name, false);
        addByte(name, b, false);
    }

    /**
     * Construct a payload item from an 8-bit integer.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param b byte (8-bit integer) to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, byte b, String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addByte(name, b, txt, isSystem, noHeadLen);
    }

    // 16 bit

    /**
     * Construct a payload item from a 16-bit integer.
     *
     * @param name name of item
     * @param s short (16-bit integer) to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, short s) throws cMsgException {
        validName(name, false);
        addShort(name, s, false);
    }

    /**
     * Construct a payload item from a 16-bit integer.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param s short (16-bit integer) to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, short s, String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addShort(name, s, txt, isSystem, noHeadLen);
    }

    // 32 bit

    /**
     * Construct a payload item from a 32-bit integer.
     *
     * @param name name of item
     * @param i int (32-bit integer) to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, int i) throws cMsgException {
        validName(name, false);
        addInt(name, i, false);
    }

    /**
     * Constructor for hidden system fields.
     * Used internally when adding system fields to a payload (e.g.
     * cMsgHistoryLengthMax) just before sending.
     *
     * @param name name of item
     * @param i int (32-bit integer) to be part of the payload
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, int i, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addInt(name, i, isSystem);
    }

    /**
     * Construct a payload item from a 32-bit integer.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param i int (32-bit integer) to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, int i, String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addInt(name, i, txt, isSystem, noHeadLen);
    }

    // 64 bit

    /**
     * Construct a payload item from a 64-bit integer.
     *
     * @param name name of item
     * @param l long (64-bit integer) to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, long l) throws cMsgException {
        validName(name, false);
        addLong(name, l, false);
    }

    /**
     * Construct a payload item from a 64-bit integer.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param l long (64-bit integer) to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, long l, String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addLong(name, l, txt, isSystem, noHeadLen);
    }

    /**
     * Construct a payload item from an unsigned 64-bit integer.
     *
     * @param name name of item
     * @param big BigInteger object (containing an unsigned 64-bit integer) to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, BigInteger big) throws cMsgException {
        if (big == null) throw new cMsgException("cannot add null payload item");
        validName(name, false);
        if ((big.compareTo(new BigInteger("18446744073709551615")) > 0) ||
            (big.compareTo(BigInteger.ZERO) < 0) ) throw new cMsgException("number must fit in an unsigned, 64-bit int");
        addNumber(name, big, cMsgConstants.payloadUint64, false);
    }

    /**
     * Construct a payload item from an unsigned 64-bit integer.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param big BigInteger object (containing an unsigned 64-bit integer) to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, BigInteger big, String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addBigInt(name, big, txt, isSystem, noHeadLen);
    }


    //--------------------------
    // Constructors, Ints Arrays
    //--------------------------

    // 8 bit

    /**
     * Construct a payload item from an array of 8-bit integers.
     *
     * @param name name of item
     * @param b byte array (array of 8-bit integers) to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, byte[] b) throws cMsgException {
        if (b == null || b.length < 1) throw new cMsgException("cannot add null/empty payload item");
        validName(name, false);
        addByte(name, b, false, true);
    }

    /**
     * Construct a payload item from an array of 8-bit integers.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param b byte array (array of 8-bit integers) to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, byte[] b, String txt, int noHeadLen, boolean isSystem)
            throws cMsgException {
        validName(name, isSystem);
        addByte(name, b, txt, isSystem, noHeadLen);
    }

    // 16 bit

    /**
     * Construct a payload item from an array of 16-bit integers.
     *
     * @param name name of item
     * @param s short array (array of 16-bit integers) to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, short[] s) throws cMsgException {
        if (s == null || s.length < 1) throw new cMsgException("cannot add null/empty payload item");
        validName(name, false);
        addShort(name, s, false, true);
    }

    /**
     * Construct a payload item from an array of 16-bit integers.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param s short array (array of 16-bit integers) to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @param unsigned did the item originally come from an unsigned 8 bit int array?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, short[] s, String txt, int noHeadLen, boolean isSystem, boolean unsigned)
            throws cMsgException {
        validName(name, isSystem);
        addShort(name, s, txt, isSystem, unsigned, noHeadLen);
    }

    // 32 bit

    /**
     * Construct a payload item from an array of 32-bit integers.
     *
     * @param name name of item
     * @param i int array (array of 32-bit integers) to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, int[] i) throws cMsgException {
        if (i == null || i.length < 1) throw new cMsgException("cannot add null/empty payload item");
        validName(name, false);
        addInt(name, i, false, true);
    }

    /**
     * Construct a payload item from an array of 32-bit integers.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param i int array (array of 32-bit integers) to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @param unsigned did the item originally come from an unsigned 16 bit int array?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, int[] i, String txt, int noHeadLen, boolean isSystem, boolean unsigned)
            throws cMsgException {
        validName(name, isSystem);
        addInt(name, i, txt, isSystem, unsigned, noHeadLen);
    }

    // 64 bit

    /**
     * Construct a payload item from an array of 64-bit integers.
     *
     * @param name name of item
     * @param l long array (array of 64-bit integers) to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, long[] l) throws cMsgException {
        if (l == null || l.length < 1) throw new cMsgException("cannot add null/empty payload item");
        validName(name, false);
        addLong(name, l, false, true);
    }

    /**
     * Constructor for hidden system fields like cMsgSenderTimeHistory.
     * Used internally when adding system fields to a payload (e.g. sender
     * time history) just before sending.
     *
     * @param name name of item
     * @param l long array (array of 64-bit integers) to be part of the payload
     * @param isSystem is the item a system field (name starts with "cmsg")?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, long[] l, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addLong(name, l, isSystem, false);
    }

    /**
     * Construct a payload item from an array of 64-bit integers.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param l long array (array of 64-bit integers) to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg")?
     * @param unsigned did the item originally come from an unsigned 32 bit int array?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, long[] l, String txt, int noHeadLen,
                    boolean isSystem, boolean unsigned) throws cMsgException {
        validName(name, isSystem);
        addLong(name, l, txt, isSystem, unsigned, noHeadLen);
    }

    // BigIntegers

    /**
     * Construct a payload item from an array of unsigned 64-bit integers.
     *
     * @param name name of item
     * @param bigs array of BigInteger objects (each containing an unsigned 64-bit integer)
     *            to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, BigInteger[] bigs) throws cMsgException {
        if (bigs == null || bigs.length < 1) throw new cMsgException("cannot add null/empty payload item");
        for (BigInteger big : bigs) {
            if (big == null) throw new cMsgException("array of BigIntegers contains null, cannot add payload item");
        }

        validName(name, false);
        for (BigInteger bi : bigs) {
            if ((bi.compareTo(new BigInteger("18446744073709551615")) > 0) ||
                (bi.compareTo(BigInteger.ZERO) < 0) )
                throw new cMsgException("number must fit in an unsigned, 64-bit int");
        }
        addNumber(name, bigs, cMsgConstants.payloadUint64A, false);
    }

    /**
     * Construct a payload item from an array of unsigned 64-bit integers.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param bigs array of BigInteger objects (each containing an unsigned 64-bit integer)
     *            to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, BigInteger[] bigs, String txt, int noHeadLen, boolean isSystem)
            throws cMsgException {
        validName(name, isSystem);
        addBigInt(name, bigs, txt, isSystem, noHeadLen);
    }

    // Objects

    /**
     * Construct a payload item from an array of objects implementing the Number interface.
     * The objects must be one of either {@link Byte}, {@link Short}, {@link Integer},
     * {@link Long}, {@link Float}, or {@link Double}.
     *
     * @param name name of item
     * @param ta array of Number objects to be part of the payload
     * @throws cMsgException if invalid name
     */
    public <T extends Number> cMsgPayloadItem(String name, T[] ta) throws cMsgException {
        if (ta == null || ta.length < 1) throw new cMsgException("cannot add null/empty payload item");
        for (T t : ta) {
            if (t == null) throw new cMsgException("array of BigIntegers contains null, cannot add payload item");
        }

        int typ;
        String s = ta[0].getClass().getName();
        System.out.println("class = " + s);

        validName(name, false);

        // casts & "instance of" does not work with generics on naked types (on t), so use getClass()

        if (s.equals("java.lang.Byte")) {
            typ = cMsgConstants.payloadInt8A;
            System.out.println("Got type = Byte !!");
        }
        else if (s.equals("java.lang.Short")) {
            typ = cMsgConstants.payloadInt16A;
            System.out.println("Got type = Short !!");
        }
        else if (s.equals("java.lang.Integer")) {
            typ = cMsgConstants.payloadInt32A;
        }
        else if (s.equals("java.lang.Long")) {
            typ = cMsgConstants.payloadInt64A;
        }
        else if (s.equals("java.lang.Float")) {
            typ = cMsgConstants.payloadFltA;
        }
        else if (s.equals("java.lang.Double")) {
            typ = cMsgConstants.payloadDblA;
        }
        else {
            throw new cMsgException("Type T[]" + s + " not allowed");
        }

        addNumber(name, ta, typ, false);
    }

    //-----------
    // Reals
    //-----------

    /**
     * Construct a payload item from a float.
     *
     * @param name name of item
     * @param f float to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, float f) throws cMsgException {
        validName(name, false);
        addFloat(name, f, false);
    }

    /**
     * Construct a payload item from a float.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param f float to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, float f, String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addFloat(name, f, txt, isSystem, noHeadLen);
    }

    /**
     * Construct a payload item from a double.
     *
     * @param name name of item
     * @param d double to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, double d) throws cMsgException {
        validName(name, false);
        addDouble(name, d, false);
    }

    /**
     * Construct a payload item from a double.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param d double to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, double d, String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addDouble(name, d, txt, isSystem, noHeadLen);
    }

    //------------
    // Real Arrays
    //------------

    /**
     * Construct a payload item from an array of floats.
     *
     * @param name name of item
     * @param f float array to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, float[] f) throws cMsgException {
        if (f == null || f.length < 1) throw new cMsgException("cannot add null/empty payload item");
        validName(name, false);
        addFloat(name, f, false, true);
    }

    /**
     * Construct a payload item from an array of floats.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param f float array to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, float[] f, String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addFloat(name, f, txt, isSystem, noHeadLen);
    }

    /**
     * Construct a payload item from an array of doubles.
     *
     * @param name name of item
     * @param d double array to be part of the payload
     * @throws cMsgException if invalid name
     */
    public cMsgPayloadItem(String name, double[] d) throws cMsgException {
        if (d == null || d.length < 1) throw new cMsgException("cannot add null/empty payload item");
        validName(name, false);
        addDouble(name, d, false, true);
    }

    /**
     * Construct a payload item from an array of doubles.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param d double array to be part of the payload
     * @param txt text representation of the payload item
     * @param noHeadLen length of the text representation NOT including the header line
     * @param isSystem is the item a system field (name starts with "cmsg") ?
     * @throws cMsgException if invalid name
     */
    cMsgPayloadItem(String name, double[] d, String txt, int noHeadLen, boolean isSystem) throws cMsgException {
        validName(name, isSystem);
        addDouble(name, d, txt, isSystem, noHeadLen);
    }


    //------------------------------
    // Number is superclass of Byte, Short, Integer, Long, Float, Double, BigDecimal, BigInteger,
    // AtomicInteger, and AtomicLong
    //------------------------------

    //------------
    // ADD STRING
    //------------

    /**
     * This method creates a String (text representation) of a String payload item.
     *
     * @param name name of item
     * @param val string to be part of the payload
     * @param isSystem is the given string (val) a system field?
     */
    private void addString(String name, String val, boolean isSystem) {
        // String is immutable so we don't have to copy it
        item = val;
        type = cMsgConstants.payloadStr;
        this.name = name;
        this.isSystem = isSystem;

        // Create string to hold all data to be transferred over
        // the network for this item.
        noHeaderLen = numDigits(val.length()) + val.length() + 2;  /* 2 newlines */
        StringBuffer buf = new StringBuffer(noHeaderLen + 100);

        buf.append(name);              buf.append(" ");
        buf.append(type);              buf.append(" ");
        buf.append(count);             buf.append(" ");
        buf.append(isSystem ? 1 : 0);  buf.append(" ");
        buf.append(noHeaderLen);       buf.append("\n");
        buf.append(val.length());      buf.append("\n");
        buf.append(val);               buf.append("\n");
        text = buf.toString();
    }

    /**
     * This method stores a String (text representation) of a String payload item.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param val string to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addString(String name, String val, String txt, boolean isSystem, int noHeadLen) {
        item = val;
        type = cMsgConstants.payloadStr;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    //-----------------
    // ADD STRING ARRAY
    //-----------------

    /**
     * This method creates a String (text representation) of a String array payload item.
     *
     * @param name name of item
     * @param vals string array to be part of the payload
     * @param isSystem is the given string array a system field?
     */
    private void addString(String name, String[] vals, boolean isSystem) {
        // since String is immutable, the clone is fine
        item  = vals.clone();
        count = vals.length;
        type = cMsgConstants.payloadStrA;
        this.name = name;
        this.isSystem = isSystem;

        StringBuilder sb = new StringBuilder(200);
        for (String val : vals) {
            sb.append(val.length());  sb.append("\n");
            sb.append(val);           sb.append("\n");
        }
        noHeaderLen = sb.length();

        StringBuffer buf = new StringBuffer(noHeaderLen + 100);
        buf.append(name);              buf.append(" ");
        buf.append(type);              buf.append(" ");
        buf.append(count);             buf.append(" ");
        buf.append(isSystem ? 1 : 0);  buf.append(" ");
        buf.append(noHeaderLen);       buf.append("\n");
        buf.append(sb);
        text = buf.toString();
    }

    /**
     * This method stores a String (text representation) of a String array payload item.
     * Used internally when decoding a text representation
     * of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param vals string array to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addString(String name, String[] vals, String txt, boolean isSystem, int noHeadLen) {
        item  = vals;
        count = vals.length;
        type = cMsgConstants.payloadStrA;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    //------------
    // ADD BINARY
    //------------

    /**
     * This method creates a String (text representation) of a byte array (binary) payload item.
     *
     * @param name name of item
     * @param bin byte array (binary) to be part of the payload
     * @param isSystem is the given byte array a system field?
     */
    private void addBinary(String name, byte[] bin, boolean isSystem) {
        item = bin.clone();
        type = cMsgConstants.payloadBin;
        this.name = name;
        this.isSystem = isSystem;

        // Create string to hold all data to be transferred over
        // the network for this item. So first convert binary to
        // text (with linebreaks every 76 chars & newline at end).
        String encodedBin = Base64.encodeToString(bin, true);
        // 4 = endian value (1 digit) + 2 spaces + 1 newline
        noHeaderLen += numDigits(encodedBin.length()) +
                       numDigits(bin.length) +
                       encodedBin.length()   + 4;
//        System.out.println("addBinary: encodedBin len = " + encodedBin.length() +
//                           ", num digits = " + numDigits(encodedBin.length()) +
//                           ", add 4 for 1 endian len, 2 spaces, 1 nl");

        StringBuffer buf = new StringBuffer(noHeaderLen + 100);
        buf.append(name);                 buf.append(" ");
        buf.append(type);
        buf.append(isSystem ? " 1 1 " : " 1 0 ");
        buf.append(noHeaderLen);          buf.append("\n");
        buf.append(encodedBin.length());  buf.append(" ");
        buf.append(bin.length);           buf.append(" ");
        buf.append(endian);               buf.append("\n");
        buf.append(encodedBin);

        text = buf.toString();
    }

    /**
     * This method stores a String (text representation) of a byte array (binary) payload item.
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param bin byte array (binary) to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addBinary(String name, byte[] bin, String txt, boolean isSystem, int noHeadLen) {
        item = bin;
        type = cMsgConstants.payloadBin;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    //-----------------
    // ADD BINARY ARRAY
    //-----------------

    /**
     * This method creates a String (text representation) of an array of
     * byte arrays (array of binary) payload item.
     *
     * @param name name of item
     * @param bin array of byte arrays (array of binary) to be part of the payload
     * @param isSystem is the given byte array a system field?
     */
    private void addBinary(String name, byte[][] bin, boolean isSystem) {
        // copy multidimensional arrays by hand in Java
        byte[][] b = new byte[bin.length][];
        for (int i=0; i<bin.length; i++) {
            b[i] = bin[i].clone();
        }
        item  = b;
        count = b.length;
        type  = cMsgConstants.payloadBinA;
        this.name = name;
        this.isSystem = isSystem;

        noHeaderLen = 0;
        String[] encodedStrs = new String[count];
        for (int i=0; i<count; i++) {
            // First convert binary to text, where the "true" arg
            // means put linebreaks in string rep including one at the end
            encodedStrs[i] = Base64.encodeToString(b[i], true);
            // 4 = endian value (1 digit) + 2 spaces + 1 newline
            noHeaderLen += numDigits(encodedStrs[i].length()) +
                           numDigits(b[i].length) +
                           encodedStrs[i].length()  + 4;
        }

        StringBuffer buf = new StringBuffer(noHeaderLen + 100);
        // header line
        buf.append(name);                      buf.append(" ");
        buf.append(type);                      buf.append(" ");
        buf.append(count);
        buf.append(isSystem ? " 1 " : " 0 ");
        buf.append(noHeaderLen);               buf.append("\n");
        // non header lines
        for (int i=0; i<count; i++) {
            buf.append(encodedStrs[i].length());  buf.append(" ");
            buf.append(b[i].length);              buf.append(" ");
            buf.append(endianArray[i]);           buf.append("\n");
            buf.append(encodedStrs[i]);
        }

        text = buf.toString();
    }

    /**
     * This method stores a String (text representation) of an array of byte arrays (binary) payload item.
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param bin array of byte arrays (binary) to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addBinary(String name, byte[][] bin, String txt, boolean isSystem, int noHeadLen) {
        item = bin;
        count = bin.length;
        type = cMsgConstants.payloadBinA;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    //------------
    // ADD MESSAGE
    //------------

    /**
     * This is a utility method which takes a given string "s" and places it into a buffer
     * "buf" in the proper format to be a text representation of a String payload item.
     *
     * @param name name of payload item
     * @param buf StringBuilder object into which will be placed characters that represent
     *            a String item of the payload
     * @param s is the string to be placed in the buffer in the form of a text representation
     *          of a String payload item
     */
    private void systemStringToBuf(String name, StringBuilder buf, String s) {
        int len = numDigits(s.length()) + s.length() + 2; // 2 newlines
        buf.append(name);       buf.append(" ");
        buf.append(cMsgConstants.payloadStr);
        buf.append(" 1 1 ");
        buf.append(len);         buf.append("\n");
        buf.append(s.length());  buf.append("\n");
        buf.append(s);           buf.append("\n");
    }

    /**
     * This method stores a String (text representation) of a cMsgMessage object payload item.
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param msg cMsgMessage object to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addMessage(String name, cMsgMessage msg, String txt, boolean isSystem, int noHeadLen) {
        item = msg;
        type = cMsgConstants.payloadMsg;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This method creates a String (text representation) of a cMsgMessage object payload item.
     *
     * @param name name of item
     * @param msg cMsgMessage object to be part of the payload
     * @param isSystem is the given string a system field?
     */
    private void addMessage(String name, cMsgMessage msg, boolean isSystem) {
        item = msg.clone();
        type = cMsgConstants.payloadMsg;
        this.name = name;
        this.isSystem = isSystem;

        // Create string to hold all data to be transferred over
        // the network for this item. Try to make sure we have some
        // room in the buffer for the large items plus 2K extra.
        int size = 0;
        String payloadRep = msg.getItemsText();
        if (payloadRep    != null)  size += payloadRep.length();
        if (msg.getText() != null)  size += msg.getText().length();
        size += msg.getByteArrayLength()*1.4;

        int fieldCount = 0;
        StringBuilder buf = new StringBuilder(size + 2048);

        // Start adding message fields to string in payload item format
        if (msg.getDomain() != null) {
            systemStringToBuf("cMsgDomain", buf, msg.getDomain());
            fieldCount++;
        }
        if (msg.getSubject() != null) {
            systemStringToBuf("cMsgSubject", buf, msg.getSubject());
            fieldCount++;
        }
        if (msg.getType() != null) {
            systemStringToBuf("cMsgType", buf, msg.getType());
            fieldCount++;
        }
        if (msg.getText() != null) {
            systemStringToBuf("cMsgText", buf, msg.getText());
            fieldCount++;
        }
        if (msg.getSender() != null) {
            systemStringToBuf("cMsgSender", buf, msg.getSender());
            fieldCount++;
        }
        if (msg.getSenderHost() != null) {
            systemStringToBuf("cMsgSenderHost", buf, msg.getSenderHost());
            fieldCount++;
        }
        if (msg.getReceiver() != null) {
            systemStringToBuf("cMsgReceiver", buf, msg.getReceiver());
            fieldCount++;
        }
        if (msg.getReceiverHost() != null) {
            systemStringToBuf("cMsgReceiverHost", buf, msg.getReceiverHost());
            fieldCount++;
        }

        // add an array of integers
        StringBuilder sb = new StringBuilder(100);
        intToStr(sb, msg.getVersion());         sb.append(" ");
        intToStr(sb, msg.getInfo());            sb.append(" ");
        intToStr(sb, msg.reserved);             sb.append(" ");
        intToStr(sb, msg.getByteArrayLength()); sb.append(" ");
        intToStr(sb, msg.getUserInt());         sb.append("\n");

        buf.append("cMsgInts ");
        buf.append(cMsgConstants.payloadInt32A);
        buf.append(" 5 1 ");
        buf.append(sb.length());  buf.append("\n");
        buf.append(sb);
        fieldCount++;

        // Send a time as 2, 64 bit integers - one for seconds, one for nanoseconds.
        // In java, time in only kept in milliseconds, so convert.
        sb.delete(0,sb.length());
        long t = msg.getUserTime().getTime();
        longToStr(sb, t/1000);   // sec
        sb.append(" ");
        longToStr(sb, (t - ((t/1000L)*1000L))*1000000L);   // nanosec
        sb.append(" ");
        t = msg.getSenderTime().getTime();
        longToStr(sb, t/1000);   // sec
        sb.append(" ");
        longToStr(sb, (t - ((t/1000L)*1000L))*1000000L);   // nanosec
        sb.append(" ");
        t = msg.getReceiverTime().getTime();
        longToStr(sb, t/1000);   // sec
        sb.append(" ");
        longToStr(sb, (t - ((t/1000L)*1000L))*1000000L);   // nanosec
        sb.append("\n");

        buf.append("cMsgTimes ");
        buf.append(cMsgConstants.payloadInt64A);
        buf.append(" 6 1 ");
        buf.append(sb.length());  buf.append("\n");
        buf.append(sb);
        fieldCount++;

        // send the byte array
        if (msg.getByteArrayLength() > 0) {
//System.out.println("ADDING BIN TO PAYLOAD MSG");
            String encodedBin = Base64.encodeToString(msg.getByteArray(), msg.getByteArrayOffset(),
                                                      msg.getByteArrayLength(), true);
            int len = numDigits(encodedBin.length()) +
                      numDigits(msg.getByteArrayLength()) +
                      encodedBin.length() + 4;  // 1 endian value, 2 spaces, 1 newline

            buf.append("cMsgBinary ");
            buf.append(cMsgConstants.payloadBin);
            buf.append(" 1 1 ");
            buf.append(len);                       buf.append("\n");
            buf.append(encodedBin.length());       buf.append(" ");
            buf.append(msg.getByteArrayLength());  buf.append(" ");
            buf.append(msg.getByteArrayEndian());  buf.append("\n");
            buf.append(encodedBin);
            fieldCount++;
        }

        // add payload items of this message
        Collection<cMsgPayloadItem> c = msg.getPayloadItems().values();
        for (cMsgPayloadItem it : c) {
            buf.append(it.text);
            fieldCount++;
        }

        // length of everything except header
        noHeaderLen = buf.length() + numDigits(fieldCount) + 1; // newline

        StringBuilder buf2 = new StringBuilder(noHeaderLen + 100);
        buf2.append(name);                 buf2.append(" ");
        buf2.append(type);                 buf2.append(" ");
        buf2.append(count);                buf2.append(" ");
        buf2.append(isSystem ? 1 : 0);     buf2.append(" ");
        buf2.append(noHeaderLen);          buf2.append("\n");
        buf2.append(fieldCount);           buf2.append("\n");
        buf2.append(buf);   // copy done here, but easier than calculating exact length beforehand (faster?)

        text = buf2.toString();
    }

    /**
     * This method stores a String (text representation) of a cMsgMessage array payload item.
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param msgs array of cMsgMessage objects to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addMessage(String name, cMsgMessage[] msgs, String txt, boolean isSystem, int noHeadLen) {
        item  = msgs;
        count = msgs.length;
        type  = cMsgConstants.payloadMsgA;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This method creates a String (text representation) of a cMsgMessage array payload item.
     *
     * @param name name of item
     * @param msgs array of cMsgMessage objects to be part of the payload
     * @param isSystem is the given string a system field?
     */
    private void addMessage(String name, cMsgMessage[] msgs, boolean isSystem) {
        // copy array by hand in Java
        cMsgMessage[] m = new cMsgMessage[msgs.length];
        for (int i=0; i<m.length; i++) {
            m[i] = (cMsgMessage)msgs[i].clone();
        }
        item  = m;
        count = m.length;
        type  = cMsgConstants.payloadMsgA;
        this.name = name;
        this.isSystem = isSystem;

        // Keep track of the number of payload fields for each msg
        int[] fieldCount = new int[count];
        // 2 fields in each msg for sure (cMsgInts & cMsgTimes)
        Arrays.fill(fieldCount, 2);

        // Create string to hold all data to be transferred over
        // the network for this item. Try to make sure we have
        // room in the buffer for the large items plus 1K/msg.
        // Add 2k extra for cushion.
        int size = 2048;

        for (int i=0; i<count; i++) {
            if (msgs[i].getItemsText() != null) size += msgs[i].getItemsText().length();
            if (msgs[i].getText() != null) size += msgs[i].getText().length();
            size += msgs[i].getByteArrayLength() * 1.4 + 1024;

            if (msgs[i].getDomain()       != null) fieldCount[i]++;
            if (msgs[i].getSubject()      != null) fieldCount[i]++;
            if (msgs[i].getType()         != null) fieldCount[i]++;
            if (msgs[i].getText()         != null) fieldCount[i]++;
            if (msgs[i].getSender()       != null) fieldCount[i]++;
            if (msgs[i].getSenderHost()   != null) fieldCount[i]++;
            if (msgs[i].getReceiver()     != null) fieldCount[i]++;
            if (msgs[i].getDomain()       != null) fieldCount[i]++;
            if (msgs[i].getReceiverHost() != null) fieldCount[i]++;
            if (msgs[i].getByteArrayLength() > 0)  fieldCount[i]++;
            fieldCount[i] += msgs[i].getPayloadSize();
        }

        StringBuilder buf = new StringBuilder(size);
        StringBuilder  sb = new StringBuilder(100);
        int j=0;

        for (cMsgMessage msg : msgs) {
            // First thing is number of payload fields
            buf.append(fieldCount[j++]);
            buf.append("\n");

            // Start adding message fields to string in payload item format
            if (msg.getDomain()       != null) systemStringToBuf("cMsgDomain", buf, msg.getDomain());
            if (msg.getSubject()      != null) systemStringToBuf("cMsgSubject", buf, msg.getSubject());
            if (msg.getType()         != null) systemStringToBuf("cMsgType", buf, msg.getType());
            if (msg.getText()         != null) systemStringToBuf("cMsgText", buf, msg.getText());
            if (msg.getSender()       != null) systemStringToBuf("cMsgSender", buf, msg.getSender());
            if (msg.getSenderHost()   != null) systemStringToBuf("cMsgSenderHost", buf, msg.getSenderHost());
            if (msg.getReceiver()     != null) systemStringToBuf("cMsgReceiver", buf, msg.getReceiver());
            if (msg.getReceiverHost() != null) systemStringToBuf("cMsgReceiverHost", buf, msg.getReceiverHost());

            // add an array of integers
            sb.delete(0, sb.length());
            intToStr(sb, msg.getVersion());
            sb.append(" ");
            intToStr(sb, msg.getInfo());
            sb.append(" ");
            intToStr(sb, msg.reserved);
            sb.append(" ");
            intToStr(sb, msg.getByteArrayLength());
            sb.append(" ");
            intToStr(sb, msg.getUserInt());
            sb.append("\n");

            buf.append("cMsgInts ");
            buf.append(cMsgConstants.payloadInt32A);
            buf.append(" 5 1 ");
            buf.append(sb.length());
            buf.append("\n");
            buf.append(sb);

            // Send a time as 2, 64 bit integers - one for seconds, one for nanoseconds.
            // In java, time in only kept in milliseconds, so convert.
            sb.delete(0, sb.length());
            long t = msg.getUserTime().getTime();
            longToStr(sb, t / 1000);   // sec
            sb.append(" ");
            longToStr(sb, (t - ((t / 1000L) * 1000L)) * 1000000L);   // nanosec
            sb.append(" ");
            t = msg.getSenderTime().getTime();
            longToStr(sb, t / 1000);   // sec
            sb.append(" ");
            longToStr(sb, (t - ((t / 1000L) * 1000L)) * 1000000L);   // nanosec
            sb.append(" ");
            t = msg.getReceiverTime().getTime();
            longToStr(sb, t / 1000);   // sec
            sb.append(" ");
            longToStr(sb, (t - ((t / 1000L) * 1000L)) * 1000000L);   // nanosec
            sb.append("\n");

            buf.append("cMsgTimes ");
            buf.append(cMsgConstants.payloadInt64A);
            buf.append(" 6 1 ");
            buf.append(sb.length());
            buf.append("\n");
            buf.append(sb);

            // send the byte array
            if (msg.getByteArrayLength() > 0) {
//System.out.println("ADDING BIN TO PAYLOAD MSG");
                String encodedBin = Base64.encodeToString(msg.getByteArray(), msg.getByteArrayOffset(),
                                                          msg.getByteArrayLength(), true);
                int len = numDigits(encodedBin.length()) +
                          numDigits(msg.getByteArrayLength()) +
                          encodedBin.length() + 4;  // 1 endian value, 2 spaces, 1 newline

                buf.append("cMsgBinary ");
                buf.append(cMsgConstants.payloadBin);
                buf.append(" 1 1 ");
                buf.append(len);                       buf.append("\n");
                buf.append(encodedBin.length());       buf.append(" ");
                buf.append(msg.getByteArrayLength());  buf.append(" ");
                buf.append(msg.getByteArrayEndian());  buf.append("\n");
                buf.append(encodedBin);
            }

            // add payload items of this message
            Collection<cMsgPayloadItem> c = msg.getPayloadItems().values();
            for (cMsgPayloadItem it : c) {
                buf.append(it.text);
            }
        }

        noHeaderLen = buf.length();

        StringBuilder buf2 = new StringBuilder(noHeaderLen + 100);
        buf2.append(name);                 buf2.append(" ");
        buf2.append(type);                 buf2.append(" ");
        buf2.append(count);                buf2.append(" ");
        buf2.append(isSystem ? 1 : 0);     buf2.append(" ");
        buf2.append(noHeaderLen);          buf2.append("\n");
        buf2.append(buf);   // copy done here, but easier than calculating exact length beforehand (faster?)

        text = buf2.toString();
    }


    //------------
    // ADD NUMBERS
    //------------


    /**
     * This method creates a String (text representation) of a payload item consisting of an object
     * implementing the Number interface. The object must be one of either {@link Byte}, {@link Short},
     * {@link Integer}, {@link Long}, {@link Float}, or {@link Double}.
     *
     * @param name name of item
     * @param t object implementing Number interface
     * @param type type of payloadItem being created (e.g. {@link cMsgConstants#payloadDbl})
     * @param isSystem is the given string a system field?
     */
    private <T extends Number> void addNumber(String name, T t, int type, boolean isSystem) {
        item = t;
        this.type = type;
        addScalar(name, t.toString(), isSystem);
    }

    /**
     * This method stores a String (text representation) of a payload item consisting of BigInteger.
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param big BigInteger object to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addBigInt(String name, BigInteger big, String txt, boolean isSystem, int noHeadLen) {
        item = big;
        type = cMsgConstants.payloadUint64;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a byte (8-bit integer).
     *
     * @param name name of item
     * @param b byte (8-bit integer) to be part of the payload
     * @param isSystem is the given string a system field?
     */
    private void addByte(String name, byte b, boolean isSystem) {
        item = b;
        type = cMsgConstants.payloadInt8;
        addScalar(name, ((Byte) b).toString(), isSystem);
    }

    /**
     * This method stores a String (text representation) of a payload item consisting of
     * a byte (8-bit integer).
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param b byte (8-bit integer) to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addByte(String name, byte b, String txt, boolean isSystem, int noHeadLen) {
        item = b;
        type = cMsgConstants.payloadInt8;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a short (16-bit integer).
     *
     * @param name name of item
     * @param s short (16-bit integer) to be part of the payload
     * @param isSystem is the given string a system field?
     */
    private void addShort(String name, short s, boolean isSystem) {
        item = s;
        type = cMsgConstants.payloadInt16;
        addScalar(name, ((Short) s).toString(), isSystem);
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a short (16-bit integer).
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param s short (16-bit integer) to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addShort(String name, short s, String txt, boolean isSystem, int noHeadLen) {
        item = s;
        type = cMsgConstants.payloadInt16;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * an int (32-bit integer).
     *
     * @param name name of item
     * @param i int (32-bit integer) to be part of the payload
     * @param isSystem is the given string a system field?
     */
    private void addInt(String name, int i, boolean isSystem) {
        item = i;
        type = cMsgConstants.payloadInt32;
        addScalar(name, ((Integer) i).toString(), isSystem);
     }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * an int (32-bit integer).
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param i int (32-bit integer) to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addInt(String name, int i, String txt, boolean isSystem, int noHeadLen) {
        item = i;
        type = cMsgConstants.payloadInt32;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a long (64-bit integer).
     *
     * @param name name of item
     * @param l long (64-bit integer) to be part of the payload
     * @param isSystem is the given string a system field?
     */
    private void addLong(String name, long l, boolean isSystem) {
        item = l;
        type = cMsgConstants.payloadInt64;
        addScalar(name, ((Long) l).toString(), isSystem);
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a long (64-bit integer).
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param l long (64-bit integer) to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addLong(String name, long l, String txt, boolean isSystem, int noHeadLen) {
        item = l;
        type = cMsgConstants.payloadInt64;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a float.
     *
     * @param name name of item
     * @param f float to be part of the payload
     * @param isSystem is the given string a system field?
     */
    private void addFloat(String name, float f, boolean isSystem) {
        item = f;
        type = cMsgConstants.payloadFlt;

        StringBuilder sb = new StringBuilder(8);
        // bit pattern of float written into int and converted to text
        intToStr(sb, Float.floatToIntBits(f));

        addScalar(name, sb, isSystem);
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a float.
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param f float to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addFloat(String name, float f, String txt, boolean isSystem, int noHeadLen) {
        item = f;
        type = cMsgConstants.payloadFlt;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a double.
     *
     * @param name name of item
     * @param d double to be part of the payload
     * @param isSystem is the given string a system field?
     */
    private void addDouble(String name, double d, boolean isSystem) {
        item = d;
        type = cMsgConstants.payloadDbl;

        StringBuilder sb = new StringBuilder(16);
        // bit pattern of double written into long and converted to text
        longToStr(sb, Double.doubleToLongBits(d));

        addScalar(name, sb, isSystem);
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a double.
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param d double to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addDouble(String name, double d, String txt, boolean isSystem, int noHeadLen) {
        item = d;
        type = cMsgConstants.payloadDbl;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This is a utility method which takes a String representation of any type of integer or
     * Number object and creates a String (text representation) of an associated payload item.
     *
     * @param name name of payload item
     * @param val is the string representation of any type of integer or
     *            Number object (excluding Float and Double)
     * @param isSystem is the given string a system field?
     */
    private void addScalar(String name, String val, boolean isSystem) {
        this.name = name;
        this.isSystem = isSystem;

        // Create string to hold all data to be transferred over
        // the network for this item.
        noHeaderLen = val.length() + 1;  /* 1 newline */

        StringBuilder buf = new StringBuilder(noHeaderLen + 100);
        buf.append(name);              buf.append(" ");
        buf.append(type);              buf.append(" ");
        buf.append(count);             buf.append(" ");
        buf.append(isSystem ? 1 : 0);  buf.append(" ");
        buf.append(noHeaderLen);       buf.append("\n");
        buf.append(val);               buf.append("\n");

        text = buf.toString();
    }


    /**
     * This is a utility method which takes a StringBuilder representation of any type of integer or
     * Number object and creates a String (text representation) of an associated payload item.
     *
     * @param name name of payload item
     * @param val is the string representation of a Float or Double
     * @param isSystem is the given string a system field?
     */
    private void addScalar(String name, StringBuilder val, boolean isSystem) {
        this.name = name;
        this.isSystem = isSystem;

        // Create string to hold all data to be transferred over
        // the network for this item.
        noHeaderLen = val.length() + 1;  /* 1 newline */

        StringBuilder buf = new StringBuilder(noHeaderLen + 100);
        buf.append(name);              buf.append(" ");
        buf.append(type);              buf.append(" ");
        buf.append(count);             buf.append(" ");
        buf.append(isSystem ? 1 : 0);  buf.append(" ");
        buf.append(noHeaderLen);       buf.append("\n");
        buf.append(val);               buf.append("\n");

        text = buf.toString();
    }


    //-------------------
    // ADD NUMBER ARRAYS
    //-------------------


    /**
     * This method creates a String (text representation) of a payload item consisting of an array of objects
     * implementing the Number interface. The objects must be one of either {@link Byte}, {@link Short},
     * {@link Integer}, {@link Long}, {@link Float}, {@link Double}, or {@link BigInteger}.
     *
     * @param name name of item
     * @param t array of objects implementing Number interface
     * @param type type of payloadItem being created (e.g. {@link cMsgConstants#payloadDblA})
     * @param isSystem is the given string a system field?
     */
    private <T extends Number> void addNumber(String name, T[] t, int type, boolean isSystem) {

        // store in a primitive form if Byte, Short, Integer, Long, Float, or Double
        if (type == cMsgConstants.payloadInt8A) {
            byte[] b = new byte[t.length];
            for (int j=0; j<t.length; j++) {
                b[j] = (Byte)t[j];
            }
            addByte(name, b, isSystem, false);
        }
        else if (type == cMsgConstants.payloadInt16A) {
            short[] s = new short[t.length];
            for (int j=0; j<t.length; j++) {
                s[j] = (Short)t[j];
            }
            addShort(name, s, isSystem, false);
        }
        else if (type == cMsgConstants.payloadInt32A) {
            int[] i = new int[t.length];
            for (int j=0; j<t.length; j++) {
                i[j] = (Integer)t[j];
            }
            addInt(name, i, isSystem, false);
        }
        else if (type == cMsgConstants.payloadInt64A) {
            long[] l = new long[t.length];
            for (int j=0; j<t.length; j++) {
                l[j] = (Long)t[j];
            }
            addLong(name, l, isSystem, false);
        }
        else if (type == cMsgConstants.payloadFltA) {
            float[] f = new float[t.length];
            for (int j=0; j<t.length; j++) {
                f[j] = (Float)t[j];
            }
            addFloat(name, f, isSystem, false);
        }
        else if (type == cMsgConstants.payloadDblA) {
            double[] d = new double[t.length];
            for (int j=0; j<t.length; j++) {
                d[j] = (Double)t[j];
            }
            addDouble(name, d, isSystem, false);
        }
        else if (type == cMsgConstants.payloadUint64A) {
            addBigInt(name, (BigInteger[]) t, isSystem);
        }
    }

    /**
     * This method stores a String (text representation) of a payload item consisting of
     * a byte array (8-bit integer array).
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param b byte array (8-bit integer array) to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addByte(String name, byte[] b, String txt, boolean isSystem, int noHeadLen) {
        item  = b;
        count = b.length;
        type  = cMsgConstants.payloadInt8A;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a byte array (8-bit integer array).
     *
     * @param name name of item
     * @param b byte array (8-bit integer array) to be part of the payload
     * @param isSystem is the given string a system field?
     * @param copy if true make full copy of array else only copy reference
     */
    private void addByte(String name, byte[] b, boolean isSystem, boolean copy) {
        if (copy) {
            item  = b.clone();
        }
        else {
            item  = b;
        }
        count = b.length;
        type  = cMsgConstants.payloadInt8A;
        this.name = name;
        this.isSystem = isSystem;

        // guess at how much memory we need
        noHeaderLen = (2+1)*count; // (length - 1)spaces + 1 newline
        StringBuilder sb = new StringBuilder(noHeaderLen + 100);

        for (int i = 0; i < count; i++) {
            sb.append( toASCII[ b[i] & 0xff ] );
            if (i < count-1) {
                sb.append(" ");
            } else {
                sb.append("\n");
            }
        }

        addArray(sb);
    }

    /**
     * This method stores a String (text representation) of a payload item consisting of
     * a short array (16-bit integer array).
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param s short array (16-bit integer array) to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param unsigned did the item originally come from an unsigned 8 bit int array?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addShort(String name, short[] s, String txt, boolean isSystem, boolean unsigned, int noHeadLen) {
        // The original text rep is no longer valid since in Java the
        // unsigned types are now represented by a larger int type.
        if (unsigned) {
            originalText = txt;
            originalType = cMsgConstants.payloadUint8A;
            addShort(name, s, isSystem, false);
            return;
        }

        item  = s;
        count = s.length;
        type  = cMsgConstants.payloadInt16A;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a short array (16-bit integer array).
     *
     * @param name name of item
     * @param s short array (16-bit integer array) to be part of the payload
     * @param isSystem is the given string a system field?
     * @param copy if true make full copy of array else only copy reference
     */
    private void addShort(String name, short[] s, boolean isSystem, boolean copy) {
        if (copy) {
            item  = s.clone();
        }
        else {
            item  = s;
        }
        count = s.length;
        type  = cMsgConstants.payloadInt16A;
        this.name = name;
        this.isSystem = isSystem;

        // guess at how much memory we need
        int lenGuess = (4+1)*count; // (length - 1)spaces + 1 newline
        StringBuilder sb = new StringBuilder(lenGuess + 100);

        // Add each array element as a string of 4 hex characters.
        // Do zero suppression as well.
        int zeros=0, suppressed=0;
        short j16;
        boolean thisOneZero=false;

        for (int i = 0; i < count; i++) {
            j16 = s[i];

            if (j16 == 0) {
                if ((++zeros < 0xfff) && (i < count-1)) {
                    continue;
                }
                thisOneZero = true;
            }

            // If the # of zero have reached their limit or the current # is not zero,
            // write out the accumulated zeros as Z-------
            if (zeros > 0) {
                // how many shorts did we not write?
                suppressed += zeros - 1;
//System.out.println("SUPPRESSED %u\n",suppressed);

                // don't use 'Z' for only 1 zero
                if (zeros == 1) {
                    sb.append("0000");
                }
                else {
                    sb.append("Z");
                    sb.append( toASCII[ zeros >> 8 & 0xff ].charAt(1) );
                    sb.append( toASCII[ zeros      & 0xff ] );
                }

                if (thisOneZero) {
                    if (i < count - 1) {
                        sb.append(" ");
                        zeros = 0;
                        thisOneZero = false;
                        continue;
                    }
                    else {
                        sb.append("\n");
                        break;
                    }
                }
                // this one is NOT zero, just wrote out the accumumlated zeros
                sb.append(" ");
                zeros = 0;
                thisOneZero = false;
            }

            sb.append( toASCII[ j16>> 8 & 0xff ] );
            sb.append( toASCII[ j16     & 0xff ] );

            if (i < count-1) {
                sb.append(" ");
            } else {
                sb.append("\n");
            }
        }

        noHeaderLen = (4+1)*(count - suppressed);
        addArray(sb);
    }

    /**
     * This method stores a String (text representation) of a payload item consisting of
     * an int array (32-bit integer array).
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param i int array (32-bit integer array) to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param unsigned did the item originally come from an unsigned 16 bit int array?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addInt(String name, int[] i, String txt, boolean isSystem, boolean unsigned, int noHeadLen) {
        // The original text rep is no longer valid since in Java the
        // unsigned types are now represented by a larger int type.
        if (unsigned) {
            originalText = txt;
            originalType = cMsgConstants.payloadUint16A;
            addInt(name, i, isSystem, false);
            return;
        }

        item  = i;
        count = i.length;
        type  = cMsgConstants.payloadInt32A;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * an int array (32-bit integer array).
     *
     * @param name name of item
     * @param i int array (32-bit integer array) to be part of the payload
     * @param isSystem is the given string a system field?
     * @param copy if true make full copy of array else only copy reference
     */
    private void addInt(String name, int[] i, boolean isSystem, boolean copy) {
        if (copy) {
            item  = i.clone();
        }
        else {
            item  = i;
        }
        count = i.length;
        type  = cMsgConstants.payloadInt32A;
        this.name = name;
        this.isSystem = isSystem;

        // guess at how much memory we need
        int lenGuess = (8+1)*count; // (length - 1)spaces + 1 newline
        StringBuilder sb = new StringBuilder(lenGuess + 100);

        // Add each array element as a string of 8 hex characters.
        // Do zero suppression as well.
        int j32, zeros=0, suppressed=0;
        boolean thisOneZero=false;

        for (int j = 0; j < count; j++) {
            j32 = i[j];

            if (j32 == 0) {
                if ((++zeros < 0xfffffff) && (j < count-1)) {
                    continue;
                }
                thisOneZero = true;
            }

            // If the # of zero have reached their limit or the current # is not zero,
            // write out the accumulated zeros as Z-------
            if (zeros > 0) {
                // how many ints did we not write?
                suppressed += zeros - 1;
//System.out.println("SUPPRESSED %u\n",suppressed);

                // don't use 'Z' for only 1 zero
                if (zeros == 1) {
                    sb.append("00000000");
                }
                else {
                    sb.append("Z");
                    sb.append( toASCII[ zeros >> 24 & 0xff ].charAt(1) );
                    sb.append( toASCII[ zeros >> 16 & 0xff ] );
                    sb.append( toASCII[ zeros >>  8 & 0xff ] );
                    sb.append( toASCII[ zeros       & 0xff ] );
                }

                if (thisOneZero) {
                    if (j < count - 1) {
                        sb.append(" ");
                        zeros = 0;
                        thisOneZero = false;
                        continue;
                    }
                    else {
                        sb.append("\n");
                        break;
                    }
                }
                // this one is NOT zero, just wrote out the accumumlated zeros
                sb.append(" ");
                zeros = 0;
                thisOneZero = false;
            }

            sb.append( toASCII[ j32>>24 & 0xff ] );
            sb.append( toASCII[ j32>>16 & 0xff ] );
            sb.append( toASCII[ j32>> 8 & 0xff ] );
            sb.append( toASCII[ j32     & 0xff ] );

            if (j < count-1) {
                sb.append(" ");
            } else {
                sb.append("\n");
            }
        }

        noHeaderLen = (8+1)*(count - suppressed);
        addArray(sb);
    }

    /**
     * This method stores a String (text representation) of a payload item consisting of
     * a long array (64-bit integer array).
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param l long array (64-bit integer array) to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param unsigned did the item originally come from an unsigned 32 bit int array?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addLong(String name, long[] l, String txt, boolean isSystem, boolean unsigned, int noHeadLen) {
        // The original text rep is no longer valid since in Java the
        // unsigned types are now represented by a larger int type.
        if (unsigned) {
            originalText = txt;
            originalType = cMsgConstants.payloadUint32A;
            addLong(name, l, isSystem, false);
            return;
        }

        item  = l;
        count = l.length;
        type  = cMsgConstants.payloadInt64A;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a long array (64-bit integer array).
     *
     * @param name name of item
     * @param l long array (64-bit integer array) to be part of the payload
     * @param isSystem is the given string a system field?
     * @param copy if true make full copy of array else only copy reference
     */
    private void addLong(String name, long[] l, boolean isSystem, boolean copy) {
        if (copy) {
            item  = l.clone();
        }
        else {
            item  = l;
        }
        count = l.length;
        type  = cMsgConstants.payloadInt64A;
        this.name = name;
        this.isSystem = isSystem;

        // guess at how much memory we need
        int lenGuess = (16+1)*count; // (length - 1)spaces + 1 newline
        StringBuilder sb = new StringBuilder(lenGuess + 100);

        // Add each array element as a string of 16 hex characters.
        // Do zero suppression as well.
        int zeros=0, suppressed=0;
        boolean thisOneZero=false;
        long j64;

        for (int i = 0; i < count; i++) {
            j64 = l[i];

            if (j64 == 0L) {
                if ((++zeros < 0xfffffff) && (i < count-1)) {
                    continue;
                }
                thisOneZero = true;
            }

            // If the # of zero have reached their limit or the current # is not zero,
            // write out the accumulated zeros as Z00000000-------
            if (zeros > 0) {
                // how many longs did we not write?
                suppressed += zeros - 1;
//System.out.println("SUPPRESSED %u\n",suppressed);

                // don't use 'Z' for only 1 zero
                if (zeros == 1) {
                    sb.append("0000000000000000");
                }
                else {
                    sb.append("Z00000000");
                    sb.append( toASCII[ zeros >> 24 & 0xff ].charAt(1) );
                    sb.append( toASCII[ zeros >> 16 & 0xff ] );
                    sb.append( toASCII[ zeros >>  8 & 0xff ] );
                    sb.append( toASCII[ zeros       & 0xff ] );
                }

                if (thisOneZero) {
                    if (i < count - 1) {
                        sb.append(" ");
                        zeros = 0;
                        thisOneZero = false;
                        continue;
                    }
                    else {
                        sb.append("\n");
                        break;
                    }
                }
                // this one is NOT zero, just wrote out the accumumlated zeros
                sb.append(" ");
                zeros = 0;
                thisOneZero = false;
            }

            sb.append( toASCII[ (int) (j64>>56 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>48 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>40 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>32 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>24 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>16 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>> 8 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64     & 0xffL) ] );

            if (i < count-1) {
                sb.append(" ");
            } else {
                sb.append("\n");
            }
        }

        noHeaderLen = (16+1)*(count - suppressed);
        addArray(sb);
    }

    /**
     * This method stores a String (text representation) of a payload item consisting of
     * a BigInteger object (unsigned 64-bit integer) array.
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param bigs BigInteger object (unsigned 64-bit integer) array to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addBigInt(String name, BigInteger[] bigs, String txt,  boolean isSystem,int noHeadLen) {
        item  = bigs;
        count = bigs.length;
        type  = cMsgConstants.payloadUint64A;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a BigInteger object (unsigned 64-bit integer) array.
     *
     * @param name name of item
     * @param bigs BigInteger object (unsigned 64-bit integer) array to be part of the payload
     * @param isSystem is the given string a system field?
     */
    private void addBigInt(String name, BigInteger[] bigs, boolean isSystem) {
        item  = bigs.clone();
        count = bigs.length;
        type  = cMsgConstants.payloadUint64A;
        this.name = name;
        this.isSystem = isSystem;

        // guess at how much memory we need
        int lenGuess = (16+1)*count; // (length - 1)spaces + 1 newline
        StringBuilder sb = new StringBuilder(lenGuess + 100);

        // Add each array element as a string of 16 hex characters.
        // Do zero suppression as well.
        int zeros=0, suppressed=0;
        boolean thisOneZero=false;
        long j64;

        for (int i = 0; i < count; i++) {
            j64 = bigs[i].longValue();

            if (j64 == 0L) {
                if ((++zeros < 0xfffffff) && (i < count-1)) {
                    continue;
                }
                thisOneZero = true;
            }

            // If the # of zero have reached their limit or the current # is not zero,
            // write out the accumulated zeros as Z00000000-------
            if (zeros > 0) {
                // how many longs did we not write?
                suppressed += zeros - 1;
//System.out.println("SUPPRESSED %u\n",suppressed);

                // don't use 'Z' for only 1 zero
                if (zeros == 1) {
                    sb.append("0000000000000000");
                }
                else {
                    sb.append("Z00000000");
                    sb.append( toASCII[ zeros >> 24 & 0xff ].charAt(1) );
                    sb.append( toASCII[ zeros >> 16 & 0xff ] );
                    sb.append( toASCII[ zeros >>  8 & 0xff ] );
                    sb.append( toASCII[ zeros       & 0xff ] );
                }

                if (thisOneZero) {
                    if (i < count - 1) {
                        sb.append(" ");
                        zeros = 0;
                        thisOneZero = false;
                        continue;
                    }
                    else {
                        sb.append("\n");
                        break;
                    }
                }
                // this one is NOT zero, just wrote out the accumumlated zeros
                sb.append(" ");
                zeros = 0;
                thisOneZero = false;
            }

            sb.append( toASCII[ (int) (j64>>56 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>48 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>40 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>32 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>24 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>16 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>> 8 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64     & 0xffL) ] );

            if (i < count-1) {
                sb.append(" ");
            } else {
                sb.append("\n");
            }
        }

        noHeaderLen = (16+1)*(count - suppressed);
        addArray(sb);
    }


    /**
     * This method stores a String (text representation) of a payload item consisting of
     * a float array.
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param f float array to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addFloat(String name, float[] f, String txt, boolean isSystem, int noHeadLen) {
        item  = f;
        count = f.length;
        type  = cMsgConstants.payloadFltA;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }

    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a float array.
     *
     * @param name name of item
     * @param f float array to be part of the payload
     * @param isSystem is the given string a system field?
     * @param copy if true make full copy of array else only copy reference
     */
    private void addFloat(String name, float[] f, boolean isSystem, boolean copy) {
        if (copy) {
            item  = f.clone();
        }
        else {
            item  = f;
        }
        count = f.length;
        type  = cMsgConstants.payloadFltA;
        this.name = name;
        this.isSystem = isSystem;

        // guess at how much memory we need
        int lenGuess = (8+1)*count; // (length - 1)spaces + 1 newline
        StringBuilder sb = new StringBuilder(lenGuess + 100);

        // Add each array element as a string of 16 hex characters.
        // Do zero suppression as well.
        int j32, zeros=0, suppressed=0;
        boolean thisOneZero=false;

        for (int i = 0; i < count; i++) {
            // bit pattern of float written into int
            j32 = Float.floatToIntBits(f[i]);

            // both values are zero in IEEE754
            if (j32 == 0 || j32 == 0x80000000) {
                if ((++zeros < 0xfffffff) && (i < count-1)) {
                    continue;
                }
                thisOneZero = true;
            }

            // If the # of zero have reached their limit or the current # is not zero,
            // write out the accumulated zeros as Z-------
            if (zeros > 0) {
                // how many floats did we not write?
                suppressed += zeros - 1;
//System.out.println("SUPPRESSED %u\n",suppressed);

                // don't use 'Z' for only 1 zero
                if (zeros == 1) {
                    sb.append("00000000");
                }
                else {
                    sb.append("Z");
                    sb.append( toASCII[ zeros >> 24 & 0xff ].charAt(1) );
                    sb.append( toASCII[ zeros >> 16 & 0xff ] );
                    sb.append( toASCII[ zeros >>  8 & 0xff ] );
                    sb.append( toASCII[ zeros       & 0xff ] );
                }

                if (thisOneZero) {
                    if (i < count - 1) {
                        sb.append(" ");
                        zeros = 0;
                        thisOneZero = false;
                        continue;
                    }
                    else {
                        sb.append("\n");
                        break;
                    }
                }
                // this one is NOT zero, just wrote out the accumumlated zeros
                sb.append(" ");
                zeros = 0;
                thisOneZero = false;
            }

            sb.append( toASCII[ j32>>24 & 0xff ] );
            sb.append( toASCII[ j32>>16 & 0xff ] );
            sb.append( toASCII[ j32>> 8 & 0xff ] );
            sb.append( toASCII[ j32     & 0xff ] );

            if (i < count-1) {
                sb.append(" ");
            } else {
                sb.append("\n");
            }
        }

        noHeaderLen = (8+1)*(count - suppressed);
        addArray(sb);
    }


    /**
     * This method stores a String (text representation) of a payload item consisting of
     * a double array.
     * Used internally when decoding a text representation of the payload into cMsgPayloadItems.
     *
     * @param name name of item
     * @param d double array to be part of the payload
     * @param txt text representation of the payload item
     * @param isSystem is the given string a system field?
     * @param noHeadLen length of the text representation NOT including the header line
     */
    private void addDouble(String name, double[] d, String txt, boolean isSystem, int noHeadLen) {
        item  = d;
        count = d.length;
        type  = cMsgConstants.payloadDblA;
        this.name = name;
        this.isSystem = isSystem;
        text = txt;
        noHeaderLen = noHeadLen;
    }


    /**
     * This method creates a String (text representation) of a payload item consisting of
     * a double array.
     *
     * @param name name of item
     * @param d double array to be part of the payload
     * @param isSystem is the given string a system field?
     * @param copy if true make full copy of array else only copy reference
     */
    private void addDouble(String name, double[] d, boolean isSystem, boolean copy) {
        if (copy) {
            item  = d.clone();
        }
        else {
            item  = d;
        }
        count = d.length;
        type  = cMsgConstants.payloadDblA;
        this.name = name;
        this.isSystem = isSystem;

        // guess at how much memory we need
        int lenGuess = (16+1)*count; // (length - 1)spaces + 1 newline
        StringBuilder sb = new StringBuilder(lenGuess + 100);

        // Add each array element as a string of 16 hex characters.
        // Do zero suppression as well.
        int zeros=0, suppressed=0;
        boolean thisOneZero=false;
        long j64;

        for (int i = 0; i < count; i++) {            
            // bit pattern of double written into long
            j64 = Double.doubleToLongBits(d[i]);

            // both values are zero in IEEE754
            if (j64 == 0L || j64 == 0x8000000000000000L) {
                if ((++zeros < 0xfffffff) && (i < count-1)) {
                    continue;
                }
                thisOneZero = true;
            }

            // If the # of zero have reached their limit or the current # is not zero,
            // write out the accumulated zeros as Z00000000-------
            if (zeros > 0) {
                // how many doubles did we not write?
                suppressed += zeros - 1;
//System.out.println("SUPPRESSED %u\n",suppressed);

                // don't use 'Z' for only 1 zero
                if (zeros == 1) {
                    sb.append("0000000000000000");
                }
                else {
                    sb.append("Z00000000");
                    sb.append( toASCII[ zeros >> 24 & 0xff ].charAt(1) );
                    sb.append( toASCII[ zeros >> 16 & 0xff ] );
                    sb.append( toASCII[ zeros >>  8 & 0xff ] );
                    sb.append( toASCII[ zeros       & 0xff ] );
                }

                if (thisOneZero) {
                    if (i < count - 1) {
                        sb.append(" ");
                        zeros = 0;
                        thisOneZero = false;
                        continue;
                    }
                    else {
                        sb.append("\n");
                        break;
                    }
                }
                // this one is NOT zero, just wrote out the accumumlated zeros
                sb.append(" ");
                zeros = 0;
                thisOneZero = false;
            }

            sb.append( toASCII[ (int) (j64>>56 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>48 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>40 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>32 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>24 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>>16 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64>> 8 & 0xffL) ] );
            sb.append( toASCII[ (int) (j64     & 0xffL) ] );

            if (i < count-1) {
                sb.append(" ");
            } else {
                sb.append("\n");
            }
        }

        noHeaderLen = (16+1)*(count - suppressed);
        addArray(sb);
    }

    /**
     * This is a utility method which takes a StringBuilder representation of any type of array
     * and creates a String (text representation) of an associated payload item.
     *
     * @param sb is the StringBuilder representation of an array
     */
    private void addArray(StringBuilder sb) {
         StringBuilder buf = new StringBuilder(100);
         buf.append(name);              buf.append(" ");
         buf.append(type);              buf.append(" ");
         buf.append(count);             buf.append(" ");
         buf.append(isSystem ? 1 : 0);  buf.append(" ");
         buf.append(noHeaderLen);       buf.append("\n");

         sb.insert(0, buf);
         text = sb.toString();
     }

    //-------------
    // GENERIC GETS
    //-------------

    /**
     * Get the name of this payload item.
     * @return name of this payload item
     */
    public String getName() {
        return name;
    }

    /**
     * Get the payload item itself as an object of class Object.
     * @return payload item as an object of class Object
     */
    public Object getItem() {
        return item;
    }

    /**
     * Get the type of this payload item.
     * The type has one of the following values:
     * <UL>
     * <LI>{@link cMsgConstants#payloadStr}         for a   String
     * <LI>{@link cMsgConstants#payloadFlt}         for a   4 byte float
     * <LI>{@link cMsgConstants#payloadDbl}         for an  8 byte float
     * <LI>{@link cMsgConstants#payloadInt8}        for an  8 bit int
     * <LI>{@link cMsgConstants#payloadInt16}       for a  16 bit int
     * <LI>{@link cMsgConstants#payloadInt32}       for a  32 bit int
     * <LI>{@link cMsgConstants#payloadInt64}       for a  64 bit int
     * <LI>{@link cMsgConstants#payloadUint8}       for an unsigned  8 bit int
     * <LI>{@link cMsgConstants#payloadUint16}      for an unsigned 16 bit int
     * <LI>{@link cMsgConstants#payloadUint32}      for an unsigned 32 bit int
     * <LI>{@link cMsgConstants#payloadUint64}      for an unsigned 64 bit int
     * <LI>{@link cMsgConstants#payloadMsg}         for a  cMsg message
     * <LI>{@link cMsgConstants#payloadBin}         for    binary
     * <p/>
     * <LI>{@link cMsgConstants#payloadStrA}        for a   String array
     * <LI>{@link cMsgConstants#payloadFltA}        for a   4 byte float array
     * <LI>{@link cMsgConstants#payloadDblA}        for an  8 byte float array
     * <LI>{@link cMsgConstants#payloadInt8A}       for an  8 bit int array
     * <LI>{@link cMsgConstants#payloadInt16A}      for a  16 bit int array
     * <LI>{@link cMsgConstants#payloadInt32A}      for a  32 bit int array
     * <LI>{@link cMsgConstants#payloadInt64A}      for a  64 bit int array
     * <LI>{@link cMsgConstants#payloadUint8A}      for an unsigned  8 bit int array
     * <LI>{@link cMsgConstants#payloadUint16A}     for an unsigned 16 bit int array
     * <LI>{@link cMsgConstants#payloadUint32A}     for an unsigned 32 bit int array
     * <LI>{@link cMsgConstants#payloadUint64A}     for an unsigned 64 bit int array
     * <LI>{@link cMsgConstants#payloadMsgA}        for a  cMsg message array
     * <LI>{@link cMsgConstants#payloadBinA}        for an array of binary items
     * </UL>
     *
     * @return type of this payload item
     */
    public int getType() {
        return type;
    }

    /**
     * Get the text representation of this payload item.
     * @return text representation of this payload item
     */
    public String getText() {
        return text;
    }

    /**
     * Get the number of elements if this payload item is an array, else return one.
     * @return number of elements if this payload item is an array, else one
     */
    public int getCount() {
        return count;
    }

    /**
     * Get the endian value if this payload item is a byte array containing binary data.
     * @return endian value if this payload item is a byte array containing binary data, else meaningless
     */
    public int getEndian() {
        return endian;
    }

    /**
     * Get the array of endian values if this payload item is an array of byte arrays containing binary data.
     * @return array of endian values if this payload item is an array of byte arrays containing binary data,
     *         else meaningless
     */
    public int[] getEndianArray() {
        return endianArray;
    }

    //----------------
    // GET STRINGS
    //----------------

    /**
     * Gets the payload item as a String object.
     * @return  payload item as a String object
     * @throws cMsgException if payload item is not of type {@link cMsgConstants#payloadStr}
     */
    public String getString() throws cMsgException {
        if (type == cMsgConstants.payloadStr)  {
            return (String)item;
        }
        throw new cMsgException("Wrong type");
    }

    /**
     * Gets the payload item as an array of String objects.
     * @return  payload item as an array of String objects
     * @throws cMsgException if payload item is not of type {@link cMsgConstants#payloadStrA}
     */
    public String[] getStringArray() throws cMsgException {
        if (type == cMsgConstants.payloadStrA)  {
            return ((String[])item).clone();
        }
        throw new cMsgException("Wrong type");
    }

    //----------------
    // GET BINARY
    //----------------

    /**
     * Gets the payload item as a byte array object holding binary data.
     * @return  payload item as a byte array object holding binary data
     * @throws cMsgException if payload item is not of type {@link cMsgConstants#payloadBin}
     */
    public byte[] getBinary() throws cMsgException {
        if (type == cMsgConstants.payloadBin)  {
            return ((byte[])item).clone();
        }
        throw new cMsgException("Wrong type");
    }

    //-----------------
    // GET BINARY ARRAY
    //-----------------

    /**
     * Gets the payload item as an array of byte array objects holding binary data.
     * @return  payload item as an array of byte array objects holding binary data
     * @throws cMsgException if payload item is not of type {@link cMsgConstants#payloadBinA}
     */
    public byte[][] getBinaryArray() throws cMsgException {
        if (type == cMsgConstants.payloadBinA)  {
            byte[][] b = new byte[count][];
            for (int i=0; i<count; i++) {
                b[i] = ((byte[][])item)[i].clone();
            }
            return b;
        }
        throw new cMsgException("Wrong type");
    }

    //----------------
    // GET MESSAGE
    //----------------

    /**
     * Gets the payload item as a cMsgMessage object.
     * @return  payload item as a cMsgMessage object
     * @throws cMsgException if payload item is not of type {@link cMsgConstants#payloadMsg}
     */
    public cMsgMessage getMessage() throws cMsgException {
        if (type == cMsgConstants.payloadMsg)  {
            return (cMsgMessage)((cMsgMessage)item).clone();
        }
        throw new cMsgException("Wrong type");
    }

    /**
     * Gets the payload item as an array of cMsgMessage objects.
     * @return  payload item as an array of cMsgMessage objects
     * @throws cMsgException if payload item is not of type {@link cMsgConstants#payloadMsgA}
     */
    public cMsgMessage[] getMessageArray() throws cMsgException {
        if (type == cMsgConstants.payloadMsgA)  {
            cMsgMessage[] m = new cMsgMessage[count];
            for (int i=0; i<count; i++) {
                m[i] = (cMsgMessage)((cMsgMessage[])item)[i].clone();
            }
            return m;
        }
        throw new cMsgException("Wrong type");
    }

    //-------------
    // GET INTEGERS
    //-------------

    /**
     * Gets the payload item as a byte (8-bit integer).
     * This method will also return all other integer types as a byte if the
     * payload item's value is in the valid range of a byte.
     *
     * @return payload item as a byte (8-bit integer)
     * @throws cMsgException if payload item is not of an integer type,
     *                       or if its value is out-of-range for a byte
     */
    public byte getByte() throws cMsgException {

        if (type == cMsgConstants.payloadInt8)  {
            return (Byte)item;
        }
        else if (type == cMsgConstants.payloadInt16 || type == cMsgConstants.payloadUint8) {
            short l = (Short)item;
            if (l <= Byte.MAX_VALUE && l >= Byte.MIN_VALUE) {
                return (byte)l;
            }
        }
        else if (type == cMsgConstants.payloadInt32 || type == cMsgConstants.payloadUint16) {
            int l = (Integer)item;
            if (l <= Byte.MAX_VALUE && l >= Byte.MIN_VALUE) {
                return (byte)l;
            }
        }
        else if (type == cMsgConstants.payloadInt64 || type == cMsgConstants.payloadUint32) {
            long l = (Long)item;
            if (l <= Byte.MAX_VALUE && l >= Byte.MIN_VALUE) {
                return (byte)l;
            }
        }
        else if (type == cMsgConstants.payloadUint64) {
            BigInteger b = (BigInteger)item;
            if (b.compareTo(new BigInteger(""+Byte.MAX_VALUE)) <= 0) {
                return (b.byteValue());
            }
        }
        throw new cMsgException("Wrong type");
    }

    /**
     * Gets the payload item as a short (16-bit integer).
     * This method will also return all other integer types as a short if the
     * payload item's value is in the valid range of a short.
     *
     * @return payload item as a short (16-bit integer)
     * @throws cMsgException if payload item is not of an integer type,
     *                       or if its value is out-of-range for a short
     */
    public short getShort() throws cMsgException {

        if (type == cMsgConstants.payloadInt8)  {
            return (Byte)item;
        }
        else if (type == cMsgConstants.payloadInt16 || type == cMsgConstants.payloadUint8) {
            return (Short)item;
        }
        else if (type == cMsgConstants.payloadInt32 || type == cMsgConstants.payloadUint16) {
            int l = (Integer)item;
            if (l <= Short.MAX_VALUE && l >= Short.MIN_VALUE) {
                return (short)l;
            }
        }
        else if (type == cMsgConstants.payloadInt64 || type == cMsgConstants.payloadUint32) {
            long l = (Long)item;
            if (l <= Short.MAX_VALUE && l >= Short.MIN_VALUE) {
                return (short)l;
            }
        }
        else if (type == cMsgConstants.payloadUint64) {
            BigInteger b = (BigInteger)item;
            if (b.compareTo(new BigInteger(""+Short.MAX_VALUE)) <= 0) {
                return (b.shortValue());
            }
        }
        throw new cMsgException("Wrong type");
    }

    /**
     * Gets the payload item as a int (32-bit integer).
     * This method will also return all other integer types as a int if the
     * payload item's value is in the valid range of a int.
     *
     * @return payload item as a int (32-bit integer)
     * @throws cMsgException if payload item is not of an integer type,
     *                       or if its value is out-of-range for a int
     */
    public int getInt() throws cMsgException {
        if (type == cMsgConstants.payloadInt8) {
            return (Byte)item;
        }
        else if (type == cMsgConstants.payloadInt16 || type == cMsgConstants.payloadUint8)  {
            return (Short)item;
        }
        else if (type == cMsgConstants.payloadInt32  || type == cMsgConstants.payloadUint16) {
            return (Integer)item;
        }
        else if (type == cMsgConstants.payloadUint32 || type == cMsgConstants.payloadInt64) {
            long l = (Long)item;
            if (l <= Integer.MAX_VALUE && l >= Integer.MIN_VALUE) {
                return (int)l;
            }
        }
        else if (type == cMsgConstants.payloadUint64) {
            BigInteger b = (BigInteger)item;
            if (b.compareTo(new BigInteger(""+Integer.MAX_VALUE)) <= 0) {
                return (b.intValue());
            }
        }
        throw new cMsgException("Wrong type");
    }

    /**
     * Gets the payload item as a long (64-bit integer).
     * This method will also return all other integer types as a long if the
     * payload item's value is in the valid range of a long.
     *
     * @return payload item as a long (64-bit integer)
     * @throws cMsgException if payload item is not of an integer type,
     *                       or if its value is out-of-range for a long
     */
    public long getLong() throws cMsgException {
        if (type == cMsgConstants.payloadInt8) {
            return (Byte)item;
        }
        else if (type == cMsgConstants.payloadInt16 || type == cMsgConstants.payloadUint8)  {
            return (Short)item;
        }
        else if (type == cMsgConstants.payloadInt32 || type == cMsgConstants.payloadUint16) {
            return (Integer)item;
        }
        else if (type == cMsgConstants.payloadInt64 || type == cMsgConstants.payloadUint32) {
            return (Long)item;
        }
        else if (type == cMsgConstants.payloadUint64) {
            BigInteger b = (BigInteger)item;
            if (b.compareTo(new BigInteger(""+Long.MAX_VALUE)) <= 0) {
                return (b.longValue());
            }
        }
        throw new cMsgException("Wrong type");
    }

    /**
     * Gets the payload item as a BigInteger object. This method is designed
     * to work with 64-bit, unsigned integer types, but also works with all
     * integer types.
     *
     * @return payload item as a BigInteger object
     * @throws cMsgException if payload item is not of an integer type
     */
    public BigInteger getBigInt() throws cMsgException {

        if (type == cMsgConstants.payloadInt8)  {
            return new BigInteger(item.toString());
        }
        else if (type == cMsgConstants.payloadInt16 || type == cMsgConstants.payloadUint8)  {
            return new BigInteger(item.toString());
        }
        else if (type == cMsgConstants.payloadInt32 || type == cMsgConstants.payloadUint16) {
            return new BigInteger(item.toString());
        }
        else if (type == cMsgConstants.payloadInt64 || type == cMsgConstants.payloadUint32) {
            return new BigInteger(item.toString());
        }
        else if (type == cMsgConstants.payloadUint64) {
            return (BigInteger)item;
        }
        throw new cMsgException("Wrong type");
    }

    //-------------------
    // GET INTEGER ARRAYS
    //-------------------

    /**
     * Gets the payload item as an array of bytes (8-bit integers).
     * This method will also return all other integer array types as a byte array if the
     * payload item's array values are in the valid range of a byte. Note that it is
     * somewhat inefficient to convert large arrays from one integer type to another and
     * have the bounds of each conversion checked.
     *
     * @return payload item as an array of bytes (8-bit integers)
     * @throws cMsgException if payload item is not of an integer array type,
     *                       or if its array values are out-of-range for a byte
     */
    public byte[] getByteArray() throws cMsgException {

        if (type == cMsgConstants.payloadInt8A)  {
            return ((byte[])item).clone();
        }
        else if (type == cMsgConstants.payloadInt16A || type == cMsgConstants.payloadUint8A)  {
            short[] S = (short[]) item;
            byte[] b  = new byte[S.length];
            for (int j = 0; j < S.length; j++) {
                if (S[j] > Byte.MAX_VALUE || S[j] < Byte.MIN_VALUE) {
                    throw new cMsgException("Cannot retrieve item as byte array, values out-of-range");
                }
                b[j] = (byte) S[j];
            }
            return b;
        }
        else if (type == cMsgConstants.payloadInt32A || type == cMsgConstants.payloadUint16A) {
            int[] I  = (int[]) item;
            byte[] b = new byte[I.length];
            for (int j = 0; j < I.length; j++) {
                if (I[j] > Byte.MAX_VALUE || I[j] < Byte.MIN_VALUE) {
                    throw new cMsgException("Cannot retrieve item as byte array, values out-of-range");
                }
                b[j] = (byte) I[j];
            }
            return b;
        }
        else if (type == cMsgConstants.payloadInt64A || type == cMsgConstants.payloadUint32A) {
            long[] L = (long[]) item;
            byte[] b = new byte[L.length];
            for (int j = 0; j < L.length; j++) {
                if (L[j] > Byte.MAX_VALUE || L[j] < Byte.MIN_VALUE) {
                    throw new cMsgException("Cannot retrieve item as byte array, values out-of-range");
                }
                b[j] = (byte) L[j];
            }
            return b;
        }
        else if (type == cMsgConstants.payloadUint64A) {
            BigInteger[] big = (BigInteger[]) item;
            BigInteger max = new BigInteger("" + Byte.MAX_VALUE);
            int j=0;
            byte[] b = new byte[big.length];
            for (BigInteger bigI : big) {
                if ( bigI.compareTo(max) > 0 ) {
                    throw new cMsgException("Cannot retrieve item as byte array, values out-of-range");
                }
                b[j++] = bigI.byteValue();
            }
            return b;
        }
        throw new cMsgException("Wrong type");
    }


    /**
     * Gets the payload item as an array of shorts (16-bit integers).
     * This method will also return all other integer array types as a short array if the
     * payload item's array values are in the valid range of a short. Note that it is
     * somewhat inefficient to convert large arrays from one integer type to another and
     * have the bounds of each conversion checked.
     *
     * @return payload item as an array of shorts (16-bit integers)
     * @throws cMsgException if payload item is not of an integer array type,
     *                       or if its array values are out-of-range for a short
     */
    public short[] getShortArray() throws cMsgException {

        if (type == cMsgConstants.payloadInt8A) {
            byte[] B  = (byte[]) item;
            short[] s = new short[B.length];
            for (int j=0; j<B.length; j++) { s[j] = B[j]; }
            return s;
        }
        else if (type == cMsgConstants.payloadInt16A || type == cMsgConstants.payloadUint8A)  {
            return ((short[])item).clone();
        }
        else if (type == cMsgConstants.payloadInt32A || type == cMsgConstants.payloadUint16A) {
            int[] I  = (int[])item;
            short[] s = new short[I.length];
            for (int j=0; j<I.length; j++) {
                if (I[j] > Short.MAX_VALUE || I[j] < Short.MIN_VALUE) {
                    throw new cMsgException("Cannot retrieve item as short array, values out-of-range");
                }
                s[j] = (short)I[j];
            }
            return s;
        }
        else if (type == cMsgConstants.payloadInt64A || type == cMsgConstants.payloadUint32A) {
                long[] L  = (long[])item;
                short[] s = new short[L.length];
                for (int j=0; j<L.length; j++) {
                     if (L[j] > Short.MAX_VALUE || L[j] < Short.MIN_VALUE) {
                        throw new cMsgException("Cannot retrieve item as short array, values out-of-range");
                    }
                    s[j] = (short)L[j];
                }
                return s;
        }
        else if (type == cMsgConstants.payloadUint64A) {
            BigInteger[] big = (BigInteger[])item;
            BigInteger max = new BigInteger(""+Short.MAX_VALUE);
            int j=0;
            short[] s = new short[big.length];
            for (BigInteger bigI : big) {
                if ( bigI.compareTo(max) > 0 ) {
                    throw new cMsgException("Cannot retrieve item as short array, values out-of-range");
                }
                s[j++] = bigI.shortValue();
            }
            return s;
        }
        throw new cMsgException("Wrong type");
    }


    /**
     * Gets the payload item as an array of ints (32-bit integers).
     * This method will also return all other integer array types as a int array if the
     * payload item's array values are in the valid range of a int. Note that it is
     * somewhat inefficient to convert large arrays from one integer type to another and
     * have the bounds of each conversion checked.
     *
     * @return payload item as an array of ints (32-bit integers)
     * @throws cMsgException if payload item is not of an integer array type,
     *                       or if its array values are out-of-range for a int
     */
    public int[] getIntArray() throws cMsgException {

        if (type == cMsgConstants.payloadInt8A)  {
            byte[] B = (byte[])item;
            int[] i  = new int[B.length];
            for (int j=0; j<B.length; j++) { i[j] = B[j]; }
            return i;
        }
        else if (type == cMsgConstants.payloadInt16A || type == cMsgConstants.payloadUint8A)  {
            short[] S = ((short[])item);
            int[] i   = new int[S.length];
            for (int j=0; j<S.length; j++) { i[j] = S[j]; }
            return i;
        }
        else if (type == cMsgConstants.payloadInt32A || type == cMsgConstants.payloadUint16A) {
            return ((int[])item).clone();
        }
        else if (type == cMsgConstants.payloadInt64A || type == cMsgConstants.payloadUint32A) {
            long[] L = (long[]) item;
            int[] i  = new int[L.length];
            for (int j = 0; j < L.length; j++) {
                if (L[j] > Integer.MAX_VALUE || L[j] < Integer.MIN_VALUE) {
                    throw new cMsgException("Cannot retrieve item as int array, values out-of-range");
                }
                i[j] = (int)L[j];
            }
            return i;
        }
        else if (type == cMsgConstants.payloadUint64A) {
            BigInteger[] big = (BigInteger[])item;
            BigInteger max = new BigInteger(""+Integer.MAX_VALUE);
            int j=0;
            int[] i = new int[big.length];
            for (BigInteger bigI : big) {
                if ( bigI.compareTo(max) > 0 ) {
                    throw new cMsgException("Cannot retrieve item as int array, values out-of-range");
                }
                i[j++] = bigI.intValue();
            }
            return i;
        }
        throw new cMsgException("Wrong type");
    }


    /**
     * Gets the payload item as an array of longs (64-bit integers).
     * This method will also return all other integer array types as a long array if the
     * payload item's array values are in the valid range of a long. Note that it is
     * somewhat inefficient to convert large arrays from one integer type to another and
     * have the bounds of each conversion checked.
     *
     * @return payload item as an array of longs (64-bit integers)
     * @throws cMsgException if payload item is not of an integer array type,
     *                       or if its array values are out-of-range for a long
     */
    public long[] getLongArray() throws cMsgException {

        if (type == cMsgConstants.payloadInt8A)  {
            byte[] B = (byte[])item;
            long[] l = new long[B.length];
            // since System.arraycopy does NOT work between arrays of primitive types ...
            for (int i=0; i<B.length; i++) { l[i] = B[i]; }
            return l;
        }
        else if (type == cMsgConstants.payloadInt16A || type == cMsgConstants.payloadUint8A)  {
            short[] S = (short[])item;
            long[] l  = new long[S.length];
            for (int i=0; i<S.length; i++) { l[i] = S[i]; }
            return l;
        }
        else if (type == cMsgConstants.payloadInt32A || type == cMsgConstants.payloadUint16A) {
            int[]  I = (int[])item;
            long[] l = new long[I.length];
            for (int i=0; i<I.length; i++) { l[i] = I[i]; }
            return l;
        }
        else if (type == cMsgConstants.payloadInt64A || type == cMsgConstants.payloadUint32A) {
            return ((long[])item).clone();
        }
        else if (type == cMsgConstants.payloadUint64A) {
            BigInteger[] big = (BigInteger[])item;
            BigInteger max = new BigInteger(""+Long.MAX_VALUE);
            int i=0;
            long[] l = new long[big.length];
            for (BigInteger bigI : big) {
                if ( bigI.compareTo(max) > 0 ) {
                    throw new cMsgException("Cannot retrieve item as long array, values out-of-range");
                }
                l[i++] = bigI.longValue();
            }
            return l;
        }
        throw new cMsgException("Wrong type");
    }


    /**
     * Gets the payload item as an array of BigInteger objects. This method is designed
     * to work with 64-bit, unsigned integer types, but also works with all
     * integer types.
     * Note that it is inefficient to convert arrays from primitive integer types to
     * BigInteger objects.
     *
     * @return payload item as an array of BigInteger objects
     * @throws cMsgException if payload item is not of an integer array type
     */
    public BigInteger[] getBigIntArray() throws cMsgException {

        if (type == cMsgConstants.payloadInt8A)  {
            byte[] B = (byte[])item;
            BigInteger[] bi = new BigInteger[B.length];
            // since System.arraycopy does NOT work between arrays of primitive types & reference types ...
            for (int i=0; i<B.length; i++) { bi[i] = new BigInteger(""+B[i]); }
            return bi;
        }
        else if (type == cMsgConstants.payloadInt16A || type == cMsgConstants.payloadUint8A)  {
            short[] S = (short[])item;
            BigInteger[] bi = new BigInteger[S.length];
            for (int i=0; i<S.length; i++) { bi[i] = new BigInteger(""+S[i]); }
            return bi;
        }
        else if (type == cMsgConstants.payloadInt32A || type == cMsgConstants.payloadUint16A) {
            int[] I = (int[])item;
            BigInteger[] bi = new BigInteger[I.length];
            for (int i=0; i<I.length; i++) { bi[i] = new BigInteger(""+I[i]); }
            return bi;
        }
        else if (type == cMsgConstants.payloadInt64A || type == cMsgConstants.payloadUint32A) {
            long[] L = (long[])item;
            BigInteger[] bi = new BigInteger[L.length];
            for (int i=0; i<L.length; i++) { bi[i] = new BigInteger(""+L[i]); }
            return bi;
        }
        else if (type == cMsgConstants.payloadUint64A) {
            BigInteger[] bi = new BigInteger[count];
            // BigInts are immutable so don't clone
            for (int i=0; i<count; i++) { bi[i] = ((BigInteger[])item)[i]; }
            return bi;
        }
        throw new cMsgException("Wrong type");
    }


    //---------------
    // GET REALS DUDE
    //---------------

    /**
     * Gets the payload item as a float.
     * This method will also return a double as a float if the
     * payload item's value is in the valid range of a float.
     *
     * @return payload item as a float
     * @throws cMsgException if payload item is not of type {@link cMsgConstants#payloadFlt},
     *                       or it's not of type {@link cMsgConstants#payloadDbl} and
     *                       its value is out-of-range for a float
     */
    public float getFloat() throws cMsgException {
        if (type == cMsgConstants.payloadFlt) {
            return (Float)item;
        }
        else if (type == cMsgConstants.payloadDbl) {
            Double l = (Double)item;
            if (l <= Float.MAX_VALUE && l >= -Float.MAX_VALUE) {
                return l.floatValue();
            }
        }
        throw new cMsgException("Wrong type");
    }

    /**
     * Gets the payload item as a double.
     * This method will also return a float as a double.
     *
     * @return payload item as a float
     * @throws cMsgException if payload item is not of type {@link cMsgConstants#payloadFlt}
     *                       or {@link cMsgConstants#payloadDbl}
     */
    public double getDouble() throws cMsgException {
        if (type == cMsgConstants.payloadFlt) {
            return (Float)item;
        }
        else if (type == cMsgConstants.payloadDbl) {
            return (Double)item;
        }
        throw new cMsgException("Wrong type");
    }

    //----------------
    // GET REAL ARRAYS
    //----------------

    /**
     * Gets the payload item as an array of floats.
     * This method will also return an array of doubles as an array of floats if the
     * payload item's values are in the valid range of a float.
     *
     * @return payload item as a float array
     * @throws cMsgException if payload item is not of type {@link cMsgConstants#payloadFltA},
     *                       or it's not of type {@link cMsgConstants#payloadDblA} and
     *                       its values are out-of-range for a float
     */
    public float[] getFloatArray() throws cMsgException {
        if (type == cMsgConstants.payloadFltA)  {
            return ((float[])item).clone();
        }
        else if (type == cMsgConstants.payloadDblA) {
            double[] D = (double[]) item;
            float[] f  = new float[D.length];
            for (int i = 0; i < D.length; i++) {
                if (D[i] > Float.MAX_VALUE || D[i] < -Float.MAX_VALUE) {
                    throw new cMsgException("Cannot retrieve item as float array, values out-of-range");
                }
                f[i] = (float)D[i];
            }
            return f;
        }
        throw new cMsgException("Wrong type");
    }

    /**
     * Gets the payload item as an array of doubles.
     * This method will also return an array of floats as an array of doubles.
     *
     * @return payload item as a double array
     * @throws cMsgException if payload item is not of type {@link cMsgConstants#payloadFltA}
     *                       or {@link cMsgConstants#payloadDblA}
     */
    public double[] getDoubleArray() throws cMsgException {
         if (type == cMsgConstants.payloadFltA)  {
             float[] F  = (float[])item;
             double[] d = new double[F.length];
             for (int i=0; i<F.length; i++) { d[i] = F[i]; }
             return d;
         }
         else if (type == cMsgConstants.payloadDblA) {
             return ((double[])item).clone();
         }
         throw new cMsgException("Wrong type");
     }


}

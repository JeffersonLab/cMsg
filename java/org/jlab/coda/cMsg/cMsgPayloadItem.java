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

import java.math.BigInteger;
import java.lang.Number;
import java.util.Collection;

/**
 * <b>This class represents an item in a cMsg message's payload.
 * The value of each item is stored in this class along with a text
 * representation of that item. Following is the text format of various
 * types of payload items ([nl] means newline, only 1 space between items):<p>
 *
 *<i>for string items:</i></b><p>
 *<pre>    item_name   item_type   item_count   isSystemItem?   item_length[nl]
 *    string_length_1[nl]
 *    string_characters_1[nl]
 *     .
 *     .
 *     .
 *    string_length_N[nl]
 *    string_characters_N</pre><p>
 *
 *<b><i>for binary (converted into text) items:</i></b><p>
 *
 *<pre>    item_name   item_type   original_binary_byte_length   isSystemItem?   item_length[nl]
 *    string_length   endian[nl]
 *    string_characters[nl]</pre><p>
 *
 *<b><i>for primitive type items:</i></b><p>
 *
 *<pre>    item_name   item_type   item_count   isSystemItem?   item_length[nl]
 *    value_1   value_2   ...   value_N[nl]</pre><p>
 *
 *  <b>A cMsg message is formatted as a compound payload. Each message has
 *  a number of fields (payload items).<p>
 *
 *  <i>for message items:</i></b><p>
 *<pre>                                                                            _
 *    item_name   item_type   item_count   isSystemItem?   item_length[nl]   /
 *    message_1_in_compound_payload_text_format[nl]                         <  field_count[nl]
 *        .                                                                  \ list_of_payload_format_items
 *        .                                                                   -
 *        .
 *    message_N_in_compound_payload_text_format[nl]</pre>
 *
 */
public class cMsgPayloadItem {

    /** Name of this item. */
    String name;

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
     * </UL>
     */
    int type;

    /** Number of items in array if array, else 1. */
    int count = 1;
    /** Length of text in chars without header (first) line. */
    int noHeaderLen;
    /** Endian value if item is binary. */
    int endian = cMsgConstants.endianBig;
    /** Is this item part of a hidden system field in the payload? */
    boolean isSystem;

    /** String representation for this item, containing name,
      * type, count, length, values, etc for wire protocol. */
    String text;

    /** Place to store the item passed to the constructor. */
    Object item;

    /** Maximum length of the name. */
    private int CMSG_PAYLOAD_NAME_LEN_MAX = 128;

    /** String containing all characters not allowed in a name. */
    private String EXCLUDED_CHARS = " \t\n`\'\"";

    /** Map the value of a byte (index) to the hex string value it represents. */
   private String toASCII[] =
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
     * Map the value of an ascii character (index) to the numerical
     * value it represents. The only characters of interest are 0-9,a-f,
     * and Z for converting hex strings back to numbers.
     */
   private byte toByte[] =
   {  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /*  0-9  */
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /* 10-19 */
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /* 20-29 */
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /* 30-39 */
      -1, -1, -1, -1, -1, -1, -1, -1,  0,  1,  /* 40-49 */
       2,  3,  4,  5,  6,  7,  8,  9, -1, -1,  /* 50-59 */
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /* 60-69 */
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /* 70-79 */
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /* 80-89 */
      -2, -1, -1, -1, -1, -1, -1, 10, 11, 12,  /* 90-99, Z maps to 90 */
      13, 14, 15}; /* 100-102 */


    /**
     * This method returns the number of digits in an integer including a minus sign.
     *
     * @param number integer
     * @return number of digits in the integer argument including a minus sign
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
     * This method returns the number of digits in an integer including a minus sign
     * for large numbers including unsigned 64 bit.
     *
     * @param number integer
     * @return number of digits in the integer argument including a minus sign
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
     * This method checks a string to see if it is a suitable payload item name.
     * It returns false if it is not, or true if it is. A check is made to see if
     * it contains any character from a list of excluded characters.
     * All names starting with "cmsg", independent of case,
     * are reserved for use by the cMsg system itself. Names may not be
     * longer than CMSG_PAYLOAD_NAME_LEN_MAX characters.
     *
     * @param name string to check
     * @param isSystem if true, allows names starting with "cmsg", else not
     *
     * @returns true if string is OK
     * @throws cMsgException if string is null, contains illegal characters, starts with
     *                       "cmsg" if not isSystem, or is too long
     */
    private void checkName(String name, boolean isSystem) throws cMsgException {
        if (name == null) {
            throw new cMsgException("name argument is null");
        }

        // check for excluded chars
        if (name.contains(EXCLUDED_CHARS)) {
            throw new cMsgException("name contains illegal characters");
        }

        // check for starting with cmsg
        if (!isSystem && name.toLowerCase().startsWith("cmsg")) {
            throw new cMsgException("names may not start with \"cmsg\"");
        }

        // check for length */
        if (name.length() > CMSG_PAYLOAD_NAME_LEN_MAX){
            throw new cMsgException("name too long, " + CMSG_PAYLOAD_NAME_LEN_MAX + " chars allowed" );
        }
    }


    /**
     * This method checks an integer to make sure it has the proper value for an endian argument
     * (cMsgConstants.endianBig, .endianLittle, .endianLocal, or .endianNotLocal). It returns
     * the final endian value of either cMsgConstants.endianBig or .endianLittle. In other words,
     * cMsgConstants.endianLocal, or .endianNotLocal are transformed to big or little.
     *
     * @param endian value to check
     *
     * @returns either cMsgConstants.endianBig or cMsgConstants.endianLittle
     * @throws cMsgException if endian is an improper value
     */
    private int checkEndian(int endian) throws cMsgException {
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


    //--------------------------
    // Constructors, String
    //--------------------------
    public cMsgPayloadItem(String name, String s) throws cMsgException {
        checkName(name, false);
        addString(name, s, false);
    }

    public cMsgPayloadItem(String name, String[] s) throws cMsgException {
        checkName(name, false);
        addString(name, s, false);
    }

    //--------------------------
    // Constructors, Binary
    //--------------------------
    public cMsgPayloadItem(String name, byte[] b, int end) throws cMsgException {
        checkName(name, false);
        endian = checkEndian(end);
        addBinary(name, b, false);
    }

    //--------------------------
    // Constructors, Message
    //--------------------------
    public cMsgPayloadItem(String name, cMsgMessage msg) throws cMsgException {
        checkName(name, false);
        addMessage(name, msg, false);
    }

    //-------------------
    // Constructors, Ints
    //-------------------

    public cMsgPayloadItem(String name, byte b) throws cMsgException {
        checkName(name, false);
        addByte(name, b, false);
    }

    public cMsgPayloadItem(String name, short s) throws cMsgException {
        checkName(name, false);
        addShort(name, s, false);
    }

    public cMsgPayloadItem(String name, int i) throws cMsgException {
        checkName(name, false);
        addInt(name, i, false);
    }

    public cMsgPayloadItem(String name, long i) throws cMsgException {
        checkName(name, false);
        addLong(name, i, false);
    }

    public cMsgPayloadItem(String name, BigInteger big) throws cMsgException {
        checkName(name, false);
        if ((big.compareTo(new BigInteger("18446744073709551615")) > 0) ||
            (big.compareTo(BigInteger.ZERO) < 0) ) throw new cMsgException("number must fit in an unsigned, 64-bit int");
        addNumber(name, big, cMsgConstants.payloadUint64, false);
    }

    public <T extends Number> cMsgPayloadItem(String name, T t) throws cMsgException {
        int typ;
        String s = t.getClass().getName();

        checkName(name, false);

        // casts & "instance of" does not work with generics on naked types (on t), so use getClass()

        if (s.equals("java.lang.Byte")) {
            typ = cMsgConstants.payloadInt8;
            System.out.println("Got type = Byte !!");
        }
        else if (s.equals("java.lang.Short")) {
            typ = cMsgConstants.payloadInt16;
        }
        else if (s.equals("java.lang.Integer")) {
            typ = cMsgConstants.payloadInt32;            
        }
        else if (s.equals("java.lang.Long")) {
            typ = cMsgConstants.payloadInt64;
        }
        else if (s.equals("java.lang.Float")) {
            typ = cMsgConstants.payloadFlt;
        }
        else if (s.equals("java.lang.Double")) {
            typ = cMsgConstants.payloadDbl;
        }
        else {
            throw new cMsgException("Type T" + s + " not allowed");
        }

        addNumber(name, t, typ, false);
    }

    //--------------------------
    // Constructors, Ints Arrays
    //--------------------------

    public cMsgPayloadItem(String name, byte[] b) throws cMsgException {
        checkName(name, false);
        addByte(name, b, false);
    }

    public cMsgPayloadItem(String name, short[] s) throws cMsgException {
        checkName(name, false);
        addShort(name, s, false);
    }

    public cMsgPayloadItem(String name, int[] i) throws cMsgException {
        checkName(name, false);
        addInt(name, i, false);
    }

    public cMsgPayloadItem(String name, long[] l) throws cMsgException {
        checkName(name, false);
        addLong(name, l, false);
    }

    public cMsgPayloadItem(String name, BigInteger[] big) throws cMsgException {
        checkName(name, false);
        for (BigInteger bi : big) {
            if ((bi.compareTo(new BigInteger("18446744073709551615")) > 0) ||
                (bi.compareTo(BigInteger.ZERO) < 0) )
                throw new cMsgException("number must fit in an unsigned, 64-bit int");
        }
        addNumber(name, big, cMsgConstants.payloadUint64A, false);
    }

    public <T extends Number> cMsgPayloadItem(String name, T[] t) throws cMsgException {
         int typ;
         String s = t[0].getClass().getName();
         System.out.println("class = " + s);

         checkName(name, false);

         // casts & "instance of" does not work with generics on naked types (on t), so use getClass()

         if (s.equals("java.lang.Byte")) {
             typ = cMsgConstants.payloadInt8;
             System.out.println("Got type = Byte !!");
         }
         else if (s.equals("java.lang.Short")) {
             typ = cMsgConstants.payloadInt16;
             System.out.println("Got type = Short !!");
         }
         else if (s.equals("java.lang.Integer")) {
             typ = cMsgConstants.payloadInt32;
         }
         else if (s.equals("java.lang.Long")) {
             typ = cMsgConstants.payloadInt64;
         }
         else if (s.equals("java.lang.Float")) {
             typ = cMsgConstants.payloadFlt;
         }
         else if (s.equals("java.lang.Double")) {
             typ = cMsgConstants.payloadDbl;
         }
         else {
             throw new cMsgException("Type T[]" + s + " not allowed");
         }

         addNumber(name, t, typ, false);
     }

    //-----------
    // Reals
    //-----------

    public cMsgPayloadItem(String name, float f) throws cMsgException {
        checkName(name, false);
        addFloat(name, f, false);
    }

    public cMsgPayloadItem(String name, double d) throws cMsgException {
        checkName(name, false);
        addDouble(name, d, false);
    }

    //------------
    // Real Arrays
    //------------

    public cMsgPayloadItem(String name, float[] f) throws cMsgException {
        checkName(name, false);
        addFloat(name, f, false);
    }

    public cMsgPayloadItem(String name, double[] d) throws cMsgException {
        checkName(name, false);
        addDouble(name, d, false);
    }


    //------------------------------
    // Number is superclass of Byte, Short, Integer, Long, Float, Double, BigDecimal, BigInteger,
    // AtomicInteger, and AtomicLong
    //------------------------------

    //------------
    // ADD STRING
    //------------

    private void addString(String name, String val, boolean isSystem) {
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

    //-----------------
    // ADD STRING ARRAY
    //-----------------

    private void addString(String name, String[] vals, boolean isSystem) {
        item  = vals;
        count = vals.length;
        type  = cMsgConstants.payloadStrA;
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

    //------------
    // ADD BINARY
    //------------

    private void addBinary(String name, byte[] bin, boolean isSystem) {
        item = bin;
        type = cMsgConstants.payloadBin;
        this.name = name;
        this.isSystem = isSystem;

        // Create string to hold all data to be transferred over
        // the network for this item. So first convert binary to
        // text (with linebreaks every 76 chars).
        String encodedBin = Base64.encodeToString(bin, true); // newline stuck on end
        noHeaderLen = numDigits(encodedBin.length()) + encodedBin.length() + 3;  // endian value, space, 1 newline
//        System.out.println("addBinary: encodedBin len = " + encodedBin.length() +
//                           ", num digits = " + numDigits(encodedBin.length()) +
//                           ", add 3 for endian len, space, nl");

        StringBuffer buf = new StringBuffer(noHeaderLen + 100);
        buf.append(name);                 buf.append(" ");
        buf.append(type);                 buf.append(" ");
        buf.append(count);                buf.append(" ");
        buf.append(isSystem ? 1 : 0);     buf.append(" ");
        buf.append(noHeaderLen);          buf.append("\n");
        buf.append(encodedBin.length());  buf.append(" ");
        buf.append(endian);               buf.append("\n");
        buf.append(encodedBin);

        text = buf.toString();
    }


    //------------
    // ADD MESSAGE
    //------------

    private void systemStringToBuf(String name, StringBuilder buf, String s) {
        int len = numDigits(s.length()) +s.length() + 2; // 2 newlines
        buf.append(name);       buf.append(" ");
        buf.append(cMsgConstants.payloadStr);
        buf.append(" 1 1 ");
        buf.append(len);         buf.append("\n");
        buf.append(s.length());  buf.append("\n");
        buf.append(s);           buf.append("\n");
    }

    private void addMessage(String name, cMsgMessage msg, boolean isSystem) {
        item = msg;
        type = cMsgConstants.payloadBin;
        this.name = name;
        this.isSystem = isSystem;

        // Create string to hold all data to be transferred over
        // the network for this item. Try to make sure we have some
        // room in the buffer for the large items plus 2K extra.
        int size = 0;
        String payloadRep = msg.getPayload().getItemsText();
        if (payloadRep    != null)  size += payloadRep.length();
        if (msg.getText() != null)  size += msg.getText().length();
        size += msg.getByteArrayLength()*1.4;

        count = 0;
        StringBuilder buf = new StringBuilder(size + 2048);

        // Start adding message fields to string in payload item format
        if (msg.getDomain() != null) {
            systemStringToBuf("cMsgDomain", buf, msg.getDomain());
            count++;
        }
        if (msg.getSubject() != null) {
            systemStringToBuf("cMsgSubject", buf, msg.getSubject());
            count++;
        }
        if (msg.getType() != null) {
            systemStringToBuf("cMsgType", buf, msg.getType());
            count++;
        }
        if (msg.getText() != null) {
            systemStringToBuf("cMsgText", buf, msg.getText());
            count++;
        }
        if (msg.getCreator() != null) {
            systemStringToBuf("cMsgCreator", buf, msg.getCreator());
            count++;
        }
        if (msg.getSender() != null) {
            systemStringToBuf("cMsgSender", buf, msg.getSender());
            count++;
        }
        if (msg.getSenderHost() != null) {
            systemStringToBuf("cMsgSenderHost", buf, msg.getSenderHost());
            count++;
        }
        if (msg.getReceiver() != null) {
            systemStringToBuf("cMsgReceiver", buf, msg.getReceiver());
            count++;
        }
        if (msg.getReceiverHost() != null) {
            systemStringToBuf("cMsgReceiverHost", buf, msg.getReceiverHost());
            count++;
        }

        // add an array of integers
        StringBuilder sb = new StringBuilder(100);
        sb.append(msg.getVersion());         sb.append(" ");
        sb.append(msg.getInfo());            sb.append(" ");
        sb.append(msg.reserved);             sb.append(" ");
        sb.append(msg.getByteArrayLength()); sb.append(" ");
        sb.append(msg.getUserInt());         sb.append("\n");

        buf.append("cMsgInts ");
        buf.append(cMsgConstants.payloadInt32A);
        buf.append(" 5 1 ");
        buf.append(sb.length());  buf.append("\n");
        buf.append(sb);
        count++;

        // Send a time as 2, 64 bit integers - one for seconds, one for nanoseconds.
        // In java, time in only kept in milliseconds, so convert.
        sb.delete(0,sb.length());
        long t = msg.getUserTime().getTime();
        sb.append(t/1000);   // sec
        sb.append(" ");
        sb.append((t - ((t/1000L)*1000L))*1000000L);   // nanosec
        sb.append(" ");
        t = msg.getSenderTime().getTime();
        sb.append(t/1000);   // sec
        sb.append(" ");
        sb.append((t - ((t/1000L)*1000L))*1000000L);   // nanosec
        sb.append(" ");
        t = msg.getReceiverTime().getTime();
        sb.append(t/1000);   // sec
        sb.append(" ");
        sb.append((t - ((t/1000L)*1000L))*1000000L);   // nanosec
        sb.append("\n");

        buf.append("cMsgTimes ");
        buf.append(cMsgConstants.payloadInt64A);
        buf.append(" 6 1 ");
        buf.append(sb.length());  buf.append("\n");
        buf.append(sb);
        count++;

        // send the byte array
        if (msg.getByteArrayLength() > 0) {
            String encodedBin = Base64.encodeToString(msg.getByteArray(), msg.getByteArrayOffset(),
                                                      msg.getByteArrayLength(), true);
            int len = numDigits(encodedBin.length()) + encodedBin.length() + 4;  // endian value, space, 2 newlines

            buf.append("cMsgBinary ");
            buf.append(cMsgConstants.payloadBin);
            buf.append(" 1 1 ");
            buf.append(len);                       buf.append("\n");
            buf.append(encodedBin.length());       buf.append(" ");
            buf.append(msg.getByteArrayEndian());  buf.append("\n");
            buf.append(encodedBin);                buf.append("\n");
            count++;

        }

        // add payload items of this message
        cMsgPayload payload = msg.getPayload();
        if (payload != null) {
            Collection<cMsgPayloadItem> c = payload.getItems();
            for (cMsgPayloadItem it : c) {
                buf.append(it.text);
                count++;
            }
        }

        // length of everything except header
        noHeaderLen = buf.length() + numDigits(count) + 1; // newline

        StringBuilder buf2 = new StringBuilder(noHeaderLen + 100);
        buf2.append(name);                 buf2.append(" ");
        buf2.append(type);                 buf2.append(" ");
        buf2.append(count);                buf2.append(" ");
        buf2.append(isSystem ? 1 : 0);     buf2.append(" ");
        buf2.append(noHeaderLen);          buf2.append("\n");
        buf2.append(count);                buf2.append("\n");
        buf2.append(buf);   // copy done here, but easier than calculating exact length beforehand (faster?)

        text = buf2.toString();
    }


    //------------
    // ADD NUMBERS
    //------------


    private <T extends Number> void addNumber(String name, T t, int type, boolean isSystem) {
        item = t;
        this.type = type;
        addScalar(name, t.toString(), isSystem);
    }

    private void addByte(String name, byte b, boolean isSystem) {
        item = b;
        type = cMsgConstants.payloadInt8;
        addScalar(name, ((Byte) b).toString(), isSystem);
    }

    private void addShort(String name, short s, boolean isSystem) {
        item = s;
        type = cMsgConstants.payloadInt16;
        addScalar(name, ((Short) s).toString(), isSystem);
    }

    private void addInt(String name, int i, boolean isSystem) {
        item = i;
        type = cMsgConstants.payloadInt32;
        addScalar(name, ((Integer) i).toString(), isSystem);
     }

    private void addLong(String name, long l, boolean isSystem) {
        item = l;
        type = cMsgConstants.payloadInt64;
        addScalar(name, ((Long) l).toString(), isSystem);
    }

    private void addFloat(String name, float f, boolean isSystem) {
        item = f;
        type = cMsgConstants.payloadFlt;

        // bit pattern of int written into float
        int j32 = Float.floatToIntBits(f);

        StringBuilder sb = new StringBuilder(8);
        sb.append( toASCII[ j32>>24 & 0xff ] );
        sb.append( toASCII[ j32>>16 & 0xff ] );
        sb.append( toASCII[ j32>> 8 & 0xff ] );
        sb.append( toASCII[ j32     & 0xff ] );

        addScalar(name, sb, isSystem);
    }

    private void addDouble(String name, double d, boolean isSystem) {
        item = d;
        type = cMsgConstants.payloadDbl;

        // bit pattern of double written into long
        long j64 = Double.doubleToLongBits(d);

        StringBuilder sb = new StringBuilder(16);
        sb.append( toASCII[ (int) (j64>>56 & 0xffL) ] );
        sb.append( toASCII[ (int) (j64>>48 & 0xffL) ] );
        sb.append( toASCII[ (int) (j64>>40 & 0xffL) ] );
        sb.append( toASCII[ (int) (j64>>32 & 0xffL) ] );
        sb.append( toASCII[ (int) (j64>>24 & 0xffL) ] );
        sb.append( toASCII[ (int) (j64>>16 & 0xffL) ] );
        sb.append( toASCII[ (int) (j64>> 8 & 0xffL) ] );
        sb.append( toASCII[ (int) (j64     & 0xffL) ] );

        addScalar(name, sb, isSystem);

    }

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

    private <T extends Number> void addNumber(String name, T[] t, int type, boolean isSystem) {

        // store in a primitive form if Byte, Short, Integer, Long, Float, or Double
        if (type == cMsgConstants.payloadInt8A) {
            byte[] b = new byte[t.length];
            System.arraycopy(t, 0, b, 0, t.length);
            addByte(name, b, isSystem);
        }
        else if (type == cMsgConstants.payloadInt16A) {
            short[] s = new short[t.length];
            System.arraycopy(t, 0, s, 0, t.length);
            addShort(name, s, isSystem);
        }
        else if (type == cMsgConstants.payloadInt32A) {
            int[] i = new int[t.length];
            System.arraycopy(t, 0, i, 0, t.length);
            addInt(name, i, isSystem);
        }
        else if (type == cMsgConstants.payloadInt64A) {
            long[] l = new long[t.length];
            System.arraycopy(t, 0, l, 0, t.length);
            addLong(name, l, isSystem);
        }
        else if (type == cMsgConstants.payloadFltA) {
            float[] f = new float[t.length];
            System.arraycopy(t, 0, f, 0, t.length);
            addFloat(name, f, isSystem);
        }
        else if (type == cMsgConstants.payloadDblA) {
            double[] d = new double[t.length];
            System.arraycopy(t, 0, d, 0, t.length);
            addDouble(name, d, isSystem);
        }
        else if (type == cMsgConstants.payloadUint64A) {
            addBigInt(name, (BigInteger[]) t, isSystem);
        }
    }

    private void addByte(String name, byte[] b, boolean isSystem) {
        item  = b;
        count = b.length;
        type  = cMsgConstants.payloadInt8A;
        this.name = name;
        this.isSystem = isSystem;

        // guess at how much memory we need
        noHeaderLen = (2+1)*b.length; // (length - 1)spaces + 1 newline
        StringBuilder sb = new StringBuilder(noHeaderLen + 100);

        for (int i = 0; i < b.length; i++) {
            sb.append( toASCII[ b[i] & 0xff ] );
            if (i < b.length-1) {
                sb.append(" ");
            } else {
                sb.append("\n");
            }
        }

        addArray(sb);
    }

    private void addShort(String name, short[] s, boolean isSystem) {
        item  = s;
        count = s.length;
        type  = cMsgConstants.payloadInt16A;
        this.name = name;
        this.isSystem = isSystem;

        // guess at how much memory we need
        int lenGuess = (4+1)*s.length; // (length - 1)spaces + 1 newline
        StringBuilder sb = new StringBuilder(lenGuess + 100);

        // Add each array element as a string of 4 hex characters.
        // Do zero suppression as well.
        int zeros=0, suppressed=0;
        short j16;
        boolean thisOneZero=false;

        for (int i = 0; i < s.length; i++) {
            j16 = s[i];

            if (j16 == 0) {
                if ((++zeros < 0xfff) && (i < s.length-1)) {
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
                    if (i < s.length - 1) {
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

            if (i < s.length-1) {
                sb.append(" ");
            } else {
                sb.append("\n");
            }
        }

        noHeaderLen = (4+1)*(s.length - suppressed);
        addArray(sb);
    }

    private void addInt(String name, int[] i, boolean isSystem) {
        item  = i;
        count = i.length;
        type  = cMsgConstants.payloadInt32A;
        this.name = name;
        this.isSystem = isSystem;

        // guess at how much memory we need
        int lenGuess = (8+1)*i.length; // (length - 1)spaces + 1 newline
        StringBuilder sb = new StringBuilder(lenGuess + 100);

        // Add each array element as a string of 8 hex characters.
        // Do zero suppression as well.
        int j32, zeros=0, suppressed=0;
        boolean thisOneZero=false;

        for (int j = 0; j < i.length; j++) {
            j32 = i[j];

            if (j32 == 0) {
                if ((++zeros < 0xfffffff) && (j < i.length-1)) {
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
                    if (j < i.length - 1) {
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

            if (j < i.length-1) {
                sb.append(" ");
            } else {
                sb.append("\n");
            }
        }

        noHeaderLen = (8+1)*(i.length - suppressed);
        addArray(sb);
    }

    private void addLong(String name, long[] l, boolean isSystem) {
        item  = l;
        count = l.length;
        type  = cMsgConstants.payloadInt64A;
        this.name = name;
        this.isSystem = isSystem;

        // guess at how much memory we need
        int lenGuess = (16+1)*l.length; // (length - 1)spaces + 1 newline
        StringBuilder sb = new StringBuilder(lenGuess + 100);

        // Add each array element as a string of 16 hex characters.
        // Do zero suppression as well.
        int zeros=0, suppressed=0;
        boolean thisOneZero=false;
        long j64;

        for (int i = 0; i < l.length; i++) {
            j64 = l[i];

            if (j64 == 0L) {
                if ((++zeros < 0xfffffff) && (i < l.length-1)) {
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
                    if (i < l.length - 1) {
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

            if (i < l.length-1) {
                sb.append(" ");
            } else {
                sb.append("\n");
            }
        }

        noHeaderLen = (16+1)*(l.length - suppressed);
        addArray(sb);
    }

    private void addFloat(String name, float[] f, boolean isSystem) {
        item  = f;
        count = f.length;
        type  = cMsgConstants.payloadFltA;
        this.name = name;
        this.isSystem = isSystem;

        // guess at how much memory we need
        int lenGuess = (8+1)*f.length; // (length - 1)spaces + 1 newline
        StringBuilder sb = new StringBuilder(lenGuess + 100);

        // Add each array element as a string of 16 hex characters.
        // Do zero suppression as well.
        int j32, zeros=0, suppressed=0;
        boolean thisOneZero=false;

        for (int i = 0; i < f.length; i++) {
            // bit pattern of float written into int
            j32 = Float.floatToIntBits(f[i]);

            // both values are zero in IEEE754
            if (j32 == 0 || j32 == 0x80000000) {
                if ((++zeros < 0xfffffff) && (i < f.length-1)) {
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
                    if (i < f.length - 1) {
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

            if (i < f.length-1) {
                sb.append(" ");
            } else {
                sb.append("\n");
            }
        }

        noHeaderLen = (8+1)*(f.length - suppressed);
        addArray(sb);
    }

    private void addDouble(String name, double[] d, boolean isSystem) {
        item  = d;
        count = d.length;
        type  = cMsgConstants.payloadDblA;
        this.name = name;
        this.isSystem = isSystem;

        // guess at how much memory we need
        int lenGuess = (16+1)*d.length; // (length - 1)spaces + 1 newline
        StringBuilder sb = new StringBuilder(lenGuess + 100);

        // Add each array element as a string of 16 hex characters.
        // Do zero suppression as well.
        int zeros=0, suppressed=0;
        boolean thisOneZero=false;
        long j64;

        for (int i = 0; i < d.length; i++) {            
            // bit pattern of double written into long
            j64 = Double.doubleToLongBits(d[i]);

            // both values are zero in IEEE754
            if (j64 == 0L || j64 == 0x8000000000000000L) {
                if ((++zeros < 0xfffffff) && (i < d.length-1)) {
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
                    if (i < d.length - 1) {
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

            if (i < d.length-1) {
                sb.append(" ");
            } else {
                sb.append("\n");
            }
        }

        noHeaderLen = (16+1)*(d.length - suppressed);
        addArray(sb);
    }

    private void addBigInt(String name, BigInteger[] bi, boolean isSystem) {
        item  = bi;
        count = bi.length;
        type  = cMsgConstants.payloadUint64A;
        this.name = name;
        this.isSystem = isSystem;

        // guess at how much memory we need
        int lenGuess = (16+1)*bi.length; // (length - 1)spaces + 1 newline
        StringBuilder sb = new StringBuilder(lenGuess + 100);

        // Add each array element as a string of 16 hex characters.
        // Do zero suppression as well.
        int zeros=0, suppressed=0;
        boolean thisOneZero=false;
        long j64;

        for (int i = 0; i < bi.length; i++) {
            j64 = bi[i].longValue();

            if (j64 == 0L) {
                if ((++zeros < 0xfffffff) && (i < bi.length-1)) {
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
                    if (i < bi.length - 1) {
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

            if (i < bi.length-1) {
                sb.append(" ");
            } else {
                sb.append("\n");
            }
        }

        noHeaderLen = (16+1)*(bi.length - suppressed);
        addArray(sb);
    }


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

    public Object getItem() {
        return item;
    }

    public int getType() {
        return type;
    }

    public String getText() {
        return text;
    }

    public int getCount() {
        return count;
    }

    public int getEndian() {
        return endian;
    }

      //----------------
    // GET STRINGS
    //----------------

    public String getString() throws cMsgException {
        if (type == cMsgConstants.payloadStr)  {
            return (String)item;
        }
        throw new cMsgException("Wrong type");
    }

    public String[] getStringArray() throws cMsgException {
        if (type == cMsgConstants.payloadStrA)  {
            return (String[])item;
        }
        throw new cMsgException("Wrong type");
    }

    //----------------
    // GET BINARY
    //----------------

    public byte[] getBinary() throws cMsgException {
        if (type == cMsgConstants.payloadBin)  {
            return (byte[])item;
        }
        throw new cMsgException("Wrong type");
    }

    //----------------
    // GET MESSAGE
    //----------------

    public cMsgMessage getMessage() throws cMsgException {
        if (type == cMsgConstants.payloadMsg)  {
            return (cMsgMessage)item;
        }
        throw new cMsgException("Wrong type");
    }

    //-------------
    // GET INTEGERS
    //-------------

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

    public BigInteger getBigInt() throws cMsgException {

        if (type == cMsgConstants.payloadInt8A)  {
            return new BigInteger(item.toString());
        }
        else if (type == cMsgConstants.payloadInt16A || type == cMsgConstants.payloadUint8A)  {
            return new BigInteger(item.toString());
        }
        else if (type == cMsgConstants.payloadInt32A || type == cMsgConstants.payloadUint16A) {
            return new BigInteger(item.toString());
        }
        else if (type == cMsgConstants.payloadInt64A || type == cMsgConstants.payloadUint32A) {
            return new BigInteger(item.toString());
        }
        else if (type == cMsgConstants.payloadUint64A) {
            return (BigInteger)item;
        }
        throw new cMsgException("Wrong type");
    }

    //-------------------
    // GET INTEGER ARRAYS
    //-------------------

    public byte[] getByteArray() throws cMsgException {

        if (type == cMsgConstants.payloadInt8A)  {
            return (byte[])item;
        }
        else if (type == cMsgConstants.payloadInt16A || type == cMsgConstants.payloadUint8A)  {
            short[] S = (short[]) item;
            byte[] b  = new byte[S.length];
            for (int i = 0; i < S.length; i++) {
                if (S[i] > Byte.MAX_VALUE || S[i] < Byte.MIN_VALUE) {
                    throw new cMsgException("Cannot retrieve item as byte array, values out-of-range");
                }
                b[i] = (byte) S[i];
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
            for (int i = 0; i < L.length; i++) {
                if (L[i] > Byte.MAX_VALUE || L[i] < Byte.MIN_VALUE) {
                    throw new cMsgException("Cannot retrieve item as byte array, values out-of-range");
                }
                b[i] = (byte) L[i];
            }
            return b;
        }
        else if (type == cMsgConstants.payloadUint64A) {
            BigInteger[] big = (BigInteger[]) item;
            BigInteger max = new BigInteger("" + Byte.MAX_VALUE);
            int i=0;
            byte[] b = new byte[big.length];
            for (BigInteger bigI : big) {
                if ( bigI.compareTo(max) > 0 ) {
                    throw new cMsgException("Cannot retrieve item as byte array, values out-of-range");
                }
                b[i++] = bigI.byteValue();
            }
            return b;
        }
        throw new cMsgException("Wrong type");
    }


    public short[] getShortArray() throws cMsgException {

        if (type == cMsgConstants.payloadInt8A) {
            byte[] B = (byte[]) item;
            short[] s = new short[B.length];
            System.arraycopy(B, 0, s, 0, B.length);
            return s;
        }
        else if (type == cMsgConstants.payloadInt16A || type == cMsgConstants.payloadUint8A)  {
            return (short[]) item;
        }
        else if (type == cMsgConstants.payloadInt32A || type == cMsgConstants.payloadUint16A) {
            int[] I   = (int[])item;
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
                for (int i=0; i<L.length; i++) {
                     if (L[i] > Short.MAX_VALUE || L[i] < Short.MIN_VALUE) {
                        throw new cMsgException("Cannot retrieve item as short array, values out-of-range");
                    }
                    s[i] = (short)L[i];
                }
                return s;
        }
        else if (type == cMsgConstants.payloadUint64A) {
            BigInteger[] big = (BigInteger[])item;
            BigInteger max = new BigInteger(""+Short.MAX_VALUE);
            int i=0;
            short[] s = new short[big.length];
            for (BigInteger bigI : big) {
                if ( bigI.compareTo(max) > 0 ) {
                    throw new cMsgException("Cannot retrieve item as short array, values out-of-range");
                }
                s[i++] = bigI.shortValue();
            }
            return s;
        }
        throw new cMsgException("Wrong type");
    }


    public int[] getIntArray() throws cMsgException {

        if (type == cMsgConstants.payloadInt8A)  {
            byte[] B = (byte[])item;
            int[] i  = new int[B.length];
            System.arraycopy(B,0,i,0,B.length);
            return i;
        }
        else if (type == cMsgConstants.payloadInt16A || type == cMsgConstants.payloadUint8A)  {
            short[] S = ((short[])item);
            int[] i   = new int[S.length];
            System.arraycopy(S,0,i,0,S.length);
            return i;
        }
        else if (type == cMsgConstants.payloadInt32A || type == cMsgConstants.payloadUint16A) {
            return (int[])item;
        }
        else if (type == cMsgConstants.payloadInt64A || type == cMsgConstants.payloadUint32A) {
            long[] L = (long[]) item;
            int[] s  = new int[L.length];
            for (int i = 0; i < L.length; i++) {
                if (L[i] > Integer.MAX_VALUE || L[i] < Integer.MIN_VALUE) {
                    throw new cMsgException("Cannot retrieve item as int array, values out-of-range");
                }
                s[i] = (int)L[i];
            }
            return s;
        }
        else if (type == cMsgConstants.payloadUint64A) {
            BigInteger[] big = (BigInteger[])item;
            BigInteger max = new BigInteger(""+Integer.MAX_VALUE);
            int i=0;
            int[] s = new int[big.length];
            for (BigInteger bigI : big) {
                if ( bigI.compareTo(max) > 0 ) {
                    throw new cMsgException("Cannot retrieve item as int array, values out-of-range");
                }
                s[i++] = bigI.intValue();
            }
            return s;
        }
        throw new cMsgException("Wrong type");
    }


    public long[] getLongArray() throws cMsgException {

        if (type == cMsgConstants.payloadInt8A)  {
            byte[] B = (byte[])item;
            long[] l = new long[B.length];
            System.arraycopy(B,0,l,0,B.length);
            return l;
        }
        else if (type == cMsgConstants.payloadInt16A || type == cMsgConstants.payloadUint8A)  {
            short[] S = (short[])item;
            long[] l  = new long[S.length];
            System.arraycopy(S,0,l,0,S.length);
            return l;
        }
        else if (type == cMsgConstants.payloadInt32A || type == cMsgConstants.payloadUint16A) {
            int[] I  = (int[])item;
            long[] l = new long[I.length];
            System.arraycopy(I,0,l,0,I.length);
            return l;
        }
        else if (type == cMsgConstants.payloadInt64A || type == cMsgConstants.payloadUint32A) {
            return (long[])item;
        }
        else if (type == cMsgConstants.payloadUint64A) {
            BigInteger[] big = (BigInteger[])item;
            BigInteger max = new BigInteger(""+Long.MAX_VALUE);
            int i=0;
            long[] s = new long[big.length];
            for (BigInteger bigI : big) {
                if ( bigI.compareTo(max) > 0 ) {
                    throw new cMsgException("Cannot retrieve item as long array, values out-of-range");
                }
                s[i++] = bigI.longValue();
            }
            return s;
        }
        throw new cMsgException("Wrong type");
    }


    public BigInteger[] getBigIntArray() throws cMsgException {

        if (type == cMsgConstants.payloadInt8A)  {
            byte[] B = (byte[])item;
            BigInteger[] bi = new BigInteger[B.length];
            System.arraycopy(B,0,bi,0,B.length);
            return bi;
        }
        else if (type == cMsgConstants.payloadInt16A || type == cMsgConstants.payloadUint8A)  {
            short[] S = (short[])item;
            BigInteger[] bi = new BigInteger[S.length];
            System.arraycopy(S,0,bi,0,S.length);
            return bi;
        }
        else if (type == cMsgConstants.payloadInt32A || type == cMsgConstants.payloadUint16A) {
            int[] I = (int[])item;
            BigInteger[] bi = new BigInteger[I.length];
            System.arraycopy(I,0,bi,0,I.length);
            return bi;
        }
        else if (type == cMsgConstants.payloadInt64A || type == cMsgConstants.payloadUint32A) {
            long[] L = (long[])item;
            BigInteger[] bi = new BigInteger[L.length];
            System.arraycopy(L,0,bi,0,L.length);
            return bi;
        }
        else if (type == cMsgConstants.payloadUint64A) {
            return (BigInteger[])item;
        }
        throw new cMsgException("Wrong type");
    }


    //---------------
    // GET REALS DUDE
    //---------------

    public float getFloat() throws cMsgException {
        if (type == cMsgConstants.payloadFlt) {
            return (Float)item;
        }
        else if (type == cMsgConstants.payloadDbl) {
            Double l = (Double)item;
            if (l <= Float.MAX_VALUE && l >= Float.MIN_VALUE) {
                return l.floatValue();
            }
        }
        throw new cMsgException("Wrong type");
    }

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

    public float[] getFloatArray() throws cMsgException {
        if (type == cMsgConstants.payloadFltA)  {
            return (float[])item;
        }
        else if (type == cMsgConstants.payloadDblA) {
            double[] D = (double[]) item;
            float[] f  = new float[D.length];
            for (int i = 0; i < D.length; i++) {
                if (D[i] > Float.MAX_VALUE || D[i] < Float.MIN_VALUE) {
                    throw new cMsgException("Cannot retrieve item as float array, values out-of-range");
                }
                f[i] = (float)D[i];
            }
            return f;
        }
        throw new cMsgException("Wrong type");
    }

    public double[] getDoubleArray() throws cMsgException {
         if (type == cMsgConstants.payloadFltA)  {
             float[] F  = (float[])item;
             double[] d = new double[F.length];
             System.arraycopy(F,0,d,0,F.length);
             return d;
         }
         else if (type == cMsgConstants.payloadDblA) {
             return (double[])item;
         }
         throw new cMsgException("Wrong type");
     }

}

/*----------------------------------------------------------------------------*
*  Copyright (c) 2004        Southeastern Universities Research Association, *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    E.Wolin, 29-Jun-2004, Jefferson Lab                                     *
*                                                                            *
*    Authors: Elliott Wolin                                                  *
*             wolin@jlab.org                    Jefferson Lab, MS-6B         *
*             Phone: (757) 269-7365             12000 Jefferson Ave.         *
*             Fax:   (757) 269-6248             Newport News, VA 23606       *
*                                                                            *
*             Carl Timmer                                                    *
*             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
*             Phone: (757) 269-5130             12000 Jefferson Ave.         *
*             Fax:   (757) 269-6248             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/


package org.jlab.coda.cMsg;


import org.jlab.coda.cMsg.common.Base64;
import org.jlab.coda.cMsg.common.cMsgMessageContextDefault;
import org.jlab.coda.cMsg.common.cMsgMessageContextInterface;
import java.lang.*;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.io.*;
import java.math.BigInteger;
import java.text.SimpleDateFormat;


/**
 * This class implements a message in the cMsg messaging system.
 * Each cMsgMessage object contains many different fields. User-settable fields
 * include a time, an int, a text, and a binary array field. However, the most
 * complex field (and thus deserving a full explanation) is the compound payload.
 * In short, the payload allows a string to store messages of arbitrary
 * length and complexity. All types of ints (1,2,4,8 bytes, BigInteger), 4,8-byte floats,
 * strings, binary, whole messages and arrays of all these types can be stored
 * and retrieved from the compound payload. These methods are thread-safe.<p>
 *
 * Although XML would be a format well-suited to this task, it takes more time and memory
 * to decode XML than a simple format. Thus, a simple, easy-to-parse format
 * was developed to implement the payload. A side benefit is no external XML parsing
 * package is needed.<p>
 *
 * Following is the text format of a complete compound payload (where [nl] means
 * newline). Each payload consists of a number of items. The very first line is the
 * number of items in the payload. That is followed by the text representation of
 * each item. The first line of each item consists of 5 entries.
 *
 * Note that there is only 1 space or newline between all entries. The only exception
 * to the 1 space spacing is between the last two entries on each "header" line (the line
 * that contains the item_name). There may be several spaces between the last 2
 * entries on these lines.<p></b>
 *
 *<pre>    item_count[nl]</pre><p>
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
 * Notice that this format allows a message to store a message which stores a message
 * which stores a message, ad infinitum. In other words, recursive message storing.
 * The item_length in each case is the length in bytes of the rest of the item (not
 * including the newline at the end). Note that accessor methods can return null objects.
 *
 * @author Elliott Wolin
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgMessage implements Cloneable, Serializable {

    /**
     * Is message a sendAndGet request? -- stored in 1st bit of info.
     * This is only for internal use.
     */
    public static final int isGetRequest  = 0x1;
    /**
     * Is message a response to a sendAndGet? -- stored in 2nd bit of info.
     * This is only for internal use.
     */
    public static final int isGetResponse = 0x2;
    /**
     * Is message null response to a sendAndGet? -- stored in 3rd bit of info.
     * This is only for internal use.
     */
    public static final int isNullGetResponse = 0x4;
    /**
     * Is the byte array data in big endian form? -- stored in 4th bit of info.
     * This is only for internal use.
     */
    public static final int isBigEndian = 0x8;
    /**
     * Has the message been sent over the wire? -- is stored in 5th bit of info.
     * This is only for internal use.
     */
    public static final int wasSent = 0x10;
    /**
     * Does the message have a compound payload? -- is stored in 6th bit of info.
     * This is only for internal use.
     */
    public static final int hasPayload = 0x20;

    /**
     * If the message has a compound payload, is the payload only in text form
     * or has it been expanded into a hashtable of real objects?
     * Is stored in the 7th bit of info.
     * This is only for internal use.
     */
    public static final int expandedPayload = 0x40;

   /** When converting text to message fields, only convert system fields. */
    public static final int systemFieldsOnly = 0;

    /** When converting text to message fields, only convert non-system fields. */
    public static final int payloadFieldsOnly = 1;

    /** When converting text to message fields, convert all fields. */
    public static final int allFields = 2;

    /** Format to use when turning times into XML or when parsing time strings. */
    private static final String timeFormat = "EE MMM dd kk:mm:ss zz yyyy";

    /** Object to format and parse date strings. */
    transient protected static final SimpleDateFormat dateFormatter;
    static {
        dateFormatter = new SimpleDateFormat(timeFormat);
    }

    /**
     * Map the value of an ascii character (index) to the numerical
     * value it represents. The only characters of interest are 0-9,a-f,
     * and Z for converting hex strings back to numbers.
     */
    private static final int toByte[] =
    { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /*  0-9  */
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

    /** Absolute maximum number of entries a message keeps when recording the history of various parameters. */
    private static final int historyLengthAbsoluteMax = 200;

    /** Initial maximum number of entries a message keeps when recording the history of various parameters. */
    private static final int historyLengthInit = 20;

    // private/local quantities

    /** Was the byte array copied in or only a reference assigned? */
    boolean byteArrayCopied;

    /** Maximum number of entries a message keeps when recording the history of various parameters. */
    private int historyLengthMax;

    // general quantities

    /**
     * Unique message intVal created by cMsg system.
     * Used by cMsg domain server to track client's "sendAndGet" calls.
     */
    protected int sysMsgId;

    /** Message exists in this domain. */
    protected String domain;

    /**
     * Condensed information stored in bits.
     * Is message a sendAndGet request? -- stored in 1st bit.
     * Is message a response to a sendAndGet? -- stored in 2nd bit.
     * Is the response message null instead of a message? -- stored in 3rd bit.
     * Is the byte array data in big endian form? -- stored in 4th bit.
     * Was it sent over the wire? -- stored in 5th bit.
     * Does it have a compound payload? -- stored in 6th bit.
     * Is the payload expanded? -- stored in 7th bit.
     *
     * @see #isGetRequest
     * @see #isGetResponse
     * @see #isNullGetResponse
     * @see #isBigEndian
     * @see #wasSent
     * @see #hasPayload()
     * @see #isExpandedPayload()
     */
    protected int info;
    /** Version number of cMsg. */
    protected int version;
    /** Class member reserved for future use. */
    protected int reserved;

    // user-settable quantities

    /** Subject of message. */
    protected String subject;
    /** Type of message. */
    protected String type;
    /** Text of message. */
    protected String text;
    /** Integer supplied by user. */
    protected int userInt;
    /** Time supplied by user in milliseconds from midnight GMT, Jan 1st, 1970. */
    protected long userTime;
    /** Byte array of message. */
    protected byte[] bytes;
    /** Offset into byte array of first element. */
    protected int offset;
    /** Length of byte array elements to use. */
    protected int length;

    // payload quantities

    /** List of payload items. BUGBUG why create it here? for clone? */
    transient protected ConcurrentHashMap<String, cMsgPayloadItem> items =
                             new ConcurrentHashMap<String, cMsgPayloadItem>();
    /** Buffer to help build the text representation of the payload to send over network. */
    transient private StringBuilder buffer = new StringBuilder(512);
    /** String representation of the entire payload (including hidden system payload items). */
    protected String payloadText;
    /** If true, do NOT add current sender to message history in payload. */
    transient protected boolean noHistoryAdditions;

    // sender quantities

    /** Name of message sender (must be unique in some domains). */
    protected String sender;
    /** Host sender is running on. */
    protected String senderHost;
    /** Time message was sent in milliseconds from midnight GMT, Jan 1st, 1970. */
    protected long senderTime;
    /** Field used by domain server in implementing "sendAndGet". */
    protected int senderToken;

    // receiver quantities

    /** Name of message receiver. */
    protected String receiver;
    /** Host receiver is running on. */
    protected String receiverHost;
    /** Time message was received in milliseconds from midnight GMT, Jan 1st, 1970. */
    protected long receiverTime;

    // context information when passing msg to callback
    /**
     * Should the message be sent with a reliable method (TCP) or is
     * an unreliable method (UDP) ok?
     */
    protected boolean reliableSend = true;

    /** Object giving info about callback environment. */
    transient protected cMsgMessageContextInterface context = new cMsgMessageContextDefault();

    /**
     * Cloning this object does not pass on the context and copies the byte array if it exists.
     * @return a cMsgMessage object which is a copy of this message
     */
    public Object clone() {
        try {
            cMsgMessage result = (cMsgMessage) super.clone();
            result.context = new cMsgMessageContextDefault();
//            for (Map.Entry<String, cMsgPayloadItem> entry : items.entrySet()) {
//                result.items.put(entry.getKey(), (cMsgPayloadItem)entry.getValue().clone());
//            }
            if (bytes != null) {
                // Making a clone means this object must be independent
                // of the object being cloned. Thus we MUST copy the byte array.
                result.bytes = bytes.clone();
                byteArrayCopied = true;
            }
            return result;
        }
        catch (CloneNotSupportedException e) {
            return null; // never invoked
        }
    }


    /** The constructor for a blank message. */
    public cMsgMessage() {
        version = cMsgConstants.version;
        //items   = new ConcurrentHashMap<String, cMsgPayloadItem>();
        //buffer  = new StringBuilder(512);
        info   |= expandedPayload | isBigEndian;
        historyLengthMax = historyLengthInit;
    }


    /**
     * The constructor which copies a given message.
     *
     * @param msg message to be copied
     */
    public cMsgMessage(cMsgMessage msg) {
        copy(msg, this);
    }


    /**
     * This method turns the destination message into a complete copy of the source message.
     * If the source msg has a byte array it is copied into the dest msg
     * only if it was originally copied into the source msg, else only the reference
     * is copied.
     * 
     * @param src message to be copied
     * @param dst message to be copied to
     */
    public static void copy(cMsgMessage src, cMsgMessage dst) {

        // general
        dst.sysMsgId            = src.sysMsgId;
        dst.domain              = src.domain;
        dst.info                = src.info;
        dst.version             = src.version;
        dst.reserved            = src.reserved;
        dst.historyLengthMax    = src.historyLengthMax;

        // user-settable
        dst.subject             = src.subject;
        dst.type                = src.type;
        dst.text                = src.text;
        dst.userInt             = src.userInt;
        dst.userTime            = src.userTime;
        dst.byteArrayCopied     = src.byteArrayCopied;
        if (src.bytes != null) {
            if (src.byteArrayCopied) {
                dst.bytes       = src.bytes.clone();
            }
            else {
                dst.bytes       = src.bytes;
            }
        }
        dst.offset              = src.offset;
        dst.length              = src.length;

        // payload
        dst.payloadText         = src.payloadText;
        //dst.buffer              = new StringBuilder(512);
        //dst.items               = new ConcurrentHashMap<String, cMsgPayloadItem>();
        for (Map.Entry<String, cMsgPayloadItem> entry : src.items.entrySet()) {
            dst.items.put(entry.getKey(), (cMsgPayloadItem)entry.getValue().clone());
        }

        // sender
        dst.sender              = src.sender;
        dst.senderHost          = src.senderHost;
        dst.senderTime          = src.senderTime;
        dst.senderToken         = src.senderToken;

        // receiver
        dst.receiver            = src.receiver;
        dst.receiverHost        = src.receiverHost;
        dst.receiverTime        = src.receiverTime;

        // context
        dst.context             = src.context;
    }


    /**
     * Creates a complete copy of this message.
     *
     * @return copy of this message.
     */
    public cMsgMessage copy() { return (cMsgMessage) this.clone(); }


    /**
     * Creates a proper response message to this message which was sent by a client calling
     * sendAndGet.
     *
     * @return message with the response fields properly set.
     * @throws cMsgException if this message was not sent from a "sendAndGet" method call
     */
    public cMsgMessage response() throws cMsgException {
        // If this message was not sent from a "sendAndGet" method call,
        // a proper response is not possible, since the sysMsgId
        // and senderToken fields will not have been properly set.
        if (!isGetRequest()) {
            throw new cMsgException("this message not sent by client calling sendAndGet");
        }
        cMsgMessage msg = new cMsgMessage();
        msg.subject = "dummy";
        msg.type = "dummy";
        msg.sysMsgId = sysMsgId;
        msg.senderToken = senderToken;
        msg.info = isGetResponse;
        return msg;
    }


    /**
     * Creates a proper response message to this message which was sent by a client calling
     * sendAndGet. In this case, the response message is marked as a null response.
     *
     * @return message with the response fields properly set so original sender gets a null
     *         response
     * @throws cMsgException if this message was not sent from a "sendAndGet" method call
     */
    public cMsgMessage nullResponse() throws cMsgException {
        // If this message was not sent from a "get" method call,
        // a proper response is not possible, since the sysMsgId
        // and senderToken fields will not have been properly set.
        if (!isGetRequest()) {
            throw new cMsgException("this message not sent by client calling sendAndGet");
        }
        cMsgMessage msg = new cMsgMessage();
        msg.sysMsgId = sysMsgId;
        msg.senderToken = senderToken;
        msg.info = isGetResponse | isNullGetResponse;
        return msg;
    }


    /**
     * Converts existing message to response of supplied message.
     *
     * @param msg message this message will be made a response to
     */
    public void makeResponse(cMsgMessage msg) {
        this.sysMsgId    = msg.getSysMsgId();
        this.senderToken = msg.getSenderToken();
        this.info = isGetResponse;
    }


    /**
     * Converts existing message to null response of supplied message.
     *
     * @param msg message this message will be made a null response to
     */
    public void makeNullResponse(cMsgMessage msg) {
        this.sysMsgId    = msg.getSysMsgId();
        this.senderToken = msg.getSenderToken();
        this.info = isGetResponse | isNullGetResponse;
    }


    ///////////////////////
    // general quantities
    ///////////////////////

    /**
     * Does the message record sender history (assuming history length max is not exceeded)?
     * @return true if message does NOT record sender history
     */
    public boolean noHistoryAdditions() {
        return noHistoryAdditions;
    }

    /**
     * Gets the maximum number of entries this message keeps of its history of various parameters.
     * @return the maximum number of entries this message keeps of its history of various parameters
     */
    public int getHistoryLengthMax() {
        return historyLengthMax;
    }

    /**
     * Sets the maximum number of entries this message keeps of its history of various parameters.
     * Setting this quantity to zero effectively turns off keeping any history.
     *
     * @param historyLengthMax the maximum number of entries this message keeps of its history of various parameters
     * @throws cMsgException if historyLengthMax is < 0 or > historyLengthAbsoluteMax
     */
    public void setHistoryLengthMax(int historyLengthMax) throws cMsgException {
        if (historyLengthMax < 0) {
            throw new cMsgException("historyLengthMax must >= 0");
        }
        else if (historyLengthMax > historyLengthAbsoluteMax) {
            throw new cMsgException("historyLengthMax must <= " + historyLengthAbsoluteMax);
        }
        this.historyLengthMax = historyLengthMax;
    }

    /**
     * Get system intVal of message. Irrelevant to the user, used only by the system.
     * @return system intVal of message.
     */
    public int getSysMsgId() {return sysMsgId;}


    /**
     * Get domain this message exists in.
     * @return domain message exists in.
     */
    public String getDomain() {return domain;}


    /**
     * Is this message a response to a "sendAndGet" message?
     * @return true if this message is a response to a "sendAndGet" message.
     */
    public boolean isGetResponse() {return ((info & isGetResponse) == isGetResponse);}
    /**
     * Specify whether this message is a response to a "sendAndGet" message.
     * @param getResponse true if this message is a response to a "sendAndGet" message
     */
    public void setGetResponse(boolean getResponse) {
        info = getResponse ? info|isGetResponse : info & ~isGetResponse;
    }


    /**
     * Is this message a null response to a "sendAndGet" message?
     * @return true if this message is a null response to a "sendAndGet" message
     */
    public boolean isNullGetResponse() {return ((info & isNullGetResponse) == isNullGetResponse);}
    /**
     * Specify whether this message is a null response to a "sendAndGet" message.
     * @param nullGetResponse true if this message is a null response to a "sendAndGet" message
     */
    public void setNullGetResponse(boolean nullGetResponse) {
        info = nullGetResponse ? info|isNullGetResponse : info & ~isNullGetResponse;
    }


    /**
     * Is this message a "sendAndGet" request?
     * @return true if this message is a "sendAndGet" request
     */
    public boolean isGetRequest() {return ((info & isGetRequest) == isGetRequest);}


    /**
     * Gets information compacted into a bit pattern. This method is not useful
     * to the general user and is for use only by system developers.
     *
     * @return integer containing bit pattern of information
     */
    public int getInfo() {
        return info;
    }


    /**
     * Gets the version number of this message which is the same
     * as that of the cMsg software package that created it.
     * @return version number of message.
     */
    public int getVersion() {
        return version;
    }

    /////////////////////////////
    // user-settable quantities
    /////////////////////////////


    /**
     * Get subject of message.
     * @return subject of message.
     */
    public String getSubject() {return subject;}
    /**
     * Set subject of message.
     * @param subject subject of message.
     */
    public void setSubject(String subject) {this.subject = subject;}


    /**
     * Get type of message.
     * @return type of message.
     */
    public String getType() {return type;}
    /**
     * Set type of message.
     * @param type type of message.
     */
    public void setType(String type) {this.type = type;}


    /**
     * Get text of message.
     * @return text of message.
     */
    public String getText() {return text;}
    /**
     * Set text of message.
     * @param text text of message.
     */
    public void setText(String text) {this.text = text;}


    /**
     * Get user supplied integer.
     * @return user supplied integer.
     */
    public int getUserInt() {return userInt;}
    /**
     * Set message sender's intVal.
     * @param userInt message sender's intVal.
     */
    public void setUserInt(int userInt) {this.userInt = userInt;}


    /**
     * Get user supplied time.
     * @return user supplied time.
     */
    public Date getUserTime() {return new Date(userTime);}
    /**
     * Set time.
     * @param time time ad Date object.
     */
    public void setUserTime(Date time) {this.userTime = time.getTime();}
    /**
     * Set time.
     * @param time time as long (amount of millisec since Jan 1 1970, 12am, GMT)
     */
    public void setUserTime(long time) {
        if (time < 0) return;
        this.userTime = time;
    }

    // byte array stuff

    /**
     * Get byte array of message.
     * @return byte array of message.
     */
    public byte[] getByteArray() {return bytes;}

    /**
     * Copy byte array into message. Offset is set to 0. Length is set to
     * the full length of the copied array.
     * If the byte array is null, then internal byte array is set to null,
     * and both the offset & length are set to 0.
     *
     * @param b byte array of message.
     */
    public void setByteArray(byte[] b) {
        if (b == null) {
            bytes  = null;
            offset = 0;
            length = 0;
            return;
        }
        bytes  = b.clone();
        offset = 0;
        length = b.length;
        byteArrayCopied = true;
    }

    /**
     * Copy byte array into message by copying "length" number of elements
     * starting at "offset". In this message, the offset will be set to zero,
     * and the length will be set to "length".
     * If the byte array is null, then internal byte array is set to null,
     * and both the offset & length are set to 0.
     *
     * @param b byte array of message.
     * @param offset index into byte array to bytes to be copied.
     * @param length number of bytes to be copied.
     * @throws cMsgException if length, offset, or offset+length is out of array bounds
     */
    public void setByteArray(byte[] b, int offset, int length) throws cMsgException {
        if (b == null) {
            bytes = null;
            this.offset = 0;
            this.length = 0;
            return;
        }

        if ((length < 0) || (length > b.length)) {
            throw new cMsgException("length is out of array bounds");
        }
        if ((offset < 0) || (offset > b.length-1)) {
            throw new cMsgException("offset is out of array bounds");
        }
        if ((offset + length) > b.length) {
            throw new cMsgException("offset + length is out of array bounds");
        }

        if (offset == 0 && length == b.length) {
            setByteArray(b);
            return;
        }

        this.offset = 0;
        this.length = length;
        bytes = new byte[length];
        byteArrayCopied = true;

        System.arraycopy(b, offset, bytes, 0, length);
    }

    /**
     * Set byte array to the given argument without copying the byte array
     * itself - only the reference is copied.
     * In this message the offset will be set to zero, and the length will be
     * set to length of the array.
     * If the byte array is null, then internal byte array is set to null,
     * and both the offset & length are set to 0.
     *
     * @param b byte array of message.
     */
    public void setByteArrayNoCopy(byte[] b) {
        if (b == null) {
            bytes  = null;
            offset = 0;
            length = 0;
            return;
        }
        bytes  = b;
        offset = 0;
        length = b.length;
        byteArrayCopied = false;
    }

    /**
     * Set byte array to the given argument without copying the byte array
     * itself - only the reference is copied.
     * In this message the offset will be set to "offset", and the length will be
     * set to "length".
     * If the byte array is null, then internal byte array is set to null,
     * and both the offset & length are set to 0.
     *
     * @param b byte array of message.
     * @param offset index into byte array to bytes of interest.
     * @param length number of bytes of interest.
     * @throws cMsgException if length, offset, or offset+length is out of array bounds
     */
    public void setByteArrayNoCopy(byte[] b, int offset, int length) throws cMsgException {
        if (b == null) {
            bytes = null;
            this.offset = 0;
            this.length = 0;
            return;
        }

        if ((length < 0) || (length > b.length)) {
            throw new cMsgException("length is out of array bounds");
        }
        if ((offset < 0) || (offset > b.length-1)) {
            throw new cMsgException("offset is out of array bounds");
        }
        if ((offset + length) > b.length) {
            throw new cMsgException("offset + length is out of array bounds");
        }

        bytes = b;
        this.offset = offset;
        this.length = length;
        byteArrayCopied = false;
    }


    /**
     * Get byte array length of region of interest. This may be smaller
     * than the total or full length of the array.
     * @return length of byte array's data of interest.
     */
    public int getByteArrayLength() {return length;}
    /**
     * Get full length of the byte array or 0 if it is null.
     * @return length of byte array's data of interest.
     */
    public int getByteArrayLengthFull() {
        if (bytes == null) return 0;
        return bytes.length;
    }
    /**
     * Set byte array length of data of interest. This may be smaller
     * than the total or full length of the array if the user is only interested
     * in a portion of the array. If the byte array is null, all non-negative
     * values are accepted.
     * @param length of byte array's data of interest.
     * @throws cMsgException if length or offset+length is out of array bounds
     */
    public void setByteArrayLength(int length) throws cMsgException {
        if (length < 0) {
            throw new cMsgException("length is out of array bounds");
        }
        if (bytes == null) {
            this.length = length;
            return;
        }
        if (length > bytes.length) {
            throw new cMsgException("length is out of array bounds");
        }
        if ((offset + length) > bytes.length) {
            throw new cMsgException("offset + length is out of array bounds");
        }
        this.length = length;
    }
    /**
     * Reset the byte array length to the full length of the array or zero if
     * there is none.
     */
    public void resetByteArrayLength() {
        if (bytes == null) {
            this.length = 0;
            return;
        }
        this.length = bytes.length;
    }


    /**
     * Get byte array index to region of interest. This may be non-zero
     * if the user is only interested in a portion of the array.
     * @return index to byte array's data of interest.
     */
    public int getByteArrayOffset() {return offset;}
    /**
     * Set byte array index to region of interest. This may be non-zero
     * if the user is only interested in a portion of the array.
     * If the byte array is null, all non-negative values are accepted.
     * @param offset index to byte array's data of interest.
     * @throws cMsgException if offset or offset+length is out of array bounds
     */
    public void setByteArrayOffset(int offset) throws cMsgException {
        if (offset < 0) {
            throw new cMsgException("offset is out of array bounds");
        }
        if (bytes == null) {
            this.offset = offset;
            return;
        }
        if (offset > bytes.length-1) {
            throw new cMsgException("offset is out of array bounds");
        }
        if ((offset + length) > bytes.length) {
            throw new cMsgException("offset + length is out of array bounds");
        }
        this.offset = offset;
    }


    /**
     * Get endianness of the byte array data.
     * @return endianness of byte array's data (either {@link cMsgConstants#endianBig}
     *         or {@link cMsgConstants#endianLittle})
     */
    public int getByteArrayEndian() {
        if ((this.info & isBigEndian) > 1) {
            return cMsgConstants.endianBig;
        }
        return cMsgConstants.endianLittle;
    }
    /**
     * Set endianness of the byte array data. Accepted values are:
     * <ul>
     * <li>{@link cMsgConstants#endianBig} for a big endian<p>
     * <li>{@link cMsgConstants#endianLittle} for a little endian<p>
     * <li>{@link cMsgConstants#endianLocal} for same endian as local host<p>
     * <li>{@link cMsgConstants#endianNotLocal} for opposite endian as local host<p>
     * <li>{@link cMsgConstants#endianSwitch} for opposite current endian<p>
     * </ul>
     *
     * @param endian endianness of the byte array data
     * @throws cMsgException if argument invalid value
     */
    public void setByteArrayEndian(int endian) throws cMsgException {
        if ((endian != cMsgConstants.endianBig)      &&
            (endian != cMsgConstants.endianLittle)   &&
            (endian != cMsgConstants.endianLocal)    &&
            (endian != cMsgConstants.endianNotLocal) &&
            (endian != cMsgConstants.endianSwitch)) {
            throw new cMsgException("improper endian value");
        }
        if (endian == cMsgConstants.endianBig || endian == cMsgConstants.endianLocal) {
            this.info |= isBigEndian;
        }
        else if (endian == cMsgConstants.endianSwitch) {
            if ((this.info & isBigEndian) > 1) {
                 this.info &= ~isBigEndian;
            }
            else {
                this.info |= isBigEndian;
            }
        }
        else {
            this.info &= ~isBigEndian;
        }
    }
    /**
     * This method specifies whether the endian value of the byte array is little
     * endian or not. Since the Java JVM is big endian on all platforms, if the array
     * is big endian it returns false indicating that there is no need to swap data.
     * If the array is little endian then it returns true.
     *
     * @return true if byte array is little endian, else false
     */
    public boolean needToSwap() {
        return (this.info & isBigEndian) <= 1;
    }


    /////////////
    // sender
    /////////////


    /**
     * Get message sender.
     * @return message sender.
     */
    public String getSender() {return sender;}


    /**
     * Get message sender's host computer.
     * @return message sender's host computer.
     */
    public String getSenderHost() {return senderHost;}


    /**
     * Get time message was sent.
     * @return time message sent.
     */
    public Date getSenderTime() {return new Date(senderTime);}


    /**
     * Get sender's token. Used to track asynchronous responses to
     * messages requesting responses from other clients. Irrelevant to the user,
     * used only by the system.
     *
     * @return sender's token
     */
    public int getSenderToken() {return senderToken;}


    /////////////
    // receiver
    /////////////


    /**
     * Get message receiver.
     * @return message receiver.
     */
    public String getReceiver() {return receiver;}


    /**
     * Get message receiver's host computer.
     * @return message receiver's host computer.
     */
    public String getReceiverHost() {return receiverHost;}


    /**
     * Get time message was received.
     * @return time message received.
     */
    public Date getReceiverTime() {return new Date(receiverTime);}


    /////////////
    // context
    /////////////

    /**
     * Sets whether the send will be reliable (default, TCP)
     * or will be allowed to be unreliable (UDP).
     *
     * @param b false if using UDP, or true if using TCP
     */
    public void setReliableSend(boolean b) {
        reliableSend = b;
    }


    /**
     * Gets whether the send will be reliable (default, TCP)
     * or will be allowed to be unreliable (UDP).
     *
     * @return value true if using TCP, else false
     */
    public boolean getReliableSend() {
        return reliableSend;
    }

    /**
     * Gets the object containing information about the context of the
     * callback receiving this message.
     *
     * @return object containing information about the context of the
     *         callback receiving this message
     */
    public cMsgMessageContextInterface getContext() {
        return context;
    }


    /////////////
    // printing
    /////////////

    /**
     * This method escapes the chars <, >, " for putting strings into XML.
     * @param s string to be escaped
     * @return escaped string
     */
    static final String escapeAllForXML(String s) {
        StringBuilder sb = new StringBuilder(s);
        for (int i=0; i<sb.length(); i++) {
            if (sb.charAt(i) == '<') {
                sb.replace(i,i+1,"&lt;");
                i += 3;
            }
            else if (sb.charAt(i) == '>') {
                sb.replace(i,i+1,"&gt;");
                i += 3;
            }
            else if (sb.charAt(i) == '"') {
                sb.replace(i,i+1,"&#34;");
                i += 4;
            }
        }
        return sb.toString();
    }

    /**
     * This method escapes the " char for putting strings into XML.
     * @param s string to be escaped
     * @return escaped string
     */
    static final String escapeQuotesForXML(String s) {
        StringBuilder sb = new StringBuilder(s);
        for (int i=0; i<sb.length(); i++) {
            if (sb.charAt(i) == '"') {
                sb.replace(i,i+1,"&#34;");
                i += 4;
            }
        }
        return sb.toString();
    }

    /*
             A]]>B<![CDATA[C]]>D
    <![CDATA[A]]>B<![CDATA[C]]>D]]>
    <![CDATA[A]]><![CDATA[]]]]><![CDATA[>B<![CDATA[C]]><![CDATA[]]]]><![CDATA[>D]]>
             A]]><![CDATA[]]            >B<![CDATA[C            ]]            >D
             A]]><![CDATA[]]>B<![CDATA[C]]>D

             A<![CDATA[B]]>C
    <![CDATA[A<![CDATA[B]]>C]]>
    <![CDATA[A<![CDATA[B]]><![CDATA[]]]]><![CDATA[>C]]>
             A<![CDATA[B             ]]           >C
             A<![CDATA[B]]>C

             A<![CDATA[B]]><![CDATA[C]]>D
    <![CDATA[A<![CDATA[B]]><![CDATA[C]]>D]]>
    <![CDATA[A<![CDATA[B]]><![CDATA[]]]]><![CDATA[><![CDATA[C]]><![CDATA[]]]]><![CDATA[>D]]>
             A<![CDATA[B             ]]           ><![CDATA[C            ]]            >D
             A<![CDATA[B]]><![CDATA[C]]>D
     */

    /**
     * This method escapes CDATA constructs which will appear inside of XML CDATA sections.
     * It is not possible to have nested cdata sections. Actually the CDATA ending
     * sequence, ]]> , is what cannot be containing in a surrounding CDATA section.
     * This restriction can be
     * cleverly circumvented by inserting "<![CDATA[]]]]><![CDATA[>" after each CDATA ending
     * sequence. This creates independent CDATA sections which are not nested.</p>
     *
     * This method assumes that there are no nested cdata sections in the input string.
     * Nothing is done to the string if:
     * <UL>
     * <LI>there is no ]]> so no escaping is necessary
     * <LI>there is no <![CDATA[ so s contains malformed XML
     * <LI>a particular <![CDATA[ has no corresponding ]]> so s contains malformed XML
     * <LI>]]> comes before <![CDATA[ so s contains malformed XML
     * </UL>
     *
     * @param s string to be escaped
     * @return escaped string
     */
    static final String escapeCdataForXML(String s) {
        // don't need to escape anything
        int index1 = s.indexOf("]]>");
        if (index1 < 0) return s;
        int index;

        // ignore since malformed XML in s
        int index2 = s.indexOf("<![CDATA[");
        if (index2 < 0 || index1 < index2) return s;

        StringBuilder sb = new StringBuilder(s);
        String sub = "<![CDATA[]]]]><![CDATA[>";
        index1 = index2;

        while (index1 < sb.length()) {
            index = sb.indexOf("<![CDATA[", index1);
            if (index < 0) {
                index2 = sb.indexOf("]]>", index1);
                if (index2 > -1) return s;
                break;
            }

            index2 = sb.indexOf("]]>", index+9);
            if (index2 < 0) return s;
            sb.insert(index2+3,sub);

            index1 = index2+3+sub.length();
        }

        return sb.toString();
    }


    /**
     * This method converts the message to a printable string in XML format.
     * Any binary data is encoded in the base64 format.
     *
     * @return message as XML String object
     * @return a blank string if any error occurs
     */
    public String toString() {
        return toStringImpl(0, true, false, false, false, false, null);
    }


    /**
     * This method converts the message to a printable string in XML format.
     * Any binary data is encoded in the base64 format.
     *
     * @param binary includes binary as ASCII if true, else binary is ignored
     * @param compact if true, do not include attributes with null or default integer values
     * @param noSystemFields if true, do not include system (metadata) payload fields
     *
     * @return message as XML String object
     * @return a blank string if any error occurs
     */
    public String toString(boolean binary, boolean compact, boolean noSystemFields) {
        return toStringImpl(0, binary, compact, false, false, noSystemFields, null);
    }


    /**
     * This method converts the message to a printable string in XML format.
     *
     * @param margin number of spaces in the indent
     * @param binary includes binary as ASCII if true, else binary is ignored
     * @param compact if true, do not include attributes with null or default integer values
     * @param compactPayload if true includes payload only as a single string (internal format)
     * @param hasName if true, this message is in the payload and has a name
     * @param noSystemFields if true, do not include system (metadata) payload fields
     * @param pItemName if in payload, name of payload item
     *
     * @return message as XML String object
     * @return a blank string if any error occurs
     */
    private String toStringImpl(int margin,
                                boolean binary, boolean compact,
                                boolean compactPayload, boolean hasName,
                                boolean noSystemFields, String pItemName) {
        
        StringBuilder sb = new StringBuilder(2048);

        // Create the indent since a message may contain a message, etc.
        String indent = "";
        if (margin < 1) {
            margin = 0;
            indent = "";
        }
        else {
            char[] c = new char[margin];
            Arrays.fill(c, ' ');
            indent = new String(c);
        }

        // attributes offset margin+5 spaces
        char[] c = new char[margin+5];
        Arrays.fill(c, ' ');
        String offsett = new String(c);

        // Main XML element
        if (hasName) {
            sb.append(indent); sb.append("<cMsgMessage name=\"");
            sb.append(pItemName); sb.append("\"\n");
        }
        else {
            sb.append(indent); sb.append("<cMsgMessage\n");
        }

        //--------------------------------------------
        // print all attributes in a pretty form first
        //--------------------------------------------

        sb.append(offsett);
        sb.append("version           = \""); sb.append(version); sb.append("\"\n");
        sb.append(offsett);
        sb.append("userInt           = \""); sb.append(userInt); sb.append("\"\n");

        // check if getRequest, if so, it cannot also be a getResponse
        if ( (info & isGetRequest) != 0 ) {
            sb.append(offsett);
            sb.append("getRequest        = \"true\"\n");
        }
        // check if nullGetResponse, if so, then it's a getResponse too (no need to print)
        else if ( (info & isNullGetResponse) != 0 ) {
            sb.append(offsett);
            sb.append("nullGetResponse   = \"true\"\n");
        }
        else {
            sb.append(offsett);
            sb.append("getResponse       = \""); sb.append(((info & isGetResponse) != 0) ? "true" : "false"); sb.append("\"\n");
        }

        if (domain != null) {
            sb.append(offsett);
            sb.append("domain            = \""); sb.append(domain); sb.append("\"\n");
        }
        else if (!compact) {
            sb.append(offsett);
            sb.append("domain            = \"(null)\"\n");
        }

        if (sender != null) {
            sb.append(offsett);
            sb.append("sender            = \""); sb.append(escapeQuotesForXML(sender)); sb.append("\"\n");
        }
        else if (!compact) {
            sb.append(offsett);
            sb.append("sender            = \"(null)\"\n");
        }

        if (senderHost != null) {
            sb.append(offsett);
            sb.append("senderHost        = \""); sb.append(escapeQuotesForXML(senderHost)); sb.append("\"\n");
        }
        else if (!compact) {
            sb.append(offsett);
            sb.append("senderHost        = \"(null)\"\n");
        }

        if (!compact || senderTime > 0L) {
            sb.append(offsett);
            sb.append("senderTime        = \"");
            sb.append(dateFormatter.format(new Date(senderTime))); sb.append("\"\n");
        }

        if (receiver != null) {
            sb.append(offsett);
            sb.append("receiver          = \""); sb.append(escapeQuotesForXML(receiver)); sb.append("\"\n");
        }
        else if (!compact) {
            sb.append(offsett);
            sb.append("receiver          = \"(null)\"\n");
        }

        if (receiverHost != null) {
            sb.append(offsett);
            sb.append("receiverHost      = \""); sb.append(escapeQuotesForXML(receiverHost)); sb.append("\"\n");
        }
        else if (!compact) {
            sb.append(offsett);
            sb.append("receiverHost      = \"(null)\"\n");
        }

        if (!compact || receiverTime > 0L) {
            sb.append(offsett);
            sb.append("receiverTime      = \"");
            sb.append(dateFormatter.format(new Date(receiverTime))); sb.append("\"\n");
        }

        if (!compact || userTime > 0L) {
            sb.append(offsett);
            sb.append("userTime          = \"");
            sb.append(dateFormatter.format(new Date(userTime))); sb.append("\"\n");
        }

        if (subject != null) {
            sb.append(offsett);
            sb.append("subject           = \""); sb.append(escapeQuotesForXML(subject)); sb.append("\"\n");
        }
        else if (!compact) {
            sb.append(offsett);
            sb.append("subject           = \"(null)\"\n");
        }

        if (type != null) {
            sb.append(offsett);
            sb.append("type              = \""); sb.append(escapeQuotesForXML(type)); sb.append("\"\n");
        }
        else if (!compact) {
            sb.append(offsett);
            sb.append("type              = \"(null)\"\n");
        }

        if (hasPayload() && isExpandedPayload()) {
            sb.append(offsett);
            sb.append("payloadItemCount  = \""); sb.append(items.size()); sb.append("\"\n");
        }

        sb.deleteCharAt(sb.lastIndexOf("\n"));
        sb.append(">\n");

        //------------------------------------
        // print all elements in a pretty form
        //------------------------------------

        // Text
        if (text != null) {
            sb.append(offsett);
            sb.append("<text><![CDATA["); sb.append(escapeCdataForXML(text)); sb.append("]]></text>\n");
        }

        // Binary array
        if (binary && (bytes != null) && (length > 0)) {
            String endianTxt;
            int endian = getByteArrayEndian();
            if (endian == cMsgConstants.endianBig) endianTxt = "big";
            else endianTxt = "little";

            sb.append(offsett);
            sb.append("<binary endian=\""); sb.append(endianTxt);
            sb.append("\" nbytes=\""); sb.append(length);

            // put in line breaks after 76 chars (57bytes)
            if (length > 57) {
                sb.append("\">\n");
                sb.append(Base64.encodeToString(bytes, offset, length, true));
                sb.append(offsett);
                sb.append("</binary>\n");
            }
            else {
                sb.append("\">");
                sb.append(Base64.encodeToString(bytes, offset, length, false));
                sb.append("</binary>\n");
            }
        }

        // Payload
        if (hasPayload()) {
            if (compactPayload) {
                sb.append(offsett);
                sb.append("<payload compact=\"true\">\n"); sb.append(payloadText);
                sb.append(offsett);
                sb.append("</payload>\n");
            }
            else {
                try {
                    sb.append(payloadToStringImpl(margin+5, binary, compact, noSystemFields));
                }
                catch (cMsgException e) {
                    // payload is not expanded
                    sb.append(offsett);
                    sb.append("<payload expanded=\"false\">\n"); sb.append(payloadText);
                    sb.append(offsett);
                    sb.append("</payload>\n");
                }
            }
        }

        sb.append(indent); sb.append("</cMsgMessage>\n");
        
        return sb.toString();
    }


    /**
     * This method converts the message payload to a printable string in XML format.
     * Any binary data is encoded in the base64 format.
     * Returns null if there is no payload and an exception if the payload is not expanded.
     *
     * @return message payloadas XML String object
     * @throws cMsgException if payload is not expanded
     */
    public String payloadToString() throws cMsgException {
        return payloadToStringImpl(0, true, false, false).toString();
    }


    /**
     * This method converts the payload of a message to a printable string in XML format.
     * Returns null if there is no payload and an exception if the payload is not expanded.
     *
     * @param margin number of spaces in the indent
     * @param binary includes binary as ASCII if true, else binary is ignored
     * @param compact if true, do not include attributes with null or default integer values
     * @param noSystemFields if true, do not include system (metadata) payload fields
     *
     * @return payload as XML in StringBuilder object
     * @throws cMsgException if payload is not expanded
     */
    private StringBuilder payloadToStringImpl(int margin, boolean binary, boolean compact,
                                              boolean noSystemFields) throws cMsgException {

        // no payload so return
        if (!hasPayload()) {
            return null;
        }

        if (!isExpandedPayload()) {
            throw new cMsgException("payload must be expanded before converting to XML");
        }

        StringWriter  sw = new StringWriter(2048);
        PrintWriter   wr = new PrintWriter(sw);

        StringBuilder sb = new StringBuilder(2048);

        // Create the indent since a message may contain a message, etc.
        String indent;
        if (margin < 1) {
            margin = 0;
            indent = "";
        }
        else {
            char[] c = new char[margin];
            Arrays.fill(c, ' ');
            indent = new String(c);
        }

        // sub elements offset 5 spaces from element &
        // values of sub elements offset 5 spaces from sub element
        char[] c = new char[5];
        Arrays.fill(c, ' ');
        String offsett = new String(c);

        sb.append(indent); sb.append("<payload compact=\"false\">\n");

        try {
            // get all name & type info
            int typ;
            String name;
            cMsgPayloadItem item;

            for (Map.Entry<String, cMsgPayloadItem> entry : items.entrySet()) {
                name = entry.getKey();
                item = entry.getValue();
                typ  = item.getType();

                // filter out system fields (names starting with "cmsg")
                if (noSystemFields && (name.length() > 4) && name.substring(0,4).equalsIgnoreCase("cmsg")) {
                    continue;
                }

                switch (typ) {
                    case cMsgConstants.payloadInt8:
                      {   byte i = item.getByte();
                          sb.append(indent); sb.append(offsett);
                          sb.append("<int8 name=\""); sb.append(name); sb.append("\"> ");
                          sb.append(i); sb.append(" </int8>\n");
                      } break;

                    case cMsgConstants.payloadInt16:
                      {   short i = item.getShort();
                          sb.append(indent); sb.append(offsett);
                          sb.append("<int16 name=\""); sb.append(name); sb.append("\"> ");
                          sb.append(i); sb.append(" </int16>\n");
                      } break;

                    case cMsgConstants.payloadInt32:
                      {   int i = item.getInt();
                          sb.append(indent); sb.append(offsett);
                          sb.append("<int32 name=\""); sb.append(name); sb.append("\"> ");
                          sb.append(i); sb.append(" </int32>\n");
                      } break;

                    case cMsgConstants.payloadInt64:
                      {   long i = item.getLong();
                          sb.append(indent); sb.append(offsett);
                          sb.append("<int64 name=\""); sb.append(name); sb.append("\"> ");
                          sb.append(i); sb.append(" </int64>\n");
                      } break;

                    case cMsgConstants.payloadUint64:
                      {   BigInteger i = item.getBigInt();
                          sb.append(indent); sb.append(offsett);
                          sb.append("<uint64 name=\""); sb.append(name); sb.append("\"> ");
                          sb.append(i); sb.append(" </uint64>\n");
                      } break;

                    case cMsgConstants.payloadDbl:
                      {   double d = item.getDouble();
                          sb.append(indent); sb.append(offsett);
                          sb.append("<double name=\""); sb.append(name); sb.append("\"> ");
                          sb.append(d);sb.append(" </double>\n");
                      } break;

                    case cMsgConstants.payloadFlt:
                      {   float f = item.getFloat();
                          sb.append(indent); sb.append(offsett);
                          sb.append("<float name=\""); sb.append(name); sb.append("\"> ");
                          sb.append(f); sb.append(" </float>\n");
                      } break;

                   case cMsgConstants.payloadStr:
                     {   String s = item.getString();
                         sb.append(indent); sb.append(offsett);
                         sb.append("<string name=\""); sb.append(name); sb.append("\">");
                         sb.append("<![CDATA["); sb.append(escapeCdataForXML(s)); sb.append("]]></string>\n");
                     } break;

                   case cMsgConstants.payloadBin:
                     {   byte[] b   = item.getBinary();
                         int endian = item.getEndian();

                         sb.append(indent); sb.append(offsett);
                         sb.append("<binary name=\""); sb.append(name);
                         if (endian == cMsgConstants.endianBig) {
                            sb.append("\" endian=\"big\"");
                         }
                         else {
                            sb.append("\" endian=\"little\"");
                         }
                         sb.append(" nbytes=\""); sb.append(b.length);
                         if (!binary) {
                            sb.append("\" />\n");
                         }
                         else {
                            // put in line breaks after 76 chars (57bytes)
                            if (b.length > 57) {
                                sb.append("\">\n");
                                sb.append(Base64.encodeToString(b, true));
                                sb.append(indent); sb.append(offsett);
                                sb.append("</binary>\n");
                            }
                            else {
                                sb.append("\">");
                                sb.append(Base64.encodeToString(b, false));
                                sb.append("</binary>\n");
                            }
                         }
                     } break;

                   case cMsgConstants.payloadMsg:
                     {   cMsgMessage m = item.getMessage();
                         // recursion
                         sb.append(m.toStringImpl(margin+offsett.length(), binary, compact,
                                                  false, true, noSystemFields, item.name));
                     } break;

                   //-------
                   // arrays
                   //-------

                   case cMsgConstants.payloadMsgA:
                     {   cMsgMessage[] msgs = item.getMessageArray();
                         sb.append(indent); sb.append(offsett);
                         sb.append("<cMsgMessage_array name=\""); sb.append(name); sb.append("\" count=\"");
                         sb.append(msgs.length); sb.append("\">\n");
                         for (cMsgMessage m : msgs) {
                             // recursion, individual msg in an array has no name of its own
                             sb.append(m.toStringImpl(margin+5+offsett.length(), binary, compact,
                                                      false, false, noSystemFields, null));
                         }
                         sb.append(indent); sb.append(offsett);
                         sb.append("</cMsgMessage_array>\n");
                     } break;

                   case cMsgConstants.payloadInt8A:
                     {   byte[] b = item.getByteArray();
                         sb.append(indent); sb.append(offsett);
                         sb.append("<int8_array name=\""); sb.append(name); sb.append("\" count=\"");
                         sb.append(b.length); sb.append("\">\n");
                         for(int j=0; j<b.length; j++) {
                             // format little ints neatly
                             if (j%5 == 0) {wr.printf("%s%s%s%4d", indent, offsett, offsett, b[j]);}
                             else          {wr.printf(" %4d", b[j]);}
                             if (j%5 == 4 || j == b.length-1) {wr.printf("\n");}
                         }
                         wr.flush();
                         sb.append(sw.getBuffer());
                         sw.getBuffer().delete(0, sw.getBuffer().capacity());
                         sb.append(indent); sb.append(offsett);
                         sb.append("</int8_array>\n");
                     } break;

                   case cMsgConstants.payloadInt16A:
                     {   short[] i = item.getShortArray();
                         sb.append(indent); sb.append(offsett);
                         sb.append("<int16_array name=\""); sb.append(name); sb.append("\" count=\"");
                         sb.append(i.length); sb.append("\">\n");
                         for(int j=0; j<i.length; j++) {
                             // format little ints neatly
                             if (j%5 == 0) {wr.printf("%s%s%s%6d", indent, offsett, offsett, i[j]);}
                             else          {wr.printf(" %6d", i[j]);}
                             if (j%5 == 4 || j == i.length-1) {wr.printf("\n");}
                         }
                         wr.flush();
                         sb.append(sw.getBuffer());
                         sw.getBuffer().delete(0, sw.getBuffer().capacity());
                         sb.append(indent); sb.append(offsett);
                         sb.append("</int16_array>\n");
                     } break;

                   case cMsgConstants.payloadInt32A:
                     {   int[] i = item.getIntArray();
                         sb.append(indent); sb.append(offsett);
                         sb.append("<int32_array name=\""); sb.append(name); sb.append("\" count=\"");
                         sb.append(i.length); sb.append("\">\n");
                         for(int j=0; j<i.length; j++) {
                           if (j%5 == 0) {sb.append(indent);sb.append(offsett);sb.append(offsett);}
                           else {sb.append(" ");}
                           sb.append(i[j]);
                           if (j%5 == 4 || j == i.length-1) {sb.append("\n");}
                         }
                         sb.append(indent); sb.append(offsett);
                         sb.append("</int32_array>\n");
                     } break;

                   case cMsgConstants.payloadInt64A:
                     {   long[] i = item.getLongArray();
                         sb.append(indent); sb.append(offsett);
                         sb.append("<int64_array name=\""); sb.append(name); sb.append("\" count=\"");
                         sb.append(i.length); sb.append("\">\n");
                         for(int j=0; j<i.length; j++) {
                           if (j%5 == 0) {sb.append(indent);sb.append(offsett);sb.append(offsett);}
                           else {sb.append(" ");}
                           sb.append(i[j]);
                           if (j%5 == 4 || j == i.length-1) {sb.append("\n");}
                         }
                         sb.append(indent); sb.append(offsett);
                         sb.append("</int64_array>\n");
                     } break;

                   case cMsgConstants.payloadUint64A:
                     {   BigInteger[] i = item.getBigIntArray();
                         sb.append(indent); sb.append(offsett);
                         sb.append("<uint64_array name=\""); sb.append(name); sb.append("\" count=\"");
                         sb.append(i.length); sb.append("\">\n");
                         for(int j=0; j<i.length; j++) {
                           if (j%5 == 0) {sb.append(indent);sb.append(offsett);sb.append(offsett);}
                           else {sb.append(" ");}
                           sb.append(i[j]);
                           if (j%5 == 4 || j == i.length-1) {sb.append("\n");}
                         }
                         sb.append(indent); sb.append(offsett);
                         sb.append("</uint64_array>\n");
                     } break;

                   case cMsgConstants.payloadDblA:
                     {   double[] d = item.getDoubleArray();
                         sb.append(indent); sb.append(offsett);
                         sb.append("<double_array name=\""); sb.append(name); sb.append("\" count=\"");
                         sb.append(d.length); sb.append("\">\n");
                         for(int j=0; j<d.length; j++) {
                             // format doubles (16 significant digits minimum)
                             if (j%5 == 0) {wr.printf("%s%s%s%.17g", indent, offsett, offsett, d[j]);}
                             else          {wr.printf(" %.17g", d[j]);}
                             if (j%5 == 4 || j == d.length-1) {wr.printf("\n");}
                         }
                         wr.flush();
                         sb.append(sw.getBuffer());
                         sw.getBuffer().delete(0, sw.getBuffer().capacity());
                         sb.append(indent); sb.append(offsett);
                         sb.append("</double_array>\n");
                     } break;

                   case cMsgConstants.payloadFltA:
                     {   float[] f = item.getFloatArray();
                         sb.append(indent); sb.append(offsett);
                         sb.append("<float_array name=\""); sb.append(name); sb.append("\" count=\"");
                         sb.append(f.length); sb.append("\">\n");
                         for(int j=0; j<f.length; j++) {
                             // format floats (7 significant digits minimum)
                             if (j%5 == 0) {wr.printf("%s%s%s%.8g", indent, offsett, offsett, f[j]);}
                             else          {wr.printf(" %.8g", f[j]);}
                             if (j%5 == 4 || j == f.length-1) {wr.printf("\n");}
                         }
                         wr.flush();
                         sb.append(sw.getBuffer());
                         sw.getBuffer().delete(0, sw.getBuffer().capacity());
                         sb.append(indent); sb.append(offsett);
                         sb.append("</float_array>\n");
                     } break;

                   case cMsgConstants.payloadStrA:
                     {   String[] sa = item.getStringArray();
                         sb.append(indent); sb.append(offsett);
                         sb.append("<string_array name=\""); sb.append(name); sb.append("\" count=\"");
                         sb.append(sa.length); sb.append("\">\n");
                         for(String s : sa) {
                             sb.append(indent);sb.append(offsett);sb.append(offsett);
                             sb.append("<string><![CDATA["); sb.append(escapeCdataForXML(s)); sb.append("]]></string>\n");
                         }
                         sb.append(indent); sb.append(offsett);
                         sb.append("</string_array>\n");
                     } break;

                    case cMsgConstants.payloadBinA:
                      {   byte[][] b   = item.getBinaryArray();
                          int[] endian = item.getEndianArray();

                          sb.append(indent); sb.append(offsett);
                          sb.append("<binary_array name=\""); sb.append(name); sb.append("\" count=\"");
                          sb.append(b.length); sb.append("\">\n");

                          for (int i=0; i<b.length; i++) {
                              sb.append(indent);sb.append(offsett);sb.append(offsett);

                              if (endian[i] == cMsgConstants.endianBig) {
                                  sb.append("<binary endian=\"big\"");
                              }
                              else {
                                  sb.append("<binary endian=\"little\"");
                              }
                              sb.append(" nbytes=\""); sb.append(b[i].length);
                              if (!binary) {
                                  sb.append("\" />\n");
                              }
                              else {
                                  // put in line breaks after 76 chars (57 bytes)
                                  if (b[i].length > 57) {
                                      sb.append("\">\n");
                                      sb.append(Base64.encodeToString(b[i], true));
                                      sb.append(indent);sb.append(offsett);sb.append(offsett);
                                      sb.append("</binary>\n");
                                  }
                                  else {
                                      sb.append("\">");
                                      sb.append(Base64.encodeToString(b[i], false));
                                      sb.append("</binary>\n");
                                  }
                              }
                          }

                          sb.append(indent); sb.append(offsett);
                          sb.append("</binary_array>\n");

                      } break;

                    default:

                } // switch
            } // for each entry
        }
        catch (cMsgException ex) {
            return null;
        }

        sb.append(indent); sb.append("</payload>\n");
        return sb;
    }


    /////////////////////////////
    // payload
    /////////////////////////////


    /**
     * Method to convert 16 hex characters into a long value.
     * No arguments checks are made.
     * @param hex string to convert
     * @param zeroSup first char is "Z" for zero suppression so only
     *                convert next 15 chars
     * @return long value of string
     */
    static final long hexStrToLong (String hex, boolean zeroSup) {
        if (!zeroSup) {
            return (((long) toByte[hex.charAt(0)]  << 60) |
                    ((long) toByte[hex.charAt(1)]  << 56) |
                    ((long) toByte[hex.charAt(2)]  << 52) |
                    ((long) toByte[hex.charAt(3)]  << 48) |
                    ((long) toByte[hex.charAt(4)]  << 44) |
                    ((long) toByte[hex.charAt(5)]  << 40) |
                    ((long) toByte[hex.charAt(6)]  << 36) |
                    ((long) toByte[hex.charAt(7)]  << 32) |
                    ((long) toByte[hex.charAt(8)]  << 28) |
                    ((long) toByte[hex.charAt(9)]  << 24) |
                    ((long) toByte[hex.charAt(10)] << 20) |
                    ((long) toByte[hex.charAt(11)] << 16) |
                    ((long) toByte[hex.charAt(12)] << 12) |
                    ((long) toByte[hex.charAt(13)] <<  8) |
                    ((long) toByte[hex.charAt(14)] <<  4) |
                    ((long) toByte[hex.charAt(15)]));
        }
        else {
            long l=0;
            l =    (((long) toByte[hex.charAt(1)]  << 56) |
                    ((long) toByte[hex.charAt(2)]  << 52) |
                    ((long) toByte[hex.charAt(3)]  << 48) |
                    ((long) toByte[hex.charAt(4)]  << 44) |
                    ((long) toByte[hex.charAt(5)]  << 40) |
                    ((long) toByte[hex.charAt(6)]  << 36) |
                    ((long) toByte[hex.charAt(7)]  << 32) |
                    ((long) toByte[hex.charAt(8)]  << 28) |
                    ((long) toByte[hex.charAt(9)]  << 24) |
                    ((long) toByte[hex.charAt(10)] << 20) |
                    ((long) toByte[hex.charAt(11)] << 16) |
                    ((long) toByte[hex.charAt(12)] << 12) |
                    ((long) toByte[hex.charAt(13)] <<  8) |
                    ((long) toByte[hex.charAt(14)] <<  4) |
                    ((long) toByte[hex.charAt(15)]));
            return l;
        }
    }

    /**
     * Method to convert 8 hex characters into a int value.
     * No arguments checks are made.
     * @param hex string to convert
     * @param zeroSup first char is "Z" for zero suppression so only
     *                convert next 7 chars
     * @return int value of string
     */
    static final int hexStrToInt (String hex, boolean zeroSup) {
        if (!zeroSup) {
            return ((toByte[hex.charAt(0)] << 28) |
                    (toByte[hex.charAt(1)] << 24) |
                    (toByte[hex.charAt(2)] << 20) |
                    (toByte[hex.charAt(3)] << 16) |
                    (toByte[hex.charAt(4)] << 12) |
                    (toByte[hex.charAt(5)] <<  8) |
                    (toByte[hex.charAt(6)] <<  4) |
                    (toByte[hex.charAt(7)]));
        }
        else {
            int i=0;
            i = (   (toByte[hex.charAt(1)] << 24) |
                    (toByte[hex.charAt(2)] << 20) |
                    (toByte[hex.charAt(3)] << 16) |
                    (toByte[hex.charAt(4)] << 12) |
                    (toByte[hex.charAt(5)] <<  8) |
                    (toByte[hex.charAt(6)] <<  4) |
                    (toByte[hex.charAt(7)]));
            return i;
        }
    }

    /**
     * Method to convert 4 hex characters into a short value.
     * No arguments checks are made.
     * @param hex string to convert
     * @param zeroSup first char is "Z" for zero suppression so only
     *                convert next 3 chars
     * @return int value of string
     */
    static final short hexStrToShort (String hex, boolean zeroSup) {
        if (!zeroSup) {
            return  ((short) (
                    (toByte[hex.charAt(0)] << 12) |
                    (toByte[hex.charAt(1)] <<  8) |
                    (toByte[hex.charAt(2)] <<  4) |
                    (toByte[hex.charAt(3)])));
        }
        else {
            short i=0;
            i = ((short) (
                    (toByte[hex.charAt(1)] <<  8) |
                    (toByte[hex.charAt(2)] <<  4) |
                    (toByte[hex.charAt(3)])));
            return i;
        }
    }

    /**
     * Copy only the payload of the given message, overwriting
     * the existing payload.
     * @param msg message to copy payload from
     */
    public void copyPayload(cMsgMessage msg) {
        items.clear();
        for (Map.Entry<String, cMsgPayloadItem> entry : msg.getPayloadItems().entrySet()) {
            items.put(entry.getKey(), (cMsgPayloadItem)entry.getValue().clone());
        }
        payloadText = msg.payloadText;
    }

    /**
     * Gets the String representation of the compound payload of this message.
     * @return creator of this message.
     */
    public String getPayloadText() {
        return payloadText;
    }

    /**
     * Does this message have a payload?
     * @return true if message has payload, else false.
     */
    public boolean hasPayload() {
        return ((info & hasPayload) == hasPayload);
    }

    /**
     * Set the "has-a-compound-payload" bit of a message.
     * @param hp boolean which is true if msg has a compound payload, else is false (no payload)
     */
    protected void hasPayload(boolean hp) {
      info = hp ? info | hasPayload  :  info & ~hasPayload;
    }

    /**
     * Does this message have a payload that consists of objects in a hashmap
     * (ie. is "expanded")? Or does it simply have a payloadText field which has
     * not been expanded.
     * @return true if message has an expanded payload, else false.
     */
    protected boolean isExpandedPayload() {
        return ((info & expandedPayload) == expandedPayload);
    }


    /**
     * Set the "expanded-payload" bit of a message.
     * @param ep boolean which is true if msg has an expanded payload, else false
     */
    protected void setExpandedPayload(boolean ep) {
        info = ep ? info | expandedPayload  :  info & ~expandedPayload;
    }


    /**
     * If this message is unexpanded (has a non-null payloadText field but
     * no items in its payload hashmap), then expand the payload text into
     * a hashmap containing all cMsgPayloadItems.
     */
    protected void expandPayload() {
        if (isExpandedPayload() || payloadText == null) {
            setExpandedPayload(true);
            return;
        }

        try {
            // if expanding a serialized message, certain objects will be null
            if (buffer  == null) buffer  = new StringBuilder(512);
            if (items   == null) items   = new ConcurrentHashMap<String, cMsgPayloadItem>();
            if (context == null) context = new cMsgMessageContextDefault();
  
            // create real payload items from text
            setFieldsFromText(payloadText, allFields);

            setExpandedPayload(true);
        }
        catch (cMsgException e) {
            // should not be thrown if internal code is bug-free
            setExpandedPayload(false);
        }
    }


    /**
     * Gets an unmodifiable (read only) hashmap of all payload items.
     * @return a hashmap of all payload items.
     */
    public Map<String,cMsgPayloadItem> getPayloadItems() {
        if (!isExpandedPayload()) {
            expandPayload();
        }
        return Collections.unmodifiableMap(items);
    }

    /** Clears the payload of all items including system items.  */
    public void resetPayload() {
        items.clear();
        updatePayloadText();
    }

    /** Clears the payload of all user-added items.  */
    public void clearPayload() {
        for (String name : items.keySet()) {
            // Do NOT allow system items to be removed from the payload
            if (cMsgPayloadItem.validSystemName(name)) {
                continue;
            }
            items.remove(name);
        }
        updatePayloadText();
    }

    /**
     * Adds an item to the payload.
     * @param item item to add to payload
     */
    public void addPayloadItem(cMsgPayloadItem item) {
        if (item == null) return;
        items.put(item.name, item);
        hasPayload(true);
        updatePayloadText();
     }

    /**
     * Remove an item from the payload.
     * @param item item to remove from the payload
     * @return true if item removed, else false
     */
    public boolean removePayloadItem(cMsgPayloadItem item) {
        // Do NOT allow system items to be removed from the payload
        if (cMsgPayloadItem.validSystemName(item.name)) {
            return false;
        }
        boolean b = (items.remove(item.name) != null);
        if (items.size() < 1) hasPayload(false);
        updatePayloadText();
        return b;
    }

    /**
     * Remove an item from the payload.
     * @param name name of the item to remove from the payload
     * @return true if item removed, else false
     */
    public boolean removePayloadItem(String name) {
        // Do NOT allow system items to be removed from the payload
        if (cMsgPayloadItem.validSystemName(name)) {
            return false;
        }
        boolean b = (items.remove(name) != null);
        if (items.size() < 1) hasPayload(false);
        updatePayloadText();
        return b;
    }

    /**
     * Get a single, named payload item.
     * @param name name of item to retrieve
     * @return payload item if it exists, else null
     */
    public cMsgPayloadItem getPayloadItem(String name) {
         return (items.get(name));
    }

    /**
     * Get the set of payload item names (may be empty set).
     * @return set of payload item names
     */
    public Set<String> getPayloadNames() {
         return (items.keySet());
    }

    /**
     * Get the number of items in the payload.
     * @return number of items in the payload
     */
    public int getPayloadSize() {
         return (items.size());
    }

    /**
     * This method creates a string representation of the whole compound
     * payload and the hidden system fields (currently fields describing
     * the history of the message being sent) of the message and stores
     * it in the {@link #payloadText} member.
     * This string is used for sending the payload over the network.
     */
    public void updatePayloadText() {
        payloadText = createPayloadText(items);
    }


    /**
     * This method creates a string representation out of a given map of payload items
     * with names as keys.
     * @param map a map of payload items to be turned into a string representation
     * @return string representation of given map of payload items
     */
    String createPayloadText(Map<String, cMsgPayloadItem> map) {
        int count = 0, totalLen = 0;

        if (map.size() < 1) {
            return null;
        }

        // find total length of text representations of all payload items
        for (cMsgPayloadItem item : map.values()) {
            totalLen += item.text.length();
            count++;
        }

        totalLen += cMsgPayloadItem.numDigits(count) + 1; // send count & newline first

        // ensure buffer size, and clear it
        if (buffer.capacity() < totalLen) buffer.ensureCapacity(totalLen + 512);
        buffer.delete(0,buffer.capacity());

        // first item is number of fields to come (count) & newline
        buffer.append(count);
        buffer.append("\n");

        // add payload fields
        for (cMsgPayloadItem item : map.values()) {
            if (count-- < 1) break;
            buffer.append(item.text);
        }

        return buffer.toString();
    }


    /**
     * This method creates a string representation out of the existing payload text and the
     * given history parameters made into payload items together. Used when generating a text
     * representation of the payload to be sent over the network containing additions for sender
     * history of names, times, hosts.
     * <p>
     * When a client sends the same message over and over again, we do <b>NOT</b> want the history
     * to change. To ensure this, we follow a simple principle: the sender history needs to go
     * into the sent message (ie. over the wire), but must not be added to the local one.
     * Thus, if I send a message, its local sender history will not change.
     *
     * @param sendersName name to be added to history of senders
     * @param sendersHost host to be added to history of sender hosts
     * @param sendersTime time to be added to history of sender times
     * @return string representation of existing payload text and the given history payload items together
     */
    public String addHistoryToPayloadText(String sendersName, String sendersHost, long sendersTime) {
        int count = 0, totalLen = 0;
        // 3 items in history: sender, senderHost, senderTime
        cMsgPayloadItem[] history = new cMsgPayloadItem[3];

        // first add creator if necessary
        if (!items.containsKey("cMsgCreator")) {
            try {
                cMsgPayloadItem it = new cMsgPayloadItem("cMsgCreator", sendersName, true);
                addPayloadItem(it);
            }
            catch (cMsgException e) { /* never happens */ }
        }

        // set max history length if it isn't set and is NOT the default
        if (!items.containsKey("cMsgHistoryLengthMax") &&
                historyLengthMax != historyLengthInit) {
            try {
                cMsgPayloadItem it = new cMsgPayloadItem("cMsgHistoryLengthMax", historyLengthMax, true);
                addPayloadItem(it);
            }
            catch (cMsgException e) { /* never happens */ }
        }

        // if set not to record history, just return
        if (historyLengthMax < 1) {
            return payloadText;
        }

        cMsgPayloadItem item_n = items.get("cMsgSenderNameHistory");
        cMsgPayloadItem item_h = items.get("cMsgSenderHostHistory");
        cMsgPayloadItem item_t = items.get("cMsgSenderTimeHistory");

        try {
            if (item_n == null) {
                String[] newNames = {sendersName};
                history[0] = new cMsgPayloadItem("cMsgSenderNameHistory", newNames, true);

                String[] newHosts = {sendersHost};
                history[1] = new cMsgPayloadItem("cMsgSenderHostHistory", newHosts, true);

                long[] newTimes = {sendersTime};
                history[2] = new cMsgPayloadItem("cMsgSenderTimeHistory", newTimes, true);
            }
            else {
                // get existing historys in senders and senderTimes
                String[] names = item_n.getStringArray();
                String[] hosts = item_h.getStringArray();
                long[]   times = item_t.getLongArray();

                // keep only historyLengthMax number of the latest names
                int index = 0, len = names.length;
                if (names.length >= historyLengthMax) {
                    len   = historyLengthMax - 1;
                    index = names.length - len;
                }
                String[] newNames = new String[len + 1];
                String[] newHosts = new String[len + 1];
                long[]   newTimes = new long  [len + 1];

                System.arraycopy(names, index, newNames, 0, len);
                System.arraycopy(hosts, index, newHosts, 0, len);
                System.arraycopy(times, index, newTimes, 0, len);

                newNames[len] = sendersName;
                newHosts[len] = sendersHost;
                newTimes[len] = sendersTime;

                history[0] = new cMsgPayloadItem("cMsgSenderNameHistory", newNames, true);
                history[1] = new cMsgPayloadItem("cMsgSenderHostHistory", newHosts, true);
                history[2] = new cMsgPayloadItem("cMsgSenderTimeHistory", newTimes, true);
            }
        }
        catch (cMsgException e) {/* should never happen */}


        // Find total length of text representations of all existing payload items
        // except for history items which will be replaced.
        for (Map.Entry<String,cMsgPayloadItem> entry : items.entrySet()) {
            String key = entry.getKey();
            if (key.equals("cMsgSenderNameHistory") ||
                key.equals("cMsgSenderTimeHistory") ||
                key.equals("cMsgSenderHostHistory"))  {
                continue;
            }
            totalLen += entry.getValue().text.length();
            count++;
        }
        for (cMsgPayloadItem item : history) {
            totalLen += item.text.length();
            count++;
        }

        totalLen += cMsgPayloadItem.numDigits(count) + 1; // send count & newline first

        // ensure buffer size, and clear it
        if (buffer.capacity() < totalLen) buffer.ensureCapacity(totalLen + 512);
        buffer.delete(0,buffer.capacity());

        // first item is number of fields to come (count) & newline
        buffer.append(count);
        buffer.append("\n");

        // add payload fields
        for (cMsgPayloadItem item : history) {
            if (count-- < 1) break;
            buffer.append(item.text);
        }
        for (Map.Entry<String,cMsgPayloadItem> entry : items.entrySet()) {
            String key = entry.getKey();
            if (key.equals("cMsgSenderNameHistory") ||
                key.equals("cMsgSenderTimeHistory") ||
                key.equals("cMsgSenderHostHistory"))  {
                continue;
            }
            if (count-- < 1) break;
            buffer.append(entry.getValue().text);
        }

        return buffer.toString();
    }


    /**
     * This method creates a string of all the payload items concatonated.
     *
     * @return resultant string if successful
     * @return null if no payload exists
     */
    public String getItemsText() {
        int totalLen = 0;

        if (items.size() < 1) {
            return null;
        }

        // find total length, first payload, then text field
        for (cMsgPayloadItem item : items.values()) {
            totalLen += item.text.length();
        }

        // ensure buffer size, and clear it
        if (buffer.capacity() < totalLen) buffer.ensureCapacity(totalLen + 512);
        buffer.delete(0,buffer.capacity());

        // concatenate payload fields
        for (cMsgPayloadItem item : items.values()) {
            buffer.append(item.text);
        }

        return buffer.toString();
    }


    /**
     * This method takes a string representation of the whole compound payload,
     * including the system (hidden) fields of the message,
     * as it gets sent over the network and converts it into the standard message
     * payload. This overwrites any existing payload and may set system fields
     * as well depending on the given flag.
     *
     * @param text string sent over network to be unmarshalled
     * @param flag if {@link #systemFieldsOnly}, set system msg fields only,
     *             if {@link #payloadFieldsOnly} set payload msg fields only,
     *             and if {@link #allFields} set both
     * @return index index pointing just past last character in text that was parsed
     * @throws cMsgException if the text is in a bad format or the text arg is null
     */
    protected int setFieldsFromText(String text, int flag) throws cMsgException {

        int index1, index2, firstIndex;
        boolean debug = false;

        try {

            if (text == null) throw new cMsgException("bad argument");
//System.out.println("Text to decode:\n" + text + "\n\n");
            // read number of fields to come
            index1 = 0;
            index2 = text.indexOf('\n');
            if (index2 < 1) throw new cMsgException("bad format1");

            String sub = text.substring(index1, index2);
            int fields = Integer.parseInt(sub);

            if (fields < 1) throw new cMsgException("bad format2");
            if (debug) System.out.println("# fields = " + fields);

            // get rid of any existing payload
            resetPayload();

            String name, tokens[];
            int dataType, count, noHeaderLen=0, headerLen, totalItemLen;
            boolean ignore, isSystem;

            // loop through all fields in payload
            for (int i = 0; i < fields; i++) {

                if (debug) System.out.println("index1 = " + index1 + ", index2 = " + index2);
                firstIndex = index1 = index2 + 1;
                index2 = text.indexOf('\n', index1);
                if (debug) System.out.println("index1 = " + index1 + ", index2 = " + index2);
                if (index2 < 1) throw new cMsgException("bad format3");
                sub = text.substring(index1, index2);
                if (debug) System.out.println("Header text = " + sub);

// dissect line into 5 separate values
                tokens = sub.split(" ");
                //if (tokens.length != 5) throw new cMsgException("bad format4");
                if (debug) System.out.println("# items on headler line = " + tokens.length);
                name        = tokens[0];
                dataType    = Integer.parseInt(tokens[1]);
                count       = Integer.parseInt(tokens[2]);
                isSystem    = Integer.parseInt(tokens[3]) != 0;
                // There may be extra spaces between these last two values.
                // This must be accounted for as zero-len strings
                // are included as tokens.
                for (int j=4; j<tokens.length; j++) {
                    if (tokens[j].length() < 1) continue;
                    noHeaderLen = Integer.parseInt(tokens[j]);
                }

                // length of header line
                headerLen = index2 - index1 + 1;
                // length of whole item in chars
                totalItemLen = headerLen + noHeaderLen;

                if (debug) System.out.println("FIELD #" + i + ": name = " + name + ", type = " + dataType +
                        ", count = " + count + ", isSys = " + isSystem + ", len header = " + headerLen +
                        ", len noheader = " + noHeaderLen + ", sub len = " + sub.length());

                if (name.length() < 1 || count < 1 || noHeaderLen < 1 ||
                        dataType < cMsgConstants.payloadStr ||
                        dataType > cMsgConstants.payloadBinA) throw new cMsgException("bad format5");

                // ignore certain fields (by convention, system fields start with "cmsg")
                ignore = isSystem;                  // by default ignore system fields, flag == payloadFieldsOnly
                if (flag == systemFieldsOnly)  ignore = !ignore;  // only set system fields
                else if (flag == allFields)    ignore = false;    // deal with all fields

                /* skip over fields to be ignored */
                if (ignore) {
                    for (int j = 0; j < count; j++) {
                        // Skip over field
                        index1 = ++index2 + headerLen;
                        index2 = text.indexOf('\n', index1);
                        if (index2 < 1 && i != fields - 1) throw new cMsgException("bad format6");
                        if (debug) System.out.println("  skipped field");
                    }
                    continue;
                }

                // move past header line to beginning of value part */
                index1 = index2 + 1;

                // string
                if (dataType == cMsgConstants.payloadStr) {
                    addStringFromText(name, isSystem, text, index1, firstIndex, noHeaderLen);
                }
                // string array
                else if (dataType == cMsgConstants.payloadStrA) {
                    addStringArrayFromText(name, count, isSystem, text, index1, firstIndex, noHeaderLen);
                }
                // binary data
                else if (dataType == cMsgConstants.payloadBin) {
                    addBinaryFromText(name, count, isSystem, text, index1, firstIndex, noHeaderLen);
                }
                // arrays of binary data
                else if (dataType == cMsgConstants.payloadBinA) {
                    addBinaryArrayFromText(name, count, isSystem, text, index1, firstIndex, noHeaderLen);
                }
                // double or float
                else if (dataType == cMsgConstants.payloadDbl ||
                        dataType == cMsgConstants.payloadFlt) {
                    addRealFromText(name, dataType, isSystem, text, index1, firstIndex, noHeaderLen);
                }
                // double or float array
                else if (dataType == cMsgConstants.payloadDblA ||
                        dataType == cMsgConstants.payloadFltA) {
                    addRealArrayFromText(name, dataType, count, isSystem, text, index1, firstIndex, noHeaderLen);
                }
                // all ints
                else if (dataType == cMsgConstants.payloadInt8   || dataType == cMsgConstants.payloadInt16  ||
                        dataType == cMsgConstants.payloadInt32  || dataType == cMsgConstants.payloadInt64  ||
                        dataType == cMsgConstants.payloadUint8  || dataType == cMsgConstants.payloadUint16 ||
                        dataType == cMsgConstants.payloadUint32 || dataType == cMsgConstants.payloadUint64)  {

                    addIntFromText(name, dataType, isSystem, text, index1, firstIndex, noHeaderLen);
                }
                // all int arrays
                else if (dataType == cMsgConstants.payloadInt8A   || dataType == cMsgConstants.payloadInt16A  ||
                        dataType == cMsgConstants.payloadInt32A  || dataType == cMsgConstants.payloadInt64A  ||
                        dataType == cMsgConstants.payloadUint8A  || dataType == cMsgConstants.payloadUint16A ||
                        dataType == cMsgConstants.payloadUint32A || dataType == cMsgConstants.payloadUint64A)  {

                    addIntArrayFromText(name, dataType, count, isSystem, text, index1, firstIndex, noHeaderLen);
                }
                // cMsg messages
                else if (dataType == cMsgConstants.payloadMsg || dataType == cMsgConstants.payloadMsgA) {
                    cMsgMessage[] newMsgs = new cMsgMessage[count];
                    for (int j = 0; j < count; j++) {
                        // create a single message
                        newMsgs[j] = new cMsgMessage();
                        // call to setFieldsFromText to fill msg's fields
                        index1 += newMsgs[j].setFieldsFromText(text.substring(index1), allFields);
                        newMsgs[j].setExpandedPayload(true);
                    }
                    addMessagesFromText(name, dataType, isSystem, newMsgs, text,
                                        firstIndex, noHeaderLen, totalItemLen);
                }

                else {
                    throw new cMsgException("bad format7");
                }

                // go to the next line
                index2 = firstIndex + totalItemLen - 1;

            } // for each field

        }
        catch (NumberFormatException e) {
            throw new cMsgException(e);
        }

        return (index2 + 1);
    }

    /**
     * This method adds a named field of a string to the compound payload
     * of a message. The text representation of the payload item is copied in
     * (doesn't need to be generated).
     *
     * @param name name of field to add
     * @param isSystem if false, add item to payload, else set system parameters
     * @param txt string read in over wire for message's text field
     * @param index1 index into txt after the item's header line
     * @param fullIndex index into txt at beginning of item (before header line)
     * @param noHeadLen len of txt in ASCII chars NOT including header (first) line,
     *                  includes last "\n"
     *
     * @throws cMsgException if txt is in a bad format
     *
     */
    private void addStringFromText(String name, boolean isSystem, String txt,
                                   int index1, int fullIndex, int noHeadLen)
            throws cMsgException {

        // start after header line, first item is length of string but we'll skip over it
        // since we don't really need it in java
        int index2 = txt.indexOf('\n', index1);
        if (index2 < 1) throw new cMsgException("bad format");
        int secondLineLen = index2 - index1 + 1;

        // next is string value of this payload item
        index1 = index2 + 1;
        index2 = index1 + noHeadLen - secondLineLen - 1; // don't look for \n's !!!

        if (index2 < 1) throw new cMsgException("bad format");
        String val = txt.substring(index1, index2);

        // is regular field in msg
        if (isSystem) {
            // Normally don't stick system stuff in payload
            boolean putInPayload = false;
            if      (name.equals("cMsgText"))         text = val;
            else if (name.equals("cMsgSubject"))      subject = val;
            else if (name.equals("cMsgType"))         type = val;
            else if (name.equals("cMsgDomain"))       domain = val;
            else if (name.equals("cMsgSender"))       sender = val;
            else if (name.equals("cMsgSenderHost"))   senderHost = val;
            else if (name.equals("cMsgReceiver"))     receiver = val;
            else if (name.equals("cMsgReceiverHost")) receiverHost = val;
            else putInPayload = true;
            if (!putInPayload) return;
        }

        // get full text representation of item so it doesn't need to be recalculated
        // when creating the payload item (+1 includes last newline)
        String textRep = txt.substring(fullIndex, index2+1);

        // create payload item to add to msg  & store in payload
        addPayloadItem(new cMsgPayloadItem(name, val, textRep, noHeadLen, isSystem));

        return;
    }


    /**
     * This method adds a named field of a string array to the compound payload
     * of a message. The text representation of the payload item is copied in
     * (doesn't need to be generated).
     *
     * @param name name of field to add
     * @param count number of elements in array
     * @param isSystem if false, add item to payload, else set system parameters
     * @param txt string read in over wire for message's text field
     * @param index1 index into txt after the item's header line
     * @param fullIndex index into txt at beginning of item (before header line)
     * @param noHeadLen len of txt in ASCII chars NOT including header (first) line
     *
     * @throws cMsgException if txt is in a bad format
     */
    private void addStringArrayFromText(String name, int count, boolean isSystem, String txt,
                                   int index1, int fullIndex, int noHeadLen)
            throws cMsgException {

        int index2, len;
        String[] vals = new String[count];
//System.out.println("addStringArrayFromText Inn" + txt);
        for (int i = 0; i < count; i++) {
            // first item is length of string
            index2 = txt.indexOf('\n', index1);
            if (index2 < 1) throw new cMsgException("bad format");
            len = Integer.parseInt(txt.substring(index1,index2));
            if (len < 1) throw new cMsgException("bad format");

            // next is string value of this payload item
            index1 = index2 + 1;
            index2 = index1 + len;
            vals[i] = txt.substring(index1, index2);
            index1 = index2 + 1;
        }

        // get full text representation of item so it doesn't need to be recalculated
        // when creating the payload item
        String textRep = txt.substring(fullIndex, index1);
//System.out.println("addStringArrayFromText textRep len = " + textRep.length() + ", text =\n" + textRep);

        // Create payload item to add to msg  & store in payload.
        // In this case, system fields are passed on untouched.
        // This means the string array that tracks names of senders for this message
        // (cMsgSenders), is passed on as is.
        addPayloadItem(new cMsgPayloadItem(name, vals, textRep, noHeadLen, isSystem));

        return;
    }

    /**
     * This method adds a named field of a string array to the compound payload
     * of a message. The text representation of the payload item is copied in
     * (doesn't need to be generated).
     *
     * @param name name of field to add
     * @param count size in bytes of original binary data
     * @param isSystem if = 0, add item to payload, else set system parameters
     * @param txt string read in over wire for message's text field
     * @param index1 index into txt after the item's header line
     * @param fullIndex index into txt at beginning of item (before header line)
     * @param noHeadLen len of txt in ASCII chars NOT including header (first) line
     *
     * @throws cMsgException if txt is in a bad format
     */
    private void addBinaryFromText(String name, int count, boolean isSystem, String txt,
                                   int index1, int fullIndex, int noHeadLen)
            throws cMsgException {

        // start after header line, the next 3 items are:
        // length of string, length of original binary, and endian
        int index2 = txt.indexOf('\n', index1);
        if (index2 < 1) throw new cMsgException("bad format");
        String[] stuff = txt.substring(index1, index2).split(" ");
        int base64StrLen = Integer.parseInt(stuff[0]);
        if (base64StrLen < 1) throw new cMsgException("bad format");
        int binLen = Integer.parseInt(stuff[1]);
        if (binLen < 1) throw new cMsgException("bad format");
        int endian = Integer.parseInt(stuff[2]);

        // next is string value of this payload item
        index1 = index2 + 1;
        index2 = index1 + base64StrLen;
        String val = txt.substring(index1, index2);

        // decode string into binary (wrong characters are ignored)
        byte[] b = null;
        try  { b = Base64.decodeToBytes(val, "US-ASCII"); }
        catch (UnsupportedEncodingException e) {/*never happen*/}

        if (b.length != binLen) {
            System.out.println("reconstituted binary is different length than original binary");
        }

        // is regular field in msg
        if (isSystem && name.equals("cMsgBinary")) {
            // Place binary data into message's binary array - not in the payload.
            // First decode the str into binary.
            bytes  = b;
            offset = 0;
            length = bytes.length;
            setByteArrayEndian(endian);
            return;
        }

        // get full text representation of item so it doesn't need to be recalculated
        // when creating the payload item
        String textRep = txt.substring(fullIndex, index2);

        // create payload item to add to msg  & store in payload
        addPayloadItem(new cMsgPayloadItem(name, b, endian, textRep, noHeadLen, isSystem));

        return;
    }


    /**
     * This method adds a named field of a string array to the compound payload
     * of a message. The text representation of the payload item is copied in
     * (doesn't need to be generated).
     *
     * @param name name of field to add
     * @param count size in bytes of original binary data
     * @param isSystem if = 0, add item to payload, else set system parameters
     * @param txt string read in over wire for message's text field
     * @param index1 index into txt after the item's header line
     * @param fullIndex index into txt at beginning of item (before header line)
     * @param noHeadLen len of txt in ASCII chars NOT including header (first) line
     *
     * @throws cMsgException if txt is in a bad format
     */
    private void addBinaryArrayFromText(String name, int count, boolean isSystem, String txt,
                                   int index1, int fullIndex, int noHeadLen)
            throws cMsgException {

//System.out.println("addBinaryArrayFromText, txt = " + txt);

        // start after header line, first items are length of string and endian
        int index2, base64StrLen, binLen, endian;
        String val, stuff[];
        int[] endians = new int[count];
        byte[][] vals = new byte[count][];

        for (int i = 0; i < count; i++) {
            // start after header line, items are: length of string, len of binary, and endian
            index2 = txt.indexOf('\n', index1);
            if (index2 < 1) throw new cMsgException("bad format");
//System.out.println("parsing: " + txt.substring(index1, index2));
            stuff = txt.substring(index1, index2).split(" ");
//for (int j=0; j<stuff.length; j++) {
//    System.out.println("stuf[" + j + "] = " + stuff[j]);
//}
            base64StrLen = Integer.parseInt(stuff[0]);
            if (base64StrLen < 1) throw new cMsgException("bad format");
            binLen = Integer.parseInt(stuff[1]);
            if (binLen < 1) throw new cMsgException("bad format");
            endians[i] = Integer.parseInt(stuff[2]);

            // next is string value of this payload item
            index1 = index2 + 1;
            index2 = index1 + base64StrLen;
            val = txt.substring(index1, index2);
//System.out.println("val = " + val);

            // decode string into binary (wrong characters are ignored)
            try  { vals[i] = Base64.decodeToBytes(val, "US-ASCII"); }
            catch (UnsupportedEncodingException e) {/*never happen*/}
            
            if (vals[i].length != binLen) {
                System.out.println("reconstituted binary is different length than original binary");
            }

            index1 = index2;
        }

        // Get full text representation of item so it doesn't need to be recalculated
        // when creating the payload item.
        String textRep = txt.substring(fullIndex, index1);
//System.out.println("addBinaryArrayFromText textRep len = " + textRep.length() + ", text =\n" + textRep);

        // Create payload item to add to msg & store in payload.
        // In this case, system fields are passed on untouched.
        addPayloadItem(new cMsgPayloadItem(name, vals, endians, textRep, noHeadLen, isSystem));

        return;
    }


    /**
     * This method adds a named field of a double or float to the compound payload
     * of a message. The text representation of the payload item is copied in
     * (doesn't need to be generated).
     *
     * @param name name of field to add
     * @param dataType either {@link cMsgConstants#payloadDbl} or {@link cMsgConstants#payloadFlt}
     * @param isSystem if false, add item to payload, else set system parameters
     * @param txt string read in over wire for message's text field
     * @param index1 index into txt after the item's header line
     * @param fullIndex index into txt at beginning of item (before header line)
     * @param noHeadLen len of txt in ASCII chars NOT including header (first) line
     *
     * @throws cMsgException if txt is in a bad format
     *
     */
    private void addRealFromText(String name, int dataType, boolean isSystem, String txt,
                                 int index1, int fullIndex, int noHeadLen)
            throws cMsgException {

        // start after header line, first item is real value
        int index2 = txt.indexOf('\n', index1);
        if (index2 < 1) throw new cMsgException("bad format");
        String val = txt.substring(index1, index2);
//System.out.println("real (hex) = " + val + ", # chars = " + val.length());

        // get full text representation of item so it doesn't need to be recalculated
        // when creating the payload item
        String textRep = txt.substring(fullIndex, index2+1);

        if (dataType == cMsgConstants.payloadDbl) {
            // convert from 16 chars (representing hex) to double
            long lval = hexStrToLong(val, false);
            // now convert long (8 bytes of IEEE-754 format) into double
            double d = Double.longBitsToDouble(lval);

            // create payload item to add to msg  & store in payload
            addPayloadItem(new cMsgPayloadItem(name, d, textRep, noHeadLen, isSystem));
        }
        else {
            // convert from 8 chars (representing hex) to float
            int ival = hexStrToInt(val, false);
            // now convert int (4 bytes of IEEE-754 format) into float
            float f = Float.intBitsToFloat(ival);

            // create payload item to add to msg  & store in payload
            addPayloadItem(new cMsgPayloadItem(name, f, textRep, noHeadLen, isSystem));
        }

        return;
    }

    /**
     * This method adds a named field of a double or float array to the compound payload
     * of a message. The text representation of the payload item is copied in
     * (doesn't need to be generated).
     *
     * @param name name of field to add
     * @param dataType either {@link cMsgConstants#payloadDbl} or {@link cMsgConstants#payloadFlt}
     * @param count number of elements in array
     * @param isSystem if false, add item to payload, else set system parameters
     * @param txt string read in over wire for message's text field
     * @param index1 index into txt after the item's header line
     * @param fullIndex index into txt at beginning of item (before header line)
     * @param noHeadLen len of txt in ASCII chars NOT including header (first) line
     *
     * @throws cMsgException if txt is in a bad format
     *
     */
    private void addRealArrayFromText(String name, int dataType, int count, boolean isSystem, String txt,
                                 int index1, int fullIndex, int noHeadLen)
            throws cMsgException {

        // start after header line, first item is all real array values
        int index2 = txt.indexOf('\n', index1);
        if (index2 < 1) throw new cMsgException("bad format");
        String val = txt.substring(index1, index2);
//System.out.println("real array (hex) = " + val);

        // get full text representation of item so it doesn't need to be recalculated
        // when creating the payload item
        String textRep = txt.substring(fullIndex, index2+1);

        // vars used to get next number
        String num;
        int i1, i2;

        if (dataType == cMsgConstants.payloadDblA) {
            i1 = -17;
            long zeros, lval;
            double darray[] = new double[count];

            for (int i = 0; i < count; i++) {
                // pick out next number from the line
                i1 = i1 + 17;
                i2 = i1 + 16;
                num = val.substring(i1, i2);

                // if first char is Z (and not a hex char),
                // it's a signal to undo zero suppression
                if (toByte[num.charAt(0)] == -2) {
                    // convert from 15 chars (representing hex) to long
                    zeros = hexStrToLong(num, true);

                    // we have "zeros" number of zeros
                    for (int j=0; j<zeros; j++) {
                      darray[i+j] = 0.;
//System.out.println("  double[" + (i+j) + "] = 0.");
                    }
                    i += zeros - 1;
                    continue;
                }

                // convert from 16 chars (representing hex) to double
                lval = hexStrToLong(num, false);
                // now convert long (8 bytes of IEEE-754 format) into double
                darray[i] = Double.longBitsToDouble(lval);
//System.out.println("  double[" + i + "] = " +  darray[i]);
            }

            // create payload item to add to msg  & store in payload
            addPayloadItem(new cMsgPayloadItem(name, darray, textRep, noHeadLen, isSystem));
        }

        else if (dataType == cMsgConstants.payloadFltA) {
            i1 = -9;
            int   zeros, ival;
            float farray[] = new float[count];

            for (int i = 0; i < count; i++) {
                // pick out next number from the line
                i1 = i1 + 9;
                i2 = i1 + 8;
                num = val.substring(i1, i2);

                // if first char is Z (and not a hex char), undo zero suppression
                if (toByte[num.charAt(0)] == -2) {
                    // convert from 7 chars (representing hex) to long
                    zeros = hexStrToInt(num, true);

                    // we have "zeros" number of zeros
                    for (int j=0; j<zeros; j++) {
                      farray[i+j] = 0.f;
//System.out.println("  float[" + (i+j) + "] = 0.");
                    }
                    i += zeros - 1;
                    continue;
                }

                // convert from 8 chars (representing hex) to float
                ival = hexStrToInt(num, false);
                // now convert int (4 bytes of IEEE-754 format) into float
                farray[i] = Float.intBitsToFloat(ival);
//System.out.println("  float[" + i + "] = " +  farray[i]);
            }

            // create payload item to add to msg  & store in payload
            addPayloadItem(new cMsgPayloadItem(name, farray, textRep, noHeadLen, isSystem));
         }

        return;
    }

    /**
     * This method adds a named int to the compound payload
     * of a message. The text representation of the payload item is copied in
     * (doesn't need to be generated).
     *
     * @param name name of field to add
     * @param dataType either {@link cMsgConstants#payloadInt8}, {@link cMsgConstants#payloadInt16},
     *                        {@link cMsgConstants#payloadInt32}, {@link cMsgConstants#payloadInt64},
     *                        {@link cMsgConstants#payloadUint8}, {@link cMsgConstants#payloadUint16},
     *                        {@link cMsgConstants#payloadUint32}, or {@link cMsgConstants#payloadUint64}
     * @param isSystem if false, add item to payload, else do something special maybe (currently nothing)
     * @param txt string read in over wire for message's text field
     * @param index1 index into txt after the item's header line
     * @param fullIndex index into txt at beginning of item (before header line)
     * @param noHeadLen len of txt in ASCII chars NOT including header (first) line
     *
     * @throws cMsgException if txt is in a bad format
     *
     */
    private void addIntFromText(String name, int dataType, boolean isSystem, String txt,
                                int index1, int fullIndex, int noHeadLen)
             throws cMsgException {

        // start after header line, first item is integer
        int index2 = txt.indexOf('\n', index1);
        if (index2 < 1) throw new cMsgException("bad format");
        String val = txt.substring(index1, index2);
//System.out.println("add Int = " + val);

        // get full text representation of item so it doesn't need to be recalculated
        // when creating the payload item
        String textRep = txt.substring(fullIndex, index2+1);

        // system field used to set the max # of history entries in msg
        if (isSystem && name.equals("cMsgHistoryLengthMax")) {
            historyLengthMax = Integer.parseInt(val);
        }

        if (dataType == cMsgConstants.payloadInt8) {
            // create payload item to add to msg  & store in payload
            addPayloadItem(new cMsgPayloadItem(name, Byte.parseByte(val), textRep, noHeadLen, isSystem));
        }
        else if (dataType == cMsgConstants.payloadInt16) {
            addPayloadItem(new cMsgPayloadItem(name, Short.parseShort(val), textRep, noHeadLen, isSystem));
        }
        else if (dataType == cMsgConstants.payloadInt32) {
            addPayloadItem(new cMsgPayloadItem(name, Integer.parseInt(val), textRep, noHeadLen, isSystem));
        }
        else if (dataType == cMsgConstants.payloadInt64) {
            addPayloadItem(new cMsgPayloadItem(name, Long.parseLong(val), textRep, noHeadLen, isSystem));
        }
        // upgrade unsigned char to short for java
        else if (dataType == cMsgConstants.payloadUint8) {
            addPayloadItem(new cMsgPayloadItem(name, Short.parseShort(val), textRep, noHeadLen, isSystem));
        }
        // upgrade unsigned short to int for java
        else if (dataType == cMsgConstants.payloadUint16) {
            addPayloadItem(new cMsgPayloadItem(name, Integer.parseInt(val), textRep, noHeadLen, isSystem));
        }
        // upgrade unsigned int to long for java
        else if (dataType == cMsgConstants.payloadUint32) {
            addPayloadItem(new cMsgPayloadItem(name, Long.parseLong(val), textRep, noHeadLen, isSystem));
        }
        // upgrade unsigned long to BigInteger for java
        else if (dataType == cMsgConstants.payloadUint64) {
            addPayloadItem(new cMsgPayloadItem(name, new BigInteger(val), textRep, noHeadLen, isSystem));
        }

        return;
    }


    /**
     * This method adds a named field of a integer array to the compound payload
     * of a message. The text representation of the payload item is copied in
     * (doesn't need to be generated).
     *
     * @param name name of field to add
     * @param dataType any iteger type (e.g. {@link cMsgConstants#payloadInt8A})
     * @param count number of elements in array
     * @param isSystem if false, add item to payload, else set system parameters
     * @param txt string read in over wire for message's text field
     * @param index1 index into txt after the item's header line
     * @param fullIndex index into txt at beginning of item (before header line)
     * @param noHeadLen len of txt in ASCII chars NOT including header (first) line
     *
     * @throws cMsgException if txt is in a bad format
     *
     */
    private void addIntArrayFromText(String name, int dataType, int count, boolean isSystem, String txt,
                                     int index1, int fullIndex, int noHeadLen)
            throws cMsgException {

        // start after header line, first item is all int array values
        int index2 = txt.indexOf('\n', index1);
        if (index2 < 1) throw new cMsgException("bad format");
        String val = txt.substring(index1, index2);
//System.out.println("add Int array = " + val);

        // get full text representation of item so it doesn't need to be recalculated
        // when creating the payload item
        String textRep = txt.substring(fullIndex, index2+1);

        // Identify the UNSIGNED types here since they
        // don't exist in java and need to be promoted.
        // In java we need BigInteger object to represent the unsigned 64-bit ints.
        boolean bigInt     = dataType == cMsgConstants.payloadUint64A;
        boolean unsigned32 = dataType == cMsgConstants.payloadUint32A;
        boolean unsigned16 = dataType == cMsgConstants.payloadUint16A;
        boolean unsigned8  = dataType == cMsgConstants.payloadUint8A;

        // vars used to get next number
        String num;
        int i1, i2;

        // if 64 bit integers ...
        if (dataType == cMsgConstants.payloadInt64A || bigInt) {
            i1 = -17;
            long zeros, larray[] = null;
            BigInteger[] b = null;
            if (bigInt)  {b = new BigInteger[count];}
            else {larray = new long[count];}

            for (int i = 0; i < count; i++) {
                // pick out next number from the line
                i1 = i1 + 17;
                i2 = i1 + 16;
                num = val.substring(i1, i2);

                // if first char is Z (and not a hex char),
                // it's a signal to undo zero suppression
                if (toByte[num.charAt(0)] == -2) {
                    // convert from 15 chars (representing hex) to long
                    zeros = hexStrToLong(num, true);

                    // we have "zeros" number of zeros
                    for (int j=0; j<zeros; j++) {
                        if (bigInt) {
                            b[i+j] = BigInteger.ZERO;
//System.out.println("  bigInt[" + (i+j) + "] = 0");
                        }
                        else {
                            larray[i+j] = 0;
//System.out.println("  long[" + (i+j) + "] = " + larray[i+j]);
                        }
                    }
                    i += zeros - 1;
                    continue;
                }

                if (bigInt) {
                    b[i] = new BigInteger(num, 16);
                }
                else {
                    // convert from 16 chars (representing hex) to long
                    larray[i] = hexStrToLong(num, false);
//System.out.println("  long[" + (i) + "] = " + larray[i]);
                }
            }

            if (isSystem && name.equals("cMsgTimes")) {
                if (count != 6) throw new cMsgException("bad format");
                userTime      = larray[0]*1000 + larray[1]/1000000;
                senderTime    = larray[2]*1000 + larray[3]/1000000;
                receiverTime  = larray[4]*1000 + larray[5]/1000000;
                return;
            }
            else {
                if (bigInt) {
                    addPayloadItem(new cMsgPayloadItem(name, b, textRep, noHeadLen, isSystem));
                }
                else {
                    addPayloadItem(new cMsgPayloadItem(name, larray, textRep, noHeadLen, isSystem, false));
                }
            }
        }

        // else if 32 bit ints ...
        else if (dataType == cMsgConstants.payloadInt32A || unsigned32) {
            i1 = -9;
            int zeros, ival, iarray[] = null;
            long[] larray = null;
            if (unsigned32)  {larray = new long[count];}
            else {iarray = new int[count];}

            for (int i = 0; i < count; i++) {
                // pick out next number from the line
                i1 = i1 + 9;
                i2 = i1 + 8;
                num = val.substring(i1, i2);

                // if first char is Z (and not a hex char), undo zero suppression
                if (toByte[num.charAt(0)] == -2) {
                    // convert from 7 chars (representing hex) to int
                    zeros = hexStrToInt(num, true);

                    // we have "zeros" number of zeros
                    for (int j=0; j<zeros; j++) {
                        if (unsigned32) {
                            larray[i] = 0L;
                        }
                        else {
                            iarray[i+j] = 0;
//System.out.println("  int[" + (i+j) + "] = " +  iarray[i+j]);
                        }
                    }
                    i += zeros - 1;
                    continue;
                }

                // convert from 8 chars (representing hex) to int
                ival = hexStrToInt(num, false);
                if (unsigned32) {
                    // Get rid of all the extended sign bits possibly added when turning
                    // int into long as ival may be perceived as a negative number.
                    larray[i] = ((long)ival) & 0xffffffffL;
                }
                else {
                    iarray[i] = ival;
//System.out.println("  int[" + i + "] = " +  iarray[i]);
                }
            }

            if (isSystem && name.equals("cMsgInts")) {
                if (count != 5) throw new cMsgException("bad format");
                version   = iarray[0];
                info      = iarray[1]; // generally this will say the payload is unexpanded
                reserved  = iarray[2];
                length    = iarray[3];
                userInt   = iarray[4];
                return;
            }
            else {
                if (unsigned32) {
                    addPayloadItem(new cMsgPayloadItem(name, larray, textRep, noHeadLen, isSystem, unsigned32));
                }
                else {
                    addPayloadItem(new cMsgPayloadItem(name, iarray, textRep, noHeadLen, isSystem, unsigned32));
                }
            }
         }

        // else if 16 bit ints ...
        else if (dataType == cMsgConstants.payloadInt16A || unsigned16) {
            i1 = -5;
            short sval, sarray[] = null;
            int  zeros, iarray[] = null;
            if (unsigned16)  {iarray = new int[count];}
            else {sarray = new short[count];}

            for (int i = 0; i < count; i++) {
                // pick out next number from the line
                i1 = i1 + 5;
                i2 = i1 + 4;
                num = val.substring(i1, i2);

                // if first char is Z (and not a hex char), undo zero suppression
                if (toByte[num.charAt(0)] == -2) {
                    // convert from 3 chars (representing hex) to int
                    zeros = hexStrToShort(num, true);

                    // we have "zeros" number of zeros
                    for (int j=0; j<zeros; j++) {
                        if (unsigned16) {
                            iarray[i] = 0;
                        }
                        else {
                            sarray[i+j] = 0;
//System.out.println("  int[" + (i+j) + "] = " +  iarray[i+j]);
                        }
                    }
                    i += zeros - 1;
                    continue;
                }

                // convert from 4 chars (representing hex) to short
                sval = hexStrToShort(num, false);
                if (unsigned16) {
                    // Get rid of all the extended sign bits possibly added when turning
                    // short into int as sval may be perceived as a negative number.
                    iarray[i] = sval & 0xffff;
                }
                else {
                    sarray[i] = sval;
                }
            }

            if (unsigned16) {
                addPayloadItem(new cMsgPayloadItem(name, iarray, textRep, noHeadLen, isSystem, unsigned16));
            }
            else {
                addPayloadItem(new cMsgPayloadItem(name, sarray, textRep, noHeadLen, isSystem, unsigned16));
            }
         }

        // else if 8 bit ints ...
        else if (dataType == cMsgConstants.payloadInt8A || unsigned8) {
            i1 = -3;
            byte  bval, barray[] = null;
            short sarray[] = null;
            if (unsigned8)  {sarray = new short[count];}
            else {barray = new byte[count];}

            for (int i = 0; i < count; i++) {
                // pick out next number from the line
                i1 = i1 + 3;
                i2 = i1 + 2;
                num = val.substring(i1, i2);

                // convert from 2 chars (representing hex) to byte
                bval = (byte)( (toByte[num.charAt(0)] <<  4) | (toByte[num.charAt(1)]) );
                if (unsigned8) {
                    // Get rid of all the extended sign bits possibly added when turning
                    // byte into short as sval may be perceived as a negative number.
                    sarray[i] = (short)(bval & 0xff);
                }
                else {
                    barray[i] = bval;
                }
            }

            if (unsigned8) {
                addPayloadItem(new cMsgPayloadItem(name, sarray, textRep, noHeadLen, isSystem, unsigned8));
            }
            else {
                addPayloadItem(new cMsgPayloadItem(name, barray, textRep, noHeadLen, isSystem));
            }
         }


        return;
    }

    /**
     * This method adds a named field of a cMsgMessage object or array of such
     * objects to the compound payload of a message. The text representation
     * of the payload item is copied in (doesn't need to be generated).
     *
     * @param name name of field to add
     * @param dataType either {@link cMsgConstants#payloadMsg} or {@link cMsgConstants#payloadMsgA}
     * @param isSystem if false, add item to payload, else set system parameters
     * @param newMsgs array of cMsgMessage objects
     * @param txt string read in over wire for message's text field
     * @param fullIndex index into txt at beginning of item (before header line)
     * @param noHeadLen len of txt in ASCII chars NOT including header (first) line
     * @param totalItemLen len of full txt in ASCII chars including header line
     *
     * @throws cMsgException if txt is in a bad format
     */
    private void addMessagesFromText(String name, int dataType, boolean isSystem,
                                     cMsgMessage[] newMsgs, String txt,
                                     int fullIndex, int noHeadLen, int totalItemLen)
            throws cMsgException {

        // get full text representation of item so it doesn't need to be recalculated
        // when creating the payload item
        String textRep = txt.substring(fullIndex, fullIndex+totalItemLen);

        if (dataType == cMsgConstants.payloadMsg) {
            addPayloadItem(new cMsgPayloadItem(name, newMsgs[0], textRep, noHeadLen, isSystem));
        }
        else {
            addPayloadItem(new cMsgPayloadItem(name, newMsgs, textRep, noHeadLen, isSystem));
        }

        return;
    }


    /**
     * This method prints out the message payload in a readable form.
     * Simpler than XML.
     * @param level level of indentation (0 = normal)
     */
    public void payloadPrintout(int level) {
        int j, typ;
        String indent="", name;
        cMsgPayloadItem item;

        // create the indent since a message may contain a message, etc.
        if (level < 1) {
            level = 0;
        }
        // limit indentation to 10 levels
        else {
            level %= 10;
        }

        if (level > 0) {
            char[] c = new char[level * 5];
            Arrays.fill(c, ' ');
            indent = new String(c);
        }

        // get all name & type info
        for (Map.Entry<String, cMsgPayloadItem> entry : items.entrySet()) {
            name = entry.getKey();
            item = entry.getValue();
            typ  = item.getType();

            System.out.print(indent + "FIELD " + name);

            try {
                switch (typ) {
                  case cMsgConstants.payloadInt8:
                    {byte i = item.getByte();   System.out.println(" (int8): " + i);}         break;
                  case cMsgConstants.payloadInt16:
                    {short i = item.getShort();  System.out.println(" (int16): " + i);}       break;
                  case cMsgConstants.payloadInt32:
                    {int i = item.getInt();  System.out.println(" (int32): " + i);}           break;
                  case cMsgConstants.payloadInt64:
                    {long i = item.getLong();  System.out.println(" (int64): " + i);}         break;
                  case cMsgConstants.payloadUint64:
                    {BigInteger i = item.getBigInt(); System.out.println(" (uint64): " + i);} break;
                  case cMsgConstants.payloadDbl:
                    {double d = item.getDouble(); System.out.println(" (double): " + d);}     break;
                  case cMsgConstants.payloadFlt:
                    {float f = item.getFloat();  System.out.println(" (float): " + f);}       break;
                  case cMsgConstants.payloadStr:
                    {String s = item.getString(); System.out.println(" (string): " + s);}     break;

                  case cMsgConstants.payloadInt8A:
                    {byte[] i = item.getByteArray(); System.out.println(":");
                        for(j=0; j<i.length;j++) System.out.println(indent + "  int8[" + j + "] = " + i[j]);}   break;
                  case cMsgConstants.payloadInt16A:
                    {short[] i = item.getShortArray(); System.out.println(":");
                        for(j=0; j<i.length;j++) System.out.println(indent + "  int16[" + j + "] = " + i[j]);}  break;
                  case cMsgConstants.payloadInt32A:
                    {int[] i = item.getIntArray(); System.out.println(":");
                        for(j=0; j<i.length;j++) System.out.println(indent + "  int32[" + j + "] = " + i[j]);}  break;
                  case cMsgConstants.payloadInt64A:
                    {long[] i = item.getLongArray(); System.out.println(":");
                        for(j=0; j<i.length;j++) System.out.println(indent + "  int64[" + j + "] = " + i[j]);}  break;
                  case cMsgConstants.payloadUint64A:
                    {BigInteger[] i = item.getBigIntArray(); System.out.println(":");
                        for(j=0; j<i.length;j++) System.out.println(indent + "  uint16[" + j + "] = " + i[j]);} break;
                  case cMsgConstants.payloadDblA:
                    {double[] i = item.getDoubleArray(); System.out.println(":");
                        for(j=0; j<i.length;j++) System.out.println(indent + "  double[" + j + "] = " + i[j]);} break;
                  case cMsgConstants.payloadFltA:
                    {float[] i = item.getFloatArray(); System.out.println(":");
                        for(j=0; j<i.length;j++) System.out.println(indent + "  float[" + j + "] = " + i[j]);}  break;
                  case cMsgConstants.payloadStrA:
                    {String[] i = item.getStringArray(); System.out.println(":");
                        for(j=0; j<i.length;j++) System.out.println(indent + "  string[" + j + "] = " + i[j]);} break;

                  case cMsgConstants.payloadBin:
                    {int sb,sz;
                     byte[] b = item.getBinary();
                     int end = item.getEndian();
                     // only print up to 1kB
                     sz = sb = b.length; if (sb > 1024) {sb = 1024;}
                     String enc = Base64.encodeToString(b,0,sb,true);
                     if (end == cMsgConstants.endianBig)
                          System.out.print(" (binary, big endian):\n" + indent + enc);
                     else System.out.print(" (binary, little endian):\n" + indent + enc);
                     if (sz > sb) {System.out.println(indent + "... " + (sz-sb) + " more bytes of binary not printed here ...");}
                    } break;

                  case cMsgConstants.payloadBinA:
                    {int sb,sz; String enc;
                     byte[][] b = item.getBinaryArray();
                     int[] end  = item.getEndianArray();
                     // only print up to 1kB
                     System.out.println(":");
                     for (j=0; j<b.length; j++) {
                       sz = sb = b[j].length; if (sb > 1024) {sb = 1024;}
                       enc = Base64.encodeToString(b[j],0,sb,true);
                       if (end[j] == cMsgConstants.endianBig)
                            System.out.print("  binary[" + j + "], big endian = \n" + indent + enc);
                       else System.out.print("  binary[" + j + "], little endian = \n" + indent + enc);
                       if (sz > sb) {System.out.println(indent + "... " + (sz-sb) + " more bytes of binary not printed here ...");}
                     }
                    } break;

                  case cMsgConstants.payloadMsg:
                    {cMsgMessage m = item.getMessage();
                     System.out.println(" (cMsg message):");
                     m.payloadPrintout(level+1);
                    } break;

                  case cMsgConstants.payloadMsgA:
                    {cMsgMessage[] m = item.getMessageArray();
                      System.out.println(":");
                      for (j=0; j<m.length; j++) {
                       System.out.println(indent + "  message[" + j + "] =");
                       m[j].payloadPrintout(level+1);
                     }
                    } break;

                  default:
                      System.out.println();
                }
            }
            catch (cMsgException ex) {

            }
        }
        return;
    }
}

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


import java.lang.*;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.io.UnsupportedEncodingException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.math.BigInteger;


/**
  * <b>This class implements a message in the cMsg messaging system.
  * Each cMsgMessage object contains many different fields. The most complex field
  * (and thus deserving a full explanation) is the compound payload. In short,
  * the payload allows the text field of the message to store messages of arbitrary
  * length and complexity. All types of ints (1,2,4,8 bytes), 4,8-byte floats,
  * strings, binary, whole messages and arrays of all these types can be stored
  * and retrieved from the compound payload. These methods are thread-safe.<p>
  *
  * Although XML would be a format well-suited to this task, cMsg should stand
  * alone - not requiring an XML parser to work. It takes more memory and time
  * to decode XML than a simple format. Thus, a simple, easy-to-parse format
  * was developed to implement this interface.<p>
  *
  * Following is the text format of a complete compound payload (where [nl] means
  * newline). Each payload consists of a number of items. The very first line is the
  * number of items in the payload. That is followed by the text representation of
  * each item. The first line of each item consists of 5 entries.<p>
  *
  * Note that there is only 1 space or newline between all entries. The only exception
  * to the 1 space spacing is between the last two entries on each "header" line (the line
  * that contains the item_name). There may be several spaces between the last 2
  * entries on these lines.<p></b>
  *
  *<pre>    item_count[nl]</pre><p>
  *
  *<b><i>for string items:</i></b><p>
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
  *    message_N_in_compound_payload_text_format[nl]</pre><p>
  *
  * <b>Notice that this format allows a message to store a message which stores a message
  * which stores a message, ad infinitum. In other words, recursive message storing.
  * The item_length in each case is the length in bytes of the rest of the item (not
  * including the newline at the end). Note that accessor methods can return null objects.</b>
  *
  * @author Elliott Wolin
  * @author Carl Timmer
  * @version 1.0
  */
public class cMsgMessage implements Cloneable {

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

    // general quantities

    /**
     * Unique message intVal created by cMsg system.
     * Used by cMsg domain server to track client's "sendAndGet" calls.
     */
    int sysMsgId;

    /** Message exists in this domain. */
    String domain;

    /**
     * Condensed information stored in bits.
     * Is message a sendAndGet request? -- stored in 1st bit.
     * Is message a response to a sendAndGet? -- stored in 2nd bit.
     * Is the response message null instead of a message? -- stored in 3rd bit.
     * Is the byte array data in big endian form? -- stored in 4th bit.
     *
     * @see #isGetRequest
     * @see #isGetResponse
     * @see #isNullGetResponse
     * @see #isBigEndian
     */
    int info;
    /** Version number of cMsg. */
    int version;
    /** Class member reserved for future use. */
    int reserved;
    /** Maximum number of entries a message keeps when recording the history of various parameters. */
    private int historyLengthMax;

    // user-settable quantities

    /** Subject of message. */
    String subject;
    /** Type of message. */
    String type;
    /** Text of message. */
    String text;
    /** Integer supplied by user. */
    int userInt;
    /** Time supplied by user in milliseconds from midnight GMT, Jan 1st, 1970. */
    long userTime;
    /** Byte array of message. */
    byte[] bytes;
    /** Offset into byte array of first element. */
    int offset;
    /** Length of byte array elements to use. */
    int length;

    // payload quantities

    /** List of payload items. */
    ConcurrentHashMap<String, cMsgPayloadItem> items;
    /** Buffer to help build the text represenation of the payload to send over network. */
    private StringBuilder buffer;
    /** String representation of the entire payload (including hidden system payload items). */
    String payloadText;

    // sender quantities

    /** Name of message sender (must be unique in some domains). */
    String sender;
    /** Host sender is running on. */
    String senderHost;
    /** Time message was sent in milliseconds from midnight GMT, Jan 1st, 1970. */
    long senderTime;
    /** Field used by domain server in implementing "sendAndGet". */
    int senderToken;

    // receiver quantities

    /** Nme of message receiver. */
    String receiver;
    /** Host receiver is running on. */
    String receiverHost;
    /** Time message was received in milliseconds from midnight GMT, Jan 1st, 1970. */
    long receiverTime;
    /** Message intVal number generated by client. */
    int receiverSubscribeId;

    // context information when passing msg to callback

    /** Object giving info about environment running callback with this message. */
    cMsgMessageContextInterface context = new cMsgMessageContextDefault();

    /**
     * Cloning this object does not pass on the context except for the value
     * of reliableSend and copies the byte array if it exists.
     * @return a cMsgMessage object which is a copy of this message
     */
    public Object clone() {
        try {
            boolean reliableSend = context.getReliableSend();
            cMsgMessage result = (cMsgMessage) super.clone();
            result.context = new cMsgMessageContextDefault();
            result.context.setReliableSend(reliableSend);
            result.buffer = new StringBuilder(512);
            result.items  = new ConcurrentHashMap<String, cMsgPayloadItem>();
            for (Map.Entry<String, cMsgPayloadItem> entry : items.entrySet()) {
                result.items.put(entry.getKey(), (cMsgPayloadItem)entry.getValue().clone());
            }
            if (bytes != null) {
                result.bytes = bytes.clone();
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
        items   = new ConcurrentHashMap<String, cMsgPayloadItem>();
        buffer  = new StringBuilder(512);
        info   |= expandedPayload;
        historyLengthMax = historyLengthInit;
    }


    /**
     * The constructor which copies a given message.
     *
     * @param msg message to be copied
     */
    public cMsgMessage(cMsgMessage msg) {
        sysMsgId            = msg.sysMsgId;
        domain              = msg.domain;
        info                = msg.info;
        version             = msg.version;
        subject             = msg.subject;
        type                = msg.type;
        text                = msg.text;
        bytes               = msg.bytes.clone();
        reserved            = msg.reserved;
        userInt             = msg.userInt;
        userTime            = msg.userTime;
        sender              = msg.sender;
        senderHost          = msg.senderHost;
        senderTime          = msg.senderTime;
        senderToken         = msg.senderToken;
        receiver            = msg.receiver;
        receiverHost        = msg.receiverHost;
        receiverTime        = msg.receiverTime;
        receiverSubscribeId = msg.receiverSubscribeId;
        historyLengthMax    = msg.historyLengthMax;
        buffer              = new StringBuilder(512);
        items               = new ConcurrentHashMap<String, cMsgPayloadItem>();
        for (Map.Entry<String, cMsgPayloadItem> entry : msg.items.entrySet()) {
            items.put(entry.getKey(), (cMsgPayloadItem)entry.getValue().clone());
        }
    }


    /**
     * Creates a complete copy of this message.
     *
     * @return copy of this message.
     */
    public cMsgMessage copy() {
        return (cMsgMessage) this.clone();
    }

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
     * @param time time.
     */
    public void setUserTime(Date time) {this.userTime = time.getTime();}

    // byte array stuff

    /**
     * Get byte array of message.
     * @return byte array of message.
     */
    public byte[] getByteArray() {return bytes;}

    /**
     * Set byte array of message. If the byte array is null, both the offset
     * and length get set to 0.
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
    }

    /**
     * Set byte array of message by copying the array argument.
     * If the byte array is null, both the offset and length get set to 0.
     * @param b byte array of message.
     * @param offset index into byte array to bytes of interest.
     * @param length number of bytes of interest.
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
        bytes  = new byte[length];

        System.arraycopy(b, offset, bytes, 0, length);
    }

    /**
     * Set byte array of message to the given argument without
     * copying the byte array itself - only the reference is copied.
     * If the byte array is null, both the offset and length get set to 0.
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
    }

    /**
     * Set byte array of message to the given argument without
     * copying the byte array itself - only the reference is copied.
     * If the byte array is null, both the offset and length get set to 0.
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
    }


    /**
     * Get byte array length of data of interest. This may be smaller
     * than the total length of the array if the user is only interested
     * in a portion of the array.
     * @return length of byte array's data of interest.
     */
    public int getByteArrayLength() {return length;}
    /**
     * Set byte array length of data of interest. This may be smaller
     * than the total length of the array if the user is only interested
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
     * Get byte array index to data of interest. This may be non-zero
     * if the user is only interested in a portion of the array.
     * @return index to byte array's data of interest.
     */
    public int getByteArrayOffset() {return offset;}
    /**
     * Set byte array index to data of interest. This may be non-zero
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


    /**
     * Get receiver subscribe intVal.
     * @return receiver subscribe intVal.
     */
    public int getReceiverSubscribeId() {return receiverSubscribeId;}


    /////////////
    // context
    /////////////


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


//---------------------------------------------------------------------------------
// Format strings used to create the XML representation of a message (same as in C)
//---------------------------------------------------------------------------------

    static private final String format1 =
        "%s<cMsgMessage date=\"%s\"\n" +
        "%s     version         = \"%d\"\n" +
        "%s     domain          = \"%s\"\n" +
        "%s     getRequest      = \"%s\"\n" +
        "%s     getResponse     = \"%s\"\n" +
        "%s     nullGetResponse = \"%s\"\n" +
        "%s     sender          = \"%s\"\n" +
        "%s     senderHost      = \"%s\"\n" +
        "%s     senderTime      = \"%s\"\n" +
        "%s     userInt         = \"%d\"\n" +
        "%s     userTime        = \"%s\"\n" +
        "%s     receiver        = \"%s\"\n" +
        "%s     receiverHost    = \"%s\"\n" +
        "%s     receiverTime    = \"%s\"\n" +
        "%s     subject         = \"%s\"\n" +
        "%s     type            = \"%s\">\n" +
        "%s     <text>\n" +
        "<![CDATA[%s]]>\n" +
        "%s     </text>\n" +
        "%s     <binary endian=\"%s\">\n" +
        "<![CDATA[%s]]>\n" +
        "%s     </binary>\n" +
        "%s     <payload>\n";

    static private final String format1a =
        "%s<cMsgMessage date=\"%s\"\n" +
        "%s     version         = \"%d\"\n" +
        "%s     domain          = \"%s\"\n" +
        "%s     getRequest      = \"%s\"\n" +
        "%s     getResponse     = \"%s\"\n" +
        "%s     nullGetResponse = \"%s\"\n" +
        "%s     sender          = \"%s\"\n" +
        "%s     senderHost      = \"%s\"\n" +
        "%s     senderTime      = \"%s\"\n" +
        "%s     userInt         = \"%d\"\n" +
        "%s     userTime        = \"%s\"\n" +
        "%s     receiver        = \"%s\"\n" +
        "%s     receiverHost    = \"%s\"\n" +
        "%s     receiverTime    = \"%s\"\n" +
        "%s     subject         = \"%s\"\n" +
        "%s     type            = \"%s\">\n" +
        "%s     <text>\n" +
        "<![CDATA[%s]]>\n" +
        "%s     </text>\n" +
        "%s     <binary endian=\"%s\">\n" +
        "<![CDATA[";

    static private final String format1b =
        "]]>\n" +
        "%s     </binary>\n" +
        "%s     <payload>\n";

    static private final String format2 =
        "%s     </payload>\n" +
        "%s</cMsgMessage>\n";

    static private final String stringInArrayFormat = "%s               <string> <![CDATA[%s]]> </string>\n";
    static private final String singleStringFormat  = "%s          <string name=\"%s\"> <![CDATA[%s]]> </string>\n";
    static private final String binaryFormata       = "%s          <binary name=\"%s\" endian=\"%s\"> <![CDATA[";
    static private final String binaryFormatb       = "]]> </binary>\n";


    /**
     * This method converts the message to a printable string in XML format.
     * Any binary data is encoded in the base64 format.
     *
     * @return message as XML String object
     * @return a blank string if any error occurs
     */
    public String toString() {
        return toString2(0,0,true);
    }


    /**
      * This method converts the message to a printable string in XML format.
      *
      * @param level the level of indent or recursive messaging (0 = none)
      * @param offset the number of spaces to add to the indent
      * @param binary includes binary as ASCII if true, else binary is ignored
      *
     * @return message as XML String object
     * @return a blank string if any error occurs
      */
    private String toString2(int level, int offset, boolean binary) {
        StringWriter sw = new StringWriter(2048);
        PrintWriter  wr = new PrintWriter(sw);

        // indentation is dependent on level of recursion
        String indent = "";
        if (level > 0) {
            char[] c = new char[level * 10 + offset];
            Arrays.fill(c, ' ');
            indent = new String(c);
        }

        Date now = new Date();
        Date useTime = new Date(userTime);
        Date sendTime = new Date(senderTime);
        Date receiveTime = new Date(receiverTime);

        // everything except payload and ending XML
        if (binary && (bytes != null) && (length > 0)) {
            int endian;
            String endianTxt;

            endian = getByteArrayEndian();
            if (endian == cMsgConstants.endianBig) endianTxt = "big";
            else endianTxt = "little";

            wr.printf(format1a,
                      indent, now, indent, version, indent, domain,
                      indent, ((info & isGetRequest) != 0) ? "true" : "false",
                      indent, ((info & isGetResponse) != 0) ? "true" : "false",
                      indent, ((info & isNullGetResponse) != 0) ? "true" : "false",
                      indent, sender, indent, senderHost, indent, sendTime,
                      indent, userInt, indent, useTime,
                      indent, receiver, indent, receiverHost, indent, receiveTime,
                      indent, subject, indent, type, indent, text, indent,
                      indent, endianTxt);

            wr.print(Base64.encodeToString(bytes, offset, length, true));
            wr.printf(format1b, indent, indent);
        }
        else {
            wr.printf(format1,
                      indent, now, indent, version, indent, domain,
                      indent, ((info & isGetRequest) != 0) ? "true" : "false",
                      indent, ((info & isGetResponse) != 0) ? "true" : "false",
                      indent, ((info & isNullGetResponse) != 0) ? "true" : "false",
                      indent, sender, indent, senderHost, indent, sendTime,
                      indent, userInt, indent, useTime,
                      indent, receiver, indent, receiverHost, indent, receiveTime,
                      indent, subject, indent, type, indent, text, indent,
                      indent, "big", "null", indent, indent);
        }

        // no payload so finish up and return
        if (!hasPayload()) {
          wr.printf("%s          (null)\n", indent);
          wr.printf(format2, indent, indent);
          return sw.toString();
        }

        try {
            // get all name & type info
            int typ;
            String name;
            cMsgPayloadItem item;

            for (Map.Entry<String, cMsgPayloadItem> entry : items.entrySet()) {
                name = entry.getKey();
                item = entry.getValue();
                typ  = item.getType();

                switch (typ) {
                    case cMsgConstants.payloadInt8:
                      {byte i = item.getByte();
                       wr.printf("%s          <int8 name=\"%s\">\n", indent, name);
                       wr.printf("%s               %d\n%s          </int8>\n", indent, i, indent);
                      } break;
                    case cMsgConstants.payloadInt16:
                      {short i = item.getShort();
                       wr.printf("%s          <int16 name=\"%s\">\n", indent, name);
                       wr.printf("%s               %d\n%s          </int16>\n", indent, i, indent);
                      } break;
                    case cMsgConstants.payloadInt32:
                      {int i = item.getInt();
                       wr.printf("%s          <int32 name=\"%s\">\n", indent, name);
                       wr.printf("%s               %d\n%s          </int32>\n", indent, i, indent);
                      } break;
                    case cMsgConstants.payloadInt64:
                      {long i = item.getLong();
                       wr.printf("%s          <int64 name=\"%s\">\n", indent, name);
                       wr.printf("%s               %d\n%s          </int64>\n", indent, i, indent);
                      } break;
                    case cMsgConstants.payloadUint64:
                      {BigInteger i = item.getBigInt();
                       wr.printf("%s          <uint64 name=\"%s\">\n", indent, name);
                       wr.printf("%s               %d\n%s          </uint64>\n", indent, i, indent);
                      } break;
                    case cMsgConstants.payloadDbl:
                      {double d = item.getDouble();
                       wr.printf("%s          <double name=\"%s\">\n", indent, name);
                       wr.printf("%s               %.16g\n%s          </double>\n", indent, d, indent);
                      } break;
                    case cMsgConstants.payloadFlt:
                      {float f = item.getFloat();
                       wr.printf("%s          <float name=\"%s\">\n", indent, name);
                       wr.printf("%s               %.7g\n%s          </float>\n", indent, f, indent);
                      } break;
                   case cMsgConstants.payloadStr:
                     {String s = item.getString();
                      wr.printf(singleStringFormat, indent, name, s);
                     } break;

                   case cMsgConstants.payloadBin:
                     {if (!binary) break;
                      byte[] b = item.getBinary();
                      int endian = item.getEndian();
                      String endianTxt = (endian == cMsgConstants.endianBig) ? "big" : "little";

                      wr.printf(binaryFormata, indent, name, endianTxt);
                      String enc = Base64.encodeToString(b);
                      wr.printf("%s",enc);
                      wr.printf(binaryFormatb);
                     } break;

                   case cMsgConstants.payloadMsg:
                     {cMsgMessage m = item.getMessage();
                      wr.printf("%s", m.toString2(level+1, offset, binary));
                     } break;

                   // arrays
                   case cMsgConstants.payloadMsgA:
                     {cMsgMessage[] msgs = item.getMessageArray();
                      wr.printf("%s          <cMsgMessage_array name=\"%s\">\n", indent, name);
                      for (cMsgMessage m : msgs) {
                          wr.printf("%s", m.toString2(level+1, 5, binary));
                      }
                      wr.printf("%s          </cMsgMessage_array>\n", indent);
                     } break;

                   case cMsgConstants.payloadInt8A:
                     {byte[] b = item.getByteArray();
                      wr.printf("%s          <int8_array name=\"%s\" count=\"%d\">\n", indent, name, b.length);
                      for(int j=0;j<b.length;j++) {
                         if (j%5 == 0) {wr.printf("%s               %4d", indent, b[j]);}
                         else          {wr.printf(" %4d", b[j]);}
                         if (j%5 == 4 || j == b.length-1) {wr.printf("\n");}
                      }
                      wr.printf("%s          </int8_array>\n", indent);
                     } break;

                   case cMsgConstants.payloadInt16A:
                     {short[] i = item.getShortArray();
                      wr.printf("%s          <int16_array name=\"%s\" count=\"%d\">\n", indent, name, i.length);
                      for(int j=0;j<i.length;j++) {
                         if (j%5 == 0) {wr.printf("%s               %6d", indent, i[j]); }
                         else          {wr.printf(" %6d", i[j]); }
                         if (j%5 == 4 || j == i.length-1) {wr.printf("\n"); }
                      }
                      wr.printf("%s          </int16_array>\n", indent);
                     } break;

                   case cMsgConstants.payloadInt32A:
                     {int[] i = item.getIntArray();
                      wr.printf("%s          <int32_array name=\"%s\" count=\"%d\">\n", indent, name, i.length);
                      for(int j=0;j<i.length;j++) {
                         if (j%5 == 0) {wr.printf("%s               %d", indent, i[j]); }
                         else          {wr.printf(" %d", i[j]); }
                         if (j%5 == 4 || j == i.length-1) {wr.printf("\n"); }
                      }
                      wr.printf("%s          </int32_array>\n", indent);
                     } break;

                   case cMsgConstants.payloadInt64A:
                     {long[] i = item.getLongArray();
                      wr.printf("%s          <int64_array name=\"%s\" count=\"%d\">\n", indent, name, i.length);
                      for(int j=0;j<i.length;j++) {
                         if (j%5 == 0) {wr.printf("%s               %d", indent, i[j]); }
                         else          {wr.printf(" %d", i[j]); }
                         if (j%5 == 4 || j == i.length-1) {wr.printf("\n"); }
                      }
                      wr.printf("%s          </int64_array>\n", indent);
                     } break;

                   case cMsgConstants.payloadUint64A:
                     {BigInteger[] i = item.getBigIntArray();
                      wr.printf("%s          <uint64_array name=\"%s\" count=\"%d\">\n", indent, name, i.length);
                      for(int j=0;j<i.length;j++) {
                         if (j%5 == 0) {wr.printf("%s               %d", indent, i[j]); }
                         else          {wr.printf(" %d", i[j]); }
                         if (j%5 == 4 || j == i.length-1) {wr.printf("\n"); }
                      }
                      wr.printf("%s          </uint64_array>\n", indent);
                     } break;

                   case cMsgConstants.payloadDblA:
                     {double[] d = item.getDoubleArray();
                      wr.printf("%s          <double_array name=\"%s\" count=\"%d\">\n", indent, name, d.length);
                      for(int j=0;j<d.length;j++) {
                         if (j%5 == 0) {wr.printf("%s               %.16g", indent, d[j]); }
                         else          {wr.printf(" %.16g", d[j]); }
                         if (j%5 == 4 || j == d.length-1) {wr.printf("\n"); }
                      }
                      wr.printf("%s          </double_array>\n", indent);
                     } break;

                   case cMsgConstants.payloadFltA:
                     {float[] f = item.getFloatArray();
                      wr.printf("%s          <float_array name=\"%s\" count=\"%d\">\n", indent, name, f.length);
                      for(int j=0;j<f.length;j++) {
                         if (j%5 == 0) {wr.printf("%s               %.7g", indent, f[j]); }
                         else          {wr.printf(" %.7g", f[j]); }
                         if (j%5 == 4 || j == f.length-1) {wr.printf("\n"); }
                      }
                      wr.printf("%s          </float_array>\n", indent);
                     } break;

                   case cMsgConstants.payloadStrA:
                     {String[] sa = item.getStringArray();
                      wr.printf("%s          <string_array name=\"%s\" count=\"%d\">\n", indent, name, sa.length);
                      for(String s : sa) {
                         wr.printf(stringInArrayFormat, indent, s);
                      }
                      wr.printf("%s          </string_array>\n", indent);
                     } break;
                } // switch
            } // for each entry
        }
        catch (cMsgException ex) {
            return "";
        }

        //   </payload>
        // </cMsgMessage>
        wr.printf(format2, indent, indent);
        wr.flush();
        return sw.toString();
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
     * Gets an unmodifiable (read only) hashmap of all payload items.
     * @return a hashmap of all payload items.
     */
    public Map<String,cMsgPayloadItem> getPayloadItems() {
        return Collections.unmodifiableMap(items);
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
     * Adds a name to the history of senders of this message (in the payload).
     * This method only keeps {@link #historyLengthMax} number of the most recent names.
     * This method is reserved for system use only.
     * @param name name of sender to add to the history of senders
     */
    public void addSenderToHistory(String name) {
        // if set not to record history, just return
        if (historyLengthMax < 1) {
            return;
        }

        cMsgPayloadItem item = items.get("cMsgSenderHistory");
        try {
            if (item == null) {
                String[] newNames = {name};
                cMsgPayloadItem it = new cMsgPayloadItem("cMsgSenderHistory", newNames, true);
                addPayloadItem(it);
            }
            else {
                // get existing history
                String[] names = item.getStringArray();

                // Don't repeat names consecutively. That just means that a msg producer
                // is sending the same message repeatedly.
                if (name.equals(names[names.length-1])) return;

                // keep only historyLengthMax number of the latest names
                int index = 0, len = names.length;
                if (names.length >= historyLengthMax) {
                    len   = historyLengthMax - 1;
                    index = names.length - len;
                }
                String[] newNames = new String[len + 1];
                System.arraycopy(names, index, newNames, 0, len);
                newNames[len] = name;
                cMsgPayloadItem it = new cMsgPayloadItem("cMsgSenderHistory", newNames, true);
                addPayloadItem(it);
            }
        }
        catch (cMsgException e) {/* should never happen */}

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
     * the history of the message bding sent) of the message and stores
     * it in the {@link #payloadText} member.
     * This string is used for sending the payload over the network.
     */
    public void updatePayloadText() {
        int count = 0, totalLen = 0;

        if (items.size() < 1) {
            payloadText = null;
            return;
        }

        // find total length of text representations of all payload items
        for (cMsgPayloadItem item : items.values()) {
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
        for (cMsgPayloadItem item : items.values()) {
            if (count-- < 1) break;
            buffer.append(item.text);
        }

        payloadText = buffer.toString();
        return;
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

        if (text == null) throw new cMsgException("bad argument");
//System.out.println("Text to decode:\n" + text);
        // read number of fields to come
        index1 = 0;
        index2 = text.indexOf('\n');
        if (index2 < 1) throw new cMsgException("bad format1");

        String sub = text.substring(index1, index2);
        int fields = Integer.parseInt(sub);

        if (fields < 1) throw new cMsgException("bad format2");
        if (debug) System.out.println("# fields = " + fields);

        // get rid of any existing payload
        clearPayload();

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
                    dataType > cMsgConstants.payloadMsgA) throw new cMsgException("bad format5");

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
                }
                addMessagesFromText(name, dataType, newMsgs, text, firstIndex, noHeaderLen, totalItemLen);
            }

            else {
                throw new cMsgException("bad format7");
            }

            // go to the next line
            index2 = firstIndex + totalItemLen - 1;

        } // for each field

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
     * @param noHeadLen len of txt in ASCII chars NOT including header (first) line
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
        int skip = index2 - index1 + 1;

        // next is string value of this payload item
        index1 = index2 + 1;
        index2 = index1 + noHeadLen - skip; // don't look for \n's !!!

        if (index2 < 1) throw new cMsgException("bad format");
        String val = txt.substring(index1, index2);

        // is regular field in msg
        if (isSystem) {
            if      (name.equals("cMsgText"))         text = val;
            else if (name.equals("cMsgSubject"))      subject = val;
            else if (name.equals("cMsgType"))         type = val;
            else if (name.equals("cMsgDomain"))       domain = val;
            else if (name.equals("cMsgSender"))       sender = val;
            else if (name.equals("cMsgSenderHost"))   senderHost = val;
            else if (name.equals("cMsgReceiver"))     receiver = val;
            else if (name.equals("cMsgReceiverHost")) receiverHost = val;
            return;
        }

        // get full text representation of item so it doesn't need to be recalculated
        // when creating the payload item (+1 includes last newline)
        String textRep = txt.substring(fullIndex, index2+1);

        // create payload item to add to msg  & store in payload
        addPayloadItem(new cMsgPayloadItem(name, val, textRep, noHeadLen));

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

        int index2;
        String[] vals = new String[count];
//System.out.println("addStringArrayFromText Inn" + txt);
        for (int i = 0; i < count; i++) {
            // first item is length of string but we'll skip over it
            // since we don't really need it in java
            index2 = txt.indexOf('\n', index1);
            if (index2 < 1) throw new cMsgException("bad format");

            // next is string value of this payload item
            index1 = index2 + 1;
            index2 = txt.indexOf('\n', index1);
            if (index2 < 1) {
//System.out.println("Cannot find ending <nl> in string array text");
                throw new cMsgException("bad format");
            }
            vals[i] = txt.substring(index1, index2);
            index1 = index2 + 1;
        }

        // get full text representation of item so it doesn't need to be recalculated
        // when creating the payload item (+1 includes last newline)
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

        // start after header line, first items are length of string and endian
        int index2 = txt.indexOf('\n', index1);
        if (index2 < 1) throw new cMsgException("bad format");
        String[] stuff = txt.substring(index1, index2).split(" ");
//System.out.println("addBinFromText: stuff = " + txt.substring(index1, index2));
        int endian = Integer.parseInt(stuff[1]);

        // next is string value of this payload item
        index1 = index2 + 1;
        index2 = txt.indexOf('\n', index1);
        if (index2 < 1) throw new cMsgException("bad format");
        String val = txt.substring(index1, index2);

        // decode string into binary (wrong characters are ignored)
        byte[] b = null;
        try  { b = Base64.decodeToBytes(val, "US-ASCII"); }
        catch (UnsupportedEncodingException e) {/*never happen*/}

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
        String textRep = txt.substring(fullIndex, index2+1);

        // create payload item to add to msg  & store in payload
        addPayloadItem(new cMsgPayloadItem(name, b, endian, textRep, noHeadLen));

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
            addPayloadItem(new cMsgPayloadItem(name, d, textRep, noHeadLen));
        }
        else {
            // convert from 8 chars (representing hex) to float
            int ival = hexStrToInt(val, false);
            // now convert int (4 bytes of IEEE-754 format) into float
            float f = Float.intBitsToFloat(ival);

            // create payload item to add to msg  & store in payload
            addPayloadItem(new cMsgPayloadItem(name, f, textRep, noHeadLen));
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
            addPayloadItem(new cMsgPayloadItem(name, darray, textRep, noHeadLen));
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
            addPayloadItem(new cMsgPayloadItem(name, farray, textRep, noHeadLen));
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

        if (dataType == cMsgConstants.payloadInt8) {
            // create payload item to add to msg  & store in payload
            addPayloadItem(new cMsgPayloadItem(name, Byte.parseByte(val), textRep, noHeadLen));
        }
        else if (dataType == cMsgConstants.payloadInt16) {
            addPayloadItem(new cMsgPayloadItem(name, Short.parseShort(val), textRep, noHeadLen));
        }
        else if (dataType == cMsgConstants.payloadInt32) {
            addPayloadItem(new cMsgPayloadItem(name, Integer.parseInt(val), textRep, noHeadLen));
        }
        else if (dataType == cMsgConstants.payloadInt64) {
            addPayloadItem(new cMsgPayloadItem(name, Long.parseLong(val), textRep, noHeadLen));
        }
        // upgrade unsigned char to short for java
        else if (dataType == cMsgConstants.payloadUint8) {
            addPayloadItem(new cMsgPayloadItem(name, Short.parseShort(val), textRep, noHeadLen));
        }
        // upgrade unsigned short to int for java
        else if (dataType == cMsgConstants.payloadUint16) {
            addPayloadItem(new cMsgPayloadItem(name, Integer.parseInt(val), textRep, noHeadLen));
        }
        // upgrade unsigned int to long for java
        else if (dataType == cMsgConstants.payloadUint32) {
            addPayloadItem(new cMsgPayloadItem(name, Long.parseLong(val), textRep, noHeadLen));
        }
        // upgrade unsigned long to BigInteger for java
        else if (dataType == cMsgConstants.payloadUint64) {
            addPayloadItem(new cMsgPayloadItem(name, new BigInteger(val), textRep, noHeadLen));
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

            if (isSystem) {
                if (name.equals("cMsgTimes")) {
                  if (count != 6) throw new cMsgException("bad format");
                  userTime      = larray[0]*1000 + larray[1]/1000000;
                  senderTime    = larray[2]*1000 + larray[3]/1000000;
                  receiverTime  = larray[4]*1000 + larray[5]/1000000;
                }
                return;
            }
            else {
                if (bigInt) {
                    addPayloadItem(new cMsgPayloadItem(name, b, textRep, noHeadLen));
                }
                else {
                    addPayloadItem(new cMsgPayloadItem(name, larray, textRep, noHeadLen));
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

            if (isSystem) {
                if (name.equals("cMsgInts")) {
                  if (count != 5) throw new cMsgException("bad format");
                    version   = iarray[0];
                    info      = iarray[1];
                    reserved  = iarray[2];
                    length    = iarray[3];
                    userInt   = iarray[4];
                }
                return;
            }
            else {
                if (unsigned32) {
                    addPayloadItem(new cMsgPayloadItem(name, larray, textRep, noHeadLen));
                }
                else {
                    addPayloadItem(new cMsgPayloadItem(name, iarray, textRep, noHeadLen));
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
                addPayloadItem(new cMsgPayloadItem(name, iarray, textRep, noHeadLen));
            }
            else {
                addPayloadItem(new cMsgPayloadItem(name, sarray, textRep, noHeadLen));
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
                addPayloadItem(new cMsgPayloadItem(name, sarray, textRep, noHeadLen));
            }
            else {
                addPayloadItem(new cMsgPayloadItem(name, barray, textRep, noHeadLen));
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
     * @param newMsgs array of cMsgMessage objects
     * @param txt string read in over wire for message's text field
     * @param fullIndex index into txt at beginning of item (before header line)
     * @param noHeadLen len of txt in ASCII chars NOT including header (first) line
     * @param totalItemLen len of full txt in ASCII chars including header line
     *
     * @throws cMsgException if txt is in a bad format
     */
    private void addMessagesFromText(String name, int dataType, cMsgMessage[] newMsgs, String txt,
                                     int fullIndex, int noHeadLen, int totalItemLen)
            throws cMsgException {

        // get full text representation of item so it doesn't need to be recalculated
        // when creating the payload item
        String textRep = txt.substring(fullIndex, fullIndex+totalItemLen);

        if (dataType == cMsgConstants.payloadMsg) {
            addPayloadItem(new cMsgPayloadItem(name, newMsgs[0], textRep, noHeadLen));
        }
        else {
            addPayloadItem(new cMsgPayloadItem(name, newMsgs, textRep, noHeadLen));
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
                     String enc = Base64.encodeToString(b);
                     if (end == cMsgConstants.endianBig) System.out.println(" (binary, big endian):\n" + indent + enc.substring(0, sb));
                     else System.out.println(" (binary, little endian):\n" + indent + enc.substring(0, sb));
                     if (sz > sb) {System.out.println(indent + "... " + (sz-sb) + " bytes more binary not printed here ...");}
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

/*---------------------------------------------------------------------------*
*  Copyright (c) 2007        Jefferson Science Associates,                   *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    C.Timmer, 17-Dec-2007, Jefferson Lab                                    *
*                                                                            *
*    Authors: Carl Timmer                                                    *
*             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
*             Phone: (757) 269-5130             12000 Jefferson Ave.         *
*             Fax:   (757) 269-6248             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;

import java.util.concurrent.ConcurrentHashMap;
import java.util.Set;
import java.util.Map;


/**
 * <b>This class defines the compound payload interface to cMsg messages. In short,
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
 * Following is the text format of a complete compound payload ([nl] means newline, only 1 space between items).
 * Each payload consists of a number of items. The first line is the number of
 * items in the payload. That is followed by the text
 * representation of each item:<p></b>
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
 * including the newline at the end).</b>
 */
public class cMsgPayload {

    /** List of payload items. */
    ConcurrentHashMap<String, cMsgPayloadItem> items = new ConcurrentHashMap<String, cMsgPayloadItem>();

    /** Buffer to help build the text represenation of the payload to send over network. */
    StringBuilder buffer = new StringBuilder(4096);


    public cMsgPayload() { }

    /**
     * Clone this object.
     * @return a cMsgPayload object which is a copy of this object
     */
    public Object clone() {
        try {
            cMsgPayload result = (cMsgPayload) super.clone();
            // Ignore the buffer and clone the hashmap.
            result.buffer = new StringBuilder(4096);
            result.items  = new ConcurrentHashMap<String, cMsgPayloadItem>();
            for (Map.Entry<String, cMsgPayloadItem> entry : items.entrySet()) {
                result.items.put(entry.getKey(), (cMsgPayloadItem)entry.getValue().clone());
            }
            return result;
        }
        catch (CloneNotSupportedException e) {
            return null; // never invoked
        }
    }


    /**
     * Creates a complete copy of this object.
     * @return copy of this object.
     */
    public cMsgPayload copy() {
        return (cMsgPayload) this.clone();
    }

    /**
     * Gets a hashmap of all payload items.
     * @return a hashmap of all payload items.
     */
    public Map<String,cMsgPayloadItem> getItems() {
        return items;
    }

    /** Clears the payload of all items.  */
    public void clear() {
        items.clear();
    }

    public boolean add(cMsgPayloadItem item) {
         if (item == null) return false;
         items.put(item.name, item);
         return true;
     }

    public boolean remove(cMsgPayloadItem item) {
         return (items.remove(item.name) != null);
    }

    public boolean remove(String name) {
         return (items.remove(name) != null);
    }

    public cMsgPayloadItem get(String name) {
         return (items.get(name));
    }

    public Set<String> getNames() {
         return (items.keySet());
    }

    public int getNumberOfItems() {
         return (items.size());
    }


    /**
     * This method creates a string representation of the whole compound
     * payload and the hidden system fields (currently only the "text")
     * of the message as it gets sent over the network.
     *
     * @param cMsgText String containing the "text" field of a cMsg message
     *
     * @return resultant string if successful
     * @return null if no payload exists
     */
    synchronized public String getNetworkText(String cMsgText) {
        int msgLen = 0, count, totalLen = 0;

        count = items.size();
        if (count < 1) {
            return null;
        }

        /* find total length, first payload, then text field */
        for (cMsgPayloadItem item : items.values()) {
            totalLen += item.text.length();
        }

        if (cMsgText != null) {
            count++;

            /* length of text item minus header line */
            msgLen = cMsgPayloadItem.numDigits(cMsgText.length()) + cMsgText.length() + 2; /* 2 newlines */

            totalLen += 17 + /* 8 chars "cMsgText", 2 digit type, 1 digit count, 1 digit isSys?,  4 spaces, 1 newline*/
                        cMsgPayloadItem.numDigits(msgLen) + /* # of digits of length of what is to follow */
                        msgLen;
        }

        totalLen += cMsgPayloadItem.numDigits(count) + 1; /* send count & newline first */

        /* ensure buffer size, and clear it */
        if (buffer.capacity() < totalLen) buffer.ensureCapacity(totalLen + 1024);
        buffer.delete(0,buffer.capacity());

        /* first item is number of fields to come (count) & newline */
        buffer.append(count);
        buffer.append("\n");

        /* add message text if there is one */
        if (cMsgText != null) {
            buffer.append("cMsgText ");
            buffer.append(cMsgConstants.payloadStr);
            buffer.append(" 1 1 ");
            buffer.append(msgLen);
            buffer.append("\n");
            buffer.append(cMsgText.length());
            buffer.append("\n");
            buffer.append(cMsgText);
            buffer.append("\n");
        }

        /* add payload fields */
        for (cMsgPayloadItem item : items.values()) {
            buffer.append(item.text);
        }

        return buffer.toString();
    }

    /**
     * This method returns the length of a string representation of the whole compound
     * payload and the hidden system fields (currently only the "text")
     * of the message as it gets sent over the network.
     *
     * @param cMsgText String containing the "text" field of a cMsg message
     *
     * @return string length if successful
     * @return -1 if no payload exists
     */
    synchronized public int getNetworkTextLength(String cMsgText) {
        int msgLen = 0, count, totalLen = 0;

        count = items.size();
        if (count < 1) {
            return -1;
        }

        /* find total length, first payload, then text field */
        for (cMsgPayloadItem item : items.values()) {
            totalLen += item.text.length();
        }

        if (cMsgText != null) {
            count++;

            /* length of text item minus header line */
            msgLen = cMsgPayloadItem.numDigits(cMsgText.length()) + cMsgText.length() + 2; /* 2 newlines */

            totalLen += 17 + /* 8 chars "cMsgText", 2 digit type, 1 digit count, 1 digit isSys?,  4 spaces, 1 newline*/
                        cMsgPayloadItem.numDigits(msgLen) + /* # of digits of length of what is to follow */
                        msgLen;
        }

        totalLen += cMsgPayloadItem.numDigits(count) + 1; /* send count & newline first */

        return totalLen;
    }

    /**
     * This method creates a string of all the payload items concatonated.
     *
     * @return resultant string if successful
     * @return null if no payload exists
     */
    synchronized public String getItemsText() {
        int count, totalLen = 0;

        count = items.size();
        if (count < 1) {
            return null;
        }

        /* find total length, first payload, then text field */
        for (cMsgPayloadItem item : items.values()) {
            totalLen += item.text.length();
        }

        /* ensure buffer size, and clear it */
        if (buffer.capacity() < totalLen) buffer.ensureCapacity(totalLen + 1024);
        buffer.delete(0,buffer.capacity());

        /* concatenate payload fields */
        for (cMsgPayloadItem item : items.values()) {
            buffer.append(item.text);
        }

        return buffer.toString();
    }


}

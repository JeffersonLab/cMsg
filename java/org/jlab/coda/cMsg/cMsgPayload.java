package org.jlab.coda.cMsg;

import java.util.concurrent.ConcurrentHashMap;
import java.util.Set;
import java.util.Collection;

/**
 * 
 */
public class cMsgPayload {

    /** List of payload items. */
    private ConcurrentHashMap<String, cMsgPayloadItem> items = new ConcurrentHashMap<String, cMsgPayloadItem>();

    StringBuilder buffer = new StringBuilder(4096);


    public cMsgPayload() {     }


    /**
     * This routine checks to see if a name is already in use by an existing payloadItem.
     *
     * @param name name to check
     * @returns false if name does not exist
     * @returns true if name exists
     */
    boolean nameExists(String name) {
        return items.containsKey(name);
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

    public Collection<cMsgPayloadItem> getItems() {
         return (items.values());
    }

    public int getNumberOfItems() {
         return (items.size());
    }


    /**
     * This method creates a string representation of the whole compound
     * payload and the hidden system fields (currently only the "text")
     * of the message as it gets sent over the network.
     * If the dst argument is not NULL, this routine writes the string there.
     * If the dst argument is NULL, memory is allocated and the string placed in that.
     * In the latter case, the returned string (buf) must be freed by the user.
     *
     * @param cMsgText String containing the "text" field of a cMsg message
     *
     * @returns resultant string if successful
     * @returns null if no payload exists
     */
    synchronized String getNetworkText(String cMsgText) {
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
     * This method creates a string of all the payload items concatonated.
     *
     * @returns resultant string if successful
     * @returns null if no payload exists
     */
    synchronized String getItemsText() {
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

/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 3-Dec-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.common;

import java.io.*;
import java.util.Date;
import java.text.ParseException;
import java.math.BigInteger;
import javax.xml.parsers.* ;
import org.w3c.dom.*;
import org.xml.sax.*;
import org.jlab.coda.cMsg.cMsgMessage;
import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgPayloadItem;
import org.jlab.coda.cMsg.cMsgConstants;


/**
 * This class contains the full functionality of a message. It extends the class
 * that users have access to by defining setters and getters that the user has
 * no need of. This class is for use only by packages that are part of the cMsg
 * implementation. This whole class is really a private form of the cMsgMessage class.
 */
public class cMsgMessageFull extends cMsgMessage implements Serializable {

    /** Constructor. */
    public cMsgMessageFull() {
        super();
    }

    /**
     * Creates a deliverable message with blank fields so no NullPointerExceptions
     * are thrown when creating a message in a subdomain that is delivered to the
     * client.
     *
     * @return message with blank relevant string fields set
     */
    static public cMsgMessageFull createDeliverableMessage() {
        cMsgMessageFull msg = new cMsgMessageFull();
        msg.sender = "";
        msg.senderHost = "";
        msg.subject = "";
        msg.type = "";
        msg.payloadText = "";
        msg.text = "";
        return msg;
    }


    /**
     * Constructor using existing cMsgMessage type of message.
     * @param m regular message to create a full message from
     */
    public cMsgMessageFull(cMsgMessage m) {

        this.setVersion(m.getVersion());
        this.setDomain(m.getDomain());
        this.setSysMsgId(m.getSysMsgId());
        this.setInfo(m.getInfo());
        this.setPayloadText(m.getPayloadText());

        this.setSender(m.getSender());
        this.setSenderHost(m.getSenderHost());
        this.setSenderTime(m.getSenderTime());
        this.setSenderToken(m.getSenderToken());

        this.setUserInt(m.getUserInt());
        this.setUserTime(m.getUserTime());

        this.setReceiver(m.getReceiver());
        this.setReceiverHost(m.getReceiverHost());
        this.setReceiverTime(m.getReceiverTime());

        this.setSubject(m.getSubject());
        this.setType(m.getType());
        this.setText(m.getText());
    }

    /**
     * Constructor using XML string generated by cMsgMessage.toString().
     * @param XML XML string
     * @throws org.jlab.coda.cMsg.cMsgException if trouble parsing string
     */
    public cMsgMessageFull(String XML) throws cMsgException {

        if (XML == null) {
            cMsgException ce = new cMsgException("Null input string");
            ce.setReturnCode(1);
            throw ce;
        }

        DocumentBuilderFactory f = DocumentBuilderFactory.newInstance();
        f.setIgnoringComments(true);
        f.setCoalescing(true);
        f.setValidating(false);

        try {
            DocumentBuilder p = f.newDocumentBuilder();
            //Document d = p.parse(new StringBufferInputStream(XML));
            Document d = p.parse(new ByteArrayInputStream(XML.getBytes()));
            Element e = d.getDocumentElement();

            fillMsgFromElement(this, e);

        } catch (ParserConfigurationException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        } catch (SAXException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        } catch (IOException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        }
    }


    /**
     * Constructor reading file generated by writing cMsgMessage.toString() output.
     * @param file file containing XML description of message
     * @throws cMsgException if trouble reading file or parsing file contents
     */
    public cMsgMessageFull(File file) throws cMsgException {

        if (file == null) {
            cMsgException ce = new cMsgException("Null argument");
            ce.setReturnCode(1);
            throw ce;
        }

        DocumentBuilderFactory f = DocumentBuilderFactory.newInstance();
        f.setIgnoringComments(true);
        f.setCoalescing(true);
        f.setValidating(false);

        try {
            DocumentBuilder p = f.newDocumentBuilder();
            Document d = p.parse(file);
            Element e = d.getDocumentElement();

            fillMsgFromElement(this, e);

        } catch (ParserConfigurationException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        } catch (SAXException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        } catch (IOException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        }
    }


    /**
     * This method parses an XML string representing a cMsg message
     * (using a DOM parser) and turns it into a cMsg message.
     *
     * @param XML xml string representation of cMsg message
     * @return cMsgMessage object created from XML representation
     * @throws cMsgException problems parsing XML
     */
    static public cMsgMessage parseXml(String XML) throws cMsgException {
        cMsgMessageFull msg = new cMsgMessageFull();

        ByteArrayInputStream stream = new ByteArrayInputStream(XML.getBytes());

        DocumentBuilderFactory f = DocumentBuilderFactory.newInstance();
        f.setIgnoringComments(true);
        f.setCoalescing(true);
        f.setValidating(false);

        try {
            DocumentBuilder p = f.newDocumentBuilder();
            Document d = p.parse(new ByteArrayInputStream(XML.getBytes()));
            Element e = d.getDocumentElement();

            fillMsgFromElement(msg, e);

        } catch (ParserConfigurationException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        } catch (SAXException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        } catch (IOException ex) {
            ex.printStackTrace();
            cMsgException ce = new cMsgException(ex.toString());
            ce.setReturnCode(1);
            throw ce;
        }

        return msg;
    }


    /**
     * Debugging method to print out what is in a XML DOM node.
     * @param node node to print
     * @param indent string used to indent the XML being printed
     */
    static private void printNode(Node node, String indent) {
        switch(node.getNodeType()) {
            case Node.DOCUMENT_NODE:
                Document doc = (Document)node;
                printNode(doc.getDocumentElement(), "");
                break;
            case Node.ELEMENT_NODE:
                String name = node.getNodeName();
                System.out.print(indent + "<" + name);
                NamedNodeMap attributes = node.getAttributes();
                for (int i=0; i<attributes.getLength(); i++) {
                    Node current = attributes.item(i);
                    System.out.print(" " + current.getNodeName() +
                                     "=\"" + current.getNodeValue() + "\"");
                }
                System.out.println(">");

                // recurse on each child
                NodeList children = node.getChildNodes();
                if (children != null) {
                    for(int i=0; i<children.getLength(); i++) {
                        printNode(children.item(i), indent + "  ");
                    }
                }

                System.out.println(indent + "</" + name + ">");
                break;
            case Node.TEXT_NODE:
            case Node.CDATA_SECTION_NODE:
                System.out.println(node.getNodeValue());
        }
    }


    /**
     * This method takes a single string containing one or more numbers separated by
     * spaces or \n's and converts it into a string array whose elements each contain
     * a single number.
     *
     * @param s string containing multiple numbers
     * @param size amount of numbers in string
     * @return array whose elements each contain a single number
     */
    static private String[] splitArray(String s, int size) {
        String[] sArray = new String[size];
        s = s.trim().replace('\n', ' ');
        int endIndex, length;

        for (int i=0; i<size; i++) {
            length   = s.length();
            endIndex = s.indexOf(" ");
            if (endIndex < 0) {
                endIndex = s.indexOf("\n");
                if (endIndex < 0) endIndex = length;
            }
            sArray[i] = s.substring(0, endIndex);
            s = s.substring(endIndex, length).trim();
        }

        return sArray;
    }


    /**
     * This method fills a message from an XML DOM Element that represents a cMsg message.
     * @param msg the cMsg message in which to unmarshall the XML
     * @param e top element in an XML document representing a cMsg message
     */
    static private void fillMsgFromElement(cMsgMessageFull msg, Element e) {

        // time attributes
        try {
            msg.setUserTime(dateFormatter.parse(e.getAttribute("userTime")));
            msg.setSenderTime(dateFormatter.parse(e.getAttribute("senderTime")));
            msg.setReceiverTime(dateFormatter.parse(e.getAttribute("receiverTime")));
        }
        catch (ParseException ex) {}


        // int attributes
        String s = e.getAttribute("version");
        if (s.length() > 0) msg.setVersion(Integer.parseInt(s));

        s = e.getAttribute("userInt");
        if (s.length() > 0) msg.setUserInt(Integer.parseInt(s));

//        s = e.getAttribute("payloadCount");
//        if (s.length() > 0) payloadCount = Integer.parseInt(s);

        // boolean attributes
        s = e.getAttribute("getRequest");
        if (s.length() > 0)  msg.setGetRequest(Boolean.parseBoolean(s));

        s = e.getAttribute("nullGetResponse");
        if (s.length() > 0)  msg.setNullGetResponse(Boolean.parseBoolean(s));

        s = e.getAttribute("getResponse");
        if (s.length() > 0) msg.setGetResponse(Boolean.parseBoolean(s));

        // string attributes

//        s = e.getAttribute("name");
//        if (s.length() > 0) payloadItemName = s;

        s = e.getAttribute("domain");
        if (s.length() > 0 && !s.equals("(null)")) msg.setDomain(s);

        s = e.getAttribute("sender");
        if (s.length() > 0 && !s.equals("(null)")) msg.setSender(s);

        s = e.getAttribute("senderHost");
        if (s.length() > 0 && !s.equals("(null)")) msg.setSenderHost(s);

        s = e.getAttribute("receiver");
        if (s.length() > 0 && !s.equals("(null)")) msg.setReceiver(s);

        s = e.getAttribute("receiverHost");
        if (s.length() > 0 && !s.equals("(null)")) msg.setReceiverHost(s);

        s = e.getAttribute("subject");
        if (s.length() > 0 && !s.equals("(null)")) msg.setSubject(s);

        s = e.getAttribute("type");
        if (s.length() > 0 && !s.equals("(null)")) msg.setType(s);

        int numElements;

        // text element of cMsgMessage element
        NodeList nList = e.getElementsByTagName("text");
        if (nList != null) {
            numElements = nList.getLength();
            if (numElements > 0) {
                s = nList.item(0).getFirstChild().getNodeValue();
//System.out.println("Got text node, value = " + s);
                msg.setText(s);
            }
        }

        // binary element of cMsgMessage element
        nList = e.getElementsByTagName("binary");
        if (nList != null) {
            numElements = nList.getLength();
            if (numElements > 0) {
                Element el = (Element) nList.item(0);
                s = el.getFirstChild().getNodeValue();

                int itemBytes  = Integer.parseInt(el.getAttribute("nbytes"));
                int itemEndian = el.getAttribute("endian").equals("big") ?
                        cMsgConstants.endianBig : cMsgConstants.endianLittle;
                byte[] bytes = new byte[0];
                try {bytes = Base64.decodeToBytes(s, "US-ASCII");}
                catch (UnsupportedEncodingException ex) {/* never happen */}
                if (bytes.length != itemBytes) {
                    System.out.println("Reconstituted binary array is different size !!!");
                }
                msg.setByteArrayNoCopy(bytes);
                try { msg.setByteArrayEndian(itemEndian); }
                catch (cMsgException e1) {}
//System.out.println("Got msg binary node, val = \n" + s);
            }
        }

        // payload element of cMsgMessage element
        nList = e.getElementsByTagName("payload");
        if (nList != null) {
            numElements = nList.getLength();
            if (numElements > 0) {
                fillPayloadFromElement(msg, nList.item(0));
            }
        }

    }


    /**
     * This method fills a message payload from an XML DOM node that represents a payload.
     * @param msg the cMsg message in which to unmarshall the XML
     * @param node element in an XML document representing a cMsg message payload
     */
    static private void fillPayloadFromElement(cMsgMessageFull msg, Node node) {

        int i, numElements, itemCount;
        String itemValue, itemName, itemType;
        NodeList nList, aList;
        boolean debug = false;
        Element el;
        cMsgPayloadItem payloadItem=null;

        // each <payload> item may have many children
        nList = node.getChildNodes();
        if (nList == null) return;

        numElements = nList.getLength();
        i = numElements;

        while (i-- > 0) {
            if (nList.item(i).getNodeType() != Node.ELEMENT_NODE) continue;
            el = (Element) nList.item(i);
            itemType  = el.getNodeName();
            itemName  = el.getAttribute("name");
            // Note: not every node has a value, some have children which have values

            // Each type of item must be interpreted differently
            try {

                if (itemType.equals("string")) {
                    itemValue = el.getFirstChild().getNodeValue();
                    payloadItem = new cMsgPayloadItem(itemName, itemValue);
                    if (debug) System.out.println("Payload string \"" + itemType + "\", value = " + itemValue);
                }

                else if (itemType.equals("string_array")) {
                    if (debug) System.out.println("Payload string array \"" + itemType + "\"");
                    // itemCount number of strings to follow
                    itemCount = Integer.parseInt(el.getAttribute("count"));
                    int index = 0;
                    String[] sArray = new String[itemCount];
                    aList = el.getChildNodes();
                    if (aList == null) continue;
                    for (int j=0; j < aList.getLength(); j++) {
                        if (aList.item(j).getNodeType() != Node.ELEMENT_NODE) continue;
                        el = (Element) aList.item(j);
                        itemValue = el.getFirstChild().getNodeValue();
                        sArray[index++] = itemValue;
                        if (debug) System.out.println("  string = " + itemValue);
                    }
                    payloadItem = new cMsgPayloadItem(itemName, sArray);
                }

                else if ((itemType.startsWith("uint") ||
                          itemType.startsWith("int")) &&
                          itemType.endsWith("_array"))  {
                    int bits = Integer.parseInt(itemType.substring(itemType.indexOf("t")+1,
                                                                   itemType.indexOf("_")));
                    // promote to next type if unsigned
                    if (itemType.startsWith("u")) {
                        bits *= 2;
                    }

                    itemValue = el.getFirstChild().getNodeValue();
                    itemCount = Integer.parseInt(el.getAttribute("count"));
                    String[] sArray = splitArray(itemValue, itemCount);

                    if (debug) {
                        System.out.println("Payload int array \"" + itemType + "\", bits = " + bits);
                        for (int j=0; j<itemCount; j++) {
                            System.out.println("  int"+bits+"[" + j + "] = " + sArray[j]);
                        }
                    }

                    switch (bits) {
                        case 8:
                            byte[] b = new byte[itemCount];
                            for (int j=0; j<itemCount; j++) {
                                b[j] = Byte.parseByte(sArray[j]);
                            }
                            payloadItem = new cMsgPayloadItem(itemName, b);
                            break;
                        case 16:
                            short[] s = new short[itemCount];
                            for (int j=0; j<itemCount; j++) {
                                s[j] = Short.parseShort(sArray[j]);
                            }
                            payloadItem = new cMsgPayloadItem(itemName, s);
                            break;
                        case 32:
                            int[] ii = new int[itemCount];
                            for (int j=0; j<itemCount; j++) {
                                ii[j] = Integer.parseInt(sArray[j]);
                            }
                            payloadItem = new cMsgPayloadItem(itemName, ii);
                            break;
                        case 64:
                            long[] l = new long[itemCount];
                            for (int j=0; j<itemCount; j++) {
                                l[j] = Long.parseLong(sArray[j]);
                            }
                            payloadItem = new cMsgPayloadItem(itemName, l);
                            break;
                        case 128:  // unsigned 64 bit
                            BigInteger[] big = new BigInteger[itemCount];
                            for (int j=0; j<itemCount; j++) {
                                big[j] = new BigInteger(sArray[j]);
                            }
                            payloadItem = new cMsgPayloadItem(itemName, big);
                            break;
                        default:
                    }
                }

                else if (itemType.startsWith("uint") || itemType.startsWith("int")) {
                    int bits = Integer.parseInt(itemType.substring(itemType.indexOf("t")+1,
                                                                   itemType.length()));
                    // promote to next type if unsigned
                    if (itemType.startsWith("u")) {
                        bits *= 2;
                    }

                    itemValue = el.getFirstChild().getNodeValue().trim();
                    switch (bits) {
                        case 8:
                            byte b = Byte.parseByte(itemValue);
                            payloadItem = new cMsgPayloadItem(itemName, b);
                            break;
                        case 16:
                            short s = Short.parseShort(itemValue);
                            payloadItem = new cMsgPayloadItem(itemName, s);
                            break;
                        case 32:
                            int ii = Integer.parseInt(itemValue);
                            payloadItem = new cMsgPayloadItem(itemName, ii);
                            break;
                        case 64:
                            long l = Long.parseLong(itemValue);
                            payloadItem = new cMsgPayloadItem(itemName, l);
                            break;
                        case 128:  // unsigned 64 bit
                            BigInteger big = new BigInteger(itemValue);
                            payloadItem = new cMsgPayloadItem(itemName, big);
                            break;
                        default:
                    }

                    if (debug) System.out.println("Payload int \"" + itemType + "\", bits = " + bits +
                                                  ", val = " + itemValue);
                }

                else if (itemType.equals("double")) {
                    itemValue = el.getFirstChild().getNodeValue();
                    double d = Double.parseDouble(itemValue);
                    payloadItem = new cMsgPayloadItem(itemName, d);
                    if (debug) System.out.println("Payload double \"" + itemType + "\", value = " + itemValue);
                }

                else if (itemType.equals("float")) {
                    itemValue = el.getFirstChild().getNodeValue();
                     float f = Float.parseFloat(itemValue);
                     payloadItem = new cMsgPayloadItem(itemName, f);
                    if (debug) System.out.println("Payload float \"" + itemType + "\", value = " + itemValue);
                }

                else if (itemType.equals("double_array")) {
                    itemValue = el.getFirstChild().getNodeValue();
                    itemCount = Integer.parseInt(el.getAttribute("count"));
                    String[] sArray = splitArray(itemValue, itemCount);

                    if (debug) {
                        System.out.println("Payload double array \"" + itemType + "\"");
                        for (int j=0; j<itemCount; j++) {
                            System.out.println("  double[" + j + "] = " + sArray[j]);
                        }
                    }

                    double[] d = new double[itemCount];
                    for (int j=0; j<itemCount; j++) {
                        d[j] = Double.parseDouble(sArray[j]);
                    }
                    payloadItem = new cMsgPayloadItem(itemName, d);
                }

                else if (itemType.equals("float_array")) {
                    itemValue = el.getFirstChild().getNodeValue();
                    itemCount = Integer.parseInt(el.getAttribute("count"));
                    String[] sArray = splitArray(itemValue, itemCount);

                    if (debug) {
                        System.out.println("Payload float array \"" + itemType + "\"");
                        for (int j=0; j<itemCount; j++) {
                            System.out.println("  float[" + j + "] = " + sArray[j]);
                        }
                    }

                    float[] f = new float[itemCount];
                    for (int j=0; j<itemCount; j++) {
                        f[j] = Float.parseFloat(sArray[j]);
                    }
                    payloadItem = new cMsgPayloadItem(itemName, f);
                }

                else if (itemType.equals("binary")) {
                    itemValue = el.getFirstChild().getNodeValue();
                    int itemBytes  = Integer.parseInt(el.getAttribute("nbytes"));
                    int itemEndian = el.getAttribute("endian").equals("big") ?
                            cMsgConstants.endianBig : cMsgConstants.endianLittle;
                    byte[] bytes = new byte[0];
                    try {bytes = Base64.decodeToBytes(itemValue, "US-ASCII");}
                    catch (UnsupportedEncodingException e) {/* never happen */}
                    if (bytes.length != itemBytes) {
                        System.out.println("Reconstituted binary array is different size !!!");
                    }
                    payloadItem = new cMsgPayloadItem(itemName, bytes, itemEndian);
                    if (debug) System.out.println("Payload binary node \"" + itemType + "\", value = \n" + itemValue);
                }

                else if (itemType.equals("binary_array")) {
                    // itemCount = number of byte arrays to follow
                    itemCount = Integer.parseInt(el.getAttribute("count"));
                    int itemBytes, ind=0;
                    int[] endians = new int[itemCount];
                    byte[][] bArray = new byte[itemCount][];
                    aList = el.getChildNodes();
                    if (aList == null) continue;
                    if (debug) System.out.println("Payload binary_array node \"" + itemType + "\"");
                    for (int j=0; j < aList.getLength(); j++) {
                        if (aList.item(j).getNodeType() != Node.ELEMENT_NODE) continue;
                        el = (Element) aList.item(j);
                        itemValue    = el.getFirstChild().getNodeValue();
                        itemBytes    = Integer.parseInt(el.getAttribute("nbytes"));
                        endians[ind] = el.getAttribute("endian").equals("big") ?
                                       cMsgConstants.endianBig : cMsgConstants.endianLittle;
                        try {bArray[ind] = Base64.decodeToBytes(itemValue, "US-ASCII");}
                        catch (UnsupportedEncodingException e) {/* never happen */}
                        if (bArray[ind].length != itemBytes) {
                            System.out.println("Reconstituted binary array is different size !!!");
                        }
                        if (true) System.out.println("  bin[" + ind + "] = " + itemValue);
                        ind++;
                    }
                    payloadItem = new cMsgPayloadItem(itemName, bArray, endians);
                }

                else if (itemType.equals("cMsgMessage")) {
                    cMsgMessageFull newMsg = new cMsgMessageFull();
                    fillMsgFromElement(newMsg, el);
                    payloadItem = new cMsgPayloadItem(itemName, newMsg);
                    if (debug) System.out.println("Payload msg node \"" + itemType + "\"");
                }

                else if (itemType.equals("cMsgMessage_array")) {
                    // itemCount = number of messages to follow
                    itemCount = Integer.parseInt(el.getAttribute("count"));
                    cMsgMessageFull[] newMsgs = new cMsgMessageFull[itemCount];
                    int index = 0;
                    aList = el.getChildNodes();
                    if (aList == null) continue;

                    for (int j=0; j < aList.getLength(); j++) {
                        if (aList.item(j).getNodeType() != Node.ELEMENT_NODE) continue;
                        Element el2 = (Element) aList.item(j);
                        cMsgMessageFull newMsg = new cMsgMessageFull();
                        fillMsgFromElement(newMsg, el2);
                        newMsgs[index++] = newMsg;
                    }

                    payloadItem = new cMsgPayloadItem(itemName, newMsgs);
                    if (debug) System.out.println("Payload msg array node \"" + itemType + "\"");
                }

                else {
                    if (debug) System.out.println("Payload unknown node \"" + itemType + "\"");
                    continue;
                }

                // add parsed item to payload
                msg.addPayloadItem(payloadItem);
            }
            catch (cMsgException e) {
                if (debug) System.out.println("Payload node has bad name \"" + itemName + "\"");
                // ignore bad names
            }
        }
    }


    /**
     * Clone this object.
     * @return a cMsgMessageFull object which is a copy of this message
     */
    public Object clone() {
            return super.clone();
    }

    /**
     * Creates a complete copy of this message.
     * @return copy of this message.
     */
    public cMsgMessageFull copy() {
        return (cMsgMessageFull) this.clone();
    }


    /**
     * Creates a proper response message to this message which was sent by a client calling
     * sendAndGet.
     *
     * @return message with the response fields properly set.
     * @throws cMsgException if this message was not sent from a "sendAndGet" method call
     */
    public cMsgMessageFull response() throws cMsgException {
        // If this message was not sent from a "sendAndGet" method call,
        // a proper response is not possible, since the sysMsgId
        // and senderToken fields will not have been properly set.
        if (!isGetRequest()) {
            throw new cMsgException("this message not sent by client calling sendAndGet");
        }
        cMsgMessageFull msg = new cMsgMessageFull();
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
     * @throws cMsgException if this message was not sent from a "sendAndGet" method call
     */
    public cMsgMessageFull nullResponse() throws cMsgException {
        // If this message was not sent from a "get" method call,
        // a proper response is not possible, since the sysMsgId
        // and senderToken fields will not have been properly set.
        if (!isGetRequest()) {
            throw new cMsgException("this message not sent by client calling sendAndGet");
        }
        cMsgMessageFull msg = new cMsgMessageFull();
        msg.subject = "dummy";
        msg.type = "dummy";
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
    public void makeResponse(cMsgMessageFull msg) {
        this.sysMsgId    = msg.getSysMsgId();
        this.senderToken = msg.getSenderToken();
        this.info = isGetResponse;
    }


    /**
     * Converts existing message to null response of supplied message.
     *
     * @param msg message this message will be made a null response to
     */
    public void makeNullResponse(cMsgMessageFull msg) {
        this.sysMsgId    = msg.getSysMsgId();
        this.senderToken = msg.getSenderToken();
        this.info = isGetResponse | isNullGetResponse;
    }


    // general quantities


    /**
     * Set system intVal of message. Used by the system in doing sendAndGet.
     * @param sysMsgId system intVal of message.
     */
    public void setSysMsgId(int sysMsgId) {this.sysMsgId = sysMsgId;}


    /**
     * Set domain this message exists in.
     * @param domain domain this message exists in.
     */
    public void setDomain(String domain) {this.domain = domain;}


    /**
     * Specify whether this message is a "sendAndGet" request.
     * @param getRequest true if this message is a "sendAndGet" request
     */
    public void setGetRequest(boolean getRequest) {
        info = getRequest ? info|isGetRequest : info & ~isGetRequest;
    }


    /**
     * Set the info member.
     * @param info value of info member
     */
    public void setInfo(int info) {
        this.info = info;
    }


    /**
     * Sets the version number of this message. The version number must be the same as the
     * version number of the cMsg package - given by {@link org.jlab.coda.cMsg.cMsgConstants#version}.
     * @param version version number of message
     */
    public void setVersion(int version) {
        if (version < 0) version = 0;
        this.version = version;
    }


    // sender quantities


    /**
     * Set message sender.
     * @param sender message sender.
     */
    public void setSender(String sender) {this.sender = sender;}


    /**
     * Set message sender's host computer.
     * @param senderHost message sender's host computer.
     */
    public void setSenderHost(String senderHost) {this.senderHost = senderHost;}


    /**
     * Set time message was sent.
     *
     * @param time time message sent.
     */
    public void setSenderTime(Date time) {
        this.senderTime = time.getTime();
    }


     /**
     * Set sender's token. Used by the system in doing sendAndGet.
     * @param senderToken sender's token.
     */
    public void setSenderToken(int senderToken) {this.senderToken = senderToken;}


    // receiver quantities


    /**
     * Set message receiver.
     * @param receiver message receiver.
     */
    public void setReceiver(String receiver) {this.receiver = receiver;}


    /**
     * Set message receiver's host computer.
     * @param receiverHost message receiver's host computer.
     */
    public void setReceiverHost(String receiverHost) {this.receiverHost = receiverHost;}


    /**
      * Set time message was receivered.
      * @param time time message received.
      */
    public void setReceiverTime(Date time) {this.receiverTime = time.getTime();}



    // context quantities


    /**
     * Sets the object containing information about the context of the
     * callback receiving this message.
     * 
     * @param context object containing information about the context of the
     *                callback receiving this message
     */
    public void setContext(cMsgMessageContextInterface context) {
        this.context = context;
    }


    // payload quantities


    /**
     * Sets the message to NOT record sender history if arg is true,
     * even if the history length max is not exceeded.
     * @param noHistoryAdditions if true the message does NOT record sender history
     */
    public void setNoHistoryAdditions(boolean noHistoryAdditions) {
        this.noHistoryAdditions = noHistoryAdditions;
    }

    /**
     * Sets the String representation of the compound payload of this message.
     * @param payloadText payloadText of this message.
     */
    public void setPayloadText(String payloadText) {this.payloadText = payloadText;}

    
    /**
     * {@inheritDoc}<p/>
     * This method makes the protected method {@link cMsgMessage#isNullGetServerResponse}
     * available to others.
     * @return {@inheritDoc}
     */
    public boolean isNullGetServerResponse() {
        return ((info & nullGetServerResponse) == nullGetServerResponse);
    }


    /**
     * {@inheritDoc}<p/>
     * This method makes the protected method {@link cMsgMessage#setNullGetServerResponse}
     * available to others.
     * @return {@inheritDoc}
     */
    public void setNullGetServerResponse(boolean ngsr) {
        super.setNullGetServerResponse(ngsr);
    }


    /**
     * {@inheritDoc}<p/>
     * This method makes the protected method {@link cMsgMessage#isExpandedPayload}
     * available to others.
     * @return {@inheritDoc}
     */
    @Override
    public boolean isExpandedPayload() {
        return super.isExpandedPayload();
    }


    /**
     * {@inheritDoc}
     * @param ep {@inheritDoc}<p/>
     * This method makes the protected method {@link cMsgMessage#setExpandedPayload}
     * available to others.
     */
    @Override
    public void setExpandedPayload(boolean ep) { super.setExpandedPayload(ep); }


    /**
     * {@inheritDoc}<p/>
     * This method makes the protected method {@link cMsgMessage#expandPayload}
     * available to others.
     */
    @Override
    public void expandPayload() {
        super.expandPayload();
    }


    /**
     * If this message is expanded (has items in its payload hashmap),
     * then unexpand or compress the payload by removing all payload hashmap items.
     */
    public void compressPayload() {
        if (!isExpandedPayload() || items.size() < 1) {
            setExpandedPayload(false);
            return;
        }
        items.clear();
        setExpandedPayload(false);
    }


    /**
     * {@inheritDoc}<p/>
     * This method makes the protected method {@link cMsgMessage#setFieldsFromText}
     * available to others.
     *
     * @param text {@inheritDoc}
     * @param flag {@inheritDoc}
     * @return {@inheritDoc}
     * @throws {@inheritDoc}
     */
    @Override
    public int setFieldsFromText(String text, int flag) throws cMsgException {
        return super.setFieldsFromText(text,flag);
    }
}

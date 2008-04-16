/*----------------------------------------------------------------------------*
 *  Copyright (c) 2008        Jefferson Science Associates,                   *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *     C. Timmer, 1-apr-2008                                                  *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.subdomains;

import org.jlab.coda.cMsg.*;

import org.jlab.coda.et.*;
import java.util.regex.*;
import java.util.LinkedList;
import java.lang.*;
import java.io.IOException;


/**
 * cMsg subdomain handler for opening and putting events into an ET system.
 * WARNING: This class may need some thread-safety measures added (Timmer).
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class Et extends cMsgSubdomainAdapter {
    /** UDL remainder for this subdomain handler. */
    private String myUDLRemainder;

    /** Name of the ET system file. */
    private String etFile;

    /** Method by which to open the ET system. */
    private int openMethod;

    /** Broadcast to this port to open ET system. */
    private int broadcastPort;

    /** Multicast to this port to open ET system. */
    private int multicastPort;

    /** Directly connect to this port to open ET system. */
    private int serverPort;

    /** List of all multicast addresses (dotted decimal format) as strings. */
    private LinkedList<String> multicastAddrs = new LinkedList<String>();

    /** Directly connect to this port to open ET system. */
    private String host;

    /** Number of new events to request from the ET system at one time. */
    private int chunkSize = 1;

    /** Array of new events from ET system. */
    private Event[] events;

    /** Events are put back into the ET system one at a time but must be in an array. */
    private Event[] putEvent = new Event[1];

    /** Number of new events left before we need to call etSystem.newEvents() again. */
    private int eventsLeft = 0;

    /** ET system handling object. */
    private SystemUse etSystem;

    /** Attachment to GrandCentral station. */
    private Attachment gcAttachment;

    /** Are we shut down right now? */
    private boolean shutDown;



    /**
     * Method to tell if the "send" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSendRequest}
     * method.
     *
     * @return true
     */
    public boolean hasSend() {
        return true;
    };



    /**
     * Method to tell if the "syncSend" cMsg API function is implemented
     * by this interface implementation in the {@link #handleSyncSendRequest}
     * method.
     *
     * @return true
     */
    public boolean hasSyncSend() {
        return true;
    };


    /**
     * Method to give the subdomain handler the appropriate part
     * of the UDL the client used to talk to the domain server.
     *
     * @param udlRemainder last part of the UDL appropriate to the subdomain handler
     * @throws org.jlab.coda.cMsg.cMsgException
     */
    public void setUDLRemainder(String udlRemainder) throws cMsgException {
        myUDLRemainder=udlRemainder;
        boolean debug = false;

        if (udlRemainder == null) {
            throw new cMsgException("invalid UDL");
        }

        /* Et subdomain UDL (remainder) is of the form:
         *        EtFileName?open=<openMethod>&port=<port>&host=<host>&multi=<multicastAddr>&chunk=<chunk>
         *
         * Remember that for this domain:
         * 1) EtFileName is the name of the ET system file
         * 2) openMethod is the manner of ET system discovery with valid values being:
         *    a) direct, b) broadcast, c)multicast, or d) broadAndMulticast
         * 3) port and host are specified if openMethod = direct
         * 4) any number of multi's may be given - each being a multicast address in dotted-decimal form.
         *    This is used when openMethod = multicast or broadAndmulticast
         * 5) chunk specifies the number of new events retrieved from the ET system with one call
         */

        Pattern pattern = Pattern.compile("([\\w\\.\\-/]+)(.*)");
        Matcher matcher = pattern.matcher(udlRemainder);

        String s, remainder, mAddr;
        openMethod = Constants.broadcast;

        if (matcher.find()) {
            // ET system file
            etFile = matcher.group(1);
            // remainder
            remainder = matcher.group(2);

            if (debug) {
                System.out.println("\nparseUDL: " +
                                   "\n  ET file   = " + etFile +
                                   "\n  remainder = " + remainder);
            }
        }
        else {
            throw new cMsgException("invalid UDL");
        }

        // if ET system file not given ...
        if (etFile == null) {
            throw new cMsgException("parseUDL: no ET system file specified");
        }

        // if no remaining UDL to parse, return
        if (remainder == null) {
            return;
        }

        // look for ?chunk=value or &chunk=value
        pattern = Pattern.compile("[\\?&]chunk=([0-9]+)");
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            int chunk = Integer.parseInt(matcher.group(1));
            if (chunk > 0 && chunk < 1001) chunkSize = chunk;
//System.out.println("chunk size = " + chunk);
        }
        
        openMethod = Constants.broadcast;

        // look for ?open=value or &open=value
        pattern = Pattern.compile("[\\?&]open=([\\w]+)");
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            s = matcher.group(1);
            if (s.equalsIgnoreCase("direct")) {
                openMethod = Constants.direct;
            }
            else if(s.equalsIgnoreCase("broadcast")) {
                openMethod = Constants.broadcast;
            }
            else if(s.equalsIgnoreCase("multicast")) {
                openMethod = Constants.multicast;
            }
            else if(s.equalsIgnoreCase("broadAndMulticast")) {
                openMethod = Constants.broadAndMulticast;
            }
//System.out.println("parsed open = " + s);
        }

        serverPort    = Constants.serverPort;
        broadcastPort = Constants.broadcastPort;
        multicastPort = Constants.multicastPort;

        // look for ?port=value or &port=value
        pattern = Pattern.compile("[\\?&]port=([0-9]+)");
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            int port;
            try {
                port = Integer.parseInt(matcher.group(1));
                if (port > 1024 && port < 65536) {
                    if (openMethod == Constants.direct) {
                        serverPort = port;
                    }
                    else if (openMethod == Constants.broadcast) {
                        broadcastPort = port;
                    }
                    else if (openMethod == Constants.multicast) {
                        multicastPort = port;
                    }
                    else if (openMethod == Constants.broadAndMulticast) {
                        broadcastPort = port;
                    }
                }
                else {
                    cMsgException ex = new cMsgException("port number out of range");
                    ex.setReturnCode(cMsgConstants.errorOutOfRange);
                    throw ex;
                }
            }
            catch (NumberFormatException e) {
                cMsgException ex = new cMsgException("bad port number");
                ex.setReturnCode(cMsgConstants.errorBadFormat);
                throw ex;
            }
//System.out.println("port = " + port);
        }

        // If we're making a direct connection ...
        host = Constants.hostAnywhere;
        if (openMethod == Constants.direct) {
            // look for ?host=value or &host=value
            pattern = Pattern.compile("[\\?&]host=((?:[a-zA-Z]+[\\w\\.\\-]*)|(?:(?:[\\d]{1,3}\\.){3}[\\d]{1,3}))");
            matcher = pattern.matcher(remainder);
            if (matcher.find()) {
                host = matcher.group(1);
            }
            else {
                host = Constants.hostLocal;
            }
//System.out.println("host = " + host);
        }
        // If we're making a multicast connection ...
        else if (openMethod == Constants.multicast || openMethod == Constants.broadAndMulticast) {
            // look for ?multi=value or &multi=value
            pattern = Pattern.compile("[\\?&]multi=((?:[\\d]{1,3}\\.){3}[\\d]{1,3})");
            matcher = pattern.matcher(remainder);
            while (matcher.find()) {
                mAddr = matcher.group(1);
                multicastAddrs.add(mAddr);
//System.out.println("multicast addr = " + mAddr);
            }
        }

    }


    /**
     * Method to register domain client.
     *
     * @param info information about client
     * @throws cMsgException upon error
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {
        //System.out.println("Registering client");

        // open ET system
        SystemOpenConfig config = null;

        // create ET system object
        try {
            // how do we open the ET system?
            if (openMethod == Constants.direct) {
                config = new SystemOpenConfig(etFile, host, serverPort);
            }
            else if (openMethod == Constants.broadcast) {
                config = new SystemOpenConfig(etFile, broadcastPort, host);
            }
            else if (openMethod == Constants.multicast) {
                config = new SystemOpenConfig(etFile, host, multicastAddrs,
                                              multicastPort, Constants.multicastTTL);
            }
            else if (openMethod == Constants.broadAndMulticast) {
                config = new SystemOpenConfig (etFile, host, true,  multicastAddrs,
                                               openMethod, 0, broadcastPort, multicastPort,
                                               Constants.multicastTTL, Constants.policyFirst);
            }

            // open the ET system
            etSystem = new SystemUse(config, Constants.debugInfo);

            // get GRAND_CENTRAL station object
            Station gc = etSystem.stationNameToObject("GRAND_CENTRAL");

            // attach to grandcentral
            gcAttachment = etSystem.attach(gc);
        }
        catch (IOException e) {
            e.printStackTrace();
            cMsgException ex = new cMsgException("IO error");
            ex.setReturnCode(cMsgConstants.errorNetwork);
            throw ex;
        }
        catch (EtException e) {
            e.printStackTrace();
            cMsgException ex = new cMsgException("bad argument value");
            ex.setReturnCode(cMsgConstants.errorBadArgument);
            throw ex;
        }
        catch (EtTooManyException e) {
            e.printStackTrace();
            cMsgException ex = new cMsgException("too many valid responses");
            ex.setReturnCode(cMsgConstants.errorLimitExceeded);
            throw ex;
        }
    }


    /**
     * Executes sql insert or update statement from message payload.
     *
     * @param msg message from sender.
     * @throws cMsgException
     */
    synchronized public void handleSendRequest(cMsgMessageFull msg) throws cMsgException {
        if (shutDown) return;
        
        try {
            if (eventsLeft < 1) {
                //System.out.println("Get new events from ET system");
                int size = (int) etSystem.getEventSize();
                if (msg.getByteArrayLength() > size) size = msg.getByteArrayLength();
                events = etSystem.newEvents(gcAttachment, Constants.sleep, 0, chunkSize, size);
                eventsLeft = events.length;
            }

            int index = events.length - eventsLeft;
            //System.out.println("Put message data into new event[" + index + "] (without copy)");
            events[index].setData(msg.getByteArray());
            putEvent[0] = events[index];
            eventsLeft--;

            //System.out.println("Putting new event into ET system");
            etSystem.putEvents(gcAttachment, putEvent);
        }
        catch (IOException e) {
            //e.printStackTrace();
        }
        catch (EtException e) {
            //e.printStackTrace();
        }
        catch (EtEmptyException e) {
            // never happen
        }
        catch (EtBusyException e) {
            // never happen
        }
        catch (EtTimeoutException e) {
            // never happen
        }
        catch (EtWakeUpException e) {
            //e.printStackTrace();
        }


        //try { Thread.sleep(500); }
        //catch (InterruptedException e) { }
    }


    /**
     * Method to handle message sent by domain client in synchronous mode.
     * It requries an integer response from the subdomain handler.
     *
     * @param msg message from sender
     * @return response from subdomain handler
     * @throws cMsgException
     */
    public int handleSyncSendRequest(cMsgMessageFull msg) throws cMsgException {
        handleSendRequest(msg);
        return (0);
    }


    /**
     * Method to handle a client shutdown.
     *
     * @throws cMsgException
     */
    synchronized public void handleClientShutdown() throws cMsgException {
        //System.out.println("Shutting down client");
        // get rid of unused events
        if (eventsLeft > 0) {
            Event[] evs = new Event[eventsLeft];
            int index = events.length - eventsLeft;
            try {
                etSystem.dumpEvents(gcAttachment, evs, index, eventsLeft);
            }
            catch (Exception e) { }
        }
        etSystem.close();
        shutDown = true;
    }


}

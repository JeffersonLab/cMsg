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
import org.jlab.coda.cMsg.common.cMsgSubdomainAdapter;
import org.jlab.coda.cMsg.common.cMsgClientInfo;
import org.jlab.coda.cMsg.common.cMsgMessageFull;

import org.jlab.coda.et.*;
import org.jlab.coda.et.enums.Mode;
import org.jlab.coda.et.exception.*;

import java.util.ArrayList;
import java.util.regex.*;
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

    /** Broad/Multicast to this port to open ET system. */
    private int udpPort;

    /** Directly connect to this port to open ET system. */
    private int serverPort;

    /** List of all multicast addresses (dotted decimal format) as strings. */
    private ArrayList<String> multicastAddrs = new ArrayList<String>();

    /** Directly connect to this port to open ET system. */
    private String host;

    /** Number of new events to request from the ET system at one time. */
    private int chunkSize = 1;

    /** Array of new events from ET system. */
    private EtEvent[] events;

    /** Events are put back into the ET system one at a time but must be in an array. */
    private EtEvent[] putEvent = new EtEvent[1];

    /** Number of new events left before we need to call etSystem.newEvents() again. */
    private int eventsLeft = 0;

    /** ET system handling object. */
    private EtSystem etSystem;

    /** Attachment to GrandCentral station. */
    private EtAttachment gcAttachment;

    /** Are we shut down right now? */
    private boolean shutDown;



    /**
     * {@inheritDoc}
     *
     * @return true
     */
    public boolean hasSend() {
        return true;
    };



    /**
     * {@inheritDoc}
     *
     * @return true
     */
    public boolean hasSyncSend() {
        return true;
    };


    /**
     * Parse arg to get ET system file, method to open ET system, chunk size.
     *
     * @param udlRemainder {@inheritDoc}
     * @throws cMsgException if invalid arg
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
        openMethod = EtConstants.broadcast;

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
        
        openMethod = EtConstants.broadcast;

        // look for ?open=value or &open=value
        pattern = Pattern.compile("[\\?&]open=([\\w]+)");
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            s = matcher.group(1);
            if (s.equalsIgnoreCase("direct")) {
                openMethod = EtConstants.direct;
            }
            else if(s.equalsIgnoreCase("broadcast")) {
                openMethod = EtConstants.broadcast;
            }
            else if(s.equalsIgnoreCase("multicast")) {
                openMethod = EtConstants.multicast;
            }
            else if(s.equalsIgnoreCase("broadAndMulticast")) {
                openMethod = EtConstants.broadAndMulticast;
            }
//System.out.println("parsed open = " + s);
        }

        serverPort = EtConstants.serverPort;
        udpPort = EtConstants.udpPort;

        // look for ?port=value or &port=value
        pattern = Pattern.compile("[\\?&]port=([0-9]+)");
        matcher = pattern.matcher(remainder);
        if (matcher.find()) {
            int port;
            try {
                port = Integer.parseInt(matcher.group(1));
                if (port > 1024 && port < 65536) {
                    if (openMethod == EtConstants.direct) {
                        serverPort = port;
                    }
                    else {
                        udpPort = port;
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
        host = EtConstants.hostAnywhere;
        if (openMethod == EtConstants.direct) {
            // look for ?host=value or &host=value
            pattern = Pattern.compile("[\\?&]host=((?:[a-zA-Z]+[\\w\\.\\-]*)|(?:(?:[\\d]{1,3}\\.){3}[\\d]{1,3}))");
            matcher = pattern.matcher(remainder);
            if (matcher.find()) {
                host = matcher.group(1);
            }
            else {
                host = EtConstants.hostLocal;
            }
//System.out.println("host = " + host);
        }
        // If we're making a multicast connection ...
        else if (openMethod == EtConstants.multicast || openMethod == EtConstants.broadAndMulticast) {
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
     * Open ET system and attach to GrandCentral station.
     *
     * @param info {@inheritDoc}
     * @throws cMsgException upon IO error, bad arg, too many valid responses
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {
        //System.out.println("Registering client");

        // open ET system
        EtSystemOpenConfig config = null;

        // create ET system object
        try {
            // how do we open the ET system?
            if (openMethod == EtConstants.direct) {
                config = new EtSystemOpenConfig(etFile, host, serverPort);
            }
            else if (openMethod == EtConstants.broadcast) {
                config = new EtSystemOpenConfig(etFile, udpPort, host);
            }
            else if (openMethod == EtConstants.multicast) {
                config = new EtSystemOpenConfig(etFile, host, multicastAddrs,
                                                udpPort, EtConstants.multicastTTL);
            }
            else if (openMethod == EtConstants.broadAndMulticast) {
                // ET system will automatically find subnet addresses
                config = new EtSystemOpenConfig(etFile, host,
                                 null, multicastAddrs,
                                 false, openMethod, 0, udpPort,
                                 EtConstants.multicastTTL, EtConstants.policyFirst);
            }

            // open the ET system
            etSystem = new EtSystem(config, EtConstants.debugInfo);

            // get GRAND_CENTRAL station object
            EtStation gc = etSystem.stationNameToObject("GRAND_CENTRAL");

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
        catch (EtDeadException e) {
            e.printStackTrace();
            cMsgException ex = new cMsgException("ET system dead");
            ex.setReturnCode(cMsgConstants.errorLostConnection);
            throw ex;
        }
        catch (EtClosedException e) {
            e.printStackTrace();
            cMsgException ex = new cMsgException("etSystem object closed");
            ex.setReturnCode(cMsgConstants.errorLostConnection);
            throw ex;
        }
    }


    /**
     * If there are no events to work with, get chunk number of empty events from
     * the ET system. Take the binary array of the message argument, copy it into
     * a single event, and put that event back into the ET system.
     *
     * @param msg {@inheritDoc}
     * @throws cMsgException if errors talking to ET system
     */
    synchronized public void handleSendRequest(cMsgMessageFull msg) throws cMsgException {
        if (shutDown) return;
        
        try {
            if (eventsLeft < 1) {
                //System.out.println("Get new events from ET system");
                int size = (int) etSystem.getEventSize();
                if (msg.getByteArrayLength() > size) size = msg.getByteArrayLength();
                events = etSystem.newEvents(gcAttachment, Mode.SLEEP, 0, chunkSize, size);
                eventsLeft = events.length;
            }

            int index = events.length - eventsLeft;
            //System.out.println("Put message data into new event[" + index + "] (without copy)");
            ((EtEventImpl)events[index]).setData(msg.getByteArray());
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
        catch (EtDeadException e) {
            //e.printStackTrace();
        }
        catch (EtClosedException e) {
            //e.printStackTrace();
        }


        //try { Thread.sleep(500); }
        //catch (InterruptedException e) { }
    }


    /**
     * If there are no events to work with, get chunk number of empty events from
     * the ET system. Take the binary array of the message argument, copy it into
     * a single event, and put that event back into the ET system.
     *
     * @param msg {@inheritDoc}
     * @return 0
     * @throws cMsgException if errors talking to ET system
     */
    public int handleSyncSendRequest(cMsgMessageFull msg) throws cMsgException {
        handleSendRequest(msg);
        return (0);
    }


    /**
     * Dump leftover events, then close the ET system.
     *
     * @throws cMsgException never
     */
    synchronized public void handleClientShutdown() throws cMsgException {
        //System.out.println("Shutting down client");
        // get rid of unused events
        if (eventsLeft > 0) {
            EtEvent[] evs = new EtEvent[eventsLeft];
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

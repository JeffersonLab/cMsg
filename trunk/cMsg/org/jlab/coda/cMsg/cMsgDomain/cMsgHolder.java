/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 22-Oct-2004, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain;

import org.jlab.coda.cMsg.cMsgMessageFull;

/**
 * This class is used to help in implementing a client's "subscribeAndGet" method.
 * An object of this class stores a msg from the server to the method's caller and
 * is used to synchronize/wait/notify on. It also indicates whether the call timed
 * out or not.
 *
 * This class is also used to implement the cMsg domain server by storing an incoming message
 * along with subject, type, id, and request (or client, server, flag for a shutdown)
 * for later action by a thread from the thread pool.
 */
public class cMsgHolder {
    /** Message object. */
    public cMsgMessageFull message;

    /** Has the "subscribeAndGet" or "sendAndGet" call timed out? */
    public boolean timedOut = true;

    /** In a shutdownClients or shutdownServers call, include self or own server? */
    public boolean include;

    /** Subject. */
    public String subject;

    /** Type. */
    public String type;

    /** Namespace. */
    public String namespace;

    /** Store client(s) OR server(s) to shutdown. */
    public String client;

    /** Delay (milliseconds) in cloud locking and client registration. */
    public int delay;

    /** In shutdown call, do we want to shut ourselves down or not. */
    public int flag;

    /** Request id. */
    public int id;

    /** Request type. */
    public int request;

    /**
     * Constructor for holding sendAndGet, subscribe, and unget information from client.
     * It's also used in (un)locking cloud and registration locks on the server side.
     */
    public cMsgHolder() {
    }

    /** Constructor for holding send and get information from client. */
    public cMsgHolder(cMsgMessageFull message) {
        this.message = message;
    }

    /** Constructor for client's subscribeAndGet. */
    public cMsgHolder(String subject, String type) {
        this.subject = subject;
        this.type = type;
    }

    /** Constructor for holding shutdown information from client. */
    public cMsgHolder(String client, boolean include) {
        this.client  = client;
        this.include = include;
    }

}

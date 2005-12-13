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
 * This class is used to help in implementing a client's "subscribeAndGet" method. An object
 * of this class stores a msg from the server to the method's caller and is used to
 * synchronize/wait/notify on. It also indicates whether the call timed out or not.
 *
 * This class is also used to implement Domain server by storing an incoming message
 * along with subject, type, id, and request (or client, server, flag for a shutdown)
 * for later action by a thread from the thread pool.
 */
public class cMsgHolder {
    /** Location to store message object. */
    public cMsgMessageFull message;

    /** Has the "subscribeAndGet" call timed out? */
    public boolean timedOut = true;

    /** Store subject. */
    public String subject;

    /** Store type. */
    public String type;

    /** Store type. */
    public String namespace;

    /** Store client(s) to shutdown. */
    public String client;

    /** Store server(s) to shutdown. */
    public String server;

    /**
     * Delay (miiliseconds) in cloud locking and client registration.
     * Also used for holding subscribeAndGet timeout.
     */
    public int delay;

    /** In shutdown call, do we want to shut ourselves down or not. */
    public int flag;

    /** Store id. */
    public int id;

    /** Store request. */
    public int request;


    public cMsgHolder() {
    }

    public cMsgHolder(cMsgMessageFull message) {
        this.message = message;
    }

    /** Constructor for client's subscribeAndGet. */
    public cMsgHolder(String subject, String type) {
        this.subject = subject;
        this.type = type;
    }

    /** Constructor for holding shutdown information from client. */
    public cMsgHolder(String client, String server, int flag) {
        this.client = client;
        this.server = server;
        this.flag = flag;
    }

}

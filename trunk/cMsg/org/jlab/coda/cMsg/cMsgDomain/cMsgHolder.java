/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 22-Oct-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain;

import org.jlab.coda.cMsg.cMsgMessageFull;
import java.nio.channels.SocketChannel;

/**
 * Class used to help in implementing a client's "get" method. An object of this
 * class stores a msg from the server to the "get" caller and is used to
 * synchronize/wait/notify on.
 *
 * Also used to implement Domain server by storing an incoming message along with
 * subject, type, and id for later action by a thread from the thread pool.
 */
class cMsgHolder {
    /** Location to store message object. */
    cMsgMessageFull message;

    /** Store subject. */
    String subject;

    /** Store type. */
    String type;

    /** Store id. */
    int id;

    /** Store request. */
    int request;

    /** Store communication channel. */
    SocketChannel channel;

    public cMsgHolder() {
    }

    public cMsgHolder(cMsgMessageFull message) {
        this.message = message;
    }
    public cMsgHolder(cMsgMessageFull message, int request) {
        this.message = message;
        this.request = request;
    }

    public cMsgHolder(String subject, String type, int id, int request) {
        this.request = request;
        this.subject = subject;
        this.type = type;
        this.id = id;
    }
}

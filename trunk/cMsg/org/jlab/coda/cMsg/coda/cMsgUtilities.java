/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 7-Jul-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.coda;

import org.jlab.coda.cMsg.cMsgConstants;

import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;
import java.io.IOException;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Aug 11, 2004
 * Time: 11:39:09 AM
 * To change this template use File | Settings | File Templates.
 */
public class cMsgUtilities {
    /** Read a minimum of number of bytes from the channel into the buffer. */
    static public int readSocketBytes(ByteBuffer buffer, SocketChannel channel, int bytes, int debug) throws IOException {

        int n, tries = 0, count = 0, maxTries=50;

        buffer.clear();
        buffer.limit(bytes);

        // Keep reading until we have exactly "bytes" number of bytes,
        // or have tried "tries" number of times to read.

//        if (debug >= cMsgConstants.debugInfo) {
//            System.out.println("readSocketBytes: will read " + bytes + " bytes");
//        }
        while (count < bytes) {
            if ((n = channel.read(buffer)) < 0) {
                throw new IOException("readSocketBytes: client's socket is dead");
            }
            if (tries > maxTries) {
                throw new IOException("readSocketBytes: too many tries to read " + n + " bytes");
            }
            tries++;
            count += n;
            if (debug >= cMsgConstants.debugInfo && tries==maxTries) {
                System.out.println("readSocketBytes: called read " + tries + " times, read " + n + " bytes");
            }
            try {Thread.sleep(10);} catch (InterruptedException e) {}
        }
        return count;
    }

}

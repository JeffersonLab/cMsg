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

package org.jlab.coda.cMsg;

import java.nio.channels.SocketChannel;
import java.nio.channels.ByteChannel;
import java.nio.ByteBuffer;
import java.io.IOException;
import java.net.InetAddress;
import java.net.UnknownHostException;

/**
 * This class stores methods which are neatly self-contained and
 * may be used in more that one place.
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgUtilities {

    /**
        * Converts 4 bytes of a byte array into an integer.
        *
        * @param b byte array
        * @param off offset into the byte array (0 = start at first element)
        * @return integer value
        */
       static public final int bytesToInt(byte[] b, int off) {
         int result = ((b[off]  &0xff) << 24) |
                      ((b[off+1]&0xff) << 16) |
                      ((b[off+2]&0xff) <<  8) |
                       (b[off+3]&0xff);
         return result;
       }


    static public final ByteChannel wrapChannel2(final ByteChannel channel)
    {
        return channel;
    }


    /**
     * This method wraps a SocketChannel so that the input and output streams
     * derived from that channel do not block each other (SUN java bug id 4774871).
     * The problem is that a SocketChannel is also a SelectableChannel which has this
     * synchronization issue, and that is eliminated by making it a ByteChannel.
     *
     * @param channel the SelectableChannel which has sync problems
     * @return a ByteChannel which does not have sync problems
     */
     static public final ByteChannel wrapChannel(final ByteChannel channel) {
        return new ByteChannel() {
            public int write(ByteBuffer src) throws IOException {
                return channel.write(src);
            }

            public int read(ByteBuffer dst) throws IOException {
                return channel.read(dst);
            }

            public boolean isOpen() {
                return channel.isOpen();
            }

            public void close() throws IOException {
                channel.close();
            }
        };
    }


    /**
      * This methods reads a minimum of number of bytes from a channel into a buffer.
      *
      * @param buffer   a byte buffer which channel data is read into
      * @param channel  nio socket communication channel
      * @param bytes    minimum number of bytes to read from channel
      * @return number of bytes read
      * @throws IOException If channel is closed or cannot be read from
      */
     static public final int readSocketBytesPlain(ByteBuffer buffer, SocketChannel channel, int bytes)
             throws IOException {

         int n, count = 0;

         buffer.clear();
         buffer.limit(bytes);

         // Keep reading until we have exactly "bytes" number of bytes,

         while (count < bytes) {
             if ((n = channel.read(buffer)) < 0) {
                 throw new IOException("readSocketBytes: client's socket is dead");
             }
             count += n;
         }
         return count;
     }


    /**
      * This methods reads a minimum of number of bytes from a channel into a buffer.
      *
      * @param buffer   a byte buffer which channel data is read into
      * @param channel  nio socket communication channel
      * @param bytes    minimum number of bytes to read from channel
      * @param debug    level of debug output
      * @return number of bytes read
      * @throws IOException If channel is closed or cannot be read from
      */
     static public final int readSocketBytes(ByteBuffer buffer, SocketChannel channel, int bytes, int debug)
             throws IOException {

         int n, tries = 0, count = 0, maxTries=200;

         buffer.clear();
         buffer.limit(bytes);

         // Keep reading until we have exactly "bytes" number of bytes,
         // or have tried "tries" number of times to read.

         while (count < bytes) {
             if ((n = channel.read(buffer)) < 0) {
                 throw new IOException("readSocketBytes: client's socket is dead");
             }
             count += n;
             if (count >= bytes) {
                 break;
             }

             if (tries > maxTries) {
                 throw new IOException("readSocketBytes: too many tries to read " + n + " bytes");
             }
             tries++;
             if (debug >= cMsgConstants.debugInfo && tries==maxTries) {
                 System.out.println("readSocketBytes: called read " + tries + " times, read " + n + " bytes");
             }
             try {
                 Thread.sleep(1);
             }
             catch (InterruptedException e) {}
         }
         return count;
     }



    /**
     * Method that returns and/or prints an error explanation.
     *
     * @param error error number
     * @param debug level of debug output
     * @return error explanation
     */
    static public final String printError(int error, int debug) {
      String reason;

      switch (error) {

          case cMsgConstants.ok:
              reason = "action completed successfully";
              if (debug > cMsgConstants.debugError)
                  System.out.println("ok: " + reason);
              return(reason);

          case cMsgConstants.error:
              reason = "generic error return";
              break;

          case cMsgConstants.errorTimeout:
              reason = "no response from cMsg server within timeout period";
              break;

          case cMsgConstants.errorNotImplemented:
              reason = "function not implemented";
              break;

          case cMsgConstants.errorBadArgument:
              reason = "one or more arguments bad";
              break;

          case cMsgConstants.errorBadFormat:
              reason = "one or more arguments in the wrong format";
              break;

          case cMsgConstants.errorBadDomainType:
              reason = "domain type not supported";
              break;

          case cMsgConstants.errorAlreadyExists:
              reason = "another process in this domain is using this name";
              break;

          case cMsgConstants.errorNotInitialized:
              reason = "connection to server needs to be made";
              break;

          case cMsgConstants.errorAlreadyInitialized:
              reason = "connection to server already exists";
              break;

          case cMsgConstants.errorLostConnection:
              reason = "connection to cMsg server lost";
              break;

          case cMsgConstants.errorNetwork:
              reason = "error talking to cMsg server";
              break;

          case cMsgConstants.errorSocket:
              reason = "error setting socket options";
              break;

          case cMsgConstants.errorPend:
              reason = "error waiting for messages to arrive";
              break;

          case cMsgConstants.errorIllegalMessageType:
              reason = "pend received illegal message type";
              break;

          case cMsgConstants.errorNoMemory:
              reason = "out of memory";
              break;

          case cMsgConstants.errorOutOfRange:
              reason = "argument is out of range";
              break;

          case cMsgConstants.errorLimitExceeded:
              reason = "trying to create too many of something";
              break;

          case cMsgConstants.errorBadDomainId:
              reason = "intVal does not match any existing domain";
              break;

          case cMsgConstants.errorBadMessage:
              reason = "message is not in the correct form";
              break;

          case cMsgConstants.errorWrongDomainType:
              reason = "UDL does not match the server type";
              break;

          case cMsgConstants.errorNoClassFound:
              reason = "java class cannot be found";
              break;

          case cMsgConstants.errorDifferentVersion:
              reason = "error due to being different version";
              break;

          case cMsgConstants.errorWrongPassword:
              reason = "error due to wrong password";
              break
                      ;
          case cMsgConstants.errorServerDied:
              reason = "error due to server dying";
              break;

          case cMsgConstants.errorAbort:
              reason = "error due to aborted procedure";
              break;

          default:
              reason = "no such error (" + error + ")";
              if (debug > cMsgConstants.debugError)
                  System.out.println("error: "+ reason);
              return(reason);
      }

      if (debug > cMsgConstants.debugError)
          System.out.println("error: " + reason);

      return(reason);
    }

    /**
     * This method tests its input argument to see if it is in the proper format
     * for a server; namely, "host:port". If it is, it makes sure the host is in
     * its canonical form.
     *
     * @param s input string of a possible server name
     * @return an acceptable server name with the canonical form of the host name
     * @throws org.jlab.coda.cMsg.cMsgException if input string is not in the proper form (host:port)
     *                       or the host is unknown
     */
    static public final String constructServerName(String s) throws cMsgException {

        // Separate the server name from the server port.
        // First check for ":"
        int index = s.indexOf(":");
        if (index == -1) {
            throw new cMsgException("arg is not in the \"host:port\" format");
        }

        String sName = s.substring(0, index);
        String sPort = s.substring(index + 1);
        int port;

        // translate the port from string to int
        try {
            port = Integer.parseInt(sPort);
        }
        catch (NumberFormatException e) {
            throw new cMsgException("port needs to be an integer");
        }

        if (port < 1024 || port > 65535) {
            throw new cMsgException("port needs to be an integer between 1024 & 65535");
        }

        // See if this host is recognizable. To do that
        InetAddress address;
        try {
            address = InetAddress.getByName(sName);
        }
        catch (UnknownHostException e) {
            throw new cMsgException("specified server is unknown");
        }

        // put everything in canonical form if possible
        s = address.getCanonicalHostName() + ":" + sPort;

        return s;
    }
}

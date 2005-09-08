/*----------------------------------------------------------------------------*
 *  Copyright (c) 2005        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 8-Jun-2005, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.server;

import org.jlab.coda.cMsg.*;

import java.util.Set;
import java.io.IOException;

/**
 * This class acts to bridge two cMsg domain servers by existing in one server
 * and becoming a client of another cMsg domain server.
 */
public class cMsgServerBridge {

    /** Client of cMsg server this object is a bridge to. It is the means of connection. */
    private org.jlab.coda.cMsg.cMsgDomain.client.cMsg client;

    /** Name of cMsg server ("host:port") this server is connected to. */
    String server;

    /** The cloud status of the cMsg server this server is connected to. */
    volatile int cloudStatus = cMsgNameServer.UNKNOWNCLOUD;

    /**
     * Did this server originate the connection/bridge between this server
     * and the one this bridge is to? Or was the bridge created in
     * response to an originating connection? This information is necessary
     * to avoid infinite loops when connecting two servers to each other.
     */
    boolean isOriginator;

    /** The port this name server is listening on. */
    int port;

    /** Reference to subdomain handler object for use by all bridges in this server. */
    static private org.jlab.coda.cMsg.subdomains.cMsg subdomainHandler = new org.jlab.coda.cMsg.subdomains.cMsg();

    /**
     * This class defines the callback to be run when a message matching
     * our subscription arrives from a bridge connection and must be
     * passed on to a client(s).
     */
    static class SubscribeCallback extends cMsgCallbackAdapter {
        /**
         * Callback which passes on a message to other clients.
         *
         * @param msg message received from domain server
         * @param userObject in this case the original msg sender's namespace.
         */
        public void callback(cMsgMessage msg, Object userObject) {
//System.out.println("In bridge callback!!!");
            String namespace = (String) userObject;

            // check for null text
            if (msg.getText() == null) {
                msg.setText("");
            }

            try {
                // pass this message on to local clients
                subdomainHandler.bridgeSend(msg, namespace);
            }
            catch (cMsgException e) {
                e.printStackTrace();
            }
        }
    }


    /** Need 1 callback for subscriptions. */
    SubscribeCallback subCallback = new SubscribeCallback();

    /**
     * Constructor.
     *
     * @param server name of server to connect to in the form "host:port"
     * @param thisNameServerPort the port this name server is listening on
     * @throws cMsgException
     */
    public cMsgServerBridge(String server, int thisNameServerPort) throws cMsgException {
        this.server = server;
        port = thisNameServerPort;

        // Normally a client uses the top level API. That is unecessary
        // (and undesirable) here because we already know we're in the
        // cMsg subdomain. We'll use the cMsg domain level object so we'll
        // have access to additional methods not part of the API.
        // The next few lines simply do what the top level API does.

        // create a proper UDL out of this server name
        String UDL = "cMsg://" + server + "/cMsg";

        // create cMsg connection object
//System.out.println("      << BR: const, make client object");
        client = new org.jlab.coda.cMsg.cMsgDomain.client.cMsg();

        // Pass in the UDL
//System.out.println("      << BR: set UDL to " + UDL);
        client.setUDL(UDL);
        // Pass in the name
//System.out.println("      << BR: set Name to " + server);
        client.setName(server);
        // Pass in the description
        client.setDescription("server");
        // Pass in the UDL remainder
//System.out.println("      << BR: set UDL remainder to: " + server + "/" + "cMsg");
        client.setUDLRemainder(server + "/" + "cMsg");
    }

    /**
     * Method to connect to server.
     * @param isOriginator true if originating the connection between the 2 servers and
     *                     false if this is the response or reciprocal connection
     * @return
     * @throws cMsgException
     */
    public Set<String> connect(boolean isOriginator) throws cMsgException {
        this.isOriginator = isOriginator;
        // create a connection to the UDL
        client.start();
        return client.serverConnect(port, isOriginator);
    }

   /**
    * This method gets the cloud status of the server this server is
    * connected to. Possible values are {@link cMsgNameServer.INCLOUD},
    * {@link cMsgNameServer.NONCLOUD}, or{@link cMsgNameServer.BECOMINGCLOUD}.
    *
    * @return cloud status
    */
    int getCloudStatus() {
        return cloudStatus;
    }


    /**
     * This method locally sets the cloud status of the server this server is
     * connected to through this bridge. Possible values are {@link cMsgNameServer.INCLOUD},
     * {@link cMsgNameServer.NONCLOUD}, or{@link cMsgNameServer.BECOMINGCLOUD}.
     *
     * @param cloudStatus true if in cloud, else false
     */
    void setCloudStatus(int cloudStatus) {
        this.cloudStatus = cloudStatus;
    }


    /**
     * This method tells the server this server is connected to,
     * what cloud status this server officially has.
     *
     * @param status cMsgNameServer.INCLOUD, .NONCLOUD, or .BECOMINGCLOUD
     * @throws IOException if communication error with server
     */
    void thisServerCloudStatus(int status) throws IOException {
//System.out.println("      << BR: try setting cloud status");
        client.thisServerCloudStatus(status);
//System.out.println("      << BR: done setting cloud status");
    }


    /**
     * This method grabs the cloud lock for adding a server to the cMsg subdomain server cloud.
     *
     * @param delay time in milliseconds to wait for the lock before timing out
     * @return true if lock was obtained, else false
     * @throws IOException if communication error with server
     */
    boolean cloudLock(int delay) throws IOException {
//System.out.println("      << BR: try locking cloud");
        return client.cloudLock(delay);
    }


    /**
     * This method releases the cloud lock for adding a server to the cMsg subdomain server cloud.
     * @throws IOException if communication error with server
     */
    void cloudUnlock() throws IOException {
//System.out.println("      << BR: try unlocking cloud");
        client.cloudUnlock();
        return;
    }


    /**
     * This method grabs the lock (for adding a client) of another server's cMsg subdomain.
     *
     * @param  delay time in milliseconds to wait for the lock before timing out
     * @return true if lock was obtained, else false
     * @throws IOException if communication error with server
     */
    boolean registrationLock(int delay) throws IOException {
        return client.registrationLock(delay);
    }


    /**
     * This method releases the lock (for adding a client) of another server's cMsg subdomain.
     * @throws IOException if communication error with server
     */
    void registrationUnlock() throws IOException {
        client.registrationUnlock();
        return;
    }


    /**
     * This method gets the names of all the local clients (not servers)
     * of another server's cMsg subdomain.
     *
     * @return array of client names
     * @throws IOException
     */
    String[] getClientNames() throws IOException {
        return client.getClientNames();
    }


    /**
     * Method for a server to subscribe to receive messages of a subject
     * and type from the domain server. The combination of arguments must be unique.
     * In other words, only 1 subscription is allowed for a given set of subject,
     * type, callback, and userObj.
     *
     * @param subject    message subject
     * @param type       message type
     * @param namespace  message namespace
     * @throws cMsgException if the callback, subject, or type is null; the subject or type is
     *                       blank; an identical subscription already exists; there are
     *                       communication problems with the server
     */
    public void subscribe(String subject, String type, String namespace)
        throws cMsgException {

//System.out.println("Bridge: call serverSubscribe with ns = " + namespace);
        client.serverSubscribe(subject, type, namespace, subCallback, namespace);
        return;
    }


    /**
     * Method for a server to unsubscribe for messages of a subject
     * and type from the domain server.
     *
     * @param subject    message subject
     * @param type       message type
     * @param namespace  message namespace
     * @throws cMsgException if the callback, subject, or type is null; the subject or type is
     *                       blank; an identical subscription already exists; there are
     *                       communication problems with the server
     */
    public void unsubscribe(String subject, String type, String namespace)
        throws cMsgException {

//System.out.println("Bridge: call serverUnsubscribe with ns = " + namespace);
        client.serverUnsubscribe(subject, type, namespace, subCallback, namespace);
        return;
    }


}

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

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgDomain.cMsgSubscription;
import java.util.Set;
import java.io.IOException;

/**
 * This class acts to bridge 2 cMsg domain servers by existing in one server
 * and becoming a client of another cMsg domain server.
 */
public class cMsgServerBridge extends Thread {

    /** cMsg subdomain handler object. */
    private org.jlab.coda.cMsg.subdomains.cMsg subdomainHandler;

    private org.jlab.coda.cMsg.cMsgDomain.client.cMsg client;

    /** Name of cMsg server ("host:port") this server is connected to. */
    String server;

    /** The cloud status of the cMsg server this server is connected to. */
    volatile int cloudStatus = cMsgNameServer.UNKNOWNCLOUD;

    boolean isOriginator;

    int port;


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

        // Make a cMsg subdomain handler object.
        subdomainHandler = new org.jlab.coda.cMsg.subdomains.cMsg();
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
     * This method sets the cloud status of the server this server is
     * connected to. Possible values are {@link cMsgNameServer.INCLOUD},
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
        client.thisServerCloudStatus(status);
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


    /** This method is executed as a thread. */
     public void run() {
        cMsgSubscription sub;

        // If we have existing local subscriptions, create identical
        // subscriptions on the connected server.

        while (true) {
            sub = subdomainHandler.waitForNewSubscription();

        }





    }
}

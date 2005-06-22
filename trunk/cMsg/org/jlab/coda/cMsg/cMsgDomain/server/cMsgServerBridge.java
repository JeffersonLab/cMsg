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
import org.jlab.coda.cMsg.cMsgClientInfo;
import org.jlab.coda.cMsg.cMsgDomain.cMsgSubscription;

import java.util.concurrent.ConcurrentHashMap;

/**
 * This class acts to bridge 2 cMsg domain servers by existing in one server
 * and becoming a client of another cMsg domain server.
 */
public class cMsgServerBridge extends Thread {

    /** cMsg subdomain handler object. */
    private org.jlab.coda.cMsg.subdomains.cMsg subdomainHandler;

    private org.jlab.coda.cMsg.cMsgDomain.client.cMsg client;

    /** Name of cMsg server ("host:port") to connect to. */
    String name;

    /**
     * This hashMap stores all servers this server is connected to.
     * The String "name:port" is the key and cMsgServerStatistics is the value.
     * If this map contains a key, that server is connected to. One can also
     * obtains that server's operating statistics by looking up it value object.
     */
    static private ConcurrentHashMap<String,cMsgServerStatistics> servers =
            new ConcurrentHashMap<String,cMsgServerStatistics>(30);


    /** Constructor. */
    public cMsgServerBridge(String name) throws cMsgException {
        // Check to see if already connected to server
        if (servers.containsKey(name)) {
            throw new cMsgException("already connected to server");
        }
        this.name = name;

        // Normally a client uses the top level API. That is unecessary
        // (and undesirable) here because we already know we're in the
        // cMsg subdomain. We'll use the cMsg domain level object so we'll
        // have access to additional methods not part of the API.
        // The next few lines simply do what the top level API does.

        // create a proper UDL out of this name
        String UDL = "cMsg://" + name + "/cMsg";

        // create cMsg connection object
        client = new org.jlab.coda.cMsg.cMsgDomain.client.cMsg();

        // Pass in the UDL
        client.setUDL(UDL);
        // Pass in the name
        client.setName(name);
        // Pass in the description
        client.setDescription("server");
        // Pass in the UDL remainder
        client.setUDLRemainder(name + "/" + "cMsg");

        // now that it has the proper UDL, create a connection to that UDL
        client.serverConnect();

        // Make a cMsg subdomain handler object.
        subdomainHandler = new org.jlab.coda.cMsg.subdomains.cMsg();

        // start the ball rolling listening for new subscriptions
        this.start();

        // Make sure the thread to listen for new subs is up and running
        // before we create subs identical to those already existing.
        // Give thread time to start up. It's crude but it works.
        try { Thread.sleep(200); }
        catch (InterruptedException e) {}

        // If we have existing local subscriptions, create identical
        // subscriptions on the connected server.



    }

    /** This method is executed as a thread. */
     public void run() {
        cMsgSubscription sub;

        while (true) {
            sub = subdomainHandler.waitForNewSubscription();

        }





    }
}

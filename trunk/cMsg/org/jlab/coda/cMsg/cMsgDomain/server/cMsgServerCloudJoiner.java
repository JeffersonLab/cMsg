/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 14-Jul-2005, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.cMsgDomain.server;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgConstants;

import java.util.*;
import java.util.concurrent.ConcurrentHashMap;
import java.io.IOException;
import java.net.InetAddress;
import java.net.UnknownHostException;

/**
 * Class to oversee that connection of a cMsg server to a
 * cloud of connected cMsg servers in the cMsg subdomain
 * and to become part of that cloud.
 */
public class cMsgServerCloudJoiner extends Thread {

    /** The port this server is listening on. */
    int port;

    private HashSet<String> serversToConnectTo = new HashSet<String>(20);

    /**
      * This hashMap stores all servers this server is connected to.
      * The String "server:port" is the key and cMsgServerStatistics is the value.
      * If this map contains a key, that server is connected to. One can also
      * obtains that server's operating statistics by looking up it value object.
      */
    private ConcurrentHashMap<String,cMsgServerStatistics> connectedServers =
            new ConcurrentHashMap<String,cMsgServerStatistics>(30);

    /** Level of debug output. */
     private int debug = cMsgConstants.debugError;

    /** Constructor. */
    public cMsgServerCloudJoiner(int port, String server) {
        this.port = port;
        serversToConnectTo.add(server);
        start();
    }

    /** This method is executed as a thread. */
    public void run() {
        HashSet<String> unknownServers = new HashSet<String>(10);
//System.out.println("    << JR: Running server cloud joiner");

        do {
            // Start with clean slate - no servers we don't know about.
            // (We know about all servers.)
            unknownServers.clear();

            // Connect to all known servers in "connect to" list
            for (String server : serversToConnectTo) {

                // Check to see if already connected to server
                if (connectedServers.containsKey(server)) {
                    //throw new cMsgException("already connected to server");
                    continue;
                }

                Set<String> serverNames = null;
                try {
//System.out.println("    << JR: Creating bridge to: " + server);
                    cMsgServerBridge bridge = new cMsgServerBridge(server, port);
                    // Store reference to bridge so cMsgNameServer can use it when
                    // accepting reciprocal connection.
                    cMsgNameServer.bridgeBeingCreated = bridge;
                    // Connect returns set of servers that "server" is connected to
                    serverNames = bridge.connect(true);
                    // After our first successful connect (currently NONCLOUD status),
                    // we are now in the BECOMINGCLOUD status.
                    if (cMsgNameServer.getCloudStatus() == cMsgNameServer.NONCLOUD) {
//System.out.println("    << JR: Now in BECOMINCLOUD status");
                        cMsgNameServer.setCloudStatus(cMsgNameServer.BECOMINGCLOUD);
                    }
//System.out.println("    << JR: Adding bridge (" + bridge + ") to bridges map");
                    cMsgNameServer.bridges.put(server, bridge);
                }
                // Throws cMsgException if there are problems parsing the UDL or
                // communication problems with the server.
                catch (cMsgException e) {
                    // If we have not yet connected to the very first incloud server
                    // (given on command line), then exit with error.
                    if (cMsgNameServer.getCloudStatus() == cMsgNameServer.NONCLOUD) {
//System.out.println("      << JR: Cannot connect to given server: " + server);
                        System.exit(-1);
                    }
                }

                // If there are servers that the server we are connecting to right now
                // knows about that we don't (aren't in serversToConnectTo list or the
                // bridges list), put 'em in the "unknownServers" list.
                if (serverNames != null) {
                    for (String s : serverNames) {

                        // When comparing server names we need to be careful of
                        // comparing fully qualifed names to those that are not.
                        // So look to see if either type is in the collection of
                        // established connections.
                        String alternateName = null;
                        String sPort = s.substring(s.lastIndexOf(":")+1);
                        int index = s.indexOf(".");

                        // If the name has a dot (is qualified), create unqualified name
                        if (index > -1) {
                            alternateName = s.substring(0,index) + ":" + sPort;
//System.out.println("    << JR: alternateName (unqualified) = " + alternateName);
                        }
                        // else create qualified name
                        else {
                            try {
                                // take off ending port
                                alternateName = s.substring(0, s.lastIndexOf(":"));
                                alternateName = InetAddress.getByName(alternateName).getCanonicalHostName();
                                alternateName = alternateName + ":" + sPort;
//System.out.println("    << JR: alternateName (qualified) = " + alternateName);
                            }
                            catch (UnknownHostException e) {
//System.out.println("    << JR: error qualifing " + s);
                                alternateName = s;
                            }
                        }

                        if (!cMsgNameServer.bridges.keySet().contains(s) &&
                            !cMsgNameServer.bridges.keySet().contains(alternateName) &&
                            !serversToConnectTo.contains(s) &&
                            !serversToConnectTo.contains(alternateName)) {

                            unknownServers.add(s);
//System.out.println("    << JR: Added unknown server " + s + " to list (size = " + unknownServers.size() + ")");
                        }
                        else {
//System.out.println("    << JR: Already connected (or will connect) to server \"" + s + "\"");
                        }
                    }
                }
            }

            // we've connected to all known servers
            serversToConnectTo.clear();

            // now connect to the previously unknown servers
            serversToConnectTo.addAll(unknownServers);

//System.out.println("    << JR: Size of serversToConnectTo map: " + serversToConnectTo.size());
        } while (serversToConnectTo.size() > 0);


        int     grabLockTries = 0;
        boolean gotSelfLock   = false;
        boolean gotCloudLock  = false;
        boolean amInCloud     = false;
        cMsgServerBridge firstLockBridge = null;
        int maxNumberOfTrys=3, numberOfTrys=0, backOffFactor = 2;
        LinkedList<cMsgServerBridge> lockedBridges = new LinkedList<cMsgServerBridge>();

//System.out.println("    << JR: About to grab all cloudlocks");

    startOver:

        while (numberOfTrys++ < maxNumberOfTrys) {
//System.out.println("    << JR: startOver");

            // Now we've CONNECTED to every server in the cloud and
            // some of those that are trying to attach to the cloud.
            // We're ready to JOIN the cloud ourself.
            //
            // Strictly speaking, it's only necessary to grab the cloud
            // locks of a majority of servers already in the cloud before
            // one can join the cloud.
            // However, practically speaking, there are a couple of other
            // considerations to be taken into account:
            // 1) It's much easier to program a requirement to grab ALL the
            // locks of the in-cloud servers. That's because if someone grabs
            // just one lock, that can stop others from joining -- simplifying
            // the logic. Also, when registering a client, being able to grab
            // the local cloud lock and thereby stop noncloud servers and their
            // clients from joining simultaneously is necessary.
            // 2) If there are bridges to servers which are in the process
            // of joining (not in-cloud), then grabbing their locks will stop
            // them from trying to join simulaneously. This should, again,
            // make things work more smoothly.
            // Thus we grab our own lock (even thought we're not part of
            // the cloud yet) and the locks of all the bridges. When we
            // have all the locks of the servers IN the cloud, then we can
            // procede to join.
            //
            // Note that 2 simultaneous joiners will be connected to eachother,
            // but the one joining a bit later may know & be connected to
            // another, still later joiner that the first does NOT know about.
            // Thus, the majority calculation must only be made on cloud members.
            // We need to know what constitues the majority of servers so that
            // if 2 or more are trying to join simultaneously, the joiner with
            // the majority of locks will not back off while the others will.

            // First grab our own lock so no one tries to connect to us.
            if (!gotSelfLock) {
                cMsgNameServer.cloudLock();
                gotSelfLock = true;
            }
            lockedBridges.clear();

// BUG BUG send timeout length ??
            // We need to calculate the number of locks which constitute a
            // majority of servers currently a part of the cloud. This can
            // only be done once we have a lock of a cloud member (thereby
            // preventing anyone else from joining).
            grabLockTries = 0;
            gotCloudLock  = false;

            do {
                // Grab one lock to start with so we can calculate a majority value.
                for (cMsgServerBridge bridge : cMsgNameServer.bridges.values()) {
//System.out.println("    << JR: grabLockTries = " + grabLockTries);
//System.out.println("    << JR: status = " + bridge.getCloudStatus() + ", bridge = " + bridge);
                    if (bridge.getCloudStatus() == cMsgNameServer.INCLOUD) {
//System.out.println("    << JR: try in-cloud lock");
                        try {
                            if (bridge.cloudLock(200)) {
//System.out.println("    << JR: grabbed in-cloud lock");
                                lockedBridges.add(bridge);
                                firstLockBridge = bridge;
                                gotCloudLock = true;
                                break;
                            }
                        }
                        catch (IOException e) {}
                    }
                }

                // Can't grab a lock, wait and try again (at most 3 times)
                if (!gotCloudLock) {
                    // if we've reached our limit of tries ...
                    if (++grabLockTries > 3) {
                        if (debug >= cMsgConstants.debugWarn) {
                            System.out.println("    << JR: Failed to grab inital cloud lock");
                        }
                        // delay 1/2 sec
                        try {Thread.sleep(500);}
                        catch (InterruptedException e) {}
                        // take it from the top
//System.out.println("    << JR: continue to startOver");
                        continue startOver;
                    }

                    try {Thread.sleep(10);}
                    catch (InterruptedException e) {}
                }
            } while (!gotCloudLock);

            // Calculate the majority
            int totalCloudMembers = 0;
            for (cMsgServerBridge bridge : cMsgNameServer.bridges.values()) {
                if (bridge.getCloudStatus() == cMsgNameServer.INCLOUD) {
                    totalCloudMembers++;
                }
            }
            int majority = totalCloudMembers / 2 + 1;
            int numberOfLockedCloudMembers = 1;

            // Try to get all of the in-cloud servers' locks
            do {
                // Grab the locks of ALL other servers
                for (cMsgServerBridge bridge : cMsgNameServer.bridges.values()) {

                    // If it's already locked, skip it
                    if (lockedBridges.contains(bridge)) {
                        continue;
                    }

                    try {
                        // If sucessfull in locking remote server ...
//System.out.println("    << JR: Try to lock bridge to " + bridge.server);
                        if (bridge.cloudLock(200)) {
//System.out.println("    << JR: LOCKED IT!!");
                            lockedBridges.add(bridge);
                            if (bridge.getCloudStatus() == cMsgNameServer.INCLOUD) {
                                numberOfLockedCloudMembers++;
                            }
                        }
                        // else if cannot lock remote server, try next one
                        else {
                            continue;
                        }
                    }
                    // We're here if lock or unlock fails in its communication with server.
                    // If we cannot talk to the server, it's probably dead.
                    catch (IOException e) {
                        continue;
                    }
                }

//System.out.println("    << JR: FAILED TO LOCKED IT!!");

                // If we have all the in-cloud locks we're done and can move on.
                if (numberOfLockedCloudMembers >= totalCloudMembers) {
                    break;
                }
                // If we have a majority (but not all) in-cloud locks, try to get the rest.
                else if (numberOfLockedCloudMembers >= majority) {
                    // Let other greedy lock-grabbers release theirs locks first
//System.out.println("    << JR: Get More Locks");
                    try {Thread.sleep(10);}
                    catch (InterruptedException e) {}
                }
                // If we do NOT have the majority of locks ...
                else {
                    // release all locks
                    for (cMsgServerBridge b : lockedBridges) {
                        try {b.cloudUnlock();}
                        catch (IOException e) {}
                    }
                    cMsgNameServer.cloudUnlock();
                    gotSelfLock  = false;
                    gotCloudLock = false;

                    // Wait for a random time initially between 10 & 300
                    // milliseconds which doubles each loop.
                    Random rand = new Random();
                    int milliSec = (int) ((10 + rand.nextInt(291))*(Math.pow(2., numberOfTrys-1.)));
                    try {Thread.sleep(milliSec);}
                    catch (InterruptedException e) {}

                    // start over
//System.out.println("    << JR: Drop locks and start over");
                    continue startOver;
                }

            } while (true);

            // Tell others we are now in the cloud
            cMsgNameServer.setCloudStatus(cMsgNameServer.INCLOUD);
            for (cMsgServerBridge bridge : cMsgNameServer.bridges.values()) {
//System.out.println("    << JR: Tell server we are in the cloud");
                try {
                    bridge.thisServerCloudStatus(cMsgNameServer.INCLOUD);
                }
                catch (IOException e) {
                    System.out.println("    << JR: Bombed while Telling server we are in the cloud");
                }
            }

            // release the locks
            for (cMsgServerBridge bridge : cMsgNameServer.bridges.values()) {
                try {bridge.cloudUnlock();}
                catch (IOException e) {continue;}
            }
            cMsgNameServer.cloudUnlock();

//System.out.println("    << JR: I'm in the cloud\n\n");
            amInCloud = true;
            break;
        }

        // If we could NOT join the cloud for some reason ...
        if (!amInCloud) {
            // not really necessary since we're going to exit anyway
            if (gotSelfLock) {
                cMsgNameServer.cloudUnlock();
            }
            // need to do this, however
            if (gotCloudLock) {
                try {firstLockBridge.cloudUnlock();}
                catch (IOException e) {}
            }
            System.out.println("    << JR: *******************************************************************");
            System.out.println("    << JR: Cannot join the specified cloud, becoming the center of a new cloud");
            System.out.println("    << JR: *******************************************************************");
            System.exit(-1);
        }

        // Set our new status as part of the cloud
        cMsgNameServer.setCloudStatus(cMsgNameServer.INCLOUD);

        // Allow client and server connections
        cMsgNameServer.allowConnectionsSignal.countDown();

    }

}

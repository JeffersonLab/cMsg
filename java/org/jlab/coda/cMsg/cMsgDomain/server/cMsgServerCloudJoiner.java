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
import org.jlab.coda.cMsg.cMsgNetworkConstants;
import org.jlab.coda.cMsg.cMsgUtilities;

import java.util.*;
import java.io.IOException;
import java.net.InetAddress;
import java.net.UnknownHostException;

/**
 * This class oversees the connection of a cMsg server to a
 * cloud of connected cMsg servers in the cMsg subdomain
 * to become part of that cloud.
 */
class cMsgServerCloudJoiner extends Thread {

    /** The TCP port this server is listening on. */
    private int port;

    /** The UDP multicast port this server is listening on. */
    private int multicastPort;

    /** The object which created this object. */
    private cMsgNameServer nameServer;

    /**
     * Set of servers (host:port) currently known to be in (or are in the
     * process of becoming part of) the cloud.
     */
    private HashSet<String> serversToConnectTo = new HashSet<String>(20);

    /**
     * This is a set of servers (host:port) from which this server will attempt to
     * connect to ONE and so become a part of its cloud. An attempt will be made to
     * servers based on list order. First connection wins.
     */
    private HashSet<String> cloudServers = new HashSet<String>(20);


    /** Level of debug output. */
    private int debug;

    /**
     * Constructor.
     *
     * @param nameServer this cMsg name server that is joining the cloud
     * @param nsTcpPort TCP port this server is listening on
     * @param nsUdpPort UDP multicast port this server is listening on
     * @param servers list of cMsg name servers from which to attempt to
     *                connect to one
     * @param debug level of debug output
     */
    public cMsgServerCloudJoiner(cMsgNameServer nameServer, int nsTcpPort,
                                 int nsUdpPort, Set<String> servers, int debug) {
        this.port = nsTcpPort;
        this.multicastPort = nsUdpPort;
        this.debug = debug;
        this.nameServer = nameServer;
        this.cloudServers.addAll(servers);
        start();
    }

    /** This method is executed as a thread. */
    public void run() {

        boolean methodDebug = false;

        HashSet<String> unknownServers = new HashSet<String>(10);

        // Wait until this name server is ready to accept connections
        // before going out and connecting to servers that will turn
        // around and connect back to this one.
        try {
            nameServer.listeningThreadsStartedSignal.await();
        }
        catch (InterruptedException e) {
        }

        // not made first connection yet
        boolean madeFirstConnection = false;
        // using multicasting to connect
        boolean multicasting;

        // Find an initial server whose cloud we'll join.
        // To do that, go thru this list.
        for (String startServer : cloudServers) {

if(methodDebug) System.out.println("    << JR: Try joining cloud of server = " + startServer);
            serversToConnectTo.clear();
            serversToConnectTo.add(startServer);

            do {
                // Start with clean slate - no servers we don't know about.
                // (We know about all servers.)
                unknownServers.clear();

                // Connect to all known servers in "connect to" list
                for (String server : serversToConnectTo) {

                    // servers to consider connecting to (if not already)
                    Set<String> serverCandidates = null;

                    try {
if(methodDebug) System.out.println("    << JR: Creating bridge to: " + server);
                        // If we're multicasting, we don't know server's host yet.
                        // Once we find out, we must change the server's name so
                        // that it is unique (host:port) since "multicast:<port>"
                        // is not. First see if we're multicasting.
                        multicasting = server.startsWith("multicast:");

                        // This throws cMsgException if cannot find localhost's name
                        cMsgServerBridge bridge = new cMsgServerBridge(nameServer, server,
                                                                       port, multicastPort);
                        // Store reference to bridge so cMsgNameServer can use it when
                        // accepting reciprocal connection.
                        nameServer.bridgeBeingCreated = bridge;
                        // Connect returns set of servers that "server" is connected to
                        serverCandidates = bridge.connect(true, nameServer.cloudPassword,
                                                     nameServer.clientPassword, multicasting);
                        // Bridge's client has changed its name now that the server's host
                        // is known so now change server name to the new name
                        if (multicasting) {
                            server = bridge.client.getName();
                            bridge.setServerName(server);
if(methodDebug) System.out.println("    << JR: Set client name and bridge server name to " + server);
                        }
                        madeFirstConnection = true;
                        // After our first successful connect (currently NONCLOUD status),
                        // we are now in the BECOMINGCLOUD status.
                        if (nameServer.getCloudStatus() == cMsgNameServer.NONCLOUD) {
if(methodDebug) System.out.println("    << JR: Now in BECOMINGCLOUD status");
                            nameServer.setCloudStatus(cMsgNameServer.BECOMINGCLOUD);
                        }
if(methodDebug) System.out.println("    << JR: Adding bridge to map for server = " + server);
                        nameServer.bridges.put(server, bridge);
                    }
                    // Throws cMsgException if there are problems parsing the UDL or
                    // communication problems with the server.
                    catch (cMsgException e) {
                        // If we have not yet connected to the very first incloud server
                        // (given on command line), then exit with error.
                        if (nameServer.getCloudStatus() == cMsgNameServer.NONCLOUD) {
if(methodDebug) System.out.println("    << JR: Cannot connect to given server: " + server + ", so continue");
                            System.out.println(e.getMessage());
                            continue;
                        }
                    }

                    // If there are servers that the server we are connecting to right now
                    // knows about that we don't (aren't in serversToConnectTo list or the
                    // bridges list), put 'em in the "unknownServers" list.
                    if (serverCandidates != null) {

                        top:
                        for (String s : serverCandidates) {
                            // find colon
                            int indexColon = s.lastIndexOf(":");
                            // find port
                            String sPort = s.substring(indexColon+1);
                            int  sport = cMsgNetworkConstants.nameServerTcpPort;
                            try {sport = Integer.parseInt(sPort);}
                            catch (NumberFormatException e) {/* should never happen */}
                            // find host name
                            String hostName = s.substring(0, indexColon);


                            // for a quick, string-based check use qualified & unqualified names
                            String alternateName;
                            int indexDot = s.indexOf(".");

                            // If the name has a dot (is qualified), create unqualified name
                            if (indexDot > -1) {
                                alternateName = hostName.substring(0, indexDot) + ":" + sPort;
if(methodDebug) System.out.println("    << JR: alternateName (unqualified) = " + alternateName);
                            }
                            // else create qualified name
                            else {
                                try {
                                    alternateName = InetAddress.getByName(hostName).getCanonicalHostName();
                                    alternateName = alternateName + ":" + sPort;
if(methodDebug) System.out.println("    << JR: alternateName (qualified) = " + alternateName);
                                }
                                catch (UnknownHostException e) {
                                    alternateName = s;
                                }
                            }

                            // if we already have this entry, look at next serverName
                            if (nameServer.bridges.keySet().contains(s) ||
                                nameServer.bridges.keySet().contains(alternateName) ||
                                serversToConnectTo.contains(s) ||
                                serversToConnectTo.contains(alternateName)) {
if(methodDebug) System.out.println("    << JR: Skip -> server " + s);
                                continue;
                            }

                            // make sure we aren't going to try connecting to ourself
                            // TODO: why is checking the host enough?! check port too??
                            if (cMsgUtilities.isHostLocal(hostName) && sport == nameServer.getPort()) {
if(methodDebug) System.out.println("    << JR: Skip myself -> server " + s);
                                continue;
                            }

                            // If we've made it this far, the server we're examining (s),
                            // isn't in any of our "already-connected-to" lists and it is
                            // not this server. It's a good candidate to be added. Here comes
                            // the tricky part. Some networks are setup in a WEIRD way. For
                            // example, the super computing cluster at the University of
                            // Richmond. There is one computer of this cluster visible to the
                            // outside world (quark.richmond.edu). Inside the cluster, this
                            // name is unresolved, but that same machine is seen as "head".
                            // The problem comes in when you tell a cMsg server on one of the
                            // non-head cluster nodes to join "quark.richmond.edu:45000" in a cloud.
                            // If you do a getCanonicalHostName()
                            // on this from such a machine on the cluster, it returns quark.richmond.edu
                            // since I believe it cannot resolve that name. However, on quark,
                            // the same call to getCanonicalHostName() returns "head". Here's what
                            // happens:
                            //
                            // The first cMsg server to join the cloud does not have a problem.
                            // It connects to quark (which somehow works). However, in the reciprocal
                            // connection, quark calls itself "head:45000". Now when the second cMsg
                            // server joins, it first joins quark, so far so good, and quark says that
                            // its also connected to "physics0:45000". But when it joins "physics0:45000",
                            // that server says I am also connected to "head:45000". It then tries to
                            // connect to head but it already is since it connected to quark. Thus this
                            // attempted connection fails.
                            //
                            // To try to remedy this, we'll look to see if we're already connected to
                            // a node by doing a more sophisticated comparison of node names.


                            // look in bridges
                            for (String serv : nameServer.bridges.keySet()) {
                                // Disect key: find colon
                                indexColon = serv.lastIndexOf(":");
                                // find port
                                String servPort = serv.substring(indexColon+1);
                                // find host name
                                String servHostName = serv.substring(0, indexColon);

                                // ports are different, so look at next item
                                if (!servPort.equals(sPort)) {
                                    continue;
                                }

                                // if host & port are the same, don't add, go to next serverName
                                if (cMsgUtilities.isHostSame(hostName, servHostName)) {
if(methodDebug) System.out.println("    << JR: Do NOT add server " + s + " since " +
                               hostName + " = " + servHostName + " (found in this bridges list)");
                                    continue top;
                                }
                            }


                            // look to see if it's already in our list of servers to connect to
                            for (String serv : serversToConnectTo) {
                                indexColon = serv.lastIndexOf(":");
                                String servPort = serv.substring(indexColon+1);
                                String servHostName = serv.substring(0, indexColon);
                                if (! servPort.equals(sPort)) {
                                    continue;
                                }
                                if (cMsgUtilities.isHostSame(hostName, servHostName)) {
if(methodDebug) System.out.println("    << JR: Do NOT add server " + s + " since " +
                              hostName + " = " + servHostName + " (found in this serversToConnectTo list)");
                                    continue top;
                                }
                            }

                            // look in name server's list of connected servers
                            for (String serv : nameServer.nameServers.keySet()) {
                                indexColon = serv.lastIndexOf(":");
                                String servPort = serv.substring(indexColon+1);
                                String servHostName = serv.substring(0, indexColon);
                                if (!servPort.equals(sPort)) {
                                    continue;
                                }
                                if (cMsgUtilities.isHostSame(hostName, servHostName)) {
if(methodDebug) System.out.println("    << JR: Do NOT add server " + s + " since " +
                                   hostName + " = " + servHostName + " (found in this server's list of server clients)");
                                    continue top;
                                }
                            }

if(methodDebug) System.out.println("    << JR: Added unknown server " + s + " to list (size = " + unknownServers.size() + ")");
                            unknownServers.add(s);

                        }
                    }
                }

                // we've connected to all known servers
                serversToConnectTo.clear();

                // now connect to the previously unknown servers
                serversToConnectTo.addAll(unknownServers);

if(methodDebug) System.out.println("    << JR: Size of serversToConnectTo map: " + serversToConnectTo.size());
            } while (serversToConnectTo.size() > 0);

            if (madeFirstConnection) {
                break;
            }
        }

        // nothing worked so we're NOT part of any cloud
        if (!madeFirstConnection) {
if(methodDebug) System.out.println("    << JR: Could not connect to any given server");
            // Set our new status as part of our own cloud
            nameServer.setCloudStatus(cMsgNameServer.INCLOUD);
            // Allow client and server connections
            nameServer.allowConnectionsCloudSignal.countDown();
            return;
        }

        int     grabLockTries;
        boolean gotSelfLock   = false;
        boolean gotCloudLock  = false;
        boolean amInCloud     = false;
        cMsgServerBridge firstLockBridge = null;
        int maxNumberOfTrys=3, numberOfTrys=0;
        ArrayList<cMsgServerBridge> lockedBridges = new ArrayList<cMsgServerBridge>();

if(methodDebug) System.out.println("    << JR: About to grab all cloudlocks");

    startOver:

        while (numberOfTrys++ < maxNumberOfTrys) {
if(methodDebug) System.out.println("    << JR: startOver");

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
            // Thus we grab our own lock (even though we're not part of
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
                nameServer.cloudLock();
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
                for (cMsgServerBridge bridge : nameServer.bridges.values()) {
if(methodDebug) System.out.println("    << JR: grabLockTries = " + grabLockTries);
if(methodDebug) System.out.println("    << JR: status = " + bridge.getCloudStatus() + ", bridge = " + bridge);
                    if (bridge.getCloudStatus() == cMsgNameServer.INCLOUD) {
                        try {
if(methodDebug) System.out.println("    << JR: try in-cloud lock");
                            if (bridge.cloudLock(200)) {
if(methodDebug) System.out.println("    << JR: first grabbed 1 cloud lock (for " + bridge.serverName + ")");
                                lockedBridges.add(bridge);
                                firstLockBridge = bridge;
                                gotCloudLock = true;
                                break;
                            }
                            else {
if(methodDebug) System.out.println("    << JR: failed to grab cloud lock (for " + bridge.serverName + ")");
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
if(methodDebug) System.out.println("    << JR: continue to startOver");
                        continue startOver;
                    }

                    try {Thread.sleep(10);}
                    catch (InterruptedException e) {}
                }
            } while (!gotCloudLock);

            // Calculate the majority
            int totalCloudMembers = 0;
            for (cMsgServerBridge bridge : nameServer.bridges.values()) {
                if (bridge.getCloudStatus() == cMsgNameServer.INCLOUD) {
                    totalCloudMembers++;
                }
            }
            int majority = totalCloudMembers / 2 + 1;
            int numberOfLockedCloudMembers = 1;

            // Try to get all of the in-cloud servers' locks
            do {
                // Grab the locks of ALL other servers
                for (cMsgServerBridge bridge : nameServer.bridges.values()) {

                    // If it's already locked, skip it
                    if (lockedBridges.contains(bridge)) {
if(methodDebug) System.out.println("    << JR: Already grabbed (so skip grabbing) cloud lock for " + bridge.serverName);
                        continue;
                    }

                    try {
                        // If sucessfull in locking remote server ...
if(methodDebug) System.out.println("    << JR: Try to cloud lock bridge to " + bridge.serverName);
                        // If we can lock remote server, add to list, else try next one
                        if (bridge.cloudLock(200)) {
if(methodDebug) System.out.println("    << JR: LOCKED IT!!");
                            lockedBridges.add(bridge);
                            if (bridge.getCloudStatus() == cMsgNameServer.INCLOUD) {
                                numberOfLockedCloudMembers++;
                            }
                        }
                    }
                    // We're here if lock or unlock fails in its communication with server.
                    // If we cannot talk to the server, it's probably dead.
                    catch (IOException e) {
                    }
                }

                // If we have all the in-cloud locks we're done and can move on.
                if (numberOfLockedCloudMembers >= totalCloudMembers) {
if(methodDebug) System.out.println("    << JR: Have all locks, move on");
                    break;
                }
                // If we have a majority (but not all) in-cloud locks, try to get the rest.
                else if (numberOfLockedCloudMembers >= majority) {
                    // Let other greedy lock-grabbers release theirs locks first
if(methodDebug) System.out.println("    << JR: Get More Locks");
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
                    nameServer.cloudUnlock();
                    gotSelfLock  = false;
                    gotCloudLock = false;

                    // Wait for a random time initially between 10 & 300
                    // milliseconds which doubles each loop.
                    Random rand = new Random();
                    int milliSec = (int) ((10 + rand.nextInt(291))*(Math.pow(2., numberOfTrys-1.)));
                    try {Thread.sleep(milliSec);}
                    catch (InterruptedException e) {}

                    // start over
if(methodDebug) System.out.println("    << JR: Drop locks and start over");
                    continue startOver;
                }

            } while (true);

            // Tell others we are now in the cloud
            nameServer.setCloudStatus(cMsgNameServer.INCLOUD);
            for (cMsgServerBridge bridge : nameServer.bridges.values()) {
if(methodDebug) System.out.println("    << JR: Tell server we are in the cloud");
                try {
                    bridge.thisServerCloudStatus(cMsgNameServer.INCLOUD);
                }
                catch (IOException e) {
                    System.out.println("    << JR: Bombed while Telling server we are in the cloud");
                }
            }

            // release the locks
            //for (cMsgServerBridge bridge : nameServer.bridges.values()) {
            for (cMsgServerBridge bridge : lockedBridges) {
                try {
if(methodDebug) System.out.println("    << JR: Try unlocking cloud lock for " + bridge.serverName);
                    bridge.cloudUnlock();
if(methodDebug) System.out.println("    << JR: UNLOCKED cloud lock for " + bridge.serverName);
                }
                catch (IOException e) {
                    e.printStackTrace();
                }
            }
if(methodDebug) System.out.println("    << JR: Try unlocking cloud lock for this server");
            nameServer.cloudUnlock();
if(methodDebug) System.out.println("    << JR: Unlocked cloud lock for this server");

if(methodDebug) System.out.println("    << JR: I'm in the cloud\n\n");
            amInCloud = true;
            break;
        }

        // If we could NOT join the cloud for some reason ...
        if (!amInCloud) {
            // not really necessary since we're going to exit anyway
            if (gotSelfLock) {
                nameServer.cloudUnlock();
            }
            // need to do this, however
            if (gotCloudLock) {
                try {firstLockBridge.cloudUnlock();}
                catch (IOException e) {}
            }
            System.out.println("    << JR: *******************************************************************");
            System.out.println("    << JR: Cannot join the specified cloud, becoming the center of a new cloud");
            System.out.println("    << JR: *******************************************************************");
            //System.exit(-1);
        }

        // Set our new status as part of the cloud
        nameServer.setCloudStatus(cMsgNameServer.INCLOUD);

        // Allow client and server connections
        nameServer.allowConnectionsCloudSignal.countDown();

    }

}

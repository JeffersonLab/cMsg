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

package org.jlab.coda.cMsg.cMsgDomain.server;

import java.io.*;
import java.net.*;
import java.lang.*;
import java.util.*;
import java.util.concurrent.locks.ReentrantLock;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ConcurrentHashMap;
import java.nio.channels.Selector;
import java.nio.channels.SelectionKey;
import java.nio.channels.ServerSocketChannel;
import java.nio.channels.SocketChannel;
import java.nio.ByteBuffer;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.cMsgNetworkConstants;
import org.jlab.coda.cMsg.remoteExec.IExecutorThread;
import org.jlab.coda.cMsg.common.cMsgSubdomainInterface;
import org.jlab.coda.cMsg.cMsgDomain.subdomains.cMsgMessageDeliverer;

/**
 * This class implements a cMsg name server in the cMsg domain.
 * A word of caution. If multiple cMsgNameServer objects exist in
 * a single JVM and they both service clients in the cMsg subdomain,
 * then there will be undesirable effects. In other words, the
 * cMsg subdomain uses static data in some of its implementing
 * classes ({@link cMsgServerBridge} & {@link org.jlab.coda.cMsg.cMsgDomain.subdomains.cMsg}).
 *
 * @author Carl Timmer
 * @version 1.0
 */
public class cMsgNameServer extends Thread implements IExecutorThread {

    /** This server's name. */
    String serverName;

    /** Host this server is running on. */
    private String host;

    /** This server's TCP listening port number. */
    private int port;

    /** This server's UDP listening port number for receiving multicasts. */
    private int multicastPort;

    /** The maximum number of clients to be serviced simultaneously by a cMsgDomainServerSelect object. */
    private int clientsMax = 10;

    /** The maximum value for the cMsgDomainServer's listening port number. */
    static int domainPortMax = 65535;

    /** The value of the TCP listening port for establishing permanent client connections. */
    static int domainServerPort;

    /** Server channel (contains socket). */
    private ServerSocketChannel serverChannel;

    /** UDP socket on which to read multicast packets sent from cMsg clients. */
    private MulticastSocket multicastSocket;

    /** Thread which receives client multicasts. */
    private cMsgMulticastListeningThread multicastThread;

    /** Thread which handles the permanent client connections. */
    private cMsgConnectionHandler connectionHandler;

    /**
     * Threads which monitor the health of clients. It also sends and receives
     * general monitoring information.
     */
    private cMsgMonitorClient monitorHandler;

    /**
     * There are 3 threads which must be running before client connections are allowed.
     * Use this object to signal the point at which both of these threads have been
     * successfully started (during {@link #startServer()}) .
     */
    CountDownLatch preConnectionThreadsStartedSignal = new CountDownLatch(3);

    /**
     * There are 2 threads which must be running before client connections are allowed.
     * Use this object to signal the point at which both of these threads have been
     * successfully started (during {@link #startServer()}) .
     */
    CountDownLatch postConnectionThreadsStartedSignal = new CountDownLatch(2);

    /**
     * Set of all active cMsgDomainServer objects. It is implemented as a HashMap
     * but that is only to take advantage of the concurrency protection.
     * The values are all null. This set will be used to call the active domain servers'
     * handleServerShutdown methods when this name server is being
     * shutdown or also when creating monitoring data strings.
     */
    ConcurrentHashMap<cMsgDomainServer,String> domainServers;

    /**
     * Set of all active cMsgDomainServerSelect objects. It is implemented as a HashMap
     * but that is only to take advantage of the concurrency protection.
     * The values are all null. This set will be used to call the active domain servers'
     * handleServerShutdown methods when this name server is being
     * shutdown or also when creating monitoring data strings.
     */
    ConcurrentHashMap<cMsgDomainServerSelect,String> domainServersSelect;

    /**
     * List of all cMsgDomainServerSelect objects that have room for more clients.
     */
    List<cMsgDomainServerSelect> availableDomainServers;

    /** Level of debug output for this class. */
    private int debug;

    /**
     * Set of all subscriptions (including the subAndGets) of all clients
     * on this server. This is mutex protected by {@link #subscribeLock}.
     */
    HashSet<cMsgServerSubscribeInfo> subscriptions =
            new HashSet<cMsgServerSubscribeInfo>(100);

    /** Lock to ensure all access to {@link #subscriptions} is sequential. */
    final ReentrantLock subscribeLock = new ReentrantLock();

    /** Tell the server to kill spawned threads. */
    private boolean killAllThreads;

    /**
     * Sets boolean to kill this and all spawned threads.
     * @param b setting to true will kill this and all spawned threads
     */
    private void setKillAllThreads(boolean b) {killAllThreads = b;}

    /**
     * Gets boolean value specifying whether to kill this and all spawned threads.
     * @return boolean value specifying whether to kill this and all spawned threads
     */
    private boolean getKillAllThreads() {return killAllThreads;}

    /**
     * List of all ClientHandler objects. This list is used to
     * end these threads nicely during a shutdown.
     */
    private ArrayList<ClientHandler> handlerThreads;

    /** String which contains the entire monitor data of the server cloud (xml format). */
    String fullMonitorXML;

    /** String which contains the monitor data of this particular name server (xml format). */
    String nsMonitorXML;

    /** If true, this server does NOT send an XML string containing its state to all clients. */
    boolean monitoringOff;

    /**
     * Password that clients need to match before being allowed to connect.
     * This is subdomain independent and applies to the server as a whole.
     */
    String clientPassword;

    //--------------------------------------------------------
    // The following class members are associated with the
    // server-to-server operation of the cMsg subdomain.
    //--------------------------------------------------------

    /**
     * Password that this server needs to join a cloud and the password that
     * another server needs to join this one. This is cMsg subdomain specific.
     */
    String cloudPassword;

    /**
     * Does this server stand alone and NOT allow bridges
     * to/from other cMsg subdomain servers?
     */
    boolean standAlone;

    /**
     * cMsg server whose cloud this server is to be joined to.
     * It is in the form <host>:port .
     */
    private String serverToJoin;

    /** Server this name server is in the middle of or starting to connect to. */
    volatile cMsgServerBridge bridgeBeingCreated;

    /**
     * Use this to signal that this server's listening threads have been started
     * so bridges may be created and clients may connect.
     */
    CountDownLatch listeningThreadsStartedSignal = new CountDownLatch(2);

    /**
     * Use this to signal the point at which other servers and clients
     * are allowed to connect to this server due to server cloud issues.
     */
    CountDownLatch allowConnectionsCloudSignal = new CountDownLatch(1);

    /** Server is in the server cloud. */
    static final byte INCLOUD  = 0;
    /** Server is NOT in the server cloud. */
    static final byte NONCLOUD = 1;
    /** Server is in the process of becoming a part of the server cloud. */
    static final byte BECOMINGCLOUD = 2;
    /** Server cloud status is unknown. */
    static final byte UNKNOWNCLOUD = 3;

    /**
     * Keep track of all name servers which have connected to this server.
     * This hashmap stores the server name (host:port) as the index and the
     * cMsgClientData object as the value.
     */
    ConcurrentHashMap<String, cMsgClientData> nameServers =
            new ConcurrentHashMap<String, cMsgClientData>(20);

    /**
     * Keep track of all the cMsgServerBridge objects in the cMsg subdomain.
     * A bridge is a connection from this cMsg server to another.
     * The server name (host:port) is the key and the bridge object is the value.
     */
    ConcurrentHashMap<String, cMsgServerBridge> bridges =
            new ConcurrentHashMap<String, cMsgServerBridge>(20);

    /**
     * This value tells the relationship of this server to the cloud.
     * The value may be one of {@link #INCLOUD}, {@link #NONCLOUD},
     * or {@link #BECOMINGCLOUD}. It may only be used/changed when
     * the cloudLock is locked.
     */
    private int cloudStatus = NONCLOUD;

    /**
     * Lock to ensure that servers are added to the server cloud one-at-a-time
     * and to ensure that clients are added to servers one-at-a-time as well.
     * This is used only in the cMsg subdomain.
     */
    private ReentrantLock cloudLock = new ReentrantLock();

    /**
     * This method locks a lock used in adding servers to the server cloud and in adding
     * clients to servers. This is used only in the cMsg subdomain.
     */
    void cloudLock() {
//System.out.println(">> NS: try to lock cloud (blocking)");
        cloudLock.lock();
    }

    /**
     * This method locks a lock used in adding servers to the server cloud and in adding
     * clients to servers. This is used only in the cMsg subdomain.
     * @param delay time in milliseconds to wait for the lock before timing out
     * @return true if locked, false otherwise
     */
    boolean cloudLock(int delay) {
        try {
//System.out.println(">> NS: try to lock cloud (timeout = " + delay + " ms)");
            return cloudLock.tryLock(delay, TimeUnit.MILLISECONDS);
        }
        catch (InterruptedException e) {
            return false;
        }
    }

    /**
     * This method unlocks a lock used in adding servers to the server cloud and in adding
     * clients to servers. This is used only in the cMsg subdomain.
     */
    void cloudUnlock() {
//System.out.println(">> NS: try to unlock cloud");
        cloudLock.unlock();
//System.out.println(">> NS: unlocked cloud");
    }

    /**
     * Get the status of the relationship of this server to the cMsg subdomain
     * server cloud.
     * @return status which is one of {@link #INCLOUD}, {@link #NONCLOUD},
     *         or {@link #BECOMINGCLOUD}
     */
    public int getCloudStatus() {
        return cloudStatus;
    }

    /**
     * Set the status of the relationship of this server to the cMsg subdomain
     * server cloud. The status may only be one of {@link #INCLOUD}, {@link #NONCLOUD},
     * or {@link #BECOMINGCLOUD}.
     * @param status status of the relationship of this server to the cMsg subdomain
     *               server cloud
     */
    void setCloudStatus(int status) {
        if ((status != INCLOUD) &&
            (status != NONCLOUD) &&
            (status != BECOMINGCLOUD)) {
            return;
        }
        cloudStatus = status;
    }

    //--------------------------------------------------------
    //--------------------------------------------------------

    /**
     * Get this server's name (host:port).
     * @return server's name
     */
    public String getServerName() {
         return serverName;
     }


    /**
     * Get the host this server is running on.
     * @return server's name
     */
    public String getHost() {
         return host;
     }


    /**
     * Get the name server's listening port.
     * @return name server's listening port
     */
    public int getPort() {
        return port;
    }


    /**
     * Get the domain server's listening port.
     * @return domain server's listening port
     */
    public int getDomainPort() {
        return domainServerPort;
    }


    /**
     * Get name server's multicast listening port.
     * @return name server's multicast listening port
     */
    public int getMulticastPort() {
        return multicastPort;
    }


    /**
     * Constructor which reads environmental variables and opens listening sockets.
     *
     * @param port TCP listening port for communication from clients
     * @param domainPort  listening port for receiving 2 permanent connections from each client
     * @param udpPort UDP listening port for receiving multicasts from clients
     * @param standAlone   if true no other cMsg servers are allowed to attached to this one and form a cloud
     * @param monitoringOff if true clients are NOT sent monitoring data
     * @param clientPassword password client needs to provide to connect to this server
     * @param cloudPassword  password server needs to provide to connect to this server to become part of a cloud
     * @param serverToJoin server whose cloud this server is to be joined to
     * @param debug desired level of debug output
     * @param clientsMax max number of clients per cMsgDomainServerSelect object for regime = low
     */
    public cMsgNameServer(int port, int domainPort, int udpPort,
                          boolean standAlone, boolean monitoringOff,
                          String clientPassword, String cloudPassword, String serverToJoin,
                          int debug, int clientsMax) {

        domainServers        = new ConcurrentHashMap<cMsgDomainServer,String>(20);
        domainServersSelect  = new ConcurrentHashMap<cMsgDomainServerSelect,String>(20);
        handlerThreads = new ArrayList<ClientHandler>(10);
        availableDomainServers = Collections.synchronizedList(new LinkedList<cMsgDomainServerSelect>());   // CHANGED

        this.debug          = debug;
        this.clientsMax     = clientsMax;
        this.standAlone     = standAlone;
        this.monitoringOff  = monitoringOff;
        this.cloudPassword  = cloudPassword;
        this.clientPassword = clientPassword;
        this.serverToJoin   = serverToJoin;
        if (standAlone) {
            if (serverToJoin != null) {
                System.out.println("\nCannot have \"serverToJoin != null\" and \"standalone = true\" simultaneously");
                System.exit(-1);
            }
        }

        // read env variable for domain server port number
        if (domainPort < 1) {
            try {
                String env = System.getenv("CMSG_DOMAIN_PORT");
                if (env != null) {
                    domainPort = Integer.parseInt(env);
                }
            }
            catch (NumberFormatException ex) {
                System.out.println("Bad port number specified in CMSG_DOMAIN_PORT env variable");
                System.exit(-1);
            }
        }

        if (domainPort < 1) {
            domainPort = cMsgNetworkConstants.domainServerPort;
        }
        domainServerPort = domainPort;

        // port #'s < 1024 are reserved
        if (domainServerPort < 1024) {
            System.out.println("Domain server port number must be > 1023");
            System.exit(-1);
        }

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println(">> NS: Domain server port = " + domainServerPort);
        }

        // read env variable for starting (desired) port number
        if (port < 1) {
            try {
                String env = System.getenv("CMSG_PORT");
                if (env != null) {
                    port = Integer.parseInt(env);
                }
            }
            catch (NumberFormatException ex) {
                System.out.println("Bad port number specified in CMSG_PORT env variable");
                System.exit(-1);
            }
        }

        if (port < 1) {
            port = cMsgNetworkConstants.nameServerTcpPort;
        }

        // port #'s < 1024 are reserved
        if (port < 1024) {
            System.out.println("Port number must be > 1023");
            System.exit(-1);
        }

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println(">> NS: Name server TCP port = " + port);
        }

        // read env variable for starting (desired) UDP port number
        if (udpPort < 1) {
            try {
                String env = System.getenv("CMSG_MULTICAST_PORT");
                if (env != null) {
                    udpPort = Integer.parseInt(env);
                }
            }
            catch (NumberFormatException ex) {
                System.out.println("Bad port number specified in CMSG_MULTICAST_PORT env variable");
                ex.printStackTrace();
                System.exit(-1);
            }
        }

        if (udpPort < 1) {
            udpPort = cMsgNetworkConstants.nameServerUdpPort;
        }

        // port #'s < 1024 are reserved
        if (udpPort < 1024) {
            System.out.println("\nPort number must be > 1023");
            System.exit(-1);
        }

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println(">> NS: Name server UDP port = " + udpPort);
        }

        // At this point, bind to the TCP listening port. If that isn't possible, throw
        // an exception.
        try {
            serverChannel = ServerSocketChannel.open();
        }
        catch (IOException ex) {
            System.out.println("Exiting Server: cannot open a listening socket");
            ex.printStackTrace();
            System.exit(-1);
        }

        ServerSocket listeningSocket = serverChannel.socket();

        try {
            listeningSocket.setReuseAddress(true);
            // prefer low latency, short connection times, and high bandwidth in that order
            listeningSocket.setPerformancePreferences(1,2,0);
//System.out.println("Listening socket binding to port " + port);
            listeningSocket.bind(new InetSocketAddress(port));
        }
        catch (IOException ex) {
            System.out.println("TCP port number " + port + " in use.");
            ex.printStackTrace();
            System.exit(-1);
        }

        this.port = port;

        // Create a UDP socket for accepting multicasts from cMsg clients
        try {
            // create multicast socket to receive at all interfaces

            // Lots of bugs with multicast sockets:
            //   http://bugs.sun.com/bugdatabase/view_bug.do?bug_id=4701650
            //   http://wiki.jboss.org/wiki/CrossTalking
            // Because there are so many problems with underlying operating systems,
            // do NOT bind to the multicast address, only bind to the port.
            // Practically that means other multicasts, broadcasts or unicasts to
            // that port will get through. We'll have to implement our own filter.
            multicastSocket = new MulticastSocket(udpPort);
            multicastSocket.setReceiveBufferSize(65535);
            multicastSocket.setReuseAddress(true);
            multicastSocket.setTimeToLive(32);
//            multicastSocket.bind(new InetSocketAddress(udpPort));
            // If using standalone laptop, joinGroup throws an exception:
            // java.net.SocketException: No such device. This catch allows
            // usage with messed up / nonexistant network.
            // Be sure to join the multicast addr group on each interface
            // (something not mentioned in any javadocs or books!).
            SocketAddress sa;
            Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();
            while (enumer.hasMoreElements()) {
                try {
                    sa = new InetSocketAddress(InetAddress.getByName(cMsgNetworkConstants.cMsgMulticast), udpPort);
                    multicastSocket.joinGroup(sa, enumer.nextElement());
                }
                catch (IOException e) {/* cannot join multicast group cause network messed up */}
            }

//            try {
//                multicastSocket.joinGroup(InetAddress.getByName(cMsgNetworkConstants.cMsgMulticast));
//            } catch (IOException e) {
//                System.out.println("Cannot join multicast group cause network messed up");
//            }
//System.out.println("Created UDP multicast listening socket on all interfaces at port " + udpPort);
        }
        catch (IOException e) {
            System.out.println("UDP port number " + udpPort + " in use.");
            e.printStackTrace();
            System.exit(-1);
        }
        multicastPort = udpPort;

        // record our own name
        try {
            serverName = InetAddress.getLocalHost().getCanonicalHostName();
        }
        catch (UnknownHostException e) {
        }
        serverName = serverName + ":" + port;

        // Host this is running on
        try {
            host = InetAddress.getLocalHost().getCanonicalHostName();
        }
        catch (UnknownHostException ex) {}
    }


    /** Method to print out correct program command line usage. */
    private static void usage() {
        System.out.println("\nUsage: java [-Dport=<tcp listening port>]\n"+
                             "            [-DdomainPort=<domain server listening port>]\n" +
                             "            [-Dudp=<udp listening port>]\n" +
                             "            [-DsubdomainName=<className>]\n" +
                             "            [-Dserver=<hostname:serverport>]\n" +
                             "            [-Ddebug=<level>]\n" +
                             "            [-Dstandalone]\n" +
                             "            [-DmonitorOff]\n" +
                             "            [-Dpassword=<password>]\n" +
                             "            [-Dcloudpassword=<password>]\n" +
                             "            [-DlowRegimeSize=<size>]  cMsgNameServer\n");
        System.out.println("       port           is the TCP port this server listens on");
        System.out.println("       domainPort     is the TCP port this server listens on for connection to domain server");
        System.out.println("       udp            is the UDP port this server listens on for multicasts");
        System.out.println("       subdomainName  is the name of a subdomain and className is the");
        System.out.println("                      name of the java class used to implement the subdomain");
        System.out.println("       server         punctuation (not colon) or white space separated list of servers\n" +
                           "                      in host:port format to connect to in order to gain entry to cloud\n" +
                           "                      of servers. First successful connection used. If no connections made,\n" +
                           "                      no error indicated.");
        System.out.println("       debug          debug output level has acceptable values of:");
        System.out.println("                          info   for full output");
        System.out.println("                          warn   for severity of warning or greater");
        System.out.println("                          error  for severity of error or greater");
        System.out.println("                          severe for severity of \"cannot go on\"");
        System.out.println("                          none   for no debug output (default)");
        System.out.println("       standalone     means no other servers may connect or vice versa,");
        System.out.println("                      is incompatible with \"server\" option");
        System.out.println("       monitorOff     means montoring data is NOT sent to client,");
        System.out.println("       password       is used to block clients without this password in their UDL's");
        System.out.println("       cloudpassword  is used to join a password-protected cloud or to allow");
        System.out.println("                      servers with this password to join this cloud");
        System.out.println("       lowRegimeSize  for clients of \"regime=low\" type, this sets the number of");
        System.out.println("                      clients serviced by a single thread");
        System.out.println();
    }


    /** This method prints out the sizes of all objects which store other objects. */
    public void printSizes() {
        System.out.println("     bridges        = " + bridges.size());
        System.out.println("     nameservers    = " + nameServers.size());
        System.out.println("     domainservers  = " + domainServers.size());
        System.out.println("     subscriptions  = " + subscriptions.size());
        System.out.println("     handlerThreads = " + handlerThreads.size());

        System.out.println();

        for (cMsgServerBridge b : bridges.values()) {
            System.out.println("       bridge " + b.serverName + ":");
            b.printSizes();
        }

        if (bridges.size() > 0) {
            System.out.println();
        }
    }


    /** Run as a stand-alone application. */
    public static void main(String[] args) {

        int debug = cMsgConstants.debugNone;
        int port = 0, udpPort = 0, domainPort=0;
        int clientsMax = cMsgConstants.regimeLowMaxClients;
        boolean standAlone    = false;
        boolean monitorOff    = false;
        String serverToJoin   = null;
        String cloudPassword  = null;
        String clientPassword = null;

        if (args.length > 0) {
            usage();
            System.exit(-1);
        }

        // First check to see if debug level, port number, or timeordering
        // was set on the command line. This can be done, while ignoring case,
        // by scanning through all the properties.
        for (Iterator i = System.getProperties().keySet().iterator(); i.hasNext();) {
            String s = (String) i.next();

            if (s.contains(".")) {
                continue;
            }

            if (s.equalsIgnoreCase("port")) {
                try {
                    port = Integer.parseInt(System.getProperty(s));
                }
                catch (NumberFormatException e) {
                    System.out.println("\nBad port number specified");
                    usage();
                    e.printStackTrace();
                    System.exit(-1);
                }
            }
            if (s.equalsIgnoreCase("udp")) {
                try {
                    udpPort = Integer.parseInt(System.getProperty(s));
                }
                catch (NumberFormatException e) {
                    System.out.println("\nBad port number specified");
                    usage();
                    e.printStackTrace();
                    System.exit(-1);
                }
            }
            if (s.equalsIgnoreCase("domainPort")) {
                try {
                    domainPort = Integer.parseInt(System.getProperty(s));
                }
                catch (NumberFormatException e) {
                    System.out.println("\nBad domain server port number specified");
                    usage();
                    e.printStackTrace();
                    System.exit(-1);
                }
                if (domainPort > 65535 || domainPort < 1024) {
                    System.out.println("\nBad domain server port number specified");
                    usage();
                    System.exit(-1);
                }
            }
            if (s.equalsIgnoreCase("lowRegimeSize")) {
                try {
                    clientsMax = Integer.parseInt(System.getProperty(s));
//System.out.println("Set clientsMax to " + clientsMax);
                }
                catch (NumberFormatException e) {
                    System.out.println("\nBad maximum number of clients serviced by single thread in low regime");
                }
                if (clientsMax > 100 || clientsMax < 2) {
                    System.out.println("\nBad maximum number of clients serviced by single thread in low regime");
                }
            }
            else if (s.equalsIgnoreCase("debug")) {
                String arg = System.getProperty(s);

                if (arg.equalsIgnoreCase("info")) {
                    debug = cMsgConstants.debugInfo;
                }
                else if (arg.equalsIgnoreCase("warn")) {
                    debug = cMsgConstants.debugWarn;
                }
                else if (arg.equalsIgnoreCase("error")) {
                    debug = cMsgConstants.debugError;
                }
                else if (arg.equalsIgnoreCase("severe")) {
                    debug = cMsgConstants.debugSevere;
                }
                else if (arg.equalsIgnoreCase("none")) {
                    debug = cMsgConstants.debugNone;
                }
                else {
                    System.out.println("\nBad debug value");
                    usage();
                    System.exit(-1);
                }
            }
            else if (s.equalsIgnoreCase("server")) {
                if (standAlone) {
                    System.out.println("\nCannot specify \"server\" and \"standalone\" options simultaneously");
                    usage();
                    System.exit(-1);
                }
                serverToJoin = System.getProperty(s);
            }
            else if (s.equalsIgnoreCase("standalone")) {
                if (serverToJoin != null) {
                    System.out.println("\nCannot specify \"server\" and \"standalone\" options simultaneously");
                    usage();
                    System.exit(-1);
                }
                standAlone = true;
            }
            else if (s.equalsIgnoreCase("monitorOff")) {
                monitorOff = true;
            }
            else if (s.equalsIgnoreCase("cloudpassword")) {
                cloudPassword = System.getProperty(s);
            }
            else if (s.equalsIgnoreCase("password")) {
                clientPassword = System.getProperty(s);
            }
        }

        // create server object
        cMsgNameServer server = new cMsgNameServer(port, domainPort, udpPort,
                                                   standAlone, monitorOff,
                                                   clientPassword, cloudPassword, serverToJoin,
                                                   debug, clientsMax);

        // start server
        server.startServer();
    }


    //-------------------------------------------------------------------
    // IExecutor Interface methods so it can be started/stopped remotely
    //-------------------------------------------------------------------
    public void startItUp() {
        startServer();
    }

    public void shutItDown() {
        shutdown();
    }

    public void waitUntilDone() throws InterruptedException {
        join();
    }
    //-------------------------------------------------------------


    /**
     * Method to start up this server and join the cMsg server cloud that
     * serverToJoin is a part of. If the serverToJoin is null, this
     * server is the nucleas of a new server cloud.
     */
    public void startServer() {

        // Start thread to gather monitor info
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println(">> NS: Start keepalive/monitoring thread ");
        }
        
        try {
            monitorHandler = new cMsgMonitorClient(this, debug);
            monitorHandler.start();
        }
        catch (IOException e) {
            // cannot start up selector for reading of monitoring data
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println(">> NS: Cannot start up selector for reading monitoring data");
            }
            shutdown();
            return;
        }

        // Start domain server connection thread
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println(">> NS: Start connection handling thread");
        }
        connectionHandler = new cMsgConnectionHandler(this, debug);
        connectionHandler.start();

        // Wait until these 2 threads have successfully started before continuing,
        // otherwise we may have a race condition where a client connecting while
        // these threads are still being started.
        try {
            boolean threadsStarted = preConnectionThreadsStartedSignal.await(20L, TimeUnit.SECONDS);
            if (!threadsStarted) {
                System.out.println(">> **** cMsg server NOT started due to theads taking too long to start **** <<");
                shutdown();
                return;
            }
        }
        catch (InterruptedException e) {
            System.out.println(">> **** cMsg server NOT started due to interrupt **** <<");
            shutdown();
            return;
        }

        // start this name server accepting client connections
        start();

        if (standAlone) {
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println(">> NS: Running in stand-alone mode");
            }
            // if we're not joining a cloud, and we're not letting anyone join us
            cloudStatus = NONCLOUD;
            // allow client connections
            allowConnectionsCloudSignal.countDown();
        }
        // Create a bridge to another server (if specified) which
        // will also generate a connection to this server from that
        // server in response.
        else if (serverToJoin != null) {
            // This string is a punc (not colon) or white space separated list of servers
            // to try to connect to in order to gain entry to a cloud of servers.
            // The first successful connection is used. If no connections are made,
            // no error is indicated.
            // First make sure that each given server name is of the right format (host:port)
            LinkedHashSet<String> serverSet = new LinkedHashSet<String>();
            String[] strs = serverToJoin.split("[\\p{Punct}\\p{Space}&&[^:\\.]]");
            for (String s : strs) {
                if (s.length() < 1) continue;
                try {
                    serverSet.add(cMsgUtilities.constructServerName(s));
                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println(">> NS: Joined server " + s + " in a cloud");
                    }
                }
                catch (cMsgException e) {
System.out.println("Server to join not in \"host:port\" format:\n" + e.getMessage());
                }
            }

            if (serverSet.size() < 1) {
                // since we're not joining a cloud, then we're by definition the nucleas of one
                cloudStatus = INCLOUD;
                // allow client connections
                allowConnectionsCloudSignal.countDown();
            }
            else {
                new cMsgServerCloudJoiner(this, port, multicastPort, serverSet, debug);
            }
        }
        else {
            // if we're not joining a cloud, then we're by definition the nucleas of one
            cloudStatus = INCLOUD;
            // allow client connections
            allowConnectionsCloudSignal.countDown();
        }

        // start UDP listening thread for multicasters trying to connect
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println(">> NS: Start multicast thd on port "  + multicastPort);
        }
        multicastThread = new cMsgMulticastListeningThread(this, port, multicastPort, multicastSocket,
                                                           clientPassword, debug);
        multicastThread.start();

        // Wait until these 2 listening threads have successfully started before continuing.
        // The Afecs platform starts a cmsg name server and immediately does a connect to it
        // in the same JVM. In such cases we need to avoid a race condition whereby a client
        // connects while these threads are still being started.
        try {
            boolean threadsStarted = listeningThreadsStartedSignal.await(20L, TimeUnit.SECONDS);
            if (!threadsStarted) {
                System.out.println(">> **** cMsg server NOT started due to listening theads taking too long to start **** <<");
                shutdown();
                return;
            }
        }
        catch (InterruptedException e) {
            System.out.println(">> **** cMsg server NOT started due to interrupt **** <<");
            shutdown();
            return;
        }

        // next line in by Elliott Wolin's request
System.out.println(">> **** cMsg server sucessfully started at " + (new Date()) + " **** <<");
    }


    /**
     * Implement IExecutorThread interface so cMsgNameServer can be
     * run using the Commander/Executor framework.
     */
    public void cleanUp() {
        shutdown();
    }

    /**
     * Method to gracefully shutdown all threads associated with this server
     * and to clean up.
     */
    synchronized void shutdown() {
        // stop cloud joiners
        cloudLock();

        // Shutdown this object's listening thread
        setKillAllThreads(true);

        // Shutdown UDP listening thread
        multicastThread.killThread();
        // Shutdown connecting new clients thread
        connectionHandler.killThread();
        // Shutdown monitoring clients threads
        monitorHandler.shutdown();

        // Shutdown all domain servers
        for (cMsgDomainServer server : domainServers.keySet()) {
            // need to shutdown this domain server
            if (server.calledShutdown.compareAndSet(false, true)) {
//System.out.println("DS shutdown to be run by NameServer");
                server.shutdown();
            }
        }

        // Shutdown all domain servers Select
        for (cMsgDomainServerSelect server : domainServersSelect.keySet()) {
            // need to shutdown this domain server
            if (server.calledShutdown.compareAndSet(false, true)) {
//System.out.println("DS shutdown to be run by NameServer");
                server.shutdown();
            }
        }

        cloudUnlock();

        bridges.clear();
        nameServers.clear();
        handlerThreads.clear();
        domainServers.clear();
        domainServersSelect.clear();
        subscriptions.clear();
    }


    /**
     * This method creates a particular subdomain's client handler object.
     *
     * @param subdomain subdomain for which to create client handler
     * @return client handler object
     * @throws cMsgException if no class was found or class could not be instantiated or
     *                       accessed
     */
    static private cMsgSubdomainInterface createClientHandler(String subdomain)
            throws cMsgException {

        // Object to handle clients' inputs
        cMsgSubdomainInterface clientHandler;

        String clientHandlerClass = null;

        // no one can mess with (substitute another class for) the cMsg subdomain
        if (subdomain.equalsIgnoreCase("cMsg")) {
            clientHandlerClass = "org.jlab.coda.cMsg.cMsgDomain.subdomains.cMsg";
        }

        // Check to see if handler class name was set on the command line.
        // Do this by scanning through all the properties.
        if (clientHandlerClass == null) {
            for (Iterator i = System.getProperties().keySet().iterator(); i.hasNext();) {
                String s = (String) i.next();
                if (s.contains(".")) {
                    continue;
                }
                if (s.equalsIgnoreCase(subdomain)) {
                    clientHandlerClass = System.getProperty(s);
                    break;
                }
            }
        }

        // If it wasn't given on the command line,
        // check the appropriate environmental variable.
        if (clientHandlerClass == null) {
            clientHandlerClass = System.getenv("CMSG_SUBDOMAIN");
        }

        // If there is still no handler class look in predefined classes.
        if (clientHandlerClass == null) {
            if (subdomain.equalsIgnoreCase("CA")) {
                clientHandlerClass = "org.jlab.coda.cMsg.cMsgDomain.subdomains.CA";
            }
            else if (subdomain.equalsIgnoreCase("Et")) {
                clientHandlerClass = "org.jlab.coda.cMsg.cMsgDomain.subdomains.Et";
            }
            else if (subdomain.equalsIgnoreCase("Database")) {
                clientHandlerClass = "org.jlab.coda.cMsg.cMsgDomain.subdomains.Database";
            }
            else if (subdomain.equalsIgnoreCase("LogFile")) {
                clientHandlerClass = "org.jlab.coda.cMsg.cMsgDomain.subdomains.LogFile";
            }
            else if (subdomain.equalsIgnoreCase("Queue")) {
                clientHandlerClass = "org.jlab.coda.cMsg.cMsgDomain.subdomains.Queue";
            }
            else if (subdomain.equalsIgnoreCase("SmartSockets")) {
                clientHandlerClass = "org.jlab.coda.cMsg.cMsgDomain.subdomains.SmartSockets";
            }
            else if (subdomain.equalsIgnoreCase("TcpServer")) {
                clientHandlerClass = "org.jlab.coda.cMsg.cMsgDomain.subdomains.TcpServer";
            }
            else if (subdomain.equalsIgnoreCase("FileQueue")) {
                clientHandlerClass = "org.jlab.coda.cMsg.cMsgDomain.subdomains.FileQueue";
            }
        }

        // all options are exhaused, throw error
        if (clientHandlerClass == null) {
            cMsgException ex = new cMsgException("no handler class found");
            ex.setReturnCode(cMsgConstants.errorNoClassFound);
            throw ex;
        }


        // Get handler class name and create handler object
        try {
            clientHandler = (cMsgSubdomainInterface) (Class.forName(clientHandlerClass).newInstance());
        }
        catch (InstantiationException e) {
            cMsgException ex = new cMsgException("cannot instantiate "+ clientHandlerClass +
                                                 " class");
            ex.setReturnCode(cMsgConstants.error);
            throw ex;
        }
        catch (IllegalAccessException e) {
            cMsgException ex = new cMsgException("cannot access "+ clientHandlerClass +
                                                 " class");
            ex.setReturnCode(cMsgConstants.error);
            throw ex;
        }
        catch (ClassNotFoundException e) {
            cMsgException ex = new cMsgException("no handler class found");
            ex.setReturnCode(cMsgConstants.errorNoClassFound);
            throw ex;
        }

        return clientHandler;
    }


    /** This method is executed as a thread. */
    public void run() {
        if (debug >= cMsgConstants.debugInfo) {
            System.out.println(">> NS: Running Name Server at " + (new Date()) );
        }

        // For mismatch in magic numbers (or old cMsg versions 1 and 2)
        // a response so send it an error message.
        String s = "wrong cMsg version";
        int len = 8 + s.length();
        byte[] respond = null;
        ByteArrayOutputStream baos = new ByteArrayOutputStream(len);
        DataOutputStream out       = new DataOutputStream(baos);

        try {
            // Put our error code, string len, and string into byte array
            out.writeInt(cMsgConstants.error);
            out.writeInt(s.length());
            try {
                out.write(s.getBytes("US-ASCII"));
            }
            catch (UnsupportedEncodingException e) { }
            out.flush();
            out.close();

            // create buffer to send to bad client
            respond = baos.toByteArray();
            baos.close();
        }
        catch (IOException e) {  }

        /* Direct buffer for reading 3 magic ints with nonblocking IO. */
        int BYTES_TO_READ = 12;
        ByteBuffer buffer = ByteBuffer.allocateDirect(BYTES_TO_READ > respond.length ?
                                                      BYTES_TO_READ : respond.length);

        Selector selector = null;

        try {
            // get things ready for a select call
            selector = Selector.open();

            // set nonblocking mode for the listening socket
            serverChannel.configureBlocking(false);

            // register the channel with the selector for accepts
            serverChannel.register(selector, SelectionKey.OP_ACCEPT);

            // Tell whoever is waiting for this server to start, that
            // it has now started. Actually there is a slight race
            // condition as it is not actually started until the select
            // statement below is executed. But it'll be OK since the thread
            // which is waiting must first create a bridge to another
            // server who then must make a reciprocal connection to this
            // server (right here as a matter of fact).
            listeningThreadsStartedSignal.countDown();

            while (true) {
                // 3 second timeout
                int n = selector.select(3000);

                // if no channels (sockets) are ready, listen some more
                if (n == 0) {
                    // but first check to see if we've been commanded to die
                    if (getKillAllThreads()) {
                        return;
                    }
                    continue;
                }

                // get an iterator of selected keys (ready sockets)
                Iterator it = selector.selectedKeys().iterator();

                // look at each key
                keyLoop:
                while (it.hasNext()) {
                    SelectionKey key = (SelectionKey) it.next();

                    // is this a new connection coming in?
                    if (key.isValid() && key.isAcceptable()) {
                        ServerSocketChannel server = (ServerSocketChannel) key.channel();
                        // accept the connection from the client
                        SocketChannel channel = server.accept();

                        // Check to see if this is a legitimate cMsg client or some imposter.
                        // Don't want to block on read here since it may not be a cMsg client
                        // and may block forever - tying up the server.
                        int bytes, bytesRead=0, loops=0;
                        buffer.clear();
                        buffer.limit(BYTES_TO_READ);
                        channel.configureBlocking(false);

                        // read magic numbers
                        while (bytesRead < BYTES_TO_READ) {
//System.out.println("  try reading rest of Buffer");
//System.out.println("  Buffer capacity = " + buffer.capacity() + ", limit = " + buffer.limit()
//                    + ", position = " + buffer.position() );
                            bytes = channel.read(buffer);
                            // for End-of-stream ...
                            if (bytes == -1) {
//System.out.println("cMsgNameServer: closing bad channel");
                                channel.close();
                                it.remove();
                                continue keyLoop;
                            }
                            bytesRead += bytes;
//System.out.println("  bytes read = " + bytesRead);

                            // if we've read everything, look to see if it's sent the magic #s
                            if (bytesRead >= BYTES_TO_READ) {
                                buffer.flip();
                                int magic1 = buffer.getInt();
                                int magic2 = buffer.getInt();
                                int magic3 = buffer.getInt();
                                if (magic1 != cMsgNetworkConstants.magicNumbers[0] ||
                                    magic2 != cMsgNetworkConstants.magicNumbers[1] ||
                                    magic3 != cMsgNetworkConstants.magicNumbers[2])  {
//System.out.println("cMsgNameServer:  Magic numbers did NOT match, send response");
                                    // For old versions of cMsg (1 and 2), it's expecting a response
                                    buffer.clear();
                                    buffer.put(respond);
                                    buffer.flip();
                                    channel.write(buffer);
//System.out.println("cMsgNameServer: closing bad channel");
                                    channel.close();
                                    it.remove();
                                    continue keyLoop;
                                }
                            }
                            else {
                                // give client 10 loops (.1 sec) to send its stuff, else no deal
                                if (++loops > 10) {
                                    it.remove();
                                    continue keyLoop;
                                }
                                try { Thread.sleep(10); }
                                catch (InterruptedException e) { }
                            }
                        }

//System.out.println("cMsgNameServer:  Magic numbers did match");
                        // back to using streams
                        channel.configureBlocking(true);
                        // set socket options
                        Socket socket = channel.socket();
                        // Set tcpNoDelay so no packets are delayed
                        socket.setTcpNoDelay(true);
                        // set recv buffer size
                        socket.setReceiveBufferSize(4096);

                        // start up client handling thread & store reference
                        handlerThreads.add(new ClientHandler(channel));
//System.out.println("handlerThd size = " + handlerThreads.size());

                        if (debug >= cMsgConstants.debugInfo) {
                            System.out.println(">> NS: new connection");
                        }
                    }
                    // remove key from selected set since it's been handled
                    it.remove();
                }
            }
        }
        catch (IOException ex) {
            //if (debug >= cMsgConstants.debugError) {
System.out.println("Main server IO error");
                ex.printStackTrace();
            //}
        }
        finally {
            try {serverChannel.close();} catch (IOException e) {}
            try {selector.close();}      catch (IOException e) {}
        }

        if (debug >= cMsgConstants.debugInfo) {
            System.out.println("\n>> NS: Quitting Name Server");
        }
    }


    /** Class to handle a socket connection to the client of which there may be many. */
    private class ClientHandler extends Thread {
        /** Type of domain this is. */
        private String domain = "cMsg";

        /** Socket channel to client. */
        SocketChannel channel;

        /** Buffered input communication streams for efficiency. */
        DataInputStream  in;
        /** Buffered output communication streams for efficiency. */
        DataOutputStream out;

        cMsgClientData info;

        // The following members are associated with server clients.

        /** Does the client operate in a low, medium, or high throughput regime? */
        int regime;

        /** The relationship between the connecting server and the server cloud -
         * can be INCLOUD, NONCLOUD, or BECOMINGCLOUD.
         */
        byte connectingCloudStatus;

        /** Is connecting server client originating connection or is this a reciprocal one? */
        boolean isReciprocalConnection;

        /** TCP listening port of name server that server client is a part of. */
        int nsTcpPort;

        /** UDP multicast listening port of name server that server client is a part of. */
        int nsMulticastPort;

        /** Locally constructed name for server client. */
        String name;

        /** Server client's given cloud password. */
        String myCloudPassword;

        /** Server client's given client password. */
        String myClientPassword;

        /** Server client's host. */
        String clientHost;

        /**
         * Constructor.
         * @param channel socket channel to client
         */
        ClientHandler(SocketChannel channel) {
            this.channel = channel;
            this.start();
        }


        /**
          * This method handles all communication between a cMsg user
          * and this name server for that domain.
          * Note to those who would make changes in the protocol, keep the first three
          * ints the same. That way the server can reliably check for mismatched versions.
          */
         public void run() {
//System.out.println("clientHandler; IN");

            try {
                // buffered communication streams for efficiency
                in  = new DataInputStream(new BufferedInputStream(channel.socket().getInputStream(), 4096));
                out = new DataOutputStream(new BufferedOutputStream(channel.socket().getOutputStream(), 2048));

                // message id
                int msgId = in.readInt();
                // major version
                int version = in.readInt();
                // minor version
                int minorVersion = in.readInt();

                // immediately check if this domain server is different cMsg version than client
                if (version != cMsgConstants.version) {
                    // send error to client
                    out.writeInt(cMsgConstants.errorDifferentVersion);
                    // send error string to client
                    String s = "version mismatch";
                    out.writeInt(s.length());
                    try { out.write(s.getBytes("US-ASCII")); }
                    catch (UnsupportedEncodingException e) {}

                    out.flush();
                    return;
                }

                switch(msgId) {
                    case cMsgConstants.msgConnectRequest:
                        handleRegularClient();
                        break;
                    case cMsgConstants.msgServerConnectRequest:
                        handleServerClient();
                        break;
                    default:
                        if (debug >= cMsgConstants.debugError) {
                            System.out.println("cMsg name server: can't understand your message -> " + msgId);
                        }
                        break;
                }
            }
            catch (IOException ex) {
                if (debug >= cMsgConstants.debugError) {
                    System.out.println("cMsgNameServer's Client thread: IO error in talking to client");
                }
            }
            finally {
                handlerThreads.remove(this);
                // we are done with the channel
                try {in.close();}      catch (IOException ex) {}
                try {out.close();}     catch (IOException ex) {}
                try {channel.close();} catch (IOException ex) {}
            }
         }


        /**
         * This method handles all communication between a cMsg server client
         * and this server in the cMsg domain / cMsg subdomain.
         *
         * @throws IOException  if problems with socket communication
         */
        private void handleServerClient() {

            try {
                readServerClientInfo();

                // At this point grab the "cloud" lock so no other servers
                // can connect simultaneously
                cloudLock.lock();

                try {
                    if (!allowServerClientConnection()) {
                        return;
                    }

                    // Create unique id number to send to client which it sends back in its
                    // reponse to this server's communication in order to identify itself.
                    int uniqueKey = connectionHandler.getUniqueKey();

                    // Create object which holds all data concerning server client
                    info = new cMsgClientData(name, nsTcpPort, nsMulticastPort,
                                              clientHost, myClientPassword, uniqueKey);

                    if (debug >= cMsgConstants.debugInfo) {
                        System.out.println(">> NS: try to register " + name);
                    }

                    // Register this client. If this cMsg server already has a
                    // client by this name (it never should), it will fail.
                    try {
                        registerServer();
                    }
                    catch (cMsgException ex) {
                        // Depending on where in "registerServer" the exception occurs,
                        // the client may or may not be able to receive the data sent below.
                        
                        // send int error code to client
                        out.writeInt(ex.getReturnCode());
                        // send error string to client
                        out.writeInt(ex.getMessage().length());
                        try {
                            out.write(ex.getMessage().getBytes("US-ASCII"));
                        }
                        catch (UnsupportedEncodingException e) {
                        }

                        out.flush();
                    }
                }
                finally {
                    // At this point release the "cloud" lock
                    cloudLock.unlock();
                }
            }
            catch (IOException ex)   { }
        }


        /**
         * This method reads incoming communication from a cMsg server client.
         *
         * @throws IOException if problems with socket communication
         */
        private void readServerClientInfo() throws IOException {
//System.out.println(">> NS: IN handleServer");

            // Is client low throughput & small msg size?
            // Server clients are treated as medium throughput so ignore.
            regime = in.readInt();
            if (debug >= cMsgConstants.debugInfo) {
                if (regime == cMsgConstants.regimeHigh)
                    System.out.println(">> NS: regime = High");
                else if (regime == cMsgConstants.regimeMedium)
                    System.out.println(">> NS: regime = Medium");
                else if (regime == cMsgConstants.regimeLow)
                    System.out.println(">> NS: regime = Low");
            }

            // What relationship does the connecting server have to the server cloud?
            // Can be INCLOUD, NONCLOUD, or BECOMINGCLOUD.
            connectingCloudStatus = in.readByte();
            if (debug >= cMsgConstants.debugInfo) {
                if (connectingCloudStatus == INCLOUD)
                   System.out.println(">> NS: connection cloud status = in cloud");
                else if (connectingCloudStatus == NONCLOUD)
                   System.out.println(">> NS: connection cloud status = non cloud");
                else if (connectingCloudStatus == BECOMINGCLOUD)
                   System.out.println(">> NS: connection cloud status = becoming cloud");
            }

            // Is connecting server originating connection or is this a reciprocal one?
            isReciprocalConnection = in.readByte() == 0;
            if (debug >= cMsgConstants.debugInfo) {
                if (isReciprocalConnection)
                   System.out.println(">> NS: reciprocal connection = true");
                else
                   System.out.println(">> NS: reciprocal connection = false");
            }

            // TCP listening port of name server that client is a part of
            nsTcpPort = in.readInt();
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println(">> NS: Tcp listening port connecting server = " + nsTcpPort);
            }

            // UDP multicast listening port of name server that client is a part of
            nsMulticastPort = in.readInt();
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println(">> NS: mcast listening port connecting server = " + nsMulticastPort);
            }

            // length of server client's host name
            int lengthHost = in.readInt();
            // length of server client's cloud password
            int lengthCloudPassword = in.readInt();
            // length of server client's client password
            int lengthClientPassword = in.readInt();

            // bytes expected
            int bytesToRead = lengthHost + lengthCloudPassword + lengthClientPassword;
            int offset = 0;

            // read all string bytes
            byte[] bytes = new byte[bytesToRead];
            in.readFully(bytes, 0, bytesToRead);

            // read host
            clientHost = new String(bytes, offset, lengthHost, "US-ASCII");
            offset += lengthHost;
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println(">> NS: host = " + clientHost);
            }

            // read cloud password
            myCloudPassword = new String(bytes, offset, lengthCloudPassword, "US-ASCII");
            offset += lengthCloudPassword;
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println(">> NS: given cloud password = " + myCloudPassword);
            }

            // read client password
            myClientPassword = new String(bytes, offset, lengthClientPassword, "US-ASCII");
            offset += lengthClientPassword;
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println(">> NS: given client password = " + myClientPassword);
            }

            // Make this client's name = "host:port"
            name = clientHost + ":" + nsTcpPort;
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println(">> NS: host name = " + clientHost + ", client hame = " + name);
            }
        }


        /**
         * This method determines whether a server client is allowed to connect.
         *
         * @return true if allowed to connect, else false
         * @throws IOException if problems with socket communication
         */
        private boolean allowServerClientConnection() throws IOException {

            boolean allowConnection = false;

            try {
                // First, check to see if password matches.
//System.out.println("local cloudpassword = " + cloudPassword +
//                   ", given password = " + myCloudPassword);
                if (cloudPassword != null && !cloudPassword.equals(myCloudPassword)) {
System.out.println(">> NS: PASSWORDS DO NOT MATCH");
                    cMsgException ex = new cMsgException("wrong password - connection refused");
                    ex.setReturnCode(cMsgConstants.errorWrongPassword);
                    throw ex;
                }

                // Second, check to see if this is a stand alone server.
                if (standAlone) {
System.out.println(">> NS: This is a standalone server, refuse server connection");
                    cMsgException ex = new cMsgException("stand alone server - no server connections allowed");
                    ex.setReturnCode(cMsgConstants.error);
                    throw ex;
                }

                // Third, check to see if this server is already connected.
                if (nameServers.contains(name)) {
System.out.println(">> NS: ALREADY CONNECTED TO " + name);
                    cMsgException ex = new cMsgException("already connected");
                    ex.setReturnCode(cMsgConstants.errorAlreadyExists);
                    throw ex;
                }

                // Allow other servers to connect to this one if:
                //   (1) this server is a cloud member, or
                //   (2) this server is not a cloud member and it is a
                //       reciprocal connection from a cloud member, or
                //   (3) this server is becoming a cloud member and it is an
                //       original or reciprocal connection from a another server
                //       that is simultaneously trying to become a cloud member.
                if (cloudStatus == INCLOUD) {
                    allowConnection = true;
                    // If I'm in the cloud, the connecting server cannot be making
                    // a reciprocal connection since a reciprocal connection
                    // is the only kind I can make.
                    isReciprocalConnection = false;
                }
                else if (connectingCloudStatus == INCLOUD) {
                    allowConnection = true;
                    // If the connecting server is a cloud member and I am not,
                    // it must be making a reciprocal connection since that's the
                    // only kind of connection a cloud member can make.
                    isReciprocalConnection = true;
                }
                else if (cloudStatus == BECOMINGCLOUD && connectingCloudStatus == BECOMINGCLOUD) {
                    allowConnection = true;
                }
                else {
                    // If we've reached here, then it's a connection from a noncloud server
                    // trying to connect to a noncloud/becomingcloud server or vice versa which
                    // is forbidden. This connection will not be allowed to proceed until this
                    // server becomes part of the cloud.
                }

                if (!allowConnection) {
                    try {
                        // Wait here up to 5 sec if the connecting server is not allowed to connect.
System.out.println(">> NS: Connection NOT allowed so wait up to 5 sec for connection");
                        if (!allowConnectionsCloudSignal.await(5L, TimeUnit.SECONDS)) {
                            cMsgException ex = new cMsgException("nameserver not in server cloud - timeout error");
                            ex.setReturnCode(cMsgConstants.errorTimeout);
                            throw ex;
                        }
                    }
                    catch (InterruptedException e) {
                        cMsgException ex = new cMsgException("interrupted while waiting for name server to join server cloud");
                        ex.setReturnCode(cMsgConstants.error);
                        throw ex;
                    }
                }

                allowConnection = true; // CHANGED LOGIC HERE !!!
            }
            catch (cMsgException ex) {
                // send int error code to client
                out.writeInt(ex.getReturnCode());
                // send error string to client
                out.writeInt(ex.getMessage().length());
                try {
                    out.write(ex.getMessage().getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {
                }

                out.flush();
            }

            return allowConnection;
        }


        /**
         * This method sends back a reply to the server client trying to connect,
         * telling the domain server host and port along with names of other servers
         * in the cloud.
         *
         * @throws IOException if problems with socket communication
         */
        private void replyToServerClient() throws IOException {
            // send ok back as acknowledgment
            out.writeInt(cMsgConstants.ok);

            // send unique id number which client sends back to server in order to identify itself
            out.writeInt(info.clientKey);

            // send cMsg domain host & port contact info back to client
            out.writeInt(domainServerPort);
            out.writeInt(info.getDomainHost().length());
            try {
                out.write(info.getDomainHost().getBytes("US-ASCII"));
            }
            catch (UnsupportedEncodingException e) {
            }

            try {
                // If this is not a reciprocal connection, we need to make one.
                if (!isReciprocalConnection) {
//System.out.println(">> NS: Create reciprocal bridge to " + name);
                    cMsgServerBridge b = new cMsgServerBridge(cMsgNameServer.this, name,
                                                              port, multicastPort);
                    // connect as reciprocal (originating = false)
                    b.connect(false, cloudPassword, clientPassword, false);
//System.out.println(">> NS: Add " + name + " to bridges");
                    bridges.put(name, b);
                    // If status was NONCLOUD, it is now BECOMINGCLOUD,
                    // and if we're here it is not INCLOUD.
                    b.setCloudStatus(cMsgNameServer.BECOMINGCLOUD);
//System.out.println(">> NS: set bridge (" + b.serverName + ") status to BECOMINGCLOUD");
                }
                // If this is a reciprocal connection, look up bridge for
                // connecting server and change its cloud status.
                else {
//System.out.println(">> NS: Do NOT create reciprocal bridge to " + name);
                    // We cannot look up the bridge in "bridges" as it is still
                    // in the middle of being created and has not been added
                    // to that collection yet. We have saved a reference, however.
                    cMsgServerBridge b = bridgeBeingCreated;
                    if (b != null) {
                        b.setCloudStatus(connectingCloudStatus);
//System.out.println(">> NS: set bridge (" + b.serverName + ") status to " + b.getCloudStatus());
                    }
                    else {
//System.out.println(">> NS: bridge  = " + b);
                    }
                }
            }
            catch (cMsgException e) {
                e.printStackTrace();
            }

            // If I'm in the cloud, send a list of cMsg servers I'm already connected to.
            // If there are no connections to other servers, we can forget it.
            // Do this only if not a reciprocal connection.
            if ((cloudStatus == INCLOUD) &&
                    (nameServers.size() > 0) &&
                    (!isReciprocalConnection)) {
                // send number of servers I'm connected to
//System.out.println(">> NS: Tell connecting server " + nameServers.size() + " servers are connected to us:");
                out.writeInt(nameServers.size());

                // for each cloud server, send name length, then name
                for (String serverName : nameServers.keySet()) {
                    System.out.println(">>    - " + serverName);
                    out.writeInt(serverName.length());
                    out.write(serverName.getBytes("US-ASCII"));
                }
            }
            else {
                // no servers are connected
//System.out.println(">> NS: Tell connecting server no one is connected to us");
                out.writeInt(0);
            }
            out.flush();

            // store this connection in hashtable
            if (connectingCloudStatus == NONCLOUD) {
                // If we're this far then we're in the cloud and the connecting
                // server is trying to become part of it.
                connectingCloudStatus = BECOMINGCLOUD;
            }
//System.out.println(">> NS: Add " + name + " to nameServers with status = " + connectingCloudStatus);
            nameServers.put(name, info);

//System.out.println("");
        }


        /**
         * This method registers a server client with this server. This method passes on the
         * registration function to the subdomain handler object. These server clients exist
         * only in the cMsg subdomain.
         *
         * @throws cMsgException if a domain server could not be started for the client or if
         *                       a subdomain handler object could not be created
         * @throws IOException if problems with socket communication
         */
        synchronized private void registerServer() throws cMsgException, IOException {
            // Create instance of cMsg subdomain handler. We need to access
            // methods not in the cMsgSubdomainInterface so do a cast.
            org.jlab.coda.cMsg.cMsgDomain.subdomains.cMsg subdomainHandler =
                    (org.jlab.coda.cMsg.cMsgDomain.subdomains.cMsg) createClientHandler("cMsg");

            // The first thing we do is set the namespace to the default namespace.
            // Server clients don't use the namespace so it doesn't matter.
            subdomainHandler.setUDLRemainder(null);

            info.subdomainHandler = subdomainHandler;
            info.cMsgSubdomainHandler = subdomainHandler;
            info.setDomainHost(host);

            // The next thing to do is create an object enabling the handler
            // to communicate with only this client in this cMsg domain.
            cMsgMessageDeliverer deliverer;
            try {
                deliverer = new cMsgMessageDeliverer();
            }
            catch (IOException e) {
                cMsgException ex = new cMsgException("socket communication error");
                ex.setReturnCode(cMsgConstants.errorNetwork);
                throw ex;
            }

            // Store deliverer object in client info object.
            // The cMsg subdomain uses this reference to communicate.
            info.setDeliverer(deliverer);

            // Do a local registration with handler object.
            // Registrations with other servers is done when bridge objects contact them.
            subdomainHandler.registerServer(info);

            // Create a domain server thread, and get back its host & port
            cMsgDomainServerSelect dsServer = new cMsgDomainServerSelect(cMsgNameServer.this, 1, debug, true);

            // get ready to accept a couple connections from client
            connectionHandler.allowConnections(info);

            // send client info about connecting to domain server and
            // about other servers in the cloud
            replyToServerClient();

            // client should respond by making 2 connections to domain server (20 sec timeout)
            if (!connectionHandler.gotConnections(info, 20)) {
                // failed to get proper connections from server client, so abort
                throw new cMsgException("server client did not make connections to domain server");
            }

            // kill this thread too if name server thread quits
            dsServer.setDaemon(true);
            dsServer.startThreads();
            dsServer.addClient(info);
            // store ref to this domain server
            domainServersSelect.put(dsServer, "");
            // start monitoring client
            monitorHandler.addClient(info, dsServer);
        }



        /**
         * This method receives information about a regular (non-server) cMsg client
         * which is attempting to connect to this server and registers it.
         *
         * @throws IOException if problems with socket communication
         */
        private void handleRegularClient() throws IOException {

            InetSocketAddress add = (InetSocketAddress)(channel.socket().getRemoteSocketAddress());
//            if (debug >= cMsgConstants.debugInfo) {
//                System.out.println("connecting client:\n  client sending addr = " + add);
//                System.out.println("  client host = " +  add.getHostName());
//                System.out.println("  client addr = " +  add.getAddress().getHostAddress());
//                System.out.println("  client sending port = " +  add.getPort());
//            }

            // is client low/med/high throughput ?
            regime = in.readInt();
            // length of password
            int lengthPassword = in.readInt();
            // length of domain type client is expecting to connect to
            int lengthDomainType = in.readInt();
            // length of subdomain type client is expecting to use
            int lengthSubdomainType = in.readInt();
            // length of UDL remainder to pass to subdomain handler
            int lengthUDLRemainder = in.readInt();
            // length of client's host name
            int lengthHost = in.readInt();
            // length of client's name
            int lengthName = in.readInt();
            // length of client's UDL
            int lengthUDL = in.readInt();
            // length of client's description
            int lengthDescription = in.readInt();

            // bytes expected
            int bytesToRead = lengthPassword + lengthDomainType + lengthSubdomainType +
                              lengthUDLRemainder + lengthHost + lengthName + lengthUDL +
                              lengthDescription;
//System.out.println("getClientInfo: bytesToRead = " + bytesToRead);
            int offset = 0;

            // read all string bytes
            byte[] bytes = new byte[bytesToRead];
            in.readFully(bytes, 0, bytesToRead);

            // read password
            String password = new String(bytes, offset, lengthPassword, "US-ASCII");
            offset += lengthPassword;
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  password = " + password);
            }

            // read domain
            String domainType = new String(bytes, offset, lengthDomainType, "US-ASCII");
            offset += lengthDomainType;
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  domain = " + domainType);
            }

            // read subdomain
            String subdomainType = new String(bytes, offset, lengthSubdomainType, "US-ASCII");
            offset += lengthSubdomainType;
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  subdomain = " + subdomainType);
            }

            // Elliott wanted this printed out
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  server port = " + port);
            }

            // read UDL remainder
            String UDLRemainder = new String(bytes, offset, lengthUDLRemainder, "US-ASCII");
            offset += lengthUDLRemainder;
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  remainder = " + UDLRemainder);
            }

            // read host
            String host = new String(bytes, offset, lengthHost, "US-ASCII");
            offset += lengthHost;
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  host = " + host);
            }

            // read name
            String name = new String(bytes, offset, lengthName, "US-ASCII");
            offset += lengthName;
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  name = " + name);
            }

            // read UDL
            String UDL = new String(bytes, offset, lengthUDL, "US-ASCII");
            offset += lengthUDL;
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  UDL = " + UDL);
            }

            // read description
            String description = new String(bytes, offset, lengthDescription, "US-ASCII");
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println("  description = " + description);
            }

            // if this is not the domain of server the client is expecting, return an error
            if (!domainType.equalsIgnoreCase(this.domain)) {
//System.out.println("ERROR coming back to client, bad domain");
                // send error to client
                out.writeInt(cMsgConstants.errorWrongDomainType);
                // send error string to client
                String s = "this server implements " + this.domain + " domain";
                out.writeInt(s.length());
                try {
                    out.write(s.getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {
                }

                out.flush();
                return;
            }

            // if the client does not provide the correct password if required, return an error
            if (clientPassword != null) {

                if (debug >= cMsgConstants.debugInfo) {
                    System.out.println("  local password = " + clientPassword);
                    System.out.println("  given password = " + password);
                }

                if (password.length() < 1 || !clientPassword.equals(password)) {

                    if (debug >= cMsgConstants.debugError) {
                        System.out.println("  wrong password sent");
                    }

                    // send error to client
                    out.writeInt(cMsgConstants.errorWrongPassword);
                    // send error string to client
                    String s = "wrong password given";
                    out.writeInt(s.length());
                    try {
                        out.write(s.getBytes("US-ASCII"));
                    }
                    catch (UnsupportedEncodingException e) {
                    }

                    out.flush();
                    return;
                }
            }

            // Create unique id number to send to client which it sends back in its
            // reponse to this server's communication in order to identify itself.
            int uniqueKey = connectionHandler.getUniqueKey();

            // Try to register this client. If the cMsg system already has a
            // client by this name, it will fail.
            info = new cMsgClientData(name, port, domainServerPort, host,
                                      add.getAddress().getHostAddress(),
                                      subdomainType, UDLRemainder, UDL,
                                      description, uniqueKey);
            if (debug >= cMsgConstants.debugInfo) {
                System.out.println(">> NS: name server try to register " + name);
            }

            try {
                registerClient();
            }
            catch (cMsgException ex) {
//System.out.println("ERROR coming back to client, failed to register at " + (new Date()));
                ex.printStackTrace();
                // send int error code to client
                out.writeInt(ex.getReturnCode());
                // send error string to client
                out.writeInt(ex.getMessage().length());
                try {
                    out.write(ex.getMessage().getBytes("US-ASCII"));
                }
                catch (UnsupportedEncodingException e) {
                }
                out.flush();
            }
        }


        /**
         * This method registers a client with this server. This method passes on the
         * registration function to the subdomain handler object. This handler object
         * gets the UDL remainder (also part of the cMsgClientData object) which it can
         * parse as it sees fit.
         * <p/>
         * The subdomain should have a class by that name that can be loaded and used
         * as the subdomain handler object. The classes corresponding to these handlers
         * can be passed to the name server on the command line as in the following:
         * java cMsgNameServer -DmySubdomain=myCmsgClientHandlerClass
         *
         * @throws IOException If a client could not be registered in the cMsg server cloud
         * @throws cMsgException If a domain server could not be started for the client
         */
        private void registerClient() throws cMsgException, IOException {

            // create a subdomain handler object
            cMsgSubdomainInterface subdomainHandler = createClientHandler(info.getSubdomain());

            // The first thing we do is pass the UDL remainder to the handler.
            // In the cMsg subdomain, it is parsed to find the namespace which
            // is stored in the subdomainHandler object.
            subdomainHandler.setUDLRemainder(info.getUDLremainder());

            info.subdomainHandler = subdomainHandler;
            info.setDomainHost(host);

            // The next thing to do is create an object enabling the handler
            // to communicate with only this client in this cMsg domain.
            cMsgMessageDeliverer deliverer;
            try {
                deliverer = new cMsgMessageDeliverer();
            }
            catch (IOException e) {
                cMsgException ex = new cMsgException("socket communication error");
                ex.setReturnCode(cMsgConstants.errorNetwork);
                throw ex;
            }

            // Store deliverer object in client info object.
            info.setDeliverer(deliverer);

            // Register client with the subdomain.
            // If we're in the cMsg subdomain ...
            if (subdomainHandler instanceof org.jlab.coda.cMsg.cMsgDomain.subdomains.cMsg) {
                // Wait until clients are allowed to connect (i.e. this server
                // has joined the cloud of cMsg subdomain name servers).
                try {
                    // If we've timed out ...
                    if (!allowConnectionsCloudSignal.await(5L, TimeUnit.SECONDS)) {
                        cMsgException ex = new cMsgException("nameserver not in server cloud - timeout error");
                        ex.setReturnCode(cMsgConstants.errorTimeout);
                        throw ex;
                    }
                }
                catch (InterruptedException e) {
                    cMsgException ex = new cMsgException("interrupted while waiting for name server to join server cloud");
                    ex.setReturnCode(cMsgConstants.error);
                    throw ex;
                }

                info.cMsgSubdomainHandler = (org.jlab.coda.cMsg.cMsgDomain.subdomains.cMsg) subdomainHandler;

                // We need to do a global registration spanning
                // all cMsg domain servers in this cloud.
                cMsgSubdomainRegistration(info.cMsgSubdomainHandler);
            }
            else {
                subdomainHandler.registerClient(info);
            }

            // Run through all existing domain server select objects and remove the
            // excess -- too many that are not serving any clients (or are dead).
            int dsLimit = 20;
            cMsgDomainServerSelect ds;
            synchronized (availableDomainServers) {
                if (availableDomainServers.size() > 0) {
                    for (ListIterator it = availableDomainServers.listIterator(); it.hasNext();) {
                        ds = (cMsgDomainServerSelect)it.next();
                        // thread died, get rid of it (should never happen)
                        if (!ds.isAlive()) {
                            it.remove();
                            ds.shutdown();
//System.out.println("REMOVED EXISTING DEAD Subdomain Server Select Object");
                        }
                        else if (ds.numberOfClients() < 1) {
                            if (dsLimit-- > 0) continue;
                            it.remove();
                            ds.shutdown();
//System.out.println("REMOVED EXISTING EMPTY Subdomain Server Select Object");
                        }
                    }
                }
            }

            // Create or find the domain server object. The server's udp socket is
            // created with the object and it's port # will be available to send
            // back to the client when the connections to the client are made.
            cMsgDomainServer dServer = null;
            cMsgDomainServerSelect dsServer = null;

            if (regime == cMsgConstants.regimeLow) {
                // first look for an available domain server with room for another client
                synchronized (availableDomainServers) {
                    if (availableDomainServers.size() > 0) {
                        for (ListIterator it = availableDomainServers.listIterator(); it.hasNext();) {
                            ds = (cMsgDomainServerSelect)it.next();
                            if (ds.numberOfClients() < clientsMax && ds.isAlive()) {
                                dsServer = ds;
                                ds.setClientsMax(clientsMax);
                                // Take this domain server out of list so other clients cannot use
                                // it simultaneously. It will be added back to the list if it
                                // hasn't hit the max # of clients (in dsServer.addClient method).
                                it.remove();
//System.out.println("GRAB EXISTING EMPTY Subdomain Server Select Object for " + info.getName());
                                break;
                            }
                        }
                    }
                }

                // if none found, create one
                if (dsServer == null) {
                    // Create a domain server thread, and get back its host & port
                    dsServer = new cMsgDomainServerSelect(cMsgNameServer.this,
                                                          clientsMax, debug, false);
                }

                info.setDomainUdpPort(dsServer.getUdpPort());
            }
            else if (regime == cMsgConstants.regimeMedium) {
                // first look for an available domain server with no current client
                synchronized (availableDomainServers) {
                    if (availableDomainServers.size() > 0) {
                        for (ListIterator it = availableDomainServers.listIterator(); it.hasNext();) {
                            ds = (cMsgDomainServerSelect)it.next();
                            if (ds.numberOfClients() < 1 && ds.isAlive()) {
                                dsServer = ds;
                                ds.setClientsMax(1);
                                it.remove();
//System.out.println("GRAB EXISTING EMPTY Subdomain Server Select Object for " + info.getName());
                                break;
                            }
                        }
                    }
                }

                if (dsServer == null) {
                    dsServer = new cMsgDomainServerSelect(cMsgNameServer.this, 1, debug, false);
//System.out.println("Create NEW Subdomain Server Select Object for " + info.getName() + ", p = " + dsServer);
                }
                info.setDomainUdpPort(dsServer.getUdpPort());
            }
            else {
                dServer = new cMsgDomainServer(cMsgNameServer.this, info, false, debug);
//System.out.println("Create new Subdomain Server Object for " + info.getName());
            }

            // get ready to accept 2 permanent connections from client
            connectionHandler.allowConnections(info);

            // send client info about domain server
            sendClientConnectionInfo(info, subdomainHandler);

            // client should respond by making 2 connections to domain server (20 sec timeout)
            if (!connectionHandler.gotConnections(info, 20)) {
//System.out.println("registerClient: took too long (> 20 sec) for client to make 2 connections to server");
                // failed to get proper connections from client, so abort
                subdomainHandler.handleClientShutdown();
                deliverer.close();
                throw new cMsgException("client did not make connections to domain server");
            }

            // Start the domain server's threads.
            if (regime != cMsgConstants.regimeHigh) {
                // if threads haven't been started yet ...
                if (!dsServer.isAlive()) {
//System.out.println("registerClient: STARTING UP DSS threads !!!");
                    // kill this thread too if name server thread quits
                    dsServer.setDaemon(true);
                    dsServer.addClient(info); /* deliverer gets message channel */
                    dsServer.startThreads();
                    // store ref to this domain server
                    domainServersSelect.put(dsServer, "");
                }
                else {
                    dsServer.addClient(info); /* deliverer gets message channel */
                }
                // start monitoring client
                monitorHandler.addClient(info, dsServer);
            }
            else {
                // kill this thread too if name server thread quits
                dServer.setDaemon(true);
//System.out.println("Create new Subdomain Server Object's threads for " + info.getName());
                dServer.startThreads();
                // store ref to this domain server
                domainServers.put(dServer, "");
                // start monitoring client
                monitorHandler.addClient(info, dServer);
            }


            return;
        }


        /**
         * This method returns communication to a regular (non-server) cMsg client
         * to tell it information about the domain it is attempting to connect to
         * and about the domain server port. The client should then make 2 permanent
         * connections to the domain server.
         *
         * @param cd client data object
         * @param handler subdomain handler object
         * @throws IOException if problems with socket communication
         */
        private void sendClientConnectionInfo(cMsgClientData cd, cMsgSubdomainInterface handler)
                throws IOException {

            // send ok back as acknowledgment
            out.writeInt(cMsgConstants.ok);

            // send back attributes of clientHandler class/object
            // 1 = has, 0 = don't have: send, subscribeAndGet, sendAndGet, subscribe, unsubscribe
            byte[] atts = new byte[7];
            atts[0] = handler.hasSend() ? (byte) 1 : (byte) 0;
            atts[1] = handler.hasSyncSend() ? (byte) 1 : (byte) 0;
            atts[2] = handler.hasSubscribeAndGet() ? (byte) 1 : (byte) 0;
            atts[3] = handler.hasSendAndGet() ? (byte) 1 : (byte) 0;
            atts[4] = handler.hasSubscribe() ? (byte) 1 : (byte) 0;
            atts[5] = handler.hasUnsubscribe() ? (byte) 1 : (byte) 0;
            atts[6] = handler.hasShutdown() ? (byte) 1 : (byte) 0;
            out.write(atts);

            // send unique id number which client sends back to server in order to identify itself
            out.writeInt(cd.clientKey);

            // send cMsg domain host & port contact info back to client
            out.writeInt(info.getDomainPort());
            out.writeInt(info.getDomainUdpPort());
            out.writeInt(info.getDomainHost().length());
            try {
                out.write(info.getDomainHost().getBytes("US-ASCII"));
            }
            catch (UnsupportedEncodingException e) {
            }

            out.flush();
        }


        /**
         * This method registers regular (non-server) clients in the cMsg subdomain.
         * Registration is more complicated in this domain than other domains as it
         * must contact all other cMsg servers to which it has a bridge. To ensure
         * global uniqueness of a client name, locks must be taken out on all servers
         * so that no other potential client may connect during this time.
         *
         * @param subdomainHandler subdomain handler object
         * @throws cMsgException if a registration lock on another server cannot be grabbed within
         *                       1/2 second, or the client trying to connect here does not have a
         *                       unique name
         * @throws IOException   if trouble communicating with other servers
         */
        private void cMsgSubdomainRegistration(org.jlab.coda.cMsg.cMsgDomain.subdomains.cMsg subdomainHandler)
                throws cMsgException, IOException {

//System.out.println(">> NS: IN subdomainRegistration");
            // If there are no connections to other servers (bridges), do local registration only
            if (bridges.size() < 1) {
                subdomainHandler.registerClient(info);
//System.out.println(">> NS: no bridges so DID regular registration of " + info.getName() + " in subdomain handler");
                return;
            }

            boolean gotCloudLock  = false;
            boolean gotRegistrationLock = false;
            boolean registrationSuccessful = false;
            LinkedList<cMsgServerBridge> lockedServers = new LinkedList<cMsgServerBridge>();

            // variables having to do with grabbing locks
            int grabLockTries, maxNumberOfTrys=6, numberOfTrys=0;
            Random random = new Random(System.currentTimeMillis());
            int delay, startingDelay = 150;  // milliseconds

            startOver:

                while (numberOfTrys++ < maxNumberOfTrys) {
//System.out.println(">> NS: startOver");
                    lockedServers.clear();

                    startingDelay = numberOfTrys*startingDelay;

// BUG BUG send timeout length ??
                    // We need to calculate the number of locks which constitute a
                    // majority of servers currently a part of the cloud. This can
                    // only be done once we have a lock of a cloud member (thereby
                    // preventing anyone else from joining).
                    grabLockTries = 0;

                    do {
//System.out.println(">> NS: grabLockTries = " + grabLockTries);
//System.out.println(">> NS: try local cloud lock");

                        // First, (since we are in the cloud now) we grab our own
                        // cloud lock so we stop cloud-joiners and check all cloud
                        // members' clients. Can also calculate a majority of cloud members.
// boolean locked = cloudLock.isLocked();
//System.out.println("local cloud locked = " + locked);
                        if (!gotCloudLock && cloudLock(10)) {
//System.out.println(">> NS: grabbed local cloud lock");
                            gotCloudLock = true;
                        }

                        // Second, Grab our own registration lock

                        if (!gotRegistrationLock) {
//System.out.print(">> NS: " + info.getName() + " trying to grab registration lock ...");
                            if (subdomainHandler.registrationLock(10)) {
                                gotRegistrationLock = true;
//System.out.println(" ... DONE!");
                            }
                            else {
//System.out.println(" ... CANNOT DO IT!");
                            }
                        }

                        // Can't grab a/both locks, wait and try again (at most 5 times)
                        if (!gotCloudLock || !gotRegistrationLock) {
                            // if we've reached our limit of tries ...
                            if (++grabLockTries > 5) {
                                if (debug >= cMsgConstants.debugWarn) {
                                    System.out.println("    << JR: Failed to grab inital cloud or registration lock");
                                }

                                // release locks
                                if (gotCloudLock) {
                                    cloudUnlock();
                                    gotCloudLock = false;
                                }
                                if (gotRegistrationLock) {
                                    subdomainHandler.registrationUnlock();
                                    gotRegistrationLock = false;
                                }

                                // delay = startingDelay + (0 - 150 millisecond)
                                // where startingDelay = 150,300,450,600,750,900 in each successive round.
                                delay = startingDelay + (int)(150*random.nextDouble());
                                try {Thread.sleep(delay);}
                                catch (InterruptedException e) {}

                                // take it from the top
//System.out.println(">> NS: continue to startOver");
                                continue startOver;
                            }

//System.out.println(">> NS: cannot grab a/both locks, wait .01 sec and try again");
                            try {Thread.sleep(10);}
                            catch (InterruptedException e) {}
                        }
                    } while (!gotCloudLock || !gotRegistrationLock);

                    // Calculate the majority
                    int totalCloudMembers = 1; // we is first
                    for (cMsgServerBridge b : bridges.values()) {
                        if (b.getCloudStatus() == cMsgNameServer.INCLOUD) {
                            totalCloudMembers++;
                        }
                    }
                    int majority = totalCloudMembers / 2 + 1;
                    int numberOfLockedCloudMembers = 1;

                    // Try to get all of the in-cloud servers' registration locks
//System.out.println(">> NS: Try to get all of the in-cloud servers' registration locks");
                    do {
                        // Grab the locks of other servers
                        for (cMsgServerBridge bridge : bridges.values()) {

                            // If it's already locked or not in the cloud, skip it
                            if (lockedServers.contains(bridge) ||
                                bridge.getCloudStatus() != cMsgNameServer.INCLOUD) {

//                                if (bridge.getCloudStatus() != cMsgNameServer.INCLOUD) {
//System.out.println(">> NS: skip locking " + bridge.serverName + "'s registration locks as NOT in cloud");
//                                }
//                                else {
//System.out.println(">> NS: skip locking " + bridge.serverName + "'s registration locks as already locked");
//                                }
                                continue;
                            }

                            try {
                                // If sucessfull in locking remote server ...
//System.out.print(">> NS: Try to lock bridge to " + bridge.serverName);
                                if (bridge.registrationLock(10)) {
//System.out.println("  ... LOCKED IT!!");
                                    lockedServers.add(bridge);
                                    numberOfLockedCloudMembers++;
                                }
                                // else if cannot lock remote server, try next one
                                else {
//System.out.println(" ... CANNOT Lock it, so skip it");
                                }
                            }
                            // We're here if lock or unlock fails in its communication with server.
                            // If we cannot talk to the server, it's probably dead.
                            catch (IOException e) {
                                e.printStackTrace();
                            }
                        }

                        //System.out.println(">> NS: FAILED TO LOCKED IT!!");

                        // If we have all the in-cloud locks we're done and can move on.
                        if (numberOfLockedCloudMembers >= totalCloudMembers) {
//System.out.println(">> NS: Got all Locks");
                            break;
                        }
                        // If we have a majority (but not all) in-cloud locks, try to get the rest.
                        else if (numberOfLockedCloudMembers >= majority) {
                            // Let other greedy lock-grabbers release theirs locks first
//System.out.println(">> NS: Get More Locks");
                            try {Thread.sleep(10);}
                            catch (InterruptedException e) {}
                        }
                        // If we do NOT have the majority of locks ...
                        else {
                            // release all locks
                            for (cMsgServerBridge b : lockedServers) {
                                try {
//System.out.print(">> NS: " + b.serverName + " trying to release registration lock 1 ...");
                                    b.registrationUnlock();
//System.out.println(" ... DONE!");
                                }
                                catch (IOException e) {}
                            }
//System.out.print(">> NS: " + info.getName() + " trying to release registration lock 2 ...");
                            subdomainHandler.registrationUnlock();
//System.out.println(" ... DONE!");
                            cloudUnlock();

                            // try to lock 'em again
                            gotCloudLock = false;
                            gotRegistrationLock = false;

                            // delay = startingDelay + (0 - 150 millisecond)
                            // where startingDelay = 150,300,450,600,750,900 in each successive round.
                            delay = startingDelay + (int)(150*random.nextDouble());

                            // Wait for a random time initially between 10 & 300
                            // milliseconds which doubles each loop.
                            //int milliSec = (int) ((10 + random.nextInt(291))*(Math.pow(2., numberOfTrys-1.)));
                            try {Thread.sleep(delay);}
                            catch (InterruptedException e) {}

                            // start over
//System.out.println(">> NS: Drop locks and start over");
                            continue startOver;
                        }

                    } while (true);


                    try {
                        // Get the list of client names and namespaces from each connected server
                        // and compare to the name/namespace of the client trying to connect to
                        // this server. Only accept a unique name/namespace combo, else reject it.
                        String[] nameList;
                        for (cMsgServerBridge bridge : bridges.values()) {
                            nameList = bridge.getClientNamesAndNamespaces();
                            String name,ns;
                            for (int i=0; i < nameList.length; i+=2) {
                                name = nameList[i];
                                ns   = nameList[i+1];
                                if (name.equals(info.getName()) &&
                                        ns.equals(subdomainHandler.getNamespace())) {
//System.out.println(">> NS: THIS MATCHES NAME OF CONNECTING CLIENT");
                                    cMsgException e = new cMsgException("client already exists");
                                    e.setReturnCode(cMsgConstants.errorAlreadyExists);
                                    throw e;
                                }
                            }

                        }

                        // FINALLY, REGISTER CLIENT!!!
//System.out.println(">> NS: TRY REGISTERING CLIENT (in subdomain handler)");
                        subdomainHandler.registerClient(info);
                    }
                    finally {
                        // release the locks
                        for (cMsgServerBridge b : lockedServers) {
                            try {
//System.out.print(">> NS: " + b.serverName + " trying to release registration lock 3 ...");
                                b.registrationUnlock();
//System.out.println(" ... DONE!");
                            }
                            catch (IOException e) {}
                        }
//System.out.print(">> NS: " + info.getName() + " trying to release registration lock 4 ...");
                        subdomainHandler.registrationUnlock();
                        cloudUnlock();
//System.out.println(" ... DONE!");
                    }

//System.out.println(">> NS: registration is successful!\n\n");
                    registrationSuccessful = true;
                    break;
                }

                // If we could not register the client due to not being able to get the required locks ...
                if (!registrationSuccessful) {
                    // release the locks
                    for (cMsgServerBridge b : lockedServers) {
                        try {b.registrationUnlock();}
                        catch (IOException e) {}
                    }

                    if (gotRegistrationLock) {
//System.out.print(">> NS: " + info.getName() + " trying to release registration lock ...");
                        subdomainHandler.registrationUnlock();
//System.out.println(" ... DONE!");
                    }

                    if (gotCloudLock) {
                        cloudUnlock();
                    }

                    System.out.println(">> NS: **********************************************************************");
                    System.out.println(">> NS: Cannot register the specified client, since cannot grab required locks");
                    System.out.println(">> NS: **********************************************************************");
                    cMsgException e = new cMsgException("cannot grab required locks");
                    e.setReturnCode(cMsgConstants.error);
                    throw e;
                }
        }

    }

}



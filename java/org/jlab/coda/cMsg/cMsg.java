/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 17-Nov-2004, Jefferson Lab                                   *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;


import org.jlab.coda.cMsg.cMsgCallbackInterface;
import org.jlab.coda.cMsg.common.cMsgDomainInterface;
import org.jlab.coda.cMsg.common.cMsgShutdownHandlerInterface;

import java.util.regex.Pattern;
import java.util.regex.Matcher;
import java.util.*;
import java.util.concurrent.TimeoutException;
import java.io.*;

/**
 * This class is instantiated by a client in order to connect to a domain.
 * The instantiated object will be the main means by which the client will
 * interact with cMsg.<p>
 * This class is the "top level" API and acts as a multiplexor to direct a cMsg
 * client to the proper domain based on the Uniform Domain Locator (UDL) given.
 * Note that not all of the methods of this object are implemented in a particular
 * domain and thus may return an exeception.
 * The UDL has the general form:<p>
 *   <b>cMsg:&lt;domainType&gt;://&lt;domain dependent remainder&gt;</b><p>
 * <ul>
 * <li>initial cMsg: is not necessary<p>
 * <li>cMsg and domainType are case independent<p>
 * </ul><p>
 * For the cMsg domain the UDL has the more specific form:<p>
 *   <b>cMsg:cMsg://&lt;host&gt;:&lt;port&gt;/&lt;subdomainType&gt;/&lt;subdomain remainder&gt;?tag=value&tag2=value2 ...</b><p>
 *
 * <ul>
 * <li>port is not necessary to specify but is the name server's TCP port if connecting directly
 *    or the server's UDP port if multicasting. Defaults used if not specified are
 *    {@link cMsgNetworkConstants#nameServerTcpPort} if connecting directly, else
 *    {@link cMsgNetworkConstants#nameServerUdpPort} if multicasting<p>
 * <li>host can be "localhost" and may also be in dotted form (129.57.35.21)<p>
 * <li>if domainType is cMsg, subdomainType is automatically set to cMsg if not given.
 *    if subdomainType is not cMsg, it is required<p>
 * <li>the domain name is case insensitive as is the subdomainType<p>
 * <li>remainder is passed on to the subdomain plug-in<p>
 * <li>client's password is in tag=value part of UDL as cmsgpassword=&lt;password&gt;<p>
 * <li>multicast timeout is in tag=value part of UDL as multicastTO=&lt;time out in seconds&gt;<p>
 * <li>the tag=value part of UDL parsed here is given by regime=low or regime=high means:<p>
 *   <ul>
 *   <li>low message/data throughtput client if regime=low, meaning many clients are serviced
 *       by a single server thread and all msgs retain time order<p>
 *   <li>high message/data throughput client if regime=high, meaning each client is serviced
 *       by multiple threads to maximize throughput. Msgs are NOT guaranteed to be handled in
 *       time order<p>
 *   <li>if regime is not specified (default), it is assumed to be medium, where a single thread is
 *       dedicated to a single client and msgs are guaranteed to be handled in time order<p>
 *   </ul>
 * </ul>
 */
public class cMsg {
    /** Level of debug output for this class. */
    private int debug = cMsgConstants.debugNone;

    /** String containing the whole UDL. */
    private String UDL;

    /** String containing the client's name. */
    private String name;

    /** String containing the client's description. */
    private String description;

    /** String containing the remainder part of the UDL. */
    private String UDLremainder;

    /** String containing the domain part of the UDL. */
    private String domain;

    /** A specific implementation of the cMsg API. */
    private cMsgDomainInterface connection;


    /** Constructor. Never used. */
    private cMsg() {    }


    /**
     * Constructor which creates the object used to connect to the UDL (domain) specified.
     *
     * @param UDL semicolon separated list of Uniform Domain Locators. These UDLs have different specifics
     *            in each domain. In the cMsg domain each of the UDLs specifies a server to connect to
     * @param name name of this client which must be unique in this domain
     * @param description description of this client
     * @throws cMsgException if domain in not implemented;
     *                          there are problems communicating with the domain;
     *                          name contains colon;
     *                          the UDL is invalid;
     *                          the UDL contains an unreadable file;
     *                          any of the arguments are null
     */
    public cMsg(String UDL, String name, String description) throws cMsgException {

        if (UDL == null || name == null || description == null) {
            throw new cMsgException("a cMsg constructor argument is null");
        }

        if (badString(name) || badString(UDL) || badString(description))  {
            throw new cMsgException("improper character(s) in argument");
        }

        // do not allow colons in the name string
        if (name.contains(":")) {
            throw new cMsgException("invalid name - contains \":\"");
        }

        this.UDL  = processUDLs(UDL);
        this.name = name;
        this.description = description;

        // create real connection object in specified domain
        connection = createDomainConnection();

        // Since the connection object is created with a no-arg constructor,
        // we must pass in information with setters.

        // Pass in the UDL
        connection.setUDL(UDL);
        // Pass in the name
        connection.setName(name);
        // Pass in the description
        connection.setDescription(description);
        // Pass in the UDL remainder
        connection.setUDLRemainder(UDLremainder);
        // Pass in the default debug value
        connection.setDebug(debug);
    }


    /**
     * Set level of debug output.
     * Argument may be one of:
     * <ul>
     * <li>{@link cMsgConstants#debugNone} for no outuput<p>
     * <li>{@link cMsgConstants#debugSevere} for severe error output<p>
     * <li>{@link cMsgConstants#debugError} for all error output<p>
     * <li>{@link cMsgConstants#debugWarn} for warning and error output<p>
     * <li>{@link cMsgConstants#debugInfo} for information, warning, and error output<p>
     * </ul>
     *
     * @param debug level of debug output
     */
    public void setDebug(int debug) {
        if (debug != cMsgConstants.debugError &&
            debug != cMsgConstants.debugInfo &&
            debug != cMsgConstants.debugNone &&
            debug != cMsgConstants.debugSevere &&
            debug != cMsgConstants.debugWarn) {
            return;
        }
        this.debug = debug;
        connection.setDebug(debug);
    }


    /**
     * This method checks a string given as a function argument.
     * It returns true if it contains an unprintable character or any
     * character from a list of excluded characters (`'").
     *
     * @param s string to check
     *
     * @returns false if string is OK
     * @returns true if string contains excluded or unprintable characters
     */
    static private boolean badString(String s) {

        if (s == null) return(true);

        // check for non-printable characters & quotes
        char c;
        for (int i=0; i < s.length(); i++) {
            c = s.charAt(i);
            if (c < ' ' || c > '~') return(true);
            if (c == '\'' || c == '"' || c == '`') return(true);
        }

        // string ok
        return(false);
    }


    /**
     * This method ensures that: 1) in a semicolon separated list of UDLs, all the domains
     * are the same, 2) any domain of type "configFile" is expanded before analysis, and
     * 3) no duplicate UDLs are in the list
     *
     * @param clientUDL UDL to be analyzed
     * @return a list of semicolon separated UDLs where all domains are the same and
     *         all configFile domain UDLs are expanded
     * @throws cMsgException if more than one domain is included in the list of UDLs or
     *                       files given by a configFile domain cannot be read
     */
    private String processUDLs(String clientUDL) throws cMsgException {

        // Since the UDL may be a semicolon separated list of UDLs, separate them
        String udlStrings[]  = clientUDL.split(";");

        // trim off any white space on either end of individual UDL
        for (int i=0; i < udlStrings.length; i++) {
            udlStrings[i] = udlStrings[i].trim();
        }

        // Turn String array into a linked list (array list does not allow
        // use of the method "remove")
        List<String> l = Arrays.asList(udlStrings);
        LinkedList<String> udlList = new LinkedList<String>(l);

        // To eliminate duplicate udls, don't compare domains (which are forced to be identical
        // anyway). Just compare the udl remainders which may be case sensitive and will be
        // treated as such. Remove any duplicate items by placing them in a set (which does
        // this automatically). Make it a linked hash set to preserve order.
        LinkedHashSet<String> udlSet = new LinkedHashSet<String>();

        String udl, domainName=null;
        String[] parsedUDL;   // first element = domain, second = remainder
        int startIndex=0;
        boolean gotDomain = false;

        // One difficulty in implementing a domain in which a file contains the actual UDL
        // is that there is the possibility for self-referential, infinite loops. In other
        // words, the first file references to a 2nd file and that references the first, etc.
        // To avoid such a problem, it is NOT allowed for a configFile UDL to point to a
        // UDL in which there is another configFile domain UDL.

        topLevel:
            while(true) {
                // For each UDL in the list ...
                for (int i=startIndex; i < udlList.size(); i++) {

                    udl = udlList.get(i);
//System.out.println("udl = " + udl + ", at position " + i);

                    // Get the domain & remainder from the UDL
                    parsedUDL = parseUDL(udl);

                    // If NOT configFile domain ...
                    if (!parsedUDL[0].equalsIgnoreCase("configFile")) {
                        // Keep track of the valid UDL remainders
//System.out.println("storing remainder = " + parsedUDL[1]);
                        udlSet.add(parsedUDL[1]);

                        // Grab the first valid domain and make all other UDLs be the same
                        if (!gotDomain) {
                            domainName = parsedUDL[0];
                            gotDomain = true;
//System.out.println("Got domain, = " + parsedUDL[0]);
                        }
                        else {
                            if (!domainName.equalsIgnoreCase(parsedUDL[0])) {
                                throw new cMsgException("All UDLs must belong to the same domain");
                            }
                        }
                    }
                    // If configFile domain ...
                    else {
                        try {
//System.out.println("reading config file " + parsedUDL[1]);

                            // Read file to obtain actual UDL
                            String newUDL = readConfigFile(parsedUDL[1]);

                            // Check to see if this string contains a UDL which is in the configFile
                            // domain. That is NOT allowed in order to avoid infinite loops.
                            if (newUDL.toLowerCase().contains("configfile://")) {
                                throw new cMsgException("one configFile domain UDL may NOT reference another");
                            }

                            // Since the UDL may be a semicolon separated list of UDLs, separate them
                            String udls[] = newUDL.split(";");
                            // trim off any white space on either end of individual UDL
                            for (int j=0; j < udls.length; j++) {
                                udls[j] = udls[j].trim();
                            }

                            // Substitute these new udls for "udl" they're replacing in the original
                            // list and start the process over again
//System.out.println("  about to remove item #" + i + " from list " + udlList);
                            udlList.remove(i);
                            for (int j = 0; j < udls.length; j++) {
                                udlList.add(i+j, udls[j]);
//System.out.println("  adding udl = " + udls[j] + ", at position " + (i+j));
                            }

                            // skip over udls already done
                            startIndex = i;

                            continue topLevel;
                        }
                        catch (IOException e) {
                            e.printStackTrace();
                            throw new cMsgException("Cannot read UDL in file", e);
                        }
                    }
                }
                break;
            }

        // Warn user if there are duplicate UDLs in the UDL list that were removed
        if (udlList.size() != udlSet.size()) {
            System.out.println("\nWarning: duplicate UDL(s) removed from the UDL list\n");
        }

        // reconstruct the list of UDLs
        int i=0;
        StringBuffer finalUDL = new StringBuffer(500);
        for (String s : udlSet) {
            finalUDL.append(domainName);
            finalUDL.append("://");
            finalUDL.append(s);
            finalUDL.append(";");
            // pick off first udl remainder
            if (i++ == 0) {
                UDLremainder = s;
            }
        }
        // remove the last semicolon
        finalUDL.deleteCharAt(finalUDL.length() - 1);

        domain = domainName;

//System.out.println("Return processed UDL as " + finalUDL.toString());
//System.out.println("domain = " + domain + ", and remainder = " + UDLremainder);

        return finalUDL.toString();
    }


    /**
     * Method to read a configuration file and return the cMsg UDL stored there.
     *
     * @param fileName name of file to be read
     * @return UDL contained in config file, null if none
     * @throws IOException if file IO problem
     * @throws cMsgException if file does not exist or cannot be read
     */
    private String readConfigFile(String fileName) throws IOException, cMsgException {
        File file = new File(fileName);
        if (!file.exists()) {
            throw new cMsgException("specified file in UDL does NOT exist");
        }
        if (!file.canRead()) {
            throw new cMsgException("specified file in UDL cannot be read");
        }
        String s = "";
        BufferedReader reader = new BufferedReader(new FileReader(file));
        while (reader.ready() && s.length() < 1) {
            // readLine is a bit quirky :
            // it returns the content of a line MINUS the newline.
            // it returns null only for the END of the stream.
            // it returns an empty String if two newlines appear in a row.
            s = reader.readLine().trim();
//System.out.println("Read this string from file: " + s);
            if (s == null) break;
        }
        return s;
    }


    /**
     * Method to parse the Universal Domain Locator (or UDL) into its various components.
     * The UDL is of the form:<p>
     *   cMsg:&lt;domainType&gt;://<&lt;domain dependent remainder&gt;<p>
     * <ul>
     * <li>initial cMsg: is not necessary<p>
     * <li>cMsg and domainType are case independent<p>
     * </ul>
     *
     * @param UDL Universal Domain Locator
     * @return array of 2 Strings, the first of which is the domain, the second
     *         of which is the UDL remainder (everything after the domain://)
     * @throws cMsgException if UDL is null; or no domainType is given in UDL
     */
    private String[] parseUDL(String UDL) throws cMsgException {

        if (UDL == null) {
            throw new cMsgException("invalid UDL");
        }

        Pattern pattern = Pattern.compile("(cMsg)?:?([\\w\\-]+)://(.*)", Pattern.CASE_INSENSITIVE);
        Matcher matcher = pattern.matcher(UDL);

        String s0, s1, s2;

        if (matcher.matches()) {
            // cMsg
            s0 = matcher.group(1);
            // domain
            s1 = matcher.group(2);
            // remainder
            s2 = matcher.group(3);
        }
        else {
            throw new cMsgException("invalid UDL");
        }

        if (debug >= cMsgConstants.debugInfo) {
           System.out.println("\nparseUDL: " +
                              "\n  space     = " + s0 +
                              "\n  domain    = " + s1 +
                              "\n  remainder = " + s2);
        }

        // need domain
        if (s1 == null) {
            throw new cMsgException("invalid UDL");
        }

        // any remaining UDL is put here
        if (s2 == null) {
            throw new cMsgException("invalid UDL");
        }

        return new String[] {s1,s2};
    }


    /**
     * Creates the object that makes the real connection to a particular domain's server
     * that was specified in the UDL.
     *
     * @return connection object to domain specified in UDL
     * @throws cMsgException if object could not be created
     */
    private cMsgDomainInterface createDomainConnection()
            throws cMsgException {

        String domainConnectionClass = null;

        /** Object to handle client */
        cMsgDomainInterface domainConnection;

        // First check to see if connection class name was set on the command line.
        // Do this by scanning through all the properties.
        for (Object obj : System.getProperties().keySet()) {
            String s = (String) obj;
            if (s.contains(".")) {continue;}
            if (s.equalsIgnoreCase(domain)) {
                domainConnectionClass = System.getProperty(s);
            }
        }

        // If it wasn't given on the command line,
        // check the appropriate environmental variable.
        if (domainConnectionClass == null) {
            domainConnectionClass = System.getenv("CMSG_DOMAIN");
        }
//System.out.println("Looking for domain " + domain);
        // If there is still no handler class, look for the
        // standard, provided classes.
        if (domainConnectionClass == null) {
            // cMsg and runcontrol domains use the same client code
            if (domain.equalsIgnoreCase("cMsg"))  {
                domainConnectionClass = "org.jlab.coda.cMsg.cMsgDomain.client.cMsg";
            }
            else if (domain.equalsIgnoreCase("rc")) {
                domainConnectionClass = "org.jlab.coda.cMsg.RCDomain.RunControl";
            }
            else if (domain.equalsIgnoreCase("rcs")) {
                domainConnectionClass = "org.jlab.coda.cMsg.RCServerDomain.RCServer";
            }
            else if (domain.equalsIgnoreCase("rcm")) {
                domainConnectionClass = "org.jlab.coda.cMsg.RCMulticastDomain.RCMulticast";
            }
            else if (domain.equalsIgnoreCase("TCPS")) {
                domainConnectionClass = "org.jlab.coda.cMsg.TCPSDomain.TCPS";
            }
            else if (domain.equalsIgnoreCase("file")) {
                domainConnectionClass = "org.jlab.coda.cMsg.FileDomain.File";
            }
            else if (domain.equalsIgnoreCase("CA")) {
                domainConnectionClass = "org.jlab.coda.cMsg.CADomain.CA";
            }
        }

        // all options are exhausted, throw error
        if (domainConnectionClass == null) {
            cMsgException ex = new cMsgException("no handler class found");
            ex.setReturnCode(cMsgConstants.errorNoClassFound);
            throw ex;
        }

        // Get connection class name and create object
        try {
            domainConnection = (cMsgDomainInterface) (Class.forName(domainConnectionClass).newInstance());
        }
        catch (InstantiationException e) {
            cMsgException ex = new cMsgException("cannot instantiate "+ domainConnectionClass +
                                                 " class");
            ex.setReturnCode(cMsgConstants.error);
            throw ex;
        }
        catch (IllegalAccessException e) {
            cMsgException ex = new cMsgException("cannot access "+ domainConnectionClass +
                                                 " class");
            ex.setReturnCode(cMsgConstants.error);
            throw ex;
        }
        catch (ClassNotFoundException e) {
            cMsgException ex = new cMsgException("no handler class found");
            ex.setReturnCode(cMsgConstants.errorNoClassFound);
            throw ex;
        }

        return domainConnection;
    }

    
    /**
      * Method to connect to a particular domain.
      *
      * @throws cMsgException
      */
     public void connect() throws cMsgException {
        connection.connect();
     }


    /**
     * Method to close the connection to the domain. This method results in this object
     * becoming functionally useless.
     *
     * @throws cMsgException
     */
    public void disconnect() throws cMsgException {
        connection.disconnect();
    }

    /**
     * Method to determine if this object is still connected to the domain or not.
     *
     * @return true if connected to domain, false otherwise
     */
    public boolean isConnected() {
        return connection.isConnected();
    }

    /**
     * Method to send a message to the domain for further distribution.
     *
     * @param message message
     * @throws cMsgException
     */
    public void send(cMsgMessage message) throws cMsgException {
        connection.send(message);
    }

    /**
     * Method to send a message to the domain for further distribution
     * and wait for a response from the domain that got it.
     *
     * @param message message
     * @param timeout time in milliseconds to wait for a response
     * @return response from domain
     * @throws cMsgException
     */
    public int syncSend(cMsgMessage message, int timeout) throws cMsgException {
        return connection.syncSend(message, timeout);
    }

    /**
     * Method to force cMsg client to send pending communications with domain.
     * @param timeout time in milliseconds to wait for completion
     * @throws cMsgException
     */
    public void flush(int timeout) throws cMsgException {
        connection.flush(timeout);
    }

    /**
     * This method is like a one-time subscribe. The domain sends the first incoming
     * message of the requested subject and type to the caller.
     *
     * @param subject subject of message desired from domain
     * @param type type of message desired from domain
     * @param timeout time in milliseconds to wait for a message
     * @return response message
     * @throws cMsgException
     * @throws TimeoutException if timeout occurs
     */
    public cMsgMessage subscribeAndGet(String subject, String type, int timeout)
            throws cMsgException, TimeoutException {
        return connection.subscribeAndGet(subject, type, timeout);
    }

    /**
     * The message is sent as it would be in the {@link #send(cMsgMessage)}  method. The domain notes
     * the fact that a response to it is expected, and sends it to all subscribed to its
     * subject and type. When a marked response is received from a client, it sends that
     * first response back to the original sender regardless of its subject or type.
     * The response may be null.
     *
     * @param message message sent to domain
     * @param timeout time in milliseconds to wait for a response message
     * @return response message
     * @throws cMsgException
     * @throws TimeoutException if timeout occurs
     */
    public cMsgMessage sendAndGet(cMsgMessage message, int timeout)
            throws cMsgException, TimeoutException {
        return connection.sendAndGet(message, timeout);
    }

    /**
     * Method to subscribe to receive messages of a subject and type from the domain.
     *
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose {@link cMsgCallbackInterface#callback(cMsgMessage, Object)}
     *                method is called upon receiving a message of subject and type
     * @param userObj any user-supplied object to be given to the callback method as an argument
     * @return handle object to be used for unsubscribing
     * @throws cMsgException
     */
    public cMsgSubscriptionHandle subscribe(String subject, String type, cMsgCallbackInterface cb, Object userObj)
            throws cMsgException {
        return connection.subscribe(subject, type, cb, userObj);
    }


    /**
     * Method to unsubscribe a previous subscription to receive messages of a subject and type
     * from the domain.
     *
     * @param handle the object returned from a subscribe call
     * @throws cMsgException if there are communication problems with the domain
     */
    public void unsubscribe(cMsgSubscriptionHandle handle)
            throws cMsgException {
        connection.unsubscribe(handle);
    }

    /**
     * This method is a synchronous call to receive a message containing monitoring data
     * which describes the state of the domain the user is connected to. This method, by
     * nature, is highly dependent on the specifics of the domain the client is connected
     * to. In the cMsg domain (the only one in which this method is currently implemented),
     * the argument is ignored and an XML string is returned in the message's text field.
     *
     * @param  command directive for monitoring process
     * @return response message containing monitoring information
     * @throws cMsgException
     */
    public cMsgMessage monitor(String command)
            throws cMsgException {
        return connection.monitor(command);
    }

    /**
     * Method to start or activate the subscription callbacks.
     */
    public void start() {
        connection.start();
    }

    /**
     * Method to stop or deactivate the subscription callbacks.
     */
    public void stop() {
        connection.stop();
    }

    /**
     * Method to shutdown the given clients.
     * In the cMsg domain, wildcards used to match client names with the given string.
     * Specifically, "*" means any or no characters, "?" means exactly 1 character,
     * and "#" means 1 or no positive integer.
     *
     * @param client client(s) to be shutdown
     * @param includeMe  if true, it is permissible to shutdown calling client
     * @throws cMsgException
     */
    public void shutdownClients(String client, boolean includeMe) throws cMsgException {
        connection.shutdownClients(client, includeMe);
    }


    /**
     * Method to shutdown the given servers.
     * In the cMsg domain, wildcards used to match server names with the given string.
     * Specifically, "*" means any or no characters, "?" means exactly 1 character,
     * and "#" means 1 or no positive integer.
     *
     * @param server server(s) to be shutdown
     * @param includeMyServer  if true, it is permissible to shutdown calling client's
     *                         cMsg domain server
     * @throws cMsgException
     */
    public void shutdownServers(String server, boolean includeMyServer) throws cMsgException {
        connection.shutdownClients(server, includeMyServer);
    }


    /**
     * Method to set the shutdown handler of the client.
     *
     * @param handler shutdown handler
     */
    public void setShutdownHandler(cMsgShutdownHandlerInterface handler) {
        connection.setShutdownHandler(handler);
    }

    /**
     * Method to get the shutdown handler of the client.
     *
     * @return shutdown handler object
     */
    public cMsgShutdownHandlerInterface getShutdownHandler() {
        return connection.getShutdownHandler();
    }

    /**
     * Get the name of the domain connected to.
     * @return domain name
     */
    public String getDomain() {
        return connection.getDomain();
    }

    /**
     * Get the UDL of the client.
     * @return client's UDL
     */
    public String getUDL() {
        return UDL;
    }

    /**
     * Set the UDL of the client.
     * @param UDL UDL of client
     * @throws cMsgException
     */
    public void setUDL(String UDL) throws cMsgException {
        connection.setUDL(UDL);
    }

    /**
     * Get the UDL the client is currently connected to.
     * @return UDL the client is currently connected to
     */
    public String getCurrentUDL() {
        return connection.getCurrentUDL();
    }

    /**
     * Get the UDL remainder (UDL after cMsg:domain:// is stripped off)
     * of the client.
     * @return client's UDL remainder
     */
    public String getUDLRemainder() {
        return UDLremainder;
    }

    /**
     * Get the name of the client.
     * @return client's name
     */
    public String getName() {
        return name;
    }

    /**
     * Get the client's description.
     * @return client's description
     */
    public String getDescription() {
        return description;
    }

    /**
     * Get the host the client is running on.
     * @return client's host
     */
    public String getHost() {
        return connection.getHost();
    }

    /**
     * Get a string that the implementation class wants to return up to the top (this) level API.
     * @return a string
     */
    public String getString() {
        return connection.getString();
    }

    /**
     * Method telling whether callbacks are activated or not. The
     * start and stop methods activate and deactivate the callbacks.
     * @return true if callbacks are activated, false if they are not
     */
    public boolean isReceiving() {
        return connection.isReceiving();
    }


}

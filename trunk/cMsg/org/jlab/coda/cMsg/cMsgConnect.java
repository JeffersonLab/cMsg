/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 17-Nov-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg;


import java.util.regex.Pattern;
import java.util.regex.Matcher;

/**
 * Class to direct a cMsg client to the proper subdomain based on the UDL given.
 * It implements the cMsgInterface for the most part with the exception of a few
 * irrelevant methods.
 */
public class cMsgConnect {
    /** Level of debug output for this class. */
    int debug = cMsgConstants.debugError;

    /** String containing the remainder part of the UDL. */
    private String UDLremainder;

    /** String containing the domain part of the UDL. */
    private String domain;

    private cMsgInterface connection;

    /** Constructor. */
    public cMsgConnect() {

    }

    /**
     * Constructor which automatically tries to connect to the name server specified.
     *
     * @param UDL Uniform Domain Locator which specifies the server to connect to
     * @param name name of this client which must be unique in this domain
     * @param description description of this client
     * @throws cMsgException if domain in not implemented or there are problems communicating
     *                       with the name/domain server.
     */
    public cMsgConnect(String UDL, String name, String description) throws cMsgException {
        // parse the UDL - Uniform Domain Locator
        parseUDL(UDL);

        // create real connection object to server of specific domain
        connection = createDomainConnection();

        // Since the connection object is created with a no-arg constructor,
        // we must pass in information with setters.

        // Pass in the UDL
        connection.setUDL(UDL);
        // Pass in the name
        connection.setName(name);
        // Pass in the description
        connection.setDescription(UDLremainder);
        // Pass in the UDL remainder
        connection.setUDLRemainder(UDLremainder);
    }


    /**
     * Method to parse the Universal Domain Locator (or UDL) into its various components.
     * The UDL is of the form:
     *   cMsg:<domainType>://<domain dependent remainder>
     * where the initial "cMsg" is optional
     *
     * @param UDL Universal Domain Locator
     * @throws cMsgException if UDL is null, or no domainType is given in UDL
     */
    private void parseUDL(String UDL) throws cMsgException {

        if (UDL == null) {
            throw new cMsgException("invalid UDL");
        }

        // cMsg domain UDL is of the form:
        //       cMsg:<domainType>://<domain dependent remainder>
        // initial cMsg: in not necessary

        Pattern pattern = Pattern.compile("(cMsg)?:?(\\w+)://(.*)");
        Matcher matcher = pattern.matcher(UDL);

        String s0=null, s1=null, s2=null;

        if (matcher.find()) {
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

        // must be in cMsg space
        if (s0 != null && !s0.equals("cMsg")) {
            throw new cMsgException("invalid UDL");
        }

        // need domain
        if (s1 == null) {
            throw new cMsgException("invalid UDL");
        }
        domain = s1;

        // any remaining UDL is put here
        if (s2 == null) {
            throw new cMsgException("invalid UDL");
        }
        UDLremainder = s2;
    }

    /**
     * Creates the object that makes the real connection to a particular domain's server
     * that was specified in the UDL.
     *
     * @return connection object to domain specified in UDL
     * @throws cMsgException if object could not be created
     */
    private cMsgInterface createDomainConnection()
            throws cMsgException {

        /** Object to handle client */
        cMsgInterface domainConnection = null;

         // First check to see if connection class name was set on the command line.
        String domainConnectionClass = System.getProperty("domain");

        // If it wasn't given on the command line,
        // check the appropriate environmental variable.
        if (domainConnectionClass == null) {
            domainConnectionClass = System.getenv("CMSG_DOMAIN");
        }

        // If there is still no handler class, look for the
        // standard, provided classes.
        if (domainConnectionClass == null) {
            if (domain.equals("cMsg")) {
                domainConnectionClass = "org.jlab.coda.cMsg.cMsgDomain.cMsg";
            }
            else if (domain.equals("file")) {
                domainConnectionClass = "org.jlab.coda.cMsg.FileDomain.File";
            }
        }

        // all options are exhaused, throw error
        if (domainConnectionClass == null) {
            cMsgException ex = new cMsgException("no handler class found");
            ex.setReturnCode(cMsgConstants.errorNoClassFound);
            throw ex;
        }

        // Get connection class name and create object
        try {
            domainConnection = (cMsgInterface) (Class.forName(domainConnectionClass).newInstance());
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
      * Method to connect to a particular domain server.
      *
      * @throws cMsgException
      */
     public void connect() throws cMsgException {
        connection.connect();
     }


    /**
     * Method to close the connection to the domain server. This method results in this object
     * becoming functionally useless.
     *
     * @throws cMsgException
     */
    public void disconnect() throws cMsgException {
        connection.disconnect();
    }

    /**
     * Method to determine if this object is still connected to the domain server or not.
     *
     * @return true if connected to domain server, false otherwise
     */
    public boolean isConnected() {
        return connection.isConnected();
    }

    /**
     * Method to send a message to the domain server for further distribution.
     *
     * @param message message
     * @throws cMsgException
     */
    public void send(cMsgMessage message) throws cMsgException {
        connection.send(message);
    }

    /**
     * Method to send a message to the domain server for further distribution
     * and wait for a response from the subdomain handler that got it.
     *
     * @param message message
     * @return response from subdomain handler
     * @throws cMsgException
     */
    public int syncSend(cMsgMessage message) throws cMsgException {
        return connection.syncSend(message);
    }

    /**
     * Method to force cMsg client to send pending communications with domain server.
     * @throws cMsgException
     */
    public void flush() throws cMsgException {
        connection.flush();
    }

    /**
     * This method does two separate things depending on the specifics of message in the
     * argument. If the message to be sent has its "getRequest" field set to be true using
     * {@link cMsgMessage#isGetRequest}, then the message is sent as it would be in the
     * {@link #send} method. The server notes the fact that a response to it is expected,
     * and sends it to all subscribed to its subject and type. When a marked response is
     * received from a client, it sends that first response back to the original sender
     * regardless of its subject or type.
     *
     * In a second usage, if the message did NOT set its "getRequest" field to be true,
     * then the server grabs the first incoming message of the requested subject and type
     * and sends that to the original sender in response to the get.
     *
     * @param message message sent to server
     * @param timeout time in milliseconds to wait for a reponse message
     * @return response message
     * @throws cMsgException
     */
    public cMsgMessage get(cMsgMessage message, int timeout) throws cMsgException {
        return connection.get(message, timeout);
    }

    /**
     * Method to subscribe to receive messages of a subject and type from the domain server.
     *
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @param userObj any user-supplied object to be given to the callback method as an argument
     * @throws cMsgException
     */
    public void subscribe(String subject, String type, cMsgCallback cb, Object userObj)
            throws cMsgException {
        connection.subscribe(subject, type, cb, userObj);
    }


    /**
     * Method to unsubscribe a previous subscription to receive messages of a subject and type
     * from the domain server. Since many subscriptions may be made to the same subject and type
     * values, but with different callabacks, the callback must be specified so the correct
     * subscription can be removed.
     *
     * @param subject message subject
     * @param type    message type
     * @param cb      callback object whose single method is called upon receiving a message
     *                of subject and type
     * @throws cMsgException
     */
    public void unsubscribe(String subject, String type, cMsgCallback cb) throws cMsgException {
        connection.unsubscribe(subject, type, cb);
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
     * Get the name of the domain connected to.
     * @return domain name
     */
    public String getDomain() {
        return connection.getDomain();
    }

    /**
     * Get the name of the client.
     * @return client's name
     */
    public String getName() {
        return connection.getName();
    }

    /**
     * Get the client's description.
     * @return client's description
     */
    public String getDescription() {
        return connection.getDescription();
    }

    /**
     * Get the host the client is running on.
     * @return client's host
     */
    public String getHost() {
        return connection.getHost();
    }

    /**
     * Get boolean tells whether callbacks are activated or not. The
     * start and stop methods activate and deactivate the callbacks.
     * @return true if callbacks are activated, false if they are not
     */
    public boolean isReceiving() {
        return connection.isReceiving();
    }


}

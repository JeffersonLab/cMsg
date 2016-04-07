package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.cMsgConstants;
import org.jlab.coda.cMsg.cMsgException;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Class to test regular expression parsing of UDL.
 * @author timmer
 * Date: Mar 28, 2016
 */
public class regexpUDL {

    /** Constructor. */
    regexpUDL(String[] args) {
        decodeCommandLine(args);
    }

    /** All recognized subdomains, used in parsing UDL. */
    private String[] allowedSubdomains = {"LogFile", "CA", "Database",
                                          "Queue", "FileQueue", "SmartSockets",
                                          "TcpServer", "cMsg"};

    /**
     * Method to decode the command line used to start this application.
     * @param args command line arguments
     */
    private void decodeCommandLine(String[] args) {

        // loop over all args
        for (int i = 0; i < args.length; i++) {

            if (args[i].equalsIgnoreCase("-h")) {
                usage();
                System.exit(-1);
            }
            else {
                usage();
                System.exit(-1);
            }
        }

        return;
    }


    /** Method to print out correct program command line usage. */
    private static void usage() {
        System.out.println("\nUsage:\n\n" +
            "   java etTest\n" +
            "        [-h]                 print this help\n");
    }


    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            regexpUDL tp = new regexpUDL(args);
            tp.run();
        }
        catch (cMsgException e) {
            System.out.println(e.toString());
            System.exit(-1);
        }
    }



    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {

        try {
            String udl = "cmsg:cmsg://host:23456/";
            //String udl = "cmsg:cmsg://host:23456/NS/";
            //String udl = "cmsg:cmsg://host:23456/NS/?junk";
            //String udl = "cmsg:cmsg://host:23456/?junk";
            //String udl = "cmsg:cmsg://host:23456/cMsg/NS/?junk";
            //String udl = "cMsg:cMsg://host:23456/Database?driver=myDriver&url=myURL&";
            // strip off the cMsg:cMsg:// to begin with
            String udlLowerCase = udl.toLowerCase();
            int index = udlLowerCase.indexOf("cmsg://");
            if (index < 0) {
                throw new cMsgException("invalid UDL");
            }
            String udlRemainder = udl.substring(index+7);

            Pattern pattern = Pattern.compile("([^:/]+):?(\\d+)?/?(\\w+)?/?(.*)");
            Matcher matcher = pattern.matcher(udlRemainder);

            String udlHost, udlPort, udlSubdomain, udlSubRemainder;

            if (matcher.find()) {
                // host
                udlHost = matcher.group(1);
                // port
                udlPort = matcher.group(2);
                // subdomain
                udlSubdomain = matcher.group(3);
                // remainder
                udlSubRemainder = matcher.group(4);

                    System.out.println("\nparseUDL: " +
                                               "\n  host      = " + udlHost +
                                               "\n  port      = " + udlPort +
                                               "\n  subdomain = " + udlSubdomain +
                                               "\n  remainder = " + udlSubRemainder);
            }
            else {
                throw new cMsgException("invalid UDL");
            }


            // if subdomain not specified, use cMsg subdomain
            if (udlSubdomain == null) {
                udlSubdomain = "cMsg";
            }
            else {
                // Make sure the sub domain is recognized.
                // Because the cMsg subdomain is the only one in which a "/" is contained
                // in the remainder, and because the presence of the "cMsg" subdomain identifier
                // is optional, what will happen when it's parsed is that the namespace will be
                // interpreted as the subdomain if "cMsg" domain identifier is not there.
                // Thus we must take care of this case. If we don't recognize the subdomain,
                // assume it's the namespace of the cMsg subdomain.
                boolean foundSubD = false;
                for (String subDom: allowedSubdomains) {
                    if (subDom.equalsIgnoreCase(udlSubdomain)) {
                        foundSubD = true;
                        System.out.println("Match");
                        break;
                    }
                }

                if (!foundSubD) {
                    if (udlSubRemainder == null || udlSubRemainder.length() < 1) {
                        udlSubRemainder = udlSubdomain;
                        System.out.println("NO Match 1, use cMsg & remainder = " + udlSubRemainder);
                    }
                    else {
                        udlSubRemainder = udlSubdomain + "/" + udlSubRemainder;
                        System.out.println("NO Match 2, use cMsg & remainder = " + udlSubRemainder);
                    }
                    udlSubdomain = "cMsg";
                }
            }

            System.out.println("sub domain = " + udlSubdomain);

            // parse udlSubRemainder to find the namespace this client is in
            Pattern pattern2 = Pattern.compile("^([\\w/]*)\\?*.*");
            Matcher matcher2 = pattern2.matcher(udlSubRemainder);

            String s, namespace;

            if (matcher2.lookingAt()) {
                s = matcher2.group(1);
            }
            else {
                throw new cMsgException("invalid namespace");
            }

            if (s == null) {
                throw new cMsgException("invalid namespace");
            }

            // strip off all except one beginning slash and all ending slashes
            while (s.startsWith("/")) {
                s = s.substring(1);
            }

            while (s.endsWith("/")) {
                s = s.substring(0, s.length()-1);
            }

            // if namespace is blank, use default
            if (s.equals("")) {
                namespace = "/default";
            }
            else {
                namespace = "/" + s;
            }

            System.out.println("namespace = " + namespace);

        }
        catch (Exception e) {
            e.printStackTrace();
        }

    }

}

/*----------------------------------------------------------------------------*
 *  Copyright (c) 2008        Southeastern Universities Research Association, *
 *                            Jefferson Science Associates                    *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C.Timmer, 17-Sep-2008, Jefferson Lab                                    *
 *                                                                            *
 *    Authors: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

package org.jlab.coda.cMsg.apps;

import java.net.*;
import java.util.Enumeration;
import java.util.LinkedList;
import java.util.List;


/**
 * This is an class which printout out some local network information.
 */
public class cMsgFindLocalNetworkInfo {

    /**
     * Run as a stand-alone application.
     * @param args arguments.
     */
    public static void main(String[] args) {

        String canonicalIP = null;
         try {
             InetAddress localHost = InetAddress.getLocalHost();
             System.out.println("Canonical host name = " + localHost.getCanonicalHostName());
             System.out.println("Canonical host IP   = " + InetAddress.getByName(localHost.
                                                 getCanonicalHostName()).getHostAddress());
         }
         catch (UnknownHostException e) {}


         // List of our IP addresses
         LinkedList<String> ipList = new LinkedList<String>();

         try {
             System.out.println("\nIterating over interfaces:");
             Enumeration<NetworkInterface> enumer = NetworkInterface.getNetworkInterfaces();
             while (enumer.hasMoreElements()) {
                 NetworkInterface ni = enumer.nextElement();
                 if (ni.isUp()) {
                    System.out.println("  On network interface " + ni.getDisplayName() + ":");
                 }
                 else {
                     System.out.println("  On network interface " + ni.getDisplayName() + " (down):");
                 }
                 if (!ni.isLoopback()) {
                     List<InterfaceAddress> inAddrs = ni.getInterfaceAddresses();
                     for (InterfaceAddress ifAddr : inAddrs) {
                         InetAddress addr = ifAddr.getAddress();

                         // skip IPv6 addresses (IPv4 addr has 4 bytes)
                         if (addr.getAddress().length != 4) continue;

                         String ipAddr = addr.getHostAddress();
                         if (!ipAddr.equals(canonicalIP)) {
                             ipList.add(addr.getHostAddress());
                             System.out.println("    " + ipAddr);
                         }
                     }
                 }
             }
         }
         catch (SocketException e) {
             e.printStackTrace();
         }


        try {
            System.out.println("\nGetting all IP addresses of canonical host directly:");
            InetAddress[] localAddrs = InetAddress.getAllByName(InetAddress.getLocalHost().getCanonicalHostName());

            for (InetAddress addr : localAddrs) {
                System.out.println("  " + addr.getHostAddress() + " (name = " + addr.getHostName() + ")");
            }
         }
        catch (UnknownHostException e) {
            e.printStackTrace();
        }


    }

}

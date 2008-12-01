/*----------------------------------------------------------------------------*
*  Copyright (c) 2004        Southeastern Universities Research Association, *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    E.Wolin, 6-Jan-2005, Jefferson Lab                                      *
*                                                                            *
*    Authors: Elliott Wolin                                                  *
*             wolin@jlab.org                    Jefferson Lab, MS-6B         *
*             Phone: (757) 269-7365             12000 Jefferson Ave.         *
*             Fax:   (757) 269-6248             Newport News, VA 23606       *
*                                                                            *
*             Carl Timmer                                                    *
*             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
*             Phone: (757) 269-5130             12000 Jefferson Ave.         *
*             Fax:   (757) 269-6248             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/


package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.*;


import java.lang.*;
import java.io.*;
import java.sql.*;
import java.net.*;
import java.nio.channels.*;


//-----------------------------------------------------------------------------


/**
 * Dumps cMsgQueue binary files to XML;
 *
 * @version 1.0
 */

public class cMsgFileToXML {


    public static void main(String[] args) {

        if(args.length==0) {
            usage();
            System.exit(0);
        } else if (args[0].equalsIgnoreCase("-h")) {
            usage();
            System.exit(0);
        }


        FileInputStream fis = null;
        ObjectInputStream oin = null;
        try {
            fis = new FileInputStream(args[0]);
            oin = new ObjectInputStream(fis);
            cMsgMessageFull m = (cMsgMessageFull) oin.readObject();
            oin.close();
            //            m.expandPayload();   ???
            System.out.print(m);

        } catch (FileNotFoundException e) {
            System.out.println("?cMsgFileToXML...file does not exist");
            System.exit(-1);

        } catch (ClassNotFoundException e) {
            e.printStackTrace();
            System.exit(-1);

        } catch (IOException e) {
            e.printStackTrace();
            System.exit(-1);
        }

    }


//-----------------------------------------------------------------------------


    /** Method to print out correct program command line usage. */
    static private void usage() {
        System.out.println("\nUsage:\n\n java cMsgFileToXML fileName\n");
    }


//-----------------------------------------------------------------------------
}

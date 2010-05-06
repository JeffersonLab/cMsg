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


package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.*;
import java.io.*;


//-----------------------------------------------------------------------------


/**
 * Tests serialization/deserialization
 * @version 1.0
 */
public class cMsgSerializeTest {


    private static String UDL = "cMsg://localhost/cMsg/myNameSpace";
    private static cMsg cmsg  = null;
    private static String name = null;
    private static String description = null;


    /** dir not null to use file queue. */
    private static String dir             = null;
    private static String base            = null;
    private static String fileBase        = null;


//-----------------------------------------------------------------------------


    static public void main(String[] args) {


        // decode command line
        decode_command_line(args);


        // connect to cMsg system
//         try {
//             cmsg = new cMsg(UDL, name, description);
//             cmsg.connect();
//         } catch (cMsgException e) {
//             e.printStackTrace();
//             System.exit(-1);
//         }



        // create and fill message
        cMsgMessageFull msg = new cMsgMessageFull();
        msg.setSubject("aSubject");
        msg.setType("aType");
        msg.setText("hello world");
        msg.setUserInt(123);
        byte[] b = new byte[32];  for(int i=0; i<32; i++) b[i]=(byte)i;
        msg.setByteArray(b);
        try {
            msg.addPayloadItem(new cMsgPayloadItem("anInt",123));
            msg.addPayloadItem(new cMsgPayloadItem("aFloat",(float)234.0));
            msg.addPayloadItem(new cMsgPayloadItem("aDouble",(double)345.0));
            msg.addPayloadItem(new cMsgPayloadItem("aString","this is a payload string"));
            msg.addPayloadItem(new cMsgPayloadItem("Int8Array",b));

            cMsgMessage mcopy = new cMsgMessage(msg);
            msg.addPayloadItem(new cMsgPayloadItem("message",mcopy));
        } catch (cMsgException e) {
            e.printStackTrace();
            return;
        }


        // print message and payload string
        System.out.println("The message:");
        System.out.println(msg.toString());
        System.out.println("");
        System.out.println("Payload text:");
        System.out.println(msg.getPayloadText());
        System.out.println("");


        // serialize message to file
        msg.compressPayload();
        FileOutputStream fos = null;
        ObjectOutputStream out = null;
        try {
            fos = new FileOutputStream("serialize.dat");
            out = new ObjectOutputStream(fos);
            out.writeObject(msg);
            out.close();
        } catch (IOException ex) {
            ex.printStackTrace();
        }



        // deserialize message from file
        cMsgMessageFull mm;
        FileInputStream fis = null;
        ObjectInputStream in = null;
        try {
            fis = new FileInputStream("serialize.dat");
            in = new ObjectInputStream(fis);
            mm = (cMsgMessageFull) in.readObject();
            in.close();
            //            mm.expandPayload();
            System.out.println("Deserialized message:");
            System.out.println(mm.toString());
            System.out.println("");
            System.out.println("Payload text:");
            System.out.println(mm.getPayloadText());
            System.out.println("");
        } catch (IOException ex) {
            ex.printStackTrace();
        } catch (ClassNotFoundException ex) {
            ex.printStackTrace();
        }



//         // disconnect
//         try {
//             cmsg.disconnect();
//         } catch (Exception e) {
//             System.exit(-1);
//         }

        // done
        System.exit(0);
    }


//-----------------------------------------------------------------------------


    /**
     * Method to decode the command line used to start this application.
     * @param args command line arguments
     */
    static private void decode_command_line(String[] args) {


        // loop over all args
        for (int i = 0; i < args.length; i++) {

            if (args[i].equalsIgnoreCase("-name")) {
                name = args[i + 1];
                i++;

            } else if (args[i].equalsIgnoreCase("-descr")) {
                description = args[i + 1];
                i++;

            } else if (args[i].equalsIgnoreCase("-udl")) {
                UDL= args[i + 1];
                i++;
            }
        }

        return;
    }

}

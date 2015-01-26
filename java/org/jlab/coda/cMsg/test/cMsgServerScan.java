package org.jlab.coda.cMsg.test;

import org.jlab.coda.cMsg.cMsgException;
import org.jlab.coda.cMsg.cMsgNetworkConstants;

import java.io.*;
import java.net.Socket;

/**
 * Created by timmer on 1/26/15.
 */
public class cMsgServerScan {

    static String host = "tania.jlab.org";
    static int serverTcpPort = cMsgNetworkConstants.nameServerTcpPort;

    static public void scan() {

        // connect & talk to cMsg name server to check if name is unique
        Socket nsSocket = null;
        try {
            nsSocket = new Socket(host, serverTcpPort);
            // Set tcpNoDelay so no packets are delayed
            nsSocket.setTcpNoDelay(true);
            // no need to set buffer sizes
        }
        catch (IOException e) {
            try {
                if (nsSocket != null) nsSocket.close();
            }
            catch (IOException e1) {}
            System.out.println("scan: cannot create socket to cMsg server");
            return;
        }

        // get host & port to send messages & other info from name server
        try {
            DataOutputStream out = new DataOutputStream(new BufferedOutputStream(nsSocket.getOutputStream()));

//            out.writeInt(cMsgNetworkConstants.magicNumbers[0]);
//            out.writeInt(cMsgNetworkConstants.magicNumbers[1]);
            out.writeInt(0);
            out.writeInt(0);
//            out.writeInt(0);
            out.flush();
            Thread.sleep(10000);
            out.close();
        }
        catch (Exception e) {}

        // done talking to server
        try {
            nsSocket.close();
        }
        catch (IOException e) {}
    }

    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        scan();
    }


}

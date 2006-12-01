package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;

import java.util.concurrent.CountDownLatch;

/**
 *
 */
public class RCBroadcastServer {

    String rcClientHost;
    int rcClientTcpPort;
    String name;
    CountDownLatch latch;
    int count;

    class BroadcastCallback extends cMsgCallbackAdapter {
         public void callback(cMsgMessage msg, Object userObject) {
             rcClientHost = msg.getSenderHost();
             rcClientTcpPort = msg.getUserInt();
             name = msg.getSender();
             
             System.out.println("RAN CALLBACK, host = " + rcClientHost +
                                ", port = " + rcClientTcpPort +
                                ", name = " + name);
             latch.countDown();
        }
    }


    class rcCallback extends cMsgCallbackAdapter {
        public void callback(cMsgMessage msg, Object userObject) {
            count++;
            String s = (String) userObject;
            System.out.println("RAN CALLBACK, msg text = " + msg.getText() + ", for " + s);
        }
    }


    public RCBroadcastServer() {
        latch = new CountDownLatch(1);
    }


    public static void main(String[] args) throws cMsgException {
        RCBroadcastServer server = new RCBroadcastServer();
        server.run();
    }


    public void run() throws cMsgException {
        System.out.println("Starting RC Broadcast Server");

        // RC Broadcast domain UDL is of the form:
        //       cMsg:rcb://<udpPort>?expid=<expid>
        //
        // The intial cMsg:rcb:// is stripped off by the top layer API
        //
        // Remember that for this domain:
        // 1) udp listening port is optional and defaults to 6543 (cMsgNetworkConstants.rcBroadcastPort)
        // 2) the experiment id is given by the optional parameter expid. If none is
        //    given, the environmental variable EXPID is used. if that is not defined,
        //    an exception is thrown

        String UDL = "cMsg:rcb://?expid=carlExp";

        cMsg cmsg = new cMsg(UDL, "broadcast listener", "udp trial");
        try {cmsg.connect();}
        catch (cMsgException e) {
            System.out.println(e.getMessage());
            return;
        }

        // enable message reception
        cmsg.start();

        BroadcastCallback cb = new BroadcastCallback();
        // subject and type are ignored in this domain
        cmsg.subscribe("sub", "type", cb, null);

        // wait for incoming message from rc client
        try { latch.await(); }
        catch (InterruptedException e) {}



        // now that we have the message, we know what TCP host and port
        // to connect to in the RC Server domain.
        System.out.println("Starting RC Server");

        // send stuff to rc client
        cMsgMessage msg = new cMsgMessage();
        msg.setSubject("rcSubject");
        msg.setType("rcType");

        //try {Thread.sleep(4000);}
        //catch (InterruptedException e) {}

        // RC Server domain UDL is of the form:
        //       cMsg:rcs://<host>:<tcpPort>?port=<udpPort>
        //
        // The intial cMsg:rcs:// is stripped off by the top layer API
        //
        // Remember that for this domain:
        // 1) host is NOT optional (must start with an alphabetic character according to "man hosts" or
        //    may be in dotted form (129.57.35.21)
        // 2) host can be "localhost"
        // 3) tcp port is optional and defaults to 7654 (cMsgNetworkConstants.rcClientPort)
        // 4) the udp port to listen on may be given by the optional port parameter.
        //    if it's not given, the system assigns one
        String rcsUDL = "cMsg:rcs://" + rcClientHost + ":" + rcClientTcpPort + "/?port=5859";

        cMsg server1 = new cMsg(rcsUDL, "rc server", "udp trial");
        server1.connect();

        server1.start();

        rcCallback cb2 = new rcCallback();
        Object unsub = server1.subscribe("subby", "typey", cb2, "1st sub");

        int loops = 5;
        while(loops-- > 0) {
            //System.out.println("Send command to rc client");
            server1.send(msg);
            try {Thread.sleep(500);}
            catch (InterruptedException e) {}
        }
        server1.unsubscribe(unsub);
        server1.disconnect();


        // test reconnect feature
        System.out.println("Now wait 2 seconds and connect again");
        try {Thread.sleep(2000);}
        catch (InterruptedException e) {}

        cMsg server2 = new cMsg(rcsUDL, "rc server", "udp trial");
        server2.connect();
        server2.start();
        server2.subscribe("subby", "typey", cb2, "2nd sub");

        loops = 5;
        while(loops-- > 0) {
            server2.send(msg);
            try {Thread.sleep(500);}
            catch (InterruptedException e) {}
        }
        

    }


}

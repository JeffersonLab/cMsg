package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.cMsgDomain.cMsg;

import java.util.Date;

/**
 * Created by IntelliJ IDEA.
 * User: timmer
 * Date: Oct 21, 2004
 * Time: 10:50:02 AM
 * To change this template use File | Settings | File Templates.
 */
public class cMsgGetResponder {
    String name;
    long count;
    cMsgConnect coda;

    cMsgGetResponder(String name) {
        this.name = name;
    }

    /**
     * Run as a stand-alone application.
     */
    public static void main(String[] args) {
        try {
            cMsgGetResponder responder = null;
            if (args.length > 0) {
                responder = new cMsgGetResponder(args[0]);
            }
            else {
                responder = new cMsgGetResponder("responder");
                System.out.println("Name of this client is \"responder\"");
            }
            responder.run();
        }
        catch (cMsgException e) {
            e.printStackTrace();
            System.exit(-1);
        }
    }


    class myCallback extends cMsgCallbackAdapter {
        /**
         * Callback method definition.
         *
         * @param msg message received from domain server
         * @param userObject object passed as an argument which was set when the
         *                   client orginally subscribed to a subject and type of
         *                   message.
         */
        public void callback(cMsgMessage msg, Object userObject) {
            try {
                cMsgMessage sendMsg = msg.response();
                sendMsg.setSubject("RESPONDING");
                sendMsg.setType("TO MESSAGE");
                coda.send(sendMsg);
            }
            catch (cMsgException e) {
                e.printStackTrace();
            }
            //System.out.println(".");
        }

        public boolean maySkipMessages() {
            return false;
        }

        public boolean mustSerializeMessages() {
            return true;
        }

        public int  getMaximumThreads() {
            return 200;
        }

     }


    /**
     * This method is executed as a thread.
     */
    public void run() throws cMsgException {
        String subject = "responder", type = "TYPE";

        System.out.println("Running Message GET Responder\n");

        String UDL = "cMsg:cMsg://aslan:3456/cMsg";

        System.out.print("Try to connect ...");
        coda = new cMsgConnect(UDL, name, "getResponder");
        coda.connect();
        System.out.println(" done");

        System.out.println("Enable message receiving");
        coda.start();

        System.out.println("Subscribe to subject = " + subject + ", type = " + type);
        cMsgCallback cb = new cMsgGetResponder.myCallback();
        coda.subscribe(subject, type, cb, null);
    }
}

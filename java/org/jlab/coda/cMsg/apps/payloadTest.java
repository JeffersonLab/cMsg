package org.jlab.coda.cMsg.apps;

import org.jlab.coda.cMsg.*;

import java.math.BigInteger;
import java.math.BigDecimal;
import java.util.Date;


/**
 *
 */
public class payloadTest {

    public static void main(String[] args) {
        Byte b = (byte)127;
        Byte[] ba = new Byte[] {127,5};
        byte[] bar = new byte[] {127,5, 12, -127, 0};
        Short[] sa = new Short[] {127,5};
        BigDecimal bb = new BigDecimal("456.34");
        String[] ses = new String[] {"one", "two", "three"};
        try {
//            long t = 111222L;
//            System.out.println("     Seconds = " + (t/1000));   // sec
//            System.out.println("MilliSeconds = " + (t - ((t/1000L)*1000L)));   // sec
//            System.out.println(" NanoSeconds = " + (t - ((t/1000L)*1000L))*1000000L);   // sec
//            cMsgPayloadItem item = new cMsgPayloadItem("hey", sa);
//            System.out.println("Text:\n" + item.getText());
//            item = new cMsgPayloadItem("ho", bar, cMsgConstants.endianBig);
//            System.out.println("Text:\n" + item.getText());
//            item = new cMsgPayloadItem("ho", "MY STRING");
//            System.out.println("Text:\n" + item.getText());
            cMsgPayloadItem item = new cMsgPayloadItem("ho", ses);
            cMsgPayload pyld = new cMsgPayload();
            pyld.add(item);
          //  System.out.println("Text:\n" + item.getText());
            cMsgMessage msg = new cMsgMessage();
            msg.setSubject("sub");
            msg.setType("typ");
            msg.setText("txt");
            msg.setPayload(pyld);

            cMsgPayloadItem item2 = new cMsgPayloadItem("MY message", msg);
            System.out.println("Text:\n" + item2.getText());
        }
        catch (cMsgException e) {
            e.printStackTrace();  //To change body of catch statement use File | Settings | File Templates.
        }
    }
}

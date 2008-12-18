// still to do:


/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *     E.Wolin, 5-oct-2004                                                    *
 *                                                                            *
 *     Author: Elliott Wolin                                                  *
 *             wolin@jlab.org                    Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-7365             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/


package org.jlab.coda.cMsg.cMsgDomain.subdomains;

import org.jlab.coda.cMsg.*;
import org.jlab.coda.cMsg.common.*;

import java.io.*;
import java.nio.channels.*;
import java.util.*;
import java.util.regex.*;



//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * cMsg subdomain handler for FileQueue subdomain.<p>
 *
 * UDL:  cMsg:cMsg://host:port/FileQueue/myQueueName?dir=myDirName.<p>
 *
 * e.g. cMsg:cMsg://ollie/FileQueue/ejw?dir=/home/wolin/qdir.<p>
 *
 * Stores/retrieves cMsgMessageFull messages from file-based queue.
 *
 * @author Elliiott Wolin
 * @version 1.0
 */
public class FileQueue extends cMsgSubdomainAdapter {


    /** global list of queue names, for synchronizing access to queues */
    private static HashMap<String,Object> queueHashMap = new HashMap<String,Object>(100);


    /** registration params. */
    private cMsgClientInfo myClientInfo;


    /** UDL remainder for this subdomain handler. */
    private String myUDLRemainder;


    /** Object used to deliver messages to the client. */
    private cMsgDeliverMessageInterface myDeliverer;


    /** for synchronizing access to queue */
    private Object mySyncObject;


    // database access objects
    private String myQueueNameFull      = null;
    private String myDirectory          = ".";
    private String myFileNameBase       = null;
    private String myLoSeqFile          = null;
    private String myHiSeqFile          = null;


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     * @return true
     */
    public boolean hasSend() {
        return true;
    };


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     * @return true
     */
    public boolean hasSendAndGet() {
        return true;
    }


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     * @return true
     */
    public boolean hasSyncSend() {
        return true;
    };


//-----------------------------------------------------------------------------


    /**
     * {@inheritDoc}
     *
     * @param UDLRemainder {@inheritDoc}
     * @throws cMsgException never
     */
    public void setUDLRemainder(String UDLRemainder) throws cMsgException {
        myUDLRemainder=UDLRemainder;
    }


//-----------------------------------------------------------------------------


    /**
     * Creates files to store messages in.
     *
     * @param info information about client
     * @throws cMsgException upon bad UDL, cannot create file names or files
     */
    public void registerClient(cMsgClientInfo info) throws cMsgException {

        Pattern p;
        Matcher m;
        String remainder = null;


        // set myClientInfo
        myClientInfo=info;


        // Get an object enabling this handler to communicate
        // with only this client in this cMsg subdomain.
        myDeliverer = info.getDeliverer();


        // extract queue name from UDL remainder
        String myQueueName;
        if(myUDLRemainder.indexOf("?")>0) {
            p = Pattern.compile("^(.+?)(\\?.*)$");
            m = p.matcher(myUDLRemainder);
            if(m.find()) {
                myQueueName = m.group(1);
                remainder   = m.group(2) + "&";
            } else {
                cMsgException ce = new cMsgException("?illegal UDL");
                ce.setReturnCode(1);
                throw ce;
            }
        } else {
            myQueueName=myUDLRemainder;
        }


        // extract directory name from remainder
        if(remainder!=null) {
            p = Pattern.compile("[&\\?]dir=(.*?)&", Pattern.CASE_INSENSITIVE);
            m = p.matcher(remainder);
            if(m.find()) myDirectory = m.group(1);
        }


        // get canonical dir name
        String myCanonicalDir;
        try {
            myCanonicalDir = new File(myDirectory).getCanonicalPath();
        } catch (IOException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException("Unable to get directory canonical name for " + myDirectory);
            ce.setReturnCode(1);
            throw ce;
        }


        // form various file names using canonical dir name
        if(myCanonicalDir!=null) {
            myQueueNameFull = myCanonicalDir + "/cMsgFileQueue_" + myQueueName;
            myFileNameBase  = myQueueNameFull + "_";
            myHiSeqFile     = myFileNameBase + "Hi";
            myLoSeqFile     = myFileNameBase + "Lo";
        } else {
            cMsgException ce = new cMsgException("?null canonical dir name for " + myDirectory);
            ce.setReturnCode(1);
            throw ce;
        }


        // create queue entry if needed
        synchronized(queueHashMap) {
             if(!queueHashMap.containsKey(myQueueNameFull)) {
                 queueHashMap.put(myQueueNameFull,new Object());
             }
        }


        // get sync/lock object for this queue
        mySyncObject=queueHashMap.get(myQueueNameFull);


        // synchronize on queue name, then init files if needed
        synchronized(mySyncObject) {

            File hi = new File(myHiSeqFile);
            File lo = new File(myLoSeqFile);
            if(!hi.exists()||!lo.exists()) {
                try {
                    FileWriter h = new FileWriter(hi);  h.write("0\n");  h.close();
                    FileWriter l = new FileWriter(lo);  l.write("0\n");  l.close();
                } catch (IOException e) {
                    e.printStackTrace();
                    cMsgException ce = new cMsgException(e.toString());
                    ce.setReturnCode(1);
                    throw ce;
                }
            }
        }


    }


//-----------------------------------------------------------------------------


    /**
     * Write message to queue file.
     *
     * @param msg {@inheritDoc}
     * @throws cMsgException if error writing file
     */
    public void handleSendRequest(cMsgMessageFull msg) throws cMsgException {

        long hi;


        // get creator before payload compression
        String creator = null;
        try {
            cMsgPayloadItem creatorItem = msg.getPayloadItem("cMsgCreator");
            if(creatorItem!=null)creator=creatorItem.getString();
        } catch (cMsgException e) {
            System.err.println("?cMsgQueue...message has no creator!");
        }


        // compress payload
        msg.compressPayload();


        // write message to queue
        synchronized(mySyncObject) {

            try {

                // lock hi file
                RandomAccessFile r = new RandomAccessFile(myHiSeqFile,"rw");
                FileChannel c = r.getChannel();
                FileLock l = c.lock();

                // read hi file, increment, write out new hi value
                hi=Long.parseLong(r.readLine());
                hi++;
                r.seek(0);
                r.writeBytes(hi+"\n");

                // serialize compressed message, then write to file
                FileOutputStream fos = null;
                ObjectOutputStream oos = null;
                fos = new FileOutputStream(String.format("%s%08d",myFileNameBase,hi));
                oos = new ObjectOutputStream(fos);
                oos.writeObject(msg);
                oos.close();


                // unlock and close hi file
                l.release();
                r.close();


                } catch (IOException e) {
                    e.printStackTrace();
                    cMsgException ce = new cMsgException(e.toString());
                    ce.setReturnCode(1);
                    throw ce;
                }
        }

    }


//-----------------------------------------------------------------------------


    /**
     * Write message to queue file.
     *
     * @param msg {@inheritDoc}
     * @return 0 on success, else 1
     * @throws cMsgException never
     */
    public int handleSyncSendRequest(cMsgMessageFull msg) throws cMsgException {
        try {
            handleSendRequest(msg);
            return(0);
        } catch (cMsgException e) {
            return(1);
        }
    }


//-----------------------------------------------------------------------------


    /**
     * Returns message at head of queue.
     *
     * @param message message to generate sendAndGet response to (contents ignored)
     * @throws cMsgException if error reading file or delivering message to client
     */
    public void handleSendAndGetRequest(cMsgMessageFull message) throws cMsgException {

        RandomAccessFile rHi,rLo;
        long hi,lo;


        cMsgMessageFull response = null;


        // get message off queue
        synchronized(mySyncObject) {

            try {

                // lock hi file
                rHi = new RandomAccessFile(myHiSeqFile,"rw");
                FileChannel cHi = rHi.getChannel();
                FileLock lHi = cHi.lock();

                // lock lo file
                rLo = new RandomAccessFile(myLoSeqFile,"rw");
                FileChannel cLo = rLo.getChannel();
                FileLock lLo = cLo.lock();

                // read files
                hi=Long.parseLong(rHi.readLine());
                lo=Long.parseLong(rLo.readLine());

                // message file exists only if hi>lo
                if(hi>lo) {
                    lo++;
                    rLo.seek(0);
                    rLo.writeBytes(lo+"\n");


                    // create response from file if it exists
                    FileInputStream fis = null;
                    ObjectInputStream oin = null;
                    try {
                        fis = new FileInputStream(String.format("%s%08d",myFileNameBase,lo));
                        oin = new ObjectInputStream(fis);
                        response = (cMsgMessageFull) oin.readObject();
                        oin.close();
                        fis.close();
                        new File(String.format("%s%08d",myFileNameBase,lo)).delete();
                        response.expandPayload();
                        response.makeResponse(message);

                    } catch (FileNotFoundException e) {
                        System.err.println("?missing message file " + lo + " in queue " + myQueueNameFull);

                    } catch (ClassNotFoundException e) {
                        e.printStackTrace();
                        System.exit(-1);

                    } catch (IOException e) {
                        e.printStackTrace();
                        System.exit(-1);
                    }
                }

                // unlock and close files
                lLo.release();
                lHi.release();
                rHi.close();
                rLo.close();

            } catch (IOException e) {
                e.printStackTrace();
                cMsgException ce = new cMsgException(e.toString());
                ce.setReturnCode(1);
                throw ce;

            }
        }


        // create null response if needed
        if(response==null) {
            response = cMsgMessageFull.createDeliverableMessage();
            response.makeNullResponse(message);
        }


        // deliver response to client
        try {
            myDeliverer.deliverMessage(response, cMsgConstants.msgGetResponse);
        } catch (IOException e) {
            e.printStackTrace();
            cMsgException ce = new cMsgException(e.toString());
            ce.setReturnCode(1);
            throw ce;
        }

    }

}


//-----------------------------------------------------------------------------

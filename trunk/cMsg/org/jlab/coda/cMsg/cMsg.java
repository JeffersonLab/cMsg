/*----------------------------------------------------------------------------*
*  Copyright (c) 2004        Southeastern Universities Research Association, *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    E.Wolin, 29-Jun-2004, Jefferson Lab                                     *
*                                                                            *
*    Authors: Elliott Wolin                                                  *
*             wolin@jlab.org                    Jefferson Lab, MS-6B         *
*             Phone: (757) 269-7365             12000 Jefferson Ave.         *
*             Fax:   (757) 269-5800             Newport News, VA 23606       *
*                                                                            *
*             Carl Timmer                                                    *
*             timmer@jlab.org                   Jefferson Lab, MS-6B         *
*             Phone: (757) 269-5130             12000 Jefferson Ave.         *
*             Fax:   (757) 269-5800             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/
//  implements cMsg API for CODA domain

package org.jlab.coda.cMsg;

import java.lang.*;



public interface cMsg {

    public int send(String subject, String type, String text);


    public int flush();


    public int get(String subject, String type, int timeout, cMsgMessage msg);


    public int subscribe(String subject, String type, cMsgCallback cb, Object userObj);


    public int subscribe(String subject, String type, cMsgCallback cb);


    public int done();


    public int start();


    public int stop();


    public String getDomain();


    public String getName();


    public String getDescription();


    public String getHost();


    public int getSendSocket();


    public int getReceiveSocket();


    public int getPendThread();


    public boolean getReceiveState();


}

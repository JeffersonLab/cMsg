//  cMsgMessage.java
//
//
//   *----------------------------------------------------------------------------*
//   *  Copyright (c) 2004        Southeastern Universities Research Association, *
//   *                            Thomas Jefferson National Accelerator Facility  *
//   *                                                                            *
//   *    This software was developed under a United States Government license    *
//   *    described in the NOTICE file included as part of this distribution.     *
//   *                                                                            *
//   *    E.Wolin, 29-Jun-2004, Jefferson Lab                                     *
//   *                                                                            *
//   *    Authors: Elliott Wolin                                                  *
//   *             wolin@jlab.org                    Jefferson Lab, MS-6B         *
//   *             Phone: (757) 269-7365             12000 Jefferson Ave.         *
//   *             Fax:   (757) 269-5800             Newport News, VA 23606       *
//   *                                                                            *
//   *             Carl Timmer                                                    *
//   *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
//   *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
//   *             Fax:   (757) 269-5800             Newport News, VA 23606       *
//   *                                                                            *
//   *----------------------------------------------------------------------------*
//
//
//  Message object for cMsg system
//


public class cMsgMessage {

    public String initialized   = false;
    public String domain        = null;
    public String name          = null;
    public String description   = null;
    public int sendSocket       = 0;
    public int receiveSocket    = 0;
    public int pendThread       = 0;
    public String host          = null;
    public bool receiveState    = false;


//-----------------------------------------------------------------------------
}        //  end class definition
//-----------------------------------------------------------------------------

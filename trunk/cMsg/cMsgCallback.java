//  cMsgCallback.java
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
// cMsg callback interface definition for cMsg system
//
//


public interface cMsgCallback {

    public void callback(cMsgMessage msg, Object userObject) {
    }


//-----------------------------------------------------------------------------
}        //  end interface definition
//-----------------------------------------------------------------------------

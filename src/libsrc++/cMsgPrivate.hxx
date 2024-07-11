/*----------------------------------------------------------------------------*
*  Copyright (c) 2005        Southeastern Universities Research Association, *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    E.Wolin, 14-Jun-2007, Jefferson Lab                                     *
*                                                                            *
*    Authors: Elliott Wolin                                                  *
*             wolin@jlab.org                    Jefferson Lab, MS-12H5       *
*             Phone: (757) 269-7365             12000 Jefferson Ave.         *
*             Fax:   (757) 269-5519             Newport News, VA 23606       *
*
*----------------------------------------------------------------------------*/


#ifndef _cMsgPrivate_hxx
#define _cMsgPrivate_hxx


//-----------------------------------------------------------------------------


/**
 * This class provides a cMsg C callback method that dispatchs to a member function of another object, used internally.
 * 
 * This object extends cMsgCallback and thus has a callback method.  
 * It further stores a pointer to another object and to a member function of the object.
 * That member function is invoked by the callback method of this object.
 */ 
template <class T> class cMsgDispatcher : public cMsgCallback {

private:
  T *t;   /**<Pointer to object containing member function.*/
  void (T::*mfp)(cMsgMessage *msg, void* userArg); /**<Pointer to the member function.*/

public:
  /**
   * Constructor stores object pointer and member function pointer.
   *
   * @param t Object pointer
   * @param mfp Member function pointer
   */
  cMsgDispatcher(T *t, void (T::*mfp)(cMsgMessage *msg, void* userArg)): t(t), mfp(mfp) {}


  /** Callback method dispatches to member function. 
   *
   * @param msg Message. 
   * @param userArg User arg.
   */
  void callback(cMsgMessage *msg, void* userArg) {
    (t->*mfp)(msg,userArg);
  }

};


//-----------------------------------------------------------------------------



#endif /* _cMsgPrivate_hxx */

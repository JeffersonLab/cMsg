// to do
//   cMsgMessageBase
//   test callback class
//   add remaining functions
//   Carl:  const void*, const timespec*


/** 
 * Class definitions for cMsg C++ wrapper class.
 */


/*----------------------------------------------------------------------------*
*  Copyright (c) 2005        Southeastern Universities Research Association, *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    E.Wolin, 25-Feb-2005, Jefferson Lab                                     *
*                                                                            *
*    Authors: Elliott Wolin                                                  *
*             wolin@jlab.org                    Jefferson Lab, MS-6B         *
*             Phone: (757) 269-7365             12000 Jefferson Ave.         *
*             Fax:   (757) 269-5519             Newport News, VA 23606       *
*
*----------------------------------------------------------------------------*/


#ifndef _cMsgBase_hxx
#define _cMsgBase_hxx


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsgException {

  /** Most cMsg functions throw pointer to cMsgException, 
   *   which contains return code and error description string.
   *
   * @version 1.0
   */


private:
  char *myDescr;
  int myReturnCode;


public:

  cMsgException(void);
  cMsgException(const char *c);
  cMsgException(const char *c, int i);
  ~cMsgException(void);

  virtual void setReturnCode(int i);
  virtual int getReturnCode(void);
  virtual const char *toString(void);
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsgMessageBase {
  
  /**
   * Wrapper for cMsg message class.
   *
   * @version 1.0
   */


  // allow cMsg to see myMsgPointer
  friend class cMsg;
  
  
private:
  void *myMsgPointer;
  
  
public:
  
  cMsgMessageBase(void);
  cMsgMessageBase(void *msgPointer);
  ~cMsgMessageBase(void);

  virtual const char *getSubject(void);
  virtual void setSubject(const char *subject);
  virtual const char *getType(void);
  virtual void setType(const char *type);
  virtual const char *getText(void);
  virtual void setText(const char *text);
  virtual int getUserInt(void);
  virtual void setUserInt(int i);
  virtual int getVersion(void);
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsgCallback {

  /**
   * Pure virtual base for needed for c interface.
   *
   * @version 1.0
   */

public:
  virtual void callback(cMsgMessageBase &msg, const void *userObject) = 0;
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsgCallbackInterface:public cMsgCallback {

  /**
   * Pure virtual class for specifying subscription options.
   *
   * @version 1.0
   */

public:
  virtual bool maySkipMessages(void) = 0;
  virtual bool mustSerializeMessages(void) = 0;
  virtual int getMaxCueSize(void) = 0;
  virtual int getSkipSize(void) = 0;
  virtual int getMaxThreads(void) = 0;
  virtual int getMessagesPerThread(void) = 0;
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsgCallbackAdapter:public cMsgCallbackInterface {

  /**
   * Concrete class implements cMsgCallbackInterface default methods.
   *
   * @version 1.0
   */

public:
  virtual void callback(cMsgMessageBase &msg, const void *userObject);
  virtual bool maySkipMessages(void);
  virtual bool mustSerializeMessages(void);
  virtual int getMaxCueSize(void);
  virtual int getSkipSize(void);
  virtual int getMaxThreads(void);
  virtual int getMessagesPerThread(void);
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsg {

  /**
   * Wrapper for main cMsg class.
   * Uses special cMsg C++ functions to accomodate instance pointers in
   *   subscribe() and unsubscribe() methods.
   *
   * @version 1.0
   */

private:
  int myDomainId;


public:

  cMsg(const char *UDL, const char *name, const char *descr);
  ~cMsg(void);

  virtual void send(cMsgMessageBase &msg);
  virtual int  syncSend(cMsgMessageBase &msg);
  virtual void flush(void);
  virtual void subscribe(const char *subject, const char *type, cMsgCallbackAdapter *cba, const void *userArg);
  virtual void subscribe(const char *subject, const char *type, cMsgCallbackAdapter &cba, const void *userArg);
  virtual void unsubscribe(const char *subject, const char *type, cMsgCallbackAdapter *cba, const void *userArg);
  virtual void unsubscribe(const char *subject, const char *type, cMsgCallbackAdapter &cba, const void *userArg);
  virtual void sendAndGet(cMsgMessageBase &sendMsg, const struct timespec *timeout, cMsgMessageBase **replyMsg);
  virtual void subscribeAndGet(const char *subject, const char *type, const struct timespec *timeout, cMsgMessageBase **replyMsg);
  virtual void start(void);
  virtual void stop(void);
  virtual void disconnect(void);
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

#endif /* _cMsgBase_hxx */

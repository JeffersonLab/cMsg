// to do
//   subscription configs...see java interface
//   add remaining functions, fix exception error codes
//   const?
//   put cMsg.hxx in cMsg.h



/** 
 * Wrapper class definitions for cMsg package.
 *
 * Method code resides in cMsg.cc.
 *
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



//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsgException {

  /** Most cMsg functions throw cMsgException, 
   *   which contains return code and error description string.
   *
   * @version 1.0
   */


private:
  char *descr;
  int returnCode;


public:

  cMsgException::cMsgException(void);
  cMsgException::cMsgException(const char *c);
  cMsgException::cMsgException(const char *c, int i);
  cMsgException::~cMsgException(void);

  void cMsgException::setReturnCode(int i);
  int cMsgException::getReturnCode(void);
  const char *cMsgException::toString(void);

};



//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsgMessage {
  
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
  
  cMsgMessage::cMsgMessage(void);
  cMsgMessage::cMsgMessage(void *msgPointer);
  cMsgMessage::~cMsgMessage(void);

  const char *cMsgMessage::getSubject(void);
  void cMsgMessage::setSubject(char *subject);
  const char *cMsgMessage::getType(void);
  void cMsgMessage::setType(char *type);
  const char *cMsgMessage::getText(void);
  void cMsgMessage::setText(char *text);
  int cMsgMessage::getUserInt(void);
  void cMsgMessage::setUserInt(int i);
  int cMsgMessage::getVersion(void);

};



//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsgCallback {

  /**
   * Base for callback adapter, needed for c interface.
   * Includes just one virtual method callback().
   *
   * @version 1.0
   */

public:
  void cMsgCallback::callback(cMsgMessage &msg, void *userObject);

};



//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsgCallbackAdapter:cMsgCallback {

  /**
   * Empty, just extends cMsgCallback.
   *
   * @version 1.0
   */

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

  cMsg::cMsg(char *UDL, char *name, char *descr);

  void cMsg::send(cMsgMessage &msg);
  int  cMsg::syncSend(cMsgMessage &msg);
  void cMsg::flush(void);
  void cMsg::subscribe(char *subject, char *type, cMsgCallback *cba, void *userArg);
  void cMsg::unsubscribe(char *subject, char *type, cMsgCallback *cba, void *userArg);
  void cMsg::sendAndGet(cMsgMessage &sendMsg, struct timespec *timeout, cMsgMessage &replyMsg);
  void cMsg::subscribeAndGet(char *subject, char *type, struct timespec *timeout, cMsgMessage &replyMsg);
  void cMsg::start(void);
  void cMsg::stop(void);
  void cMsg::disconnect(void);

};



//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

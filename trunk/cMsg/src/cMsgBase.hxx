/** 
 * Class definitions for cMsg C++ wrapper classes.
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


using namespace std;
#include <string>


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsgException {

  /**
   * Most cMsg functions throw a cMsgException, 
   *   which contains return code and error description string.
   *
   * @version 1.0
   */


private:
  string myDescr;
  int myReturnCode;


public:
  cMsgException(void);
  cMsgException(const string &c);
  cMsgException(const string &c, int i);
  cMsgException(const cMsgException &e);
  ~cMsgException(void);

  virtual void setReturnCode(int i);
  virtual int getReturnCode(void);
  virtual string toString(void);
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsgMessageBase {
  
  /**
   * Wrapper for cMsg message class.  
   *
   * Due to a name clash between the C and C++ API's, cMsgMessageBase 
   *  is typedef'd to cMsgMessage in cMsg.hxx, so it is only 
   *  available in user code.
   *
   * Internally cMsgMessageBase is always used.
   *
   * @version 1.0
   */


  // allow cMsg to see myMsgPointer
  friend class cMsg;
  
  
private:
  void *myMsgPointer;
  
  
public:
  cMsgMessageBase(void) throw(cMsgException);
  cMsgMessageBase(const cMsgMessageBase &m) throw(cMsgException);
  cMsgMessageBase(void *msgPointer) throw(cMsgException);
  ~cMsgMessageBase(void) throw(cMsgException);

  virtual string getSubject(void) throw(cMsgException);
  virtual void setSubject(const string &subject) throw(cMsgException);
  virtual string getType(void) throw(cMsgException);
  virtual void setType(const string &type) throw(cMsgException);
  virtual string getText(void) throw(cMsgException);
  virtual void setText(const string &text) throw(cMsgException);
  virtual int getUserInt(void) throw(cMsgException);
  virtual void setUserInt(int i) throw(cMsgException);
  virtual timespec getUserTime(void) throw(cMsgException);
  virtual void setUserTime(const timespec &userTime) throw(cMsgException);
  virtual int getVersion(void) throw(cMsgException);
  virtual string getDomain(void) throw(cMsgException);
  virtual string getCreator(void) throw(cMsgException);
  virtual string getReceiver(void) throw(cMsgException);
  virtual string getReceiverHost(void) throw(cMsgException);
  virtual string getSender(void) throw(cMsgException);
  virtual string getSenderHost(void) throw(cMsgException);
  virtual timespec getReceiverTime(void) throw(cMsgException);
  virtual timespec getSenderTime(void) throw(cMsgException);
  virtual bool isGetRequest(void) throw(cMsgException);
  virtual bool isGetResponse(void) throw(cMsgException);
  virtual bool isNullGetResponse(void) throw(cMsgException);
  virtual void makeNullResponse(cMsgMessageBase &msg) throw(cMsgException);
  virtual void makeNullResponse(cMsgMessageBase *msg) throw(cMsgException);
  virtual void makeResponse(cMsgMessageBase &msg) throw(cMsgException);
  virtual void makeResponse(cMsgMessageBase *msg) throw(cMsgException);
  virtual void setGetResponse(bool b) throw(cMsgException);
  virtual void setNullGetResponse(bool b) throw(cMsgException);
  virtual string toString(void) throw(cMsgException);
  virtual cMsgMessageBase copy(void) throw(cMsgException);
  virtual cMsgMessageBase nullResponse(void) throw(cMsgException);
  virtual cMsgMessageBase response(void) throw(cMsgException);
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsgCallback {

  /**
   * Pure virtual base class for needed for c interface.
   *
   * @version 1.0
   */

public:
  virtual void callback(cMsgMessageBase msg, void *userObject) = 0;
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsgShutdownHandlerInterface {

  /**
   * Pure virtual class defines shutdown handler method.
   *
   * @version 1.0
   */

public:
  virtual void handleShutdown(void) = 0;
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


class cMsgShutdownHandlerDefault:public cMsgShutdownHandlerInterface {

  /**
   * Concrete class supplies default shutdown handler.
   *
   * @version 1.0
   */

public:
  virtual void handleShutdown(void);
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
   * Concrete class implements default cMsgCallbackInterface methods and 
   *  sets default parameters.
   *
   * Users can override as desired.
   *
   * @version 1.0
   */

public:
  virtual void callback(cMsgMessageBase msg, void *userObject);
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
   * Main cMsg wrapper class.
   *
   * The C++ API is as similar to the Java API as possible, but they are 
   *   different due to differnces in the two languages.
   *
   * @version 1.0
   */

private:
  int myDomainId;
  string myUDL;
  string myName;
  string myDescr;
  bool connected;
  bool receiving;


public:
  cMsg(const string &UDL, const string &name, const string &descr);
  ~cMsg(void);

  virtual void connect() throw(cMsgException);
  virtual void disconnect(void) throw(cMsgException);
  virtual void send(cMsgMessageBase &msg) throw(cMsgException);
  virtual void send(cMsgMessageBase *msg) throw(cMsgException);
  virtual int  syncSend(cMsgMessageBase &msg) throw(cMsgException);
  virtual int  syncSend(cMsgMessageBase *msg) throw(cMsgException);
  virtual void subscribe(const string &subject, const string &type, cMsgCallbackAdapter *cba, void *userArg)
    throw(cMsgException);
  virtual void subscribe(const string &subject, const string &type, cMsgCallbackAdapter &cba, void *userArg)
    throw(cMsgException);
  virtual void unsubscribe(const string &subject, const string &type, cMsgCallbackAdapter *cba, void *userArg)
    throw(cMsgException);
  virtual void unsubscribe(const string &subject, const string &type, cMsgCallbackAdapter &cba, void *userArg)
    throw(cMsgException);
  virtual cMsgMessageBase sendAndGet(cMsgMessageBase &sendMsg, const timespec &timeout) throw(cMsgException);
  virtual cMsgMessageBase sendAndGet(cMsgMessageBase *sendMsg, const timespec &timeout) throw(cMsgException);
  virtual cMsgMessageBase subscribeAndGet(const string &subject, const string &type, const timespec &timeout)
    throw(cMsgException);
  virtual void flush(void) throw(cMsgException);
  virtual void start(void) throw(cMsgException);
  virtual void stop(void) throw(cMsgException);
  virtual string getUDL(void);
  virtual string getName(void);
  virtual string getDescription(void);
  virtual bool isConnected(void);
  virtual bool isReceiving(void);
  virtual void setShutdownHandler(cMsgShutdownHandlerInterface *handler, void* userArg) throw(cMsgException);
  virtual void shutdown(string &client, string &server, int flag) throw(cMsgException);
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

#endif /* _cMsgBase_hxx */

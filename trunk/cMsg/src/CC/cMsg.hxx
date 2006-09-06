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


#ifndef _cMsg_hxx
#define _cMsg_hxx


#include <cMsg.h>

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
  virtual ~cMsgException(void);

  virtual void setReturnCode(int i);
  virtual int getReturnCode(void) const;
  virtual string toString(void) const;
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
  cMsgMessage(void) throw(cMsgException);
  cMsgMessage(const cMsgMessage &m) throw(cMsgException);
  cMsgMessage(void *msgPointer) throw(cMsgException);
  virtual ~cMsgMessage(void);

  virtual string getSubject(void) const throw(cMsgException);
  virtual void setSubject(const string &subject) throw(cMsgException);
  virtual string getType(void) const throw(cMsgException);
  virtual void setType(const string &type) throw(cMsgException);
  virtual string getText(void) const throw(cMsgException);
  virtual void setText(const string &text) throw(cMsgException);
  virtual void setByteArrayLength(int length) throw(cMsgException);
  virtual int getByteArrayLength(void) const throw(cMsgException);
  virtual void setByteArrayOffset(int offset) throw(cMsgException);
  virtual int getByteArrayOffset(void) const throw(cMsgException);
  virtual int getByteArrayEndian(void) const throw(cMsgException);
  virtual void setByteArrayEndian(int endian) throw(cMsgException);
  virtual bool needToSwap(void) const throw(cMsgException);
  virtual void setByteArray(char *array) throw(cMsgException);
  virtual char* getByteArray(void) const throw(cMsgException);
  virtual void setByteArrayAndLimits(char *array, int offset, int length) throw(cMsgException);
  virtual void copyByteArray(char* array, int offset, int length) throw(cMsgException);
  virtual int getUserInt(void) const throw(cMsgException);
  virtual void setUserInt(int i) throw(cMsgException);
  virtual struct timespec getUserTime(void) const throw(cMsgException);
  virtual void setUserTime(const struct timespec &userTime) throw(cMsgException);
  virtual int getVersion(void) const throw(cMsgException);
  virtual string getDomain(void) const throw(cMsgException);
  virtual string getCreator(void) const throw(cMsgException);
  virtual string getReceiver(void) const throw(cMsgException);
  virtual string getReceiverHost(void) const throw(cMsgException);
  virtual string getSender(void) const throw(cMsgException);
  virtual string getSenderHost(void) const throw(cMsgException);
  virtual struct timespec getReceiverTime(void) const throw(cMsgException);
  virtual struct timespec getSenderTime(void) const throw(cMsgException);
  virtual bool isGetRequest(void) const throw(cMsgException);
  virtual bool isGetResponse(void) const throw(cMsgException);
  virtual bool isNullGetResponse(void) const throw(cMsgException);
  virtual void makeNullResponse(cMsgMessage &msg) throw(cMsgException);
  virtual void makeNullResponse(cMsgMessage *msg) throw(cMsgException);
  virtual void makeResponse(cMsgMessage &msg) throw(cMsgException);
  virtual void makeResponse(cMsgMessage *msg) throw(cMsgException);
  virtual void setGetResponse(bool b) throw(cMsgException);
  virtual void setNullGetResponse(bool b) throw(cMsgException);
  virtual string toString(void) const throw(cMsgException);
  virtual cMsgMessage *copy(void) const throw(cMsgException);
  virtual cMsgMessage *nullResponse(void) const throw(cMsgException);
  virtual cMsgMessage *response(void) const throw(cMsgException);
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
  virtual void callback(cMsgMessage *msg, void *userObject) = 0;
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
  virtual size_t getStackSize(void) = 0;
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
  virtual void callback(cMsgMessage *msg, void *userObject) = 0;
  virtual bool maySkipMessages(void);
  virtual bool mustSerializeMessages(void);
  virtual int getMaxCueSize(void);
  virtual int getSkipSize(void);
  virtual int getMaxThreads(void);
  virtual int getMessagesPerThread(void);
  virtual size_t getStackSize(void);
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
  void  *myDomainId;
  string myUDL;
  string myName;
  string myDescr;
  bool initialized;


public:
  cMsg(const string &UDL, const string &name, const string &descr);
  virtual ~cMsg(void);
  virtual void connect() throw(cMsgException);
  virtual void disconnect(void) throw(cMsgException);
  virtual void send(cMsgMessage &msg) throw(cMsgException);
  virtual void send(cMsgMessage *msg) throw(cMsgException);
  virtual int  syncSend(cMsgMessage &msg, const struct timespec *timeout = NULL) throw(cMsgException);
  virtual int  syncSend(cMsgMessage *msg, const struct timespec *timeout = NULL) throw(cMsgException);
  virtual void *subscribe(const string &subject, const string &type, cMsgCallbackAdapter *cba, void *userArg)
    throw(cMsgException);
  virtual void *subscribe(const string &subject, const string &type, cMsgCallbackAdapter &cba, void *userArg)
    throw(cMsgException);
  virtual void unsubscribe(void *handle) throw(cMsgException);
  virtual cMsgMessage *sendAndGet(cMsgMessage &sendMsg, const struct timespec *timeout = NULL) 
    throw(cMsgException);
  virtual cMsgMessage *sendAndGet(cMsgMessage *sendMsg, const struct timespec *timeout = NULL)
    throw(cMsgException);
  virtual cMsgMessage *subscribeAndGet(const string &subject, const string &type, const struct timespec *timeout = NULL)
    throw(cMsgException);
  virtual void flush(const struct timespec *timeout = NULL) throw(cMsgException);
  virtual void start(void) throw(cMsgException);
  virtual void stop(void) throw(cMsgException);
  virtual string getUDL(void) const;
  virtual string getName(void) const;
  virtual string getDescription(void) const;
  virtual bool isConnected(void) const;
  virtual bool isReceiving(void) const;
  virtual void setShutdownHandler(cMsgShutdownHandler *handler, void* userArg) throw(cMsgException);
  virtual void shutdownClients(string &client, int flag) throw(cMsgException);
  virtual void shutdownServers(string &server, int flag) throw(cMsgException);
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


#endif /* _cMsg_hxx */


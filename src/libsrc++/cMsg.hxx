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
#include <string>
#include <exception>
#include <vector>
#include <map>


/**
 * All cMsg symbols reside in the cmsg namespace.  
 */
namespace cmsg {

using namespace std;


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * Exception includes description and return code.
 */
class cMsgException : public exception {

public:
  cMsgException(void);
  cMsgException(const string &descr);
  cMsgException(const string &descr, int code);
  cMsgException(const cMsgException &e);
  virtual ~cMsgException(void) throw();

  virtual string toString(void) const throw();
  virtual const char *what(void) const throw();


public:
  string descr;    /**<Description.*/
  int returnCode;  /**<Return code.*/
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * Class for wrapping cMsg message.  
 */
class cMsgMessage {

  friend class cMsg;  /**<Allows cMsg to see myMsgPointer.*/
  
  
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
  virtual string getSubscriptionDomain() const throw(cMsgException);
  virtual string getSubscriptionSubject() const throw(cMsgException);
  virtual string getSubscriptionType() const throw(cMsgException);
  virtual string getSubscriptionUDL() const throw(cMsgException);
  virtual int    getSubscriptionCueSize(void) const throw(cMsgException);
  virtual bool   getReliableSend(void) const throw(cMsgException);
  virtual void   setReliableSend(bool b) throw(cMsgException);
  
  //---------------
  // PAYLOAD STUFF
  //---------------

  virtual bool   hasPayload() const;
  
  virtual void   payloadClear(void);
  virtual void   payloadPrint(void);
  virtual void   payloadSetFromText(const string &text) throw(cMsgException);
  virtual void   payloadSetSystemFieldsFromText(const string &text) throw(cMsgException);
  virtual void   payloadSetAllFieldsFromText(const string &text) throw(cMsgException);
  virtual void   payloadCopy(cMsgMessage &msg) throw(cMsgException);

  virtual bool   payloadRemoveField(const string &name);
  virtual string payloadGetFieldDescription(const string &name) const throw(cMsgException);  
  virtual string payloadGetText() const throw(cMsgException);
  
  virtual map<string,int> *payloadGet() const throw(cMsgException);
  virtual int    payloadGetCount() const;
  virtual bool   payloadContainsName (const string &name) const;
  virtual int    payloadGetType      (const string &name) const throw(cMsgException);
  virtual string payloadGetFieldText (const string &name) const throw(cMsgException);
  
  //
  // Methods to get a payload item's value
  //
  virtual void   getBinary(string name, char **val, int &len, int &endian) const throw(cMsgException);

  virtual cMsgMessage *getMessage(string name) const throw(cMsgException);
  virtual vector<cMsgMessage> *getMessageVector(string name) const throw(cMsgException);

  virtual string getString(string name) const throw(cMsgException);
  virtual vector<string> *getStringVector(string name) const throw(cMsgException);
  
  virtual float  getFloat(string name) const throw(cMsgException);
  virtual double getDouble(string name) const throw(cMsgException);
  virtual vector<float> *getFloatVector(string name) const throw(cMsgException);
  virtual vector<double> *getDoubleVector(string name) const throw(cMsgException);
  
  virtual int8_t   getInt8(string name)   const throw(cMsgException);
  virtual int16_t  getInt16(string name)  const throw(cMsgException);
  virtual int32_t  getInt32(string name)  const throw(cMsgException);
  virtual int64_t  getInt64(string name)  const throw(cMsgException);
  virtual uint8_t  getUint8(string name)  const throw(cMsgException);
  virtual uint16_t getUint16(string name) const throw(cMsgException);
  virtual uint32_t getUint32(string name) const throw(cMsgException);
  virtual uint64_t getUint64(string name) const throw(cMsgException);
  
  virtual vector<int8_t>   *getInt8Vector (string name) const throw(cMsgException);
  virtual vector<int16_t>  *getInt16Vector(string name) const throw(cMsgException);
  virtual vector<int32_t>  *getInt32Vector(string name) const throw(cMsgException);
  virtual vector<int64_t>  *getInt64Vector(string name) const throw(cMsgException);

  virtual vector<uint8_t>  *getUint8Vector (string name) const throw(cMsgException);
  virtual vector<uint16_t> *getUint16Vector(string name) const throw(cMsgException);
  virtual vector<uint32_t> *getUint32Vector(string name) const throw(cMsgException);
  virtual vector<uint64_t> *getUint64Vector(string name) const throw(cMsgException);
  
  //
  // Methods to add items to a payload
  //
  virtual void addBinary(string name, const char *src, int size, int endian);
  
  virtual void addString(string name, string s);
  virtual void addStringArray(string name, const char **strs, int len);
  virtual void addStringArray(string name, string *strs, int len);
  virtual void addStringVector(string name, vector<string> &strs);

  virtual void addMessage(string name, cMsgMessage &msg);
  virtual void addMessageArray(string name, cMsgMessage *msg, int len);
  virtual void addMessageVector(string name, vector<cMsgMessage> &msg);

  virtual void addFloat(string name, float val);
  virtual void addDouble(string name, double val);
  virtual void addFloatArray(string name, float *vals, int len);
  virtual void addDoubleArray(string name, double *vals, int len);
  virtual void addFloatVector(string name, vector<float> &vals);
  virtual void addDoubleVector(string name, vector<double> &vals);

  virtual void addInt8 (string name, int8_t  val);
  virtual void addInt16(string name, int16_t val);
  virtual void addInt32(string name, int32_t val);
  virtual void addInt64(string name, int64_t val);
   
  virtual void addUint8 (string name, uint8_t  val);
  virtual void addUint16(string name, uint16_t val);
  virtual void addUint32(string name, uint32_t val);
  virtual void addUint64(string name, uint64_t val);
  
  virtual void addInt8Array (string name, int8_t *vals,  int len);
  virtual void addInt16Array(string name, int16_t *vals, int len);
  virtual void addInt32Array(string name, int32_t *vals, int len);
  virtual void addInt64Array(string name, int64_t *vals, int len);
   
  virtual void addUint8Array (string name, uint8_t *vals,  int len);
  virtual void addUint16Array(string name, uint16_t *vals, int len);
  virtual void addUint32Array(string name, uint32_t *vals, int len);
  virtual void addUint64Array(string name, uint64_t *vals, int len);
  
  virtual void addInt8Vector (string name, vector<int8_t>  &vals);
  virtual void addInt16Vector(string name, vector<int16_t> &vals);
  virtual void addInt32Vector(string name, vector<int32_t> &vals);
  virtual void addInt64Vector(string name, vector<int64_t> &vals);
  
  virtual void addUint8Vector (string name, vector<uint8_t>  &vals);
  virtual void addUint16Vector(string name, vector<uint16_t> &vals);
  virtual void addUint32Vector(string name, vector<uint32_t> &vals);
  virtual void addUint64Vector(string name, vector<uint64_t> &vals);
   

//private:
  void *myMsgPointer;  /**<Pointer to C message structure.*/
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * Interface defines callback method.
 */
class cMsgCallback {

public:
  virtual void callback(cMsgMessage *msg, void *userObject) = 0;
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * Manages subscriptions configurations.
 */
class cMsgSubscriptionConfig {

public:
  cMsgSubscriptionConfig(void);
  virtual ~cMsgSubscriptionConfig(void);

  virtual int getMaxCueSize(void);
  virtual void setMaxCueSize(int size);
  virtual int getSkipSize(void);
  virtual void setSkipSize(int size);
  virtual bool getMaySkip(void);
  virtual void setMaySkip(bool maySkip);
  virtual bool getMustSerialize(void);
  virtual void setMustSerialize(bool mustSerialize);
  virtual int getMaxThreads(void);
  virtual void setMaxThreads(int max);
  virtual int getMessagesPerThread(void);
  virtual void setMessagesPerThread(int mpt);
  virtual size_t getStackSize(void);
  virtual void setStackSize(size_t size);

public:
  cMsgSubscribeConfig *config;   /**<Pointer to subscription config struct.*/
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------


/**
 * Wraps most cMsg C calls, provides main functionality.
 */
class cMsg {

public:
  cMsg(const string &UDL, const string &name, const string &descr);
  virtual ~cMsg(void);
  virtual void connect() throw(cMsgException);
  virtual void disconnect(void) throw(cMsgException);
  virtual void send(cMsgMessage &msg) throw(cMsgException);
  virtual void send(cMsgMessage *msg) throw(cMsgException);
  virtual int  syncSend(cMsgMessage &msg, const struct timespec *timeout = NULL) throw(cMsgException);
  virtual int  syncSend(cMsgMessage *msg, const struct timespec *timeout = NULL) throw(cMsgException);
  virtual void *subscribe(const string &subject, const string &type, cMsgCallback *cb, void *userArg, 
                          const cMsgSubscriptionConfig *cfg = NULL) throw(cMsgException);
  virtual void *subscribe(const string &subject, const string &type, cMsgCallback &cb, void *userArg,
                          const cMsgSubscriptionConfig *cfg = NULL) throw(cMsgException);
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
  virtual void shutdownClients(const string &client, int flag) throw(cMsgException);
  virtual void shutdownServers(const string &server, int flag) throw(cMsgException);
  virtual cMsgMessage *monitor(const string &monString) throw(cMsgException);


private:
  void  *myDomainId;    /**<C Domain id.*/
  string myUDL;         /**<UDL.*/
  string myName;        /**<Name.*/
  string myDescr;       /**<Description.*/
  bool initialized;     /**<True if initialized.*/
};


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// private templates that should not be in doxygen doc
#include <cMsgPrivate.hxx>


} // namespace cMsg

#endif /* _cMsg_hxx */


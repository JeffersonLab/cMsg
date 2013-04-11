// still to do:

//  why is myMsgPointer public?
//  is friend class needed?
//  should subscription config be public?
//  add stream operators to cMsgMessage?  e.g:  msg << setName(aName) << aValue;



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
class cMsgException : public std::exception {

public:
  cMsgException(void);
  cMsgException(const string &descr);
  cMsgException(const string &descr, int code);
  cMsgException(const cMsgException &e);
  virtual ~cMsgException(void) throw();

  virtual string toString(void)  const throw();
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
  cMsgMessage(void)                   throw(cMsgException);
  cMsgMessage(const cMsgMessage &m)   throw(cMsgException);
  cMsgMessage(void *msgPointer) throw(cMsgException);
  virtual ~cMsgMessage(void);

  virtual string getSubject(void)               const throw(cMsgException);
  virtual void   setSubject(const string &subject)    throw(cMsgException);
  virtual string getType(void)                  const throw(cMsgException);
  virtual void   setType(const string &type)          throw(cMsgException);
  virtual string getText(void)                  const throw(cMsgException);
  virtual void   setText(const string &text)          throw(cMsgException);
  
  virtual void   setByteArrayLength(int length)       throw(cMsgException);
  virtual void   resetByteArrayLength();
  virtual int    getByteArrayLength(void);
  virtual int    getByteArrayLengthFull(void);
  virtual void   setByteArrayOffset(int offset)       throw(cMsgException);
  virtual int    getByteArrayOffset(void);  
  virtual int    getByteArrayEndian(void);
  virtual void   setByteArrayEndian(int endian)       throw(cMsgException);
  virtual bool   needToSwap(void)               const throw(cMsgException);  
  virtual char*  getByteArray(void);
  virtual void   setByteArray(char *array, int length)       throw(cMsgException);
  virtual void   setByteArrayNoCopy(char* array, int length) throw(cMsgException);

  virtual int    getUserInt(void)               const throw(cMsgException);
  virtual void   setUserInt(int i)                    throw(cMsgException);
  virtual struct timespec getUserTime(void)     const throw(cMsgException);
  virtual void   setUserTime(const struct timespec &userTime) throw(cMsgException);
  virtual int    getVersion(void)               const throw(cMsgException);
  virtual string getDomain(void)                const throw(cMsgException);
  virtual string getReceiver(void)              const throw(cMsgException);
  virtual string getReceiverHost(void)          const throw(cMsgException);
  virtual string getSender(void)                const throw(cMsgException);
  virtual string getSenderHost(void)            const throw(cMsgException);
  virtual struct timespec getReceiverTime(void) const throw(cMsgException);
  virtual struct timespec getSenderTime(void)   const throw(cMsgException);
  virtual bool   isGetRequest(void)             const throw(cMsgException);
  virtual bool   isGetResponse(void)            const throw(cMsgException);
  virtual bool   isNullGetResponse(void)        const throw(cMsgException);

  virtual void   makeNullResponse(const cMsgMessage &msg)   throw(cMsgException);
  virtual void   makeNullResponse(const cMsgMessage *msg)   throw(cMsgException);
  virtual void   makeResponse(const cMsgMessage &msg)       throw(cMsgException);
  virtual void   makeResponse(const cMsgMessage *msg)       throw(cMsgException);

  virtual void   setGetResponse(bool b)               throw(cMsgException);
  virtual void   setNullGetResponse(bool b)           throw(cMsgException);
  virtual string toString(void)                 const throw(cMsgException);
  virtual cMsgMessage *copy(void)               const throw(cMsgException);
  virtual cMsgMessage *nullResponse(void)       const throw(cMsgException);
  virtual cMsgMessage *response(void)           const throw(cMsgException);
  virtual string getSubscriptionDomain()        const throw(cMsgException);
  virtual string getSubscriptionSubject()       const throw(cMsgException);
  virtual string getSubscriptionType()          const throw(cMsgException);
  virtual string getSubscriptionUDL()           const throw(cMsgException);
  virtual int    getSubscriptionCueSize(void)   const throw(cMsgException);
  virtual bool   getReliableSend(void)          const throw(cMsgException);
  virtual void   setReliableSend(bool b)              throw(cMsgException);
  


  //---------------
  // PAYLOAD STUFF
  //---------------


public:

  virtual bool   hasPayload() const;
  
  virtual void   payloadClear(void);
  virtual void   payloadReset(void);
  virtual void   payloadPrint(void) const;
  virtual void   payloadCopy(const cMsgMessage &msg)            throw(cMsgException);

  virtual bool   payloadRemoveField(const string &name);
  virtual string payloadGetText() const;
  virtual void   payloadSetFromText(const string &txt)          throw(cMsgException);
  virtual string payloadGetFieldDescription(const string &name) const throw(cMsgException);  
  
  virtual map<string,int> *payloadGet()                         const throw(cMsgException);
  virtual int    payloadGetCount()                              const;
  virtual bool   payloadContainsName (const string &name)       const;
  virtual int    payloadGetType      (const string &name)       const throw(cMsgException);
  virtual void   setHistoryLengthMax (int len)                  const throw(cMsgException);
  

  //
  // Methods to get a payload item's value
  //
  virtual void getBinary(const string &name, const char **val, int &len, int &endian)
               const throw(cMsgException);
  virtual void getBinaryArray(const string &name, const char ***vals, int **lens, int **endians, int &count)
               const throw(cMsgException);

  virtual cMsgMessage          *getMessage(const string &name)        const throw(cMsgException);
  virtual vector<cMsgMessage>  *getMessageVector(const string &name)  const throw(cMsgException);
  virtual vector<cMsgMessage*> *getMessagePVector(const string &name) const throw(cMsgException);
  virtual cMsgMessage          *getMessageArray (const string &name)  const throw(cMsgException);
  virtual cMsgMessage*         *getMessagePArray (const string &name) const throw(cMsgException);

  virtual string          getString(const string &name)       const throw(cMsgException);
  virtual vector<string> *getStringVector(const string &name) const throw(cMsgException);
  virtual string         *getStringArray(const string &name)  const throw(cMsgException);
  
  virtual float           getFloat(const string &name)        const throw(cMsgException);
  virtual vector<float>  *getFloatVector(const string &name)  const throw(cMsgException);
  virtual float          *getFloatArray(const string &name)   const throw(cMsgException);

  virtual double          getDouble(const string &name)       const throw(cMsgException);
  virtual vector<double> *getDoubleVector(const string &name) const throw(cMsgException);
  virtual double         *getDoubleArray(const string &name)  const throw(cMsgException);
  
  virtual int8_t            getInt8  (const string &name)      const throw(cMsgException);
  virtual int16_t           getInt16 (const string &name)      const throw(cMsgException);
  virtual int32_t           getInt32 (const string &name)      const throw(cMsgException);
  virtual int64_t           getInt64 (const string &name)      const throw(cMsgException);

  virtual vector<int8_t>   *getInt8Vector (const string &name) const throw(cMsgException);
  virtual vector<int16_t>  *getInt16Vector(const string &name) const throw(cMsgException);
  virtual vector<int32_t>  *getInt32Vector(const string &name) const throw(cMsgException);
  virtual vector<int64_t>  *getInt64Vector(const string &name) const throw(cMsgException);

  virtual int8_t           *getInt8Array  (const string &name) const throw(cMsgException);
  virtual int16_t          *getInt16Array (const string &name) const throw(cMsgException);
  virtual int32_t          *getInt32Array (const string &name) const throw(cMsgException);
  virtual int64_t          *getInt64Array (const string &name) const throw(cMsgException);


  virtual uint8_t           getUint8 (const string &name)       const throw(cMsgException);
  virtual uint16_t          getUint16(const string &name)       const throw(cMsgException);
  virtual uint32_t          getUint32(const string &name)       const throw(cMsgException);
  virtual uint64_t          getUint64(const string &name)       const throw(cMsgException);
  
  virtual vector<uint8_t>  *getUint8Vector (const string &name) const throw(cMsgException);
  virtual vector<uint16_t> *getUint16Vector(const string &name) const throw(cMsgException);
  virtual vector<uint32_t> *getUint32Vector(const string &name) const throw(cMsgException);
  virtual vector<uint64_t> *getUint64Vector(const string &name) const throw(cMsgException);

  virtual uint8_t          *getUint8Array  (const string &name) const throw(cMsgException);
  virtual uint16_t         *getUint16Array (const string &name) const throw(cMsgException);
  virtual uint32_t         *getUint32Array (const string &name) const throw(cMsgException);
  virtual uint64_t         *getUint64Array (const string &name) const throw(cMsgException);
  

  //
  // Methods to add items to a payload
  //

  virtual void add(const string &name, const char *src, int size, int endian);
  virtual void add(const string &name, const char **srcs, int number,
                   const int sizes[], const int endians[]);
  
  virtual void add(const string &name, const string &s);
  virtual void add(const string &name, const string *s);
  virtual void add(const string &name, const char **strs, int len);
  virtual void add(const string &name, const string *strs, int len);
  virtual void add(const string &name, const vector<string> &strs);
  virtual void add(const string &name, const vector<string> *strs);

  virtual void add(const string &name, const cMsgMessage &msg);
  virtual void add(const string &name, const cMsgMessage  *msg);
  virtual void add(const string &name, const cMsgMessage  *msg, int len);
  virtual void add(const string &name, const cMsgMessage* *msg, int len);
  virtual void add(const string &name, const vector<cMsgMessage> &msgVec);
  virtual void add(const string &name, const vector<cMsgMessage> *msgVec);
  virtual void add(const string &name, const vector<cMsgMessage*> &msgPVec);
  virtual void add(const string &name, const vector<cMsgMessage*> *msgPVec);

  virtual void add(const string &name, float val);
  virtual void add(const string &name, double val);
  virtual void add(const string &name, const float *vals, int len);
  virtual void add(const string &name, const double *vals, int len);
  virtual void add(const string &name, const vector<float> &vals);
  virtual void add(const string &name, const vector<float> *vals);
  virtual void add(const string &name, const vector<double> &vals);
  virtual void add(const string &name, const vector<double> *vals);

  virtual void add(const string &name, int8_t  val);
  virtual void add(const string &name, int16_t val);
  virtual void add(const string &name, int32_t val);
  virtual void add(const string &name, int64_t val);
   
  virtual void add(const string &name, uint8_t  val);
  virtual void add(const string &name, uint16_t val);
  virtual void add(const string &name, uint32_t val);
  virtual void add(const string &name, uint64_t val);
  
  virtual void add(const string &name, const int8_t  *vals, int len);
  virtual void add(const string &name, const int16_t *vals, int len);
  virtual void add(const string &name, const int32_t *vals, int len);
  virtual void add(const string &name, const int64_t *vals, int len);
   
  virtual void add(const string &name, const uint8_t  *vals, int len);
  virtual void add(const string &name, const uint16_t *vals, int len);
  virtual void add(const string &name, const uint32_t *vals, int len);
  virtual void add(const string &name, const uint64_t *vals, int len);
  
  virtual void add(const string &name, const vector<int8_t>  &vals);
  virtual void add(const string &name, const vector<int8_t>  *vals);
  virtual void add(const string &name, const vector<int16_t> &vals);
  virtual void add(const string &name, const vector<int16_t> *vals);
  virtual void add(const string &name, const vector<int32_t> &vals);
  virtual void add(const string &name, const vector<int32_t> *vals);
  virtual void add(const string &name, const vector<int64_t> &vals);
  virtual void add(const string &name, const vector<int64_t> *vals);
  
  virtual void add(const string &name, const vector<uint8_t>  &vals);
  virtual void add(const string &name, const vector<uint8_t>  *vals);
  virtual void add(const string &name, const vector<uint16_t> &vals);
  virtual void add(const string &name, const vector<uint16_t> *vals);
  virtual void add(const string &name, const vector<uint32_t> &vals);
  virtual void add(const string &name, const vector<uint32_t> *vals);
  virtual void add(const string &name, const vector<uint64_t> &vals);
  virtual void add(const string &name, const vector<uint64_t> *vals);
   

public:
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
  virtual ~cMsgCallback() {};
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

  virtual int    getMaxCueSize(void) const;
  virtual void   setMaxCueSize(int size);
  virtual int    getSkipSize(void) const;
  virtual void   setSkipSize(int size);
  virtual bool   getMaySkip(void) const;
  virtual void   setMaySkip(bool maySkip);
  virtual bool   getMustSerialize(void) const;
  virtual void   setMustSerialize(bool mustSerialize);
  virtual int    getMaxThreads(void) const;
  virtual void   setMaxThreads(int max);
  virtual int    getMessagesPerThread(void) const;
  virtual void   setMessagesPerThread(int mpt);
  virtual size_t getStackSize(void) const;
  virtual void   setStackSize(size_t size);

public:  // cMsg class must pass this to underlying C routines
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
  virtual void connect()              throw(cMsgException);
  virtual void disconnect(void)       throw(cMsgException);
  virtual void send(cMsgMessage &msg) throw(cMsgException);
  virtual void send(cMsgMessage *msg) throw(cMsgException);
  virtual int  syncSend(cMsgMessage &msg, const struct timespec *timeout = NULL) throw(cMsgException);
  virtual int  syncSend(cMsgMessage *msg, const struct timespec *timeout = NULL) throw(cMsgException);
  virtual void *subscribe(const string &subject, const string &type, cMsgCallback *cb, void *userArg, 
                          const cMsgSubscriptionConfig *cfg = NULL) throw(cMsgException);
  virtual void *subscribe(const string &subject, const string &type, cMsgCallback &cb, void *userArg,
                          const cMsgSubscriptionConfig *cfg = NULL) throw(cMsgException);
  virtual void unsubscribe(void *handle) throw(cMsgException);
  virtual void subscriptionPause(void *handle)         throw(cMsgException);
  virtual void subscriptionResume(void *handle)        throw(cMsgException);
  virtual void subscriptionQueueClear(void *handle)    throw(cMsgException);
  virtual int  subscriptionQueueCount(void *handle)    throw(cMsgException);
  virtual bool subscriptionQueueIsFull(void *handle)   throw(cMsgException);
  virtual int  subscriptionMessagesTotal(void *handle) throw(cMsgException);
  virtual cMsgMessage *sendAndGet(cMsgMessage &sendMsg, const struct timespec *timeout = NULL)
    throw(cMsgException);
  virtual cMsgMessage *sendAndGet(cMsgMessage *sendMsg, const struct timespec *timeout = NULL)
    throw(cMsgException);
  virtual cMsgMessage *subscribeAndGet(const string &subject, const string &type, const struct timespec *timeout = NULL)
    throw(cMsgException);
  virtual void   flush(const struct timespec *timeout = NULL) throw(cMsgException);
  virtual void   start(void) throw(cMsgException);
  virtual void   stop(void)  throw(cMsgException);
  virtual void   setUDL(const string &udl) throw(cMsgException);
  virtual string getUDL(void)         const;
  virtual string getCurrentUDL(void)  const throw(cMsgException);
  virtual string getName(void)        const;
  virtual string getDescription(void) const;
  virtual bool   isConnected(void)    const throw(cMsgException);
  virtual bool   isReceiving(void)    const throw(cMsgException);
  virtual void   setShutdownHandler(cMsgShutdownHandler *handler, void* userArg) throw(cMsgException);
  virtual void   shutdownClients(const string &client, int flag) throw(cMsgException);
  virtual void   shutdownServers(const string &server, int flag) throw(cMsgException);
  virtual cMsgMessage *monitor(const string &monString)          throw(cMsgException);
  virtual void setMonitoringString(const string &monString)      throw(cMsgException);


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


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
  cMsgMessage(void)                  ;
  cMsgMessage(const cMsgMessage &m)  ;
  cMsgMessage(void *msgPointer);
  virtual ~cMsgMessage(void);

  virtual string getSubject(void)               const;
  virtual void   setSubject(const string &subject)   ;
  virtual string getType(void)                  const;
  virtual void   setType(const string &type)         ;
  virtual string getText(void)                  const;
  virtual void   setText(const string &text)         ;
  
  virtual void   setByteArrayLength(int length)      ;
  virtual void   resetByteArrayLength();
  virtual int    getByteArrayLength(void);
  virtual int    getByteArrayLengthFull(void);
  virtual void   setByteArrayOffset(int offset)      ;
  virtual int    getByteArrayOffset(void);  
  virtual int    getByteArrayEndian(void);
  virtual void   setByteArrayEndian(int endian)      ;
  virtual bool   needToSwap(void)               const;  
  virtual char*  getByteArray(void);
  virtual void   setByteArray(char *array, int length)      ;
  virtual void   setByteArrayNoCopy(char* array, int length);

  virtual int    getUserInt(void)               const;
  virtual void   setUserInt(int i)                   ;
  virtual struct timespec getUserTime(void)     const;
  virtual void   setUserTime(const struct timespec &userTime);
  virtual int    getVersion(void)               const;
  virtual string getDomain(void)                const;
  virtual string getReceiver(void)              const;
  virtual string getReceiverHost(void)          const;
  virtual string getSender(void)                const;
  virtual string getSenderHost(void)            const;
  virtual struct timespec getReceiverTime(void) const;
  virtual struct timespec getSenderTime(void)   const;
  virtual bool   isGetRequest(void)             const;
  virtual bool   isGetResponse(void)            const;
  virtual bool   isNullGetResponse(void)        const;

  virtual void   makeNullResponse(const cMsgMessage &msg)  ;
  virtual void   makeNullResponse(const cMsgMessage *msg)  ;
  virtual void   makeResponse(const cMsgMessage &msg)      ;
  virtual void   makeResponse(const cMsgMessage *msg)      ;

  virtual void   setGetResponse(bool b)              ;
  virtual void   setNullGetResponse(bool b)          ;
  virtual string toString(void)                 const;
  virtual cMsgMessage *copy(void)               const;
  virtual cMsgMessage *nullResponse(void)       const;
  virtual cMsgMessage *response(void)           const;
  virtual string getSubscriptionDomain()        const;
  virtual string getSubscriptionSubject()       const;
  virtual string getSubscriptionType()          const;
  virtual string getSubscriptionUDL()           const;
  virtual int    getSubscriptionCueSize(void)   const;
  virtual bool   getReliableSend(void)          const;
  virtual void   setReliableSend(bool b)             ;
  


  //---------------
  // PAYLOAD STUFF
  //---------------


public:

  virtual bool   hasPayload() const;
  
  virtual void   payloadClear(void);
  virtual void   payloadReset(void);
  virtual void   payloadPrint(void) const;
  virtual void   payloadCopy(const cMsgMessage &msg)           ;

  virtual bool   payloadRemoveField(const string &name);
  virtual string payloadGetText() const;
  virtual void   payloadSetFromText(const string &txt)         ;
  virtual string payloadGetFieldDescription(const string &name) const;  
  
  virtual map<string,int> *payloadGet()                         const;
  virtual int    payloadGetCount()                              const;
  virtual bool   payloadContainsName (const string &name)       const;
  virtual int    payloadGetType      (const string &name)       const;
  virtual void   setHistoryLengthMax (int len)                  const;
  

  //
  // Methods to get a payload item's value
  //
  virtual void getBinary(const string &name, const char **val, int &len, int &endian)
               const;
  virtual void getBinaryArray(const string &name, const char ***vals, int **lens, int **endians, int &count)
               const;

  virtual cMsgMessage          *getMessage(const string &name)        const;
  virtual vector<cMsgMessage>  *getMessageVector(const string &name)  const;
  virtual vector<cMsgMessage*> *getMessagePVector(const string &name) const;
  virtual cMsgMessage          *getMessageArray (const string &name)  const;
  virtual cMsgMessage*         *getMessagePArray (const string &name) const;

  virtual string          getString(const string &name)       const;
  virtual vector<string> *getStringVector(const string &name) const;
  virtual string         *getStringArray(const string &name)  const;
  
  virtual float           getFloat(const string &name)        const;
  virtual vector<float>  *getFloatVector(const string &name)  const;
  virtual float          *getFloatArray(const string &name)   const;

  virtual double          getDouble(const string &name)       const;
  virtual vector<double> *getDoubleVector(const string &name) const;
  virtual double         *getDoubleArray(const string &name)  const;
  
  virtual int8_t            getInt8  (const string &name)      const;
  virtual int16_t           getInt16 (const string &name)      const;
  virtual int32_t           getInt32 (const string &name)      const;
  virtual int64_t           getInt64 (const string &name)      const;

  virtual vector<int8_t>   *getInt8Vector (const string &name) const;
  virtual vector<int16_t>  *getInt16Vector(const string &name) const;
  virtual vector<int32_t>  *getInt32Vector(const string &name) const;
  virtual vector<int64_t>  *getInt64Vector(const string &name) const;

  virtual int8_t           *getInt8Array  (const string &name) const;
  virtual int16_t          *getInt16Array (const string &name) const;
  virtual int32_t          *getInt32Array (const string &name) const;
  virtual int64_t          *getInt64Array (const string &name) const;


  virtual uint8_t           getUint8 (const string &name)       const;
  virtual uint16_t          getUint16(const string &name)       const;
  virtual uint32_t          getUint32(const string &name)       const;
  virtual uint64_t          getUint64(const string &name)       const;
  
  virtual vector<uint8_t>  *getUint8Vector (const string &name) const;
  virtual vector<uint16_t> *getUint16Vector(const string &name) const;
  virtual vector<uint32_t> *getUint32Vector(const string &name) const;
  virtual vector<uint64_t> *getUint64Vector(const string &name) const;

  virtual uint8_t          *getUint8Array  (const string &name) const;
  virtual uint16_t         *getUint16Array (const string &name) const;
  virtual uint32_t         *getUint32Array (const string &name) const;
  virtual uint64_t         *getUint64Array (const string &name) const;
  

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
  virtual void connect()             ;
  virtual void disconnect(void)      ;
  virtual void send(cMsgMessage &msg);
  virtual void send(cMsgMessage *msg);
  virtual int  syncSend(cMsgMessage &msg, const struct timespec *timeout = NULL);
  virtual int  syncSend(cMsgMessage *msg, const struct timespec *timeout = NULL);
  virtual void *subscribe(const string &subject, const string &type, cMsgCallback *cb, void *userArg, 
                          const cMsgSubscriptionConfig *cfg = NULL);
  virtual void *subscribe(const string &subject, const string &type, cMsgCallback &cb, void *userArg,
                          const cMsgSubscriptionConfig *cfg = NULL);
  virtual void unsubscribe(void *handle);
  virtual void subscriptionPause(void *handle)        ;
  virtual void subscriptionResume(void *handle)       ;
  virtual void subscriptionQueueClear(void *handle)   ;
  virtual int  subscriptionQueueCount(void *handle)   ;
  virtual bool subscriptionQueueIsFull(void *handle)  ;
  virtual int  subscriptionMessagesTotal(void *handle);
  virtual cMsgMessage *sendAndGet(cMsgMessage &sendMsg, const struct timespec *timeout = NULL)
   ;
  virtual cMsgMessage *sendAndGet(cMsgMessage *sendMsg, const struct timespec *timeout = NULL)
   ;
  virtual cMsgMessage *subscribeAndGet(const string &subject, const string &type, const struct timespec *timeout = NULL)
   ;
  virtual void   flush(const struct timespec *timeout = NULL);
  virtual void   start(void);
  virtual void   stop(void) ;
  virtual void   setUDL(const string &udl);
  virtual string getUDL(void)         const;
  virtual string getCurrentUDL(void)  const;
  virtual string getName(void)        const;
  virtual string getDescription(void) const;
  virtual bool   isConnected(void)    const;
  virtual bool   isReceiving(void)    const;
  virtual void   setShutdownHandler(cMsgShutdownHandler *handler, void* userArg);
  virtual void   shutdownClients(const string &client, int flag);
  virtual void   shutdownServers(const string &server, int flag);
  virtual cMsgMessage *monitor(const string &monString)         ;
  virtual void setMonitoringString(const string &monString)     ;


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


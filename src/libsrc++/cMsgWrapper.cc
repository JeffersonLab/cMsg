// to do

//   subscribe must lock list until done ?



/*---------------------------------------------------------------------------*
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
*                                                                            *
*----------------------------------------------------------------------------*/


#include <cMsg.hxx>
#include <cMsgPrivate.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <vector>
#include <sstream>

using namespace std;
using namespace cmsg;



//-----------------------------------------------------------------------------
//  local data and static functions
//-----------------------------------------------------------------------------


namespace cmsg {


/** Used internally, stores callback dispatching info. */
typedef struct {
  cMsgCallback *cb;      /**<Callback object.*/
  void *userArg;         /**<User arg.*/
} dispatcherStruct;


/** Used internally, holds subscription info. */
typedef struct {
  void  *domainId;      /**<Domain ID.*/
  void  *handle;        /**<Subscription handle.*/
  string subject;       /**<Subject.*/
  string type;          /**<Type.*/
  dispatcherStruct *d;  /**<Pointer to callback dispatcher struct.*/
} subscrStruct;


/** Used internally, vector of current subscriptions.  */
static vector<subscrStruct*> subscrVec;


/** Used internally, subscription mutex. */
static pthread_mutex_t subscrMutex = PTHREAD_MUTEX_INITIALIZER;


//-----------------------------------------------------------------------------


/** Used internally, C callback dispatches to object member function. */
static void callbackDispatcher(void *msg, void *userArg) {
  dispatcherStruct *ds = (dispatcherStruct*)userArg;
  ds->cb->callback(new cMsgMessage(msg),ds->userArg);
}


//-----------------------------------------------------------------------------


/** 
 * Used internally, true if subscription exists.
 *
 * @param domainId Domain ID
 * @param subject Subject
 * @param type Type
 * @param cb Callback object
 * @param userArg User arg
 *
 * @return True if subscription exists
 */
 /*
static bool subscriptionExists(void *domainId, const string &subject, const string &type, 
                               cMsgCallback *cb, void *userArg) {

  bool itExists = false;


  // search list for matching subscription
  pthread_mutex_lock(&subscrMutex);
  for(unsigned int i=0; i<subscrVec.size(); i++) {
    if( (subscrVec[i]->domainId   == domainId) &&
        (subscrVec[i]->subject    == subject) &&
        (subscrVec[i]->type       == type) &&
        (subscrVec[i]->d->cb      == cb) &&
        (subscrVec[i]->d->userArg == userArg)
        ) {
      itExists=true;
      break;
    }
  }
  pthread_mutex_unlock(&subscrMutex);


  // done searching
  return(itExists);
}
 */

//-----------------------------------------------------------------------------


/**
 * Used internally, adds subscription to list.
 *
 * @param domainId Domain ID
 * @param subject Subject
 * @param type Type
 * @param d Dispatcher struct
 * @param handle Handle to subscription in internal list
 */
static void addSubscription(void *domainId, const string &subject, const string &type,
                            dispatcherStruct *d, void *handle) {

  subscrStruct *s = new subscrStruct();

  s->domainId=domainId;
  s->subject=subject;
  s->type=type;
  s->d=d;
  s->handle=handle;

  pthread_mutex_lock(&subscrMutex);
  subscrVec.push_back(s);
  pthread_mutex_unlock(&subscrMutex);

  return;
}


//-----------------------------------------------------------------------------


/** 
 * Used internally, removes subscription from list.
 *
 * @param domainId Domain ID
 * @param handle Handle to subscription in internal list
 *
 * @return True if subscription deleted
 */
static bool deleteSubscription(void *domainId, void *handle) {

  bool deleted = false;

  pthread_mutex_lock(&subscrMutex);
  vector<subscrStruct*>::iterator iter;
  for(iter=subscrVec.begin(); iter!=subscrVec.end(); iter++) {
    if(((*iter)->domainId==domainId)&&((*iter)->handle==handle)) {
      delete((*iter)->d);
      delete(*iter);
      subscrVec.erase(iter);
      deleted=true;
      break;
    }
  }
  pthread_mutex_unlock(&subscrMutex);

  return(deleted);
}


//-----------------------------------------------------------------------------


/** 
 * Used internally, removes all subscriptions of a single connection.
 *
 * @param domainId Domain ID
 */
static void deleteSubscriptions(void *domainId) {

  pthread_mutex_lock(&subscrMutex);
  vector<subscrStruct*>::iterator iter;
  for(iter=subscrVec.begin(); iter!=subscrVec.end(); iter++) {
    if((*iter)->domainId==domainId) {
      delete((*iter)->d);
      delete(*iter);
      subscrVec.erase(iter);
    }
  }
  pthread_mutex_unlock(&subscrMutex);

  return;
}


//-----------------------------------------------------------------------------

} // namespace cmsg



//-----------------------------------------------------------------------------
//  cMsgException methods
//-----------------------------------------------------------------------------


/**
 * Empty constructor.
 */
cMsgException::cMsgException(void) : descr(""), returnCode(0) {}


//-----------------------------------------------------------------------------


/**
 * String constructor.
 *
 * @param c Description
 */
cMsgException::cMsgException(const string &c) : descr(c), returnCode(0) {}


//-----------------------------------------------------------------------------


/**
 * String and code constructor.
 *
 * @param c Description
 * @param code Return code
 */
cMsgException::cMsgException(const string &c, int code) : descr(c), returnCode(code) {}


//-----------------------------------------------------------------------------


/**
 * Copy constructor.
 *
 * @param e Another exception
 */
cMsgException::cMsgException(const cMsgException &e) : descr(e.descr),  returnCode(e.returnCode) {}


//-----------------------------------------------------------------------------


/**
 * Destructor does nothing.
 */
cMsgException::~cMsgException(void) throw() {
}


//-----------------------------------------------------------------------------


/**
 * Gets string represention of exception.
 *
 * @return String representing exception
 */
string cMsgException::toString(void) const throw() {
  stringstream ss;
  ss << "?cMsgException returnCode = " << returnCode << "    descr = " << descr << ends;
  return(ss.str());
}


//-----------------------------------------------------------------------------


/**
 * Gets char* represention of exception.
 *
 * @return char* representing exception
 */
const char *cMsgException::what(void) const throw() {
  return(toString().c_str());
}


//-----------------------------------------------------------------------------
//  cMsgMessage methods
//-----------------------------------------------------------------------------


/**
 * Default constructor creates message.
 * @throws cMsgException
 */
cMsgMessage::cMsgMessage(void) throw(cMsgException) {
    
  myMsgPointer=cMsgCreateMessage();
  if(myMsgPointer==NULL) {
    throw(cMsgException("?cMsgMessage constructor...unable to create message",CMSG_ERROR));
  }
}


//-----------------------------------------------------------------------------


/**
 * Copy constructor creates message object from another message object.
 *
 * @param msg The other message
 * @throws cMsgException
 */
cMsgMessage::cMsgMessage(const cMsgMessage &msg) throw(cMsgException) {

  myMsgPointer=cMsgCopyMessage(msg.myMsgPointer);
  if(myMsgPointer==NULL) {
    throw(cMsgException("?cMsgMessage copy constructor...unable to create message",CMSG_ERROR));
  }
}


//-----------------------------------------------------------------------------


/**
 * Constructor creates message object from C message pointer.
 *
 * @param msgPointer  C pointer to message
 * @throws cMsgException
 */
cMsgMessage::cMsgMessage(void *msgPointer) throw(cMsgException) {

  myMsgPointer=msgPointer;
  if(myMsgPointer==NULL) {
    throw(cMsgException("?cMsgMessage pointer constructor...unable to create message",CMSG_ERROR));
  }
}


//-----------------------------------------------------------------------------


/**
 * Destructor frees C message pointer struct.
 */
cMsgMessage::~cMsgMessage(void) {
  if(myMsgPointer!=NULL)cMsgFreeMessage(&myMsgPointer);
}


//-----------------------------------------------------------------------------


/**
 * Gets message subject.
 *
 * @return Message subject
 * @throws cMsgException
 */
string cMsgMessage::getSubject(void) const throw(cMsgException) {

  const char *s;

  int stat;
  if((stat=cMsgGetSubject(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  if(s==NULL) {
    return("null");
  } else {
    return(string(s));
  }
}


//-----------------------------------------------------------------------------


/**
 * Sets message subject.
 *
 * @param subject Message subject
 * @throws cMsgException
 */
void cMsgMessage::setSubject(const string &subject) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetSubject(myMsgPointer,subject.c_str()))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets message type.
 *
 * @return Message type
 * @throws cMsgException
 */
string cMsgMessage::getType(void) const throw(cMsgException) {

  const char *s;

  int stat;
  if((stat=cMsgGetType(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  if(s==NULL) {
    return("null");
  } else {
    return(string(s));
  }
}
  

//-----------------------------------------------------------------------------


/**
 * Sets message type.
 *
 * @param type Message type
 * @throws cMsgException
 */
void cMsgMessage::setType(const string &type) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetType(myMsgPointer,type.c_str()))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets message text.
 *
 * @return Message text
 * @throws cMsgException
 */
string cMsgMessage::getText(void) const throw(cMsgException) {

  const char *s;

  int stat;
  if((stat=cMsgGetText(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  if(s==NULL) {
    return("null");
  } else {
    return(string(s));
  }
}


//-----------------------------------------------------------------------------


/**
 * Sets message text.
 *
 * @param text Message text
 * @throws cMsgException
 */
void cMsgMessage::setText(const string &text) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetText(myMsgPointer,text.c_str()))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Sets message region-of-interest byte array length.
 *
 * @param length Array length in bytes
 * @throws cMsgException
 */
void cMsgMessage::setByteArrayLength(int length) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetByteArrayLength(myMsgPointer,length)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Sets message region-of-interest byte array length to the full
 * length of the array.
 */
void cMsgMessage::resetByteArrayLength() {
  cMsgResetByteArrayLength(myMsgPointer);
}


//-----------------------------------------------------------------------------


/**
 * Gets message region-of-interest byte array length.
 *
 * @return Length of array region-of-interest in bytes
 */
int cMsgMessage::getByteArrayLength(void) {

  int i;
  cMsgGetByteArrayLength(myMsgPointer,&i);
  return(i);
}


//-----------------------------------------------------------------------------


/**
 * Gets message full byte array length.
 *
 * @return Full array length in bytes
 */
int cMsgMessage::getByteArrayLengthFull(void) {
  int i;
  cMsgGetByteArrayLengthFull(myMsgPointer,&i);
  return(i);
}


//-----------------------------------------------------------------------------


/**
 * Specifies offset in byte array.
 *
 * @param offset Offset in byte array
 * @throws cMsgException
 */
void cMsgMessage::setByteArrayOffset(int offset) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetByteArrayOffset(myMsgPointer,offset)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets offset in byte array.
 *
 * @return Offset in byte array
 */
int cMsgMessage::getByteArrayOffset(void) {

  int i;
  cMsgGetByteArrayOffset(myMsgPointer,&i);
  return(i);
}


//-----------------------------------------------------------------------------


/**
 * Specifies byte array by copying it.
 *
 * @param array Byte array
 * @param length Length of byte array in bytes
 * @throws cMsgException
 */
void cMsgMessage::setByteArray(char *array, int length) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetByteArray(myMsgPointer,array, length)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Specifies the byte array by only copying the pointer to the array.
 *
 * @param array  Byte array
 * @param length Length of byte array in bytes
 * @throws cMsgException
 */
void cMsgMessage::setByteArrayNoCopy(char* array, int length) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetByteArrayNoCopy(myMsgPointer,array,length)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets byte array.
 *
 * @return Byte array
 */
char* cMsgMessage::getByteArray(void) {

  char *p;
  cMsgGetByteArray(myMsgPointer,&p);
  return(p);
}


//-----------------------------------------------------------------------------


/**
 * Gets endian-ness of message byte array.
 * Valid return values are:
 *<ul>
 *<li>CMSG_ENDIAN_BIG
 *<li>CMSG_ENDIAN_LITTLE
 *</ul>
 *
 * @return Endian-ness value
 */
int cMsgMessage::getByteArrayEndian(void) {

  int endian;
  cMsgGetByteArrayEndian(myMsgPointer,&endian);
  return(endian);
}


//-----------------------------------------------------------------------------


/**
 * Sets endian-ness of message byte array.
 * Valid values are:
 *<ul>
 *<li>ENDIAN_BIG
 *<li>ENDIAN_LITTLE
 *<li>ENDIAN_LOCAL
 *<li>ENDIAN_NOTLOCAL
 *<li>ENDIAN_SWITCH
 *</ul>
 *
 * @param endian Endian-ness value
 * @throws cMsgException
 */
void cMsgMessage::setByteArrayEndian(int endian) throw(cMsgException) {
  int stat;

  if((stat=cMsgSetByteArrayEndian(myMsgPointer,endian))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  return;
}


//-----------------------------------------------------------------------------


/**
 * True if need to swap byte array.
 *
 * @return True if must swap
 * @throws cMsgException
 */
bool cMsgMessage::needToSwap(void) const throw(cMsgException) {

  int flag,stat;

  if((stat=cMsgNeedToSwap(myMsgPointer,&flag))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  return(flag==1);
}


//-----------------------------------------------------------------------------


/**
 * Gets message user int.
 *
 * @return User int
 * @throws cMsgException
 */
int cMsgMessage::getUserInt(void) const throw(cMsgException) {

  int i;

  int stat;
  if((stat=cMsgGetUserInt(myMsgPointer,&i))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(i);
}


//-----------------------------------------------------------------------------


/**
 * Sets message user int.
 *
 * @param i User int
 * @throws cMsgException
 */
void cMsgMessage::setUserInt(int i) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetUserInt(myMsgPointer,i))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets message user time.
 *
 * @return Timespec holding user time
 * @throws cMsgException
 */
struct timespec cMsgMessage::getUserTime(void) const throw(cMsgException) {

  struct timespec t;

  int stat;
  if((stat=cMsgGetUserTime(myMsgPointer,&t))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(t);
}


//-----------------------------------------------------------------------------


/**
 * Sets message user time.
 *
 * @param userTime Timespec holding user time
 */
void cMsgMessage::setUserTime(const struct timespec &userTime) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetUserTime(myMsgPointer, &userTime))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets cMsg version.
 *
 * @return Version
 */
int cMsgMessage::getVersion(void) const throw(cMsgException) {

  int version;

  int stat;
  if((stat=cMsgGetVersion(myMsgPointer, &version))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(version);
}


//-----------------------------------------------------------------------------


/**
 * Copies a message.
 *
 * @return Copy of message
 */
cMsgMessage *cMsgMessage::copy(void) const throw(cMsgException) {

  void *newPointer = cMsgCopyMessage(myMsgPointer);
  return(new cMsgMessage(newPointer));
}


//-----------------------------------------------------------------------------


/**
 * Gets message domain.
 *
 * @return Domain
 * @throws cMsgException
 */
string cMsgMessage::getDomain(void) const throw(cMsgException) {

  const char *s;

  int stat;
  if((stat=cMsgGetDomain(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    return(string(s));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets message receiver.
 *
 * @return Receiver
 * @throws cMsgException
 */
string cMsgMessage::getReceiver(void) const throw(cMsgException) {

  const char *s;

  int stat;
  if((stat=cMsgGetReceiver(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    return(string(s));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets message receiver host.
 *
 * @return Receiver host
 * @throws cMsgException
 */
string cMsgMessage::getReceiverHost(void) const throw(cMsgException) {

  const char *s;

  int stat;
  if((stat=cMsgGetReceiverHost(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    return(string(s));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets message sender.
 *
 * @return Sender
 * @throws cMsgException
 */
string cMsgMessage::getSender(void) const throw(cMsgException) {

  const char *s;

  int stat;
  if((stat=cMsgGetSender(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    return(string(s));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets message sender host.
 *
 * @return Sender host
 * @throws cMsgException
 */
string cMsgMessage::getSenderHost(void) const throw(cMsgException) {

  const char *s;

  int stat;
  if((stat=cMsgGetSenderHost(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    return(string(s));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets message receiver time.
 *
 * @return Receiver time
 * @throws cMsgException
 */
struct timespec cMsgMessage::getReceiverTime(void) const throw(cMsgException) {

  struct timespec t;

  int stat;
  if((stat=cMsgGetReceiverTime(myMsgPointer,&t))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(t);
}


//-----------------------------------------------------------------------------


/**
 * Gets message sender time.
 *
 * @return Sender time
 * @throws cMsgException
 */
struct timespec cMsgMessage::getSenderTime(void) const throw(cMsgException) {

  struct timespec t;

  int stat;
  if((stat=cMsgGetSenderTime(myMsgPointer,&t))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(t);
}


//-----------------------------------------------------------------------------


/**
 * True if message is a get request.
 *
 * @return True if get request
 */
bool cMsgMessage::isGetRequest(void) const throw(cMsgException) {
  
  int b;

  int stat;
  if((stat=cMsgGetGetRequest(myMsgPointer,&b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(b);
}


//-----------------------------------------------------------------------------


/**
 * True if message is a get response.
 *
 * @return True if get response
 */
bool cMsgMessage::isGetResponse(void) const throw(cMsgException) {
  
  int b;

  int stat;
  if((stat=cMsgGetGetResponse(myMsgPointer,&b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(b);
}


//-----------------------------------------------------------------------------


/**
 * True if message is a NULL get response.
 *
 * @return True if NULL get response
 * @throws cMsgException
 */
bool cMsgMessage::isNullGetResponse(void) const throw(cMsgException) {
  
  int b;

  int stat;
  if((stat=cMsgGetNullGetResponse(myMsgPointer,&b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(b);
}


//-----------------------------------------------------------------------------


/**
 * Makes a message a null response message.
 *
 * @param msg Message to make a null response
 * @throws cMsgException
 */
void cMsgMessage::makeNullResponse(const cMsgMessage &msg) throw(cMsgException) {

  cMsgMessage_t *t = (cMsgMessage_t*)myMsgPointer;
  cMsgMessage_t *m = (cMsgMessage_t*)msg.myMsgPointer;

  t->sysMsgId    = m->sysMsgId;
  t->senderToken = m->senderToken;
  t->info        = CMSG_IS_GET_RESPONSE | CMSG_IS_NULL_GET_RESPONSE;
}


//-----------------------------------------------------------------------------


/**
 * Makes a message a null response message.
 *
 * @param msg Message to make a null response
 * @throws cMsgException
 */
void cMsgMessage::makeNullResponse(const cMsgMessage *msg) throw(cMsgException) {

  cMsgMessage_t *t = (cMsgMessage_t*)myMsgPointer;
  cMsgMessage_t *m = (cMsgMessage_t*)msg->myMsgPointer;

  t->sysMsgId    = m->sysMsgId;
  t->senderToken = m->senderToken;
  t->info        = CMSG_IS_GET_RESPONSE | CMSG_IS_NULL_GET_RESPONSE;
}


//-----------------------------------------------------------------------------


/**
 * Makes a message a response message.
 *
 * @param msg Message to make a response
 * @throws cMsgException
 */
void cMsgMessage::makeResponse(const cMsgMessage &msg) throw(cMsgException) {

  cMsgMessage_t *t = (cMsgMessage_t*)myMsgPointer;
  cMsgMessage_t *m = (cMsgMessage_t*)msg.myMsgPointer;

  t->sysMsgId    = m->sysMsgId;
  t->senderToken = m->senderToken;
  t->info        = CMSG_IS_GET_RESPONSE;
}


//-----------------------------------------------------------------------------


/**
 * Makes a message a response message.
 *
 * @param msg Message to make a response
 * @throws cMsgException
 */
void cMsgMessage::makeResponse(const cMsgMessage *msg) throw(cMsgException) {

  cMsgMessage_t *t = (cMsgMessage_t*)myMsgPointer;
  cMsgMessage_t *m = (cMsgMessage_t*)msg->myMsgPointer;

  t->sysMsgId    = m->sysMsgId;
  t->senderToken = m->senderToken;
  t->info        = CMSG_IS_GET_RESPONSE;
}


//-----------------------------------------------------------------------------


/**
 * Creates a null response message.
 *
 * @return Null response message
 * @throws cMsgException
 */
cMsgMessage *cMsgMessage::nullResponse(void) const throw(cMsgException) {

  void *newMsgPointer;
  if((newMsgPointer=cMsgCreateNullResponseMessage(myMsgPointer))==NULL) {
    throw(cMsgException("?cMsgMessage::nullResponse...unable to create message",CMSG_ERROR));
  }

  return(new cMsgMessage(newMsgPointer));
}


//-----------------------------------------------------------------------------


/**
 * Creates a response message.
 *
 * @return Response message
 * @throws cMsgException
 */
cMsgMessage *cMsgMessage::response(void) const throw(cMsgException) {

  void *newMsgPointer;
  if((newMsgPointer=cMsgCreateResponseMessage(myMsgPointer))==NULL) {
    throw(cMsgException("?cMsgMessage::response...unable to create message",CMSG_ERROR));
  }

  return(new cMsgMessage(newMsgPointer));
}


//-----------------------------------------------------------------------------


/**
 * Makes message a get response message.
 *
 * @param b True to make message a get response message
 * @throws cMsgException
 */
void cMsgMessage::setGetResponse(bool b) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetGetResponse(myMsgPointer,b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Makes message a null response message.
 *
 * @param b True to make message a null response message
 * @throws cMsgException
 */
void cMsgMessage::setNullGetResponse(bool b) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetNullGetResponse(myMsgPointer,b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets xml representation of message.
 *
 * @return xml representation of message
 * @throws cMsgException
 */
string cMsgMessage::toString(void) const throw(cMsgException) {

  char *cs;

  int stat;
  if((stat=cMsgToString(myMsgPointer,&cs))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  string s(cs);
  free(cs);
  return(s);
           
}


//-----------------------------------------------------------------------------
//   message context accessor functions
//-----------------------------------------------------------------------------


/**
 * Gets subscription domain.
 *
 * @return Subscription domain
 * @throws cMsgException
 */
string cMsgMessage::getSubscriptionDomain(void) const throw(cMsgException) {

  const char *s;

  int stat;
  if((stat=cMsgGetSubscriptionDomain(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    return(string(s));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets subscription subject.
 *
 * @return Subscription subject
 * @throws cMsgException
 */
string cMsgMessage::getSubscriptionSubject(void) const throw(cMsgException) {

  const char *s;

  int stat;
  if((stat=cMsgGetSubscriptionSubject(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    return(string(s));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets subscription type.
 *
 * @return Subscription type
 * @throws cMsgException
 */
string cMsgMessage::getSubscriptionType(void) const throw(cMsgException) {

  const char *s;

  int stat;
  if((stat=cMsgGetSubscriptionType(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    return(string(s));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets subscription UDL.
 *
 * @return Subscription UDL
 * @throws cMsgException
 */
string cMsgMessage::getSubscriptionUDL(void) const throw(cMsgException) {

  const char *s;

  int stat;
  if((stat=cMsgGetSubscriptionUDL(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    return(string(s));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets current subscription cue size.
 *
 * @return Current cue size
 * @throws cMsgException
 */
int cMsgMessage::getSubscriptionCueSize(void) const throw(cMsgException) {

  int i;

  int stat;
  if((stat=cMsgGetSubscriptionCueSize(myMsgPointer,&i))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(i);
}


//-----------------------------------------------------------------------------


/**
 * True if message sent via reliable send.
 *
 * @return True if reliable send used
 * @throws cMsgException
 */
bool cMsgMessage::getReliableSend(void) const throw(cMsgException) {

  int i;

  int stat;
  if((stat=cMsgGetReliableSend(myMsgPointer,&i))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(i == 0 ? false : true);
}


//-----------------------------------------------------------------------------


/**
 * Sets message reliable send flag.
 *
 * @param b True if reliable send should be used
 * @throws cMsgException
 */
void cMsgMessage::setReliableSend(bool b) throw(cMsgException) {

  int i = b ? 1 : 0;
  
  int stat;
  if((stat=cMsgSetReliableSend(myMsgPointer,i))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------
// cMsgSubscriptionConfig methods
//-----------------------------------------------------------------------------


/**
 * Constructor creates empty subscription config.
 */
cMsgSubscriptionConfig::cMsgSubscriptionConfig(void) {
  config = cMsgSubscribeConfigCreate();
}


//-----------------------------------------------------------------------------


/**
 * Deletes subscription config.
 */
cMsgSubscriptionConfig::~cMsgSubscriptionConfig(void) {
  cMsgSubscribeConfigDestroy(config);
}


//-----------------------------------------------------------------------------


/**
 * Gets max cue size.
 *
 * @return Max cue size
 */
int cMsgSubscriptionConfig::getMaxCueSize(void) const {
  int size;
  cMsgSubscribeGetMaxCueSize(config,&size);
  return(size);
}


//-----------------------------------------------------------------------------


/**
 * Sets max cue size.
 *
 * @param size Max cue size
 */
void cMsgSubscriptionConfig::setMaxCueSize(int size) {
  cMsgSubscribeSetMaxCueSize(config,size);
}


//-----------------------------------------------------------------------------


/**
 * Gets skip size.
 *
 * @return Skip size
 */
int cMsgSubscriptionConfig::getSkipSize(void) const {
  int size;
  cMsgSubscribeGetSkipSize(config,&size);
  return(size);
}


//-----------------------------------------------------------------------------


/**
 * Sets skip size.
 *
 * @param size Skip size
 */
void cMsgSubscriptionConfig::setSkipSize(int size) {
  cMsgSubscribeSetSkipSize(config,size);
}


//-----------------------------------------------------------------------------


/**
 * True if may skip messages upon overflow.
 *
 * @return True if can skip
 */
bool cMsgSubscriptionConfig::getMaySkip(void) const {
  int maySkip;
  cMsgSubscribeGetMaySkip(config,&maySkip);
  return((maySkip==0)?false:true);
}


//-----------------------------------------------------------------------------


/**
 * Sets message skip permission.
 *
 * @param maySkip True if can skip messages
 */
void cMsgSubscriptionConfig::setMaySkip(bool maySkip) {
  cMsgSubscribeSetMaySkip(config,(maySkip)?1:0);
}


//-----------------------------------------------------------------------------


/**
 * Gets must serialize flag.
 *
 * @return True if must serialize
 */
bool cMsgSubscriptionConfig::getMustSerialize(void) const {
  int maySerialize;
  cMsgSubscribeGetMustSerialize(config,&maySerialize);
  return((maySerialize==0)?false:true);
}


//-----------------------------------------------------------------------------


/**
 * Sets must serialize flag.
 *
 * @param mustSerialize True if must serialize
 */
void cMsgSubscriptionConfig::setMustSerialize(bool mustSerialize) {
  cMsgSubscribeSetMustSerialize(config,(mustSerialize)?1:0);
}


//-----------------------------------------------------------------------------


/**
 * Gets max callback threads.
 *
 * @return Maximum number of threads
 */
int cMsgSubscriptionConfig::getMaxThreads(void) const {
  int max;
  cMsgSubscribeGetMaxThreads(config,&max);
  return(max);
}


//-----------------------------------------------------------------------------


/**
 * Sets max callback threads.
 *
 * @param max Max callback threads
 */
void cMsgSubscriptionConfig::setMaxThreads(int max) {
  cMsgSubscribeSetMaxThreads(config,max);
}


//-----------------------------------------------------------------------------


/**
 * Gets max messages per thread.
 *
 * @return Max messages per callback thread
 */
int cMsgSubscriptionConfig::getMessagesPerThread(void) const {
  int mpt;
  cMsgSubscribeGetMessagesPerThread(config,&mpt);
  return(mpt);
}


//-----------------------------------------------------------------------------


/**
 * Sets max messages per thread.
 *
 * @param mpt Max messages per callback thread
 */
void cMsgSubscriptionConfig::setMessagesPerThread(int mpt) {
  cMsgSubscribeSetMessagesPerThread(config,mpt);
}


//-----------------------------------------------------------------------------


/**
 * Gets message stack size.
 *
 * @return Message stack size
 */
size_t cMsgSubscriptionConfig::getStackSize(void) const {
  size_t size;
  cMsgSubscribeGetStackSize(config,&size);
  return(size);
}


//-----------------------------------------------------------------------------


/**
 * Sets message stack size.
 *
 * @param size Message stack size
 */
void cMsgSubscriptionConfig::setStackSize(size_t size) {
  cMsgSubscribeSetStackSize(config,size);
}


//-----------------------------------------------------------------------------
//  cMsg methods
//-----------------------------------------------------------------------------


/**
 * Constructor for cMsg system object.
 *
 * @param UDL Connection UDL
 * @param name Name
 * @param descr Description
 */
cMsg::cMsg(const string &UDL, const string &name, const string &descr)
  : myUDL(UDL), myName(name), myDescr(descr), initialized(false) {
}


//-----------------------------------------------------------------------------


/**
 * Destructor disconects from cMsg system.
 * @throws cMsgException
 */
cMsg::~cMsg(void) {
  cMsg::disconnect();
}


//-----------------------------------------------------------------------------


/**
 * Connects to cMsg system. If a connection has already been made, a reconnection will be
 * attempted. In a reconnection, if there is no current connection one will be made.
 * If there is a current, healthy connection and if the UDL to which a connection attempt
 * will be made is identical to the currentUDL, nothing is done. If there is a current,
 * healthy connection, and if the UDL to which a connection attempt will be made is
 * different from the current UDL, the current connection will be disconnected and a
 * new connection attempted with the new UDL.
 * 
 * @throws cMsgException
 */
void cMsg::connect(void) throw(cMsgException) {
    int stat;
   
    // if we're already initialized (called connect once), we want to reconnect
    if(initialized) {
        if((stat=cMsgReconnect(myDomainId))!=CMSG_OK) {
            throw(cMsgException(cMsgPerror(stat),stat));
        }
        return;
    }


    if((stat=cMsgConnect(myUDL.c_str(),myName.c_str(),myDescr.c_str(),&myDomainId))!=CMSG_OK) {
        throw(cMsgException(cMsgPerror(stat),stat));
    }
    initialized=true;
}


//-----------------------------------------------------------------------------


/**
 * Disconnects from cMsg system.
 * @throws cMsgException
 */
void cMsg::disconnect(void) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));

  cMsgDisconnect(&myDomainId);

  deleteSubscriptions(myDomainId);
}


//-----------------------------------------------------------------------------


/**
 * Sends message.
 *
 * @param msg Message to send
 * @throws cMsgException
 */
void cMsg::send(cMsgMessage &msg) throw(cMsgException) {
    
  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  if((stat=cMsgSend(myDomainId,msg.myMsgPointer))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Sends message.
 *
 * @param msg Message to send
 * @throws cMsgException
 */
void cMsg::send(cMsgMessage *msg) throw(cMsgException) {
  cMsg::send(*msg);
}


//-----------------------------------------------------------------------------


/**
 * Synchronously sends message.
 *
 * @param msg Message to send
 * @param timeout Timeout
 * @throws cMsgException
 */
int cMsg::syncSend(cMsgMessage &msg, const struct timespec *timeout) throw(cMsgException) {
    
  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int response;

  int stat;
  if((stat=cMsgSyncSend(myDomainId,msg.myMsgPointer,timeout,&response))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(response);
}


//-----------------------------------------------------------------------------


/**
 * Synchronously sends message.
 *
 * @param msg Message to send
 * @param timeout Timeout
 * @throws cMsgException
 */
int cMsg::syncSend(cMsgMessage *msg, const struct timespec *timeout) throw(cMsgException) {
  return(cMsg::syncSend(*msg, timeout));
}


//-----------------------------------------------------------------------------


/**
 * Subscribes to subject,type and specifies callback, userArg
 *
 * @param subject Subject, can be regex
 * @param type Type, can be regex
 * @param cb Callback object to deliver messages to
 * @param userArg Passed to callback with message
 * @param cfg Subscription config object
 *
 * @return Subscription handle, needed to unsubscribe
 * @throws cMsgException if subscription fails, object was not initialized, or for
 *                       some underlying domain's reason
 */
void *cMsg::subscribe(const string &subject, const string &type, cMsgCallback *cb, void *userArg,
                      const cMsgSubscriptionConfig *cfg) throw(cMsgException) {
    
  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  void *handle;


  // create and fill dispatcher struct
  dispatcherStruct *d = new dispatcherStruct();
  d->cb=cb;
  d->userArg=userArg;


  // subscribe and get handle
  stat=cMsgSubscribe(myDomainId,
                     (subject.size()<=0)?NULL:subject.c_str(),
                     (type.size()<=0)?NULL:type.c_str(),
                     callbackDispatcher,
                     (void*)d,
                     (cfg==NULL)?NULL:(cfg->config),
                     &handle);


  // check if subscription accepted
  if(stat!=CMSG_OK) {
    delete(d);
    throw(cMsgException(cMsgPerror(stat),stat));
  }


  // add this subscription to internal list
  addSubscription(myDomainId,subject,type,d,handle);


  return(handle);
}


//-----------------------------------------------------------------------------


/**
 * Subscribes to subject,type and specifies callback,userArg
 *
 * @param subject Subject, can be regex
 * @param type Type, can be regex
 * @param cb Callback object to deliver messages to
 * @param userArg Passed to callback with message
 * @param cfg Subscription config object
 *
 * @return Subscription handle, needed to unsubscribe
 * @throws cMsgException if subscription fails, object was not initialized, or for
 *                       some underlying domain's reason
 */
void *cMsg::subscribe(const string &subject, const string &type, cMsgCallback &cb, void *userArg,
                      const cMsgSubscriptionConfig *cfg) throw(cMsgException) {
  return(cMsg::subscribe(subject, type, &cb, userArg, cfg));
}


//-----------------------------------------------------------------------------


/**
 * Unsubscribes.
 *
 * @param handle Subscription handle
 * @throws cMsgException
 */
void cMsg::unsubscribe(void *handle) throw(cMsgException) {

    if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


    int stat;


  // remove subscription from internal list
    if(!deleteSubscription(myDomainId,handle)) {
        throw(cMsgException(cMsgPerror(CMSG_BAD_ARGUMENT),CMSG_BAD_ARGUMENT));
    }


  // unsubscribe
    if((stat=cMsgUnSubscribe(myDomainId,handle))!=CMSG_OK) {
        throw(cMsgException(cMsgPerror(stat),stat));
    }

}


//-----------------------------------------------------------------------------


/**
 * Pause delivery of messages to the given subscription's callback.
 *
 * @param handle Subscription handle
 * @throws cMsgException
 */
void cMsg::subscriptionPause(void *handle) throw(cMsgException) {

    if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));

    int stat;

    // pause
    if((stat=cMsgSubscriptionPause(myDomainId,handle))!=CMSG_OK) {
        throw(cMsgException(cMsgPerror(stat),stat));
    }

}


//-----------------------------------------------------------------------------


/**
 * Resume delivery of messages to the given subscription's callback if paused.
 *
 * @param handle Subscription handle
 * @throws cMsgException
 */
void cMsg::subscriptionResume(void *handle) throw(cMsgException) {

    if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));

    int stat;

    // resume
    if((stat=cMsgSubscriptionResume(myDomainId,handle))!=CMSG_OK) {
        throw(cMsgException(cMsgPerror(stat),stat));
    }

}


//-----------------------------------------------------------------------------


/**
 * Clear all messages from the given subscription callback's queue.
 *
 * @param handle Subscription handle
 * @throws cMsgException
 */
void cMsg::subscriptionQueueClear(void *handle) throw(cMsgException) {

    if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));

    int stat;

    // clear Q
    if((stat=cMsgSubscriptionQueueClear(myDomainId,handle))!=CMSG_OK) {
        throw(cMsgException(cMsgPerror(stat),stat));
    }

}


//-----------------------------------------------------------------------------


/**
 * Return the number of messages currently in the given subscription callback's queue.
 *
 * @param handle Subscription handle
 * @return number of messages currently in the given subscription callback's queue
 * @throws cMsgException
 */
int cMsg::subscriptionQueueCount(void *handle) throw(cMsgException) {

    if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));

    int stat, count;

    // get Q count
    if((stat=cMsgSubscriptionQueueCount(myDomainId,handle,&count))!=CMSG_OK) {
        throw(cMsgException(cMsgPerror(stat),stat));
    }
    return count;

}


//-----------------------------------------------------------------------------


/**
 * Return the total number of messages passed to the given subscription's callback.
 *
 * @param handle Subscription handle
 * @return total number of messages passed to the given subscription's callback
 * @throws cMsgException
 */
int cMsg::subscriptionMessagesTotal(void *handle) throw(cMsgException) {

    if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));

    int stat, count;

    // total # of messages callback received
    if((stat=cMsgSubscriptionMessagesTotal(myDomainId,handle,&count))!=CMSG_OK) {
        throw(cMsgException(cMsgPerror(stat),stat));
    }
    return count;

}


//-----------------------------------------------------------------------------


/**
 * Returns whether the given subscription callback's queue is full (true) or not.
 *
 * @param handle Subscription handle
 * @return boolean stating whether the given subscription callback's queue is full or not
 * @throws cMsgException
 */
bool cMsg::subscriptionQueueIsFull(void *handle) throw(cMsgException) {

    if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));

    int stat, val;

    // Q is full?
    if((stat=cMsgSubscriptionQueueIsFull(myDomainId,handle,&val))!=CMSG_OK) {
        throw(cMsgException(cMsgPerror(stat),stat));
    }
    return (val == 0 ? false : true);

}


//-----------------------------------------------------------------------------


/**
 * Sends message and gets reply.
 *
 * @param sendMsg Message to send
 * @param timeout Timeout
 *
 * @return Reply message
 * @throws cMsgException
 */
cMsgMessage *cMsg::sendAndGet(cMsgMessage &sendMsg, const struct timespec *timeout) throw(cMsgException) {
    
  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  void *replyPtr;

  int stat;
  if((stat=cMsgSendAndGet(myDomainId,sendMsg.myMsgPointer,timeout,&replyPtr))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  return(new cMsgMessage(replyPtr));
}


//-----------------------------------------------------------------------------


/**
 * Sends message and gets reply.
 *
 * @param sendMsg Message to send
 * @param timeout Timeout
 *
 * @return Reply message
 * @throws cMsgException
 */
cMsgMessage *cMsg::sendAndGet(cMsgMessage *sendMsg, const struct timespec *timeout) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  void *replyPtr;

  int stat;
  if((stat=cMsgSendAndGet(myDomainId,sendMsg->myMsgPointer,timeout,&replyPtr))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  return(new cMsgMessage(replyPtr));
}

//-----------------------------------------------------------------------------


/**
 * Subscribes to subject/type, returns one matching message, then unsubscribes.
 *
 * @param subject Subject, can be regex
 * @param type Type, can be regex
 * @param timeout Timeout
 *
 * @return Matching message
 * @throws cMsgException
 */
cMsgMessage *cMsg::subscribeAndGet(const string &subject, const string &type, const struct timespec *timeout) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  void *replyPtr;

  int stat;
  if((stat=cMsgSubscribeAndGet(myDomainId,subject.c_str(),type.c_str(),timeout,&replyPtr))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  return(new cMsgMessage(replyPtr));
}


//-----------------------------------------------------------------------------


/**
 * Flushes outgoing message queues.
 *
 * @param timeout Timeout
 * @throws cMsgException
 */
void cMsg::flush(const struct timespec *timeout) throw(cMsgException) {
    
  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  if((stat=cMsgFlush(myDomainId, timeout))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Enables delivery of messages to callbacks.
 * @throws cMsgException
 */
void cMsg::start(void) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  if((stat=cMsgReceiveStart(myDomainId))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Disables delivery of messages to callbacks.
 * @throws cMsgException
 */
void cMsg::stop(void) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  if((stat=cMsgReceiveStop(myDomainId))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Gets connection description.
 *
 * @return Description
 */
string cMsg::getDescription(void) const {
  return(myDescr);
}


//-----------------------------------------------------------------------------


/**
 * Gets connection name.
 *
 * @return Name
 */
string cMsg::getName(void) const {
  return(myName);
}


//-----------------------------------------------------------------------------


/**
 * Gets connection UDL.
 *
 * @return UDL
 */
string cMsg::getUDL(void) const {
    return(myUDL);
}


//-----------------------------------------------------------------------------


/**
 * Sets the connection UDL.
 * Any subsequent failovers or reconnects will use the new UDL.
 *
 * @param udl new UDL
 * @throws cMsgException
 */
void cMsg::setUDL(const string &udl) throw(cMsgException) {

    int stat;
    if((stat=cMsgSetUDL(myDomainId,udl.c_str()))!=CMSG_OK) {
        throw(cMsgException(cMsgPerror(stat),stat));
    }
}


//-----------------------------------------------------------------------------


/**
 * Gets UDL of the current connection, "null" if no connection.
 *
 * @return UDL of current connection
 * @throws cMsgException
 */
string cMsg::getCurrentUDL(void) const throw(cMsgException) {
    const char *s;

    int stat;
    if((stat=cMsgGetCurrentUDL(myDomainId,&s))!=CMSG_OK) {
        throw(cMsgException(cMsgPerror(stat),stat));
    };

    if(s==NULL) {
        return("null");
    } else {
        return(string(s));
    }
}


//-----------------------------------------------------------------------------


/**
 * True if connected.
 *
 * @return True if connected
 * @throws cMsgException
 */
bool cMsg::isConnected(void) const throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat,connected;
  if((stat=cMsgGetConnectState(myDomainId,&connected))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(connected==1);
}


//-----------------------------------------------------------------------------


/**
 * True if receiving messages.
 *
 * @return True if receiving messages
 * @throws cMsgException
 */
bool cMsg::isReceiving(void) const throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat,receiving;
  if((stat=cMsgGetReceiveState(myDomainId,&receiving))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(receiving==1);
}


//-----------------------------------------------------------------------------


/**
 * Sets shutdown handler.
 *
 * @param handler Shutdown handler
 * @param userArg Arg passed to handler upon shutdown
 * @throws cMsgException
 */
void cMsg::setShutdownHandler(cMsgShutdownHandler *handler, void* userArg) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  if((stat=cMsgSetShutdownHandler(myDomainId,handler,userArg))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Shuts down a client.
 *
 * @param client The client
 * @param flag Shutdown flag
 * @throws cMsgException
 */
void cMsg::shutdownClients(const string &client, int flag) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  if((stat=cMsgShutdownClients(myDomainId,client.c_str(),flag))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Shuts down a server.
 *
 * @param server The server
 * @param flag Shutdown flag
 * @throws cMsgException
 */
void cMsg::shutdownServers(const string &server, int flag) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  if((stat=cMsgShutdownServers(myDomainId,server.c_str(),flag))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


/**
 * Returns domain-dependent monitoring information.
 *
 * @param monString Monitoring request string
 * @return Message containing monitoring information in text field
 * @throws cMsgException
 */
cMsgMessage *cMsg::monitor(const string &monString) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));

  void *m = cMsgCreateMessage();
  if(m==NULL)throw(cMsgException("?cMsgMessage constructor...unable to create message",CMSG_ERROR));

  int stat;
  if((stat=cMsgMonitor(myDomainId,monString.c_str(),&m))!=CMSG_OK) {
    cMsgFreeMessage(&m);
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  return(new cMsgMessage(m));
}


//-----------------------------------------------------------------------------


/**
 * Sets monitoring string.
 *
 * @param monString Monitoring string
 * @throws cMsgException
 */
 void cMsg::setMonitoringString(const string &monString) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));

  int stat;
  if((stat=cMsgMonitor(myDomainId,monString.c_str(),NULL))!=CMSG_OK)
    throw(cMsgException(cMsgPerror(stat),stat));

  return;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

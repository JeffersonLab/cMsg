// to do
//   word doc, doxygen doc



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


#include <cMsg.hxx>
#include <cMsgPrivate.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <vector>



//-----------------------------------------------------------------------------
//  local data and static functions
//-----------------------------------------------------------------------------


// stores callback dispatching info
typedef struct {
  cMsgCallbackAdapter *cba;
  void *userArg;
} dispatcherStruct;


// holds local subscription info
typedef struct {
  int domainId;
  string subject;
  string type;
  dispatcherStruct *d;
  void *handle;
} subscrStruct;


// vector of current subscriptions and mutex
static vector<subscrStruct*> subscrVec;
static pthread_mutex_t subscrMutex = PTHREAD_MUTEX_INITIALIZER;



//-----------------------------------------------------------------------------


static void callbackDispatcher(void *msg, void *userArg) {
  dispatcherStruct *ds = (dispatcherStruct*)userArg;
  ds->cba->callback(new cMsgMessage(msg),ds->userArg);
}


//-----------------------------------------------------------------------------


static bool subscriptionExists(int domainId, const string &subject, const string &type, 
                               cMsgCallbackAdapter *cba, void *userArg) {

  bool itExists = false;


  // search list for matching subscription
  pthread_mutex_lock(&subscrMutex);
  for(unsigned int i=0; i<subscrVec.size(); i++) {
    if( (subscrVec[i]->domainId   == domainId) &&
        (subscrVec[i]->subject    == subject) &&
        (subscrVec[i]->type       == type) &&
        (subscrVec[i]->d->cba     == cba) &&
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


//-----------------------------------------------------------------------------


static void addSubscription(int domainId, const string &subject, const string &type,
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


static bool deleteSubscription(int domainId, void *handle) {

  bool deleted = false;
  vector<subscrStruct*>::iterator iter;

  pthread_mutex_lock(&subscrMutex);
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






//-----------------------------------------------------------------------------
//  cMsgException methods
//-----------------------------------------------------------------------------


cMsgException::cMsgException(void) {
  myReturnCode=0;
}


//-----------------------------------------------------------------------------


cMsgException::cMsgException(const string &c) {
  myDescr=c;
  myReturnCode=0;
}


//-----------------------------------------------------------------------------


cMsgException::cMsgException(const string &c, int i) {
  myDescr=c;
  myReturnCode=i;
}


//-----------------------------------------------------------------------------


cMsgException::cMsgException(const cMsgException &e) {
  myDescr=e.myDescr;
  myReturnCode=e.myReturnCode;
}


//-----------------------------------------------------------------------------


cMsgException::~cMsgException(void) {
}


//-----------------------------------------------------------------------------


void cMsgException::setReturnCode(int i) {
  myReturnCode=i;
}


//-----------------------------------------------------------------------------


int cMsgException::getReturnCode(void) const {
  return(myReturnCode);
}


//-----------------------------------------------------------------------------



string cMsgException::toString(void) const {
  return(myDescr);
}


//-----------------------------------------------------------------------------
//  cMsgMessage methods
//-----------------------------------------------------------------------------


cMsgMessage::cMsgMessage(void) throw(cMsgException) {
    
  myMsgPointer=cMsgCreateMessage();
  if(myMsgPointer==NULL) {
    throw(cMsgException("?cMsgMessage constructor...unable to create message",CMSG_ERROR));
  }
}


//-----------------------------------------------------------------------------


cMsgMessage::cMsgMessage(const cMsgMessage &msg) throw(cMsgException) {

  myMsgPointer=cMsgCopyMessage(msg.myMsgPointer);
  if(myMsgPointer==NULL) {
    throw(cMsgException("?cMsgMessage copy constructor...unable to create message",CMSG_ERROR));
  }
}


//-----------------------------------------------------------------------------


cMsgMessage::cMsgMessage(void *msgPointer) throw(cMsgException) {

  myMsgPointer=msgPointer;
  if(myMsgPointer==NULL) {
    throw(cMsgException("?cMsgMessage pointer constructor...unable to create message",CMSG_ERROR));
  }
}


//-----------------------------------------------------------------------------


cMsgMessage::~cMsgMessage(void) {

  if(myMsgPointer==NULL)return;
  cMsgFreeMessage(myMsgPointer);
}


//-----------------------------------------------------------------------------


string cMsgMessage::getSubject(void) const throw(cMsgException) {

  char *s;

  int stat;
  if((stat=cMsgGetSubject(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  if(s==NULL) {
    return("null");
  } else {
    string ss = string(s);
    free(s);
    return(ss);
  }
}


//-----------------------------------------------------------------------------


void cMsgMessage::setSubject(const string &subject) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetSubject(myMsgPointer,subject.c_str()))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


string cMsgMessage::getType(void) const throw(cMsgException) {

  char *s;

  int stat;
  if((stat=cMsgGetType(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  if(s==NULL) {
    return("null");
  } else {
    string ss = string(s);
    free(s);
    return(ss);
  }
}
  

//-----------------------------------------------------------------------------


void cMsgMessage::setType(const string &type) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetType(myMsgPointer,type.c_str()))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


string cMsgMessage::getText(void) const throw(cMsgException) {

  char *s;

  int stat;
  if((stat=cMsgGetText(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  if(s==NULL) {
    return("null");
  } else {
    string ss = string(s);
    free(s);
    return(ss);
  }
}


//-----------------------------------------------------------------------------


void cMsgMessage::setText(const string &text) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetText(myMsgPointer,text.c_str()))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsgMessage::setByteArrayLength(int length) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetByteArrayLength(myMsgPointer,length)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


int cMsgMessage::getByteArrayLength(void) const throw(cMsgException) {

  int i;

  int stat;
  if((stat=cMsgGetByteArrayLength(myMsgPointer,&i))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(i);
}


//-----------------------------------------------------------------------------


void cMsgMessage::setByteArrayOffset(int offset) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetByteArrayOffset(myMsgPointer,offset)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


int cMsgMessage::getByteArrayOffset(void) const throw(cMsgException) {

  int i;

  int stat;
  if((stat=cMsgGetByteArrayOffset(myMsgPointer,&i))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(i);
}


//-----------------------------------------------------------------------------


void cMsgMessage::setByteArray(char *array) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetByteArray(myMsgPointer,array)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


char* cMsgMessage::getByteArray(void) const throw(cMsgException) {

  char *p;

  int stat;
  if((stat=cMsgGetByteArray(myMsgPointer,&p)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(p);
}


//-----------------------------------------------------------------------------



void cMsgMessage::setByteArrayAndLimits(char *array, int offset, int length) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetByteArrayAndLimits(myMsgPointer,array,offset,length)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsgMessage::copyByteArray(char* array, int offset, int length) throw(cMsgException) {

  int stat;
  if((stat=cMsgCopyByteArray(myMsgPointer,array,offset,length)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


int cMsgMessage::getUserInt(void) const throw(cMsgException) {

  int i;

  int stat;
  if((stat=cMsgGetUserInt(myMsgPointer,&i))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(i);
}


//-----------------------------------------------------------------------------


void cMsgMessage::setUserInt(int i) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetUserInt(myMsgPointer,i))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


struct timespec cMsgMessage::getUserTime(void) const throw(cMsgException) {

  struct timespec t;

  int stat;
  if((stat=cMsgGetUserTime(myMsgPointer,&t))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(t);
}


//-----------------------------------------------------------------------------


void cMsgMessage::setUserTime(const struct timespec &userTime) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetUserTime(myMsgPointer, &userTime))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


int cMsgMessage::getVersion(void) const throw(cMsgException) {

  int version;

  int stat;
  if((stat=cMsgGetVersion(myMsgPointer, &version))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(version);
}


//-----------------------------------------------------------------------------


cMsgMessage *cMsgMessage::copy(void) const throw(cMsgException) {

  void *newPointer = cMsgCopyMessage(myMsgPointer);
  return(new cMsgMessage(newPointer));
}


//-----------------------------------------------------------------------------


string cMsgMessage::getCreator() const throw(cMsgException) {

  char *s;

  int stat;
  if((stat=cMsgGetCreator(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    string ss = string(s);
    free(s);
    return(ss);
  }
}


//-----------------------------------------------------------------------------


string cMsgMessage::getDomain() const throw(cMsgException) {

  char *s;

  int stat;
  if((stat=cMsgGetDomain(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    string ss = string(s);
    free(s);
    return(ss);
  }
}


//-----------------------------------------------------------------------------


string cMsgMessage::getReceiver() const throw(cMsgException) {

  char *s;

  int stat;
  if((stat=cMsgGetReceiver(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    string ss = string(s);
    free(s);
    return(ss);
  }
}


//-----------------------------------------------------------------------------


string cMsgMessage::getReceiverHost() const throw(cMsgException) {

  char *s;

  int stat;
  if((stat=cMsgGetReceiverHost(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    string ss = string(s);
    free(s);
    return(ss);
  }
}


//-----------------------------------------------------------------------------


string cMsgMessage::getSender() const throw(cMsgException) {

  char *s;

  int stat;
  if((stat=cMsgGetSender(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    string ss = string(s);
    free(s);
    return(ss);
  }
}


//-----------------------------------------------------------------------------


string cMsgMessage::getSenderHost() const throw(cMsgException) {

  char *s;

  int stat;
  if((stat=cMsgGetSenderHost(myMsgPointer,&s))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  };

  if(s==NULL) {
    return("null");
  } else {
    string ss = string(s);
    free(s);
    return(ss);
  }
}


//-----------------------------------------------------------------------------


struct timespec cMsgMessage::getReceiverTime(void) const throw(cMsgException) {

  struct timespec t;

  int stat;
  if((stat=cMsgGetReceiverTime(myMsgPointer,&t))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(t);
}


//-----------------------------------------------------------------------------


struct timespec cMsgMessage::getSenderTime(void) const throw(cMsgException) {

  struct timespec t;

  int stat;
  if((stat=cMsgGetSenderTime(myMsgPointer,&t))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(t);
}


//-----------------------------------------------------------------------------


bool cMsgMessage::isGetRequest() const throw(cMsgException) {
  
  int b;

  int stat;
  if((stat=cMsgGetGetRequest(myMsgPointer,&b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(b);
}


//-----------------------------------------------------------------------------


bool cMsgMessage::isGetResponse() const throw(cMsgException) {
  
  int b;

  int stat;
  if((stat=cMsgGetGetResponse(myMsgPointer,&b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(b);
}


//-----------------------------------------------------------------------------


bool cMsgMessage::isNullGetResponse() const throw(cMsgException) {
  
  int b;

  int stat;
  if((stat=cMsgGetNullGetResponse(myMsgPointer,&b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(b);
}


//-----------------------------------------------------------------------------


int cMsgMessage::getByteArrayEndian(void) const throw(cMsgException) {
  int stat,endian;

  if((stat=cMsgGetByteArrayEndian(myMsgPointer,&endian))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  return(endian);
}


//-----------------------------------------------------------------------------


void cMsgMessage::setByteArrayEndian(int endian) throw(cMsgException) {
  int stat;

  if((stat=cMsgSetByteArrayEndian(myMsgPointer,endian))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  return;
}


//-----------------------------------------------------------------------------


bool cMsgMessage::needToSwap(void) const throw(cMsgException) {

  int flag,stat;

  if((stat=cMsgNeedToSwap(myMsgPointer,&flag))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  return(flag==1);
}


//-----------------------------------------------------------------------------


void cMsgMessage::makeNullResponse(cMsgMessage &msg) throw(cMsgException) {

  cMsgMessage_t *t = (cMsgMessage_t*)myMsgPointer;
  cMsgMessage_t *m = (cMsgMessage_t*)msg.myMsgPointer;

  t->sysMsgId    = m->sysMsgId;
  t->senderToken = m->senderToken;
  t->info        = CMSG_IS_GET_RESPONSE | CMSG_IS_NULL_GET_RESPONSE;
}


//-----------------------------------------------------------------------------


void cMsgMessage::makeNullResponse(cMsgMessage *msg) throw(cMsgException) {

  cMsgMessage_t *t = (cMsgMessage_t*)myMsgPointer;
  cMsgMessage_t *m = (cMsgMessage_t*)msg->myMsgPointer;

  t->sysMsgId    = m->sysMsgId;
  t->senderToken = m->senderToken;
  t->info        = CMSG_IS_GET_RESPONSE | CMSG_IS_NULL_GET_RESPONSE;
}


//-----------------------------------------------------------------------------


void cMsgMessage::makeResponse(cMsgMessage &msg) throw(cMsgException) {

  cMsgMessage_t *t = (cMsgMessage_t*)myMsgPointer;
  cMsgMessage_t *m = (cMsgMessage_t*)msg.myMsgPointer;

  t->sysMsgId    = m->sysMsgId;
  t->senderToken = m->senderToken;
  t->info        = CMSG_IS_GET_RESPONSE;
}


//-----------------------------------------------------------------------------


void cMsgMessage::makeResponse(cMsgMessage *msg) throw(cMsgException) {

  cMsgMessage_t *t = (cMsgMessage_t*)myMsgPointer;
  cMsgMessage_t *m = (cMsgMessage_t*)msg->myMsgPointer;

  t->sysMsgId    = m->sysMsgId;
  t->senderToken = m->senderToken;
  t->info        = CMSG_IS_GET_RESPONSE;
}


//-----------------------------------------------------------------------------


cMsgMessage *cMsgMessage::nullResponse(void) const throw(cMsgException) {

  void *newMsgPointer;
  if((newMsgPointer=cMsgCreateNullResponseMessage(myMsgPointer))==NULL) {
    throw(cMsgException("?cMsgMessage::nullResponse...unable to create message",CMSG_ERROR));
  }

  return(new cMsgMessage(newMsgPointer));
}


//-----------------------------------------------------------------------------


cMsgMessage *cMsgMessage::response(void) const throw(cMsgException) {

  void *newMsgPointer;
  if((newMsgPointer=cMsgCreateResponseMessage(myMsgPointer))==NULL) {
    throw(cMsgException("?cMsgMessage::response...unable to create message",CMSG_ERROR));
  }

  return(new cMsgMessage(newMsgPointer));
}


//-----------------------------------------------------------------------------


void cMsgMessage::setGetResponse(bool b) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetGetResponse(myMsgPointer,b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsgMessage::setNullGetResponse(bool b) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetNullGetResponse(myMsgPointer,b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


string cMsgMessage::toString(void) const throw(cMsgException) {

  char *cs;

  cMsgToString(myMsgPointer,&cs);
  string s(cs);
  free(cs);
  return(s);
           
}


//-----------------------------------------------------------------------------
// cMsgCallbackAdapter methods
//-----------------------------------------------------------------------------


bool cMsgCallbackAdapter::maySkipMessages(void) {
  return(false);
}

//-----------------------------------------------------------------------------


bool cMsgCallbackAdapter::mustSerializeMessages(void) {
  return(true);
}

//-----------------------------------------------------------------------------


int cMsgCallbackAdapter::getMaxCueSize(void) {
  return(1000);
}


//-----------------------------------------------------------------------------


int cMsgCallbackAdapter::getSkipSize(void) {
  return(200);
}


//-----------------------------------------------------------------------------


int cMsgCallbackAdapter::getMaxThreads(void) {
  return(100);
}


//-----------------------------------------------------------------------------


int cMsgCallbackAdapter::getMessagesPerThread(void) {
  return(50);
}


//-----------------------------------------------------------------------------
//  cMsg methods
//-----------------------------------------------------------------------------


cMsg::cMsg(const string &UDL, const string &name, const string &descr) 
  : myUDL(UDL), myName(name), myDescr(descr), initialized(false) {
}


//-----------------------------------------------------------------------------


cMsg::~cMsg(void) {
  cMsg::disconnect();
}


//-----------------------------------------------------------------------------


void cMsg::connect(void) throw(cMsgException) {

  if(initialized)throw(cMsgException(cMsgPerror(CMSG_ALREADY_INIT),CMSG_ALREADY_INIT));


  int stat;
  if((stat=cMsgConnect(myUDL.c_str(),myName.c_str(),myDescr.c_str(),&myDomainId))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  initialized=true;
}


//-----------------------------------------------------------------------------


void cMsg::disconnect(void) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  cMsgDisconnect(myDomainId);
}


//-----------------------------------------------------------------------------


void cMsg::send(cMsgMessage &msg) throw(cMsgException) {
    
  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  if((stat=cMsgSend(myDomainId,msg.myMsgPointer))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::send(cMsgMessage *msg) throw(cMsgException) {
  cMsg::send(*msg);
}


//-----------------------------------------------------------------------------


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


int cMsg::syncSend(cMsgMessage *msg, const struct timespec *timeout) throw(cMsgException) {
  return(cMsg::syncSend(*msg, timeout));
}


//-----------------------------------------------------------------------------


void *cMsg::subscribe(const string &subject, const string &type, cMsgCallbackAdapter *cba, void *userArg) throw(cMsgException) {
    
  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  void *handle;

  // check if this is a duplicate subscription
  if(subscriptionExists(myDomainId,subject,type,cba,userArg))
    throw(cMsgException(cMsgPerror(CMSG_ALREADY_EXISTS),CMSG_ALREADY_EXISTS));


  // create and fill config
  cMsgSubscribeConfig *myConfig = cMsgSubscribeConfigCreate();

  cMsgSubscribeSetMaxCueSize(myConfig,        cba->getMaxCueSize());
  cMsgSubscribeSetSkipSize(myConfig,          cba->getSkipSize());
  cMsgSubscribeSetMaySkip(myConfig,           (cba->maySkipMessages())?1:0);
  cMsgSubscribeSetMustSerialize(myConfig,     (cba->mustSerializeMessages())?1:0);
  cMsgSubscribeSetMaxThreads(myConfig,        cba->getMaxThreads());
  cMsgSubscribeSetMessagesPerThread(myConfig, cba->getMessagesPerThread());


  // create and fill dispatcher struct
  dispatcherStruct *d = new dispatcherStruct();
  d->cba=cba;
  d->userArg=userArg;


  // subscribe and get handle
  stat=cMsgSubscribe(myDomainId,subject.c_str(),type.c_str(),callbackDispatcher,(void*)d,myConfig,&handle);


  // destroy config
  cMsgSubscribeConfigDestroy(myConfig);


  // check if subscription accepted
  if(stat!=CMSG_OK) throw(cMsgException(cMsgPerror(stat),stat));


  // add this subscription to internal list
  addSubscription(myDomainId,subject,type,d,handle);


  return(handle);
}


//-----------------------------------------------------------------------------


void *cMsg::subscribe(const string &subject, const string &type, cMsgCallbackAdapter &cba, void *userArg) throw(cMsgException) {
  return(cMsg::subscribe(subject, type, &cba, userArg));
}


//-----------------------------------------------------------------------------


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


void cMsg::flush(const struct timespec *timeout) throw(cMsgException) {
    
  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  if((stat=cMsgFlush(myDomainId, timeout))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::start(void) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  if((stat=cMsgReceiveStart(myDomainId))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::stop(void) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  if((stat=cMsgReceiveStop(myDomainId))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


string cMsg::getDescription(void) const {
  return(myDescr);
}


//-----------------------------------------------------------------------------


string cMsg::getName(void) const {
  return(myName);
}


//-----------------------------------------------------------------------------


string cMsg::getUDL(void) const{
  return(myUDL);
}


//-----------------------------------------------------------------------------


bool cMsg::isConnected(void) const {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat,connected;
  if((stat=cMsgGetConnectState(myDomainId,&connected))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(connected==1);
}


//-----------------------------------------------------------------------------


bool cMsg::isReceiving(void) const {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat,receiving;
  if((stat=cMsgGetReceiveState(myDomainId,&receiving))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(receiving==1);
}


//-----------------------------------------------------------------------------


void cMsg::setShutdownHandler(cMsgShutdownHandler *handler, void* userArg) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  if((stat=cMsgSetShutdownHandler(myDomainId,handler,userArg))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::shutdownClients(string &client, int flag) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  if((stat=cMsgShutdownClients(myDomainId,client.c_str(),flag))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::shutdownServers(string &server, int flag) throw(cMsgException) {

  if(!initialized)throw(cMsgException(cMsgPerror(CMSG_NOT_INITIALIZED),CMSG_NOT_INITIALIZED));


  int stat;
  if((stat=cMsgShutdownServers(myDomainId,server.c_str(),flag))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

// to do
//   shutdown handler
//   defaults for subscribe config
//   documentation



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


#include <cMsgBase.hxx>
#include <cMsgBase.h>
#include <cMsgPrivate.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>


//-----------------------------------------------------------------------------


// misc string conversion functions

static string itos(int i) {
  char c[64];
  sprintf(c,"%d",i);
  return(string(c));

}

static string btos(bool b) {
  return(b?"true":"false");
}


static string ttos(const struct timespec& t) {
  string s(ctime(&t.tv_sec));
  return(s.substr(0,s.length()-1));
}


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
//  cMsgMessageBase methods
//-----------------------------------------------------------------------------


cMsgMessageBase::cMsgMessageBase(void) throw(cMsgException) {
    
  myMsgPointer=cMsgCreateMessage();
  if(myMsgPointer==NULL) {
    throw(cMsgException("?cMsgMessageBase constructor...unable to create message",CMSG_ERROR));
  }
}


//-----------------------------------------------------------------------------


cMsgMessageBase::cMsgMessageBase(const cMsgMessageBase &msg) throw(cMsgException) {

  myMsgPointer=cMsgCopyMessage(msg.myMsgPointer);
  if(myMsgPointer==NULL) {
    throw(cMsgException("?cMsgMessageBase copy constructor...unable to create message",CMSG_ERROR));
  }
}


//-----------------------------------------------------------------------------


cMsgMessageBase::cMsgMessageBase(void *msgPointer) throw(cMsgException) {

  myMsgPointer=msgPointer;
  if(myMsgPointer==NULL) {
    throw(cMsgException("?cMsgMessageBase pointer constructor...unable to create message",CMSG_ERROR));
  }
}


//-----------------------------------------------------------------------------


cMsgMessageBase::~cMsgMessageBase(void) {

  if(myMsgPointer==NULL)return;
  cMsgFreeMessage(myMsgPointer);
}


//-----------------------------------------------------------------------------


string cMsgMessageBase::getSubject(void) const throw(cMsgException) {

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


void cMsgMessageBase::setSubject(const string &subject) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetSubject(myMsgPointer,subject.c_str()))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


string cMsgMessageBase::getType(void) const throw(cMsgException) {

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


void cMsgMessageBase::setType(const string &type) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetType(myMsgPointer,type.c_str()))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


string cMsgMessageBase::getText(void) const throw(cMsgException) {

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


void cMsgMessageBase::setText(const string &text) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetText(myMsgPointer,text.c_str()))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::setByteArrayLength(int length) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetByteArrayLength(myMsgPointer,length)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


int cMsgMessageBase::getByteArrayLength(void) const throw(cMsgException) {

  int i;

  int stat;
  if((stat=cMsgGetByteArrayLength(myMsgPointer,&i))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(i);
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::setByteArrayOffset(int offset) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetByteArrayOffset(myMsgPointer,offset)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


int cMsgMessageBase::getByteArrayOffset(void) const throw(cMsgException) {

  int i;

  int stat;
  if((stat=cMsgGetByteArrayOffset(myMsgPointer,&i))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(i);
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::setByteArray(char *array) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetByteArray(myMsgPointer,array)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


char* cMsgMessageBase::getByteArray(void) const throw(cMsgException) {

  char *p;

  int stat;
  if((stat=cMsgGetByteArray(myMsgPointer,&p)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(p);
}


//-----------------------------------------------------------------------------



void cMsgMessageBase::setByteArrayAndLimits(char *array, int offset, int length) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetByteArrayAndLimits(myMsgPointer,array,offset,length)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::copyByteArray(char* array, int offset, int length) throw(cMsgException) {

  int stat;
  if((stat=cMsgCopyByteArray(myMsgPointer,array,offset,length)!=CMSG_OK)) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


int cMsgMessageBase::getUserInt(void) const throw(cMsgException) {

  int i;

  int stat;
  if((stat=cMsgGetUserInt(myMsgPointer,&i))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(i);
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::setUserInt(int i) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetUserInt(myMsgPointer,i))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


struct timespec cMsgMessageBase::getUserTime(void) const throw(cMsgException) {

  struct timespec t;

  int stat;
  if((stat=cMsgGetUserTime(myMsgPointer,&t))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(t);
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::setUserTime(const struct timespec &userTime) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetUserTime(myMsgPointer, &userTime))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


int cMsgMessageBase::getVersion(void) const throw(cMsgException) {

  int version;

  int stat;
  if((stat=cMsgGetVersion(myMsgPointer, &version))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(version);
}


//-----------------------------------------------------------------------------


cMsgMessageBase cMsgMessageBase::copy(void) const throw(cMsgException) {

  void *newPointer = cMsgCopyMessage(myMsgPointer);
  return(cMsgMessageBase(newPointer));
}


//-----------------------------------------------------------------------------


string cMsgMessageBase::getCreator() const throw(cMsgException) {

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


string cMsgMessageBase::getDomain() const throw(cMsgException) {

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


string cMsgMessageBase::getReceiver() const throw(cMsgException) {

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


string cMsgMessageBase::getReceiverHost() const throw(cMsgException) {

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


string cMsgMessageBase::getSender() const throw(cMsgException) {

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


string cMsgMessageBase::getSenderHost() const throw(cMsgException) {

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


struct timespec cMsgMessageBase::getReceiverTime(void) const throw(cMsgException) {

  struct timespec t;

  int stat;
  if((stat=cMsgGetReceiverTime(myMsgPointer,&t))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(t);
}


//-----------------------------------------------------------------------------


struct timespec cMsgMessageBase::getSenderTime(void) const throw(cMsgException) {

  struct timespec t;

  int stat;
  if((stat=cMsgGetSenderTime(myMsgPointer,&t))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(t);
}


//-----------------------------------------------------------------------------


bool cMsgMessageBase::isGetRequest() const throw(cMsgException) {
  
  int b;

  int stat;
  if((stat=cMsgGetGetRequest(myMsgPointer,&b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(b);
}


//-----------------------------------------------------------------------------


bool cMsgMessageBase::isGetResponse() const throw(cMsgException) {
  
  int b;

  int stat;
  if((stat=cMsgGetGetResponse(myMsgPointer,&b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(b);
}


//-----------------------------------------------------------------------------


bool cMsgMessageBase::isNullGetResponse() const throw(cMsgException) {
  
  int b;

  int stat;
  if((stat=cMsgGetNullGetResponse(myMsgPointer,&b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(b);
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::makeNullResponse(cMsgMessageBase &msg) throw(cMsgException) {

  cMsgMessage *t = (cMsgMessage*)myMsgPointer;
  cMsgMessage *m = (cMsgMessage*)msg.myMsgPointer;

  t->sysMsgId    = m->sysMsgId;
  t->senderToken = m->senderToken;
  t->info        = CMSG_IS_GET_RESPONSE | CMSG_IS_NULL_GET_RESPONSE;
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::makeNullResponse(cMsgMessageBase *msg) throw(cMsgException) {

  cMsgMessage *t = (cMsgMessage*)myMsgPointer;
  cMsgMessage *m = (cMsgMessage*)msg->myMsgPointer;

  t->sysMsgId    = m->sysMsgId;
  t->senderToken = m->senderToken;
  t->info        = CMSG_IS_GET_RESPONSE | CMSG_IS_NULL_GET_RESPONSE;
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::makeResponse(cMsgMessageBase &msg) throw(cMsgException) {

  cMsgMessage *t = (cMsgMessage*)myMsgPointer;
  cMsgMessage *m = (cMsgMessage*)msg.myMsgPointer;

  t->sysMsgId    = m->sysMsgId;
  t->senderToken = m->senderToken;
  t->info        = CMSG_IS_GET_RESPONSE;
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::makeResponse(cMsgMessageBase *msg) throw(cMsgException) {

  cMsgMessage *t = (cMsgMessage*)myMsgPointer;
  cMsgMessage *m = (cMsgMessage*)msg->myMsgPointer;

  t->sysMsgId    = m->sysMsgId;
  t->senderToken = m->senderToken;
  t->info        = CMSG_IS_GET_RESPONSE;
}


//-----------------------------------------------------------------------------


cMsgMessageBase cMsgMessageBase::nullResponse(void) const throw(cMsgException) {

  void *newMsgPointer;
  if((newMsgPointer=cMsgCreateNullResponseMessage(myMsgPointer))==NULL) {
    throw(cMsgException("?cMsgMessageBase::nullResponse...unable to create message",CMSG_ERROR));
  }

  return(cMsgMessageBase(newMsgPointer));
}


//-----------------------------------------------------------------------------


cMsgMessageBase cMsgMessageBase::response(void) const throw(cMsgException) {

  void *newMsgPointer;
  if((newMsgPointer=cMsgCreateResponseMessage(myMsgPointer))==NULL) {
    throw(cMsgException("?cMsgMessageBase::response...unable to create message",CMSG_ERROR));
  }

  return(cMsgMessageBase(newMsgPointer));
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::setGetResponse(bool b) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetGetResponse(myMsgPointer,b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::setNullGetResponse(bool b) throw(cMsgException) {

  int stat;
  if((stat=cMsgSetNullGetResponse(myMsgPointer,b))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


string cMsgMessageBase::toString(void) const throw(cMsgException) {

  char *cs;

  cMsgToString(myMsgPointer,&cs);
  string s(cs);
  free(cs);
  return(s);
           
}


//-----------------------------------------------------------------------------
// cMsgShutdownHandlerDefault methods
//-----------------------------------------------------------------------------


void cMsgShutdownHandlerDefault::handleShutdown(void) {
  exit(-1);
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
  return(1000);
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


cMsg::cMsg(const string &UDL, const string &name, const string &descr) {
  myUDL=UDL;
  myName=name;
  myDescr=descr;
  connected=false;
  receiving=false;
}


//-----------------------------------------------------------------------------


cMsg::~cMsg(void) {
  cMsg::disconnect();
}


//-----------------------------------------------------------------------------


void cMsg::connect(void) throw(cMsgException) {

  int stat;
  if((stat=cMsgConnect(myUDL.c_str(),myName.c_str(),myDescr.c_str(),&myDomainId))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  connected=true;
}


//-----------------------------------------------------------------------------


void cMsg::disconnect(void) {

  if(connected)cMsgDisconnect(myDomainId);
  connected=false;
}


//-----------------------------------------------------------------------------


void cMsg::send(cMsgMessageBase &msg) throw(cMsgException) {
    
  int stat;
  if((stat=cMsgSend(myDomainId,msg.myMsgPointer))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::send(cMsgMessageBase *msg) throw(cMsgException) {
  cMsg::send(*msg);
}


//-----------------------------------------------------------------------------


int cMsg::syncSend(cMsgMessageBase &msg) throw(cMsgException) {
    
  int response;

  int stat;
  if((stat=cMsgSyncSend(myDomainId,msg.myMsgPointer,&response))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  return(response);
}


//-----------------------------------------------------------------------------


int cMsg::syncSend(cMsgMessageBase *msg) throw(cMsgException) {
  return(cMsg::syncSend(*msg));
}


//-----------------------------------------------------------------------------


void cMsg::subscribe(const string &subject, const string &type, cMsgCallbackAdapter *cba, void *userArg) throw(cMsgException) {
    

  // create and fill config
  cMsgSubscribeConfig *myConfig = cMsgSubscribeConfigCreate();

  cMsgSubscribeSetMaxCueSize(myConfig,        cba->getMaxCueSize());


  printf("max cue size for subject %s, type %s is %d\n",subject.c_str(),type.c_str(),cba->getMaxCueSize());


  cMsgSubscribeSetSkipSize(myConfig,          cba->getSkipSize());
  cMsgSubscribeSetMaySkip(myConfig,           (cba->maySkipMessages())?1:0);
  cMsgSubscribeSetMustSerialize(myConfig,     (cba->mustSerializeMessages())?1:0);
  cMsgSubscribeSetMaxThreads(myConfig,        cba->getMaxThreads());
  cMsgSubscribeSetMessagesPerThread(myConfig, cba->getMessagesPerThread());


  // subscribe
  int stat=cMsgSubscribe(myDomainId,subject.c_str(),type.c_str(),cba,userArg,myConfig);


  // destroy config
  cMsgSubscribeConfigDestroy(myConfig);


  if(stat!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::subscribe(const string &subject, const string &type, cMsgCallbackAdapter &cba, void *userArg) throw(cMsgException) {
  cMsg::subscribe(subject, type, &cba, userArg);
}


//-----------------------------------------------------------------------------


void cMsg::unsubscribe(const string &subject, const string &type, cMsgCallbackAdapter *cba, void *userArg) throw(cMsgException) {
    
  int stat;
  if((stat=cMsgUnSubscribe(myDomainId,subject.c_str(),type.c_str(),cba,userArg))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::unsubscribe(const string &subject, const string &type, cMsgCallbackAdapter &cba, void *userArg) throw(cMsgException) {
  cMsg::unsubscribe(subject, type, &cba, userArg);
}


//-----------------------------------------------------------------------------


cMsgMessageBase cMsg::sendAndGet(cMsgMessageBase &sendMsg, const struct timespec &timeout) throw(cMsgException) {
    
  void *replyPtr;

  int stat;
  if((stat=cMsgSendAndGet(myDomainId,sendMsg.myMsgPointer,&timeout,&replyPtr))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  return(cMsgMessageBase(replyPtr));
}


//-----------------------------------------------------------------------------


cMsgMessageBase cMsg::sendAndGet(cMsgMessageBase *sendMsg, const struct timespec &timeout) throw(cMsgException) {

  void *replyPtr;

  int stat;
  if((stat=cMsgSendAndGet(myDomainId,sendMsg->myMsgPointer,&timeout,&replyPtr))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  return(cMsgMessageBase(replyPtr));
}

//-----------------------------------------------------------------------------


cMsgMessageBase cMsg::subscribeAndGet(const string &subject, const string &type, const struct timespec &timeout) throw(cMsgException) {

  void *replyPtr;

  int stat;
  if((stat=cMsgSubscribeAndGet(myDomainId,subject.c_str(),type.c_str(),&timeout,&replyPtr))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }

  return(cMsgMessageBase(replyPtr));
}


//-----------------------------------------------------------------------------


void cMsg::flush(void) throw(cMsgException) {
    
  int stat;
  if((stat=cMsgFlush(myDomainId))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::start(void) throw(cMsgException) {

  int stat;
  if((stat=cMsgReceiveStart(myDomainId))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
  receiving=true;
}


//-----------------------------------------------------------------------------


void cMsg::stop(void) throw(cMsgException) {

  receiving=false;

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
  return(connected);
}


//-----------------------------------------------------------------------------


bool cMsg::isReceiving(void) const {
  return(receiving);
}


//-----------------------------------------------------------------------------


void cMsg::setShutdownHandler(cMsgShutdownHandlerInterface *handler, void* userArg) throw(cMsgException) {

  int stat;
//   if((stat=cMsgSetShutdownHandler(myDomainId,handler,userArg))!=CMSG_OK) {
//     throw(cMsgException(cMsgPerror(stat),stat));
//   }
}


//-----------------------------------------------------------------------------


void cMsg::shutdownClients(string &client, int flag) throw(cMsgException) {

  int stat;
  if((stat=cMsgShutdownClients(myDomainId,client.c_str(),flag))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::shutdownServers(string &server, int flag) throw(cMsgException) {

  int stat;
  if((stat=cMsgShutdownServers(myDomainId,server.c_str(),flag))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

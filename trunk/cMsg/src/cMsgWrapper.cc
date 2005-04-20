// to do
//   shutdown handler
//   documentation
//   harmonize C and Java api's 
//   does use of return-by-value (copy constructors) slow things down too much?



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


cMsgMessageBase::~cMsgMessageBase(void) throw(cMsgException) {

  if(myMsgPointer==NULL)return;

  int stat;
  if((stat=cMsgFreeMessage(myMsgPointer))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
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
    return(string(s));
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
    return(string(s));
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
    return(string(s));
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
    return(string(s));
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
    return(string(s));
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
    return(string(s));
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
    return(string(s));
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
    return(string(s));
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
    return(string(s));
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

  time_t now = time(NULL);
  string t(ctime(&now));

  string s(
    "<cMsgMessage date=\"" + t.substr(0,t.length()-1) + "\"\n"
    + "     " + "version              = \"" + itos(getVersion()) + "\"\n"
    + "     " + "domain               = \"" + getDomain() + "\"\n"
    + "     " + "getRequest           = \"" + btos(isGetRequest()) + "\"\n"
    + "     " + "getResponse          = \"" + btos(isGetResponse()) + "\"\n"
    + "     " + "nullGetResponse      = \"" + btos(isNullGetResponse()) + "\"\n"
    + "     " + "creator              = \"" + getCreator() + "\"\n"
    + "     " + "sender               = \"" + getSender() + "\"\n"
    + "     " + "senderHost           = \"" + getSenderHost() + "\"\n"
    + "     " + "senderTime           = \"" + ttos(getSenderTime()) + "\"\n"
    + "     " + "userInt              = \"" + itos(getUserInt()) + "\"\n"
    + "     " + "userTime             = \"" + ttos(getUserTime()) + "\"\n"
    + "     " + "receiver             = \"" + getReceiver() + "\"\n"
    + "     " + "receiverHost         = \"" + getReceiverHost() + "\"\n"
    + "     " + "receiverTime         = \"" + ttos(getReceiverTime()) + "\"\n"
    + "     " + "subject              = \"" + getSubject() + "\"\n"
    + "     " + "type                 = \"" + getType() + "\">\n"
    + "<![CDATA[\n" + getText() + "\n]]>\n"
    + "</cMsgMessage>\n\n");

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


void cMsgCallbackAdapter::callback(cMsgMessageBase msg, void *userObject) {
  return;
}


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
  return(60000);
}


//-----------------------------------------------------------------------------


int cMsgCallbackAdapter::getSkipSize(void) {
  return(10000);
}


//-----------------------------------------------------------------------------


int cMsgCallbackAdapter::getMaxThreads(void) {
  return(1000);
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


void cMsg::disconnect(void) throw(cMsgException) {

  connected=false;

  int stat;
  if((stat=cMsgDisconnect(myDomainId))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
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


void cMsg::shutdown(string &client, string &server, int flag) throw(cMsgException) {

  int stat;
  if((stat=cMsgShutdown(myDomainId,client.c_str(),server.c_str(),flag))!=CMSG_OK) {
    throw(cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

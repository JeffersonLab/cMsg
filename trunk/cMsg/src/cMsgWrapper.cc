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
#include <stdlib.h>
#include <string.h>


/**
 * Method code for cMsg C++ wrapper class.
 */


//-----------------------------------------------------------------------------
//  cMsgException methods
//-----------------------------------------------------------------------------


cMsgException::cMsgException(void) {
  myDescr=NULL;
  myReturnCode=0;
}


//-----------------------------------------------------------------------------


cMsgException::cMsgException(const char *c) {
  myDescr=strdup(c);
  myReturnCode=0;
}


//-----------------------------------------------------------------------------


cMsgException::cMsgException(const char *c, int i) {
  myDescr=strdup(c);
  myReturnCode=i;
}


//-----------------------------------------------------------------------------


cMsgException::~cMsgException(void) {
  if(myDescr!=NULL)free(myDescr);
}


//-----------------------------------------------------------------------------


void cMsgException::setReturnCode(int i) {
  myReturnCode=i;
}


//-----------------------------------------------------------------------------


int cMsgException::getReturnCode(void) {
  return(myReturnCode);
}


//-----------------------------------------------------------------------------


const char *cMsgException::toString(void) {
  return(myDescr);
}



//-----------------------------------------------------------------------------
//  cMsgMessageBase methods
//-----------------------------------------------------------------------------


cMsgMessageBase::cMsgMessageBase(void) {
    
  myMsgPointer=cMsgCreateMessage();
  if(myMsgPointer==NULL) {
    throw(new cMsgException("?cMsgMessageBase...unable to create message",CMSG_ERROR));
  }
}


//-----------------------------------------------------------------------------


cMsgMessageBase::cMsgMessageBase(void *msgPointer) {
  myMsgPointer=msgPointer;
}


//-----------------------------------------------------------------------------


cMsgMessageBase::~cMsgMessageBase(void) {
  cMsgFreeMessage(myMsgPointer);
}


//-----------------------------------------------------------------------------


const char *cMsgMessageBase::getSubject(void) {

  int stat;
  char *s;

  if((stat=cMsgGetSubject(myMsgPointer,&s))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
  return(s);
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::setSubject(const char *subject) {

  int stat=cMsgSetSubject(myMsgPointer,subject);

  if(stat!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


const char *cMsgMessageBase::getType(void) {

  int stat;
  char *s;

  if((stat=cMsgGetType(myMsgPointer,&s))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
  return(s);
}
  

//-----------------------------------------------------------------------------


void cMsgMessageBase::setType(const char *type) {

  int stat = cMsgSetType(myMsgPointer,type);

  if(stat!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


const char *cMsgMessageBase::getText(void) {

  int stat;
  char *s;

  if((stat=cMsgGetText(myMsgPointer,&s))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
  return(s);
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::setText(const char *text) {

  int stat=cMsgSetText(myMsgPointer,text);

  if(stat!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


int cMsgMessageBase::getUserInt(void) {

  int i;
  int stat;
  if((stat=cMsgGetUserInt(myMsgPointer,&i))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
  return(i);
}


//-----------------------------------------------------------------------------


void cMsgMessageBase::setUserInt(int i) {

  int stat;
  if((stat=cMsgSetUserInt(myMsgPointer,i))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


int cMsgMessageBase::getVersion(void) {

  int version;
  int stat;
  if((stat=cMsgGetVersion(myMsgPointer, &version))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
  return(version);
}


//-----------------------------------------------------------------------------
// cMsgCallbackAdapter methods
//-----------------------------------------------------------------------------


void cMsgCallbackAdapter::callback(cMsgMessageBase &msg, const void *userObject) {
  delete(&msg);
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


cMsg::cMsg(const char *UDL, const char *name, const char *descr) {

  int stat=cMsgConnect(UDL, name, descr, &myDomainId);
  
  if(stat!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


cMsg::~cMsg(void) {
  cMsgDisconnect(myDomainId);
}


//-----------------------------------------------------------------------------


void cMsg::send(cMsgMessageBase &msg) {
    
  int stat;

  if((stat=cMsgSend(myDomainId,msg.myMsgPointer))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


int cMsg::syncSend(cMsgMessageBase &msg) {
    
  int response;
  int stat;
  if((stat=cMsgSyncSend(myDomainId,msg.myMsgPointer,&response))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
  return(response);
}


//-----------------------------------------------------------------------------


void cMsg::flush(void) {
    
  int stat;
  if((stat=cMsgFlush(myDomainId))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::subscribe(const char *subject, const char *type, cMsgCallbackAdapter *cba, const void *userArg) {
    

  // create and fill config
  cMsgSubscribeConfig *myConfig = cMsgSubscribeConfigCreate();

  cMsgSubscribeSetMaxCueSize(myConfig,        cba->getMaxCueSize());
  cMsgSubscribeSetSkipSize(myConfig,          cba->getSkipSize());
  cMsgSubscribeSetMaySkip(myConfig,           (cba->maySkipMessages())?1:0);
  cMsgSubscribeSetMustSerialize(myConfig,     (cba->mustSerializeMessages())?1:0);
  cMsgSubscribeSetMaxThreads(myConfig,        cba->getMaxThreads());
  cMsgSubscribeSetMessagesPerThread(myConfig, cba->getMessagesPerThread());


  // subscribe
  int stat=cMsgSubscribe(myDomainId,subject,type,cba,(void*)userArg,myConfig);


  // destroy config
  cMsgSubscribeConfigDestroy(myConfig);


  if(stat!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::subscribe(const char *subject, const char *type, cMsgCallbackAdapter &cba, const void *userArg) {
  cMsg::subscribe(subject, type, &cba, userArg);
}


//-----------------------------------------------------------------------------


void cMsg::unsubscribe(const char *subject, const char *type, cMsgCallbackAdapter *cba, const void *userArg) {
    
  int stat=cMsgUnSubscribe(myDomainId,subject,type,cba,(void*)userArg);

  if(stat!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::unsubscribe(const char *subject, const char *type, cMsgCallbackAdapter &cba, const void *userArg) {
  cMsg::unsubscribe(subject, type, &cba, (void*)userArg);
}


//-----------------------------------------------------------------------------


void cMsg::sendAndGet( cMsgMessageBase &sendMsg, const struct timespec *timeout, cMsgMessageBase **replyMsg) {
    
  void *replyPtr;

  int stat;
  if((stat=cMsgSendAndGet(myDomainId,sendMsg.myMsgPointer,(struct timespec*)timeout,&replyPtr))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
  *replyMsg = new cMsgMessageBase(replyPtr);
}


//-----------------------------------------------------------------------------


void cMsg::subscribeAndGet(const char *subject, const char *type, const struct timespec *timeout, cMsgMessageBase **replyMsg) {

  void *replyPtr;

  int stat = cMsgSubscribeAndGet(myDomainId,subject,type,(struct timespec*)timeout,&replyPtr);

  if(stat!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }

  *replyMsg = new cMsgMessageBase(replyPtr);
}


//-----------------------------------------------------------------------------


void cMsg::start(void) {

  int stat;
  if((stat=cMsgReceiveStart(myDomainId))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::stop(void) {

  int stat;
  if((stat=cMsgReceiveStop(myDomainId))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::disconnect(void) {

  int stat;
  if((stat=cMsgDisconnect(myDomainId))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------






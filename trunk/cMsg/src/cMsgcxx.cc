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


#include "cMsg.h"
#include "stdlib.h"
#include "string.h"



/**
 * Contains code for all cMsg package methods.
 */


//-----------------------------------------------------------------------------
//  cMsgException methods
//-----------------------------------------------------------------------------


cMsgException::cMsgException(void) {
  descr=NULL;
  returnCode=0;
}


//-----------------------------------------------------------------------------


cMsgException::cMsgException(const char *c) {
  descr=strdup(c);
  returnCode=0;
}


//-----------------------------------------------------------------------------


cMsgException::cMsgException(const char *c, int i) {
  descr=strdup(c);
  returnCode=i;
}


//-----------------------------------------------------------------------------


cMsgException::~cMsgException(void) {
  if(descr!=NULL)free(descr);
}


//-----------------------------------------------------------------------------


void cMsgException::setReturnCode(int i) {
  returnCode=i;
}


//-----------------------------------------------------------------------------


int cMsgException::getReturnCode(void) {
  return(returnCode);
}


//-----------------------------------------------------------------------------


const char *cMsgException::toString(void) {
  return(descr);
}



//-----------------------------------------------------------------------------
//  cMsgMessage methods
//-----------------------------------------------------------------------------


cMsgMessage::cMsgMessage(void) {
    
  myMsgPointer=cMsgCreateMessage();
  if(myMsgPointer==NULL) {
    throw(new cMsgException("?cMsgMessage...unable to create message",CMSG_ERROR));
  }
}


//-----------------------------------------------------------------------------


cMsgMessage::cMsgMessage(void *msgPointer) {
  myMsgPointer=msgPointer;
}


//-----------------------------------------------------------------------------


cMsgMessage::~cMsgMessage(void) {
  cMsgFreeMessage(myMsgPointer);
}


//-----------------------------------------------------------------------------


const char *cMsgMessage::getSubject(void) {

  int stat;
  char *s;
  if((stat=cMsgGetSubject(myMsgPointer,&s))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
  return(s);
}


//-----------------------------------------------------------------------------


void cMsgMessage::setSubject(char *subject) {

  int stat;
  if((stat=cMsgSetSubject(myMsgPointer,subject))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


const char *cMsgMessage::getType(void) {

  char *s;
  int stat;
  if((stat=cMsgGetType(myMsgPointer,&s))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
  return(s);
}
  

//-----------------------------------------------------------------------------


void cMsgMessage::setType(char *type) {

  int stat;
  if((stat=cMsgSetType(myMsgPointer,type))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


const char *cMsgMessage::getText(void) {

  char *s;
  int stat;
  if((stat=cMsgGetText(myMsgPointer,&s))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
  return(s);
}


//-----------------------------------------------------------------------------


void cMsgMessage::setText(char *text) {

  int stat;
  if((stat=cMsgSetText(myMsgPointer,text))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


int cMsgMessage::getUserInt(void) {

  int i;
  int stat;
  if((stat=cMsgGetUserInt(myMsgPointer,&i))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
  return(i);
}


//-----------------------------------------------------------------------------


void cMsgMessage::setUserInt(int i) {

  int stat;
  if((stat=cMsgSetUserInt(myMsgPointer,i))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


int cMsgMessage::getVersion(void) {

  int version;
  int stat;
  if((stat=cMsgGetVersion(myMsgPointer, &version))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
  return(version);
}



//-----------------------------------------------------------------------------
//  cMsgCallback methods
//-----------------------------------------------------------------------------


void cMsgCallback::callback(cMsgMessage &msg, void *userObject) {};



//-----------------------------------------------------------------------------
//  cMsg methods
//-----------------------------------------------------------------------------


cMsg::cMsg(char *UDL, char *name, char *descr) {

  int stat;
  if((stat=cMsgConnect(UDL, name, descr, &myDomainId))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::send(cMsgMessage &msg) {
    
  int stat;
  if((stat=cMsgSend(myDomainId,msg.myMsgPointer))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


int cMsg::syncSend(cMsgMessage &msg) {
    
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


//  void cMsg::subscribe(char *subject, char *type, cMsgCallbackAdapter &cba, void *userArg) {
void cMsg::subscribe(char *subject, char *type, cMsgCallback *cba, void *userArg) {
    
  int stat;
  if((stat=cMsgSubscribe(myDomainId,subject,type,cba,userArg,NULL))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


//  void cMsg::unsubscribe(char *subject, char *type, cMsgCallbackAdapter &cba, void *userArg) {
void cMsg::unsubscribe(char *subject, char *type, cMsgCallback *cba, void *userArg) {
    
  int stat;
  if((stat=cMsgUnSubscribe(myDomainId,subject,type,cba,userArg))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
}


//-----------------------------------------------------------------------------


void cMsg::sendAndGet(cMsgMessage &sendMsg, struct timespec *timeout, cMsgMessage &replyMsg) {
    
  void *replyPtr;

  int stat;
  if((stat=cMsgSendAndGet(myDomainId,sendMsg.myMsgPointer,timeout,&replyPtr))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
  replyMsg = new cMsgMessage(replyPtr);
}


//-----------------------------------------------------------------------------


void cMsg::subscribeAndGet(char *subject, char *type, struct timespec *timeout, cMsgMessage &replyMsg) {

  void *replyPtr;

  int stat;
  if((stat=cMsgSubscribeAndGet(myDomainId,subject,type,timeout,&replyPtr))!=CMSG_OK) {
    throw(new cMsgException(cMsgPerror(stat),stat));
  }
  replyMsg = new cMsgMessage(replyPtr);
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






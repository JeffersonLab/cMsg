/*----------------------------------------------------------------------------*
 *                                                                            *
 *  Copyright (c) 2006        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    c. Timmer, 31-Mar-2006, Jefferson Lab                                     *
 *                                                                            *
 *    Authors: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 *
 * Description:
 *
 *  Implements a dummy domain (just prints stuff out) to serve as an example
 *  of how to go about writing a dynamically loadable domain in C.
 *
 *----------------------------------------------------------------------------*/


#include "cMsgPrivate.h"
#include "cMsg.h"
#include "cMsgNetwork.h"


#ifdef VXWORKS
#include <vxWorks.h>
#endif



/* Prototypes of the 17 functions which implement the standard cMsg tasks. */
int   cmsg_dummy_connect(const char *myUDL, const char *myName, const char *myDescription,
              const char *UDLremainder, void **domainId);
int   cmsg_dummy_reconnect(void *domainId);
int   cmsg_dummy_send(void *domainId, void *msg);
int   cmsg_dummy_syncSend(void *domainId, void *msg, int *response);
int   cmsg_dummy_flush(void *domainId);
int   cmsg_dummy_subscribe(void *domainId, const char *subject, const char *type,
                           cMsgCallbackFunc *callback, void *userArg,
                           cMsgSubscribeConfig *config, void **handle);
int   cmsg_dummy_unsubscribe(void *domainId, void *handle);
int   cmsg_dummy_subscriptionPause (void *domainId, void *handle);
int   cmsg_dummy_subscriptionResume(void *domainId, void *handle);
int   cmsg_dummy_subscriptionQueueClear(void *domainId, void *handle);
int   cmsg_dummy_subscriptionQueueCount(void *domainId, void *handle, int *count);
int   cmsg_dummy_subscriptionQueueIsFull(void *domainId, void *handle, int *full);
int   cmsg_dummy_subscriptionMessagesTotal(void *domainId, void *handle, int *total);
int   cmsg_dummy_subscribeAndGet(void *domainId, const char *subject, const char *type,
                                 const struct timespec *timeout, void **replyMsg);
int   cmsg_dummy_sendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout, void **replyMsg);
int   cmsg_dummy_monitor(void *domainId, const char *command, void **replyMsg);
int   cmsg_dummy_start(void *domainId);
int   cmsg_dummy_stop(void *domainId);
int   cmsg_dummy_disconnect(void *domainId);
int   cmsg_dummy_shutdownClients(void *domainId, const char *client, int flag);
int   cmsg_dummy_shutdownServers(void *domainId, const char *server, int flag);
int   cmsg_dummy_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg);
int   cmsg_dummy_isConnected(void *domainId, int *connected);
int   cmsg_dummy_setUDL(void *domainId, const char *udl, const char *remainder);
int   cmsg_dummy_getCurrentUDL(void *domainId, char **udl);


/** List of the functions which implement the standard tasks in this domain. */
static domainFunctions functions = { cmsg_dummy_connect, cmsg_dummy_reconnect,
                                     cmsg_dummy_send, cmsg_dummy_syncSend, cmsg_dummy_flush,
                                     cmsg_dummy_subscribe, cmsg_dummy_unsubscribe,
                                     cmsg_dummy_subscriptionPause, cmsg_dummy_subscriptionResume,
                                     cmsg_dummy_subscriptionQueueClear, cmsg_dummy_subscriptionMessagesTotal,
                                     cmsg_dummy_subscriptionQueueCount, cmsg_dummy_subscriptionQueueIsFull,
                                     cmsg_dummy_subscribeAndGet, cmsg_dummy_sendAndGet,
                                     cmsg_dummy_monitor, cmsg_dummy_start,
                                     cmsg_dummy_stop, cmsg_dummy_disconnect,
                                     cmsg_dummy_shutdownClients, cmsg_dummy_shutdownServers,
                                     cmsg_dummy_setShutdownHandler, cmsg_dummy_isConnected
                                     cmsg_dummy_setUDL, cmsg_dummy_getCurrentUDL
};


/* for registering the domain */
domainTypeInfo dummyDomainTypeInfo = {"dummy",&functions};

/*-------------------------------------------------------------------*/


int cmsg_dummy_setUDL(void *domainId, const char *newUDL, const char *newRemainder) {
  printf("setUDL\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_getCurrentUDL(void *domainId, char **udl) {
  printf("getCurrentUDL\n");
  return(CMSG_OK);
}



/*-------------------------------------------------------------------*/


int cmsg_dummy_connect(const char *myUDL, const char *myName, const char *myDescription,
                  const char *UDLremainder, void **domainId) {
  printf("Connect, my name is %s\n", myName);
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_reconnect(void *domainId) {
  printf("Reconnect\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_send(void *domainId, void *vmsg) {
  printf("Send\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_syncSend(void *domainId, void *vmsg, int *response) {
  *response=0;
  printf("SyncSend\n");
  return(cmsg_dummy_send(domainId, vmsg));
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_subscribeAndGet(void *domainId, const char *subject, const char *type,
                          const struct timespec *timeout, void **replyMsg) {
  printf("SubscribeAndGet\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_sendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout,
                     void **replyMsg) {
  printf("SendAndGet\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_flush(void *domainId) {
  printf("Flush\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_subscribe(void *domainId, const char *subject, const char *type,
                         cMsgCallbackFunc *callback, void *userArg,
                         cMsgSubscribeConfig *config, void **handle) {
  printf("Subscribe\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_unsubscribe(void *domainId, void *handle) {
  printf("Unsubscribe\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_subscriptionResume(void *domainId, void *handle) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_subscriptionQueueClear(void *domainId, void *handle) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_subscriptionQueueCount(void *domainId, void *handle, int *count) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_subscriptionQueueIsFull(void *domainId, void *handle, int *full) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_subscriptionMessagesTotal(void *domainId, void *handle, int *total) {
    return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_monitor(void *domainId, const char *command, void **replyMsg) {
  printf("Monitor\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_start(void *domainId) {
  printf("Start\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_stop(void *domainId) {
  printf("Stop\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_disconnect(void *domainId) {
  printf("Disconnect\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_isConnected(void *domainId, int *connected) {
  printf("IsConnected\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/*   shutdown handler functions                                      */
/*-------------------------------------------------------------------*/


int cmsg_dummy_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg) {
  printf("SetShutdownHandler\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_shutdownClients(void *domainId, const char *client, int flag) {
  printf("ShutdownClients\n");
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cmsg_dummy_shutdownServers(void *domainId, const char *server, int flag) {
  printf("ShutdownServers\n");
  return(CMSG_OK);
}



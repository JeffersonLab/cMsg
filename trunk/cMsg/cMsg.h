/*
 *
 *  cMsg.h
 *
 *  Defines cMsg (CODA Message) API, message structure, and return codes
 *
 *  E.Wolin, C.Timmer, V.Gurjyan, 24-Jun-2004, DAQ Group, Jefferson Lab
 *
 *
 *  Still to do:
 *    should cMsgPerror print or just return string?
 *    should strings be case sensitive?
 *
 *
 *
 *
 *
 * Introduction
 * ------------
 *
 * cMsg is a simple abstract API to an underlying message service.  It is powerful
 * enough to support point-to-point communication, synchronous and asynchronous 
 * publish/subscribe communication, and network-accessible message queues.  Note 
 * that a given underlying implementation may not necessarily implement all these
 * features.  
 *
 *
 * Implementations
 * ---------------
 *
 * The CODA group is supplying a default underlying implementation, but users can
 * add additional implementations if desired.  This is not particularly difficult,
 * but you should probably talk to the CODA group if you want to do this.  The 
 * default implementation has domainType "coda" (see below).
 *
 *
 * Domains
 * -------
 *
 * The abstraction relies on the important concept of a "domain".  A domain is
 * basically a completely isolated namespace.  Messages sent to a domain cannot
 * be seen outside of that domain.  How the domain concept is implemented is up
 * to the underlying implementation.  A complete cMsg domain specifier looks
 * like:
 *
 *       domainType://domainName
 *
 * where the form of the domainName is not specified and is interpreted by the 
 * underlying implementation.  The full domain specifier for the default CODA
 * implementation looks like:
 *
 *      coda://node:port/namespace
 *
 * where node:port correspond to the node and port of a CODA message server, and 
 * namespace allows for multiple domains on the same server.  If the port is missing 
 * a default port is used.
 *
 * A process can connect to many domains if desired.  Note that in the default CODA 
 * implementation domains are implemented in a "heavyweight" manner, via separate 
 * threads, processes, etc.  The efficient, lightweight way to distribute messages
 * within an application is to use subjects (see below).
 *
 *
 * Messages
 * --------
 *
 * All messages within a domain are sent via cMsgSend().  Messages are sent to a
 * subject and have a type, and both are arbitrary strings.  The payload consists of
 * a single text string.  Users must call cMsgFlush() to initiate delivery of messages 
 * in the outbound send queues, although the implementation may deliver messages 
 * before cMsgFlush() is called.
 *
 * Message consumers ask the system to deliver messages to them that match various 
 * subject/type combinations (each may be NULL).  The messages can be delivered 
 * asynchronously to callbacks (via cMsgSubscribe), or synchronously (via cMsgGet).
 * cMsgFree() must be called when the user is done processing the message.
 *
 * cMsgReceiveStart() must be called to start delivering messages to callbacks.  
 * Messages sent to the domain with this subject/type before this call are not
 * received.  Note that cMsgGet() always gets a message.
 *
 * In the default CODA implementation perl-like subject wildcard characters are
 * supported, multiple callbacks for the same subject/type are allowed, and each 
 * callback executes in its own thread.
 *
 *
 *
 *
 *
 * API Specification
 * -----------------
 *
 *   Note:  all calls return an integer cMsg error code, defined below.
 *
 *
 *
 * int cMsgConnect(char *myDomain, char *myName, char *myDescription, int *domainId)
 *
 *   Called once to connect to a domain.  myName must be unique within the domain.  
 *   myDesctiption is an arbitrary string.  If successful, fills domainId, required
 *   by many calls below.  
 *  
 *
 *
 * int cMsgSend(int domainId, char *subject, char *type, char *text)
 *
 *   Sends a text string to subject/type.  Must call cMsgFlush() to force delivery.
 *
 *
 *
 * int cMsgFlush(int domainId)
 *
 *   Force flush of all outgoing send message queues.  Implementation may flush queues
 *   on occasion by itself.
 *
 *
 *
 * int cMsgGet(int domainId, char *subject, char *type, time_t timeout, cMsg **msg)
 *
 *   Synchronously get one message from subject/type, fail if no message within timeout.
 *   Independent of receive start/stop state.
 *
 *
 *
 * int cMsgSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback, void *userArg)
 *
 *    Subscribe to subject/type and deliver message to callback(userarg).
 *
 *
 *
 * int cMsgUnSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback)
 *
 *    Unsubscribe subject/type/callback combination.
 *
 *
 *
 * int cMsgReceiveStart(int domainId)
 *
 *    Enable receipt of subscribed messages and delivery to callbacks.  
 *
 *
 *
 * int cMsgReceiveStop(int domainId)
 *
 *    Stop receipt of subscribed messages and delivery to callbacks.  
 *
 *
 *
 * int cMsgFree(cMsg *msg)
 *
 *    Free message and deallocate memory.  Must be called to avoid memory leaks.
 *
 *
 *
 * int cMsgDisconnect(int domainId)
 *
 *    Disconnect from domain, unregister all callbacks, stop message receipt, etc.
 *
 *
 *
 * int cMsgPerror(int error)
 *
 *    Print information about a cMsg error return code.
 *
 *
 *
 *  Access functions
 *  ----------------
 *
 * See below for a long list of information access functions, all of which should
 * be self-explanatory (e.g. cMsgGetName(), etc.)
 *
 *
 */


#ifndef _cMsg_h
#define _cMsg_h


/* sccs id */
char sccsid[] = "%Z% cMsg abstract API definition";


/* required includes */
#include <time.h>


/* message structure */
typedef struct cMsg {
  int domainId;
  int sysMsgId;
  int receiverSubscribeId;
  char *sender;
  int senderId;
  char *senderHost;
  time_t senderTime;
  int senderMsgId;
  char *receiver;
  char *receiverHost;
  time_t receiverTime;
  char *domain;
  char *subject;
  char *type;
  char *text;
};


/* message receive callback */
typedef void cMsgCallback(cMsg *msg, void *userArg);


/* function prototypes */
#ifdef __cplusplus
extern "C" {
#endif

  int cMsgConnect(char *myDomain, char *myName, char *myDescription, int *domainId);
  int cMsgSend(int domainId, char *subject, char *type, char *text);
  int cMsgFlush(int domainId);
  int cMsgGet(int domainId, char *subject, char *type, time_t timeout, cMsg **msg);
  int cMsgSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback, void *userArg);
  int cMsgUnSubscribe(int domainId, char *subject, char *type, cMsgCallback *callback);
  int cMsgReceiveStart(int domainId);
  int cMsgReceiveStop(int domainId);
  int cMsgFree(cMsg *msg);
  int cMsgDisconnect(int domainId);
  int cMsgPerror(int error);
  

  char*     cMsgGetDomain(int domainId);
  char*     cMsgGetName(int domainId);
  char*     cMsgGetDescription(int domainId);
  char*     cMsgGetHost(int domainId);
  int 	    cMsgGetSendSocket(int domainId);
  int 	    cMsgGetReceiveSocket(int domainId);
  pthread_t cMsgGetPendThread(int domainId);
  int 	    cMsgGetInitState(int domainId);
  int 	    cMsgGetReceiveState(int domainId);


#ifdef __cplusplus
}
#endif


/* return codes */
enum {
  CMSG_OK              	  = 0,
  CMSG_ERROR,
  CMSG_NOT_IMPLEMENTED,
  CMSG_BAD_ARGUMENT,
  CMSG_NAME_EXISTS,
  CMSG_NOT_INITIALIZED,
  CMSG_ALREADY_INIT,
  CMSG_LOST_CONNECTION,
  CMSG_TIMEOUT,
  CMSG_NETWORK_ERROR,
  CMSG_PEND_ERROR,
  CMSG_ILLEGAL_MSGTYPE,
  CMSG_OUT_OF_MEMORY,
};


#endif /* _cMsg_h */

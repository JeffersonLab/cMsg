/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    Author:  Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *      Header for cMsg routines
 *
 *----------------------------------------------------------------------------*/
 
#ifndef __cMsgPrivate_h
#define __cMsgPrivate_h

#include "cMsg.h"

#ifdef	__cplusplus
extern "C" {
#endif

/* debug levels */
#define CMSG_DEBUG_NONE   0
#define CMSG_DEBUG_SEVERE 1
#define CMSG_DEBUG_ERROR  2
#define CMSG_DEBUG_WARN   3
#define CMSG_DEBUG_INFO   4

/* see "Programming with POSIX threads' by Butenhof */
#define err_abort(code,text) do { \
    fprintf (stderr, "%s at \"%s\":%d: %s\n", \
        text, __FILE__, __LINE__, strerror (code)); \
    exit (-1); \
    } while (0)


/* system msg id types */
enum msgId {
  CMSG_SERVER_CONNECT     = 0,
  CMSG_SERVER_RESPONSE,
  CMSG_KEEP_ALIVE,
  CMSG_HEARTBEAT,
  CMSG_SHUTDOWN,
  CMSG_GET_REQUEST,
  CMSG_GET_RESPONSE,
  CMSG_SEND_REQUEST,
  CMSG_SEND_RESPONSE,
  CMSG_SUBSCRIBE_REQUEST,
  CMSG_UNSUBSCRIBE_REQUEST,
  CMSG_SUBSCRIBE_RESPONSE
};


/* struct for passing data from main to network threads */
typedef struct mainThreadInfo_t {
  int domainId;  /* domain id # */
  int listenFd;  /* listening socket file descriptor */
  int blocking;  /* block in accept (CMSG_BLOCKING) or
                     not (CMSG_NONBLOCKING)? */
} mainThreadInfo;

/* prototypes of non-public functions */
int   readMessage(int fd, cMsg *msg);
int   cMsgRunCallbacks(int domainId, int command, cMsg *msg);
void *cMsgServerListeningThread(void *arg);


#ifdef	__cplusplus
}
#endif

#endif

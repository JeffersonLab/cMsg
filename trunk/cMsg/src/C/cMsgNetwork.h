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
 *      Header for cMsg routines dealing with network communications
 *
 *----------------------------------------------------------------------------*/
 
#ifndef __cMsgNetwork_h
#define __cMsgNetwork_h


#ifdef VXWORKS
#include <ioLib.h>       /* writev */
#include <inetLib.h>     /* htonl stuff */
#else
#include <sys/uio.h>     /* writev */
#include <arpa/inet.h>	 /* htonl stuff */
#endif


#include <sys/types.h>	 /* basic system data types */
#include <sys/socket.h>	 /* basic socket definitions */
#include <netinet/in.h>	 /* sockaddr_in{} and other Internet defns */
/*#include <netinet/ip.h>*/	 /* IPTOS_LOWDELAY defn */
#include <netinet/tcp.h> /* TCP_NODELAY def */
#include <net/if.h>	 /* find broacast addr */
#include <netdb.h>	 /* herrno */
#include <fcntl.h>


#ifdef sun
#include <sys/sockio.h>  /* find broacast addr */
#else
#include <sys/ioctl.h>   /* find broacast addr */
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#if defined sun || defined VXWORKS || defined __APPLE__
#  define socklen_t int
#endif

#ifdef linux
#ifndef _SC_IOV_MAX
#  define _SC_IOV_MAX _SC_T_IOV_MAX
#endif
#endif

/* cMsg definitions */
#define	SA                  struct sockaddr
#define LISTENQ             10

/* set send and receive TCP buffer sizes */
#ifdef VXWORKS
/*
 * The 6100 board likes 36K buffers if there is no tcpNoDelay,
 * but it likes 22K if the socket is set for no delay.
 */
#define CMSG_SOCKBUFSIZE    65536
#else
/*#define CMSG_SOCKBUFSIZE    49640*/  /* multiple of 1460 - ethernet MSS */
#define CMSG_SOCKBUFSIZE    65536
#endif

#define CMSG_IOV_MAX        16     /* minimum for POSIX systems */
/*
 * MAXHOSTNAMELEN is defined to be 256 on Solaris and 64 on Linux.
 * Make it to be uniform across all platforms.
 */
#define CMSG_MAXHOSTNAMELEN 256

/** TCP port at which a client starts looking for an unused listening port. */
#define CMSG_CLIENT_LISTENING_PORT 2345
/** TCP port at which a name server starts looking for an unused listening port. */
#define CMSG_NAME_SERVER_STARTING_PORT 3456;
/** TCP port at which a domain server starts looking for an unused listening port. */
#define CMSG_DOMAIN_SERVER_STARTING_PORT 4567;

/** UDP port at which a RC client looks for its server. */
#define RC_BROADCAST_PORT 6543;

/* socket and/or thread blocking options */
#define CMSG_BLOCKING    0
#define CMSG_NONBLOCKING 1

/* cMsg prototypes */
extern int   cMsgTcpListen(int blocking, unsigned short port, int *listenFd);
extern int   cMsgGetListeningSocket(int blocking, unsigned short startingPort,
                                    int *finalPort, int *fd);
extern int   cMsgTcpConnect(const char *ip_address, unsigned short port, int *fd);
extern int   cMsgAccept(int fd, struct sockaddr *sa, socklen_t *salenptr);

extern int   cMsgTcpRead(int fd, void *vptr, int n);
extern int   cMsgTcpWrite(int fd, const void *vptr, int n);
extern int   cMsgTcpWritev(int fd, struct iovec iov[], int nbufs, int iov_max);
extern int   cMsgLocalHost(char *host, int length);

extern int   cMsgStringToNumericIPaddr(const char *ip_address, struct sockaddr_in *addr);
extern int   cMsgLocalByteOrder(int *endian);
extern const char *cMsgHstrerror(int err);

#ifdef	__cplusplus
}
#endif

#endif

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
#include <arpa/inet.h>   /* htonl stuff */
#endif

#include <netinet/tcp.h> /* TCP_NODELAY def */

#include <cMsgCommonNetwork.h> 


/*
 * MAXHOSTNAMELEN is defined to be 256 on Solaris and is the max length
 * of the host name so we add one for the terminator. On Linux the
 * situation is less clear but 257 appears to be the max (whether that
 * includes termination is not clear).
 * We need it to be uniform across all platforms since we transfer
 * this info across the network. Define it to be 256 for everyone.
 */
#define CMSG_MAXHOSTNAMELEN 256


/* The following is alot of stuff to define 64 bit byte swapping */

/*
 * Make solaris compatible with Linux. On Solaris,
 * _BIG_ENDIAN  or  _LITTLE_ENDIAN is defined
 * depending on the architecture.
 */
#ifdef sun

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321

#if defined(_BIG_ENDIAN)
#define __BYTE_ORDER __BIG_ENDIAN
#else
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif

/*
 * On vxworks, _BIG_ENDIAN = 1234 and _LITTLE_ENDIAN = 4321,
 * which is a bit backwards. _BYTE_ORDER is also defined.
 * In types/vxArch.h, these definitions are carefully set
 * to these reversed values. In other header files such as
 * netinet/ip.h & tcp.h, the values are normal (ie
 * _BIG_ENDIAN = 4321). What's this all about?
 */
#elif VXWORKS

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321

#if _BYTE_ORDER == _BIG_ENDIAN
#define __BYTE_ORDER __BIG_ENDIAN
#else
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif

#endif

/* Byte swapping for 64 bits. */
#if __BYTE_ORDER == __BIG_ENDIAN
#define ntoh64(x) (x)
#define hton64(x) ntoh64(x)
#else
    extern uint64_t NTOH64(uint64_t n);
#define ntoh64(x) NTOH64(x)
#define hton64(x) NTOH64(x)
#endif


#if defined sun || defined VXWORKS || defined __APPLE__
#  define socklen_t int
#endif

#ifdef linux
#ifndef _SC_IOV_MAX
#  define _SC_IOV_MAX _SC_T_IOV_MAX
#endif
#endif


/* set send and receive TCP buffer sizes */
#ifdef VXWORKS
/*
 * The 6100 board likes 36K buffers if there is no tcpNoDelay,
 * but it likes 22K if the socket is set for no delay.
 */
#define CMSG_BIGSOCKBUFSIZE     65536
#else
/*#define CMSG_BIGSOCKBUFSIZE    131072*/
#define CMSG_BIGSOCKBUFSIZE    1024000
#endif

#define CMSG_IOV_MAX        16     /* minimum for POSIX systems */


/** Default TCP port at which a cMsg domain name server listens for client connections. */
#define CMSG_NAME_SERVER_STARTING_PORT 45000

/** Default UDP port at which a cMsg name server listens for broadcasts. */
#define CMSG_NAME_SERVER_MULTICAST_PORT 45000

/** TCP port at which a run control client starts looking for a port to listen on and the port
 * that a run control server assumes a client is waiting for connections on. */
#define RC_CLIENT_LISTENING_PORT 45800

/** Default UDP port at which a run control broadcast server listens for broadcasts
 *  and at which a rc domain client looks for the broadcast server. */
#define RC_MULTICAST_PORT 45200


/** First int to send in UDP multicast to server if cMsg domain. */
#define CMSG_DOMAIN_MULTICAST 1
/** First int to send in UDP multicast to server if RC domain. */
#define RC_DOMAIN_MULTICAST 2

/** The biggest single UDP packet size is 2^16 - IP 64 byte header - 8 byte UDP header. */
#define BIGGEST_UDP_PACKET_SIZE 65463

/** Socket or thread is blocking. */
#define CMSG_BLOCKING    0
/** Socket or thread is nonblocking. */
#define CMSG_NONBLOCKING 1

/** Ints representing ascii for "cMsg is cool", used to filter out portscanning software. */
#define CMSG_MAGIC_INT1 0x634d7367
#define CMSG_MAGIC_INT2 0x20697320
#define CMSG_MAGIC_INT3 0x636f6f6c

/** Multicast address for run control broadcast domain server. */
#define RC_MULTICAST_ADDR "239.210.0.0"
    
/** Multicast address for cMsg domain name server. */
#define CMSG_MULTICAST_ADDR "239.220.0.0"

/** The size (in bytes) of biggest buffers used to send UDP data from client to server. */
#define CMSG_BIGGEST_UDP_BUFFER_SIZE 65536


#ifdef	__cplusplus
}
#endif

#endif

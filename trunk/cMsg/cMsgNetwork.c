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
 *      Routines dealing with network communications.
 *      Modified from original version to work with cMsg.
 *
 *----------------------------------------------------------------------------*/

#ifdef VXWORKS
#include <vxWorks.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#ifdef VXWORKS

#include <taskLib.h>
#include <sockLib.h>
#include <inetLib.h>
#include <hostLib.h>
#include <ioLib.h>
#include <time.h>
#include <net/uio.h>
#include <net/if_dl.h>

#else

#include <sys/uio.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/utsname.h>

#endif

#include <netdb.h>

#ifdef sun
#include <sys/filio.h>
#endif

#include "cMsgNetwork.h"
#include "cMsgPrivate.h"
#include "cMsg.h"

/* set the debug level here */
static int debug = CMSG_DEBUG_INFO;

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/


int cMsgTcpListen(int blocking, unsigned short port, int *listenFd)
{
  int                 listenfd, err, val;
  const int           debug=0, on=1, size=CMSG_SOCKBUFSIZE /* bytes */;
  struct sockaddr_in  servaddr;

  if (listenFd == NULL) {
     if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: null \"listenFd\" argument\n");
     return(CMSG_BAD_ARGUMENT);
  }
  
  err = listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (err < 0) {
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: socket error\n");
    return(CMSG_SOCKET_ERROR);
  }

  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family      = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port        = htons(port);
  
  /* don't wait for messages to cue up, send any message immediately */
  err = setsockopt(listenfd, IPPROTO_TCP, TCP_NODELAY, (const void *) &on, sizeof(on));
  if (err < 0) {
    close(listenfd);
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: setsockopt error\n");
    return(CMSG_SOCKET_ERROR);
  }
  
  /* set send buffer size */
  err = setsockopt(listenfd, SOL_SOCKET, SO_SNDBUF, (const void *) &size, sizeof(size));
  if (err < 0) {
    close(listenfd);
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: setsockopt error\n");
    return(CMSG_SOCKET_ERROR);
  }
  
  /* set receive buffer size */
  err = setsockopt(listenfd, SOL_SOCKET, SO_RCVBUF, (const void *) &size, sizeof(size));
  if (err < 0) {
    close(listenfd);
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: setsockopt error\n");
    return(CMSG_SOCKET_ERROR);
  }
  
  /* reuse this port after program quits */
  err = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void *) &on, sizeof(on));
  if (err < 0) {
    close(listenfd);
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: setsockopt error\n");
    return(CMSG_SOCKET_ERROR);
  }
  
  /* send periodic (every 2 hrs.) signal to see if socket's other end is alive */
  err = setsockopt(listenfd, SOL_SOCKET, SO_KEEPALIVE, (const void *) &on, sizeof(on));
  if (err < 0) {
    close(listenfd);
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: setsockopt error\n");
    return(CMSG_SOCKET_ERROR);
  }
  
  /* make this socket non-blocking if desired */
  if (blocking == CMSG_NONBLOCKING) {
    val = fcntl(listenfd, F_GETFL, 0);
    if (val > -1) {
      fcntl(listenfd, F_SETFL, val | O_NONBLOCK);
    }
  }
  
  /* don't let anyone else have this port */
  err = bind(listenfd, (SA *) &servaddr, sizeof(servaddr));
  if (err < 0) {
    close(listenfd);
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: bind error\n");
    return(CMSG_SOCKET_ERROR);
  }
  
  /* tell system you're waiting for others to connect to this socket */
  err = listen(listenfd, LISTENQ);
  if (err < 0) {
    close(listenfd);
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: listen error\n");
    return(CMSG_SOCKET_ERROR);
  }
  
  if (listenFd != NULL) *listenFd = listenfd;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/* Start with startingPort & keeping trying different port #s until  */
/* one is found that is free for listening on. Try 500 port numbers. */


int cMsgGetListeningSocket(int blocking, unsigned short startingPort, unsigned short *finalPort, int *fd) {
  unsigned short  i, port=startingPort, trylimit=500;
  int listenFd;
  
  /* for a limited number of times */
  for (i=0; i < trylimit; i++) {
    /* try to listen on a port */
    if (cMsgTcpListen(blocking, port, &listenFd) != CMSG_OK) {
      if (debug >= CMSG_DEBUG_WARN) {
	fprintf(stderr, "cMsgGetListeningPort: tried but could not listen on port %hu\n", port);
      }
      port++;
      continue;
    }
    break;
  }

  if (listenFd < 0) {
    if (debug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgServerListeningThread: ports %hu thru %hu busy\n", startingPort, startingPort+499);
    }
    return(CMSG_SOCKET_ERROR);
  }
  
  if (debug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cMsgServerListeningThread: listening on port %hu\n", port);
  }
  
  if (finalPort != NULL) *finalPort = port;
  if (fd != NULL) *fd = listenFd;
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgTcpConnect(const char *ip_address, unsigned short port, int *fd)
{
  int                 sockfd, err;
  const int           debug=1, on=1, size=CMSG_SOCKBUFSIZE /* bytes */;
  struct sockaddr_in  servaddr;
  struct in_addr      **pptr;
  struct hostent      *hp;
  
  if (ip_address == NULL || fd == NULL) {
     if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: null argument(s)\n");
     return(CMSG_BAD_ARGUMENT);
  }
  
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
     if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: socket error, %s\n", strerror(errno));
     return(CMSG_SOCKET_ERROR);
  }
	
  /* don't wait for messages to cue up, send any message immediately */
  err = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const void *) &on, sizeof(on));
  if (err < 0) {
    close(sockfd);
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: setsockopt error\n");
    return(CMSG_SOCKET_ERROR);
  }
  
  /* set send buffer size */
  err = setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (const void *) &size, sizeof(size));
  if (err < 0) {
    close(sockfd);
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: setsockopt error\n");
    return(CMSG_SOCKET_ERROR);
  }
  
  /* set receive buffer size */
  err = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (const void *) &size, sizeof(size));
  if (err < 0) {
    close(sockfd);
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: setsockopt error\n");
    return(CMSG_SOCKET_ERROR);
  }
	
  bzero(&servaddr, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port   = htons(port);

#ifdef VXWORKS

  servaddr.sin_addr.s_addr = hostGetByName(ip_address);
  if (servaddr.sin_addr.s_addr == ERROR) {
    close(sockfd);
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: unknown server address for host %s\n",ip_address);
    return(CMSG_NETWORK_ERROR);
  }

  if (connect(sockfd,(struct sockaddr *) &servaddr, sizeof(servaddr)) == ERROR) {
    close(sockfd);
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: error in connect\n");
    return(CMSG_NETWORK_ERROR);
  }
  else {
    if (debug >= CMSG_DEBUG_INFO) fprintf(stderr, "cMsgTcpConnect: connected to server\n");
  }   

#else
	
  if ((hp = gethostbyname(ip_address)) == NULL) {
    close(sockfd);
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: hostname error - %s\n", cMsgHstrerror(h_errno));
    return(CMSG_NETWORK_ERROR);
  }
  pptr = (struct in_addr **) hp->h_addr_list;

  for ( ; *pptr != NULL; pptr++) {
    memcpy(&servaddr.sin_addr, *pptr, sizeof(struct in_addr));
    if ((err = connect(sockfd, (SA *) &servaddr, sizeof(servaddr))) < 0) {
      if (debug >= CMSG_DEBUG_WARN) fprintf(stderr, "cMsgTcpConnect: error attempting to connect to server\n");
    }
    else {
      if (debug >= CMSG_DEBUG_INFO) fprintf(stderr, "cMsgTcpConnect: connected to server\n");
      break;
    }
  }
  
#endif  


/* for debugging, print out our port */
/*
{
  struct sockaddr_in localaddr;
  socklen_t	  addrlen, len;
  unsigned short  portt;
  
  addrlen = sizeof(localaddr);
  getsockname(sockfd, (SA *) &localaddr, &addrlen);
  portt = ntohs(localaddr.sin_port);
  printf("My Port is %hu\n", portt);
}
*/	
  
  if (err == -1) {
    close(sockfd);
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: socket connect error, %s\n", strerror(errno));
    return(CMSG_NETWORK_ERROR);
  }
  
  if (fd != NULL)  *fd = sockfd;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


int cMsgAccept(int fd, struct sockaddr *sa, socklen_t *salenptr)
{
  int  n;

again:
  if ((n = accept(fd, sa, salenptr)) < 0) {
#ifdef	EPROTO
    if (errno == EPROTO || errno == ECONNABORTED) {
#else
    if (errno == ECONNABORTED) {
#endif
      goto again;
    }
    else {
      if (debug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cMsgAccept: error, errno = %d\n", errno);
      }
    }
  }
  return(n);
}


/*-------------------------------------------------------------------*/


int cMsgTcpWritev(int fd, struct iovec iov[], int nbufs, int iov_max)
{
  struct iovec *iovp;
  int n_write, n_sent, nbytes, cc, i;
  
  /* determine total # of bytes to be sent */
  nbytes = 0;
  for (i=0; i < nbufs; i++) {
    nbytes += iov[i].iov_len;
  }
  
  n_sent = 0;
  while (n_sent < nbufs) {  
    n_write = ((nbufs - n_sent) >= iov_max)?iov_max:(nbufs - n_sent);
    iovp     = &iov[n_sent];
    n_sent  += n_write;
      
   retry:
    if ( (cc = writev(fd, iovp, n_write)) == -1) {
      if (errno == EWOULDBLOCK) {
        goto retry;
      }
      if (debug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr,"tcp_writev(%d,,%d) = writev(%d,,%d) = %d\n",
		fd,nbufs,fd,n_write,cc);
      }
      perror("tcp_writev");
      return(-1);
    }
  }
  return(nbytes);
}


/*-------------------------------------------------------------------*/


int cMsgTcpWrite(int fd, const void *vptr, int n)
{
  int		nleft;
  int		nwritten;
  const char	*ptr;

  ptr = (char *) vptr;
  nleft = n;
  
  while (nleft > 0) {
    if ( (nwritten = write(fd, ptr, nleft)) <= 0) {
      if (errno == EINTR) {
        nwritten = 0;		/* and call write() again */
      }
      else {
        return(nwritten);	/* error */
      }
    }

    nleft -= nwritten;
    ptr   += nwritten;
  }
  return(n);
}


/*-------------------------------------------------------------------*/


int cMsgTcpRead(int fd, void *vptr, int n)
{
  int	nleft;
  int	nread;
  char	*ptr;

  ptr = (char *) vptr;
  nleft = n;
  
  while (nleft > 0) {
    if ( (nread = read(fd, ptr, nleft)) < 0) {
      if (errno == EINTR) {
        nread = 0;		/* and call read() again */
      }
      else {
        return(nread);
      }
    }
    else if (nread == 0) {
      break;			/* EOF */
    }
    
    nleft -= nread;
    ptr   += nread;
  }
  return(n - nleft);		/* return >= 0 */
}


/*-------------------------------------------------------------------*/


int cMsgByteOrder(int *endian)
{
  union {
    short  s;
    char   c[sizeof(short)];
  } un;

  un.s = 0x0102;
  if (sizeof(short) == 2) {
    if (un.c[0] == 1 && un.c[1] == 2) {
      *endian = CMSG_ENDIAN_BIG;
      return(CMSG_OK);
    }
    else if (un.c[0] == 2 && un.c[1] == 1) {
      *endian = CMSG_ENDIAN_LITTLE;
      return(CMSG_OK);
    }
    else {
      if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgByteOrder: unknown endian\n");
      return(CMSG_ERROR);
    }
  }
  else {
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgByteOrder: sizeof(short) = %d\n", sizeof(short));
    return(CMSG_ERROR);
  }
}


/*-------------------------------------------------------------------*/


const char *cMsgHstrerror(int err)
{
    if (err == 0)
	    return("no error");

    if (err == HOST_NOT_FOUND)
	    return("Unknown host");

    if (err == TRY_AGAIN)
	    return("Hostname lookup failure");

    if (err == NO_RECOVERY)
	    return("Unknown server error");

    if (err == NO_DATA)
    return("No address associated with name");

    return("unknown error");
}


/*-------------------------------------------------------------------*/
/*    Return the default fully qualified host name of this host      */


int cMsgDefaultHost(char *host, int length)
{
#ifdef VXWORKS
  if (host == NULL) {
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgDefaultHost: bad argument\n");
    return(CMSG_ERROR);
  }

  if (gethostname(host, length) < 0) return(CMSG_ERROR);
  return(CMSG_OK);

#else
  struct utsname myname;
  struct hostent *hptr;
  
  if (host == NULL) {
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgDefaultHost: bad argument\n");
    return(CMSG_ERROR);
  }

  /* find out the name of the machine we're on */
  if (uname(&myname) < 0) {
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgDefaultHost: cannot find hostname\n");
    return(CMSG_ERROR);
  }
  if ( (hptr = gethostbyname(myname.nodename)) == NULL) {
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgDefaultHost: cannot find hostname\n");
    return(CMSG_ERROR);
  }

  /* return the null-teminated canonical name */
  strncpy(host, hptr->h_name, length);
  host[length-1] = '\0';
  
  return(CMSG_OK);
#endif
}


/*-------------------------------------------------------------------*/
/*      Return the default dotted-decimal address of this host       */


int cMsgDefaultAddress(char *address, int length)
{
#ifdef VXWORKS

  char name[CMSG_MAXHOSTNAMELEN];
  union {
    char ip[4];
    int  ipl;
  } u;

  if (address == NULL) {
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgDefaultAddress: bad argument\n");
    return(CMSG_ERROR);
  }

  if (gethostname(name,CMSG_MAXHOSTNAMELEN)) return(CMSG_ERROR);

  u.ipl = hostGetByName(name);
  if (u.ipl == -1) return(CMSG_ERROR);

  sprintf(address,"%d.%d.%d.%d",u.ip[0],u.ip[1],u.ip[2],u.ip[3]);
  
  return(CMSG_OK);

#else

  struct utsname myname;
  struct hostent *hptr;
  char           **pptr, *val;
  
  if (address == NULL) {
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgDefaultAddress: bad argument\n");
    return(CMSG_ERROR);
  }

  /* find out the name of the machine we're on */
  if (uname(&myname) < 0) {
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgDefaultAddress: cannot find hostname\n");
    return(CMSG_ERROR);
  }
  if ( (hptr = gethostbyname(myname.nodename)) == NULL) {
    if (debug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgDefaultAddress: cannot find hostname\n");
    return(CMSG_ERROR);
  }

  /* find address from hostent structure */
  pptr = hptr->h_addr_list;
  val  = inet_ntoa(*((struct in_addr *) *pptr));
  
  /* return the null-teminated dotted-decimal address */
  if (val == NULL) {
    return(CMSG_ERROR);
  }
  strncpy(address, val, length);
  address[length-1] = '\0';
  
  return(CMSG_OK);
  
#endif
}

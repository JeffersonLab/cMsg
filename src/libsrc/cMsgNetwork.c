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
#include <strings.h>

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>

#ifdef sun
#include <sys/filio.h>
#endif

#include "cMsgNetwork.h"
#include "cMsgPrivate.h"
#include "cMsg.h"
#include "regex.h"

/* set the debug level here */
/* static int cMsgDebug = CMSG_DEBUG_INFO; */


#ifndef VXWORKS
/* mutex to protect non-reentrant gethostbyname in Linux since the
 * gethostbyname_r is so buggy. */
static pthread_mutex_t getHostByNameMutex = PTHREAD_MUTEX_INITIALIZER;
#endif

/*-------------------------------------------------------------------*/
/* Byte swapping for 64 bits. */
/*-------------------------------------------------------------------*/


uint64_t NTOH64(uint64_t n) {
    uint64_t h;
    uint64_t tmp = ntohl(n & 0x00000000ffffffff);
    h = ntohl(n >> 32);
    h |= tmp << 32;
    return h;
}


/*-------------------------------------------------------------------*/


int cMsgTcpListen(int blocking, unsigned short port, int *listenFd)
{
  int                 listenfd, err, val;
  const int           on=1;
  struct sockaddr_in  servaddr;

  if (listenFd == NULL) {
     if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: null \"listenFd\" argument\n");
     return(CMSG_BAD_ARGUMENT);
  }
  
  err = listenfd = socket(AF_INET, SOCK_STREAM, 0);
  if (err < 0) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: socket error\n");
    return(CMSG_SOCKET_ERROR);
  }

  memset((void *)&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family      = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port        = htons(port);
  
  /* don't wait for messages to cue up, send any message immediately */
  err = setsockopt(listenfd, IPPROTO_TCP, TCP_NODELAY, (char*) &on, sizeof(on));
  if (err < 0) {
    close(listenfd);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: setsockopt error\n");
    return(CMSG_SOCKET_ERROR);
  }
   
  /* reuse this port after program quits */
  err = setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (char*) &on, sizeof(on));
  if (err < 0) {
    close(listenfd);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: setsockopt error\n");
    return(CMSG_SOCKET_ERROR);
  }
  
  /* send periodic (every 2 hrs.) signal to see if socket's other end is alive */
  err = setsockopt(listenfd, SOL_SOCKET, SO_KEEPALIVE, (char*) &on, sizeof(on));
  if (err < 0) {
    close(listenfd);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: setsockopt error\n");
    return(CMSG_SOCKET_ERROR);
  }
  
  /* make this socket non-blocking if desired */
  if (blocking == CMSG_NONBLOCKING) {  
#ifdef VXWORKS
    if ( (val = ioctl(listenfd, FIONBIO, (int)&on)) < 0) {
#else
    if ( (val = ioctl(listenfd, FIONBIO, &on)) < 0) {
#endif
      if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: setsockopt error\n");
      return(CMSG_SOCKET_ERROR);
    }
  }
  
  
  /* don't let anyone else have this port */
  err = bind(listenfd, (SA *) &servaddr, sizeof(servaddr));
  if (err < 0) {
    close(listenfd);
    /* Don't print this as it happens often with no effect. */
    /*if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: bind error\n");*/
    return(CMSG_SOCKET_ERROR);
  }
  
  /* tell system you're waiting for others to connect to this socket */
  err = listen(listenfd, LISTENQ);
  if (err < 0) {
    close(listenfd);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpListen: listen error\n");
    return(CMSG_SOCKET_ERROR);
  }
  
  if (listenFd != NULL) *listenFd = listenfd;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/* Start with startingPort & keeping trying different port #s until  */
/* one is found that is free for listening on. Try 1500 port numbers. */


int cMsgGetListeningSocket(int blocking, unsigned short startingPort, int *finalPort, int *fd) {
  unsigned short  i, port=startingPort, trylimit=1500;
  int listenFd;
  
  /* for a limited number of times */
  for (i=0; i < trylimit; i++) {
    /* try to listen on a port */
    if (cMsgTcpListen(blocking, port, &listenFd) != CMSG_OK) {
      if (cMsgDebug >= CMSG_DEBUG_WARN) {
         fprintf(stderr, "cMsgGetListeningPort: tried but could not listen on port %hu\n", port);
      }
      port++;
      if (port < 1025) port = 1025;
      continue;
    }
    break;
  }

  if (listenFd < 0) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgServerListeningThread: ports %hu thru %hu busy\n", startingPort, startingPort+499);
    }
    return(CMSG_SOCKET_ERROR);
  }
  
  if (cMsgDebug >= CMSG_DEBUG_INFO) {
    fprintf(stderr, "cMsgServerListeningThread: listening on port %hu\n", port);
  }
  
  if (finalPort != NULL) *finalPort = port;
  if (fd != NULL) *fd = listenFd;
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/

/** 
 *  (12/4/06)     Default tcp buffer size, bytes
 *  Platform         send      recv
 * --------------------------------------------
 *  Linux java       43690      8192
 *  Linux 2.4,2.6    87380     16384
 *  Solaris 10       49152     49152
 *  
 */

/**
 * This routine makes a TCP connection to a server.
 *
 * @param ip_address name of host to connect to (may be dotted-decimal)
 * @param port port to connect to
 * @param sendBufSize size of socket's send buffer in bytes
 * @param rcvBufSize  size of socket's receive buffer in bytes
 * @param fd pointer to file descriptor which get filled in
 * @param localPort pointer to local (ephemeral) port number which get filled in
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if ip_adress or fd args are NULL
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 * @returns CMSG_SOCKET_ERROR if socket could not be created or socket options could not be set.
 * @returns CMSG_NETWORK_ERROR if host name could not be resolved or could not connect
 *
 */
int cMsgTcpConnect(const char *ip_address, unsigned short port,
                   int sendBufSize, int rcvBufSize, int *fd, int *localPort)
{
  int                 sockfd, err=0;
  const int           on=1;
  struct sockaddr_in  servaddr;
#ifndef VXWORKS
  int                 status;
  struct in_addr      **pptr;
  struct hostent      *hp;
  int h_errnop        = 0;
#ifdef sun
  struct hostent      *result;
  char                *buff;
  int buflen          = 8192;
#endif
#endif

  if (ip_address == NULL || fd == NULL) {
     if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: null argument(s)\n");
     return(CMSG_BAD_ARGUMENT);
  }
  
  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
     if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: socket error, %s\n", strerror(errno));
     return(CMSG_SOCKET_ERROR);
  }
	
  /* don't wait for messages to cue up, send any message immediately */
  err = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char*) &on, sizeof(on));
  if (err < 0) {
    close(sockfd);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: setsockopt error\n");
    return(CMSG_SOCKET_ERROR);
  }
  
  /* set send buffer size unless default specified by a value <= 0 */
  if (sendBufSize > 0) {
    err = setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, (char*) &sendBufSize, sizeof(sendBufSize));
    if (err < 0) {
      close(sockfd);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: setsockopt error\n");
      return(CMSG_SOCKET_ERROR);
    }
  }
  
  /* set receive buffer size unless default specified by a value <= 0  */
  if (rcvBufSize > 0) {
    err = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (char*) &rcvBufSize, sizeof(rcvBufSize));
    if (err < 0) {
      close(sockfd);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: setsockopt error\n");
      return(CMSG_SOCKET_ERROR);
    }
  }
	
  memset((void *)&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_port   = htons(port);

#if defined VXWORKS

  err = cMsgStringToNumericIPaddr(ip_address, &servaddr);
  if (err != CMSG_OK || err == ERROR) {
    close(sockfd);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: unknown server address for host %s\n",ip_address);
    return(CMSG_NETWORK_ERROR);
  }

  if (connect(sockfd,(struct sockaddr *) &servaddr, sizeof(servaddr)) == ERROR) {
    close(sockfd);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: error in connect\n");
    return(CMSG_NETWORK_ERROR);
  }
  else {
    if (cMsgDebug >= CMSG_DEBUG_INFO) fprintf(stderr, "cMsgTcpConnect: connected to server\n");
  }   

  /* find & return the local port number of this socket */
  if (localPort != NULL) {
      int prt, len;
      struct sockaddr_in ss;
      
      len = sizeof(ss);
      if (getsockname(sockfd, (SA *) &ss, &len) == 0) {
          *localPort = (int) ntohs(ss.sin_port);
      }
      else {
          *localPort = 0;
      }
  }

/*
 * Need to make things reentrant so use gethostbyname_r.
 * Unfortunately the linux folks defined the function 
 * differently from the solaris folks!
 */
#elif defined sun
	
  /* Malloc hostent local structure and buffer to store canonical hostname, aliases etc.*/
  if ( (result = (struct hostent *)malloc(sizeof(struct hostent))) == NULL) {
    return(CMSG_OUT_OF_MEMORY); 
  }
  if ( (buff = (char *)malloc(buflen)) == NULL) {
    return(CMSG_OUT_OF_MEMORY);  
  }

  if((hp = gethostbyname_r(ip_address, result, buff, buflen, &h_errnop)) == NULL){
    close(sockfd);
    if (result != NULL) free(result);
    free(buff);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: hostname error - %s\n", cMsgHstrerror(h_errnop));
    return(CMSG_NETWORK_ERROR);
  }
  /*printf("Gethostbyname => %s %d \n", hp->h_name,(int)hp->h_addr_list[0]);*/

  pptr = (struct in_addr **) hp->h_addr_list;

  for ( ; *pptr != NULL; pptr++) {
    memcpy(&servaddr.sin_addr, *pptr, sizeof(struct in_addr));
    if ((err = connect(sockfd, (SA *) &servaddr, sizeof(servaddr))) < 0) {
      free(result);
      free(buff);
      if (cMsgDebug >= CMSG_DEBUG_WARN) {
        fprintf(stderr, "cMsgTcpConnect: error attempting to connect to server\n");
      }
    }
    else {
      /* free the hostent and buff*/
      free(result);
      free(buff);
      if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "cMsgTcpConnect: connected to server\n");
      }
      break;
    }
  }

  /* if there's no error, find & return the local port number of this socket */
  if (err != -1 && localPort != NULL) {
      int prt;
      socklen_t len;
      struct sockaddr_in ss;
      
      len = sizeof(ss);
      if (getsockname(sockfd, (SA *) &ss, &len) == 0) {
          *localPort = (int) ntohs(ss.sin_port);
      }
      else {
          *localPort = 0;
      }
  }
   
/*#elif defined linux || defined Darwin*/
#else

/*
 * There seem to be serious bugs with Linux implementation of
 * gethostbyname_r. See:
 * http://curl.haxx.se/mail/lib-2003-10/0201.html
 * http://bugs.sun.com/bugdatabase/view_bug.do?bug_id=6369541
 
 * Sooo, let's us the non-reentrant version and simply protect
 * with our own mutex.
 */
 
  /* make gethostbyname thread-safe */
  status = pthread_mutex_lock(&getHostByNameMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Lock gethostbyname Mutex");
  }
   
  if ((hp = gethostbyname(ip_address)) == NULL) {
    status = pthread_mutex_unlock(&getHostByNameMutex);
    if (status != 0) {
      cmsg_err_abort(status, "Unlock gethostbyname Mutex");
    }
    close(sockfd);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: hostname error - %s\n", cMsgHstrerror(h_errnop));
    return(CMSG_NETWORK_ERROR);
  }
  pptr = (struct in_addr **) hp->h_addr_list;

  for ( ; *pptr != NULL; pptr++) {
    memcpy(&servaddr.sin_addr, *pptr, sizeof(struct in_addr));
    if ((err = connect(sockfd, (SA *) &servaddr, sizeof(servaddr))) < 0) {
      if (cMsgDebug >= CMSG_DEBUG_WARN) {
        fprintf(stderr, "cMsgTcpConnect: error attempting to connect to server, %s\n", strerror(errno));
      }
    }
    else {
      if (cMsgDebug >= CMSG_DEBUG_INFO) {
        fprintf(stderr, "cMsgTcpConnect: connected to server\n");
      }
      break;
    }
  }

  /* if there's no error, find & return the local port number of this socket */
  if (err != -1 && localPort != NULL) {
      int prt;
      socklen_t len;
      struct sockaddr_in ss;
      
      len = sizeof(ss);
      if (getsockname(sockfd, (SA *) &ss, &len) == 0) {
          *localPort = (int) ntohs(ss.sin_port);
      }
      else {
          *localPort = 0;
      }
  }
   
  status = pthread_mutex_unlock(&getHostByNameMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Unlock gethostbyname Mutex");
  }
    
#endif
  
  if (err == -1) {
    close(sockfd);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgTcpConnect: socket connect error\n");
    return(CMSG_NETWORK_ERROR);
  }
  
  if (fd != NULL)  *fd = sockfd;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/

/**
 * Function to take a string IP address, either an alphabetic host name such as
 * mycomputer.jlab.org or one in presentation format such as 129.57.120.113,
 * and convert it to binary numeric format and place it in a sockaddr_in
 * structure.
 *
 * @param ip_address string IP address of a host
 * @param addr pointer to struct holding the binary numeric value of the host
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if ip_address is null
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 * @returns CMSG_NETWORK_ERROR if the numeric address could not be obtained/resolved
 */
int cMsgStringToNumericIPaddr(const char *ip_address, struct sockaddr_in *addr)
{
  int err=0, isDottedDecimal=0;
  const char *pattern = "([0-9]+\\.[0-9\\.]+)";
  regmatch_t matches[2]; /* we have 2 potential matches: 1 whole, 1 sub */
  regex_t    compiled;
#ifndef VXWORKS
  int                 status;
  struct in_addr      **pptr;
  struct hostent      *hp;
  int h_errnop        = 0;
#ifdef sun
  struct hostent      *result;
  char                *buff;
  int buflen          = 8192;
#endif
#endif

  if (ip_address == NULL) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgStringToNumericIPaddr: null argument\n");
    return(CMSG_BAD_ARGUMENT);
  }
  	
  /*
   * Check to see if ip_address is in dotted-decimal form. If so, use different
   * routines to process that address than if it were a name.
   */

  /* compile regular expression */
  err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED);
  /* this will never happen */
  if (err != 0) {
     return(CMSG_ERROR);
  }

  /* find matches */
  err = cMsgRegexec(&compiled, ip_address, 2, matches, 0);
  if (err != 0) {
    /* no match so not in dotted-decimal form */
    cMsgRegfree(&compiled);
  }
  else {
    isDottedDecimal = 1;
  }


#if defined VXWORKS

  if (isDottedDecimal) {
    err = addr->sin_addr.s_addr = (int) inet_addr((char *) ip_address);
    /* If ip_address = 255.255.255.255, then err = -1 (= ERROR)
     * no matter what. There's no way to check for an error in this
     * case so assume things are OK.
     */
    if (strcmp("255.255.255.255", ip_address) == 0) {
      err = 0;
    }
    /* free up memory */
    cMsgRegfree(&compiled);
  }
  else {
    err = addr->sin_addr.s_addr = hostGetByName((char *) ip_address);
  }
  
  if (err == ERROR) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgStringToNumericIPaddr: unknown address for host %s\n",ip_address);
    }
    return(CMSG_NETWORK_ERROR);
  }

#else

  if (isDottedDecimal) {
    if (inet_pton(AF_INET, ip_address, &addr->sin_addr) < 1) {
      cMsgRegfree(&compiled);
      return(CMSG_NETWORK_ERROR);
    }

    /* free up memory */
    cMsgRegfree(&compiled);
    return(CMSG_OK);
  }
  

/*
 * Need to make things reentrant so use gethostbyname_r.
 * Unfortunately the linux folks defined the function 
 * differently from the solaris folks!
 */
#if defined sun
	
  /* Malloc hostent local structure and buffer to store canonical hostname, aliases etc.*/
  if ( (result = (struct hostent *)malloc(sizeof(struct hostent))) == NULL) {
    return(CMSG_OUT_OF_MEMORY); 
  }
  if ( (buff = (char *)malloc(buflen)) == NULL) {
    return(CMSG_OUT_OF_MEMORY);  
  }

  if((hp = gethostbyname_r(ip_address, result, buff, buflen, &h_errnop)) == NULL){
    if (result != NULL) free(result);
    free(buff);
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgStringToNumericIPaddr: hostname error - %s\n", cMsgHstrerror(h_errnop));
    }
    return(CMSG_NETWORK_ERROR);
  }
  /*printf("Gethostbyname => %s %d \n", hp->h_name,(int)hp->h_addr_list[0]);*/

  pptr = (struct in_addr **) hp->h_addr_list;

  for ( ; *pptr != NULL; pptr++) {
    memcpy(&addr->sin_addr, *pptr, sizeof(struct in_addr));
    break;
  }
  
  free(result);
  free(buff);

  /* else linux || Darwin */
#else

/*
 * There seem to be serious bugs with Linux implementation of
 * gethostbyname_r. See:
 * http://curl.haxx.se/mail/lib-2003-10/0201.html
 * http://bugs.sun.com/bugdatabase/view_bug.do?bug_id=6369541
 
 * Sooo, let's use the non-reentrant version and simply protect
 * with our own mutex.
 */
 
  /* make gethostbyname thread-safe */
  status = pthread_mutex_lock(&getHostByNameMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Lock gethostbyname Mutex");
  }
   
  if ((hp = gethostbyname(ip_address)) == NULL) {
    status = pthread_mutex_unlock(&getHostByNameMutex);
    if (status != 0) {
      cmsg_err_abort(status, "Unlock gethostbyname Mutex");
    }
    if (cMsgDebug >= CMSG_DEBUG_ERROR) {
      fprintf(stderr, "cMsgStringToNumericIPaddr: hostname error - %s\n", cMsgHstrerror(h_errnop));
    }
    return(CMSG_NETWORK_ERROR);
  }
  
  pptr = (struct in_addr **) hp->h_addr_list;

  for ( ; *pptr != NULL; pptr++) {
    memcpy(&addr->sin_addr, *pptr, sizeof(struct in_addr));
    break;
  }
   
  status = pthread_mutex_unlock(&getHostByNameMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Unlock gethostbyname Mutex");
  }
  
#endif 
#endif 
    
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
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cMsgAccept: errno = %d, err = %s\n", errno, strerror(errno));
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
fprintf(stderr,"cMsgTcpWritev gives EWOULDBLOCK\n");
        goto retry;
      }
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr,"cMsgTcpWritev(%d,,%d) = writev(%d,,%d) = %d\n",
		fd,nbufs,fd,n_write,cc);
      }
      perror("cMsgTcpWritev");
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
    if ( (nwritten = write(fd, (char*)ptr, nleft)) <= 0) {
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
      /*
      if (errno == EINTR)            fprintf(stderr, "call interrupted\n");
      else if (errno == EAGAIN)      fprintf(stderr, "non-blocking return, or socket timeout\n");
      else if (errno == EWOULDBLOCK) fprintf(stderr, "nonblocking return\n");
      else if (errno == EIO)         fprintf(stderr, "I/O error\n");
      else if (errno == EISDIR)      fprintf(stderr, "fd refers to directory\n");
      else if (errno == EBADF)       fprintf(stderr, "fd not valid or not open for reading\n");
      else if (errno == EINVAL)      fprintf(stderr, "fd not suitable for reading\n");
      else if (errno == EFAULT)      fprintf(stderr, "buffer is outside address space\n");
      else {perror("cMsgTcpRead");}
      */
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


int cMsgLocalByteOrder(int *endian)
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
      if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgByteOrder: unknown endian\n");
      return(CMSG_ERROR);
    }
  }
  else {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgByteOrder: sizeof(short) = %u\n", sizeof(short));
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
	    return("Temporary error on name server - try again later");

    if (err == NO_RECOVERY)
	    return("Unrecoverable name server error");

    if (err == NO_DATA)
    return("No address associated with name");

    return("unknown error");
}


/**
 * This routine finds out if the two given nodes are the same machine.
 * Comparison of names can be risky as there can be more than one name
 * for a domain (as in cebaf.gov & jlab.org) or more that one name
 * for any host. Do a complete comparison by comparing the binary
 * address values.
 *
 * @param node1 first node
 * @param node2 second node
 * @param same pointer filled in with 1 if same, else 0
 *
 * @returns CMSG_BAD_ARGUMENT if any arg is NULL
 * @returns CMSG_ERROR if error finding host
 */
int cMsgNodeSame(const char *node1, const char *node2, int *same)
{
#ifdef VXWORKS
  int nodeAddr1, nodeAddr2;

  if ((node1 == NULL) || (node2 == NULL) || same == NULL) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgNodeSame: bad argument(s)\n");
    return CMSG_BAD_ARGUMENT;
  }
    
  /* do a quick check of name against name, it may work */
  if (strcmp(node1, node2) == 0) {
    *same = 1;
    return CMSG_OK;
  }
  
  nodeAddr1 = hostGetByName((char *)node1);
  if (nodeAddr1 == ERROR)
    return CMSG_ERROR;

  nodeAddr2 = hostGetByName((char *)node2);
  if (nodeAddr2 == ERROR)
    return CMSG_ERROR;

  if (nodeAddr1 == nodeAddr2)
    *same = 1;
  else
    *same = 0;
  return CMSG_OK;

#else
  struct hostent *hptr;
  char **pptr;
  /* save up to maxAddresses ip addresses for each node */
  int maxAddresses = 10;
  struct in_addr   node1addrs[maxAddresses], node2addrs[maxAddresses];
  int              n1addrs=0, n2addrs=0, i, j;
  
  if ((node1 == NULL) || (node2 == NULL) || same == NULL) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgNodeSame: bad argument(s)\n");
    return CMSG_BAD_ARGUMENT;
  }
  
  /* do a quick check of name against name, it may work */
  if (strcmp(node1, node2) == 0) {
    *same = 1;
    return CMSG_OK;
  }
   
  /* Since gethostbyname uses static data and gethostbyname_r
  * is buggy on linux, do things the hard way and save
  * the results into arrays so we don't overwrite data.
  */
   
  if ( (hptr = gethostbyname(node1)) == NULL) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgNodeSame: gethostbyname error\n");
    return CMSG_ERROR;
  }
  for (pptr = hptr->h_addr_list; *pptr != NULL; pptr++) {
    node1addrs[n1addrs++] = *((struct in_addr *) *pptr);
    if (n1addrs > maxAddresses-1) break;
  }

  if ( (hptr = gethostbyname(node2)) == NULL) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgNodeSame: gethostbyname error\n");
    return CMSG_ERROR;
  }
  for (pptr = hptr->h_addr_list; *pptr != NULL; pptr++) {
    node2addrs[n2addrs++] = *((struct in_addr *) *pptr);
    if (n2addrs > maxAddresses-1) break;
  }

  /* look through addresses for a match */
  for (i=0; i < n1addrs; i++) {
    for (j=0; j < n2addrs; j++) {
      if (node1addrs[i].s_addr == node2addrs[j].s_addr) {
        *same = 1;
        return CMSG_OK;
      }
    }
  }
  
  *same = 0;
  return CMSG_OK;
#endif
}


/**
 * This routine finds out if the given host is the same as the
 * local host or not.
 *
 * @param host hostname
 * @param isLocal pointer filled in with 1 if local host, else 0
 *
 * @returns CMSG_ERROR if could not find the host
 * @returns CMSG_BAD_ARGUMENT if either argument is NULL
 */
int cMsgNodeIsLocal(const char *host, int *isLocal)
{
#ifdef VXWORKS
  int hostAddr1, hostAddr2;
  char name[CMSG_MAXHOSTNAMELEN];

  if (host == NULL || isLocal == NULL) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgNodeIsLocal: bad argument(s)\n");
    return CMSG_BAD_ARGUMENT;
  }

  hostAddr1 = hostGetByName((char *)host);
  if(hostAddr1 == ERROR)
    return CMSG_ERROR;

  if(gethostname(name,CMSG_MAXHOSTNAMELEN))
    return CMSG_ERROR;

  hostAddr2 = hostGetByName(name);
  if(hostAddr2 == ERROR)
    return CMSG_ERROR;

  if(hostAddr1 == hostAddr2)
    *isLocal = 1;
  else
    *isLocal = 0;
  return CMSG_OK;

#else
  struct utsname myname;
  int status, same=0;
  int debugTemp, debug=0;
  
  debugTemp = cMsgDebug;
  if (debug) cMsgDebug = CMSG_DEBUG_INFO;
  
  if (host == NULL || isLocal == NULL) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgNodeIsLocal: bad argument(s)\n");
    cMsgDebug = debugTemp;
    return CMSG_BAD_ARGUMENT;
  }

  /* find out the name of the machine we're on */
  if (uname(&myname) < 0) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgNodeIsLocal: cannot find hostname\n");
    cMsgDebug = debugTemp;
    return CMSG_ERROR;
  }
  
  if ( (status = cMsgNodeSame(host, myname.nodename, &same)) != CMSG_OK) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgNodeIsLocal: error in cMsgNodeSame\n");
    cMsgDebug = debugTemp;
    return status;
  }
  
  cMsgDebug = debugTemp;
  
  if (same)
    *isLocal = 1;
  else
    *isLocal = 0;
  
  return CMSG_OK;
#endif
}


/*-------------------------------------------------------------------*/
/*    Return the default fully qualified host name of this host      */
int cMsgLocalHost(char *host, int length)
{
#ifdef VXWORKS
  if (host == NULL) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgLocalHost: bad argument\n");
    return(CMSG_ERROR);
  }

  if (gethostname(host, length) < 0) return(CMSG_ERROR);
  return(CMSG_OK);

#else
  struct utsname myname;
  struct hostent *hptr;
  int status;
  
  if (host == NULL) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgLocalHost: bad argument\n");
    return(CMSG_ERROR);
  }

  /* find out the name of the machine we're on */
  if (uname(&myname) < 0) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgLocalHost: cannot find hostname\n");
    return(CMSG_ERROR);
  }
  
  /* make gethostbyname thread-safe */
  status = pthread_mutex_lock(&getHostByNameMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Lock gethostbyname Mutex");
  }   

  if ( (hptr = gethostbyname(myname.nodename)) == NULL) {
    status = pthread_mutex_unlock(&getHostByNameMutex);
    if (status != 0) {
      cmsg_err_abort(status, "Unlock gethostbyname Mutex");
    }
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgLocalHost: cannot find hostname\n");
    return(CMSG_ERROR);
  }

  /* return the null-teminated canonical name */
  strncpy(host, hptr->h_name, length);
  host[length-1] = '\0';
  
  status = pthread_mutex_unlock(&getHostByNameMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Unlock gethostbyname Mutex");
  }

  return(CMSG_OK);
#endif
}


/*-------------------------------------------------------------------*/
/*      Return the default dotted-decimal address of this host       */


int cMsgLocalAddress(char *address, int length)
{
#ifdef VXWORKS

  char name[CMSG_MAXHOSTNAMELEN];
  union {
    char ip[4];
    int  ipl;
  } u;

  if (address == NULL) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgLocalAddress: bad argument\n");
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
  int            status;
  
  if (address == NULL) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgLocalAddress: bad argument\n");
    return(CMSG_ERROR);
  }

  /* find out the name of the machine we're on */
  if (uname(&myname) < 0) {
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgLocalAddress: cannot find hostname\n");
    return(CMSG_ERROR);
  }
  
  /* make gethostbyname thread-safe */
  status = pthread_mutex_lock(&getHostByNameMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Lock gethostbyname Mutex");
  }   

  if ( (hptr = gethostbyname(myname.nodename)) == NULL) {
    status = pthread_mutex_unlock(&getHostByNameMutex);
    if (status != 0) {
      cmsg_err_abort(status, "Unlock gethostbyname Mutex");
    }
    if (cMsgDebug >= CMSG_DEBUG_ERROR) fprintf(stderr, "cMsgLocalAddress: cannot find hostname\n");
    return(CMSG_ERROR);
  }

  /* find address from hostent structure */
  pptr = hptr->h_addr_list;
  val  = inet_ntoa(*((struct in_addr *) *pptr));
  
  /* return the null-teminated dotted-decimal address */
  if (val == NULL) {
    status = pthread_mutex_unlock(&getHostByNameMutex);
    if (status != 0) {
      cmsg_err_abort(status, "Unlock gethostbyname Mutex");
    }
    return(CMSG_ERROR);
  }
  strncpy(address, val, length);
  address[length-1] = '\0';

  status = pthread_mutex_unlock(&getHostByNameMutex);
  if (status != 0) {
    cmsg_err_abort(status, "Unlock gethostbyname Mutex");
  }
  
  return(CMSG_OK);
  
#endif
}

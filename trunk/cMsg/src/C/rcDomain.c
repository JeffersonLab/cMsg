/*----------------------------------------------------------------------------*
 *                                                                            *
 *  Copyright (c) 2006        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 7-Apr-2006, Jefferson Lab                                    *
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


#include "errors.h"
#include "cMsgPrivate.h"
#include "cMsgBase.h"
#include "cMsgNetwork.h"
#include "rwlock.h"
#include "regex.h"
#include <stdlib.h>
#include <time.h>
#include <ctype.h>

#ifdef VXWORKS
#include <vxWorks.h>
#endif


/* for c++ */
#ifdef __cplusplus
extern "C" {
#endif


/**
 * Structure for arg to be passed to receiver thread.
 * Allows data to flow back and forth with receiver thread.
 */
typedef struct receiverArg_t {
    int sockfd;
    unsigned short port;
    socklen_t len;
    struct sockaddr_in clientaddr;
} receiverArg;

/**
 * Read/write lock to prevent connect or disconnect from being
 * run simultaneously with any other function.
 */
static rwLock_t connectLock  = RWL_INITIALIZER;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond   = PTHREAD_COND_INITIALIZER;

/* Local prototypes */
static void  connectReadLock(void);
static void  connectReadUnlock(void);
static void  connectWriteLock(void);
static void  connectWriteUnlock(void);
static void *receiver(void *arg);
static int   getAbsoluteTime(const struct timespec *deltaTime, struct timespec *absTime);
static int   parseUDL(const char *UDL, char **host,
                      unsigned short *port, char **UDLRemainder);


/* Prototypes of the 14 functions which implement the standard tasks in cMsg. */
static int   cmsgd_connect(const char *myUDL, const char *myName,
                           const char *myDescription,
                           const char *UDLremainder, void **domainId);
static int   cmsgd_send(void *domainId, void *msg);
static int   cmsgd_syncSend(void *domainId, void *msg, int *response);
static int   cmsgd_flush(void *domainId);
static int   cmsgd_subscribe(void *domainId, const char *subject, const char *type,
                             cMsgCallback *callback, void *userArg,
                             cMsgSubscribeConfig *config, void **handle);
static int   cmsgd_unsubscribe(void *domainId, void *handle);
static int   cmsgd_subscribeAndGet(void *domainId, const char *subject, const char *type,
                                   const struct timespec *timeout, void **replyMsg);
static int   cmsgd_sendAndGet(void *domainId, void *sendMsg,
                              const struct timespec *timeout, void **replyMsg);
static int   cmsgd_start(void *domainId);
static int   cmsgd_stop(void *domainId);
static int   cmsgd_disconnect(void *domainId);
static int   cmsgd_shutdownClients(void *domainId, const char *client, int flag);
static int   cmsgd_shutdownServers(void *domainId, const char *server, int flag);
static int   cmsgd_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler,
                                      void *userArg);

/** List of the functions which implement the standard cMsg tasks in the cMsg domain. */
static domainFunctions functions = {cmsgd_connect, cmsgd_send,
                                    cmsgd_syncSend, cmsgd_flush,
                                    cmsgd_subscribe, cmsgd_unsubscribe,
                                    cmsgd_subscribeAndGet, cmsgd_sendAndGet,
                                    cmsgd_start, cmsgd_stop, cmsgd_disconnect,
                                    cmsgd_shutdownClients, cmsgd_shutdownServers,
                                    cmsgd_setShutdownHandler};

/* CC domain type */
domainTypeInfo rcDomainTypeInfo = {
  "rc",
  &functions
};


/*-------------------------------------------------------------------*/


static int cmsgd_connect(const char *myUDL, const char *myName, const char *myDescription,
                         const char *UDLremainder, void **domainId) {
  
    unsigned short serverPort;
    char  *serverHost, *expid, buffer[1024];
    int  err, status, len, expidLen, expidLenNet, sockfd;
    
    pthread_t thread;
    receiverArg arg;
    
    struct timespec wait, time;
    struct sockaddr_in servaddr;
    int numLoops = 5;
    int gotResponse = 0;
        
       
    /* clear array */
    bzero(buffer, 1024);
    
    parseUDL(myUDL, &serverHost, &serverPort, NULL);

    /* cannot run this simultaneously with any other routine */
    connectWriteLock();  

    /*-------------------------------------------------------
     * broadcast on local subnet to find CodaComponent server
     *-------------------------------------------------------
     */
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(serverPort);
    
    /* create socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if ( sockfd < 0) {
        /* error */
    }

    /* Put EXPID (experiment id string) into byte array.
     * The host and port are automatically sent by UDP
     * to the recipient.
     */
    expid       = getenv("EXPID");
    expidLen    = strlen(expid);
    expidLenNet = htonl(expidLen);
printf("GOT EXPID = %s\n", expid);
    
    memcpy((void *)buffer, (const void *) &expidLenNet, sizeof(expidLenNet));
    len = sizeof(expidLenNet);
    memcpy((void *)(buffer+len), (const void *) expid, expidLen);
    len += expidLen;
    
    /* broadcast if no host given, otherwise unicast */     
    if (strlen(serverHost) < 1) {
        if ( inet_pton(AF_INET, "255.255.255.255",  &servaddr.sin_addr) < 1) {
          /* error */
        }
    }
    else {
        if ( inet_pton(AF_INET, serverHost,  &servaddr.sin_addr) < 1) {
          /* error */
        }
    }
    
    /* create a thread which will receive any responses to our broadcast */
    bzero(&arg.clientaddr, sizeof(arg.clientaddr));
    arg.len = sizeof(arg.clientaddr);
    arg.port = 0;
    arg.sockfd = sockfd;
    arg.clientaddr.sin_family = AF_INET;
    
    status = pthread_create(&thread, NULL, receiver, (void *)(&arg) );
    if (status != 0) {
        err_abort(status, "Creating keep alive thread");
    }
    
    /* wait for a response for 2 seconds */
    wait.tv_sec  = 2;
    wait.tv_nsec = 0;
    
    while (!gotResponse && numLoops > 0) {
        /* send UDP  packet to CC server */
        sendto(sockfd, (void *)buffer, len, 0, (SA *) &servaddr, sizeof(servaddr));

        getAbsoluteTime(&wait, &time);
        status = pthread_cond_timedwait(&cond, &mutex, &time);
        if (status == ETIMEDOUT) {
            numLoops--;
            continue;
        }
        if (status != 0) {
            err_abort(status, "pthread_cond_timedwait");
        }
        gotResponse = 1;
    }


    if (!gotResponse) {
printf("Got no response\n");
        connectWriteUnlock();
        return(CMSG_NETWORK_ERROR);
    }
    else {
printf("Got a response!\n");
printf("main connect thread: clientaddr = %p, len = %d\n", &(arg.clientaddr), arg.len);    
    }
printf("main connect thread: returned len = %d, sizeof(clientaddr) = %d\n",
       arg.len, sizeof(arg.clientaddr));    

    /*
     * Create a UDP "connection". This means all subsequent sends are to be
     * done with the "send" and not the "sendto" function. The benefit is 
     * that the udp socket does not have to connect, send, and disconnect
     * for each message sent.
     */
printf("main connect thread: sending to port = %hu\n", ntohs(arg.clientaddr.sin_port));    
    err = connect(sockfd, (SA *)&arg.clientaddr, sizeof(arg.clientaddr));
    /*err = connect(sockfd, (SA *)&arg.clientaddr, arg.len);*/
    if (err < 0) {
printf("Cannot do \"connect\" on udp socket = %d\n", sockfd);
perror("connect");
        connectWriteUnlock();
        return(CMSG_SOCKET_ERROR);
    }
    
    *domainId = (void *) sockfd;
        
    connectWriteUnlock();

    return(CMSG_OK);
}

/*-------------------------------------------------------------------*/

/**
 * This routine starts a thread to receive a return UDP packet from
 * the server due to our initial broadcast.
 */
static void *receiver(void *arg) {

    receiverArg *threadArg = (receiverArg *) arg;
    char buf[1024];
    int err;
    struct sockaddr_in clientaddr;
    socklen_t len;
    struct timespec wait;
    
    wait.tv_sec  = 0;
    wait.tv_nsec = 100000000;
    
    /*
     * Wait .1 sec for the main thread to hit the timed wait
     * before we look for a response.
     */
    nanosleep(&wait, NULL);
        
    err = recvfrom(threadArg->sockfd, (void *)buf, 1024, 0,
                  (SA *) &threadArg->clientaddr, &(threadArg->len));
    if (err < 0) {
        /* error */
    }
    
    /* Tell main thread we are done. */
    pthread_cond_signal(&cond);
    
    return NULL;
}


/*-------------------------------------------------------------------*/


static int cmsgd_send(void *domainId, void *vmsg) {  

    int err=CMSG_OK, len, lenText, fd;
    int outGoing[2];
    char buffer[2048];
    cMsgMessage *msg = (cMsgMessage *) vmsg;
    
    /* clear array */
    bzero(buffer, 2048);
        
    /* Cannot run this while connecting/disconnecting */
    connectReadLock();
    
    fd = (int) domainId;
printf("Try sending on udp socket = %d\n", fd);

    if (msg->text == NULL) {
      lenText = 0;
    }
    else {
      lenText = strlen(msg->text);
    }

    /* flag */
    outGoing[0] = htonl(1);
    /* length of "text" string */
    outGoing[1] = htonl(lenText);

    /* copy data into a single buffer */
    memcpy(buffer, (void *)outGoing, sizeof(outGoing));
    len = sizeof(outGoing);
    memcpy(buffer+len, (void *)msg->text, lenText);
    len += lenText;
    buffer[len] = '\0'; /* add null to end of string */
    len += 1;        /* total length of message */

    /* send data over (connected) socket */
    if (cMsgTcpWrite(fd, (void *) buffer, len) != len) {
    /*if (send(fd, (void *) buffer, len, 0) != len) {*/
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "cmsgd_send: write failure\n");
      }
      err = CMSG_NETWORK_ERROR;
    }
    
    /* done protecting communications */
    connectReadUnlock();
    
    return(err);

}


/*-------------------------------------------------------------------*/


static int cmsgd_disconnect(void *domainId) {
    /* cannot run this simultaneously with any other routine */
    int fd;
    connectWriteLock();  
      fd = (int) domainId;
      close(fd);
    connectWriteUnlock();
    
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int cmsgd_syncSend(void *domainId, void *vmsg, int *response) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int cmsgd_subscribeAndGet(void *domainId, const char *subject, const char *type,
                                 const struct timespec *timeout, void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int cmsgd_sendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout,
                            void **replyMsg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int cmsgd_flush(void *domainId) {  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int cmsgd_subscribe(void *domainId, const char *subject, const char *type, cMsgCallback *callback,
                           void *userArg, cMsgSubscribeConfig *config, void **handle) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int cmsgd_unsubscribe(void *domainId, void *handle) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int cmsgd_start(void *domainId) {
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


static int cmsgd_stop(void *domainId) {
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/*   shutdown handler functions                                      */
/*-------------------------------------------------------------------*/


static int cmsgd_setShutdownHandler(void *domainId, cMsgShutdownHandler *handler,
                                    void *userArg) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int cmsgd_shutdownClients(void *domainId, const char *client, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


static int cmsgd_shutdownServers(void *domainId, const char *server, int flag) {
  return(CMSG_NOT_IMPLEMENTED);
}


/*-------------------------------------------------------------------*/


/**
 * This routine parses, using regular expressions, the cMsg domain
 * portion of the UDL sent from the next level up" in the API.
 */
static int parseUDL(const char *UDL,
                    char **host,
                    unsigned short *port,
                    char **UDLRemainder) {

    int        err, len, bufLength, Port, index;
    unsigned int i;
    char       *p, *udl, *udlLowerCase, *udlRemainder, *pswd;
    char       *buffer;
    const char *pattern = "(([a-zA-Z]+[a-zA-Z0-9\\.\\-]*)|([0-9]+\\.[0-9\\.]+))?:?([0-9]+)?/?(.*)";
    regmatch_t matches[6]; /* we have 6 potential matches: 1 whole, 5 sub */
    regex_t    compiled;
    
    if (UDL == NULL) {
        return (CMSG_BAD_FORMAT);
    }
    
    /* make a copy */
    udl = (char *) strdup(UDL);
    
    /* make a copy in all lower case */
    udlLowerCase = (char *) strdup(UDL);
    for (i=0; i<strlen(udlLowerCase); i++) {
      udlLowerCase[i] = tolower(udlLowerCase[i]);
    }
  
    /* strip off the beginning cMsg:CC:// */
    p = strstr(udlLowerCase, "cc://");
    if (p == NULL) {
      free(udl);
      free(udlLowerCase);
      return(CMSG_BAD_ARGUMENT);  
    }
    index = (int) (p - udlLowerCase);
    free(udlLowerCase);
    
    udlRemainder = udl + index + 5;
/*printf("parseUDLregex: udl remainder = %s\n", udlRemainder);*/   
 
    /* make a big enough buffer to construct various strings, 256 chars minimum */
    len       = strlen(udlRemainder) + 1;
    bufLength = len < 256 ? 256 : len;    
    buffer    = (char *) malloc(bufLength);
    if (buffer == NULL) {
      free(udl);
      return(CMSG_OUT_OF_MEMORY);
    }
    
    /* cMsg domain UDL is of the form:
     *        cMsg:CC://<host>:<port>
     * 
     * Remember that for this domain:
     * 1) the first "cMsg:" is optional
     * 1) port is necessary
     * 2) host is NOT necessary and may be "localhost" and may also includes dots (.)
     */

    /* compile regular expression */
    err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED);
    if (err != 0) {
        free(udl);
        free(buffer);
        return (CMSG_ERROR);
    }
    
    /* find matches */
    err = cMsgRegexec(&compiled, udlRemainder, 6, matches, 0);
    if (err != 0) {
        /* no match */
        free(udl);
        free(buffer);
        return (CMSG_BAD_FORMAT);
    }
    
    /* free up memory */
    cMsgRegfree(&compiled);
            
    /* find host name */
    if (matches[1].rm_so > -1) {
       buffer[0] = 0;
       len = matches[1].rm_eo - matches[1].rm_so;
       strncat(buffer, udlRemainder+matches[1].rm_so, len);
                
        /* if the host is "localhost", find the actual host name */
        if (strcmp(buffer, "localhost") == 0) {
/*printf("parseUDLregex: host = localhost\n");*/
            /* get canonical local host name */
            if (cMsgLocalHost(buffer, bufLength) != CMSG_OK) {
                /* error */
                host = NULL;
            }
        }
        
        if (host != NULL) {
            *host = (char *)strdup(buffer);
        }
/*printf("parseUDLregex: host = %s\n", buffer);*/
    }


    /* find port */
    index = 4;
    if (matches[index].rm_so < 0) {
        /* no match for port so use default */
/*printf("parseUDLregex: no port found, use default\n");*/
        Port = CC_BROADCAST_PORT;
        if (cMsgDebug >= CMSG_DEBUG_WARN) {
            fprintf(stderr, "parseUDLregex: guessing that the name server port is %d\n",
                   Port);
        }
    }
    else {
/*printf("parseUDLregex: found port\n");*/
        buffer[0] = 0;
        len = matches[index].rm_eo - matches[index].rm_so;
        strncat(buffer, udlRemainder+matches[index].rm_so, len);        
        Port = atoi(buffer);        
    }

    if (Port < 1024 || Port > 65535) {
      if (host != NULL) free((void *) *host);
      free(udl);
      free(buffer);
      return (CMSG_OUT_OF_RANGE);
    }
               
    if (port != NULL) {
      *port = Port;
    }
/*printf("parseUDLregex: port = %hu\n", Port );*/

    /* find subdomain remainder */
    index++;
    buffer[0] = 0;
    if (matches[index].rm_so < 0) {
        /* no match */
        if (UDLRemainder != NULL) {
            *UDLRemainder = NULL;
        }
    }
    else {
        len = matches[index].rm_eo - matches[index].rm_so;
        strncat(buffer, udlRemainder+matches[index].rm_so, len);
                
        if (UDLRemainder != NULL) {
            *UDLRemainder = (char *) strdup(buffer);
        }        
/*printf("parseUDLregex: remainder = %s, len = %d\n", buffer, len);*/
    }


    /* UDL parsed ok */
/*printf("DONE PARSING UDL\n");*/
    free(udl);
    free(buffer);
    return(CMSG_OK);
}

/*-------------------------------------------------------------------*/


/**
 * This routine locks the read lock used to allow simultaneous
 * execution of cmsgd_Send, cmsgd_SyncSend, cmsgd_Subscribe, cmsgd_Unsubscribe,
 * cmsgd_SendAndGet, and cmsgd_SubscribeAndGet, but NOT allow simultaneous
 * execution of those routines with cmsgd_Connect or cmsgd_Disconnect.
 */
static void connectReadLock(void) {

  int status = rwl_readlock(&connectLock);
  if (status != 0) {
    err_abort(status, "Failed read lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the read lock used to allow simultaneous
 * execution of cmsgd_Send, cmsgd_SyncSend, cmsgd_Subscribe, cmsgd_Unsubscribe,
 * cmsgd_SendAndGet, and cmsgd_SubscribeAndGet, but NOT allow simultaneous
 * execution of those routines with cmsgd_Connect or cmsgd_Disconnect.
 */
static void connectReadUnlock(void) {

  int status = rwl_readunlock(&connectLock);
  if (status != 0) {
    err_abort(status, "Failed read unlock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine locks the write lock used to allow simultaneous
 * execution of cmsgd_Send, cmsgd_SyncSend, cmsgd_Subscribe, cmsgd_Unsubscribe,
 * cmsgd_SendAndGet, and cmsgd_SubscribeAndGet, but NOT allow simultaneous
 * execution of those routines with cmsgd_Connect or cmsgd_Disconnect.
 */
static void connectWriteLock(void) {

  int status = rwl_writelock(&connectLock);
  if (status != 0) {
    err_abort(status, "Failed read lock");
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine unlocks the write lock used to allow simultaneous
 * execution of cmsgd_Send, cmsgd_SyncSend, cmsgd_Subscribe, cmsgd_Unsubscribe,
 * cmsgd_SendAndGet, and cmsgd_SubscribeAndGet, but NOT allow simultaneous
 * execution of those routines with cmsgd_Connect or cmsgd_Disconnect.
 */
static void connectWriteUnlock(void) {

  int status = rwl_writeunlock(&connectLock);
  if (status != 0) {
    err_abort(status, "Failed read unlock");
  }
}


/*-------------------------------------------------------------------*/
/*   miscellaneous local functions                                   */
/*-------------------------------------------------------------------*/

/** This routine translates a delta time into an absolute time for pthread_cond_wait. */
static int getAbsoluteTime(const struct timespec *deltaTime, struct timespec *absTime) {
    struct timespec now;
    long   nsecTotal;
    
    if (absTime == NULL || deltaTime == NULL) {
      return CMSG_BAD_ARGUMENT;
    }
    
    clock_gettime(CLOCK_REALTIME, &now);
    nsecTotal = deltaTime->tv_nsec + now.tv_nsec;
    if (nsecTotal >= 1000000000L) {
      absTime->tv_nsec = nsecTotal - 1000000000L;
      absTime->tv_sec  = deltaTime->tv_sec + now.tv_sec + 1;
    }
    else {
      absTime->tv_nsec = nsecTotal;
      absTime->tv_sec  = deltaTime->tv_sec + now.tv_sec;
    }
    return CMSG_OK;
}


/*-------------------------------------------------------------------*/



#ifdef __cplusplus
}
#endif

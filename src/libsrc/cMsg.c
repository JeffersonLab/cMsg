/*----------------------------------------------------------------------------*
 *
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    E.Wolin, 15-Jul-2004, Jefferson Lab                                     *
 *                                                                            *
 *    Authors: Elliott Wolin                                                  *
 *             wolin@jlab.org                    Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-7365             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *             Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*
 *
 * Description:
 *
 *  Implements cMsg client api and dispatches to multiple domains
 *  Includes all message functions
 *
 *
 *----------------------------------------------------------------------------*/

/**
 * @mainpage
 * 
 * <H2>
 * cMsg (pronounced "see message") is a software package conceived and written
 * at Jefferson Lab by Elliott Wolin, Carl Timmer, and Vardan Gyurjyan. At one
 * level this package is an API to a message-passing system (or domain as we
 * refer to it). Users can create messages and have them handled knowing only
 * a UDL (Uniform Domain Locator) to specify a particular server (domain) to use.
 * Thus the API acts essentially as a multiplexor, diverting messages to the
 * desired domain.
 * </H2><p>
 * <H2>
 * At another level, cMsg is an implementation of a domain. Not only can cMsg
 * pass off messages to Channel Access, SmartSockets, or a number of other
 * domains, it can handle messages itself. It was designed to be extremely
 * flexible and so adding another domain is relatively easy.
 * </H2><p>
 * <H2>
 * There is a User's Guide, API documentation in the form of web pages generated
 * by javadoc and doxygen, and a Developer's Guide. Try it and let us know what
 * you think about it. If you find any bugs, we have a bug-report web page at
 * http://xdaq.jlab.org/CODA/portal/html/ .
 * </H2><p>
 */
  
 
/**
 * @file
 * This file contains the entire cMsg user API.
 *
 * <b>Introduction</b>
 *
 * The user API acts as a multiplexor. Depending on the particular UDL used to
 * connect to a specific cMsg server, the API will direct the user's library
 * calls to the appropriate cMsg implementation.
 */  
 
/* system includes */
#include <strings.h>
#include <dlfcn.h>

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>

#ifdef Darwin
#include <time.h>
#endif

/* package includes */
#include "cMsgNetwork.h"
#include "cMsgPrivate.h"
#include "cMsg.h"
#include "cMsgRegex.h"


/** Number of local array elements in connectionPointers array. */
#define LOCAL_ARRAY_SIZE 200

/* local variables */

/* Local array (one element for each connect) holding pointers to allocated memory
 * containing connection information.
 * Assume client does no more than LOCAL_ARRAY_SIZE concurrent connects.
 * Usage protected by generalMutex. */
static void* connectPointers[LOCAL_ARRAY_SIZE];
/* Counter to help orderly use of connectPointers array. */
static int connectPtrsCounter = 0;

/** Is the one-time initialization done? */
static int oneTimeInitialized = 0;
/** Pthread mutex serializing calls to initialize and free memory. */
static pthread_mutex_t generalMutex = PTHREAD_MUTEX_INITIALIZER;
/** Store references to different domains and their cMsg implementations. */
static domainTypeInfo dTypeInfo[CMSG_MAX_DOMAIN_TYPES];
/** Excluded characters from subject, type, and description strings. */
static const char *excludedChars = "`\'\"";


/** Global debug level. */
int cMsgDebug = CMSG_DEBUG_ERROR;

/** For domain implementations. */
extern domainTypeInfo  cmsgDomainTypeInfo;
extern domainTypeInfo  fileDomainTypeInfo;
extern domainTypeInfo    rcDomainTypeInfo;
extern domainTypeInfo   emuDomainTypeInfo;
/*extern domainTypeInfo dummyDomainTypeInfo;*/

/**
 * This structure contains the components of a given UDL broken down
 * into its consituent parts.
 */
typedef struct parsedUDL_t {
    char *udl;                /**< whole UDL for name server. */
    char *domain;             /**< domain name. */
    char *remainder;          /**< domain specific part of the UDL. */
    struct parsedUDL_t *next; /**< next element in linked list. */
} parsedUDL;


/* local prototypes */
static cMsgDomain* prepareToCallFunc(int index);
static void        cleanupAfterFunc(int index);

static int   readConfigFile(char *fileName, char **newUDL);
static int   splitUDL(const char *myUDL, parsedUDL** list, int *count);
static int   expandConfigFileUDLs(parsedUDL **list, int *count);
static int   isSameDomain(parsedUDL *list);
static int   reconstructUDL(parsedUDL *pList, char **UDL);
static int   processUDL(const char *myUDL, char **newUDL, char **domainType,
                        char **remainderFirstUDL);

static int   checkString(const char *s);
static int   registerPermanentDomains();
static int   registerDynamicDomains(char *domainType);
static void  domainInit(cMsgDomain *domain);
static void  domainFree(cMsgDomain *domain);
static int   parseUDL(const char *UDL, char **domainType, char **UDLremainder);
static void  generalMutexLock(void);
static void  generalMutexUnlock(void);
static void  initMessage(cMsgMessage_t *msg);
static int   freeMessage(void *vmsg);
static int   freeMessage_r(void *vmsg);
static int   messageStringSize(const void *vmsg, int margin, int binary, int compactPayload, int onlyPayload);
static int   cMsgToStringImpl(const void *vmsg, char **string,
                              int level, int margin, int binary,
                              int compact, int compactPayload,
                              int hasName, int noSystemFields,
                              const char *itemName);
                                                          
static int   cMsgPayloadToStringImpl(const void *vmsg, char **string, int level, int margin,
                                     int binary, int compactPayload, int noSystemFields);


/**
 * Routine to trim white space from front and back of given string.
 * Changes made to argument string in place.
 *
 * @param s string to be trimmed
 */
void cMsgTrim(char *s) {
    int i, len, frontCount=0;
    char *firstChar, *lastChar;
    
    if (s == NULL) return;

    len = (int)strlen(s);

    if (len < 1) return;
    
    firstChar = s;
    lastChar  = s + len - 1;

    /* find first front end nonwhite char */
    while (isspace(*firstChar) && *firstChar != '\0') {
        firstChar++;
    }
    frontCount = (int) (firstChar - s);

    /* Check to see if string all white space, if so send back blank string. */
    if (frontCount >= len) {
        s[0] = '\0';
        return;
    }

    /* find first back end nonwhite char */
    while (isspace(*lastChar)) {
        lastChar--;
    }

    /* number of nonwhite chars */
    len = (int) (lastChar - firstChar + 1);

    /* move chars to front of string */
    for (i=0; i < len; i++) {
        s[i] = s[frontCount + i];
    }
    /* add null at end */
    s[len] = '\0';
}

/**
 * Routine to trim a given character from front and back of given string.
 * Changes made to argument string in place.
 *
 * @param s string to be trimmed
 * @param trimChar character to be trimmed
 */
void cMsgTrimChar(char *s, char trimChar) {
    int i, len, frontCount=0;
    char *firstChar, *lastChar;
    
    if (s == NULL) return;

    len = strlen(s);

    if (len < 1) return;
    
    firstChar = s;
    lastChar  = s + len - 1;

    /* find first front end non trimChar */
    while (*firstChar == trimChar && *firstChar != '\0') {
        firstChar++;
    }
    frontCount = firstChar - s;

    /* Check to see if string all trim char, if so send back blank string. */
    if (frontCount >= len) {
        s[0] = '\0';
        return;
    }

    /* find first back end non trimChar */
    while (*lastChar == trimChar) {
        lastChar--;
    }

    /* number of non trimChars */
    len = lastChar - firstChar + 1;

    /* move chars to front of string */
    for (i=0; i < len; i++) {
        s[i] = s[frontCount + i];
    }
    /* add null at end */
    s[len] = '\0';
}

/**
 * Routine to eliminate contiguous occurrences of a given character in given string.
 * Changes made to argument string in place.
 *
 * @param s string to be trimmed
 * @param trimChar character to be trimmed
 */
void cMsgTrimDoubleChars(char *s, char trimChar) {
    char *pc, *nextChar = s + 1;
    
    if (s == NULL || strlen(s) < 2) return;

    /* while we haven't hit the end of the string ... */
    while (*s != '\0') {
        /* while 2 identical chars side-by-side, shift everything down */
        while (*s == trimChar && *nextChar == trimChar) {
            /* move chars one spot to left */
            pc = nextChar;
            while (*pc != '\0') {
                *pc = *(pc + 1);
                 pc++;
            }
        }
        
        nextChar = ++s + 1;
    }

}


#if !defined linux || !defined _GNU_SOURCE
char *strndup(const char *s1, size_t count) {
    size_t len;
    char *s;
    if (s1 == NULL) return NULL;

    len = strlen(s1) > count ? count : strlen(s1);
    if ((s = (char *) malloc(len+1)) == NULL) return NULL;
    s[len] = '\0';
    return strncpy(s, s1, len);
}
#endif


/*-------------------------------------------------------------------*
 * Mutex functions
 *-------------------------------------------------------------------*/


/** This routine locks the mutex used to initialize and free memory. */
static void generalMutexLock(void) {
    int status = pthread_mutex_lock(&generalMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed mutex lock");
    }
}


/** This routine unlocks the mutex used to initialize and free memory. */
static void generalMutexUnlock(void) {
    int status = pthread_mutex_unlock(&generalMutex);
    if (status != 0) {
        cmsg_err_abort(status, "Failed mutex unlock");
    }
}


/*-------------------------------------------------------------------*
 * Misc functions
 *-------------------------------------------------------------------*/


/** This routine does bookkeeping before calling domain-level functions. */
static cMsgDomain* prepareToCallFunc(int index) {
    cMsgDomain *domain;

    generalMutexLock();
    domain = connectPointers[index];
    if (domain == NULL)
/*printf("prepareToCallFunc: grabbed 1st mutex, index = %d, domain = %p\n",index, domain);*/
    /* if bad index or disconnect has already been run, bail out */
    if (domain == NULL || domain->disconnectCalled) {
        generalMutexUnlock();
        return(NULL);
    }
    domain->functionsRunning++;
    generalMutexUnlock();
    return(domain);
}


/** This routine does bookkeeping after calling domain-level functions. */
static void cleanupAfterFunc(int index) {
    cMsgDomain *domain;

    generalMutexLock();
    domain = connectPointers[index];
    /* domain should not have been freed since we incremented functionsRunning
     * before calling this function in prepareToCallFunc.*/
    domain->functionsRunning--;
    /* free memory if disconnect was called and no more functions using domain */
    if (domain->disconnectCalled && domain->functionsRunning < 1) {
/*printf("cleanupAfterFunc: freeing memory: index = %d, domain = %p\n", index, domain);*/
        domainFree(domain);
        free(domain);
        connectPointers[index] = NULL;
    }
    generalMutexUnlock();
}


/*-------------------------------------------------------------------*/
/**
 * Routine to free a single element of structure parsedUDL.
 * @param p pointer to element to be freed
 */
static void freeElement(parsedUDL *p) {
    if (p->udl       != NULL) {free(p->udl);       p->udl = NULL;}
    if (p->domain    != NULL) {free(p->domain);    p->domain = NULL;}
    if (p->remainder != NULL) {free(p->remainder); p->remainder = NULL;}
}

/**
 * Routine to free a linked list of elements of structure parsedUDL.
 * @param p pointer to head of list to be freed
 */
static void freeList(parsedUDL *p) {
    parsedUDL *pPrev=NULL;
    
    while (p != NULL) {
        freeElement(p);
        pPrev = p;
        p = p->next;
        free(pPrev);
    }
}


/*-------------------------------------------------------------------*/


/**
 * Routine to read a configuration file and return the cMsg UDL stored there.
 * @param fileName name of file to be read
 * @param newUDL pointer to char pointer that gets filled with the UDL
 *               contained in config file, NULL if none
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if not successful reading file
 * @returns CMSG_BAD_ARGUMENT if one of the arguments is bad
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 */
static int readConfigFile(char *fileName, char **newUDL) {
#define MAX_STR_LEN 2000
    int   gotUDL=0;
    FILE *fp;
    char  str[MAX_STR_LEN];
    
    if (fileName == NULL) return(CMSG_BAD_ARGUMENT);
    
    if ( (fp = fopen(fileName,"r")) == NULL) {
        return(CMSG_ERROR);
    }
    
    /* skip over lines with no UDL (no ://) */
    while (fgets(str, MAX_STR_LEN, fp) != NULL) {
    
/*printf("readConfigFile: string = %s\n", str);*/
      /* Remove white space at beginning and end. */
      cMsgTrim(str);
      
      /* if first char is #, treat as comment */
      if (str[0] == '#') {
/*printf("SKIP over comment\n");*/
        continue;
      }
      /* length of 5 is shortest possible UDL (a://b) */
      else if (strlen(str) < 5) {
        continue;
      }
      else {
/*printf("read configFile, UDL = %s\n", str);*/
        /* this string is not a UDL so continue on */
        if (strstr(str, "://") == NULL) {
            continue;
        }
        if (newUDL != NULL) *newUDL = strdup(str);
        gotUDL = 1;
        break;
      }
    }
    
    fclose(fp);

    if (!gotUDL) {
      return(CMSG_ERROR);
    }
    return(CMSG_OK);
}


/**
 * Routine to split a string of semicolon separated UDLs into a linked list
 * of parsedUDL structures. Each structure contains easily accessible info
 * about the UDL it represents.
 *
 * @param myUDL UDL to be split
 * @param list pointer to parsedUDL pointer that gets filled with the head of
 *             the linked list
 * @param count pointer to int which gets filled with number of items in list
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_FORMAT if one of the UDLs is not in the correct format
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 */
static int splitUDL(const char *myUDL, parsedUDL** list, int *count) {

  char *p, *udl, *domain, *remainder;
  int udlCount=0, err, gotFirst=0; 
  parsedUDL *pUDL, *prevUDL=NULL, *firstUDL=NULL;

  /*
   * The UDL may be a semicolon separated list of UDLs.
   * Separate them and return a linked list of them.
   */
  udl = strdup(myUDL);
  if (udl == NULL) return(CMSG_OUT_OF_MEMORY);       
  p = strtok(udl, ";");
  
  while (p != NULL) {
    /* Parse the UDL (Uniform Domain Locator) */
    cMsgTrim(p); /* get rid of any leading/trailing whitespace */
    if ( (err = parseUDL(p, &domain, &remainder)) != CMSG_OK ) {
      /* There's been a parsing error */
      free(udl);
      return(CMSG_BAD_FORMAT);
    }
    
    pUDL = (parsedUDL *)calloc(1, sizeof(parsedUDL));
    pUDL->udl = strdup(p);
    pUDL->domain = domain;
    pUDL->remainder = remainder;
    
    /* linked list */
    if (prevUDL != NULL) prevUDL->next = pUDL;
    prevUDL = pUDL;
    
    /* bookkeeping */
    if (!gotFirst) {
        firstUDL = pUDL;
        gotFirst = 1;
    }
    udlCount++;
    p = strtok(NULL, ";");
  }
  
  free(udl);
  
  if (count != NULL) *count = udlCount;
  
  if (list != NULL) {
    *list = firstUDL;
  }
  else if (firstUDL != NULL) {
    freeList(firstUDL);
  }

  return(CMSG_OK);
}


/**
 * Routine to check a linked list of parsedUDL structures to see if all the
 * elements' domains are the same.
 *
 * @param list pointer to head of parsedUDL linked list
 *
 * @returns CMSG_OK if successful or list is NULL
 * @returns CMSG_BAD_ARGUMENT if domain arg is NULL
 * @returns CMSG_WRONG_DOMAIN_TYPE one of the domains is of the wrong type
 */
static int isSameDomain(parsedUDL *list) {
    parsedUDL *pUDL;

    if (list == NULL) return(CMSG_OK);

    /* first make sure all domains are the same as the first one */
    pUDL = list;
    while (pUDL != NULL) {
        if (strcasecmp(pUDL->domain, list->domain) != 0) {
            return(CMSG_WRONG_DOMAIN_TYPE);
        }
        pUDL = pUDL->next;
    }
    
    return(CMSG_OK);
}


/**
 * Routine to remove duplicate entries in a linked list of parsedUDL structures.
 *
 * @param list pointer to head of parsedUDL linked list
 */
static void removeDuplicateUDLs(parsedUDL *list) {
  
  int index1=0, index2=0, itemRemoved=0;
  parsedUDL *pUDL, *pUDL2, *pPrev=NULL;

  if (list == NULL) return;
    
  /* eliminate duplicates from the linked list */
  pUDL = list;
  while (pUDL != NULL) {
    pUDL2  = list;
    /* start comparing with the next element on the list */
    index2 = index1 + 1;
    while (pUDL2 != NULL) {
      if (index2-- > 0) {
        pPrev = pUDL2;
        pUDL2 = pUDL2->next;
        continue;
      }
      /* if the part of the UDL after cMsg:<domain>:// is identical, remove 2nd occurrance */
      if (strcmp(pUDL->remainder, pUDL2->remainder) == 0) {
         /* remove from list */
         pPrev->next = pUDL2->next;
         pUDL2 = pPrev;
         itemRemoved++;
      }
    
      pPrev = pUDL2;
      pUDL2 = pUDL2->next;    
    }
    
    pUDL = pUDL->next;
    index1++;
  }
  
  if (itemRemoved) {
    if (cMsgDebug >= CMSG_DEBUG_WARN) {
      fprintf(stderr, "cleanUpUDLs: duplicate UDL removed from list\n");
    }
  }
  
  return;
}


/*-------------------------------------------------------------------*/


/**
 * Routine to expand all the UDLs in the given list in the configFile domain.
 * Each config file UDL in the original list has its file read to obtain its
 * UDL which is then substituted for the original UDL.
 *
 * @param list pointer to the linked list
 * @param count pointer to int which gets filled with number of items in list
 *
 * @returns CMSG_OK if successful or list is NULL
 * @returns CMSG_ERROR if not successful reading file
 * @returns CMSG_BAD_FORMAT if file contains UDL of configFile domain or
 *                          if one of the UDLs is not in the correct format
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 */
static int expandConfigFileUDLs(parsedUDL **list, int *count) {
  
  int i, err, len=0, size;
  parsedUDL *pUDL, *newList, *pLast, *pTmp, *pPrev=NULL, *pFirst;
  char  *newUDL, *udlLowerCase;

  if (list == NULL) return(CMSG_OK);
    
  pFirst = pUDL = *list;
  if (pUDL == NULL) return(CMSG_OK);
  
  while (pUDL != NULL) {
    
    /* if not configFile domain, skip to next */
    if (strcasecmp(pUDL->domain, "configFile") != 0) {
/* printf("expandConfigFileUDLs: in %s domain\n", pUDL->domain); */
        pPrev = pUDL;
        pUDL  = pUDL->next;
        len++;
        continue;
    }
/* printf("expandConfigFileUDLs: in configFile domain\n"); */

    /* Do something special if the domain is configFile.
     * Read the file and use that as the real UDL.  */

    /* read file (remainder of UDL) */
    if ( (err = readConfigFile(pUDL->remainder, &newUDL)) != CMSG_OK ) {
      return(err);      
    }
/* printf("expandConfigFileUDLs: read file, udl = %s\n", newUDL); */

    /* make a copy in all lower case */
    udlLowerCase = strdup(newUDL);
    len = (int)strlen(udlLowerCase);
    for (i=0; i<len; i++) {
      udlLowerCase[i] = (char)tolower(udlLowerCase[i]);
    }

    if (strstr(udlLowerCase, "configfile") != NULL) {
      free(newUDL);
      free(udlLowerCase);
      if (cMsgDebug >= CMSG_DEBUG_ERROR) {
        fprintf(stderr, "expandConfigFileUDLs: one configFile domain UDL may NOT reference another\n");
      }
      return(CMSG_BAD_FORMAT);
    }

    free(udlLowerCase);

    /* The UDL may be a semicolon separated list, so put elements into a linked list */
    if ( (err = splitUDL(newUDL, &newList, &size)) != CMSG_OK) {
        free(newUDL);
        return(err);
    }
    len += size;
    free(newUDL);

    /* find the end of the file's list */
    pLast = newList;
    while (pLast->next != NULL) {
      pLast = pLast->next;
    }
     
    /**********************************************************/
    /* Replace the UDL that was expanded in the original list */
    /**********************************************************/
   
    /* This item will be analyzed next */
    pTmp = pUDL->next;
    
    /* If this is not the first item in the original list, have the previous item point
     * to the new list (eliminating the current item which is being replaced) */
    if (pPrev != NULL) {
        pPrev->next = newList;
    }
    /* If the first item in the original list is being replaced, record that */
    else {
        pFirst = newList;
    }
    /*pUDL->next  = newList;*/
    pLast->next = pTmp;
    
    /* move on to next item in original list */
    pUDL = pTmp;
    
  }
  
  if (list  != NULL) *list  = pFirst;
  if (count != NULL) *count = len;
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * Routine to create a single, semicolon-separated UDL from the given linked
 * list of parsedUDL structures.
 *
 * @param pList pointer to the head of the linked list
 * @param UDL pointer to char pointer which gets filled with the resultant list
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if pList arg is NULL
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 */
static int reconstructUDL(parsedUDL *pList, char **UDL) {
  size_t  prefixLen, totalLen=0;
  char *udl, *prefix;
  parsedUDL *pUDL;

  if (pList == NULL) return (CMSG_BAD_ARGUMENT);
  
 /* Reconstruct the UDL for passing on to the proper domain (if necessary).
   * Do that by first scanning thru the list to get the length of string
   * we'll be dealing with. Then scan again to construct the string. */
  prefixLen = 8 + strlen(pList->domain); /* length of cMsg:<domainType>:// */
  prefix = (char *) calloc(1, prefixLen+1);
  if (prefix == NULL) {
    return(CMSG_OUT_OF_MEMORY);
  }
  sprintf(prefix, "%s%s%s", "cMsg:", pList->domain, "://");
  
  pUDL = pList;
  while (pUDL != NULL) {
    totalLen += prefixLen + strlen(pUDL->remainder) + 1; /* +1 is for semicolon */
    pUDL = pUDL->next;
  }
  totalLen--; /* don't need last semicolon */
  
  udl = (char *) calloc(1,totalLen+1);
  if (udl == NULL) {
    free(prefix);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* String all UDLs together */
  pUDL = pList;
  while (pUDL != NULL) {
    strcat(udl, prefix);
    strcat(udl, pUDL->remainder);
    if (pUDL->next != NULL) strcat(udl, ";");
    pUDL = pUDL->next;
  }
  
  free(prefix);
  
  if (UDL != NULL) {
    *UDL = udl;
  }
  else {
    free(udl);
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine takes a UDL (which may be a semicolon-separated list
 * of UDLs) and remakes it into a new UDL that has configFile domain
 * UDLs expanded, duplicated UDLs removed, and ensures all UDLs in the
 * new list are in the same domain. The returned strings have had their
 * memory allocated in this routine and thus must be freed by the caller.
 *
 * @param myUDL the Universal Domain Locator used to uniquely identify the cMsg
 *              server to connect to
 * @param newUDL pointer to char pointer which gets filled with the new UDL (mem allocated)
 * @param domainTYpe pointer to char pointer which gets filled with the domain (mem allocated)
 * @param remainderFirstUDL pointer to char pointer which gets filled with the
 *                          remainder (beginning cMsg:&lt;domain&gt;:// stripped off)
 *                          of the first UDL in the new list (mem allocated)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if not successful reading file for configFile domain UDL
 * @returns CMSG_BAD_FORMAT if configFile domain file contains UDL of configFile
 *                          domain or if one of the UDLs is not in the correct format
 * @returns CMSG_BAD_ARGUMENT if myUDL or remainderFirstUDL are NULL or
 *                            myUDL arg has illegal characters
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 * @returns CMSG_WRONG_DOMAIN_TYPE one of the domains is of the wrong type
 */
static int processUDL(const char *myUDL, char **newUDL, char **domainType,
                      char **remainderFirstUDL) {  
    int err, listSize=0;
    parsedUDL *pList;

    /* check args */
    if (newUDL == NULL || checkString(myUDL) != CMSG_OK) {
        return(CMSG_BAD_ARGUMENT);
    }

    /* The UDL may be a semicolon separated list, so put elements into a linked list */
    if ( (err = splitUDL(myUDL, &pList, NULL)) != CMSG_OK) {
        return(err);
    }
  
   /* Expand any elements referring to a configFile UDL.
    * The expansion is only done at one level, i.e. no
    * files referring to other files. */
    if ( (err = expandConfigFileUDLs(&pList, &listSize)) != CMSG_OK) {
        freeList(pList);
        return(err);
    }

    if (listSize > 1) {
        /* Make sure that all UDLs in the list are of the same domain */
        if ( (err = isSameDomain(pList)) != CMSG_OK) {
            freeList(pList);
            return(err);
        }

        /* Remove duplicate UDLs from list. A warning is printed to stderr 
         * if the debug was set and any UDLs were removed. */
        removeDuplicateUDLs(pList);
    }
  
    /* Take the linked list and turn it into a semicolon separated string */
    if ( (err = reconstructUDL(pList, newUDL)) != CMSG_OK) {
        freeList(pList);
        return(err);
    }
    /*printf("processUDL: reconstructed udl = %s\n", *newUDL);*/

    /* return values */
    if (domainType != NULL) *domainType = strdup(pList->domain);
    if (remainderFirstUDL != NULL) *remainderFirstUDL = strdup(pList->remainder);
  
    freeList(pList);

    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine resets the UDL (may be a semicolon separated list of single UDLs).
 * If a reconnect is done, the new UDLs will be used in the connection(s).
 *
 * @param domainId domain connection id
 * @param UDL new UDL
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if not successful reading file for configFile domain UDL
 * @returns CMSG_BAD_FORMAT if configFile domain file contains UDL of configFile
 *                          domain or if one of the UDLs is not in the correct format
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called or
 *                            UDL arg has illegal characters or is NULL
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 * @returns CMSG_WRONG_DOMAIN_TYPE one of the domains is of the wrong type
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSetUDL
 */
int cMsgSetUDL(void *domainId, const char *UDL) {
    int err;
    intptr_t    index; /* int the size of a ptr */
    cMsgDomain *domain;
    char  *domainType=NULL, *newUDL=NULL, *remainder=NULL;
    
    /* check args */
    if (UDL == NULL)  {
        return(CMSG_BAD_ARGUMENT);
    }

    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
    

    /* chew on the UDL, transform it */
    if ((err = processUDL(UDL, &newUDL, &domainType, &remainder)) != CMSG_OK ) {
        return(err);
    }

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    if (strcasecmp(domainType, domain->type) != 0) {
        free(newUDL); free(remainder); free(domainType);
        cleanupAfterFunc((int)index);
        return(CMSG_WRONG_DOMAIN_TYPE);
    }
    err = domain->functions->setUDL(domain->implId, newUDL, remainder);
    cleanupAfterFunc((int)index);
    
    return(err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine gets the UDL current used in the existing connection.
 *
 * @param domainId domain connection id
 * @param udl pointer filled in with current UDL or NULL if no connection
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called or
 *                            udl arg is NULL
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgGetConnectState
 */
int cMsgGetCurrentUDL(void *domainId, const char **udl) {
    int err;
    intptr_t    index; /* int the size of a ptr */
    cMsgDomain *domain;
    
    /* check args */
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
    
    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    /* dispatch to function registered for this domain type */
    err = domain->functions->getCurrentUDL(domain->implId, udl);
    cleanupAfterFunc((int)index);
    
    return(err);
}


/*-------------------------------------------------------------------*/


/**
* This routine gets the IP address (in dotted-decimal form) that the
* client used to make the network connection to itsserver.
* Do NOT write into or free the returned char pointer.
*
* @param domainId id of the domain connection
* @param ipAddress pointer filled in with server IP address
*
* @returns CMSG_OK if successful
* @returns CMSG_NOT_IMPLEMENTED if not implemented in the given domain
* @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
*/
int cMsgGetServerHost(void *domainId, const char **ipAddress) {
    int err;
    intptr_t    index; /* int the size of a ptr */
    cMsgDomain *domain;
    
    /* check args */
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
    
    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    /* dispatch to function registered for this domain type */
    err = domain->functions->getServerHost(domain->implId, ipAddress);
    cleanupAfterFunc((int)index);
    
    return(err);
}


/*-------------------------------------------------------------------*/


/**
* This routine gets the port that the client used to make the
* network connection to its server.
*
* @param domainId id of the domain connection
* @param port pointer filled in with server TCP port
*
* @returns CMSG_OK if successful
* @returns CMSG_NOT_IMPLEMENTED if not implemented in the given domain
* @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
*/
int cMsgGetServerPort(void *domainId, int *port) {
    int err;
    intptr_t    index; /* int the size of a ptr */
    cMsgDomain *domain;
    
    /* check args */
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
    
    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    /* dispatch to function registered for this domain type */
    err = domain->functions->getServerPort(domain->implId, port);
    cleanupAfterFunc((int)index);
    
    return(err);
}


/*-------------------------------------------------------------------*/


/**
* This routine does general I/O and returns a string for each string argument.
*
* @param domainId id of the domain connection
* @param command command whose value determines what is returned in string arg
* @param string  pointer which gets filled in with a return string (may be NULL)
*
* @returns CMSG_OK if successful
* @returns CMSG_NOT_IMPLEMENTED this routine is not implemented
*/
int cMsgGetInfo(void *domainId, const char *command, char **string) {
    int err;
    intptr_t    index;
    cMsgDomain *domain;
    
    /* check args */
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
    
    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    /* dispatch to function registered for this domain type */
    err = domain->functions->getInfo(domain->implId, command, string);
    cleanupAfterFunc((int)index);
    
    return(err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine is called once to connect to a domain.
 * The argument "myUDL" is the Universal Domain Locator used to uniquely
 * identify the cMsg server to connect to. It has the form:<p>
 *       <b><i>cMsg:domainType://domainInfo </i></b><p>
 * The argument "myName" is the client's name and may be required to be
 * unique within the domain depending on the domain.
 * The argument "myDescription" is an arbitrary string used to describe the
 * client.
 * If successful, this routine fills the argument "domainId", which identifies
 * the connection uniquely and is required as an argument by many other routines.
 * 
 * @param myUDL the Universal Domain Locator used to uniquely identify the cMsg
 *        server to connect to
 * @param myName name of this client
 * @param myDescription description of this client
 * @param domainId pointer to pointer which gets filled with a unique id referring
 *        to this connection.
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if regular expression compilation fails during UDL parsing
 *                     or circular UDL references
 * @returns CMSG_BAD_ARGUMENT if one of the arguments is bad
 * @returns CMSG_BAD_FORMAT if the UDL is formatted incorrectly
 * @returns CMSG_OUT_OF_MEMORY if out of memory (allocated or static)
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgConnect
 */   
int cMsgConnect(const char *myUDL, const char *myName,
                const char *myDescription, void **domainId) {

  intptr_t    index; /* int the size of a ptr (for casting) */
  int         i, err, len;
  char       *domainType=NULL, *newUDL=NULL, *remainder=NULL;
  cMsgDomain *domain;

  /* check args */
  if ( (checkString(myName)        != CMSG_OK) ||
       (checkString(myDescription) != CMSG_OK) ||
       (domainId                   == NULL   ))  {
      return(CMSG_BAD_ARGUMENT);
  }
  
  /* check for colon in name */
  len = (int)strlen(myName);
  for (i=0; i<len; i++) {
      if (myName[i] == ':') return(CMSG_BAD_ARGUMENT);
  }

  /* First, grab mutex for thread safety. */
  generalMutexLock();

  /* do one time initialization */
  if (!oneTimeInitialized) {

    /* clear array */
    for (i=0; i<CMSG_MAX_DOMAIN_TYPES; i++) {
        dTypeInfo[i].type = NULL;
    }

    /* register domain types */
    if ( (err = registerPermanentDomains()) != CMSG_OK ) {
        /* if we can't find the domain lib, or run out of memory, return error */
        generalMutexUnlock();
        return(err);
    }
    
    for (i=0; i<LOCAL_ARRAY_SIZE; i++) {
        connectPointers[i] = NULL;
    }

    oneTimeInitialized = 1;
  }

  /* look for space in static array to keep connection ptr */
  index = -1;
  if (connectPtrsCounter >= LOCAL_ARRAY_SIZE) {
      connectPtrsCounter = 0;
  }
  
  tryagain:
  for (i=connectPtrsCounter; i<LOCAL_ARRAY_SIZE; i++) {
      /* if this index is unused ... */
      if (connectPointers[i] == NULL) {
          connectPtrsCounter++;
          index = i;
          break;
      }
  }

  /* there may be available slots we missed */
  if (index < 0 && connectPtrsCounter > 0) {
      connectPtrsCounter = 0;
      goto tryagain;
  }

  generalMutexUnlock();

  
  /* if no slots available .. */
  if (index < 0) {
      return(CMSG_OUT_OF_MEMORY);
  }
  
      
  /* chew on the UDL, transform it, and extract info from it */
  if ((err = processUDL(myUDL, &newUDL, &domainType, &remainder)) != CMSG_OK ) {
      return(err);
  }
  
  /* register dynamic domain types */
  if ( (err = registerDynamicDomains(domainType)) != CMSG_OK ) {
    /* if we can't find the domain lib, or run out of memory, return error */
      free(newUDL); free(remainder); free(domainType);
      return(err);
  }
  

  /* allocate struct to hold connection info */
  domain = (cMsgDomain *) calloc(1, sizeof(cMsgDomain));
  if (domain == NULL) {
      free(newUDL); free(remainder); free(domainType);
      return(CMSG_OUT_OF_MEMORY);
  }
  domainInit(domain);  


  /* store stuff */
  domain->name         = strdup(myName);
  domain->udl          = newUDL;
  domain->description  = strdup(myDescription);
  domain->type         = domainType;
  domain->UDLremainder = remainder;

  connectPointers[index] = (void *)domain;
  
  /* if such a domain type exists, store pointer to functions */
  domain->functions = NULL;
  for (i=0; i<CMSG_MAX_DOMAIN_TYPES; i++) {
    if (dTypeInfo[i].type != NULL) {
      if (strcasecmp(dTypeInfo[i].type, domain->type) == 0) {
        domain->functions = dTypeInfo[i].functions;
        break;
      }
    }
  }
  if (domain->functions == NULL) return(CMSG_BAD_DOMAIN_TYPE);
  

  /* dispatch to connect function registered for this domain type */
  err = domain->functions->connect(newUDL, myName, myDescription,
                                  remainder, &domain->implId);
  if (err != CMSG_OK) {
      connectPointers[index] = NULL;
      domainFree(domain);
      free(domain);
      return err;
  }  
/*  printf("cMsgConnect: domainId = %p, domain = %p\n", domainId, domain);
    printf("           : &domain->implId = %p, domain>implId = %p\n", &domain->implId, domain->implId);
*/

  *domainId = (void *)index;
  
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/


/**
 * This routine tries to reconnect to the server if a connection is broken.
 * The domainId argument is created by first calling cMsgConnect()
 * and establishing a connection to a cMsg server
 *
 * @param domainId domain connection id
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if domainId is bad or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgReconnect
 */
int cMsgReconnect(void *domainId) {
    int err;
    intptr_t    index; /* int the size of a ptr */
    cMsgDomain *domain;

    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    /* dispatch to function registered for this domain type */
    err = domain->functions->reconnect(domain->implId);
    cleanupAfterFunc((int)index);

    return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine disconnects the client from the cMsg server.
 * May only call this once if it succeeds since it frees memory.
 *
 * @param domainId address of domain connection id
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if this routine already called for the given domainId
 * @returns CMSG_BAD_ARGUMENT if the domainId or the pointer it refers to is NULL,
 *                            or cMsgDisconnect() already called
 * @returns CMSG_LOST_CONNECTION if no longer connected to domain
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgDisconnect
 */   
int cMsgDisconnect(void **domainId) {
    int err;
    intptr_t    index; /* int the size of a ptr */
    cMsgDomain *domain;

    if (domainId == NULL) return(CMSG_BAD_ARGUMENT);
    index = (intptr_t) *domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    generalMutexLock();
    domain = connectPointers[index];
    if (domain == NULL || domain->disconnectCalled) {
        return(CMSG_BAD_ARGUMENT);
        generalMutexUnlock();
    }
    domain->disconnectCalled = 1;
    domain->functionsRunning++;
/*printf("cMsgDisconnect: grabbed mutex, index = %d, domain = %p, functionsRunning = %d\n",
           index, domain, domain->functionsRunning);
*/
    generalMutexUnlock();

    if ( (err = domain->functions->disconnect(&domain->implId)) != CMSG_OK ) {
        generalMutexLock();
        domain->disconnectCalled = 0;
        generalMutexUnlock();
        return err;
    }

    cleanupAfterFunc((int)index);

    /* make this id unusable from now on (copied id's will not be affected) */
    *domainId = (void *)(-1);

    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine sends a msg to the specified domain server. It is completely
 * asynchronous and never blocks. The domain may require cMsgFlush() to be
 * called to force delivery.
 * The domainId argument is created by calling cMsgConnect()
 * and establishing a connection to a cMsg server. The message to be sent
 * may be created by calling cMsgCreateMessage(),
 * cMsgCreateNewMessage(), or cMsgCopyMessage().
 *
 * @param domainId domain connection id
 * @param msg pointer to a message structure
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSend
 */
int cMsgSend(void *domainId, void *msg) {
    int err;
    intptr_t    index; /* int the size of a ptr */
    cMsgDomain *domain;
    
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    /* dispatch to function registered for this domain type */
    err = domain->functions->send(domain->implId, msg);
    cleanupAfterFunc((int)index);
    
    return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine sends a msg to the specified domain server and receives a response.
 * It is a synchronous routine and as a result blocks until it receives a status
 * integer from the cMsg server.
 * The domainId argument is created by calling cMsgConnect()
 * and establishing a connection to a cMsg server. The message to be sent
 * may be created by calling cMsgCreateMessage(),
 * cMsgCreateNewMessage(), or cMsgCopyMessage().
 *
 * @param domainId domain connection id
 * @param msg pointer to a message structure
 * @param timeout amount of time to wait for the response
 * @param response integer pointer that gets filled with the response
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSyncSend
 */   
int cMsgSyncSend(void *domainId, void *msg, const struct timespec *timeout, int *response) {
    int err;
    intptr_t    index;
    cMsgDomain *domain;
    
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
  
    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->syncSend(domain->implId, msg, timeout, response);
    cleanupAfterFunc((int)index);

    return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine sends any pending (queued up) communication with the server.
 * The implementation of this routine depends entirely on the domain in which 
 * it is being used. In the cMsg domain, this routine does nothing as all server
 * communications are sent immediately upon calling any function.
 *
 * @param domainId domain connection id
 * @param timeout amount of time to wait for completion
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgFlush
 */   
int cMsgFlush(void *domainId, const struct timespec *timeout) {
    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
  
    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->flush(domain->implId, timeout);
    cleanupAfterFunc((int)index);
  
    return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine subscribes to messages of the given subject and type.
 * When a message is received, the given callback is passed the message
 * pointer and the userArg pointer and then is executed. A configuration
 * structure is given to determine the behavior of the callback.
 * Only 1 subscription for a specific combination of subject, type, callback
 * and userArg is allowed.
 *
 * @param domainId domain connection id
 * @param subject subject of messages subscribed to
 * @param type type of messages subscribed to
 * @param callback pointer to callback to be executed on receipt of message
 * @param userArg user-specified pointer to be passed to the callback
 * @param config pointer to callback configuration structure
 * @param handle pointer to handle (void pointer) to be used for unsubscribing
 *               from this subscription
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSubscribe
 */   
int cMsgSubscribe(void *domainId, const char *subject, const char *type, cMsgCallbackFunc *callback,
                  void *userArg, cMsgSubscribeConfig *config, void **handle) {
                    
  int err;
  intptr_t    index;
  cMsgDomain *domain;
  
  index = (intptr_t) domainId;
  if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

  if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
  err = domain->functions->subscribe(domain->implId, subject, type,
                                     callback, userArg, config, handle);
  cleanupAfterFunc((int)index);
  
  return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine unsubscribes to messages of the given handle (which
 * represents a given subject, type, callback, and user argument).
 *
 * @param domainId domain connection id
 * @param handle void pointer obtained from cMsgSubscribe
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgUnSubscribe
 */   
int cMsgUnSubscribe(void *domainId, void *handle) {
    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->unsubscribe(domain->implId, handle);
    cleanupAfterFunc((int)index);
  
    return err;
} 


/*-------------------------------------------------------------------*/


/**
 * This routine pauses the delivery of messages to the given subscription callback.
 *
 * @param domainId domain connection id
 * @param handle void pointer obtained from cMsgSubscribe
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement this function
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSubscriptionPause
 */
int cMsgSubscriptionPause(void *domainId, void *handle) {
    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->subscriptionPause(domain->implId, handle);
    cleanupAfterFunc((int)index);
  
    return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine resumes the delivery of messages to the given subscription callback.
 *
 * @param domainId domain connection id
 * @param handle void pointer obtained from cMsgSubscribe
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement this function
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSubscriptionPause
 */
int cMsgSubscriptionResume(void *domainId, void *handle) {
    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->subscriptionResume(domain->implId, handle);
    cleanupAfterFunc((int)index);
  
    return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine returns the number of messages currently in a subscription callback's queue.
 *
 * @param domainId domain connection id
 * @param handle void pointer obtained from cMsgSubscribe
 * @param count int pointer filled in with number of messages in subscription callback queue
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSubscriptionPause
 */
int cMsgSubscriptionQueueCount(void *domainId, void *handle, int *count) {
    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->subscriptionQueueCount(domain->implId, handle, count);
    cleanupAfterFunc((int)index);
  
    return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine returns true(1) if a subscription callback's queue is full, else false(0).
 *
 * @param domainId domain connection id
 * @param handle void pointer obtained from cMsgSubscribe
 * @param full int pointer filled in with 1 if subscription callback queue full, else 0
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSubscriptionPause
 */
int cMsgSubscriptionQueueIsFull(void *domainId, void *handle, int *full) {
    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->subscriptionQueueIsFull(domain->implId, handle, full);
    cleanupAfterFunc((int)index);
  
    return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine clears a subscription callback's queue of all messages.
 *
 * @param domainId domain connection id
 * @param handle void pointer obtained from cMsgSubscribe
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if the id/handle is bad or handle is NULL
 */
int cMsgSubscriptionQueueClear(void *domainId, void *handle) {
    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->subscriptionQueueClear(domain->implId, handle);
    cleanupAfterFunc((int)index);
  
    return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine returns the total number of messages sent to a subscription callback.
 *
 * @param domainId domain connection id
 * @param handle void pointer obtained from cMsgSubscribe
 * @param total int pointer filled in with total number of messages sent to a subscription callback
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSubscriptionPause
 */
int cMsgSubscriptionMessagesTotal(void *domainId, void *handle, int *total) {
    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->subscriptionMessagesTotal(domain->implId, handle, total);
    cleanupAfterFunc((int)index);
  
    return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine gets one message from another cMsg client by sending out
 * an initial message to that responder. It is a synchronous routine that
 * fails when no reply is received with the given timeout. This function
 * can be thought of as a peer-to-peer exchange of messages.
 * One message is sent to all listeners. The first responder
 * to the initial message will have its single response message sent back
 * to the original sender. In the cMsg domain, if there are no subscribers
 * to get the sent message, this routine returns CMSG_OK, but with a NULL message.
 *
 * @param domainId domain connection id
 * @param sendMsg messages to send to all listeners
 * @param timeout amount of time to wait for the response message
 * @param replyMsg message received from the responder; may be NULL
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSendAndGet
 */   
int cMsgSendAndGet(void *domainId, void *sendMsg, const struct timespec *timeout, void **replyMsg) {
    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->sendAndGet(domain->implId, sendMsg, timeout, replyMsg);
    cleanupAfterFunc((int)index);
  
    return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine gets one message from a one-time subscription to the given
 * subject and type.
 *
 * @param domainId domain connection id
 * @param subject subject of message subscribed to
 * @param type type of message subscribed to
 * @param timeout amount of time to wait for the message
 * @param replyMsg message received
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSendAndGet
 */   
int cMsgSubscribeAndGet(void *domainId, const char *subject, const char *type,
                        const struct timespec *timeout, void **replyMsg) {
    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->subscribeAndGet(domain->implId, subject, type,
                                              timeout, replyMsg);
    cleanupAfterFunc((int)index);
  
    return err;
}


/*-------------------------------------------------------------------*/


/**
 * This method is a synchronous call to receive a message containing monitoring
 * data which describes the state of the cMsg domain the user is connected to.
 * The time is data was sent can be obtained by calling cMsgGetSenderTime.
 * The monitoring data in xml format can be obtained by calling cMsgGetText.
 *
 * @param domainId domain connection id
 * @param command string to monitor data collecting routine
 * @param replyMsg message received from the domain containing monitor data
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgSendAndGet
 */   
int cMsgMonitor(void *domainId, const char *command, void **replyMsg) {
    
    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->monitor(domain->implId, command, replyMsg);
    cleanupAfterFunc((int)index);
  
    return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine enables the receiving of messages and delivery to callbacks.
 * The receiving of messages is disabled by default and must be explicitly enabled.
 *
 * @param domainId domain connection id
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgReceiveStart
 */   
int cMsgReceiveStart(void *domainId) {

  int err;
  intptr_t    index;
  cMsgDomain *domain;
  
  index = (intptr_t) domainId;
  if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

  if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
  err = domain->functions->start(domain->implId);
  if (err == CMSG_OK) domain->receiveState = 1;
  cleanupAfterFunc((int)index);
  
  return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine disables the receiving of messages and delivery to callbacks.
 * The receiving of messages is disabled by default. This routine only has an
 * effect when cMsgReceiveStart() was previously called.
 *
 * @param domainId domain connection id
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgReceiveStop
 */   
int cMsgReceiveStop(void *domainId) {

    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->stop(domain->implId);
    if (err == CMSG_OK) domain->receiveState = 0;
    cleanupAfterFunc((int)index);
  
    return err;
}


/*-------------------------------------------------------------------*/


/**
 * This routine gets the state of a cMsg connection. If connectState gets
 * filled with a one, there is a valid connection. Anything else (zero
 * in this case), indicates client is not connected. The meaning of
 * "connected" may vary with domain.
 *
 * @param domainId domain connection id
 * @param connected integer pointer to be filled in with connection state,
 *                     (1-connected, 0-unconnected)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgGetConnectState
 */
int cMsgGetConnectState(void *domainId, int *connected) {
    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->isConnected(domain->implId, connected);
    cleanupAfterFunc((int)index);
  
    return err;
}


/*-------------------------------------------------------------------*/
/*   shutdown handler functions                                      */
/*-------------------------------------------------------------------*/


/**
 * This routine sets the shutdown handler function.
 *
 * @param domainId domain connection id
 * @param handler shutdown handler function
 * @param userArg argument to shutdown handler 
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgDisconnect
 */   
int cMsgSetShutdownHandler(void *domainId, cMsgShutdownHandler *handler, void *userArg) {
    int err; 
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);

    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->setShutdownHandler(domain->implId, handler, userArg);
    cleanupAfterFunc((int)index);
  
    return err;
}

/*-------------------------------------------------------------------*/

/**
 * Method to shutdown the given clients.
 *
 * @param domainId domain connection id
 * @param client client(s) to be shutdown
 * @param flag   flag describing the mode of shutdown: 0 to not include self,
 *               CMSG_SHUTDOWN_INCLUDE_ME to include self in shutdown.
 * 
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement shutdown
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called or
 *                            flag argument improper value
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgDisconnect
 */
int cMsgShutdownClients(void *domainId, const char *client, int flag) {  
    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
    if (flag != 0 && flag!= CMSG_SHUTDOWN_INCLUDE_ME) return(CMSG_BAD_ARGUMENT);
    
    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->shutdownClients(domain->implId, client, flag);
    cleanupAfterFunc((int)index);
  
    return err;
}

/*-------------------------------------------------------------------*/

/**
 * Method to shutdown the given servers.
 *
 * @param domainId domain connection id
 * @param server server(s) to be shutdown
 * @param flag   flag describing the mode of shutdown: 0 to not include self,
 *               CMSG_SHUTDOWN_INCLUDE_ME to include self in shutdown.
 * 
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_IMPLEMENTED if the subdomain used does NOT implement shutdown
 * @returns CMSG_NETWORK_ERROR if error in communicating with the server
 * @returns CMSG_BAD_ARGUMENT if bad domainId or cMsgDisconnect() already called or
 *                            flag argument improper value
 * @returns any errors returned from the actual domain dependent implemenation
 *          of cMsgDisconnect
 */
int cMsgShutdownServers(void *domainId, const char *server, int flag) {  
    int err;
    intptr_t    index;
    cMsgDomain *domain;
  
    index = (intptr_t) domainId;
    if (index < 0 || index > LOCAL_ARRAY_SIZE-1) return(CMSG_BAD_ARGUMENT);
    if (flag != 0 && flag!= CMSG_SHUTDOWN_INCLUDE_ME) return(CMSG_BAD_ARGUMENT);
    
    if ( (domain = prepareToCallFunc((int)index)) == NULL ) return (CMSG_BAD_ARGUMENT);
    err = domain->functions->shutdownServers(domain->implId, server, flag);
    cleanupAfterFunc((int)index);
  
    return err;
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/


/**
 * This routine returns a string describing the given error condition.
 * It can also print out that same string with printf if the debug level
 * is set to CMSG_DEBUG_ERROR or CMSG_DEBUG_SEVERE by cMsgSetDebugLevel().
 * The returned string is a static char array. This means it is not 
 * thread-safe and will be overwritten on subsequent calls.
 *
 * @param error error condition
 *
 * @returns error string
 */   
char *cMsgPerror(int error) {

  static char temp[256];

  switch(error) {

  case CMSG_OK:
    sprintf(temp, "CMSG_OK:  action completed successfully\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_OK:  action completed successfully\n");
    break;

  case CMSG_ERROR:
    sprintf(temp, "CMSG_ERROR:  generic error return\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_ERROR:  generic error return\n");
    break;

  case CMSG_TIMEOUT:
    sprintf(temp, "CMSG_TIMEOUT:  no response from cMsg server within timeout period\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_TIMEOUT:  no response from cMsg server within timeout period\n");
    break;

  case CMSG_NOT_IMPLEMENTED:
    sprintf(temp, "CMSG_NOT_IMPLEMENTED:  function not implemented\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_NOT_IMPLEMENTED:  function not implemented\n");
    break;

  case CMSG_BAD_ARGUMENT:
    sprintf(temp, "CMSG_BAD_ARGUMENT:  one or more arguments bad\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_BAD_ARGUMENT:  one or more arguments bad\n");
    break;

  case CMSG_BAD_FORMAT:
    sprintf(temp, "CMSG_BAD_FORMAT:  one or more arguments in the wrong format\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_BAD_FORMAT:  one or more arguments in the wrong format\n");
    break;

  case CMSG_BAD_DOMAIN_TYPE:
    sprintf(temp, "CMSG_BAD_DOMAIN_TYPE:  domain type not supported\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_BAD_DOMAIN_TYPE:  domain type not supported\n");
    break;

  case CMSG_ALREADY_EXISTS:
    sprintf(temp, "CMSG_ALREADY_EXISTS: a unique item with that property already exists\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_ALREADY_EXISTS:  a unique item with that property already exists\n");
    break;

  case CMSG_NOT_INITIALIZED:
    sprintf(temp, "CMSG_NOT_INITIALIZED:  cMsgConnect needs to be called\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_NOT_INITIALIZED:  cMsgConnect needs to be called\n");
    break;

  case CMSG_ALREADY_INIT:
    sprintf(temp, "CMSG_ALREADY_INIT:  cMsgConnect already called\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_ALREADY_INIT:  cMsgConnect already called\n");
    break;

  case CMSG_LOST_CONNECTION:
    sprintf(temp, "CMSG_LOST_CONNECTION:  connection to cMsg server lost\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_LOST_CONNECTION:  connection to cMsg server lost\n");
    break;

  case CMSG_NETWORK_ERROR:
    sprintf(temp, "CMSG_NETWORK_ERROR:  error talking to cMsg server\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_NETWORK_ERROR:  error talking to cMsg server\n");
    break;

  case CMSG_SOCKET_ERROR:
    sprintf(temp, "CMSG_SOCKET_ERROR:  error setting socket options\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_SOCKET_ERROR:  error setting socket options\n");
    break;

  case CMSG_PEND_ERROR:
    sprintf(temp, "CMSG_PEND_ERROR:  error waiting for messages to arrive\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_PEND_ERROR:  error waiting for messages to arrive\n");
    break;

  case CMSG_ILLEGAL_MSGTYPE:
    sprintf(temp, "CMSG_ILLEGAL_MSGTYPE:  pend received illegal message type\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_ILLEGAL_MSGTYPE:  pend received illegal message type\n");
    break;

  case CMSG_OUT_OF_MEMORY:
    sprintf(temp, "CMSG_OUT_OF_MEMORY:  ran out of memory\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_OUT_OF_MEMORY:  ran out of memory\n");
    break;

  case CMSG_OUT_OF_RANGE:
    sprintf(temp, "CMSG_OUT_OF_RANGE:  argument is out of range\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_OUT_OF_RANGE:  argument is out of range\n");
    break;

  case CMSG_LIMIT_EXCEEDED:
    sprintf(temp, "CMSG_LIMIT_EXCEEDED:  trying to create too many of something\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_LIMIT_EXCEEDED:  trying to create too many of something\n");
    break;

  case CMSG_BAD_DOMAIN_ID:
    sprintf(temp, "CMSG_BAD_DOMAIN_ID: id does not match any existing domain\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_BAD_DOMAIN_ID: id does not match any existing domain\n");
    break;

  case CMSG_BAD_MESSAGE:
    sprintf(temp, "CMSG_BAD_MESSAGE: message is not in the correct form\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_BAD_MESSAGE: message is not in the correct form\n");
    break;

  case CMSG_WRONG_DOMAIN_TYPE:
    sprintf(temp, "CMSG_WRONG_DOMAIN_TYPE: UDL does not match the server type\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_WRONG_DOMAIN_TYPE: UDL does not match the server type\n");
    break;
  case CMSG_NO_CLASS_FOUND:
    sprintf(temp, "CMSG_NO_CLASS_FOUND: class cannot be found to instantiate a subdomain client handler\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_NO_CLASS_FOUND: class cannot be found to instantiate a subdomain client handler\n");
    break;

  case CMSG_DIFFERENT_VERSION:
    sprintf(temp, "CMSG_DIFFERENT_VERSION: client and server are different versions\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_DIFFERENT_VERSION: client and server are different versions\n");
    break;

  case CMSG_WRONG_PASSWORD:
    sprintf(temp, "CMSG_WRONG_PASSWORD: wrong password given\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_WRONG_PASSWORD: wrong password given\n");
    break;

  case CMSG_SERVER_DIED:
    sprintf(temp, "CMSG_SERVER_DIED: server died\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_SERVER_DIED: server died\n");
    break;

  case CMSG_ABORT:
    sprintf(temp, "CMSG_ABORT: abort procedure\n");
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("CMSG_ABORT: aborted procedure\n");
    break;

  default:
    sprintf(temp, "?cMsgPerror...no such error: %d\n",error);
    if (cMsgDebug>CMSG_DEBUG_ERROR) printf("?cMsgPerror...no such error: %d\n",error);
    break;
  }

  return(temp);
}


/*-------------------------------------------------------------------*/


/**
 * This routine sets the level of debug output. The argument should be
 * one of:<p>
 * - #CMSG_DEBUG_NONE
 * - #CMSG_DEBUG_INFO
 * - #CMSG_DEBUG_SEVERE
 * - #CMSG_DEBUG_ERROR
 * - #CMSG_DEBUG_WARN
 *
 * @param level debug level desired
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if debug level is bad
 */   
int cMsgSetDebugLevel(int level) {
  
  if ((level != CMSG_DEBUG_NONE)  &&
      (level != CMSG_DEBUG_INFO)  &&
      (level != CMSG_DEBUG_WARN)  &&
      (level != CMSG_DEBUG_ERROR) &&
      (level != CMSG_DEBUG_SEVERE)) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  cMsgDebug = level;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*
 *
 * Internal functions
 *
 *-------------------------------------------------------------------*/


/**
 * This routine registers a few permanent domain implementations consisting
 * of a set of functions that implement all the basic domain
 * functionality (connect, disconnect, send, syncSend, flush, subscribe,
 * unsubscribe, sendAndGet, subscribeAndGet, start, and stop). These
 * permanent domains are the cmsg, rc, and file domains.
 * This routine must be updated to include each new permanent domain
 * accessible from the "C" language.
 */   
static int registerPermanentDomains() {
  
  /* cMsg type */
  dTypeInfo[0].type = strdup(cmsgDomainTypeInfo.type);
  dTypeInfo[0].functions = cmsgDomainTypeInfo.functions;


  /* runcontrol (rc) domain */
  dTypeInfo[1].type = strdup(rcDomainTypeInfo.type);
  dTypeInfo[1].functions = rcDomainTypeInfo.functions;


  /* for file domain */
  dTypeInfo[2].type = strdup(fileDomainTypeInfo.type);
  dTypeInfo[2].functions = fileDomainTypeInfo.functions;
  
  /* for file domain */
  dTypeInfo[3].type = strdup(emuDomainTypeInfo.type);
  dTypeInfo[3].functions = emuDomainTypeInfo.functions;
  
  
  /* for dummy domain */
  /*
  dTypeInfo[4].type = strdup(dummyDomainTypeInfo.type);
  dTypeInfo[4].functions = dummyDomainTypeInfo.functions;
  */
  return(CMSG_OK);
}


/**
 * This routine registers domain implementations dynamically.
 * The registration includes the name of the domain
 * along with the set of functions that implement all the basic domain
 * functionality (connect, disconnect, send, syncSend, flush, subscribe,
 * unsubscribe, sendAndGet, subscribeAndGet, start, and stop).
 * This routine is used when a connection to a user-written domain
 * is found in a client's UDL.
 */   
static int registerDynamicDomains(char *domainType) {

  char *lowerCase;
  unsigned int i;
  int   len, index=-1;
  char  functionName[256];
  domainFunctions *funcs;
  char  libName[256];
  void *libHandle, *sym;

  /*
   * We have already loaded the cmsg, rc, and file domains.
   * Now we need to dynamically load any libraries needed
   * to support other domains. Look for shared libraries
   * with names of cMsgLib<domain>.so where domain is the
   * domain found in the parsing of the UDL lowered to all
   * lower case letters.
   */
   
  /* First lower the domain name to all lower case letters. */
  lowerCase = strdup(domainType);
  len = (int)strlen(lowerCase);
  for (i=0; i<len; i++) {
    lowerCase[i] = (char)tolower(lowerCase[i]);
  }
  
  /* Check to see if it's been loaded already */
  for (i=0; i < CMSG_MAX_DOMAIN_TYPES; i++) {
    if (dTypeInfo[i].type == NULL) {
        if (index < 0) {
            index = i;
        }
        continue;
    }    
    
    if ( strcmp(lowerCase, dTypeInfo[i].type) == 0 ) {
        /* already have this domain loaded */
/* printf("registerDynamicDomains: domain %s is already loaded\n", lowerCase); */
        free(lowerCase);
        return(CMSG_OK);    
    }  
  }
  
  /* we need to load a new domain library */  
  funcs = (domainFunctions *) malloc(sizeof(domainFunctions));
  if (funcs == NULL) {
    free(lowerCase);
    return(CMSG_OUT_OF_MEMORY);  
  }


  /* create name of library to look for */
  sprintf(libName, "libcmsg%s.so", lowerCase);
/* printf("registerDynamicDomains: looking for %s\n", libName); */
  
  /* open library */
  libHandle = dlopen(libName, RTLD_NOW);
  if (libHandle == NULL) {
    free(funcs);
    free(lowerCase);
    return(CMSG_ERROR);
  }
  
  /* get "connect" function */
  sprintf(functionName, "cmsg_%s_connect", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
    free(funcs);
    free(lowerCase);
    dlclose(libHandle);
    return(CMSG_ERROR);
  }
  funcs->connect = (CONNECT_PTR) sym;

  /* get "reconnect" function */
  sprintf(functionName, "cmsg_%s_reconnect", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
      free(funcs);
      free(lowerCase);
      dlclose(libHandle);
      return(CMSG_ERROR);
  }
  funcs->reconnect = (START_STOP_PTR) sym;

  /* get "send" function */
  sprintf(functionName, "cmsg_%s_send", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
    free(funcs);
    free(lowerCase);
    dlclose(libHandle);
    return(CMSG_ERROR);
  }
  funcs->send = (SEND_PTR) sym;

  /* get "syncSend" function */
  sprintf(functionName, "cmsg_%s_syncSend", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
    free(funcs);
    free(lowerCase);
    dlclose(libHandle);
    return(CMSG_ERROR);
  }
  funcs->syncSend = (SYNCSEND_PTR) sym;

  /* get "flush" function */
  sprintf(functionName, "cmsg_%s_flush", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
    free(funcs);
    free(lowerCase);
    dlclose(libHandle);
    return(CMSG_ERROR);
  }
  funcs->flush = (FLUSH_PTR) sym;

  /* get "subscribe" function */
  sprintf(functionName, "cmsg_%s_subscribe", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
    free(funcs);
    free(lowerCase);
    dlclose(libHandle);
    return(CMSG_ERROR);
  }
  funcs->subscribe = (SUBSCRIBE_PTR) sym;

  /* get "unsubscribe" function */
  sprintf(functionName, "cmsg_%s_unsubscribe", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
      free(funcs);
      free(lowerCase);
      dlclose(libHandle);
      return(CMSG_ERROR);
  }
  funcs->unsubscribe = (UNSUBSCRIBE_PTR) sym;

  /* get "subscriptionPause" function */
  sprintf(functionName, "cmsg_%s_subscriptionPause", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
      free(funcs);
      free(lowerCase);
      dlclose(libHandle);
      return(CMSG_ERROR);
  }
  funcs->subscriptionPause = (UNSUBSCRIBE_PTR) sym;

  /* get "subscriptionResume" function */
  sprintf(functionName, "cmsg_%s_subscriptionResume", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
      free(funcs);
      free(lowerCase);
      dlclose(libHandle);
      return(CMSG_ERROR);
  }
  funcs->subscriptionResume = (UNSUBSCRIBE_PTR) sym;

  /* get "subscriptionQueueClear" function */
  sprintf(functionName, "cmsg_%s_subscriptionQueueClear", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
      free(funcs);
      free(lowerCase);
      dlclose(libHandle);
      return(CMSG_ERROR);
  }
  funcs->subscriptionQueueClear = (UNSUBSCRIBE_PTR) sym;

  /* get "subscriptionQueueCount" function */
  sprintf(functionName, "cmsg_%s_subscriptionQueueCount", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
      free(funcs);
      free(lowerCase);
      dlclose(libHandle);
      return(CMSG_ERROR);
  }
  funcs->subscriptionQueueCount = (SUBSCRIPTION_PTR) sym;

  /* get "subscriptionQueueIsFull" function */
  sprintf(functionName, "cmsg_%s_subscriptionQueueIsFull", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
      free(funcs);
      free(lowerCase);
      dlclose(libHandle);
      return(CMSG_ERROR);
  }
  funcs->subscriptionQueueIsFull = (SUBSCRIPTION_PTR) sym;

  /* get "subscriptionMessagesTotal" function */
  sprintf(functionName, "cmsg_%s_subscriptionMessagesTotal", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
      free(funcs);
      free(lowerCase);
      dlclose(libHandle);
      return(CMSG_ERROR);
  }
  funcs->subscriptionMessagesTotal = (SUBSCRIPTION_PTR) sym;

  /* get "subscribeAndGet" function */
  sprintf(functionName, "cmsg_%s_subscribeAndGet", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
    free(funcs);
    free(lowerCase);
    dlclose(libHandle);
    return(CMSG_ERROR);
  }
  funcs->subscribeAndGet = (SUBSCRIBE_AND_GET_PTR) sym;

  /* get "sendAndGet" function */
  sprintf(functionName, "cmsg_%s_sendAndGet", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
    free(funcs);
    free(lowerCase);
    dlclose(libHandle);
    return(CMSG_ERROR);
  }
  funcs->sendAndGet = (SEND_AND_GET_PTR) sym;

  /* get "start" function */
  sprintf(functionName, "cmsg_%s_start", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
    free(funcs);
    free(lowerCase);
    dlclose(libHandle);
    return(CMSG_ERROR);
  }
  funcs->start = (START_STOP_PTR) sym;

  /* get "stop" function */
  sprintf(functionName, "cmsg_%s_stop", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
    free(funcs);
    free(lowerCase);
    dlclose(libHandle);
    return(CMSG_ERROR);
  }
  funcs->stop = (START_STOP_PTR) sym;

  /* get "disconnect" function */
  sprintf(functionName, "cmsg_%s_disconnect", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
    free(funcs);
    free(lowerCase);
    dlclose(libHandle);
    return(CMSG_ERROR);
  }
  funcs->disconnect = (DISCONNECT_PTR) sym;

  /* get "shutdownClients" function */
  sprintf(functionName, "cmsg_%s_shutdownClients", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
    free(funcs);
    free(lowerCase);
    dlclose(libHandle);
    return(CMSG_ERROR);
  }
  funcs->shutdownClients = (SHUTDOWN_PTR) sym;

  /* get "shutdownServers" function */
  sprintf(functionName, "cmsg_%s_shutdownServers", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
    free(funcs);
    free(lowerCase);
    dlclose(libHandle);
    return(CMSG_ERROR);
  }
  funcs->shutdownServers = (SHUTDOWN_PTR) sym;

  /* get "setShutdownHandler" function */
  sprintf(functionName, "cmsg_%s_setShutdownHandler", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
    free(funcs);
    free(lowerCase);
    dlclose(libHandle);
    return(CMSG_ERROR);
  }
  funcs->setShutdownHandler = (SET_SHUTDOWN_HANDLER_PTR) sym;

  /* get "isConnected" function */
  sprintf(functionName, "cmsg_%s_isConnected", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
    free(funcs);
    free(lowerCase);
    dlclose(libHandle);
    return(CMSG_ERROR);
  }
  funcs->isConnected = (ISCONNECTED_PTR) sym;

  /* get "setUDL" function */
  sprintf(functionName, "cmsg_%s_setUDL", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
      free(funcs);
      free(lowerCase);
      dlclose(libHandle);
      return(CMSG_ERROR);
  }
  funcs->setUDL = (SETUDL_PTR) sym;

  /* get "getCurrentUDL" function */
  sprintf(functionName, "cmsg_%s_getCurrentUDL", lowerCase);
  sym = dlsym(libHandle, functionName);
  if (sym == NULL) {
      free(funcs);
      free(lowerCase);
      dlclose(libHandle);
      return(CMSG_ERROR);
  }
  funcs->getCurrentUDL = (GETUDL_PTR) sym;

   /* for new domain */
  dTypeInfo[index].type = lowerCase; 
  dTypeInfo[index].functions = funcs;

      
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine initializes the given domain data structure. All strings
 * are set to null.
 *
 * @param domain pointer to structure holding domain info
 */   
static void domainInit(cMsgDomain *domain) {  
  domain->receiveState   = 0;
      
  domain->implId         = NULL;
  domain->type           = NULL;
  domain->name           = NULL;
  domain->udl            = NULL;
  domain->description    = NULL;
  domain->UDLremainder   = NULL;
  domain->functions      = NULL;  
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees all of the allocated memory of the given domain
 * data structure. 
 *
 * @param domain pointer to structure holding domain info
 */   
static void domainFree(cMsgDomain *domain) {  
  if (domain->type         != NULL) {free(domain->type);         domain->type         = NULL;}
  if (domain->name         != NULL) {free(domain->name);         domain->name         = NULL;}
  if (domain->udl          != NULL) {free(domain->udl);          domain->udl          = NULL;}
  if (domain->description  != NULL) {free(domain->description);  domain->description  = NULL;}
  if (domain->UDLremainder != NULL) {free(domain->UDLremainder); domain->UDLremainder = NULL;}
}


/*-------------------------------------------------------------------*/
/*   miscellaneous local functions                                   */
/*-------------------------------------------------------------------*/


/**
 * This routine parses the UDL given by the client in cMsgConnect().
 *
 * The UDL is the Universal Domain Locator used to uniquely
 * identify the cMsg server to connect to. It has the form:<p>
 *       <b><i>cMsg:domainType://domainInfo </i></b><p>
 * The domainType portion gets returned as the domainType.
 * The domainInfo portion gets returned the the UDLremainder.
 * Memory gets allocated for both the domainType and domainInfo which
 * must be freed by the caller.
 *
 * @param UDL UDL
 * @param domainType string which gets filled in with the domain type (eg. cMsg)
 * @param UDLremainder string which gets filled in with everything in the UDL
 *                     after the ://
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if regular expression compilation fails
 * @returns CMSG_BAD_ARGUMENT if the UDL is null
 * @returns CMSG_BAD_FORMAT if the UDL is formatted incorrectly
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 */   
static int parseUDL(const char *UDL, char **domainType, char **UDLremainder) {

    int        err;
    size_t     len, bufLength;
    char       *udl, *buffer;
    const char *pattern = "(cMsg)?:?([a-zA-Z0-9_\\-]+)://(.*)?";  
    regmatch_t matches[4]; /* we have 4 potential matches: 1 whole, 3 sub */
    regex_t    compiled;
    
    if (UDL == NULL) {
        return (CMSG_BAD_FORMAT);
    }
    
    /* make a copy */
    udl = strdup(UDL);
    
    /* make a big enough buffer to construct various strings, 256 chars minimum */
    len       = strlen(UDL) + 1;
    bufLength = len < 256 ? 256 : len;    
    buffer    = (char *) malloc(bufLength);
    if (buffer == NULL) {
      free(udl);
      return(CMSG_OUT_OF_MEMORY);
    }

    /*
     * cMsg domain UDL is of the form:
     *        cMsg:<domain>://<domain-specific-stuff>
     * where the first "cMsg:" is optional and case insensitive.
     */

    /* compile regular expression (case insensitive matching) */
    err = cMsgRegcomp(&compiled, pattern, REG_EXTENDED|REG_ICASE);
    if (err != 0) {
        free(udl);
        free(buffer);
        return (CMSG_ERROR);
    }
    
    /* find matches */
    err = cMsgRegexec(&compiled, udl, 4, matches, 0);
    if (err != 0) {
        /* no match */
        free(udl);
        free(buffer);
        return (CMSG_BAD_FORMAT);
    }
    
    /* free up memory */
    cMsgRegfree(&compiled);
            
    /* find domain name */
    if ((unsigned int)(matches[2].rm_so) < 0) {
        /* no match for host */
        free(udl);
        free(buffer);
        return (CMSG_BAD_FORMAT);
    }
    else {
       buffer[0] = 0;
       len = matches[2].rm_eo - matches[2].rm_so;
       strncat(buffer, udl+matches[2].rm_so, len);
                        
        if (domainType != NULL) {
            *domainType = strdup(buffer);
        }
    }
/*printf("parseUDL: domain = %s\n", buffer);*/


    /* find domain remainder */
    buffer[0] = 0;
    if (matches[3].rm_so < 0) {
        /* no match */
        if (UDLremainder != NULL) {
            *UDLremainder = NULL;
        }
    }
    else {
        buffer[0] = 0;
        len = matches[3].rm_eo - matches[3].rm_so;
        strncat(buffer, udl+matches[3].rm_so, len);
                
        if (UDLremainder != NULL) {
            *UDLremainder = strdup(buffer);
        }        
/* printf("parseUDL: domain remainder = %s\n", buffer); */
    }

    /* UDL parsed ok */
    free(udl);
    free(buffer);
    return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine checks a string given as a function argument.
 * It returns an error if it contains an unprintable character or any
 * character from a list of excluded characters (`'").
 *
 * @param s string to check
 *
 * @returns CMSG_OK if string is OK
 * @returns CMSG_ERROR if string contains excluded or unprintable characters
 */   
static int checkString(const char *s) {

  int i, len;

  if (s == NULL) return(CMSG_ERROR);
  len = (int)strlen(s);

  /* check for printable character */
  for (i=0; i<len; i++) {
    if (isprint((int)s[i]) == 0) return(CMSG_ERROR);
  }

  /* check for excluded chars */
  if (strpbrk(s, excludedChars) != NULL) return(CMSG_ERROR);
  
  /* string ok */
  return(CMSG_OK);
}



/*-------------------------------------------------------------------*/
/*   system accessor functions                                       */
/*-------------------------------------------------------------------*/

/**
 * This routine gets the UDL used to establish a cMsg connection.
 * If successful, this routine will return a pointer to char inside the
 * system structure. The user may NOT write to this memory location!
 *
 * @param domainId id of the domain connection
 * @param udl pointer to pointer filled with the UDL
 *
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 * @returns CMSG_OK if successful
 */   
int cMsgGetUDL(void *domainId, char **udl) {

  cMsgDomain *domain = (cMsgDomain *) domainId;
  
  if (domain == NULL || udl == NULL) return(CMSG_BAD_ARGUMENT);
  
  if (domain->udl == NULL) {
    *udl = NULL;
  }
  else {
    *udl = domain->udl;
  }
  return(CMSG_OK);
}
  
  
/*-------------------------------------------------------------------*/

/**
 * This routine gets the client name used in a cMsg connection.
 * If successful, this routine will return a pointer to char inside the
 * system structure. The user may NOT write to this memory location!
 *
 * @param domainId id of the domain connection
 * @param name pointer to pointer filled with the name
 *
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 * @returns CMSG_OK if successful
 */   
int cMsgGetName(void *domainId, char **name) {

  cMsgDomain *domain = (cMsgDomain *) domainId;

  if (domain == NULL || name == NULL) return(CMSG_BAD_ARGUMENT);
  
  if (domain->name == NULL) {
    *name = NULL;
  }
  else {
    *name = domain->name;
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/


/**
 * This routine gets the client description used in a cMsg connection.
 * If successful, this routine will return a pointer to char inside the
 * system structure. The user may NOT write to this memory location!
 *
 * @param domainId id of the domain connection
 * @param description pointer to pointer filled with the description
 *
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 * @returns CMSG_OK if successful
 */   
int cMsgGetDescription(void *domainId, char **description) {

  cMsgDomain *domain = (cMsgDomain *) domainId;

  if (domain == NULL || description == NULL) return(CMSG_BAD_ARGUMENT);
  
  if (domain->description == NULL) {
    *description = NULL;
  }
  else {
    *description = domain->description;
  }
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine gets the message receiving state of a cMsg connection. If
 * receiveState gets filled with a one, all messages sent to the client
 * will be received and sent to appropriate callbacks . Anything else (zero
 * in this case), indicates no messages will be received or sent to callbacks.
 *
 * @param domainId id of the domain connection
 * @param receiveState integer pointer to be filled in with the receive state
 *
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 * @returns CMSG_OK if successful
 */   
int cMsgGetReceiveState(void *domainId, int *receiveState) {

  cMsgDomain *domain = (cMsgDomain *) domainId;
  
  if (domain == NULL || receiveState == NULL) return(CMSG_BAD_ARGUMENT);
  *receiveState = domain->receiveState;
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/*   message accessor functions                                      */
/*-------------------------------------------------------------------*/


/**
 * This routine initializes a given message structure.
 *
 * @param msg pointer to message structure being initialized
 */   
static void initMessage(cMsgMessage_t *msg) {
    int endian;
    if (msg == NULL) return;
    
    msg->version          = CMSG_VERSION_MAJOR;
    msg->sysMsgId         = 0;
    msg->bits             = 0;
    msg->info             = 0;
    msg->historyLengthMax = CMSG_HISTORY_LENGTH_MAX;
    msg->payloadCount     = 0;
    
    /* default is local endian */
    if (cMsgNetLocalByteOrder(&endian) == CMSG_OK) {
        if (endian == CMSG_ENDIAN_BIG) {
            msg->info |= CMSG_IS_BIG_ENDIAN;
        }
        else {
            msg->info &= ~CMSG_IS_BIG_ENDIAN;
        }
    }
    
    msg->domain      = NULL;
    msg->payloadText = NULL;
    msg->payload     = NULL;
    msg->subject     = NULL;
    msg->type        = NULL;
    msg->text        = NULL;
    msg->byteArray   = NULL;
    msg->next        = NULL;

    msg->byteArrayOffset     = 0;
    msg->byteArrayLength     = 0;
    msg->byteArrayLengthFull = 0;
    msg->reserved            = 0;
    msg->userInt             = 0;
    msg->userTime.tv_sec     = 0;
    msg->userTime.tv_nsec    = 0;

    msg->sender             = NULL;
    msg->senderHost         = NULL;
    msg->senderTime.tv_sec  = 0;
    msg->senderTime.tv_nsec = 0;
    msg->senderToken        = 0;

    msg->receiver             = NULL;
    msg->receiverHost         = NULL;
    msg->receiverTime.tv_sec  = 0;
    msg->receiverTime.tv_nsec = 0;
    msg->receiverSubscribeId  = 0;
    
    msg->udpSend = 0;
    msg->context.domain  = NULL;
    msg->context.subject = NULL;
    msg->context.type    = NULL;
    msg->context.udl     = NULL;
    msg->context.cueSize = NULL;

    return;
  }


/*-------------------------------------------------------------------*/


/**
 * This routine frees the memory of the components of a message,
 * but not the message itself.
 *
 * @param vmsg address of pointer to message structure being freed
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if msg is NULL
 */   
static int freeMessage(void *vmsg) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
   
  if (msg->domain       != NULL) {free(msg->domain);       msg->domain       = NULL;}
  if (msg->subject      != NULL) {free(msg->subject);      msg->subject      = NULL;}
  if (msg->type         != NULL) {free(msg->type);         msg->type         = NULL;}
  if (msg->text         != NULL) {free(msg->text);         msg->text         = NULL;}
  if (msg->sender       != NULL) {free(msg->sender);       msg->sender       = NULL;}
  if (msg->senderHost   != NULL) {free(msg->senderHost);   msg->senderHost   = NULL;}
  if (msg->receiver     != NULL) {free(msg->receiver);     msg->receiver     = NULL;}
  if (msg->receiverHost != NULL) {free(msg->receiverHost); msg->receiverHost = NULL;}
  
  if (msg->context.domain  != NULL) {free(msg->context.domain);  msg->context.domain  = NULL;}
  if (msg->context.subject != NULL) {free(msg->context.subject); msg->context.subject = NULL;}
  if (msg->context.type    != NULL) {free(msg->context.type);    msg->context.type    = NULL;}
  if (msg->context.udl     != NULL) {free(msg->context.udl);     msg->context.udl     = NULL;}
  if (msg->context.cueSize != NULL) {                            msg->context.cueSize = NULL;}
  
  /* remove compound payload */
  cMsgPayloadReset(vmsg);
  
  /* only free byte array if it was copied into the msg */
  if ((msg->byteArray != NULL) && ((msg->bits & CMSG_BYTE_ARRAY_IS_COPIED) > 0)) {
    free(msg->byteArray);
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees the memory of the components of a message,
 * but in a way which avoids mutex deadlock when recursively freeing a
 * payload's cMsgMessage items.
 *
 * @param vmsg address of pointer to message structure being freed
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if msg is NULL
 */   
static int freeMessage_r(void *vmsg) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
   
  if (msg->domain       != NULL) {free(msg->domain);       msg->domain       = NULL;}
  if (msg->subject      != NULL) {free(msg->subject);      msg->subject      = NULL;}
  if (msg->type         != NULL) {free(msg->type);         msg->type         = NULL;}
  if (msg->text         != NULL) {free(msg->text);         msg->text         = NULL;}
  if (msg->sender       != NULL) {free(msg->sender);       msg->sender       = NULL;}
  if (msg->senderHost   != NULL) {free(msg->senderHost);   msg->senderHost   = NULL;}
  if (msg->receiver     != NULL) {free(msg->receiver);     msg->receiver     = NULL;}
  if (msg->receiverHost != NULL) {free(msg->receiverHost); msg->receiverHost = NULL;}
  
  if (msg->context.domain  != NULL) {free(msg->context.domain);  msg->context.domain  = NULL;}
  if (msg->context.subject != NULL) {free(msg->context.subject); msg->context.subject = NULL;}
  if (msg->context.type    != NULL) {free(msg->context.type);    msg->context.type    = NULL;}
  if (msg->context.udl     != NULL) {free(msg->context.udl);     msg->context.udl     = NULL;}
  if (msg->context.cueSize != NULL) {                            msg->context.cueSize = NULL;}
  
  /* remove compound payload */
  cMsgPayloadReset_r(vmsg);
  
  /* only free byte array if it was copied into the msg */
  if ((msg->byteArray != NULL) && ((msg->bits & CMSG_BYTE_ARRAY_IS_COPIED) > 0)) {
    free(msg->byteArray);
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees the memory allocated in the creation of a message.
 * The cMsg client must call this routine on any messages created to avoid
 * memory leaks.
 *
 * @param vmsg address of pointer to message structure being freed
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if msg is NULL
 */   
int cMsgFreeMessage(void **vmsg) {
  int err;
  cMsgMessage_t *msg = (cMsgMessage_t *) (*vmsg);

  if ( (err = freeMessage(msg)) != CMSG_OK) {
    return err;
  }
  free(msg);
  *vmsg = NULL;

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees the memory allocated in the creation of a message,
 * but in a way which avoids mutex deadlock when recursively freeing a
 * payload's cMsgMessage items.
 *
 * @param vmsg address of pointer to message structure being freed
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if msg is NULL
 */   
int cMsgFreeMessage_r(void **vmsg) {
  int err;
  cMsgMessage_t *msg = (cMsgMessage_t *) (*vmsg);

  if ( (err = freeMessage_r(msg)) != CMSG_OK) {
    return err;
  }
  free(msg);
  *vmsg = NULL;

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine copies a message. Memory is allocated with this
 * function and can be freed by cMsgFreeMessage().
 * If the given message has a byte array that was copied in, it
 * is copied again into the new message. Otherwise, if it has a
 * byte array whose pointer was copied, the new message will point
 * to the same byte array.
 *
 * @param vmsg pointer to message structure being copied
 *
 * @returns a pointer to the message copy
 * @returns NULL if argument is NULL or no memory available
 */   
  void *cMsgCopyMessage(const void *vmsg) {
    int err, hasPayload=0;
    cMsgMessage_t *newMsg, *msg = (cMsgMessage_t *)vmsg;

    if (vmsg == NULL) {
      return NULL;
    }
    
    /* create a message structure */
    if ((newMsg = (cMsgMessage_t *)calloc(1, sizeof(cMsgMessage_t))) == NULL) {
      return NULL;
    }
    
    /*----------------*/
    /* copy over ints */
    /*----------------*/
    
    newMsg->version             = msg->version;
    newMsg->sysMsgId            = msg->sysMsgId;
    newMsg->info                = msg->info;
    newMsg->bits                = msg->bits;
    newMsg->reserved            = msg->reserved;
    newMsg->userInt             = msg->userInt;
    newMsg->userTime            = msg->userTime;
    newMsg->senderTime          = msg->senderTime;
    newMsg->senderToken         = msg->senderToken;
    newMsg->receiverTime        = msg->receiverTime;
    newMsg->receiverSubscribeId = msg->receiverSubscribeId;
    newMsg->byteArrayOffset     = msg->byteArrayOffset;
    newMsg->byteArrayLength     = msg->byteArrayLength;
    newMsg->historyLengthMax    = msg->historyLengthMax;
    newMsg->udpSend             = msg->udpSend;

    /*-------------------*/
    /* copy over strings */
    /*-------------------*/
    
    /* copy domain */
    if (msg->domain != NULL) {
        if ( (newMsg->domain = strdup(msg->domain)) == NULL) {
            freeMessage((void *)newMsg);
            free(newMsg);
            return NULL;
        }
    }
    else {
        newMsg->domain = NULL;
    }
        
        
    /* copy subject */
    if (msg->subject != NULL) {
        if ( (newMsg->subject = strdup(msg->subject)) == NULL) {
            freeMessage((void *)newMsg);
            free(newMsg);
            return NULL;
        }
    }
    else {
        newMsg->subject = NULL;
    }
        
    /* copy type */
    if (msg->type != NULL) {
        if ( (newMsg->type = strdup(msg->type)) == NULL) {
            freeMessage((void *)newMsg);
            free(newMsg);
            return NULL;
        }
    }
    else {
        newMsg->type = NULL;
    }
        
    /* copy text */
    if (msg->text != NULL) {
        if ( (newMsg->text = strdup(msg->text)) == NULL) {
            freeMessage((void *)newMsg);
            free(newMsg);
            return NULL;
        }
    }
    else {
        newMsg->text = NULL;
    }
    
    /* copy sender */
    if (msg->sender != NULL) {
        if ( (newMsg->sender = strdup(msg->sender)) == NULL) {
            freeMessage((void *)newMsg);
            free(newMsg);
            return NULL;
        }
    }
    else {
        newMsg->sender = NULL;
    }
    
    /* copy senderHost */
    if (msg->senderHost != NULL) {
        if ( (newMsg->senderHost = strdup(msg->senderHost)) == NULL) {
            freeMessage((void *)newMsg);
            free(newMsg);
            return NULL;
        }
    }
    else {
        newMsg->senderHost = NULL;
    }
        
    /* copy receiver */
    if (msg->receiver != NULL) {
        if ( (newMsg->receiver = strdup(msg->receiver)) == NULL) {
            freeMessage((void *)newMsg);
            free(newMsg);
            return NULL;
        }
    }
    else {
        newMsg->receiver = NULL;
    }
        
    /* copy receiverHost */
    if (msg->receiverHost != NULL) {
        if ( (newMsg->receiverHost = strdup(msg->receiverHost)) == NULL) {
            freeMessage((void *)newMsg);
            free(newMsg);
            return NULL;
        }
    }
    else {
        newMsg->receiverHost = NULL;
    }

    /*----------------------------*/
    /* copy over compound payload */
    /*----------------------------*/
    err = cMsgHasPayload(vmsg, &hasPayload);
    if (err == CMSG_OK && hasPayload) {
      err = cMsgPayloadCopy(vmsg, newMsg);
      if (err != CMSG_OK) {
          freeMessage((void *)newMsg);
          free(newMsg);
          return NULL;         
      }
    }
    
    /*-----------------------*/
    /* copy over binary data */
    /*-----------------------*/
    
    /* copy byte array */
    if (msg->byteArray != NULL) {
      /* if byte array was copied into msg, copy it again */
      if ((msg->bits & CMSG_BYTE_ARRAY_IS_COPIED) > 0) {
        newMsg->byteArray = (char *) malloc((size_t)msg->byteArrayLengthFull);
        if (newMsg->byteArray == NULL) {
            freeMessage((void *)newMsg);
            free(newMsg);
            return NULL;
        }
        memcpy(newMsg->byteArray, (void *)msg->byteArray,
                                  (size_t)msg->byteArrayLengthFull);
      }
      /* else copy pointer only */
      else {
        newMsg->byteArray = msg->byteArray;
      }
    }
    else {
      newMsg->byteArray = NULL;
    }

    /*-------------------------------------------------------*/
    /* copy over context (only important inside of callback) */
    /*-------------------------------------------------------*/
    
    newMsg->context.cueSize = msg->context.cueSize;
    if (msg->context.domain != NULL) {
      if ( (newMsg->context.domain = strdup(msg->context.domain)) == NULL) {
        freeMessage((void *)newMsg);
        free(newMsg);
        return NULL;
      }
    }
    if (msg->context.subject != NULL) {
      if ( (newMsg->context.subject = strdup(msg->context.subject)) == NULL) {
        freeMessage((void *)newMsg);
        free(newMsg);
        return NULL;
      }
    }
    if (msg->context.type != NULL) {
      if ( (newMsg->context.type = strdup(msg->context.type)) == NULL) {
        freeMessage((void *)newMsg);
        free(newMsg);
        return NULL;
      }
    }
    if (msg->context.udl != NULL) {
      if ( (newMsg->context.udl = strdup(msg->context.udl)) == NULL) {
        freeMessage((void *)newMsg);
        free(newMsg);
        return NULL;
      }
    }

    return (void *)newMsg;
  }


/*-------------------------------------------------------------------*/


/**
 * This routine initializes a message. It frees all allocated memory,
 * sets all strings to NULL, and sets all numeric values to their default
 * state.
 *
 * @param vmsg pointer to message structure being initialized
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if msg is NULL
 */   
int cMsgInitMessage(void *vmsg) {
    int err;
    cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

    if ( (err = freeMessage(msg)) != CMSG_OK) {
      return err;
    }
    
    initMessage(msg);
    return(CMSG_OK);
  }


/*-------------------------------------------------------------------*/


/**
 * This routine creates a new, initialized message. Memory is allocated with this
 * function and can be freed by cMsgFreeMessage().
 *
 * @returns a pointer to the new message
 * @returns NULL if no memory available
 */   
void *cMsgCreateMessage(void) {
  cMsgMessage_t *msg;
  
  msg = (cMsgMessage_t *) malloc(sizeof(cMsgMessage_t));
  if (msg == NULL) return NULL;
  /* initialize the memory */
  initMessage(msg);
  
  return((void *)msg);
}


/*-------------------------------------------------------------------*/


/**
 * This routine copies the given message, clears the history,
 * and is marked as NOT having been sent.
 * Memory is allocated with this function and can be freed by cMsgFreeMessage().
 *
 * @param vmsg pointer to message being copied
 *
 * @returns a pointer to the new message
 * @returns NULL if no memory available or message argument is NULL
 */   
void *cMsgCreateNewMessage(const void *vmsg) {  
    cMsgMessage_t *newMsg;
    
    if (vmsg == NULL) return NULL;  
    
    if ((newMsg = (cMsgMessage_t *)cMsgCopyMessage(vmsg)) == NULL) {
      return NULL;
    }
    
    /* BUG BUG clear history here */
    
    newMsg->info = newMsg->info & ~CMSG_WAS_SENT;
    
    return (void *)newMsg;
}


/*-------------------------------------------------------------------*/


/**
 * This routine creates a new, initialized message with some fields
 * copied from the given message in order to make it a proper response
 * to a sendAndGet() request. Memory is allocated with this
 * function and can be freed by cMsgFreeMessage().
 *
 * @param vmsg pointer to message to which response fields are set
 *
 * @returns a pointer to the new message
 * @returns NULL if no memory available, message argument is NULL, or
 *               message argument is not from calling sendAndGet()
 */   
void *cMsgCreateResponseMessage(const void *vmsg) {
    cMsgMessage_t *newMsg;
    cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
    
    if (vmsg == NULL) return NULL;
    
    /* if message is not a get request ... */
    if (!(msg->info & CMSG_IS_GET_REQUEST)) return NULL;
    
    if ((newMsg = (cMsgMessage_t *)cMsgCreateMessage()) == NULL) {
      return NULL;
    }
    
    newMsg->senderToken = msg->senderToken;
    newMsg->sysMsgId    = msg->sysMsgId;
    newMsg->info        = CMSG_IS_GET_RESPONSE;
    newMsg->subject     = strdup("dummy");
    newMsg->type        = strdup("dummy");

    return (void *)newMsg;
}


/*-------------------------------------------------------------------*/


/**
 * This routine creates a new, initialized message with some fields
 * copied from the given message in order to make it a proper "NULL"
 * (or no message) response to a sendAndGet() request.
 * Memory is allocated with this function and can be freed by
 * cMsgFreeMessage().
 *
 * @param vmsg pointer to message to which response fields are set
 *
 * @returns a pointer to the new message
 * @returns NULL if no memory available, message argument is NULL, or
 *               message argument is not from calling sendAndGet()
 */   
void *cMsgCreateNullResponseMessage(const void *vmsg) {
    cMsgMessage_t *newMsg;
    cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
    
    if (vmsg == NULL) return NULL;  
    
    /* if message is not a get request ... */
    if (!(msg->info & CMSG_IS_GET_REQUEST)) return NULL;
    
    if ((newMsg = (cMsgMessage_t *)cMsgCreateMessage()) == NULL) {
      return NULL;
    }
    
    newMsg->senderToken = msg->senderToken;
    newMsg->sysMsgId    = msg->sysMsgId;
    newMsg->info        = CMSG_IS_GET_RESPONSE | CMSG_IS_NULL_GET_RESPONSE;
    newMsg->subject     = strdup("dummy");
    newMsg->type        = strdup("dummy");
    
    return (void *)newMsg;
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/


/**
 * This routine gets the maximum number of entries this message keeps
 * of its history of various parameters (sender's name, host, time).
 *
 * @param vmsg pointer to message
 * @param len integer pointer to be filled inwith max number of entries this
 *            message keeps of its history of various parameters
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetHistoryLengthMax(const void *vmsg, int *len) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || len == NULL) return(CMSG_BAD_ARGUMENT);
  *len = msg->historyLengthMax;
  return(CMSG_OK);
}

/**
 * This routine sets the maximum number of entries this message keeps
 * of its history of various parameters (sender's name, host, time).
 *
 * @param vmsg pointer to message
 * @param len max number of entries this message keeps
 *            of its history of various parameters
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message argument is NULL
 * @returns CMSG_OUT_OF_RANGE if len < 0 or > CMSG_HISTORY_LENGTH_ABS_MAX
 */
int cMsgSetHistoryLengthMax(void *vmsg, int len) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (len < 0 || len > CMSG_HISTORY_LENGTH_ABS_MAX) {
    return(CMSG_OUT_OF_RANGE);
  }
  msg->historyLengthMax = len;
  
  return(CMSG_OK);
}

    
/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/


/**
 * This routine returns whether a message has been sent over the wire or not. 
 *
 * @param vmsg pointer to message
 * @param hasBeenSent pointer which gets filled with 1 if msg has been sent, else 0
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgWasSent(const void *vmsg, int *hasBeenSent) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || hasBeenSent == NULL) return(CMSG_BAD_ARGUMENT);
  
  *hasBeenSent = ((msg->info & CMSG_WAS_SENT) == CMSG_WAS_SENT) ? 1 : 0;

  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/


/**
 * This routine gets the cMsg major version number of a message.
 *
 * @param vmsg pointer to message
 * @param version integer pointer to be filled in with cMsg major version
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetVersion(const void *vmsg, int *version) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || version == NULL) return(CMSG_BAD_ARGUMENT);
  *version = msg->version;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the "get response" field of a message. The "get
 * reponse" field indicates the message is a response to a message sent
 * by a sendAndGet call, if it has a value of 1. Any other value indicates
 * it is not a response to a sendAndGet.
 *
 * @param vmsg pointer to message
 * @param getResponse set to 1 if message is a response to a sendAndGet,
 *                    anything else otherwise
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message argument is NULL
 */   
int cMsgSetGetResponse(void *vmsg, int getResponse) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  msg->info = getResponse ? msg->info |  CMSG_IS_GET_RESPONSE :
                            msg->info & ~CMSG_IS_GET_RESPONSE;

  return(CMSG_OK);
}

/**
 * This routine gets the "get response" field of a message. The "get
 * reponse" field indicates the message is a response to a message sent
 * by a sendAndGet call, if it has a value of 1. A value of 0 indicates
 * it is not a response to a sendAndGet.
 *
 * @param vmsg pointer to message
 * @param getResponse integer pointer to be filled in 1 if message
 *                    is a response to a sendAndGet and 0 otherwise
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetGetResponse(const void *vmsg, int *getResponse) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || getResponse == NULL) return(CMSG_BAD_ARGUMENT);
  *getResponse = (msg->info & CMSG_IS_GET_RESPONSE) == CMSG_IS_GET_RESPONSE ? 1 : 0;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the "null get response" field of a message. If it
 * has a value of 1, the "null get response" field indicates that if the
 * message is a response to a message sent by a sendAndGet call, when sent
 * it will be received as a NULL pointer - not a message. Any other value
 * indicates it is not a null get response to a sendAndGet.
 *
 * @param vmsg pointer to message
 * @param nullGetResponse set to 1 if message is a null get response to a
 *                        sendAndGet, anything else otherwise
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message argument is NULL
 */   
int cMsgSetNullGetResponse(void *vmsg, int nullGetResponse) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  msg->info = nullGetResponse ? msg->info |  CMSG_IS_NULL_GET_RESPONSE :
                                msg->info & ~CMSG_IS_NULL_GET_RESPONSE;

  return(CMSG_OK);
}

/**
 * This routine gets the "NULL get response" field of a message. If it
 * has a value of 1, the "NULL get response" field indicates that if the
 * message is a response to a message sent by a sendAndGet call, when sent
 * it will be received as a NULL pointer - not a message. Any other value
 * indicates it is not a null get response to a sendAndGet.
 *
 * @param vmsg pointer to message
 * @param nullGetResponse integer pointer to be filled in with 1 if message
 *                        is a NULL response to a sendAndGet and 0 otherwise
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetNullGetResponse(const void *vmsg, int *nullGetResponse) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || nullGetResponse == NULL) return(CMSG_BAD_ARGUMENT);
  *nullGetResponse = (msg->info & CMSG_IS_NULL_GET_RESPONSE) == CMSG_IS_NULL_GET_RESPONSE ? 1 : 0;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the "get request" field of a message. The "get
 * request" field indicates the message was sent by a sendAndGet call,
 * if it has a value of 1. A value of 0 indicates it was not sent by
 * a sendAndGet.
 *
 * @param vmsg pointer to message
 * @param getRequest integer pointer to be filled in with 1 if message
 *                   sent by a sendAndGet and 0 otherwise
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetGetRequest(const void *vmsg, int *getRequest) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || getRequest == NULL) return(CMSG_BAD_ARGUMENT);
  *getRequest = (msg->info & CMSG_IS_GET_REQUEST) == CMSG_IS_GET_REQUEST ? 1 : 0;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the domain of a message. When a message is newly
 * created (eg. by cMsgCreateMessage()), the domain field of a message
 * is not set. In the cMsg domain, the cMsg server sets this field
 * when it receives a client's sent message.
 * Messages received from the server will have this field set.
 * If successful, this routine will return a pointer to char inside the
 * message structure. The user may NOT write to this memory location!
 *
 * @param vmsg pointer to message
 * @param domain pointer to pointer filled with message's cMsg domain
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetDomain(const void *vmsg, const char **domain) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || domain == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->domain == NULL) {
    *domain = NULL;
  }
  else {
    *domain = msg->domain;
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the payload text of a message.
 * If successful, this routine will return a pointer to char inside the
 * message structure. The user may NOT write to this memory location!
 *
 * @param vmsg pointer to message
 * @param payloadText pointer to pointer filled with message's payload text
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetPayloadText(const void *vmsg, const char **payloadText) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || payloadText == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->payloadText == NULL) {
    *payloadText = NULL;
  }
  else {
    *payloadText = msg->payloadText;
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the subject of a message.
 *
 * @param vmsg pointer to message
 * @param subject message subject
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgSetSubject(void *vmsg, const char *subject) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->subject != NULL) free(msg->subject);  
  if (subject == NULL) {
    msg->subject = NULL;    
  }
  else {
    msg->subject = strdup(subject);
  }

  return(CMSG_OK);
}

/**
 * This routine gets the subject of a message.
 * If successful, this routine will return a pointer to char inside the
 * message structure. The user may NOT write to this memory location!
 *
 * @param vmsg pointer to message
 * @param subject pointer to pointer filled with message's subject
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetSubject(const void *vmsg, const char **subject) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || subject == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->subject == NULL) {
    *subject = NULL;
  }
  else {
    *subject = msg->subject;
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the type of a message.
 *
 * @param vmsg pointer to message
 * @param type message type
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgSetType(void *vmsg, const char *type) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->type != NULL) free(msg->type);
  if (type == NULL) {
    msg->type = NULL;    
  }
  else {
    msg->type = strdup(type);
  }

  return(CMSG_OK);
}

/**
 * This routine gets the type of a message.
 * If successful, this routine will return a pointer to char inside the
 * message structure. The user may NOT write to this memory location!
 *
 * @param vmsg pointer to message
 * @param type pointer to pointer filled with message's type
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetType(const void *vmsg, const char **type) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || type == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->type == NULL) {
    *type = NULL;
  }
  else {
    *type = msg->type;
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the text of a message.
 *
 * @param vmsg pointer to message
 * @param text message text
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgSetText(void *vmsg, const char *text) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->text != NULL) free(msg->text);
  if (text == NULL) {
    msg->text = NULL;    
  }
  else {
    msg->text = strdup(text);
  }

  return(CMSG_OK);
}

/**
 * This routine gets the text of a message.
 * If successful, this routine will return a pointer to char inside the
 * message structure. The user may NOT write to this memory location!
 *
 * @param vmsg pointer to message
 * @param text pointer to pointer filled with a message's text
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetText(const void *vmsg, const char **text) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || text == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->text == NULL) {
    *text = NULL;
  }
  else {
    *text = msg->text;
  }
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets a message's user-defined integer.
 *
 * @param vmsg pointer to message
 * @param userInt message's user-defined integer
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgSetUserInt(void *vmsg, int userInt) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  msg->userInt = userInt;

  return(CMSG_OK);
}

/**
 * This routine gets a message's user-defined integer.
 *
 * @param vmsg pointer to message
 * @param userInt integer pointer to be filled with message's user-defined integer
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetUserInt(const void *vmsg, int *userInt) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || userInt == NULL) return(CMSG_BAD_ARGUMENT);
  *userInt = msg->userInt;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets a message's user-defined time (in seconds since
 * midnight GMT, Jan 1st, 1970).
 *
 * @param vmsg pointer to message
 * @param userTime message's user-defined time
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgSetUserTime(void *vmsg, const struct timespec *userTime) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  msg->userTime = *userTime;

  return(CMSG_OK);
}

/**
 * This routine gets a message's user-defined time (in seconds since
 * midnight GMT, Jan 1st, 1970).
 *
 * @param vmsg pointer to message
 * @param userTime time_t pointer to be filled with message's user-defined time
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetUserTime(const void *vmsg, struct timespec *userTime) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || userTime == NULL) return(CMSG_BAD_ARGUMENT);
  *userTime = msg->userTime;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the endianness of the byte array data.
 * Valid values are:
 *<ul>
 *<li>{@link CMSG_ENDIAN_BIG}
 *<li>{@link CMSG_ENDIAN_LITTLE}
 *<li>{@link CMSG_ENDIAN_LOCAL}
 *<li>{@link CMSG_ENDIAN_NOTLOCAL}
 *<li>{@link CMSG_ENDIAN_SWITCH}
 *</ul>
 *
 * @param vmsg pointer to message
 * @param endian byte array's endianness
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if local endianness is unknown
 * @returns CMSG_BAD_ARGUMENT if message is NULL, or endian is not equal to either
 *                            CMSG_ENDIAN_BIG,   CMSG_ENDIAN_LITTLE,
 *                            CMSG_ENDIAN_LOCAL, CMSG_ENDIAN_NOTLOCAL, or
 *                            CMSG_ENDIAN_SWITCH 
 */   
int cMsgSetByteArrayEndian(void *vmsg, int endian) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  int ndian;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
    
  if ((endian != CMSG_ENDIAN_BIG)   && (endian != CMSG_ENDIAN_LITTLE)   &&
      (endian != CMSG_ENDIAN_LOCAL) && (endian != CMSG_ENDIAN_NOTLOCAL) &&
      (endian != CMSG_ENDIAN_SWITCH)) {
      return(CMSG_BAD_ARGUMENT);
  }
  
  /* set to local endian value */
  if (endian == CMSG_ENDIAN_LOCAL) {
      if (cMsgNetLocalByteOrder(&ndian) != CMSG_OK) {
          return CMSG_ERROR;
      }
      if (ndian == CMSG_ENDIAN_BIG) {
          msg->info |= CMSG_IS_BIG_ENDIAN;
      }
      else {
          msg->info &= ~CMSG_IS_BIG_ENDIAN;
      }
  }
  /* set to opposite of local endian value */
  else if (endian == CMSG_ENDIAN_NOTLOCAL) {
      if (cMsgNetLocalByteOrder(&ndian) != CMSG_OK) {
          return CMSG_ERROR;
      }
      if (ndian == CMSG_ENDIAN_BIG) {
          msg->info &= ~CMSG_IS_BIG_ENDIAN;
      }
      else {
          msg->info |= CMSG_IS_BIG_ENDIAN;
      }
  }
  /* switch endian value from big to little or vice versa */
  else if (endian == CMSG_ENDIAN_SWITCH) {
      /* if big switch to little */
      if ((msg->info & CMSG_IS_BIG_ENDIAN) > 1) {
          msg->info &= ~CMSG_IS_BIG_ENDIAN;
      }
      /* else switch to big */
      else {
          msg->info |= CMSG_IS_BIG_ENDIAN;
      }
  }
  /* set to big endian */
  else if (endian == CMSG_ENDIAN_BIG) {
      msg->info |= CMSG_IS_BIG_ENDIAN;
  }
  /* set to little endian */
  else if (endian == CMSG_ENDIAN_LITTLE) {
      msg->info &= ~CMSG_IS_BIG_ENDIAN;
  }

  return(CMSG_OK);
}

/**
 * This routine gets the endianness of the byte array data.
 * Valid returned values are:
 *<ul>
 *<li>{@link CMSG_ENDIAN_BIG}
 *<li>{@link CMSG_ENDIAN_LITTLE}
 *</ul>
 *
 * @param vmsg pointer to message
 * @param endian int pointer to be filled with byte array data endianness
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetByteArrayEndian(const void *vmsg, int *endian) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || endian == NULL) return(CMSG_BAD_ARGUMENT);
  
  if ((msg->info & CMSG_IS_BIG_ENDIAN) > 1) {
      *endian = CMSG_ENDIAN_BIG;
  }
  else {
      *endian = CMSG_ENDIAN_LITTLE;
  }

  return (CMSG_OK);
}

/**
 * This method specifies whether the endian value of the byte array is
 * the same value as the local host. If not, a 1 is returned indicating
 * that the data needs to be swapped. If so, a 0 is returned indicating
 * that no swap is needed.
 *
 * @param vmsg pointer to message
 * @param swap int pointer to be filled with 1 if byte array needs swapping, else 0
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if local endianness is unknown
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */
int cMsgNeedToSwap(const void *vmsg, int *swap) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  int localEndian, msgEndian;
  
  if (msg == NULL || swap == NULL) return(CMSG_BAD_ARGUMENT);
  
  /* find local host's byte order */ 
  if (cMsgNetLocalByteOrder(&localEndian) != CMSG_OK) return CMSG_ERROR;
  
  /* find messge byte array's byte order */
  if ((msg->info & CMSG_IS_BIG_ENDIAN) > 1) {
      msgEndian = CMSG_ENDIAN_BIG;
  }
  else {
      msgEndian = CMSG_ENDIAN_LITTLE;
  }
  
  if (localEndian == msgEndian) {
      *swap = 0;
  }
  else {
      *swap = 1;
  }
  
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the region-of-interest length of a message's byte array.
 * This may be smaller than the full length of the array if the user is
 * only interested in a portion of the array. If the byte array is null,
 * all non-negative values are accepted.
 *
 * @param vmsg pointer to message
 * @param length byte array's length (in bytes)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_OUT_OF_RANGE if offset + length is beyond array bounds
 * @returns CMSG_BAD_ARGUMENT if message is NULL or length < 0
 */   
int cMsgSetByteArrayLength(void *vmsg, int length) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (length < 0 || msg == NULL) return(CMSG_BAD_ARGUMENT);
  
  if (msg->byteArray == NULL) {
    msg->byteArrayLength = length;
    return(CMSG_OK);
  }
    
  if ((msg->byteArrayOffset + length) > msg->byteArrayLengthFull) {
    return(CMSG_OUT_OF_RANGE);
  }
  
  msg->byteArrayLength = length;
  return(CMSG_OK);
}

/**
 * This routine resets the region-of-interest length of a message's byte array
 * to its total length or zero if there is none.
 *
 * @param vmsg pointer to message
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgResetByteArrayLength(void *vmsg) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);

  if (msg->byteArray == NULL) {
    msg->byteArrayLength = 0;
    return(CMSG_OK);
  }
   
  msg->byteArrayLength = msg->byteArrayLengthFull;
  return(CMSG_OK);
}

/**
 * This routine gets the region-of-interest length of a message's byte array.
 *
 * @param vmsg pointer to message
 * @param length int pointer to be filled with byte array length (in bytes)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetByteArrayLength(const void *vmsg, int *length) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || length == NULL) return(CMSG_BAD_ARGUMENT);
  *length = msg->byteArrayLength;
  return (CMSG_OK);
}

/**
 * This routine gets the total length of a message's byte array.
 *
 * @param vmsg pointer to message
 * @param length int pointer to be filled with byte array's total length (in bytes)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetByteArrayLengthFull(const void *vmsg, int *length) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || length == NULL) return(CMSG_BAD_ARGUMENT);
  *length = msg->byteArrayLengthFull;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the region-of-interest offset of a message's byte array.
 * This may be non-zero if the user is only interested in a portion of
 * the array. If the byte array is null, all non-negative values are accepted.
 *
 * @param vmsg pointer to message
 * @param offset byte array's offset index
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_OUT_OF_RANGE if offset + length is beyond array bounds
 * @returns CMSG_BAD_ARGUMENT if message is NULL or offset < 0
 */   
int cMsgSetByteArrayOffset(void *vmsg, int offset) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (offset < 0 || msg == NULL) return(CMSG_BAD_ARGUMENT);
  
  if (msg->byteArray == NULL) {
    msg->byteArrayOffset = offset;
    return(CMSG_OK);
  }
    
  if ((msg->byteArrayLength + offset) > msg->byteArrayLengthFull) {
    return(CMSG_OUT_OF_RANGE);
  }
    
  msg->byteArrayOffset = offset;

  return(CMSG_OK);
}

/**
 * This routine gets the region-of-interest offset of a message's byte array.
 *
 * @param vmsg pointer to message
 * @param offset int pointer to be filled with byte array offset index
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetByteArrayOffset(const void *vmsg, int *offset) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || offset == NULL) return(CMSG_BAD_ARGUMENT);
  *offset = msg->byteArrayOffset;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets a message's byte array by copying the array arg
 * pointer but <b>NOT</b> the data pointed to. The length arg sets the total
 * length of the array in bytes. Any pre-existing byte array data is freed
 * if it was copied into the given message. If the given array is null,
 * the message's byte array is set to null and both offset & length are
 * set to 0.
 *
 * @param vmsg pointer to message
 * @param array byte array
 * @param length number of bytes in array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL or length is < 0
 */   
int cMsgSetByteArrayNoCopy(void *vmsg, char *array, int length) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || length < 0) return(CMSG_BAD_ARGUMENT);

  /* if there is a pre-existing array that was copied in, free it */
  if (msg->bits & CMSG_BYTE_ARRAY_IS_COPIED) {
    if (msg->byteArray != NULL) free(msg->byteArray);
  }

  if (array == NULL) {
    msg->byteArray           = NULL;
    msg->byteArrayOffset     = 0;
    msg->byteArrayLength     = 0;
    msg->byteArrayLengthFull = 0;
    return(CMSG_OK);
  }
  
  msg->bits &= ~CMSG_BYTE_ARRAY_IS_COPIED; /* byte array is NOT copied */
  msg->byteArray           = array;
  msg->byteArrayOffset     = 0;
  msg->byteArrayLength     = length;
  msg->byteArrayLengthFull = length;

  return(CMSG_OK);
}


/**
 * This routine sets a message's byte array by copying "length" number
 * of bytes into a newly allocated array. The offset is reset to 0
 * while the length is set to the given value. Any pre-existing byte array
 * memory is freed if it was copied into the given message. If the given
 * array is null, the message's byte array is set to null and both
 * offset & length are set to 0.
 *
 * @param vmsg pointer to message
 * @param array byte array
 * @param length number of bytes in array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL, or length < 0
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 */
int cMsgSetByteArray(void *vmsg, char *array, int length) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || length < 0) return(CMSG_BAD_ARGUMENT);

  /* if there is a pre-existing array that was copied in, free it */
  if (msg->bits & CMSG_BYTE_ARRAY_IS_COPIED) {
    if (msg->byteArray != NULL) free(msg->byteArray);
  }
  
  if (array == NULL) {
    msg->byteArray           = NULL;
    msg->byteArrayOffset     = 0;
    msg->byteArrayLength     = 0;
    msg->byteArrayLengthFull = 0;
    return(CMSG_OK);
  }
  
  msg->byteArray = (char *) malloc((size_t)length);
  if (msg->byteArray == NULL) {
    return (CMSG_OUT_OF_MEMORY);
  }

  memcpy(msg->byteArray, (void *)array, (size_t)length);
  msg->bits |= CMSG_BYTE_ARRAY_IS_COPIED; /* byte array IS copied */
  msg->byteArrayOffset     = 0;
  msg->byteArrayLength     = length;
  msg->byteArrayLengthFull = length;

  return(CMSG_OK);
}


/**
 * This routine gets a message's byte array.
 *
 * @param vmsg pointer to message
 * @param array pointer to be filled with byte array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetByteArray(const void *vmsg, char **array) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || array == NULL) return(CMSG_BAD_ARGUMENT);
  *array = msg->byteArray;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the sender of a message.
 * If successful, this routine will return a pointer to char inside the
 * message structure. The user may NOT write to this memory location!
 *
 * @param vmsg pointer to message
 * @param sender pointer to pointer filled with message's sender
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetSender(const void *vmsg, const char **sender) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || sender == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->sender == NULL) {
    *sender = NULL;
  }
  else {
    *sender = msg->sender;
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the host of the sender of a message.
 * If successful, this routine will return a pointer to char inside the
 * message structure. The user may NOT write to this memory location!
 *
 * @param vmsg pointer to message
 * @param senderHost pointer to pointer filled with host of the message sender
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetSenderHost(const void *vmsg, const char **senderHost) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || senderHost == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->senderHost == NULL) {
    *senderHost = NULL;
  }
  else {
    *senderHost = msg->senderHost;
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the time a message was last sent (in seconds since
 * midnight GMT, Jan 1st, 1970).
 *
 * @param vmsg pointer to message
 * @param senderTime pointer to be filled with time message was last sent
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetSenderTime(const void *vmsg, struct timespec *senderTime) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || senderTime == NULL) return(CMSG_BAD_ARGUMENT);
  *senderTime = msg->senderTime;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the receiver of a message.
 * If successful, this routine will return a pointer to char inside the
 * message structure. The user may NOT write to this memory location!
 *
 * @param vmsg pointer to message
 * @param receiver pointer to pointer filled with message's receiver
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetReceiver(const void *vmsg, const char **receiver) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || receiver == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->receiver == NULL) {
    *receiver = NULL;
  }
  else {
    *receiver = msg->receiver;
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the host of the receiver of a message. This field
 * is NULL for a newly created message.
 * If successful, this routine will return a pointer to char inside the
 * message structure. The user may NOT write to this memory location!
 *
 * @param vmsg pointer to message
 * @param receiverHost pointer to pointer filled with host of the message receiver
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetReceiverHost(const void *vmsg, const char **receiverHost) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || receiverHost == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->receiverHost == NULL) {
    *receiverHost = NULL;
  }
  else {
    *receiverHost = msg->receiverHost;
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine gets the time a message was received (in seconds since
 * midnight GMT, Jan 1st, 1970).
 *
 * @param vmsg pointer to message
 * @param receiverTime pointer to be filled with time message was received
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetReceiverTime(const void *vmsg, struct timespec *receiverTime) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || receiverTime == NULL) return(CMSG_BAD_ARGUMENT);
  *receiverTime = msg->receiverTime;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine converts the message to an XML string.
 * Everything is displayed including binary.
 *
 * @param vmsg pointer to message
 * @param string is pointer to char* that will hold the malloc'd string
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if internal payload parsing error,
 *                     or cannot get a payload item's type or count
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 */   
int cMsgToString(const void *vmsg, char **string) {
    return cMsgToStringImpl(vmsg, string, 0, 0, 1, 0, 0, 0, 0, NULL);
}


/**
 * This routine converts the message to an XML string.
 *
 * @param vmsg pointer to message
 * @param string is pointer to char* that will hold the malloc'd string
 * @param binary includes binary as ASCII if true, else binary is ignored
 * @param compact if true (!=0), do not include attributes with null or default integer values
 * @param noSystemFields if true (!=0), do not include system (metadata) payload fields
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if internal payload parsing error,
 *                     or cannot get a payload item's type or count
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 */   
int cMsgToString2(const void *vmsg, char **string, int binary, int compact, int noSystemFields) {
    return cMsgToStringImpl(vmsg, string, 0, 0, binary, compact, 0, 0, noSystemFields, NULL);
}


/**
 * This routine converts the message payload to an XML string.
 *
 * @param vmsg pointer to message
 * @param string is pointer to char* that will hold the malloc'd string
 * @param binary includes binary as ASCII if true, else binary is ignored
 * @param compact if true (!=0), do not include attributes with null or default integer values
 * @param noSystemFields if true (!=0), do not include system (metadata) payload fields
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if internal payload parsing error,
 *                     or cannot get a payload item's type or count
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 */   
int cMsgPayloadToString(const void *vmsg, char **string, int binary, int compact, int noSystemFields) {
    return cMsgPayloadToStringImpl(vmsg, string, 0, 0, binary, compact, noSystemFields);
}


/*-------------------------------------------------------------------*/
/**
 * This routine calculate the amount of memory in bytes needed to hold the
 * message in an XML string.
 *
 * @param vmsg pointer to message
 * @param margin the beginning indentation (# of spaces)
 * @param binary includes binary as ASCII if true, else binary is ignored
 * @param compactPayload if true (!=0), includes payload only as a single string (internal format)
 * @param onlyPayload if true, return memory needed to hold only payload as an XML string
 *
 * @returns number of bytes if successful
 * @returns -1 if error
 */   
static int messageStringSize(const void *vmsg, int margin, int binary,
                             int compactPayload, int onlyPayload) {
    payloadItem *item;
    int i, totalLen=0, len, hasPayload;
      
    int payloadLen=0;                       /* the whole payload length in chars */
    int formatItemStrLen   = 50 + margin;   /* Max XML chars surrounding single string item minus fields */
    int formatItemBinLen   = 60 + margin;   /* Max XML chars surrounding binary item minus fields */
    int formatItemNumLen   = 70 + 2*margin; /* Max XML chars surrounding numeric item/array minus fields */
     
    /* non-payload stuff */
    int formatItemTxtLen   = 40 + margin;   /* Max XML chars surrounding text element */
    int formatItemBALen    = 65 + 2*margin; /* Max XML chars surrounding binary array element */
    int formatAttributeLen = 27*17;         /* XML chars in attributes and begin & end XML
                                              (not including values and text, bin array, payload) */

    cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
    if (msg == NULL) return(-1);

    if (!onlyPayload) {
        totalLen  = formatAttributeLen + 16*margin + formatItemTxtLen + formatItemBALen;
        totalLen += 3*30; /* user,sender,receiver times */
    
        /* add string lengths */
        if(msg->domain!=NULL)        totalLen += strlen(msg->domain);
        if(msg->sender!=NULL)        totalLen += strlen(msg->sender);
        if(msg->senderHost!=NULL)    totalLen += strlen(msg->senderHost);
        if(msg->receiver!=NULL)      totalLen += strlen(msg->receiver);
        if(msg->receiverHost!=NULL)  totalLen += strlen(msg->receiverHost);
        if(msg->subject!=NULL)       totalLen += strlen(msg->subject);
        if(msg->type!=NULL)          totalLen += strlen(msg->type);
        if(msg->text!=NULL)          totalLen += strlen(msg->text);
    
        if (binary && msg->byteArray != NULL && msg->byteArrayLength > 0) {
            totalLen += cMsg_b64_encode_len(msg->byteArray + msg->byteArrayOffset,
                                            (unsigned int)msg->byteArrayLength, 1);
        }
    }
    /* calculate payload length */

    /* is there a payload? */
    cMsgHasPayload(msg, &hasPayload);

    if (!hasPayload) {
    }
    else if (compactPayload) {
        payloadLen = (int)strlen(msg->payloadText) + 2*margin + 50;
    }
    else {
        item = msg->payload;
        while (item != NULL) {
            /* add length of XML surrounding each element */
            if (item->type == CMSG_CP_STR) {
                payloadLen += item->length + formatItemStrLen;
            }
            else if (item->type == CMSG_CP_STR_A) {
                payloadLen += item->length + formatItemNumLen + (formatItemStrLen + 5) * item->count;
            }
            else if (item->type == CMSG_CP_BIN) {
                payloadLen += formatItemBinLen;
                if (binary) {
                    /* this has already been converted into text and size is at most item->length */
                    payloadLen += item->length;
                }
            }
            else if (item->type == CMSG_CP_BIN_A) {
                payloadLen += formatItemBALen + 10 + (formatItemBinLen + 5) * item->count; 
                if (binary) {
                    /* this has already been converted into text and size is at most item->length */
                    payloadLen += item->length;
                }
            }
            else if (item->type == CMSG_CP_MSG) {
                len = messageStringSize(item->array, margin+5, binary, compactPayload, 0);
                /*printf("messageStringSize: margin %d msg size = %d\n",margin+5,len);*/
                if (len < 0) return(len);
                payloadLen += len;
            }
            else if (item->type == CMSG_CP_MSG_A) {
                void **msgs = (void **)item->array;
                for (i=0; i<item->count; i++) {
                    len = messageStringSize(msgs[i], margin+10 , binary, compactPayload, 0);
                    /*printf("messageStringSize: margin %d msg size = %d\n",margin+10,len);*/
                    if (len < 0) return(len);
                    payloadLen += len;
                }
                payloadLen += formatItemNumLen; /* estimate of surrounding XML for array */
            }
            /* if numbers or arrays of numbers ... */
            else {
                /* XML + (indent) * (# rows of 5 numbers each) */
                payloadLen += item->length + formatItemNumLen + (15 + margin)*(item->count/5 + 1);
                /* extra spaces in int8 and int16 for nice looking printout */
                if (item->type == CMSG_CP_INT8_A || item->type == CMSG_CP_UINT8_A) payloadLen += 2*item->count;
                else if (item->type == CMSG_CP_INT16_A || item->type == CMSG_CP_UINT16_A) payloadLen += 4*item->count;
            }
            item = item->next;
        }
    }
    
    /*printf("messageStringSize: margin %d, totalLen = %d, payload len = %d, total len = %d \n",
                   margin, totalLen, payloadLen, totalLen + payloadLen + 1024);*/
    totalLen += payloadLen;
    
    return(totalLen);
}

                    
/*-------------------------------------------------------------------*/

                    
/**
 * This routine escapes the " char for putting strings into XML.
 * If no quotes are found, the original string is returned
 * (no memory is allocated). If quotes are found, a new string
 * is returned which must be freed by the caller.
 * 
 * @param s string to be escaped
 * @return escaped string, NULL if no memory or arg is NULL
 */
char* escapeQuotesForXML(char *s) {
    char *pchar=s, *newString, *pcharDest, *sub="&#34;";
    int i, count=0;
    size_t slen, index, lenLeft;
    
    if (s == NULL) return NULL;
    slen = strlen(s);
    
    /* count quotes */
    while ( (pchar = strpbrk(pchar,"\"")) != NULL) {
        pchar++;
        count++;
    }
    if (count == 0) return s;

/*printf("escapeQuotesForXML: found %d quote(s) in string %s\n",count,s);*/
    newString = (char *) calloc(1, slen + count*4 + 1);
    if (newString == NULL) return NULL;
    
    pchar = s;
    pcharDest = newString;
    for (i=0; i<count; i++) {
        index = strcspn(pchar, "\"");
        memcpy(pcharDest, pchar, index);
        pcharDest += index;
        memcpy(pcharDest, sub, 5);
        pcharDest += 5;
        pchar += index + 1;
    }
    lenLeft = s+slen-pchar;
    if (lenLeft > 0) {
        memcpy(pcharDest, pchar, lenLeft);
    }
/*printf("escapeQuotesForXML: new string = %s\n", newString);*/
    
    return newString;
}

            
/*-------------------------------------------------------------------*/


/**
* This routine escapes CDATA constructs which will appear inside of XML CDATA sections.
* It is not possible to have nested cdata sections. Actually the CDATA ending
* sequence, ]]&gt; , is what cannot be containing in a surrounding CDATA section.
* This restriction can be
* cleverly circumvented by inserting "&lt;![CDATA[]]]]&gt;&lt;![CDATA[&gt;" after each CDATA ending
* sequence. This creates independent CDATA sections which are not nested.</p>
*
* This routine assumes that there are no nested cdata sections in the input string.
* Nothing is done to the string if:
* <UL>
* <LI>there is no ]]&gt; so no escaping is necessary
* <LI>there is no &lt;![CDATA[ so s contains malformed XML
* <LI>a particular &lt;![CDATA[ has no corresponding ]]&gt; so s contains malformed XML
* <LI>]]&gt; comes before &lt;![CDATA[ so s contains malformed XML
* </UL>
*
* @param s string to be escaped
* @return escaped string, NULL if no memory or arg is NULL
*/
char* escapeCdataForXML(char *s) {
    char *p1, *p2, *pchar=s, *newString, *pcharDest, *sub="<![CDATA[]]]]><![CDATA[>";
    int i, count=0;
    size_t slen, lenLeft;

    if (s == NULL) return NULL;
    
    /* don't need to escape anything */
    if ( (p1 = strstr(s, "]]>")) == NULL ) return s;
    
    /* ignore since malformed XML in s */
    if ( (p2 = strstr(s, "<![CDATA[")) == NULL ) return s;
    if (  p2 > p1 ) return s;
   
    slen = strlen(s);

    /* 1) count # of places we need to insert string, 2) check XML format */
    pchar = p2;
    while (pchar < s+slen) {
        p1 = strstr(pchar, "<![CDATA[");
        if (p1 == NULL) {
            /* check for another ]]>, if there is one, bad XML */
            p2 = strstr(pchar, "]]>");
            if (p2 != NULL) return s;
            break;
        }

        p2 = strstr(p1+9, "]]>");
        if (p2 == NULL) {
            /* no matching CDATA ending, malformed XML in s */
            return s;
        }

        count++;
        pchar = p2+3;
    }
    if (count == 0) return s;

/*printf("escapeCdataForXML: found %d CDATA structures(s) in string %s, allocate %d chars\n",
    count, s,slen + count*24 );*/
    newString = (char *) calloc(1, slen + count*24 + 1);
    if (newString == NULL) return NULL;

    p1 = s;
    pcharDest = newString;
    for (i=0; i<count; i++) {
        p2 = strstr(p1, "]]>") + 3;
        memcpy(pcharDest, p1, p2-p1);
        pcharDest += p2-p1;
        memcpy(pcharDest, sub, 24);
        pcharDest += 24;
        p1 = p2;
    }
    
    lenLeft = s+slen-p1;
    if (lenLeft > 0) {
        memcpy(pcharDest, p1, lenLeft);
    }
/*printf("escapeCdataForXML: new string = %s\n", newString);*/

    return newString;
}


/*-------------------------------------------------------------------*/


/**
 * This routine converts the message to an XML string.
 *
 * @param vmsg pointer to message
 * @param string is pointer to char* that will hold the malloc'd string if level = 0,
 *               otherwise this arg is the pointer to a pointer into an existing buffer
 *               which gets filled and which will change the pointer and return it as
 *               the end of its additions (for recursive messaging)
 * @param level the level of indent or recursive messaging (0 = none)
 * @param margin number of spaces in the indent
 * @param binary includes binary as ASCII if true (!=0), else binary is ignored
 * @param compact if true (!=0), do not include attributes with null or default integer values
 * @param compactPayload if true, includes payload only as a single string (internal format)
 * @param hasName if true, this message is in the payload and has a name
 * @param noSystemFields if true (!=0), do not include system (metadata) payload fields
 * @param itemName if in payload, name of payload item
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if internal payload parsing error,
 *                     or cannot get a payload item's type or count
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 */   
static int cMsgToStringImpl(const void *vmsg, char **string,
                            int level, int margin, int binary,
                            int compact, int compactPayload,
                            int hasName, int noSystemFields,
                            const char *itemName) {

  time_t now;
  int    j, err, len, count, endian, hasPayload, indentLen;
  size_t slen, offsetLen;
  char   *buf, *pchar, *indent, *offsett, *str;
  char   userTimeBuf[32],senderTimeBuf[32],receiverTimeBuf[32];
  struct tm tBuf;

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);

  /* get times in ascii */
  localtime_r(&msg->userTime.tv_sec, &tBuf);
  strftime(userTimeBuf, 32, CMSG_TIME_FORMAT, &tBuf);     userTimeBuf[strlen(userTimeBuf)]='\0';
  localtime_r(&msg->senderTime.tv_sec, &tBuf);
  strftime(senderTimeBuf, 32, CMSG_TIME_FORMAT, &tBuf);   senderTimeBuf[strlen(senderTimeBuf)]='\0';
  localtime_r(&msg->receiverTime.tv_sec, &tBuf);
  strftime(receiverTimeBuf, 32, CMSG_TIME_FORMAT, &tBuf); receiverTimeBuf[strlen(receiverTimeBuf)]='\0';

  /* Create the indent since a message may contain a message, etc. */
  if (margin < 1) {
      margin = 0;
      indent = "";
      indentLen = 0;
  }
  else {
      indent = (char *)malloc((size_t)(margin + 1));
      if (indent == NULL) {
        return(CMSG_OUT_OF_MEMORY);
      }
      memset(indent, '\040',(size_t) margin); /* ASCII space = char #32 (40 octal) */
      indent[margin] = '\0';
      indentLen = margin;
  }

  /* indentation of 5 spaces past margin */
  offsett = (char *)malloc((size_t)(margin+6));
  if (offsett == NULL) {
      if (margin > 0) free(indent);
      return(CMSG_OUT_OF_MEMORY);
  }
  memset(offsett, '\040',(size_t)(margin+5));
  offsett[margin+5] = '\0';
  offsetLen = (size_t)(margin+5);
 
 /* Allocate and zero buffer if first level only. */
  if (level < 1) {
      level = 0;
      /* find msg size */
      slen = (size_t)messageStringSize(vmsg, margin, binary, compactPayload, 0);
      /* add 5% (plus 100 bytes for little msgs) */
      slen += slen*5/100 + 100;
      /*printf("cMsgToStringImpl: length of buffer needed = %d\n", (int)slen);*/
      pchar = buf = (char*)calloc(1, slen);
      if (buf == NULL) {
          if (margin > 0) free(indent);
          return(CMSG_OUT_OF_MEMORY);
      }
  }
  /* Otherwise use an existing buffer. */
  else {
      pchar = *string;
      buf   = pchar;
  }

  /* Do we have a payload? */
  cMsgHasPayload(vmsg, &hasPayload);

  /* Main XML element */
  if (hasName) {
      strncpy(pchar, indent, (size_t)indentLen); pchar+=indentLen;
      strncpy(pchar,"<cMsgMessage name=\"",19); pchar+=19;
      slen = strlen(itemName);
      strncpy(pchar,itemName, slen); pchar+=slen;
      strncpy(pchar,"\"\n",2); pchar+=2;
  }
  else {
      strncpy(pchar, indent, (size_t)indentLen); pchar+=indentLen;
      strncpy(pchar,"<cMsgMessage\n",13); pchar+=13;
  }

  /*---------------------------------------------
   * print all attributes in a pretty form first
   *---------------------------------------------*/

  strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
  strncpy(pchar,"version           = \"",21); pchar+=21;
  sprintf(pchar, "%d%n", msg->version, &len); pchar+=len;
  strncpy(pchar,"\"\n",2); pchar+=2;

  strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
  strncpy(pchar,"userInt           = \"",21); pchar+=21;
  sprintf(pchar, "%d%n", msg->userInt, &len); pchar+=len;
  strncpy(pchar,"\"\n",2); pchar+=2;

  /* check if getRequest, if so, it cannot also be a getResponse */
  if ( msg->info & CMSG_IS_GET_REQUEST ) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"getRequest        = \"true\"\n",27); pchar+=27;
  }
  /* check if nullGetResponse, if so, then it's a getResponse too (no need to print) */
  else if ( msg->info & CMSG_IS_NULL_GET_RESPONSE ) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"nullGetResponse   = \"true\"\n",27); pchar+=27;
  }
  else {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      if ( msg->info & CMSG_IS_GET_RESPONSE ) {
          strncpy(pchar,"getResponse       = \"true\"\n",27); pchar+=27;
      }
      else {
          strncpy(pchar,"getResponse       = \"false\"\n",28); pchar+=28;
      }
  }

  /* domain */
  if (msg->domain != NULL) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"domain            = \"",21); pchar+=21;
      slen = strlen(msg->domain);
      strncpy(pchar , msg->domain, slen); pchar+=slen;
      strncpy(pchar,"\"\n",2); pchar+=2;
  }
  else if (!compact) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"domain            = \"(null)\"\n", 29); pchar+=29;
  }

  /* sender */
  if (msg->sender != NULL) {
      strncpy(pchar,offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"sender            = \"",21);  pchar+=21;
      str = escapeQuotesForXML(msg->sender);
      if (str == NULL) {
          if (margin > 0) free(indent);
          free(offsett);
          return CMSG_OUT_OF_MEMORY;
      }
      slen = strlen(str);
      strncpy(pchar, str, slen); pchar+=slen;
      strncpy(pchar,"\"\n",2); pchar+=2;
      if (str != msg->sender) free(str);
  }
  else if (!compact) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"sender            = \"(null)\"\n", 29); pchar+=29;
  }

  /* senderHost */
  if (msg->senderHost != NULL) {
      strncpy(pchar,offsett,offsetLen); pchar+=offsetLen;
      strncpy(pchar,"senderHost        = \"",21);  pchar+=21;
      str = escapeQuotesForXML(msg->senderHost);
      if (str == NULL) {
          if (margin > 0) free(indent);
          free(offsett);
          return CMSG_OUT_OF_MEMORY;
      }
      slen = strlen(str);
      strncpy(pchar, str, slen); pchar+=slen;
      strncpy(pchar,"\"\n",2); pchar+=2;
      if (str != msg->senderHost) free(str);
  }
  else if (!compact) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"senderHost        = \"(null)\"\n", 29); pchar+=29;
  }

  /* senderTime */
  if (!compact || msg->senderTime.tv_sec > 0) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"senderTime        = \"",21);  pchar+=21;
      slen = strlen(senderTimeBuf);
      strncpy(pchar, senderTimeBuf, slen); pchar+=slen;
      strncpy(pchar,"\"\n",2); pchar+=2;
  }

  /* receiver */
  if (msg->receiver != NULL) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"receiver          = \"",21);  pchar+=21;
      str = escapeQuotesForXML(msg->receiver);
      if (str == NULL) {
          if (margin > 0) free(indent);
          free(offsett);
          return CMSG_OUT_OF_MEMORY;
      }
      slen = strlen(str);
      strncpy(pchar, str, slen); pchar+=slen;
      strncpy(pchar,"\"\n",2); pchar+=2;
      if (str != msg->receiver) free(str);
  }
  else if (!compact) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"receiver          = \"(null)\"\n", 29); pchar+=29;
  }

  /* receiverHost */
  if (msg->receiverHost != NULL) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"receiverHost      = \"",21);  pchar+=21;
      str = escapeQuotesForXML(msg->receiverHost);
      if (str == NULL) {
          if (margin > 0) free(indent);
          free(offsett);
          return CMSG_OUT_OF_MEMORY;
      }
      slen = strlen(str);
      strncpy(pchar, str, slen); pchar+=slen;
      strncpy(pchar,"\"\n",2); pchar+=2;
      if (str != msg->receiverHost) free(str);
  }
  else if (!compact) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"receiverHost      = \"(null)\"\n", 29); pchar+=29;
  }

  /* receiverTime */
  if (!compact || msg->receiverTime.tv_sec > 0) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"receiverTime      = \"",21);  pchar+=21;
      slen = strlen(receiverTimeBuf);
      strncpy(pchar, receiverTimeBuf, slen); pchar+=slen;
      strncpy(pchar,"\"\n",2); pchar+=2;
  }

  if (!compact || msg->userTime.tv_sec > 0) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"userTime          = \"",21);  pchar+=21;
      slen = strlen(userTimeBuf);
      strncpy(pchar, userTimeBuf, slen); pchar+=slen;
      strncpy(pchar,"\"\n",2); pchar+=2;
  }

  /* subject */
  if (msg->subject != NULL) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"subject           = \"",21);  pchar+=21;
      str = escapeQuotesForXML(msg->subject);
      if (str == NULL) {
          if (margin > 0) free(indent);
          free(offsett);
          return CMSG_OUT_OF_MEMORY;
      }
      slen = strlen(str);
      strncpy(pchar, str, slen); pchar+=slen;
      strncpy(pchar,"\"\n",2); pchar+=2;
      if (str != msg->subject) free(str);
  }
  else if (!compact) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"subject           = \"(null)\"\n", 29); pchar+=29;
  }

  /* type */
  if (msg->type != NULL) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"type              = \"",21);  pchar+=21;      
      str = escapeQuotesForXML(msg->type);
      if (str == NULL) {
          if (margin > 0) free(indent);
          free(offsett);
          return CMSG_OUT_OF_MEMORY;
      }
      slen = strlen(str);
      strncpy(pchar, str, slen); pchar+=slen;
      strncpy(pchar,"\"\n",2); pchar+=2;
      if (str != msg->type) free(str);
  }
  else if (!compact) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"type              = \"(null)\"\n", 29); pchar+=29;
  }

  /* payload count */
  if (hasPayload) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"payloadItemCount  = \"",21); pchar+=21;
      sprintf(pchar, "%d%n", msg->payloadCount, &len); pchar+=len;
      strncpy(pchar,"\"\n",2); pchar+=2;
  }

  /* end of attributes, add > */
  pchar--;
  strncpy(pchar,">\n",2); pchar+=2;

 /*-------------------------------------
  * print all elements in a pretty form
  *-------------------------------------*/
 
  /* Text */
  if (msg->text != NULL) {
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"<text><![CDATA[",15); pchar+=15;
      str = escapeCdataForXML(msg->text);
      if (str == NULL) {
          if (margin > 0) free(indent);
          free(offsett);
          return CMSG_OUT_OF_MEMORY;
      }
      slen = strlen(str);
      strncpy(pchar,str, slen); pchar+=slen;
      strncpy(pchar,"]]></text>\n",11); pchar+=11;
      if (str != msg->text) free(str);
  }
        
  /* Binary array */
  if (binary && (msg->byteArray != NULL) && (msg->byteArrayLength > 0)) {
      cMsgGetByteArrayEndian(vmsg, &endian);
      
      strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
      strncpy(pchar,"<binary endian=\"",16); pchar+=16;
      if (endian == CMSG_ENDIAN_BIG) {
          strncpy(pchar,"big",3); pchar+=3;
      }
      else {
          strncpy(pchar,"little",6); pchar+=6;
      }
      strncpy(pchar,"\" nbytes=\"",10); pchar+=10;
      sprintf(pchar, "%d%n", msg->byteArrayLength, &len); pchar+=len;

      /* put in line breaks after 76 chars (57 bytes) */
      if (msg->byteArrayLength > 57) {
          strncpy(pchar,"\">\n",3); pchar+=3;
          count = cMsg_b64_encode(msg->byteArray + msg->byteArrayOffset,
                                  (unsigned int)msg->byteArrayLength, pchar, 1); pchar+=count;
          strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
          strncpy(pchar,"</binary>\n",10);  pchar+=10;
      }
      else {
          strncpy(pchar,"\">",2); pchar+=2;
          count = cMsg_b64_encode(msg->byteArray + msg->byteArrayOffset,
                                  (unsigned int)msg->byteArrayLength, pchar, 0); pchar+=count;
          strncpy(pchar,"</binary>\n",10); pchar+=10;
      }
  }

  /* Payload */
  if (hasPayload) {
      if (compactPayload) {
          strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
          strncpy(pchar,"<payload compact=\"true\">\n",25); pchar+=25;
          slen = strlen(msg->payloadText);
          strncpy(pchar, msg->payloadText, slen); pchar+=slen;
          strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
          strncpy(pchar,"</payload>\n",11); pchar+=11;
      }
      else {
          err = cMsgPayloadToStringImpl(vmsg, &pchar, level+1, margin+5, binary, compact, noSystemFields);
          if (err != CMSG_OK) {
              /* payload is not expanded */
              strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
              strncpy(pchar,"<payload expanded=\"false\">\n",27); pchar+=27;
              slen = strlen(msg->payloadText);
              strncpy(pchar, msg->payloadText, slen); pchar+=slen;
              strncpy(pchar, offsett, offsetLen); pchar+=offsetLen;
              strncpy(pchar,"</payload>\n",11); pchar+=11;
          }
      }
  }

  strncpy(pchar,indent, (size_t)indentLen);  pchar+=indentLen;
  strncpy(pchar,"</cMsgMessage>\n",15); pchar+=15;
  
  if (margin > 0) free(indent);
  free(offsett);

  /* hand newly allocated buffer off to user */
  if (level < 1) {
    *string = buf;
  }
  /* or else hand pointer into the old buffer back to caller */
  else  {
      *string = pchar;
  }

  return (CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine converts the message payload to an XML string.
 *
 * @param vmsg pointer to message
 * @param string is pointer to char* that will hold the malloc'd string if level = 0,
 *               otherwise this arg is the pointer to a pointer into an existing buffer
 *               which gets filled and which will change the pointer and return it as
 *               the end of its additions (for recursive messaging)
 * @param level the level of recursive messaging (0 = none)
 * @param margin number of spaces in the indent
 * @param binary includes binary as ASCII if true (!=0), else binary is ignored
 * @param compact if true (!=0), do not include attributes with null or default integer values
 * @param noSystemFields if true (!=0), do not include system (metadata) payload fields
 *
 * @returns CMSG_OK if successful or no payload
 * @returns CMSG_ERROR if internal payload parsing error,
 *                     or cannot get a payload item's type or count
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 * @returns CMSG_OUT_OF_MEMORY if out of memory
 */   
static int cMsgPayloadToStringImpl(const void *vmsg, char **string, int level, int margin,
                                   int binary, int compact, int noSystemFields) {

  int i, j, ok, len, count, hasPayload, *types, indentLen, namesLen=0;
  size_t slen, offsetLen, offset5Len;
  char *buffer=NULL, *pchar, *name, *indent, *offsett, *offset5, **names;

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);

  /* is there a payload? */
  cMsgHasPayload(msg, &hasPayload);

  if (!hasPayload) return(CMSG_OK);

  /* Create the indent since a message may contain a message, etc. */
  if (margin < 1) {
      margin = 0;
      indent = "";
      indentLen = 0;
  }
  else {
      indent = (char *)malloc((size_t)(margin + 1));
      if (indent == NULL) {
          return(CMSG_OUT_OF_MEMORY);
      }
      memset(indent, '\040', (size_t)margin); /* ASCII space = char #32 (40 octal) */
      indent[margin] = '\0';
      indentLen = margin;
  }

  /* indentation of 5 spaces */
  offset5 = (char *)malloc(6);
  if (offset5 == NULL) {
      if (margin > 0) free(indent);
      return(CMSG_OUT_OF_MEMORY);
  }
  memset(offset5, '\040', 5);
  offset5[5] = '\0';
  offset5Len = 5;
 
  /* indentation of margin + 5 spaces */
  offsett = (char *)malloc((size_t)(margin+6));
  if (offsett == NULL) {
      if (margin > 0) free(indent);
      free(offset5);
      return(CMSG_OUT_OF_MEMORY);
  }
  memset(offsett, '\040', (size_t)(margin+5));
  offsett[margin+5] = '\0';
  offsetLen = (size_t)(margin+5);
 
  /* Allocate and zero buffer if first level only. */
  if (level < 1) {
      level = 0;
      /* find payload size */
      slen = (size_t)messageStringSize(vmsg, margin, binary, 0, 1) + 1000;
      /*printf("cMsgToStringImpl: length of buffer needed = %d\n", slen);*/
      pchar = buffer = (char*)calloc(1, slen);
      if (buffer == NULL) {
          if (margin > 0) free(indent);
          free(offsett); free(offset5);
          return(CMSG_OUT_OF_MEMORY);
      }
  }
  /* Otherwise use an existing buffer. */
  else {
      pchar  = *string;
      buffer = pchar;
  }
 
  /* if we're here, there is a payload and we want it in full XML format */
  strncpy(pchar,indent, (size_t)indentLen); pchar+=indentLen;
  strncpy(pchar,"<payload compact=\"false\">\n",26); pchar+=26;

  /* get all name & type info */
  ok = cMsgPayloadGetInfo(msg, &names, &types, &namesLen);
  if (ok!=CMSG_OK) {
      if (level  < 1) free(buffer);
      if (margin > 0) free(indent);
      free(offsett); free(offset5);
      return(CMSG_ERROR);
  }
  
  for (i=0; i<namesLen; i++) {
          
    name = names[i];

    /* filter out system fields (names starting with "cmsg") */
    if (noSystemFields && (strncasecmp(name, "cmsg", 4) == 0) ) {
        continue;
    }
   
    switch (types[i]) {
      case CMSG_CP_INT8:
        {int8_t i; ok=cMsgGetInt8(msg, name, &i); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5); if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);     pchar+=offsetLen;
         strncpy(pchar,"<int8 name=\"",12);    pchar+=12;
         slen = strlen(name);
         strncpy(pchar,name,slen);             pchar+=slen;
         strncpy(pchar,"\"> ",3);              pchar+=3;
         sprintf(pchar,"%d%n",i,&len);         pchar+=len;
         strncpy(pchar," </int8>\n",9);        pchar+=9;
        } break;
      case CMSG_CP_INT16:
        {int16_t i; ok=cMsgGetInt16(msg, name, &i); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5); if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);     pchar+=offsetLen;
         strncpy(pchar,"<int16 name=\"",13);   pchar+=13;
         slen = strlen(name);
         strncpy(pchar,name,slen);             pchar+=slen;
         strncpy(pchar,"\"> ",3);              pchar+=3;
         sprintf(pchar,"%hd%n",i,&len);        pchar+=len;
         strncpy(pchar," </int16>\n",10);      pchar+=10;
        } break;
      case CMSG_CP_INT32:
        {int32_t i; ok=cMsgGetInt32(msg, name, &i); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5); if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);     pchar+=offsetLen;
         strncpy(pchar,"<int32 name=\"",13);   pchar+=13;
         slen = strlen(name);
         strncpy(pchar,name,slen);             pchar+=slen;
         strncpy(pchar,"\"> ",3);              pchar+=3;
         sprintf(pchar,"%d%n",i,&len);         pchar+=len;
         strncpy(pchar," </int32>\n",10);      pchar+=10;
        } break;
      case CMSG_CP_INT64:
        {int64_t i; ok=cMsgGetInt64(msg, name, &i); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5); if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);     pchar+=offsetLen;
         strncpy(pchar,"<int64 name=\"",13);   pchar+=13;
         slen = strlen(name);
         strncpy(pchar,name,slen);             pchar+=slen;
         strncpy(pchar,"\"> ",3);              pchar+=3;
         sprintf(pchar,"%lld%n",i,&len);       pchar+=len;
         strncpy(pchar," </int64>\n",10);      pchar+=10;
        } break;
      case CMSG_CP_UINT8:
        {uint8_t i; ok=cMsgGetUint8(msg, name, &i); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5); if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);     pchar+=offsetLen;
         strncpy(pchar,"<uint8 name=\"",13);   pchar+=13;
         slen = strlen(name);
         strncpy(pchar,name,slen);             pchar+=slen;
         strncpy(pchar,"\"> ",3);              pchar+=3;
         sprintf(pchar,"%u%n",i,&len);         pchar+=len;
         strncpy(pchar," </uint8>\n",10);      pchar+=10;
        } break;
      case CMSG_CP_UINT16:
        {uint16_t i; ok=cMsgGetUint16(msg, name, &i); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5); if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);     pchar+=offsetLen;
         strncpy(pchar,"<uint16 name=\"",14);  pchar+=14;
         slen = strlen(name);
         strncpy(pchar,name,slen);             pchar+=slen;
         strncpy(pchar,"\"> ",3);              pchar+=3;
         sprintf(pchar,"%hu%n",i,&len);        pchar+=len;
         strncpy(pchar," </uint16>\n",11);     pchar+=11;
        } break;
      case CMSG_CP_UINT32:
        {uint32_t i; ok=cMsgGetUint32(msg, name, &i); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5); if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);     pchar+=offsetLen;
         strncpy(pchar,"<uint32 name=\"",14);  pchar+=14;
         slen = strlen(name);
         strncpy(pchar,name,slen);             pchar+=slen;
         strncpy(pchar,"\"> ",3);              pchar+=3;
         sprintf(pchar,"%u%n",i,&len);         pchar+=len;
         strncpy(pchar," </uint32>\n",11);     pchar+=11;
        } break;
      case CMSG_CP_UINT64:
        {uint64_t i; ok=cMsgGetUint64(msg, name, &i); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5); if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);     pchar+=offsetLen;
         strncpy(pchar,"<uint64 name=\"",14);  pchar+=14;
         slen = strlen(name);
         strncpy(pchar,name,slen);             pchar+=slen;
         strncpy(pchar,"\"> ",3);              pchar+=3;
         sprintf(pchar,"%llu%n",i,&len);       pchar+=len;
         strncpy(pchar," </uint64>\n",11);     pchar+=11;
        } break;
      case CMSG_CP_DBL:
        {double d; ok=cMsgGetDouble(msg, name, &d); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5); if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);     pchar+=offsetLen;
         strncpy(pchar,"<double name=\"",14);  pchar+=14;
         slen = strlen(name);
         strncpy(pchar,name,slen);             pchar+=slen;
         strncpy(pchar,"\"> ",3);              pchar+=3;
         sprintf(pchar,"%.17g%n",d,&len);      pchar+=len;
         strncpy(pchar," </double>\n",11);     pchar+=11;
        } break;
      case CMSG_CP_FLT:
        {float f; ok=cMsgGetFloat(msg, name, &f); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5); if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);     pchar+=offsetLen;
         strncpy(pchar,"<float name=\"",13);   pchar+=13;
         slen = strlen(name);   
         strncpy(pchar,name,slen);             pchar+=slen;
         strncpy(pchar,"\"> ",3);              pchar+=3;
         sprintf(pchar,"%.8g%n",f,&len);       pchar+=len;
         strncpy(pchar," </float>\n",10);      pchar+=10;
        } break;
      case CMSG_CP_STR:
      {char *s,*str; ok=cMsgGetString(msg, name, (const char **)&s); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5); if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);     pchar+=offsetLen;
         strncpy(pchar,"<string name=\"",14);  pchar+=14;
         slen = strlen(name);
         strncpy(pchar,name,slen);             pchar+=slen;
         strncpy(pchar,"\"><![CDATA[",11);     pchar+=11;
         str = escapeCdataForXML(s);
         if (str == NULL) {
             if(level < 1){free(buffer);} free(offsett);free(offset5); if(margin>0){free(indent);}
             return(CMSG_OUT_OF_MEMORY);
         }
         slen = strlen(str);
         strncpy(pchar,str,slen);              pchar+=slen;
         strncpy(pchar,"]]></string>\n",13);   pchar+=13;
         if (str != s) free(str);
        } break;
        
      case CMSG_CP_BIN:
        {const char *s; int sz, endian; char *endianTxt;
         ok=cMsgGetBinary(msg, name, &s, &sz, &endian); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);     pchar+=offsetLen;
         strncpy(pchar,"<binary name=\"",14);  pchar+=14;
         slen = strlen(name);
         strncpy(pchar,name,slen);             pchar+=slen;
         if (endian == CMSG_ENDIAN_BIG) {
             strncpy(pchar,"\" endian=\"big\"",14);     pchar+=14;
         }
         else {
             strncpy(pchar,"\" endian=\"little\"",17);  pchar+=17;
         }
         strncpy(pchar," nbytes=\"",9);  pchar+=9;
         sprintf(pchar,"%d%n",sz,&len);  pchar+=len;
         
         if (!binary) {
             strncpy(pchar,"\" />\n",5); pchar+=5;
         }
         else {
             /* put in line breaks after 76 chars (57 bytes) */
             if (sz > 57) {
                strncpy(pchar,"\">\n",3);              pchar+=3;
                count = cMsg_b64_encode(s, (unsigned int)sz, pchar, 1); pchar += count;
                strncpy(pchar,offsett,offsetLen);      pchar+=offsetLen;
                strncpy(pchar,"</binary>\n",10);       pchar+=10;
             }
             else {
                 strncpy(pchar,"\">",2);                pchar+=2;
                 count = cMsg_b64_encode(s, (unsigned int)sz, pchar, 0); pchar += count;
                 strncpy(pchar,"</binary>\n",10);       pchar+=10;
             }
         }
        } break;
 
      case CMSG_CP_MSG:
        {const void *m; ok=cMsgGetMessage(msg, name, &m);    if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         ok = cMsgToStringImpl(m, &pchar, level+1, margin+5, binary, compact,
                               0, 1, noSystemFields, name);
         if(ok!=CMSG_OK) {
           if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
        } break;
         
      /* arrays */
      case CMSG_CP_MSG_A:
        {const void **m; ok=cMsgGetMessageArray(msg, name, &m, &count);  if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);                pchar+=offsetLen;
         strncpy(pchar,"<cMsgMessage_array name=\"",25);  pchar+=25;
         slen = strlen(name);
         strncpy(pchar,name,slen);                        pchar+=slen;
         strncpy(pchar,"\" count=\"",9);                  pchar+=9;
         sprintf(pchar,"%d%n",count,&len);                pchar+=len;
         strncpy(pchar,"\">\n",3);                        pchar+=3;
         for(j=0;j<count;j++) {
           ok = cMsgToStringImpl(m[j], &pchar, level+1, margin+10, binary, compact,
                                 0, 0, noSystemFields,NULL);
           if(ok!=CMSG_OK) {
               if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         }
         strncpy(pchar,offsett,offsetLen);           pchar+=offsetLen;
         strncpy(pchar,"</cMsgMessage_array>\n",21); pchar+=21;
        } break;
         
      case CMSG_CP_BIN_A:
        {const char **s; int *sizes, *endians, nchars; char *endianTxt;
         ok=cMsgGetBinaryArray(msg, name, &s, &sizes, &endians, &count); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);           pchar+=offsetLen;
         strncpy(pchar,"<binary_array name=\"",20);  pchar+=20;
         slen = strlen(name);
         strncpy(pchar,name,slen);                   pchar+=slen;
         strncpy(pchar,"\" count=\"",9);             pchar+=9;
         sprintf(pchar,"%d%n",count,&len);           pchar+=len;
         strncpy(pchar,"\">\n",3);                   pchar+=3;
         for(j=0;j<count;j++) {
             strncpy(pchar,offsett,offsetLen);        pchar+=offsetLen;
             strncpy(pchar,offset5,offset5Len);       pchar+=offset5Len;
             if (endians[j] == CMSG_ENDIAN_BIG) {
                 strncpy(pchar,"<binary endian=\"big\"",20);     pchar+=20;
             }
             else {
                 strncpy(pchar,"<binary endian=\"little\"",23);  pchar+=23;
             }
             strncpy(pchar," nbytes=\"",9);             pchar+=9;
             sprintf(pchar,"%d%n",sizes[j],&len);       pchar+=len;
             
             if (!binary) {
                 strncpy(pchar,"\" />\n",5);            pchar+=5;
             }
             else {
                 /* put in line breaks after 76 chars (57 bytes) */
                 if (sizes[j]> 57) {
                     strncpy(pchar,"\">\n",3);              pchar+=3;
                     nchars = cMsg_b64_encode(s[j], (unsigned int)sizes[j], pchar, 1); pchar += nchars;
                     strncpy(pchar,offsett,offsetLen);      pchar+=offsetLen;
                     strncpy(pchar,offset5,offset5Len);     pchar+=offset5Len;
                     strncpy(pchar,"</binary>\n",10);       pchar+=10;
                 }
                 else {
                     strncpy(pchar,"\">",2);                pchar+=2;
                     nchars = cMsg_b64_encode(s[j], (unsigned int)sizes[j], pchar, 0); pchar += nchars;
                     strncpy(pchar,"</binary>\n",10);       pchar+=10;
                 }
             }
         }
         strncpy(pchar,offsett,offsetLen);              pchar+=offsetLen;
         strncpy(pchar,"</binary_array>\n",16);         pchar+=16;
        } break;
 
      case CMSG_CP_INT8_A:
        {const int8_t *i; ok=cMsgGetInt8Array(msg, name, &i, &count); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);            pchar+=offsetLen;
         strncpy(pchar,"<int8_array name=\"",18);     pchar+=18;
         slen = strlen(name);
         strncpy(pchar,name,slen);                    pchar+=slen;
         strncpy(pchar,"\" count=\"",9);              pchar+=9;
         sprintf(pchar,"%d%n",count,&len);            pchar+=len;
         strncpy(pchar,"\">\n",3);                    pchar+=3;
         for(j=0;j<count;j++) {
            if (j%5 == 0) {sprintf(pchar, "%s%s%4d%n", offsett, offset5, i[j], &len); pchar+=len;}
            else          {sprintf(pchar, " %4d%n", i[j], &len); pchar+=len;}
            if (j%5==4 || j==count-1) {strncpy(pchar,"\n",1); pchar++;}
         }
         strncpy(pchar,offsett,offsetLen);       pchar+=offsetLen;
         strncpy(pchar,"</int8_array>\n",14);    pchar+=14;
        } break;
      case CMSG_CP_INT16_A:
        {const int16_t *i; ok=cMsgGetInt16Array(msg, name, &i, &count); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);            pchar+=offsetLen;
         strncpy(pchar,"<int16_array name=\"",19);    pchar+=19;
         slen = strlen(name);
         strncpy(pchar,name,slen);                    pchar+=slen;
         strncpy(pchar,"\" count=\"",9);              pchar+=9;
         sprintf(pchar,"%d%n",count,&len);            pchar+=len;
         strncpy(pchar,"\">\n",3);                    pchar+=3;
         for(j=0;j<count;j++) {
             if (j%5 == 0) {sprintf(pchar, "%s%s%6hd%n", offsett, offset5, i[j], &len); pchar+=len;}
             else          {sprintf(pchar, " %6hd%n", i[j], &len); pchar+=len;}
             if (j%5==4 || j==count-1) {strncpy(pchar,"\n",1); pchar++;}
         }
         strncpy(pchar,offsett,offsetLen);       pchar+=offsetLen;
         strncpy(pchar,"</int16_array>\n",15);   pchar+=15;
        } break;
      case CMSG_CP_INT32_A:
        {const int32_t *i; ok=cMsgGetInt32Array(msg, name, &i, &count); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);            pchar+=offsetLen;
         strncpy(pchar,"<int32_array name=\"",19);    pchar+=19;
         slen = strlen(name);
         strncpy(pchar,name,slen);                    pchar+=slen;
         strncpy(pchar,"\" count=\"",9);              pchar+=9;
         sprintf(pchar,"%d%n",count,&len);            pchar+=len;
         strncpy(pchar,"\">\n",3);                    pchar+=3;
         for(j=0;j<count;j++) {
             if (j%5 == 0) {sprintf(pchar, "%s%s%d%n", offsett, offset5, i[j], &len); pchar+=len;}
             else          {sprintf(pchar, " %d%n", i[j], &len); pchar+=len;}
             if (j%5==4 || j==count-1) {strncpy(pchar,"\n",1); pchar++;}
         }
         strncpy(pchar,offsett,offsetLen);       pchar+=offsetLen;
         strncpy(pchar,"</int32_array>\n",15);   pchar+=15;
        } break;
      case CMSG_CP_INT64_A:
        {const int64_t *i; ok=cMsgGetInt64Array(msg, name, &i, &count); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);            pchar+=offsetLen;
         strncpy(pchar,"<int64_array name=\"",19);    pchar+=19;
         slen = strlen(name);
         strncpy(pchar,name,slen);                    pchar+=slen;
         strncpy(pchar,"\" count=\"",9);              pchar+=9;
         sprintf(pchar,"%d%n",count,&len);            pchar+=len;
         strncpy(pchar,"\">\n",3);                    pchar+=3;
         for(j=0;j<count;j++) {
             if (j%5 == 0) {sprintf(pchar, "%s%s%lld%n", offsett, offset5, i[j], &len); pchar+=len;}
             else          {sprintf(pchar, " %lld%n", i[j], &len); pchar+=len;}
             if (j%5==4 || j==count-1) {strncpy(pchar,"\n",1); pchar++;}
         }
         strncpy(pchar,offsett,offsetLen);       pchar+=offsetLen;
         strncpy(pchar,"</int64_array>\n",15);   pchar+=15;
        } break;
      case CMSG_CP_UINT8_A:
        {const uint8_t *i; ok=cMsgGetUint8Array(msg, name, &i, &count); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);            pchar+=offsetLen;
         strncpy(pchar,"<uint8_array name=\"",19);    pchar+=19;
         slen = strlen(name);
         strncpy(pchar,name,slen);                    pchar+=slen;
         strncpy(pchar,"\" count=\"",9);              pchar+=9;
         sprintf(pchar,"%d%n",count,&len);            pchar+=len;
         strncpy(pchar,"\">\n",3);                    pchar+=3;
         for(j=0;j<count;j++) {
             if (j%5 == 0) {sprintf(pchar, "%s%s%3u%n", offsett, offset5, i[j], &len); pchar+=len;}
             else          {sprintf(pchar, " %3u%n", i[j], &len); pchar+=len;}
             if (j%5==4 || j==count-1) {strncpy(pchar,"\n",1); pchar++;}
         }
         strncpy(pchar,offsett,offsetLen);       pchar+=offsetLen;
         strncpy(pchar,"</uint8_array>\n",15);   pchar+=15;
        } break;
      case CMSG_CP_UINT16_A:
        {const uint16_t *i; ok=cMsgGetUint16Array(msg, name, &i, &count); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);            pchar+=offsetLen;
         strncpy(pchar,"<uint16_array name=\"",20);   pchar+=20;
         slen = strlen(name);
         strncpy(pchar,name,slen);                    pchar+=slen;
         strncpy(pchar,"\" count=\"",9);              pchar+=9;
         sprintf(pchar,"%d%n",count,&len);            pchar+=len;
         strncpy(pchar,"\">\n",3);                    pchar+=3;
         for(j=0;j<count;j++) {
             if (j%5 == 0) {sprintf(pchar, "%s%s%5hu%n", offsett, offset5, i[j], &len); pchar+=len;}
             else          {sprintf(pchar, " %5hu%n", i[j], &len); pchar+=len;}
             if (j%5==4 || j==count-1) {strncpy(pchar,"\n",1); pchar++;}
         }
         strncpy(pchar,offsett,offsetLen);       pchar+=offsetLen;
         strncpy(pchar,"</uint16_array>\n",16);  pchar+=16;
        } break;
      case CMSG_CP_UINT32_A:
        {const uint32_t *i; ok=cMsgGetUint32Array(msg, name, &i, &count); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);            pchar+=offsetLen;
         strncpy(pchar,"<uint32_array name=\"",20);   pchar+=20;
         slen = strlen(name);
         strncpy(pchar,name,slen);                    pchar+=slen;
         strncpy(pchar,"\" count=\"",9);              pchar+=9;
         sprintf(pchar,"%d%n",count,&len);            pchar+=len;
         strncpy(pchar,"\">\n",3);                    pchar+=3;
         for(j=0;j<count;j++) {
             if (j%5 == 0) {sprintf(pchar, "%s%s%u%n", offsett, offset5, i[j], &len); pchar+=len;}
             else          {sprintf(pchar, " %u%n", i[j], &len); pchar+=len;}
             if (j%5==4 || j==count-1) {strncpy(pchar,"\n",1); pchar++;}
         }
         strncpy(pchar,offsett,offsetLen);       pchar+=offsetLen;
         strncpy(pchar,"</uint32_array>\n",16);  pchar+=16;
        } break;
      case CMSG_CP_UINT64_A:
        {const uint64_t *i; ok=cMsgGetUint64Array(msg, name, &i, &count); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);            pchar+=offsetLen;
         strncpy(pchar,"<uint64_array name=\"",20);   pchar+=20;
         slen = strlen(name);
         strncpy(pchar,name,slen);                    pchar+=slen;
         strncpy(pchar,"\" count=\"",9);              pchar+=9;
         sprintf(pchar,"%d%n",count,&len);            pchar+=len;
         strncpy(pchar,"\">\n",3);                    pchar+=3;
         for(j=0;j<count;j++) {
             if (j%5 == 0) {sprintf(pchar, "%s%s%llu%n", offsett, offset5, i[j], &len); pchar+=len;}
             else          {sprintf(pchar, " %llu%n", i[j], &len); pchar+=len;}
             if (j%5==4 || j==count-1) {strncpy(pchar,"\n",1); pchar++;}
         }
         strncpy(pchar,offsett,offsetLen);       pchar+=offsetLen;
         strncpy(pchar,"</uint64_array>\n",16);  pchar+=16;
        } break;
      case CMSG_CP_DBL_A:
        {const double *d; ok=cMsgGetDoubleArray(msg, name, &d, &count); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);            pchar+=offsetLen;
         strncpy(pchar,"<double_array name=\"",20);   pchar+=20;
         slen = strlen(name);
         strncpy(pchar,name,slen);                    pchar+=slen;
         strncpy(pchar,"\" count=\"",9);              pchar+=9;
         sprintf(pchar,"%d%n",count,&len);            pchar+=len;
         strncpy(pchar,"\">\n",3);                    pchar+=3;
         for(j=0;j<count;j++) {
             if (j%5 == 0) {sprintf(pchar, "%s%s%.17g%n", offsett, offset5, d[j], &len); pchar+=len;}
             else          {sprintf(pchar, " %.17g%n", d[j], &len); pchar+=len;}
             if (j%5==4 || j==count-1) {strncpy(pchar,"\n",1); pchar++;}
         }
         strncpy(pchar,offsett,offsetLen);       pchar+=offsetLen;
         strncpy(pchar,"</double_array>\n",16);  pchar+=16;
        } break;
      case CMSG_CP_FLT_A:
        {const float *f; ok=cMsgGetFloatArray(msg, name, &f, &count); if(ok!=CMSG_OK) {
         if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);            pchar+=offsetLen;
         strncpy(pchar,"<float_array name=\"",19);    pchar+=19;
         slen = strlen(name);
         strncpy(pchar,name,slen);                    pchar+=slen;
         strncpy(pchar,"\" count=\"",9);              pchar+=9;
         sprintf(pchar,"%d%n",count,&len);            pchar+=len;
         strncpy(pchar,"\">\n",3);                    pchar+=3;
         for(j=0;j<count;j++) {
             if (j%5 == 0) {sprintf(pchar, "%s%s%.8g%n", offsett, offset5, f[j], &len); pchar+=len;}
             else          {sprintf(pchar, " %.8g%n", f[j], &len); pchar+=len;}
             if (j%5==4 || j==count-1) {strncpy(pchar,"\n",1); pchar++;}
         }
         strncpy(pchar,offsett,offsetLen);       pchar+=offsetLen;
         strncpy(pchar,"</float_array>\n",15);   pchar+=15;
        } break;
      case CMSG_CP_STR_A:
      {char **s;char *str; ok=cMsgGetStringArray(msg, name, (const char ***)&s, &count); if(ok!=CMSG_OK) {
        if(level < 1){free(buffer);} free(offsett);free(offset5);if(margin>0){free(indent);} return(CMSG_ERROR);}
         strncpy(pchar,offsett,offsetLen);            pchar+=offsetLen;
         strncpy(pchar,"<string_array name=\"",20);   pchar+=20;
         slen = strlen(name);
         strncpy(pchar,name,slen);                    pchar+=slen;
         strncpy(pchar,"\" count=\"",9);              pchar+=9;
         sprintf(pchar,"%d%n",count,&len);            pchar+=len;
         strncpy(pchar,"\">\n",3);                    pchar+=3;
         for(j=0;j<count;j++) {
             strncpy(pchar,offsett,offsetLen);        pchar+=offsetLen;
             strncpy(pchar,offset5,offset5Len);       pchar+=offset5Len;
             strncpy(pchar,"<string><![CDATA[",17);   pchar+=17;
             str = escapeCdataForXML(s[j]);
             if (str == NULL) {
                 if(level < 1){free(buffer);} free(offsett);free(offset5); if(margin>0){free(indent);}
                 return(CMSG_OUT_OF_MEMORY);
             }
             slen = strlen(str);
             strncpy(pchar,str,slen);                 pchar+=slen;
             strncpy(pchar,"]]></string>\n",13);      pchar+=13;
             if (str != s[j]) free(str);
         }
         strncpy(pchar,offsett,offsetLen);            pchar+=offsetLen;
         strncpy(pchar,"</string_array>\n",16);       pchar+=16;
        } break;
    }
            
  } /* for loop thru all names/types */
  
   free(names);
   free(types);
   free(offsett);
   free(offset5);
   
   /*   </payload> */
   strncpy(pchar,indent,(size_t)indentLen); pchar+=indentLen;
   strncpy(pchar,"</payload>\n",11); pchar+=11;

  if (margin > 0) free(indent);

  /* hand newly allocated buffer off to user */
  if (level < 1) {
    *string = buffer;
  }
  /* or else hand pointer into the old buffer back to caller */
  else  {
    *string = pchar;
  }

  return (CMSG_OK);
}


/*-------------------------------------------------------------------*/
/*   message context accessor functions                              */
/*-------------------------------------------------------------------*/


/**
 * This routine gets the domain a subscription is running in and is valid
 * only when used in a callback on the message given in the callback
 * argument.
 * If successful, this routine will return a pointer to char inside the
 * message structure. The user may NOT write to this memory location!
 *
 * @param vmsg pointer to message
 * @param domain pointer to pointer filled with a subscription's domain
 *                or NULL if no information is available
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetSubscriptionDomain(const void *vmsg, const char **domain) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || domain == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->context.domain == NULL) {
    *domain = NULL;
  }
  else {
    *domain = msg->context.domain;
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/

/**
 * This routine gets the subject a subscription is using and is valid
 * only when used in a callback on the message given in the callback
 * argument.
 * If successful, this routine will return a pointer to char inside the
 * message structure. The user may NOT write to this memory location!
 *
 * @param vmsg pointer to message
 * @param subject pointer to pointer filled with a subscription's subject
 *                or NULL if no information is available
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetSubscriptionSubject(const void *vmsg, const char **subject) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || subject == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->context.subject == NULL) {
    *subject = NULL;
  }
  else {
    *subject = msg->context.subject;
  }
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/

/**
 * This routine gets the type a subscription is using and is valid
 * only when used in a callback on the message given in the callback
 * argument.
 * If successful, this routine will return a pointer to char inside the
 * message structure. The user may NOT write to this memory location!
 *
 * @param vmsg pointer to message
 * @param type pointer to pointer filled with a subscription's type
 *                or NULL if no information is available
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetSubscriptionType(const void *vmsg, const char **type) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || type == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->context.type == NULL) {
    *type = NULL;
  }
  else {
    *type = msg->context.type;
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/

/**
 * This routine gets the udl of a subscription's connection and is valid
 * only when used in a callback on the message given in the callback
 * argument.
 * If successful, this routine will return a pointer to char inside the
 * message structure. The user may NOT write to this memory location!
 *
 * @param vmsg pointer to message
 * @param udl pointer to pointer filled with the udl of a subscription's
 *            connection or NULL if no information is available
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetSubscriptionUDL(const void *vmsg, const char **udl) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || udl == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->context.udl == NULL) {
    *udl = NULL;
  }
  else {
    *udl = msg->context.udl;
  }
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/

/**
 * This routine gets the cue size of a callback and is valid
 * only when used in a callback on the message given in the callback
 * argument.
 *
 * @param vmsg pointer to message
 * @param size pointer which gets filled with a callback's cue size
 *             or -1 if no information is available
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetSubscriptionCueSize(const void *vmsg, int *size) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || size == NULL) return(CMSG_BAD_ARGUMENT);
  if (msg->context.cueSize == NULL) {
    *size = -1;
  }
  else {
    *size = (*(msg->context.cueSize));
  }
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/

/**
 * This routine sets whether the send will be reliable (default, TCP)
 * or will be allowed to be unreliable (UDP).
 *
 * @param vmsg pointer to message
 * @param boolean 0 if false (use UDP), anything else true (use TCP)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message is NULL
 */   
int cMsgSetReliableSend(void *vmsg, int boolean) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  
  msg->udpSend = boolean == 0 ? 1 : 0;

  return(CMSG_OK);
}

/**
 * This routine gets whether the send will be reliable (default, TCP)
 * or will be allowed to be unreliable (UDP).
 *
 * @param vmsg pointer to message
 * @param boolean int pointer to be filled with 1 if true (TCP), else
 *                0 (UDP)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetReliableSend(void *vmsg, int *boolean) {

  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || boolean == NULL) return(CMSG_BAD_ARGUMENT);
  *boolean = msg->udpSend == 1 ? 0 : 1;
  return (CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*   subscribe config functions                                      */
/*-------------------------------------------------------------------*/


/**
 * This routine creates a structure of configuration information used
 * to determine the behavior of a cMsgSubscribe()'s callback. The
 * configuration is filled with default values. Each aspect of the
 * configuration may be modified by setter and getter functions. The
 * defaults are:
 * - maximum messages to cue for callback is 10000
 * - no messages may be skipped
 * - calls to the callback function must be serialized
 * - may skip up to 2000 messages at once if skipping is enabled
 * - maximum number of threads when parallelizing calls to the callback
 *   function is 100
 * - enough worker threads are started so that there are
 *   at most 150 unprocessed messages for each thread
 *
 * Note that this routine allocates memory and cMsgSubscribeConfigDestroy()
 * must be called to free it.
 *
 * @returns NULL if no memory available
 * @returns pointer to configuration if successful
 */   
cMsgSubscribeConfig *cMsgSubscribeConfigCreate(void) {
  subscribeConfig *sc;
  
  sc = (subscribeConfig *) malloc(sizeof(subscribeConfig));
  if (sc == NULL) {
    return NULL;
  }
  
  /* default configuration for a subscription */
  sc->maxCueSize = 10000;   /* maximum number of messages to cue for callback */
  sc->skipSize   =  2000;   /* number of messages to skip over (delete) from the cue
                             * for a callback when the cue size has reached it limit */
  sc->maySkip        = 0;   /* may NOT skip messages if too many are piling up in cue */
  sc->mustSerialize  = 1;   /* messages must be processed in order */
  sc->init           = 1;   /* done intializing structure */
  sc->maxThreads     = 20;  /* max number of worker threads to run callback
                             * if mustSerialize = 0 */
  sc->msgsPerThread  = 500; /* enough worker threads are started so that there are
                             * at most this many unprocessed messages for each thread */
  sc->stackSize      = 0;   /* normally only used in vxworks  */

  return (cMsgSubscribeConfig*) sc;

}


/*-------------------------------------------------------------------*/



/**
 * This routine frees the memory associated with a configuration
 * created by cMsgSubscribeConfigCreate();
 * 
 * @param config pointer to configuration
 *
 * @returns CMSG_OK
 */   
int cMsgSubscribeConfigDestroy(cMsgSubscribeConfig *config) {
  if (config != NULL) {
    free((subscribeConfig *) config);
  }
  return CMSG_OK;
}


/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets a subscribe configuration's maximum message cue
 * size. Messages are kept in the cue until they can be processed by
 * the callback function.
 *
 * @param config pointer to configuration
 * @param size maximum cue size
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if configuration was not initialized
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL or size < 1
 */   
int cMsgSubscribeSetMaxCueSize(cMsgSubscribeConfig *config, int size) {
  subscribeConfig *sc = (subscribeConfig *) config;
  
  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }
   
  if (config == NULL || size < 1)  {
    return CMSG_BAD_ARGUMENT;
  }
  
  sc->maxCueSize = size;
  return CMSG_OK;
}


/**
 * This routine gets a subscribe configuration's maximum message cue
 * size. Messages are kept in the cue until they can be processed by
 * the callback function.
 *
 * @param config pointer to configuration
 * @param size integer pointer to be filled with configuration's maximum
 *             cue size
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgSubscribeGetMaxCueSize(cMsgSubscribeConfig *config, int *size) {
  subscribeConfig *sc = (subscribeConfig *) config;   
  
  if (config == NULL || size == NULL) return(CMSG_BAD_ARGUMENT);
  *size = sc->maxCueSize;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the number of messages to skip over (delete) if too
 * many messages are piling up in the cue. Messages are only skipped if
 * cMsgSubscribeSetMaySkip() sets the configuration to do so.
 *
 * @param config pointer to configuration
 * @param size number of messages to skip (delete)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if configuration was not initialized
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL or size < 0
 */   
int cMsgSubscribeSetSkipSize(cMsgSubscribeConfig *config, int size) {
  subscribeConfig *sc = (subscribeConfig *) config;
  
  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }
   
  if (config == NULL || size < 0)  {
    return CMSG_BAD_ARGUMENT;
  }
  
  sc->skipSize = size;
  return CMSG_OK;
}

/**
 * This routine gets the number of messages to skip over (delete) if too
 * many messages are piling up in the cue. Messages are only skipped if
 * cMsgSubscribeSetMaySkip() sets the configuration to do so.
 *
 * @param config pointer to configuration
 * @param size integer pointer to be filled with the number of messages
 *             to skip (delete)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgSubscribeGetSkipSize(cMsgSubscribeConfig *config, int *size) {
  subscribeConfig *sc = (subscribeConfig *) config;    

  if (config == NULL || size == NULL) return(CMSG_BAD_ARGUMENT);
  *size = sc->skipSize;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets whether messages may be skipped over (deleted)
 * if too many messages are piling up in the cue. The maximum number
 * of messages skipped at once is determined by cMsgSubscribeSetSkipSize().
 *
 * @param config pointer to configuration
 * @param maySkip set to 0 if messages may NOT be skipped, set to anything
 *                else otherwise
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if configuration was not initialized
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL
 */   
int cMsgSubscribeSetMaySkip(cMsgSubscribeConfig *config, int maySkip) {
  subscribeConfig *sc = (subscribeConfig *) config;

  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  } 
    
  if (config == NULL)  {
    return CMSG_BAD_ARGUMENT;
  }
    
  sc->maySkip = maySkip;
  return CMSG_OK;
}

/**
 * This routine gets whether messages may be skipped over (deleted)
 * if too many messages are piling up in the cue. The maximum number
 * of messages skipped at once is determined by cMsgSubscribeSetSkipSize().
 *
 * @param config pointer to configuration
 * @param maySkip integer pointer to be filled with 0 if messages may NOT
 *                be skipped (deleted), or anything else otherwise
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgSubscribeGetMaySkip(cMsgSubscribeConfig *config, int *maySkip) {
  subscribeConfig *sc = (subscribeConfig *) config;    

  if (config == NULL || maySkip == NULL) return(CMSG_BAD_ARGUMENT);
  *maySkip = sc->maySkip;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets whether a subscribe's callback must be run serially
 * (in one thread), or may be parallelized (run simultaneously in more
 * than one thread) if more than 1 message is waiting in the cue.
 *
 * @param config pointer to configuration
 * @param serialize set to 0 if callback may be parallelized, or set to
 *                  anything else if callback must be serialized
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if configuration was not initialized
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL
 */   
int cMsgSubscribeSetMustSerialize(cMsgSubscribeConfig *config, int serialize) {
  subscribeConfig *sc = (subscribeConfig *) config;

  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }   
    
  if (config == NULL)  {
    return CMSG_BAD_ARGUMENT;
  }
    
  sc->mustSerialize = serialize;
  return CMSG_OK;
}

/**
 * This routine gets whether a subscribe's callback must be run serially
 * (in one thread), or may be parallelized (run simultaneously in more
 * than one thread) if more than 1 message is waiting in the cue.
 *
 * @param config pointer to configuration
 * @param serialize integer pointer to be filled with 0 if callback may be
 *                  parallelized, or anything else if callback must be serialized
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgSubscribeGetMustSerialize(cMsgSubscribeConfig *config, int *serialize) {
  subscribeConfig *sc = (subscribeConfig *) config;

  if (config == NULL || serialize == NULL) return(CMSG_BAD_ARGUMENT);
  *serialize = sc->mustSerialize;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the maximum number of threads a parallelized
 * subscribe's callback can run at once. This setting is only used if
 * cMsgSubscribeSetMustSerialize() was called with an argument of 0.
 *
 * @param config pointer to configuration
 * @param threads the maximum number of threads a parallelized
 *                subscribe's callback can run at once
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if configuration was not initialized
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL or threads < 0
 */   
int cMsgSubscribeSetMaxThreads(cMsgSubscribeConfig *config, int threads) {
  subscribeConfig *sc = (subscribeConfig *) config;
  
  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }
   
  if (config == NULL || threads < 1)  {
    return CMSG_BAD_ARGUMENT;
  }
  
  sc->maxThreads = threads;
  return CMSG_OK;
}

/**
 * This routine gets the maximum number of threads a parallelized
 * subscribe's callback can run at once. This setting is only used if
 * cMsgSubscribeSetMustSerialize() was called with an argument of 0.
 *
 * @param config pointer to configuration
 * @param threads integer pointer to be filled with the maximum number
 *                of threads a parallelized subscribe's callback can run at once
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgSubscribeGetMaxThreads(cMsgSubscribeConfig *config, int *threads) {
  subscribeConfig *sc = (subscribeConfig *) config;    

  if (config == NULL || threads == NULL) return(CMSG_BAD_ARGUMENT);
  *threads = sc->maxThreads;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the maximum number of unprocessed messages per thread
 * before a new thread is started, if a callback is parallelized 
 * (cMsgSubscribeSetMustSerialize() set to 0).
 *
 * @param config pointer to configuration
 * @param mpt set to maximum number of unprocessed messages per thread
 *            before starting another thread
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if configuration was not initialized
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL or mpt < 1
 */   
int cMsgSubscribeSetMessagesPerThread(cMsgSubscribeConfig *config, int mpt) {
  subscribeConfig *sc = (subscribeConfig *) config;
  
  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }
   
  if (config == NULL || mpt < 1)  {
    return CMSG_BAD_ARGUMENT;
  }
  
  sc->msgsPerThread = mpt;
  return CMSG_OK;
}

/**
 * This routine gets the maximum number of unprocessed messages per thread
 * before a new thread is started, if a callback is parallelized 
 * (cMsgSubscribeSetMustSerialize() set to 0).
 *
 * @param config pointer to configuration
 * @param mpt integer pointer to be filled with the maximum number of
 *            unprocessed messages per thread before starting another thread
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgSubscribeGetMessagesPerThread(cMsgSubscribeConfig *config, int *mpt) {
  subscribeConfig *sc = (subscribeConfig *) config;    

  if (config == NULL || mpt == NULL) return(CMSG_BAD_ARGUMENT);
  *mpt = sc->msgsPerThread;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/

/**
 * This routine sets the stack size in bytes of the subscription thread.
 * By default the stack size is unspecified. 
 *
 * @param config pointer to configuration
 * @param size stack size in bytes of subscription thread
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_NOT_INITIALIZED if configuration was not initialized
 * @returns CMSG_BAD_ARGUMENT if configuration is NULL or size < 1 byte
 */   
int cMsgSubscribeSetStackSize(cMsgSubscribeConfig *config, size_t size) {
  subscribeConfig *sc = (subscribeConfig *) config;
  
  if (sc->init != 1) {
    return CMSG_NOT_INITIALIZED;
  }
   
  if (config == NULL || size < 1)  {
    return CMSG_BAD_ARGUMENT;
  }
  
  sc->stackSize = size;
  return CMSG_OK;
}

/**
 * This routine gets the stack size in bytes of the subscription thread.
 * By default the stack size is unspecified (returns 0).
 *
 * @param config pointer to configuration
 * @param size pointer to be filled with stack size in bytes of subscription thread
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgSubscribeGetStackSize(cMsgSubscribeConfig *config, size_t *size) {
  subscribeConfig *sc = (subscribeConfig *) config;    

  if (config == NULL || size == NULL) return(CMSG_BAD_ARGUMENT);
  *size = sc->stackSize;
  return(CMSG_OK);
}

/*-------------------------------------------------------------------*/

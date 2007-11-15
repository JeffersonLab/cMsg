/*----------------------------------------------------------------------------*
 *  Copyright (c) 2007        Southeastern Universities Research Association, *
 *                            Jefferson Science Associates                    *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C.Timmer, 28-Jun-2007, Jefferson Lab                                    *
 *                                                                            *
 *    Authors: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-6248             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/

/**
 * @file
 * <H1><b>Introduction</b></H1><p>
 *
 * <H2>
 * This file contains the compound payload interface to cMsg messages. In short,
 * this allows the text field of the message to store messages of arbitrary
 * length and complexity. All types of ints (1,2,4,8 bytes), 4,8-byte floats,
 * strings, binary, whole messages and arrays of all these types can be stored
 * and retrieved from the compound payload. These routines are thread-safe.
 * </H2><p>
 *
 * <H2>
 * Although XML would be a format well-suited to this task, cMsg should stand
 * alone - not requiring an XML parser to work. It takes more memory and time
 * to decode XML than a simple format. Thus, a simple, easy-to-parse format
 * was developed to implement this interface.
 * </H2><p>
 *
 * <H2>
 * Following is the text format of a complete compound payload (nl = newline).
 * Each payload consists of a number of items. The first line is the number of
 * items in the payload:
 *
 *    item_count<nl>
 *
 *  Each item type has its own format as follows.
 
 *  for string items:
 *
 *    item_name item_type item_count isSystemItem? item_length<nl>
 *    string_length1<nl>
 *    string_characters1<nl>
 *     .
 *     .
 *     .
 *    string_lengthN<nl>
 *    string_charactersN<nl>
 *
 *  for binary (converted into text) items:
 *
 *    item_name item_type original_binary_byte_length isSystemItem? item_length<nl>
 *    string_length endian<nl>
 *    string_characters<nl>
 *   
 *  for primitive type items:
 *
 *    item_name item_type item_count isSystemItem? item_length<nl>
 *    value1 value2 ... valueN<nl>
 *
 *  A cMsg message is formatted as a compound payload. Each message has
 *  a number of fields (payload items).
 *
 *  for message items:
 *                                                                   _
 *    item_name item_type item_count isSystemItem? item_length<nl>  /
 *    message1_in_compound_payload_text_format<nl>                 <  field_count<nl>
 *        .                                                         \ list_of_payload_format_items
 *        .                                                          -
 *        .
 *    messageN_in_compound_payload_text_format<nl>
 *
 * Notice that this format allows a message to store a message which stores a message
 * which stores a message, ad infinitum. In other words, recursive message storing.
 * The item_length in each case is the length in bytes of the rest of the item (not
 * including the newline at the end of the header line) - so it does NOT include the
 * header line for the item.
 *
 * </H2><p>
 */  

/* system includes */
#ifdef VXWORKS
#include <vxWorks.h>
#include <taskLib.h>
#include <symLib.h>
#include <symbol.h>
#include <sysSymTbl.h>
#else
#include <strings.h>
#include <dlfcn.h>
#include <inttypes.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

#include "cMsgPrivate.h"
#include "cMsgNetwork.h"

/** Maximum len in chars for a payload item name. */
#define CMSG_PAYLOAD_NAME_LEN 128

/*-------------------------------------------------------------------*/
/* prototypes of static functions */
static int  setFieldsFromText(void *vmsg, const char *text, int flag, char **ptr);
static int  insertItem(cMsgMessage_t *msg, payloadItem *item, int place);
static int  moveItem(cMsgMessage_t *msg, const char *name, int placeFrom, int placeTo);
static int  removeItem(cMsgMessage_t *msg, const char *name, int place, payloadItem **pitem);
static int  setMarker(cMsgMessage_t *msg, const char *name, int place);

static int  addMessage(void *vmsg, char *name, const void *vmessage, int place, int isSystem);
static int  addMessageFromText(void *vmsg, char *name, void *vmessage, char *msgText,
                               int textLength, int place, int isSystem);
static int  addBinary(void *vmsg, const char *name, const char *src, size_t size,
                      int place, int isSystem, int endian);
static int  addBinaryFromString(void *vmsg, const char *name, const char *val, int count,
                                int length, int place, int isSystem, int endian);
static int  addInt(void *vmsg, const char *name, int64_t val, int type, int place, int isSystem);
static int  addIntArray(void *vmsg, const char *name, const int *vals,
                       int type, int len, int place, int isSystem);
static int  addString(void *vmsg, const char *name, const char *val, int place, int isSystem, int copy);
static int  addStringArray(void *vmsg, const char *name, const char **vals,
                           int len, int place, int isSystem, int copy);
static int  addReal(void *vmsg, const char *name, double val, int type, int place, int isSystem);
static int  addRealArray(void *vmsg, const char *name, const double *vals,
                         int type, int len, int place, int isSystem);

static int  getInt(const void *vmsg, int type, int64_t *val);
static int  getReal(const void *vmsg, int type, double *val);
static int  getArray(const void *vmsg, int type, const void **vals, size_t *len);

static int  goodFieldName(const char *s, int isSystem);
static void payloadItemInit(payloadItem *item);
static void payloadItemFree(payloadItem *item);
static int  nameExists(const void *vmsg, const char *name);
static void setPayload(cMsgMessage_t *msg, int hasPayload);
static int  numDigits(int64_t i, int isUint64);
static void grabMutex(void);
static void releaseMutex(void);
static payloadItem *copyPayloadItem(const payloadItem *from);
static void cMsgPayloadPrintout2(void *msg, int level);

/*-------------------------------------------------------------------*/

/** Excluded characters from name strings. */
static const char *excludedChars = " \t\n`\'\"";

/*-------------------------------------------------------------------*/

/** Mutex to make the payload linked list thread-safe. */
#ifdef linux
  static pthread_mutex_t mutex_recursive = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
#elif sun
  static int initialized = 0;
  static pthread_mutex_t mutex_recursive;
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
#elif Darwin
  static pthread_mutex_t mutex_recursive = PTHREAD_MUTEX_INITIALIZER;
#endif

/** Routine to grab the pthread mutex used to protect payload linked list. */
static void grabMutex(void) {
  int status;
#ifdef sun  
  /* if I think the mutex is not initialized, make sure it is */
  if (!initialized) {
    /* make sure our mutex is recursive first */
    status = pthread_mutex_lock(&mutex);
    if (status != 0) {
      cmsg_err_abort(status, "Lock linked list Mutex");
    }
    if (!initialized) {
      /* We need our mutex to be recursive, since cMsgCopyPayload calls copyPayloadItem
       * which can call cMsgCopyMessage which can call cMsgCopyPayload. However, then
       * it needs to be initialized that way (which the static initializer will not
       * do).*/
      pthread_mutexattr_t attr;
      pthread_mutexattr_init(&attr);
      pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
      pthread_mutex_init(&mutex_recursive, &attr);
      initialized = 1;
    }
    status = pthread_mutex_unlock(&mutex);
    if (status != 0) {
      cmsg_err_abort(status, "Unlock linked list Mutex");
    }
  }
#endif  
  status = pthread_mutex_lock(&mutex_recursive);
  if (status != 0) {
    cmsg_err_abort(status, "Lock linked list Mutex");
  }
}

/** Routine to release the pthread mutex used to protect payload linked list. */
static void releaseMutex(void) {
  int status = pthread_mutex_unlock(&mutex_recursive);
  if (status != 0) {
    cmsg_err_abort(status, "Unlock linked list Mutex");
  }
}

/*-------------------------------------------------------------------*/


/**
 * This routine checks a string to see if it is a suitable field name.
 * It returns 0 if it is not, or a 1 if it is. A check is made to see if
 * it contains an unprintable character or any character from a list of
 * excluded characters. All names starting with "cmsg", independent of case,
 * are reserved for use by the cMsg system itself. Names may not be
 * longer than 1024 characters (including null terminator).
 *
 * @param s string to check
 * @param isSystem if true, allows names starting with "cmsg", else not
 *
 * @returns 1 if string is OK
 * @returns 0 if string contains illegal characters
 */   
static int goodFieldName(const char *s, int isSystem) {

  int i, len;

  if (s == NULL) return(0);
  len = strlen(s);

  /* check for printable character */
  for (i=0; i<len; i++) {
    if (isprint((int)s[i]) == 0) return(0);
  }

  /* check for excluded chars */
  if (strpbrk(s, excludedChars) != NULL) return(0);
  
  /* check for starting with cmsg  */
  if (!isSystem) {
    if (strncasecmp(s, "cmsg", 4) == 0) return(0);
  }
  
  /* check for length */
  if (strlen(s) > CMSG_PAYLOAD_NAME_LEN) return(0);
  
  /* string ok */
  return(1);
}



/*-------------------------------------------------------------------*/


/**
 * This routine initializes the given payloadItem data structure.
 * All strings are set to null.
 *
 * @param item pointer to structure holding payload item
 */   
static void payloadItemInit(payloadItem *item) {  
  if (item == NULL) return;

  item->type        = 0;
  item->count       = 0;
  item->length      = 0;
  item->noHeaderLen = 0;
  item->endian      = CMSG_ENDIAN_BIG;
  item->text    = NULL;
  item->name    = NULL;
  item->next    = NULL;
  item->pointer = NULL;
  item->array   = NULL;
  item->val     = 0;
  item->dval    = 0.;
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees the allocated memory of the given payloadItem
 * data structure. 
 *
 * @param item pointer to structure holding payload item info
 */   
static void payloadItemFree(payloadItem *item) {
  if (item == NULL) return;
  
  if (item->text != NULL) {free(item->text);  item->text  = NULL;}
  if (item->name != NULL) {free(item->name);  item->name  = NULL;}
  
  if (item->type != CMSG_CP_STR_A) {
    if (item->array != NULL) {free(item->array); item->array = NULL;}
  }
  else {
    /* with string array, first free individual strings, then array */
    int i;
    for (i=0; i<item->count; i++) {
        free( ((char **)(item->array))[i] );
    }
    free(item->array);
    item->array = NULL;
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine checks to see if a name is already in use by an existing field. 
 *
 * @param vmsg pointer to message
 * @param name name to check
 *
 * @returns 0 if name does not exist
 * @returns 1 if name exists
 */   
static int nameExists(const void *vmsg, const char *name) {  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  payloadItem *item;
  
  grabMutex();
  
  if (msg == NULL || msg->payload == NULL) {
    releaseMutex();
    return(0);
  }
  
  item = msg->payload;
  while (item != NULL) {
    if (strcmp(item->name, name) == 0) {
      releaseMutex();
      return(1);
    }
    item = item->next;
  }
 
  releaseMutex();
  return(0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine sets the "has-a-compound-payload" field of a message. 
 *
 * @param msg pointer to message
 * @param hasPayload boolean (0 if has no compound payload, else has payload)
 */   
static void setPayload(cMsgMessage_t *msg, int hasPayload) {  
  
  if (msg == NULL) return;
  
  msg->info = hasPayload ? msg->info |  CMSG_HAS_PAYLOAD :
                           msg->info & ~CMSG_HAS_PAYLOAD;

  return;
}


/*-------------------------------------------------------------------*/


/**
 * This routine returns whether a message has a compound payload or not. 
 *
 * @param vmsg pointer to message
 * @param hasPayload pointer which gets filled with 1 if msg has compound payload, else 0
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgHasPayload(const void *vmsg, int *hasPayload) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || hasPayload == NULL) return(CMSG_BAD_ARGUMENT);
  
  *hasPayload = ((msg->info & CMSG_HAS_PAYLOAD) == CMSG_HAS_PAYLOAD) ? 1 : 0;

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees the allocated memory of the given message's entire payload
 * and then initializes the payload components of the message. 
 *
 * @param vmsg pointer to message
 */   
void cMsgPayloadClear(void *vmsg) {
  payloadItem *item, *next;
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  grabMutex();
  
  if (msg == NULL || msg->payload == NULL)  {
    releaseMutex();
    return;
  }
  
  item = msg->payload;
  while (item != NULL) {
    next = item->next;
    payloadItemFree(item);
    free(item);
    item = next;
  }
  msg->payload = NULL;
  msg->marker  = NULL;
  
  /* write that we no longer have a payload */
  setPayload(msg, 0);
  
  releaseMutex();
}


/*-------------------------------------------------------------------*/


/**
 * This routine returns the number of digits in an integer
 * including a minus sign.
 *
 * @param number integer
 * @param isUint64 is number an unsigned 64 bit integer (0=no)
 * @returns number of digits in the integer argument including a minus sign
 */   
static int numDigits(int64_t number, int isUint64) {
  int digits = 1;
  uint64_t step = 10;
  
  if (isUint64) {
    uint64_t num = (uint64_t) number;
    while (step <= num) {
      if (++digits >= 20) return 20; /* 20 digits at most in uint64_t */
      step *= 10;
    }
    return digits;
  }
  
  if (number < 0) {
    digits++;
    number *= -1;
  }
  
  while (step <= number) {
	  digits++;
	  step *= 10;
  }
  return digits;
}

/**
* Counts number decimal digits in a 32 bit signed number.
* 0 => 1, 9 => 1, 99 => 2, 2,147,483,647 => 10
* @param x number whose digits you wish to count.
* Must lie in range 0 .. Integer.MAX_VALUE;
*
* @return number of digits in x, e.g. Integer.toString(x).length()
*/
static int widthInDigits (const int x ) {
   /* do an unravelled binary search */
   if ( x < 10000 ) {
      if ( x < 100 ) {
         if ( x < 10 ) return 1;
         else return 2;
      }
      else {
         if ( x < 1000 ) return 3;
         else return 4;
      }
   }
   else {
      if ( x < 1000000 ) {
         if ( x < 100000 ) return 5;
         else return 6;
      }
      else {
         if ( x < 100000000 ) {
            if ( x < 10000000 ) return 7;
            else return 8;
         }
         else {
            if ( x < 1000000000 ) return 9;
            else return 10;
         }
      }
   }
}

/*-------------------------------------------------------------------*/


/**
 * This routine gets the position of the current field.
 *
 * @param vmsg pointer to message
 * @param position pointer which gets filled in with the positin of the current field
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_ARGUMENT if either argument is NULL
 */   
int cMsgGetFieldPosition(const void *vmsg, int *position) {
  int i=0;
  payloadItem *item;
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || position == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  if (msg->marker == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }

  item = msg->payload;
  while (item != NULL) {
    if (item == msg->marker) {
      *position = i;
      releaseMutex();
      return(CMSG_OK);
    }
    i++;
    item = item->next;
  }
  
  releaseMutex();
  return(CMSG_ERROR);  
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine places the current position at the named field if its exists.
 *
 * @param vmsg pointer to message
 * @param name name of field to find
 *
 * @returns 1 if successful
 * @returns 0 if no field with that name was found
 */   
int cMsgGoToFieldName(void *vmsg, const char *name) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL) return(0);
  
  return setMarker(msg, name, -1);  
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine places the current position at the numbered field if it exists.
 *
 * @param vmsg pointer to message
 * @param position position of payload item to find (first = 0),
 *                 or CMSG_CP_END to find the last item
 *
 * @returns 1 if successful
 * @returns 0 if no such numbered field exists
 */   
int cMsgGoToField(void *vmsg, int position) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(0);
  if (position < 0 && position != CMSG_CP_END) return(0);
  
  return setMarker(msg, NULL, position);  
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine places the current position at the next field if its exists.
 *
 * @param vmsg pointer to message
 *
 * @returns 1 if successful
 * @returns 0 if no next field
 */   
int cMsgNextField(void *vmsg) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(0);
  
  return setMarker(msg, NULL, CMSG_CP_NEXT);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine tells whether the next field exists.
 *
 * @param vmsg pointer to message
 *
 * @returns 1 if next field exists
 * @returns 0 if no next field
 */   
int cMsgHasNextField(const void *vmsg) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  grabMutex();
  
  if (msg == NULL || msg->marker == NULL) {
    releaseMutex();
    return(0);
  }
  
  if (msg->marker->next != NULL) {
    releaseMutex();
    return(1);
  }
  
  releaseMutex();
  return(0);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine places the current position at the first field if its exists.
 *
 * @param vmsg pointer to message
 *
 * @returns 1 if successful
 * @returns 0 if no payload fields
 */   
int cMsgFirstField(void *vmsg) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(0);
  
  return setMarker(msg, NULL, 0);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine removes the named field if its exists.
 *
 * @param vmsg pointer to message
 * @param name name of field to remove
 *
 * @returns 1 if successful
 * @returns 0 if no field with that name was found
 */   
int cMsgRemoveFieldName(void *vmsg, const char *name) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL) return(0);
  
  return removeItem(msg, name, -1, NULL);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine removes the field at the given position if it exists.
 *
 * @param vmsg pointer to message
 * @param position position in list of payload items (first = 0) or 
 *                 CMSG_CP_END if last item, or CMSG_CP_MARKER if item at marker
 *
 * @returns 1 if successful
 * @returns 0 if no numbered field
 */   
int cMsgRemoveField(void *vmsg, int position) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(0);
  if (position < 0 && position != CMSG_CP_END) return(0);
  
  return removeItem(msg, NULL, position, NULL);
  
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine moves the named field if its exists.
 *
 * @param vmsg pointer to message
 * @param name name of field to move
 * @param placeTo   place in list of payload items (first = 0,
 *                  CMSG_CP_END if last item, CMSG_CP_MARKER if after item at marker)
 *                  to move field to
 *
 * @returns 1 if successful
 * @returns 0 if field doesn't exist or cannot be placed at the desired location
 */   
int cMsgMoveFieldName(void *vmsg, const char *name, int placeTo) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL) return(0);
  
  return moveItem(msg, name, -1, placeTo);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine moves the field at the given position if it exists.
 *
 * @param vmsg pointer to message
 * @param placeFrom place in list of payload items (first = 0,
 *                  CMSG_CP_END if last item, CMSG_CP_MARKER if item at marker)
 *                  of field to move (if "name" not NULL)
 * @param placeTo   place in list of payload items (first = 0,
 *                  CMSG_CP_END if last item, CMSG_CP_MARKER if after item at marker)
 *                  to move field to
 *
 * @returns 1 if successful
 * @returns 0 if field doesn't exist or cannot be placed at the desired location
 */   
int cMsgMoveField(void *vmsg, int placeFrom, int placeTo) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(0);
  
  return moveItem(msg, NULL, placeFrom, placeTo);
  
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine sets the marker (position) to a given payload item. If the "name"
 * argument is not null, that is where the marker will be set, otherwise
 * the marker will be set to the item at "place".
 * Error checking of arguments done by calling routines.
 *
 * @param msg message handle
 * @param name name of payload item to mark; if name is null "place" arg is used
 * @param place place in list of payload items (first = 0), 
 *              CMSG_CP_END if last item, or
 *              CMSG_CP_NEXT if item past current marker
 *
 * @returns 1 if successful
 * @returns 0 if item doesn't exist
 */   
static int setMarker(cMsgMessage_t *msg, const char *name, int place) {
  int i=0, found=0;
  payloadItem *item;
  
  /* make changing linked-list thread-safe */
  grabMutex();
  
  if (msg->payload == NULL) {
    releaseMutex();
    return(0);
  }

  /* mark the item "name" if there is one */
  if (name != NULL) {
    item = msg->payload;
    while (item != NULL) {
      if (strcmp(item->name, name) == 0) {
        found = 1;
        break;
      }
      item = item->next;
    }
    if (!found) {
      releaseMutex();
      return(0);
    }
    msg->marker = item;
  }
  /* or mark the last item */
  else if (place == CMSG_CP_END) {
    item = msg->payload;
    while (item->next != NULL) {
      item = item->next;
    }
    msg->marker = item;
  }
  /* or mark the next item */
  else if (place == CMSG_CP_NEXT) {
    if (msg->marker == NULL || msg->marker->next == NULL) {
      releaseMutex();
      return(0);
    }
    msg->marker = msg->marker->next;
  }
  /* or mark first item ... */
  else if (place == 0) {
    msg->marker = msg->payload;
  }
  /* or mark elsewhere ... */
  else if (place > 0) {
    item = msg->payload;
    while (item != NULL) {
      if (i == place) {
        msg->marker = item;
        break;
      }
      i++;
      item = item->next;
    }
  }
  else {
    releaseMutex();
    return(0);
  }
  
  releaseMutex();
  return(1);
}


/*-------------------------------------------------------------------*/


/**
 * This routine takes a given payload item and inserts it into an
 * existing message's payload in the given order or place. Error
 * checking of arguments done by calling routines.
 * Marker is placed at the item inserted.
 *
 * @param msg message handle
 * @param item payload item to add
 * @param place place in list of payload items (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after the marker
 *
 * @returns 1 if successful
 * @returns 0 if item cannot be placed at the desired location
 */   
static int insertItem(cMsgMessage_t *msg, payloadItem *item, int place) {
  int i=0, found=0;
  payloadItem *prev=NULL, *next=NULL;

  /* placing payload item in msg's linked list requires mutex protection */
  grabMutex();

  /* put it at the end ... */
  if (place == CMSG_CP_END) {
    if (msg->payload == NULL) {
      msg->payload = item;
    }
    else {
      prev = msg->payload;
      while (prev->next != NULL) {
        prev = prev->next;
      }
      prev->next = item;
    }
    msg->marker = item;
  }
  /* put it after marker ... */
  else if (place == CMSG_CP_MARKER) {
    if (msg->payload == NULL) {
      msg->payload = item;
    }
    else {
      prev = msg->marker;
      next = prev->next;
      prev->next = item;
      item->next = next;
    }
    msg->marker = item;
  }
  /* put it at the beginning ... */
  else if (place == 0) {
    if (msg->payload == NULL) {
      msg->payload = item;
    }
    else {
      next = msg->payload;
      msg->payload = item;
      item->next = next;
    }
    msg->marker = item;
  }
  /* put it somewhere in the middle ... */
  else if (place > 0) {
    prev = msg->payload;
    while (prev != NULL) {
      if (i == place-1) {
        found = 1;
        break;
      }
      i++;
      prev = prev->next;
    }

    if (!found) {
      releaseMutex();
      return(0);
    }

    next = prev->next;
    prev->next  = item;
    item->next  = next;
    msg->marker = item;
  }
  else {
    releaseMutex();
    return(0);
  }
  
  /* store in msg struct that this msg has a compound payload */
  setPayload(msg, 1);  
  
  releaseMutex();
  return(1);
}


/*-------------------------------------------------------------------*/


/**
 * This routine finds a given payload item and removes it from an existing
 * message's payload. If the "name" argument is not null, that is the payload
 * item that will be removed, otherwise the item at "place" will be removed.
 * Error checking of arguments done by calling routines.
 * If the marker is at deleted item, it will point to the next item if
 * there is one, or the last item if not. It can return a pointer to the
 * removed item.
 *
 * @param msg message handle
 * @param name name of payload item to remove; if name is null "place" arg is used
 * @param place place of item to remove in list of payload items (first = 0),
 *              CMSG_CP_END if last item, or CMSG_CP_MARKER if item at marker
 * @param pItem if not NULL, it is filled with removed item, else the removed item is freed
 *
 * @returns 1 if successful
 * @returns 0 if item doesn't exist
 */   
static int removeItem(cMsgMessage_t *msg, const char *name, int place, payloadItem **pItem) {
  int i=0, found=0, spot=0;
  payloadItem *item, *prev, *next;
  
  grabMutex();
  
  if (msg->payload == NULL) {
    releaseMutex();
    return(0);
  }

  /* find the place "name" if there is one */
  if (name != NULL) {
    item = msg->payload;
    while (item != NULL) {
      if (strcmp(item->name, name) == 0) {
        found = 1;
        break;
      }
      spot++;
      item = item->next;
    }
    if (!found) {
      releaseMutex();
      return(0);
    }
    place = spot;
  }
  /* or find the place of the last item */
  else if (place == CMSG_CP_END) {
    item = msg->payload;
    while (item->next != NULL) {
      item = item->next;
      spot++;
    }
    place = spot;
  }
  /* or find the place of the marker item */
  else if (place == CMSG_CP_MARKER) {
    if (msg->marker == NULL) {
      releaseMutex();
      return(0);
    }
    found = 0;
    item  = msg->payload;
    while (item != NULL) {
      if (item == msg->marker) {
        found = 1;
        break;
      }
      item = item->next;
      spot++;
    }
    place = spot;
  }
  
  /* remove first item ... */
  if (place == 0) {
    /* remove item from linked list */
    item = msg->payload;
    next = item->next;
    msg->payload = next;
    
    /* set marker if the item it pointed to was deleted */
    if (msg->marker == item) {
      msg->marker = next;
    }
    
    /* free allocated memory or return item */
    if (pItem == NULL) {
      payloadItemFree(item);
      free(item);
    } else {
      *pItem = item;
    }
  }
  /* remove from elsewhere ... */
  else if (place > 0) {
    found = 0;
    
    /* remove item from linked list */
    prev = msg->payload;
    while (prev != NULL) {
      if (i == place-1) {
        found = 1;
        break;
      }
      i++;
      prev = prev->next;
    }

    if (!found) {
      releaseMutex();
      return(0);
    }
    
    item = prev->next;
    if (item == NULL) {
      releaseMutex();
      return(0);    
    }

    next = item->next;
    prev->next = next;
    
    /* reset marker */
    if (msg->marker == item) {
      if (next != NULL) {
        msg->marker = next;
      }
      else {
        msg->marker = prev;
      }
    }
    
    /* free allocated memory or return item */
    if (pItem == NULL) {
      payloadItemFree(item);
      free(item);
    } else {
      *pItem = item;
    }
  }
  else {
    releaseMutex();
    return(0);
  }
  
  /* store in msg struct that this msg does NOT have a compound payload anymore */
  if (msg->payload == NULL) setPayload(msg, 0);  
  
  releaseMutex();
  return(1);
}


/*-------------------------------------------------------------------*/


/**
 * This routine moves a given payload item from one place in the order
 * of payload items. If the marker is at the moved item, it moves with
 * the item. If the "name" argument is not null, that is the payload
 * item that will be moved, otherwise the item at "placeFrom" will be moved.
 *
 * @param msg message handle
 * @param name name of payload item to remove; if name is null "place" arg is used
 * @param placeFrom place in list of payload items (first = 0,
 *                  CMSG_CP_END if last item, CMSG_CP_MARKER if item at marker)
 *                  of item to move (if "name" not NULL)
 * @param placeTo   place in list of payload items (first = 0,
 *                  CMSG_CP_END if last item, CMSG_CP_MARKER if after item at marker)
 *                  to move item to
 *
 * @returns 1 if successful
 * @returns 0 if item doesn't exist or cannot be placed at the desired location
 */   
static int moveItem(cMsgMessage_t *msg, const char *name, int placeFrom, int placeTo) {
  int ok;
  payloadItem *item;
  
  if (name == NULL) {
    if (placeFrom == placeTo) return(1);
    if (placeFrom < 0 && placeFrom != CMSG_CP_END && placeFrom != CMSG_CP_MARKER) return(0);
  }
  if (placeTo < 0 && placeTo != CMSG_CP_END && placeTo != CMSG_CP_MARKER) return(0);
  
  ok = removeItem(msg, name, placeFrom, &item);
  if (!ok) return(ok);
  
  ok = insertItem(msg, item, placeTo);
  if (!ok) return(ok);
  
  return(1);
}


/*-------------------------------------------------------------------*/

#define CMSG_SYSTEM_FIELDS  0
#define CMSG_PAYLOAD_FIELDS 1
#define CMSG_BOTH_FIELDS    2

/**
 * This routine takes a pointer to a string representation of the
 * whole compound payload, including the system (hidden) fields of the message,
 * as it gets sent over the network and converts it into the standard message
 * payload. This overwrites any existing payload and may set system fields
 * as well depending on the given flag.
 *
 * @param vmsg pointer to message
 * @param text string sent over network to be unmarshalled
 * @param flag if 0, set system msg fields only, if 1 set payload msg fields only,
 *             and if 2 set both
 * @param ptr pointer to next line (used for recursive unmarshalling of messages,
 *            since a payload may contain a msg which contains a payload, etc).
 *            If ptr == text, then there was nothing more to parse/unmarshal.
 *
 * @returns CMSG_OK             if successful
 * @returns CMSG_ERROR          if payload item cannot be placed at the desired location
 * @returns CMSG_OUT_OF_MEMORY  if out of memory
 * @returns CMSG_BAD_ARGUMENT   if the msg and/or text argument is NULL
 * @returns CMSG_ALREADY_EXISTS if text contains name that is being used already
 * @returns CMSG_BAD_FORMAT     if the text is in the wrong format or contains values
 *                              that don't make sense such as place < 0 and
 *                              place != CMSG_CP_END and place != CMSG_CP_MARKER
 */   
static int setFieldsFromText(void *vmsg, const char *text, int flag, char **ptr) {
  char *s, *t, *tt, *pmsgTxt, name[CMSG_PAYLOAD_NAME_LEN+1];
  int i, j, err, len, val, type, count, fields, ignore;
  int totalLen, isSystem, msgTxtLen, numChars, debug=0;
  int64_t   int64;
  uint64_t uint64;
  double   dbl;
  float    flt;
  
  /* payloadItem *item, *prev; */
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || text == NULL) return(CMSG_BAD_ARGUMENT);
  
  t = text;
  s = strpbrk(t, "\n");
  /* nothing to parse */
  if (s == NULL) {
    if (ptr != NULL) *ptr = text;
    return(CMSG_OK);
  }
  
  /* read number of fields to come */
  sscanf(t, "%d", &fields);
  if (fields < 1) return(CMSG_BAD_FORMAT);
if(debug) printf("# fields = %d\n", fields);
  
  t = s+1;
  s = strpbrk(t, "\n");
  if (s == NULL) return(CMSG_BAD_FORMAT);
  
  /* get rid of any existing payload */
  cMsgPayloadClear(vmsg);
  
  for (i=0; i<fields; i++) {
    /* store in case field is a message and need to get the text */
    pmsgTxt = t;
    
    /* read line */
    memset(name, 0, CMSG_PAYLOAD_NAME_LEN+1);
    sscanf(t, "%s%d%d%d%d%n", name, &type, &count, &isSystem, &totalLen, &msgTxtLen);
if(debug) printf("FIELD #%d, name = %s, type = %d, count = %d, isSys = %d, len = %d, t = %p\n",
                 i, name, type, count, isSystem, totalLen, t);
    
    if (strlen(name) < 1 || count < 1 || totalLen < 1 ||
        type < CMSG_CP_STR || type > CMSG_CP_MSG_A) return(CMSG_BAD_FORMAT);
    
    /* ignore certain fields (by convention, system fields start with "cmsg") */
    /* isSystem = strncasecmp(name, "cmsg", 4) == 0 ? 1 : 0; */
    ignore = isSystem;               /* by default ignore a system field, for flag == 1 */
    if (flag == 0) ignore = !ignore; /* only set system fields, for flag = 0*/
    else if (flag == 2) ignore = 0;  /* deal with all fields, for flag = 2 */
    
    /* skip over fields to be ignored */
    if (ignore) {
      for (j=0; j<count; j++) {
        /* skip over field */
        t = s+1+totalLen+1;
        s = strpbrk(t, "\n");
        if (s == NULL) return(CMSG_BAD_FORMAT);
if(debug) printf("  skipped field\n");
      }
      continue;
    }

    /* READ IN STRINGS */
    if (type == CMSG_CP_STR || type == CMSG_CP_STR_A) {

      t = s+1;
      s = strpbrk(t, "\n");
      if (s == NULL) return(CMSG_BAD_FORMAT);
     
      /* single string */
      if (type == CMSG_CP_STR) {
          char *txt;
          
          /* read length of string */
          sscanf(t, "%d", &len);
          if (len < 1) return(CMSG_BAD_FORMAT);
          t = s+1;
          
          {
            txt = (char *) malloc(len+1);
            if (txt == NULL) return(CMSG_OUT_OF_MEMORY);
            memcpy(txt, t, len);
            txt[len] = '\0';
if(debug)  printf("  string = %s, length = %d, t = %p\n", txt, len, t);
           
            /* special case where cMsgText payload field is the message text */
            if (isSystem) {
              if (strcmp(name, "cMsgText") == 0) {
                if (msg->text != NULL) free(msg->text);
                msg->text = txt;
              }
              else if (strcmp(name, "cMsgSubject") == 0) {
                if (msg->subject != NULL) free(msg->subject);
                msg->subject = txt;
              }
              else if (strcmp(name, "cMsgType") == 0) {
                if (msg->type != NULL) free(msg->type);
                msg->type = txt;
              }
              else if (strcmp(name, "cMsgDomain") == 0) {
                /* no routines to set domain since it's illegal, so set it by hand */
                if (msg->domain != NULL) free(msg->domain);
                msg->domain = txt;
              }
              else if (strcmp(name, "cMsgCreator") == 0) {
                /* set it by hand */
                if (msg->creator != NULL) free(msg->creator);
                msg->creator = txt;
              }
              else if (strcmp(name, "cMsgSender") == 0) {
                /* set it by hand */
                if (msg->sender != NULL) free(msg->sender);
                msg->sender = txt;
              }
              else if (strcmp(name, "cMsgSenderHost") == 0) {
                /* set it by hand */
                if (msg->senderHost != NULL) free(msg->senderHost);
                msg->senderHost = txt;
              }
              else if (strcmp(name, "cMsgReceiver") == 0) {
                /* set it by hand */
                if (msg->receiver != NULL) free(msg->receiver);
                msg->receiver = txt;
              }
              else if (strcmp(name, "cMsgReceiverHost") == 0) {
                /* set it by hand */
                if (msg->receiverHost != NULL) free(msg->receiverHost);
                msg->receiverHost = txt;
              }
            }
            else {
              err = addString(vmsg, name, txt, CMSG_CP_MARKER, 1, 0);
              if (err != CMSG_OK) {
                if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
                return(err);
              }
            }
          }
          
          t = s+1+len+1;
          s = strpbrk(t, "\n");
          /* s will be null if it's the very last item */
          if (s == NULL && i != fields-1) return(CMSG_BAD_FORMAT);
if(debug) printf("  skip to t = %p\n", t);
      }
      /* array of strings */
      else {
        char **txtArray = (char **) calloc(count, sizeof(char *));
        if (txtArray == NULL) return(CMSG_OUT_OF_MEMORY);
        
        for (j=0; j<count; j++) {
          /* read length of string */
          sscanf(t, "%d", &len);
          if (len < 1) return(CMSG_BAD_FORMAT);
          t = s+1;          
          
          txtArray[j] = (char *)malloc(len+1);
          if (txtArray[j] == NULL) {
            free(txtArray);
            return(CMSG_OUT_OF_MEMORY);
          }
          memcpy(txtArray[j], t, len);
          txtArray[j][len] = '\0';
if(debug) printf("  string[%d] = %s, length = %d, t = %p\n", j, txtArray[j], strlen(txtArray[j]), t);
          
          t = s+1+len+1;
          s = strpbrk(t, "\n");
          /* s will be null if it's the very last item */
          if (s == NULL && i != fields-1  && j != count-1) return(CMSG_BAD_FORMAT);
        }

        /* add array to payload (cast to avoid compiler warning )*/
        err = addStringArray(vmsg, name, (const char **)txtArray, count, CMSG_CP_MARKER, 1, 0);
        if (err != CMSG_OK) {
          if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
          return(err);
        }
      }
    }
    
    /* READ IN BINARY DATA */
    else if (type == CMSG_CP_BIN) {
      int endian;
      /* move to next line to read number(s) */
      t = s+1;
      s = strpbrk(t, "\n");
      if (s == NULL) return(CMSG_BAD_FORMAT);
     
      /* read length of string & endian */
      sscanf(t, "%d %d", &len, &endian);
      if (len < 1) return(CMSG_BAD_FORMAT);
if(debug) printf("  len = %d, endian = %d, t = %p\n", len, endian, t);
      t = s+1;
if(debug) {
  /* read only "len" # of chars */
  char txt[len+1];
  memcpy(txt, t, len);
  txt[len] = '\0';
  printf("  bin as string = %s, length = %d, t = %p\n", txt, strlen(txt), t);
}
      err = addBinaryFromString(vmsg, name, t, count, len, CMSG_CP_MARKER, 1, endian);
      if (err != CMSG_OK) {
        if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
        return(err);
      }

      t = s+1+len+1;
      s = strpbrk(t, "\n");
      /* s will be null if it's the very last item */
      if (s == NULL && i != fields-1) return(CMSG_BAD_FORMAT);
if(debug) printf("  skip to t = %p\n", t);
    }
    
    /* READ IN MESSAGE DATA */
    else if (type == CMSG_CP_MSG) {
      char *endptr, *ptext;
      void *newMsg;
      intptr_t diff;
      
      /* save beginning pointer */
      ptext = s+1;

      /* create a single message */
      newMsg = cMsgCreateMessage();
      if (newMsg == NULL) return(CMSG_OUT_OF_MEMORY);
if(debug) printf("\n**** included msg ****\n\n");

      /* recursive call to setFieldsFromText to fill msg's fields */
      setFieldsFromText(newMsg, ptext, CMSG_BOTH_FIELDS, &endptr);
      if (endptr == t) {
        /* nothing more to parse */
      }
if(debug) printf("\n**** end included msg ****\n\n");
      diff = endptr - pmsgTxt;

if(debug) {
  /* read only "len" # of chars */
  char txt[diff+1];
  memcpy(txt, pmsgTxt, diff);
  txt[diff] = '\0';
  printf("endptr = %p, pmsgTxt = %p, diff = %d\n", endptr, pmsgTxt, diff);
  printf("adding the following text as part of the message being added:\n");
  printf("msg as string =\n%s\nlength = %d, t = %p\n", txt, strlen(txt), t);
  printf("\nnewMsg %p (before adding as a payload item)\n\n", newMsg);
  cMsgPayloadPrintout(newMsg);
}
      err = addMessageFromText(vmsg, name, newMsg, pmsgTxt,
                               diff, CMSG_CP_MARKER, 1);
      if (err != CMSG_OK) {
        if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
        return(err);
      }
      t = endptr;
      s = strpbrk(t, "\n");
      /* s will be null if it's the very last item */
      if (s == NULL && i != fields-1) return(CMSG_BAD_FORMAT);
      
if(debug) printf("  skip to t = %p\n", t);
    }
    
    /* READ IN NUMBERS */
    else {
      
      /* move to next line to read number(s) */
      t = s+1;
      s = strpbrk(t, "\n");
      if (s == NULL) return(CMSG_BAD_FORMAT);
      
      /* reals */
      if (type == CMSG_CP_FLT || type == CMSG_CP_DBL) {
          sscanf(t, "%lg", &dbl);
if(debug) printf("read dbl/flt as %.16lg\n", dbl);

          err = addReal(vmsg, name, dbl, type, CMSG_CP_MARKER, 1);  
          if (err != CMSG_OK) {
            if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
            return(err);
          }
      }

      /* double array */
      else if (type == CMSG_CP_DBL_A) {          
          double myArray[count];

          tt = t;
          for (j=0; j<count; j++) {
            /* read next double */
            sscanf(tt, "%lg%n", &dbl, &numChars);
if(debug) printf("  double[%d] = %.16lg, numChars = %d, t = %p\n", j, dbl, numChars, tt);
            tt += numChars + 1; /* go forward # of chars in number + space */
            myArray[j] = dbl;
          }

          /* add array to payload */
          err = addRealArray(vmsg, name, myArray, type, count, CMSG_CP_MARKER, 1); 
          if (err != CMSG_OK) {
            if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
            return(err);
          }
      }

      /* float array */
      else if (type == CMSG_CP_FLT_A) {          
          float myArray[count];

          tt = t;
          for (j=0; j<count; j++) {
            /* read next float */
            sscanf(tt, "%g%n", &flt, &numChars);
if(debug) printf("  float[%d] = %.7g, numChars = %d, t = %p\n", j, flt, numChars, tt);
            tt += numChars + 1; /* go forward # of chars in number + space */
            myArray[j] = flt;
          }

          /* add array to payload */
          err = addRealArray(vmsg, name, (double *)myArray, type, count, CMSG_CP_MARKER, 1); 
          if (err != CMSG_OK) {
            if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
            return(err);
          }
      }

      /* signed ints & smaller unsigned ints */
      else if (type == CMSG_CP_INT8   || type == CMSG_CP_INT16  ||
               type == CMSG_CP_INT32  || type == CMSG_CP_INT64  ||
               type == CMSG_CP_UINT8  || type == CMSG_CP_UINT16 ||
               type == CMSG_CP_UINT32)  {
          /* read int */
          sscanf(t, "%lld", &int64);
if(debug) printf("read int as %lld\n", int64);

          err = addInt(vmsg, name, int64, type, CMSG_CP_MARKER, 1);  
          if (err != CMSG_OK) {
            if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
            return(err);
          }
      }
      
      /* unsigned 64 bit int */
      else if (type == CMSG_CP_UINT64) {
          /* read int */
          sscanf(t, "%llu", &uint64);
if(debug) printf("read int as %llu\n", uint64);

          err = addInt(vmsg, name, (int64_t) uint64, type, CMSG_CP_MARKER, 1);  
          if (err != CMSG_OK) {
            if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
            return(err);
          }
      }
      
      /* 8-bit, signed int array */
      else if (type == CMSG_CP_INT8_A) {
        /* array in which to store numbers */
        int8_t myArray[count];

        tt = t;
        for (j=0; j<count; j++) {
          /* read next int */
          sscanf(tt, "%d%n", &val, &numChars);
if(debug) printf("  int8[%d] = %d, numDigits(%d) = %d, t = %p\n", j, val, val, numChars, tt);
          tt += numChars + 1; /* go forward # of chars in number + space */
          myArray[j] = (int8_t) val;
        }

        /* add array to payload */
        err = addIntArray(vmsg, name, (const int *)myArray, type, count, CMSG_CP_MARKER, 1); 
        if (err != CMSG_OK) {
          if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
          return(err);
        }
      }

      /* 16-bit, signed int array */
      else if (type == CMSG_CP_INT16_A) {
        /* array in which to store numbers */
        int16_t val16, myArray[count];

        tt = t;
        for (j=0; j<count; j++) {
          /* read next int */
          sscanf(tt, "%hd%n", &val16, &numChars);
if(debug) printf("  int16[%d] = %hd, numDigits(%hd) = %d, t = %p\n", j, val16, val16, numChars, tt);
          tt += numChars + 1; /* go forward # of chars in number + space */
          myArray[j] = val16;
        }

        /* add array to payload */
        err = addIntArray(vmsg, name, (const int *)myArray, type, count, CMSG_CP_MARKER, 1); 
        if (err != CMSG_OK) {
          if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
          return(err);
        }
      }

      /* 32-bit, signed int array */
      else if (type == CMSG_CP_INT32_A) {
        /* array in which to store numbers */
        int32_t val32, myArray[count];

        tt = t;
        for (j=0; j<count; j++) {
          /* read next int */
          sscanf(tt, "%d%n", &val32, &numChars);
if(debug) printf("  int32[%d] = %d, numDigits(%d) = %d, t = %p\n", j, val32, val32, numChars, tt);
          tt += numChars + 1; /* go forward # of chars in number + space */
          myArray[j] = val32;
        }
        
        if (isSystem) {
          if (strcmp(name, "cMsgInts") == 0) {
            if (count != 5) {
              return(CMSG_BAD_FORMAT);            
            }
            msg->version         = myArray[0];
            msg->info            = myArray[1];
            msg->reserved        = myArray[2];
            msg->byteArrayLength = myArray[3];
            msg->userInt         = myArray[4];
          }
        }
        else {
          /* add array to payload */
          err = addIntArray(vmsg, name, (const int *)myArray, type, count, CMSG_CP_MARKER, 1); 
          if (err != CMSG_OK) {
            if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
            return(err);
          }
        }
      }
      
      /* 64-bit, signed int array */
      else if (type == CMSG_CP_INT64_A) {
        /* array in which to store numbers */
        int64_t val64, myArray[count];

        tt = t;
        for (j=0; j<count; j++) {
          /* read next int */
          sscanf(tt, "%lld%n", &val64, &numChars);
if(debug) printf("  int64[%d] = %lld, numDigits(%lld) = %d, t = %p\n", j, val64, val64, numChars, tt);
          tt += numChars + 1; /* go forward # of chars in number + space */
          myArray[j] = val64;
        }

        if (isSystem) {
          if (strcmp(name, "cMsgTimes") == 0) {
            if (count != 6) {
              return(CMSG_BAD_FORMAT);            
            }
            msg->userTime.tv_sec      = myArray[0];
            msg->userTime.tv_nsec     = myArray[1];
            msg->senderTime.tv_sec    = myArray[2];
            msg->senderTime.tv_nsec   = myArray[3];
            msg->receiverTime.tv_sec  = myArray[4];
            msg->receiverTime.tv_nsec = myArray[5];
          }
        }
        else {
          /* add array to payload */
          err = addIntArray(vmsg, name, (const int *)myArray, type, count, CMSG_CP_MARKER, 1); 
          if (err != CMSG_OK) {
            if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
            return(err);
          }
        }
      }

      /* 8-bit, unsigned int array */
      else if (type == CMSG_CP_UINT8_A) {
        /* array in which to store numbers */
        uint8_t myArray[count];

        tt = t;
        for (j=0; j<count; j++) {
          /* read next int */
          sscanf(tt, "%d%n", &val, &numChars);
if(debug) printf("  uint8[%d] = %d, numDigits(%d) = %d, t = %p\n", j, val, val, numChars, tt);
          tt += numChars + 1; /* go forward # of chars in number + space */
          myArray[j] = (uint8_t) val;
        }

        /* add array to payload */
        err = addIntArray(vmsg, name, (const int *)myArray, type, count, CMSG_CP_MARKER, 1); 
        if (err != CMSG_OK) {
          if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
          return(err);
        }
      }

      /* 16-bit, unsigned int array */
      else if (type == CMSG_CP_UINT16_A) {
        /* array in which to store numbers */
        uint16_t val16, myArray[count];

        tt = t;
        for (j=0; j<count; j++) {
          /* read next int */
          sscanf(tt, "%hu%n", &val16, &numChars);
if(debug) printf("  uint16[%d] = %hu, numDigits(%hu) = %d, t = %p\n", j, val16, val16, numChars, tt);
          tt += numChars + 1; /* go forward # of chars in number + space */
          myArray[j] = val16;
        }

        /* add array to payload */
        err = addIntArray(vmsg, name, (const int *)myArray, type, count, CMSG_CP_MARKER, 1); 
        if (err != CMSG_OK) {
          if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
          return(err);
        }
      }

      /* 32-bit, unsigned int array */
      else if (type == CMSG_CP_UINT32_A) {
        /* array in which to store numbers */
        uint32_t val32, myArray[count];

        tt = t;
        for (j=0; j<count; j++) {
          /* read next int */
          sscanf(tt, "%u%n", &val32, &numChars);
if(debug) printf("  uint32[%d] = %u, numDigits(%u) = %d, t = %p\n", j, val32, val32, numChars, tt);
          tt += numChars + 1; /* go forward # of chars in number + space */
          myArray[j] = val32;
        }

        /* add array to payload */
        err = addIntArray(vmsg, name, (const int *)myArray, type, count, CMSG_CP_MARKER, 1); 
        if (err != CMSG_OK) {
          if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
          return(err);
        }
      }

      /* 64-bit, unsigned int array */
      else if (type == CMSG_CP_UINT64_A) {
        /* array in which to store numbers */
        uint64_t val64, myArray[count];

        tt = t;
        for (j=0; j<count; j++) {
          /* read next int */
          sscanf(tt, "%llu%n", &val64, &numChars);
if(debug) printf("  uint64[%d] = %llu, numDigits(%llu) = %d, t = %p\n", j, val64, val64, numChars, tt);
          tt += numChars + 1; /* go forward # of chars in number + space */
          myArray[j] = val64;
        }

        /* add array to payload */
        err = addIntArray(vmsg, name, (const int *)myArray, type, count, CMSG_CP_MARKER, 1); 
        if (err != CMSG_OK) {
          if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
          return(err);
        }
      }

      /* go to the next line */
      t = s+1;
      s = strpbrk(t, "\n");
      /* s will be null if it's the very last item */
      if (s == NULL && i != fields-1) return(CMSG_BAD_FORMAT);
      
    } /* reading in numbers */
  } /* for each field */
  
  if (ptr != NULL) *ptr = t;
     
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine copies a given payload item and returns
 * a pointer to the copy.
 *
 * @param from pointer to payload item to be copied
 *
 * @returns pointer to copy of payload item if successful
 * @returns NULL if argument is null or memory cannot be allocated
 */   
static payloadItem *copyPayloadItem(const payloadItem *from) {
  int i, len;
  char *s;
  payloadItem *item;
  
  if (from == NULL) return(NULL);
  
  /* create empty payload item */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(NULL);
  payloadItemInit(item);
  
  item->name = strdup(from->name);
  if (item->name == NULL) {
    free(item);
    return(NULL);
  }
  
  item->text = strdup(from->text);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(NULL);
  }
  
  item->length      = from->length;
  item->noHeaderLen = from->noHeaderLen;
  item->type        = from->type;
  item->endian      = from->endian;
  len = item->count = from->count;
  
  /* item->next is not set here. That doesn't make sense since
   * item will be in a different linked list */
  
  /* set the value */
  switch (from->type) {
    case CMSG_CP_STR:
      item->array = (void *)strdup((char *)from->array);
      if (item->array == NULL) {payloadItemFree(item); free(item); return(NULL);}
      break;
    case CMSG_CP_STR_A:
      item->array = calloc(len, sizeof(char *));
      if (item->array == NULL) {payloadItemFree(item); free(item); return(NULL);}
      /* copy all strings */
      for (i=0; i < len; i++) {
          s = strdup( ((char **)from->array)[i] );
          /* being lazy here, should free strings allocated - possible mem leak */
          if (s == NULL) {payloadItemFree(item); free(item); return(NULL);}
          ((char **)(item->array))[i] = s;
      }
      break;
      
      
    case CMSG_CP_INT8:
    case CMSG_CP_INT16:
    case CMSG_CP_INT32:
    case CMSG_CP_INT64:
    case CMSG_CP_UINT8:
    case CMSG_CP_UINT16:
    case CMSG_CP_UINT32:
    case CMSG_CP_UINT64:
      item->val = from->val;
      break;
      
      
    case CMSG_CP_FLT:
    case CMSG_CP_DBL:
      item->dval = from->dval;
      break;
      
      
    case CMSG_CP_INT8_A:
      item->array = calloc(len, sizeof(int8_t));
      if (item->array == NULL) {payloadItemFree(item); free(item); return(NULL);}
      for (i=0; i<len; i++) ((int8_t *)item->array)[i] = ((int8_t *)from->array)[i];
      break;
    case CMSG_CP_INT16_A:
      item->array = calloc(len, sizeof(int16_t));
      if (item->array == NULL) {payloadItemFree(item); free(item); return(NULL);}
      for (i=0; i<len; i++) ((int16_t *)item->array)[i] = ((int16_t *)from->array)[i];
      break;
    case CMSG_CP_INT32_A:
      item->array = calloc(len, sizeof(int32_t));
      if (item->array == NULL) {payloadItemFree(item); free(item); return(NULL);}
      for (i=0; i<len; i++) ((int32_t *)item->array)[i] = ((int32_t *)from->array)[i];
      break;
    case CMSG_CP_INT64_A:
      item->array = calloc(len, sizeof(int64_t));
      if (item->array == NULL) {payloadItemFree(item); free(item); return(NULL);}
      for (i=0; i<len; i++) ((int64_t *)item->array)[i] = ((int64_t *)from->array)[i];
      break;
    case CMSG_CP_UINT8_A:
      item->array = calloc(len, sizeof(uint8_t));
      if (item->array == NULL) {payloadItemFree(item); free(item); return(NULL);}
      for (i=0; i<len; i++) ((uint8_t *)item->array)[i] = ((uint8_t *)from->array)[i];
      break;
    case CMSG_CP_UINT16_A:
      item->array = calloc(len, sizeof(uint16_t));
      if (item->array == NULL) {payloadItemFree(item); free(item); return(NULL);}
      for (i=0; i<len; i++) ((uint16_t *)item->array)[i] = ((uint16_t *)from->array)[i];
      break;
    case CMSG_CP_UINT32_A:
      item->array = calloc(len, sizeof(uint32_t));
      if (item->array == NULL) {payloadItemFree(item); free(item); return(NULL);}
      for (i=0; i<len; i++) ((uint32_t *)item->array)[i] = ((uint32_t *)from->array)[i];
      break;
    case CMSG_CP_UINT64_A:
      item->array = calloc(len, sizeof(uint64_t));
      if (item->array == NULL) {payloadItemFree(item); free(item); return(NULL);}
      for (i=0; i<len; i++) ((uint64_t *)item->array)[i] = ((uint64_t *)from->array)[i];
      break;
    case CMSG_CP_FLT_A:
      item->array = calloc(len, sizeof(float));
      if (item->array == NULL) {payloadItemFree(item); free(item); return(NULL);}
      for (i=0; i<len; i++) ((float *)item->array)[i] = ((float *)from->array)[i];
      break;
    case CMSG_CP_DBL_A:
      item->array = calloc(len, sizeof(double));
      if (item->array == NULL) {payloadItemFree(item); free(item); return(NULL);}
      for (i=0; i<len; i++) ((double *)item->array)[i] = ((double *)from->array)[i];
      break;
      
      
    case CMSG_CP_BIN:
      if (from->array == NULL)
        break;
      item->array = malloc(from->count);
      if (item->array == NULL) {payloadItemFree(item); free(item); return(NULL);}
      item->array = memcpy(item->array, from->array, from->count);
      break;
      
      
    case CMSG_CP_MSG:
      if (from->array == NULL)
        break;
      item->array = cMsgCopyMessage(from->array);
      if (item->array == NULL) {payloadItemFree(item); free(item); return(NULL);}
      break;
      
      
    default:
      break;
  }
    
  return(item);
}


/*-------------------------------------------------------------------*/


/**
 * This routine copies the payload from one message to another.
 * The original payload of the "to" message is overwritten.
 *
 * @param vmsgFrom pointer to message to copy payload from
 * @param vmsgTo pointer to message to copy payload to
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either argument is null
 * @returns CMSG_OUT_OF_MEMORY if operating system out of memory
 */   
int cMsgCopyPayload(const void *vmsgFrom, void *vmsgTo) {
  int first=1, markerSet=0;
  payloadItem *item, *next, *copiedItem, *prevCopied=NULL, *firstCopied=NULL;
  cMsgMessage_t *msgFrom = (cMsgMessage_t *)vmsgFrom;
  cMsgMessage_t *msgTo   = (cMsgMessage_t *)vmsgTo;
  
  grabMutex();
  
  if (msgTo == NULL || msgFrom == NULL) {
    releaseMutex();
    return(CMSG_BAD_ARGUMENT);
  }
  
  if (msgFrom->payload == NULL) {
    releaseMutex();
    return(CMSG_OK);
  }
  
  /* copy linked list of payload items one-by-one */
  item = msgFrom->payload;
  while (item != NULL) {
    copiedItem = copyPayloadItem(item);
    if (copiedItem == NULL) {
      releaseMutex();
      return(CMSG_OUT_OF_MEMORY);
    }
    
    if (first) {
        firstCopied = prevCopied = copiedItem;
        first = 0;
    }
    else {
        prevCopied->next = copiedItem;
        prevCopied = copiedItem;
    }
    
    if (msgFrom->marker == item) {
      msgTo->marker = copiedItem;
      markerSet++;
    }

    item = item->next;
  }
  
  /* clear msgTo's list and replace it with the copied list */
  item = msgTo->payload;
  while (item != NULL) {
    next = item->next;
    payloadItemFree(item);
    free(item);
    item = next;
  }
  msgTo->payload = firstCopied;
  if (!markerSet) msgTo->marker = firstCopied;
  
  releaseMutex();
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/* users do not need access to this */
/*-------------------------------------------------------------------*/

/**
 * This routine returns the length of a string representation of the
 * whole compound payload and the hidden system fields (currently only
 * the "text") of the message as it gets sent over the network.
 *
 * @param vmsg pointer to message
 *
 * @returns 0 if no payload exists or vmsg is NULL
 * @returns length of string representation if payload exists
 */   
int cMsgGetPayloadTextLength(const void *vmsg) {
  int totalLen=0, msgLen=0, count=0;
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  grabMutex();
  
  if (msg == NULL || msg->payload == NULL) {
    releaseMutex();
    return(0);
  }
  
  /* find total length, first payload, then text field */
  item = msg->payload;
  while (item != NULL) {
    totalLen += strlen(item->text);
    item = item->next;
    count++;
  }
  
  if (msg->text != NULL) {
    count++;

    /* length of text item minus header line */
    msgLen = numDigits(strlen(msg->text), 0) + strlen(msg->text) + 2; /* 2 newlines */

    totalLen += strlen("cMsgText") +
                2 + /* 2 digit type */
                1 + /* 1 digit count */
                1 + /* 1 digit isSys? */
                numDigits(msgLen, 0) + /* # of digits of length of what is to follow */
                msgLen + 
                5; /* 4 spaces, 1 newline */
  }
  
  totalLen += numDigits(count, 0) + 1; /* send count & newline first */
  totalLen += 1; /* for null terminator */
  
  releaseMutex();
  
  return totalLen;
}


/*-------------------------------------------------------------------*/
/* users do not need access to this */
/*-------------------------------------------------------------------*/

/**
 * This routine creates a string representation of the whole compound
 * payload and the hidden system fields (currently only the "text")
 * of the message as it gets sent over the network.
 * If the dst argument is not NULL, this routine writes the string there.
 * If the dst argument is NULL, memory is allocated and the string placed in that.
 * In the latter case, the returned string (buf) must be freed by the user.
 *
 * @param vmsg pointer to message
 * @param buf pointer to buffer filled in with payload text if dst is NULL
 *            (user needs to free buffer if not NULL)
 * @param dst pointer to where payload text is to be written if not NULL
 *            (no memory allocated in this case)
 * @param size if dst is not NULL, size of memory in bytes available to write in
 * @param length size in bytes of written string
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload exists
 * @returns CMSG_BAD_ARGUMENT if vmsg is NULL
 * @returns CMSG_OUT_OF_MEMORY if no more memory, or dst is too small (if not NULL)
 */   
int cMsgGetPayloadText(const void *vmsg, char **buf, char *dst, size_t size, size_t *length) {
  char *s, *pBuf;
  int len, msgLen=0, count=0;
  size_t totalLen=0;
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  grabMutex();
  
  if (msg->payload == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  
  /* find total length, first payload, then text field */
  item = msg->payload;
  while (item != NULL) {
    totalLen += strlen(item->text);
    item = item->next;
    count++;
  }
  
  if (msg->text != NULL) {
    count++;
    
    /* length of text item minus header line */
    msgLen = numDigits(strlen(msg->text), 0) + strlen(msg->text) + 2; /* 2 newlines */

    totalLen += strlen("cMsgText") +
                2 + /* 2 digit type */
                1 + /* 1 digit count */
                1 + /* 1 digit isSys? */
                numDigits(msgLen, 0) + /* # of digits of length of what is to follow */
                msgLen + 
                5; /* 4 spaces, 1 newline */
  }
  
  totalLen += numDigits(count, 0) + 1; /* send count & newline first */
  totalLen += 1; /* for null terminator */
  
  if (dst == NULL) {
    pBuf = s = (char *) calloc(1, totalLen);
    if (s == NULL) {
      releaseMutex();
      return(CMSG_OUT_OF_MEMORY);
    }
  }
  else {
    /* check to see if enough space to write in */
    if (totalLen < size) return(CMSG_OUT_OF_MEMORY);
    pBuf = s = dst;
  }
  
  /* first item is number of fields to come (count) & newline */
  sprintf(s, "%d\n%n", count, &len);
  
  /* add message text if there is one */
  if (msg->text != NULL) {
    s += len;
    sprintf(s, "cMsgText %d 1 1 %d\n%d\n%s\n%n", CMSG_CP_STR, msgLen,
                                             strlen(msg->text), msg->text, &len);
  }
  
  /* add payload fields */
  item = msg->payload;
  while (item != NULL) {
    s += len;
    sprintf(s, "%s%n", item->text, &len);
    item = item->next;
  }
  
  releaseMutex();
  
  if (length != NULL) {
    *length = totalLen;
  }
  
  if (dst == NULL) {
    if (buf != NULL) *buf = pBuf;
  }
  else {
    if (buf != NULL) *buf = NULL;
  }
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a description of the current field name.
 * Do NOT write to this location in memory.
 *
 * @param vmsg pointer to message
 *
 * @returns NULL if no payload exists
 * @returns field name if field exists
 */   
const char *cMsgGetFieldDescription(const void *vmsg) {
  static char s[64];
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  int count;

  if (msg == NULL || msg->marker == NULL) return(NULL);
  
  count = msg->marker->count;
  
  switch (msg->marker->type) {
    case CMSG_CP_STR:
        sprintf(s, "string");
        break;
    case CMSG_CP_INT8:
        sprintf(s, "8 bit int");
        break;
    case CMSG_CP_INT16:
        sprintf(s, "16 bit int");
        break;
    case CMSG_CP_INT32:
        sprintf(s, "32 bit int");
        break;
    case CMSG_CP_INT64:
        sprintf(s, "64 bit int");
        break;
    case CMSG_CP_UINT8:
        sprintf(s, "8 bit unsigned int");
        break;
    case CMSG_CP_UINT16:
        sprintf(s, "16 bit unsigned int");
        break;
    case CMSG_CP_UINT32:
        sprintf(s, "32 bit unsigned int");
        break;
    case CMSG_CP_UINT64:
        sprintf(s, "64 bit unsigned int");
        break;
    case CMSG_CP_FLT:
        sprintf(s, "32 bit float");
        break;
    case CMSG_CP_DBL:
        sprintf(s, "64 bit double");
        break;
    case CMSG_CP_STR_A:
        if (count > 1) sprintf(s, "string array");
        else sprintf(s, "string");
        break;
    case CMSG_CP_INT8_A:
        if (count > 1) sprintf(s, "8 bit int array");
        else sprintf(s, "8 bit int");
        break;
    case CMSG_CP_INT16_A:
        if (count > 1) sprintf(s, "16 bit int array");
        else sprintf(s, "16 bit int");
        break;
    case CMSG_CP_INT32_A:
        if (count > 1) sprintf(s, "32 bit int array");
        else sprintf(s, "32 bit int");
        break;
    case CMSG_CP_INT64_A:
        if (count > 1) sprintf(s, "64 bit int array");
        else sprintf(s, "64 bit int");
        break;
    case CMSG_CP_UINT8_A:
        if (count > 1) sprintf(s, "8 bit unsigned int array");
        else sprintf(s, "8 bit unsigned int");
        break;
    case CMSG_CP_UINT16_A:
        if (count > 1) sprintf(s, "16 bit unsigned int array");
        else sprintf(s, "16 bit unsigned int");
        break;
    case CMSG_CP_UINT32_A:
        if (count > 1) sprintf(s, "32 bit unsigned int array");
        else sprintf(s, "32 bit unsigned int");
        break;
    case CMSG_CP_UINT64_A:
        if (count > 1) sprintf(s, "64 bit unsigned int array");
        else sprintf(s, "64 bit unsigned int");
        break;
    case CMSG_CP_FLT_A:
        if (count > 1) sprintf(s, "32 bit float array");
        else sprintf(s, "32 bit float");
        break;
    case CMSG_CP_DBL_A:
        if (count > 1) sprintf(s, "64 bit double array");
        else sprintf(s, "64 bit double");
        break;
    case CMSG_CP_MSG:
        sprintf(s, "cMsg message");
        break;
    default :
        sprintf(s, "Unknown data type");
  }
  
  return(s);
}

  
/*-------------------------------------------------------------------*/

/**
 * This routine prints out the message payload in a readable form.
 *
 * @param msg pointer to message
 */
static void cMsgPayloadPrintout2(void *msg, int level) {
  int type, ok, j, len, place;
  char *indent;
  
  
  ok = cMsgFirstField(msg);
  if (!ok) {
    printf("No payload fields in message.\n");
    return;
  }
  
  /* remember marker */
  ok = cMsgGetFieldPosition(msg, &place);
  if (ok != CMSG_OK) {
    printf("Internal error in message handling.\n");
    return;
  }
  
  /* create the indent since a message may contain a message, etc. */
  if (level < 1) {
    indent = "";
  }
  else {
    indent = (char *)malloc(level*5+1);
    for (j=0; j<level*5; j++) { /* indent by 5 spaces for each level */
      indent[j] = '\040';       /* ASCII space = char #32 (40 octal) */
    }
    indent[level*5] = '\0';
  }

  do {
    printf("%sFIELD %s", indent, cMsgGetFieldName(msg));
    cMsgGetFieldType(msg, &type);
    
    switch (type) {
      case CMSG_CP_INT8:
        {int8_t i;   ok=cMsgGetInt8(msg, &i);   if(ok==CMSG_OK) printf(" (int8): %d\n", i);}       break;
      case CMSG_CP_INT16:
        {int16_t i;  ok=cMsgGetInt16(msg, &i);  if(ok==CMSG_OK) printf(" (int16): %hd\n", i);}     break;
      case CMSG_CP_INT32:
        {int32_t i;  ok=cMsgGetInt32(msg, &i);  if(ok==CMSG_OK) printf(" (int32): %d\n", i);}      break;
      case CMSG_CP_INT64:
        {int64_t i;  ok=cMsgGetInt64(msg, &i);  if(ok==CMSG_OK) printf(" (int64): %lld\n", i);}    break;
      case CMSG_CP_UINT8:
        {uint8_t i;  ok=cMsgGetUint8(msg, &i);  if(ok==CMSG_OK) printf(" (uint8): %u\n", i);}      break;
      case CMSG_CP_UINT16:
        {uint16_t i; ok=cMsgGetUint16(msg, &i); if(ok==CMSG_OK) printf(" (uint16): %hu\n", i);}    break;
      case CMSG_CP_UINT32:
        {uint32_t i; ok=cMsgGetUint32(msg, &i); if(ok==CMSG_OK) printf(" (uint32): %u\n", i);}     break;
      case CMSG_CP_UINT64:
        {uint64_t i; ok=cMsgGetUint64(msg, &i); if(ok==CMSG_OK) printf(" (uint64): %llu\n", i);}   break;
      case CMSG_CP_DBL:
        {double d;   ok=cMsgGetDouble(msg, &d); if(ok==CMSG_OK) printf(" (double): %.16lg\n", d);} break;
      case CMSG_CP_FLT:
        {float f;    ok=cMsgGetFloat(msg, &f);  if(ok==CMSG_OK) printf(" (float): %.7g\n", f);}    break;
      case CMSG_CP_STR:
        {char *s;    ok=cMsgGetString(msg, &s); if(ok==CMSG_OK) printf(" (string): %s\n", s);}     break;
      case CMSG_CP_INT8_A:
        {const int8_t *i; ok=cMsgGetInt8Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  int8[%d] = %d\n", indent, j,i[j]);} break;
      case CMSG_CP_INT16_A:
        {const int16_t *i; ok=cMsgGetInt16Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  int16[%d] = %hd\n", indent, j,i[j]);} break;
      case CMSG_CP_INT32_A:
        {const int32_t *i; ok=cMsgGetInt32Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  int32[%d] = %d\n", indent, j,i[j]);} break;
      case CMSG_CP_INT64_A:
        {const int64_t *i; ok=cMsgGetInt64Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  int64[%d] = %lld\n", indent, j,i[j]);} break;
      case CMSG_CP_UINT8_A:
        {const uint8_t *i; ok=cMsgGetUint8Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  uint8[%d] = %u\n", indent, j,i[j]);} break;
      case CMSG_CP_UINT16_A:
        {const uint16_t *i; ok=cMsgGetUint16Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  uint16[%d] = %hu\n", indent, j,i[j]);} break;
      case CMSG_CP_UINT32_A:
        {const uint32_t *i; ok=cMsgGetUint32Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  uint8[%d] = %u\n", indent, j,i[j]);} break;
      case CMSG_CP_UINT64_A:
        {const uint64_t *i; ok=cMsgGetUint64Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  uint64[%d] = %llu\n", indent, j,i[j]);} break;
      case CMSG_CP_DBL_A:
        {const double *i; ok=cMsgGetDoubleArray(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  double[%d] = %.16lg\n", indent, j,i[j]);} break;
      case CMSG_CP_FLT_A:
        {const float *i; ok=cMsgGetFloatArray(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  float[%d] = %.7g\n", indent, j,i[j]);} break;
      case CMSG_CP_STR_A:
        {const char **i; ok=cMsgGetStringArray(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  string[%d] = %s\n", indent, j,i[j]);} break;
         
      case CMSG_CP_BIN:
        {char *b, *enc; size_t sb,sz; unsigned int se; int end;
         ok=cMsgGetBinary(msg, &b, &sz, &end); if(ok!=CMSG_OK) break;
         /* only print up to 1kB */
         sb = sz; if (sb > 1024) {sb = 1024;}
         se = cMsg_b64_encode_len(b, sb);
         enc = (char *)malloc(se+1); if (enc == NULL) break;
         enc[se] = '\0';
         cMsg_b64_encode(b, sb, enc);
         if (end == CMSG_ENDIAN_BIG) printf(" (binary, big endian):\n%s%s\n", indent, enc);
         else printf(" (binary, little endian):\n%s%s\n", indent, enc);
         if (sz > sb) {printf("%s... %u bytes more binary not printed here ...\n", indent, (sz-sb));}
         free(enc);
        } break;
        
      case CMSG_CP_MSG:
        {void *v; ok=cMsgGetMessage(msg, &v); if(ok!=CMSG_OK) break;
         printf(": (cMsg message):\n");
         cMsgPayloadPrintout2(v, level+1);
        } break;
        
      default:
        printf("\n");
    }
  } while (cMsgNextField(msg));
  
  if (level > 0) free(indent);
  
  /* restore marker */
  cMsgGoToField(msg, place);
  
  return;  
}


/*-------------------------------------------------------------------*/

/**
 * This routine prints out the message payload in a readable form.
 *
 * @param msg pointer to message
 */
void cMsgPayloadPrintout(void *msg) {
  cMsgPayloadPrintout2(msg,0);  
}


/*-------------------------------------------------------------------*/


/**
 * This routine returns a pointer to the current field name.
 * Do NOT write to this location in memory.
 *
 * @param vmsg pointer to message
 *
 * @returns NULL if no field exists
 * @returns field name if field exists
 */   
char *cMsgGetFieldName(const void *vmsg) {
  char *s;
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  grabMutex();
  if (msg == NULL || msg->marker == NULL) {
    releaseMutex();
    return(NULL);
  }
  s = msg->marker->name;
  releaseMutex();
  return(s);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a pointer to the string representation of the current field.
 * Do NOT write to this location in memory.
 *
 * @param vmsg pointer to message
 *
 * @returns NULL if no field exists
 * @returns text if field exists
 */   
char *cMsgGetFieldText(const void *vmsg) {
  char *s;
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  grabMutex();
  if (msg == NULL || msg->marker == NULL) {
    releaseMutex();
    return(NULL);
  }
  s = msg->marker->text;
  releaseMutex();
  return(s);
}

  
/*-------------------------------------------------------------------*/


/* users should not have access to this !!! */
/**
 * This routine takes a pointer to a string representation of the
 * whole compound payload, including the system (hidden) fields of the message,
 * as it gets sent over the network and converts it into the standard message
 * payload. All system information is ignored. This overwrites any existing
 * payload and skips over any fields with names starting with "cMsg"
 * (as they are reserved for system use).
 *
 * @param vmsg pointer to message
 * @param text string sent over network to be unmarshalled
 *
 * @returns NULL if no payload exists or no memory
 */   
int cMsgSetPayloadFromText(void *vmsg, const char *text) {
  return setFieldsFromText(vmsg, text, 1, NULL);
}

  
/*-------------------------------------------------------------------*/


/* users should not have access to this !!! */
/**
 * This routine takes a pointer to a string representation of the
 * whole compound payload, including the system (hidden) fields of the message,
 * as it gets sent over the network and converts it into the hidden system fields
 * of the message. All non-system information is ignored. This overwrites any existing
 * system fields.
 *
 * @param vmsg pointer to message
 * @param text string sent over network to be unmarshalled
 *
 * @returns NULL if no payload exists or no memory
 */   
int cMsgSetSystemFieldsFromText(void *vmsg, const char *text) {
  return setFieldsFromText(vmsg, text, 0, NULL);
}

  
/*-------------------------------------------------------------------*/


/* users should not have access to this !!! */
/**
 * This routine takes a pointer to a string representation of the
 * whole compound payload, including the system (hidden) fields of the message,
 * as it gets sent over the network and converts it into the hidden system fields
 * and payload of the message. This overwrites any existing system fields and payload.
 *
 * @param vmsg pointer to message
 * @param text string sent over network to be unmarshalled
 *
 * @returns NULL if no payload exists or no memory
 */   
int cMsgSetAllFieldsFromText(void *vmsg, const char *text) {
  return setFieldsFromText(vmsg, text, 2, NULL);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the user pointer of the current field.
 * Used to implement C++ interface to compound payload.
 *
 * @param vmsg pointer to message
 * @param p pointer that gets filled with user pointer
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg or marker is NULL
 */   
int cMsgGetFieldPointer(const void *vmsg, void **p) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  grabMutex();
  if (msg == NULL || msg->marker == NULL || p == NULL) {
    releaseMutex();
    return(CMSG_BAD_ARGUMENT);
  }
  *p = msg->marker->pointer;
  releaseMutex();
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine sets the user pointer of the current field.
 * Used to implement C++ interface to compound payload.
 *
 * @param vmsg pointer to message
 * @param p user pointer value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if either arg or marker is NULL
 */   
int cMsgSetFieldPointer(const void *vmsg, void *p) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  grabMutex();
  if (msg == NULL || msg->marker == NULL || p == NULL) {
    releaseMutex();
    return(CMSG_BAD_ARGUMENT);
  }
  msg->marker->pointer = p;
  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the type of the current field.
 *
 * @param vmsg pointer to message
 * @param type pointer that gets filled with type
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetFieldType(const void *vmsg, int *type) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  grabMutex();
  if (msg == NULL || type == NULL) {
    releaseMutex();
    return(CMSG_BAD_ARGUMENT);
  }
  else if (msg->marker == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  *type = msg->marker->type;
  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the count (array size) of the current field.
 *
 * @param vmsg pointer to message
 * @param type pointer that gets filled with count
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetFieldCount(const void *vmsg, int *count) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  grabMutex();
  if (msg == NULL || count == NULL) {
    releaseMutex();
    return(CMSG_BAD_ARGUMENT);
  }
  else if (msg->marker == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  *count = msg->marker->count;
  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the value of the current field as a cMsg message if its exists.
 * Do NOT write into the returned pointer's memory location.
 *
 * @param vmsg pointer to message
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type (cMsg message)
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetMessage(const void *vmsg, void **val) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  item = msg->marker;
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  else if (item->type != CMSG_CP_MSG || item->count < 1) {
    releaseMutex();
    return(CMSG_BAD_FORMAT);
  }
  if (item->array == NULL)
    printf("cMsgGetMessage: item->array == NULL !!!\n");
  
  *val = (void *)item->array;
  
  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the value of the current field as a binary array if its exists.
 * Do NOT write into the returned pointer's memory location.
 *
 * @param vmsg pointer to message
 * @param val pointer filled with field value
 * @param len pointer filled with number of bytes in binary array
 * @param endian pointer filled with endian of data (CMSG_ENDIAN_BIG/LITTLE)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type (binary of >0 size)
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetBinary(const void *vmsg, char **val, size_t *len, int *endian) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || val == NULL || len == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  item = msg->marker;
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  else if (item->type != CMSG_CP_BIN || item->count < 1) {
    releaseMutex();
    return(CMSG_BAD_FORMAT);
  }
  
  *val = (char *)item->array;
  *len = item->count;
  if (endian != NULL) *endian = item->endian;
  
  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a real value of the current field if its exists.
 *
 * @param vmsg pointer to message
 * @param type type of real to get (CMSG_CP_DBL or CMSG_CP_FLT)
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if vmsg or val is NULL
 */   
static int getReal(const void *vmsg, int type, double *val) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);

  grabMutex();
  
  item = msg->marker;
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  else if (item->type != type || item->count > 1) {
    releaseMutex();
    return(CMSG_BAD_FORMAT);
  }
  
  *val = item->dval;

  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the float current field if its exists.
 *
 * @param vmsg pointer to message
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type (float)
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetFloat(const void *vmsg, float *val) {
  int err;
  double dbl;
  
  err = getReal(vmsg, CMSG_CP_FLT, &dbl);
  if (err != CMSG_OK) return(err);
  *val = (float) dbl;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the double current field if its exists.
 *
 * @param vmsg pointer to message
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type (double)
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetDouble(const void *vmsg, double *val) {
  return getReal(vmsg, CMSG_CP_DBL, val);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns an array of the current field if its exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param type type of array to get (CMSG_CP_DBL_A, CMSG_CP_INT32_A, etc.)
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if any pointer arg is NULL
 */   
static int getArray(const void *vmsg, int type, const void **vals, size_t *len) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || vals == NULL || len == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  item = msg->marker;
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  else if (item->type != type || item->count < 1 || item->array == NULL) {
    releaseMutex();
    return(CMSG_BAD_FORMAT);
  }
  
  *vals = item->array;
  *len  = item->count;
  
  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the float array of the current field if its exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetFloatArray(const void *vmsg, const float **vals, size_t *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, CMSG_CP_FLT_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (float *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the double array of the current field if its exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetDoubleArray(const void *vmsg, const double **vals, size_t *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, CMSG_CP_DBL_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (double *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns an integer value of the current field if its exists.
 *
 * @param vmsg pointer to message
 * @param type type of integer to get (e.g. CMSG_CP_INT32)
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if vmsg or val is NULL
 */   
static int getInt(const void *vmsg, int type, int64_t *val) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);

  grabMutex();
  
  item = msg->marker;
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  else if (item->type != type || item->count > 1) {
    releaseMutex();
    return(CMSG_BAD_FORMAT);
  }
  
  *val = item->val;
  
  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 8 bit, signed integer current field if its exists.
 *
 * @param vmsg pointer to message
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetInt8(const void *vmsg, int8_t *val) {
  int err;
  int64_t int64;
  
  err = getInt(vmsg, CMSG_CP_INT8, &int64);
  if (err != CMSG_OK) return(err);
  *val = (int8_t) int64;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 16 bit, signed integer current field if its exists.
 *
 * @param vmsg pointer to message
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetInt16(const void *vmsg, int16_t *val) {
  int err;
  int64_t int64;
  
  err = getInt(vmsg, CMSG_CP_INT16, &int64);
  if (err != CMSG_OK) return(err);
  *val = (int16_t) int64;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 32 bit, signed integer current field if its exists.
 *
 * @param vmsg pointer to message
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetInt32(const void *vmsg, int32_t *val) {
  int err;
  int64_t int64;
  
  err = getInt(vmsg, CMSG_CP_INT32, &int64);
  if (err != CMSG_OK) return(err);
  *val = (int32_t) int64;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 64 bit, signed integer current field if its exists.
 *
 * @param vmsg pointer to message
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetInt64(const void *vmsg, int64_t *val) {  
  return getInt(vmsg, CMSG_CP_INT64, val);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 8 bit, unsigned integer current field if its exists.
 *
 * @param vmsg pointer to message
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetUint8(const void *vmsg, uint8_t *val) {
  int err;
  int64_t int64;
  
  err = getInt(vmsg, CMSG_CP_UINT8, &int64);
  if (err != CMSG_OK) return(err);
  *val = (uint8_t) int64;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 16 bit, unsigned integer current field if its exists.
 *
 * @param vmsg pointer to message
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetUint16(const void *vmsg, uint16_t *val) {
  int err;
  int64_t int64;
  
  err = getInt(vmsg, CMSG_CP_UINT16, &int64);
  if (err != CMSG_OK) return(err);
  *val = (uint16_t) int64;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 32 bit, unsigned integer current field if its exists.
 *
 * @param vmsg pointer to message
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetUint32(const void *vmsg, uint32_t *val) {
  int err;
  int64_t int64;
  
  err = getInt(vmsg, CMSG_CP_UINT32, &int64);
  if (err != CMSG_OK) return(err);
  *val = (uint32_t) int64;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 64 bit, unsigned integer current field if its exists.
 *
 * @param vmsg pointer to message
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetUint64(const void *vmsg, uint64_t *val) {
  int err;
  int64_t int64;
  
  err = getInt(vmsg, CMSG_CP_UINT64, &int64);
  if (err != CMSG_OK) return(err);
  *val = (uint64_t) int64;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 8 bit, signed integer array of the current field if its exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetInt8Array(const void *vmsg, const int8_t **vals, size_t *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, CMSG_CP_INT8_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (int8_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 16 bit, signed integer array of the current field if its exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetInt16Array(const void *vmsg, const int16_t **vals, size_t *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, CMSG_CP_INT16_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (int16_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 32 bit, signed integer array of the current field if its exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetInt32Array(const void *vmsg, const int32_t **vals, size_t *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, CMSG_CP_INT32_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (int32_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 64 bit, signed integer array of the current field if its exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetInt64Array(const void *vmsg, const int64_t **vals, size_t *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, CMSG_CP_INT64_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (int64_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 8 bit, unsigned integer array of the current field if its exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetUint8Array(const void *vmsg, const uint8_t **vals, size_t *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, CMSG_CP_UINT8_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (uint8_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 16 bit, unsigned integer array of the current field if its exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetUint16Array(const void *vmsg, const uint16_t **vals, size_t *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, CMSG_CP_UINT16_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (uint16_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 32 bit, unsigned integer array of the current field if its exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetUint32Array(const void *vmsg, const uint32_t **vals, size_t *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, CMSG_CP_UINT32_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (uint32_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the 64 bit, unsigned integer array of the current field if its exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type
 * @returns CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetUint64Array(const void *vmsg, const uint64_t **vals, size_t *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, CMSG_CP_UINT64_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (uint64_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the value of the current field as a string if its exists.
 * Do NOT write into the returned pointer's memory location.
 *
 * @param vmsg pointer to message
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type (single string)
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetString(const void *vmsg, char **val) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  item = msg->marker;
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  else if (item->type != CMSG_CP_STR || item->count > 1) {
    releaseMutex();
    return(CMSG_BAD_FORMAT);
  }
  
  *val = (char *)item->array;
  
  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the string array of the current field if its exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param array pointer to array of pointers which gets filled with string array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no payload
 * @returns CMSG_BAD_FORMAT field is not right type (string array)
 * @returns CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgGetStringArray(const void *vmsg, const char ***array, size_t *len) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || array == NULL || len == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  item = msg->marker;
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  else if (item->type != CMSG_CP_STR_A || item->count < 1 || item->array == NULL) {
    releaseMutex();
    return(CMSG_BAD_FORMAT);
  }
  
  *array = (const char **) item->array;
  *len = item->count;
  
  releaseMutex();
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named field of binary data to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param src pointer to binary data to add
 * @param size size in bytes of data to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 * @param isSystem if = 0, is not a system field, else is (name starts with "cmsg")
 * @param endian endian value of binary data, may be CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *               CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if string cannot be placed at the desired location,
 *                     or cannot find local endian
 * @returns CMSG_BAD_ARGUMENT if message, src or name is NULL, size < 1, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER, or
 *                            endian != CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *                            CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed,
 *                          or if error in binary-to-text transformation
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
static int addBinary(void *vmsg, const char *name, const char *src, size_t size,
                     int place, int isSystem, int endian) {
  payloadItem *item;
  int ok, len, textLen, totalLen;
  unsigned int binLen, numChars;
  char *s;
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || src == NULL) return(CMSG_BAD_ARGUMENT);
  if (place < 0 &&
      place != CMSG_CP_END &&
      place != CMSG_CP_MARKER)                    return(CMSG_BAD_ARGUMENT);
  if (size < 1)                                   return(CMSG_BAD_ARGUMENT);
  if (!goodFieldName(name, isSystem))             return(CMSG_BAD_FORMAT);
  if (nameExists(vmsg, name))                     return(CMSG_ALREADY_EXISTS);
  if (isSystem) isSystem = 1; /* force it to be = 1, need to it be 1 digit */
  if ((endian != CMSG_ENDIAN_BIG)   && (endian != CMSG_ENDIAN_LITTLE)   &&
      (endian != CMSG_ENDIAN_LOCAL) && (endian != CMSG_ENDIAN_NOTLOCAL))  {
      return(CMSG_BAD_ARGUMENT);
  }
  else {
    int ndian;
    if (endian == CMSG_ENDIAN_LOCAL) {
        if (cMsgLocalByteOrder(&ndian) != CMSG_OK) {
          return CMSG_ERROR;
        }
        if (ndian == CMSG_ENDIAN_BIG) {
            endian = CMSG_ENDIAN_BIG;
        }
        else {
            endian = CMSG_ENDIAN_LITTLE;
        }
    }
    /* set to opposite of local endian value */
    else if (endian == CMSG_ENDIAN_NOTLOCAL) {
        if (cMsgLocalByteOrder(&ndian) != CMSG_OK) {
            return CMSG_ERROR;
        }
        if (ndian == CMSG_ENDIAN_BIG) {
            endian = CMSG_ENDIAN_LITTLE;
        }
        else {
            endian = CMSG_ENDIAN_BIG;
        }
    }
  }
  
  /* payload item */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);
  
  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type   = CMSG_CP_BIN;
  item->count  = size;
  item->endian = endian;
  
  /* store original data */
  item->array = (void *) malloc(size);
  if (item->array == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  memcpy(item->array, src, size);
    
  /* Create a string to hold all data to be transferred over the network.
   * That means converting binary to text */
   
  /* first find size of text-encoded binary data */
  binLen = cMsg_b64_encode_len(src, size);
 
  /* length of string to contain all text representation except first line */
  textLen = numDigits(binLen, 0) + 1 /*endian*/ +
            binLen + 
            3; /* 1 space, 2 newlines */
  item->noHeaderLen = textLen;
            
  /* length of first line of text representation + textLen + null */
  totalLen = strlen(name) +
             2 + /* 2 digit type */
             numDigits(item->count, 0) +
             1 + /* 1 digit isSystem */
             numDigits(textLen, 0) + 
             6 + /* 4 spaces, 1 newline, 1 null term */
             textLen;
                
/*printf("addBinary: encoded bin len = %u, allocate = %d\n", binLen, totalLen);*/
  s = item->text = (char *) malloc(totalLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->text[totalLen-1] = '\0';
  
  /* write first line & length */
  sprintf(s, "%s %d %d %d %d\n%u %d\n%n", name, item->type, item->count,
                                       isSystem, textLen, binLen, endian, &len);
  s+= len;
  
  /* write the binary-encoded text */
  numChars = cMsg_b64_encode(src, size, s);
  s+= numChars;
  if (binLen != numChars) {
    payloadItemFree(item);
    free(item);
    return(CMSG_BAD_FORMAT);  
  }
/*printf("addBinary: actually add bytes = %u\n", numChars);*/
  
  /* put newline at end of everything */
  sprintf(s++, "\n");
    
  item->length = strlen(item->text);
/*printf("addBinary: total string len = %d\n", item->length);*/
  /* place payload item in msg's linked list */
  ok = insertItem(msg, item, place);
  if (!ok) {
    payloadItemFree(item);
    free(item);
    return(CMSG_ERROR);  
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named field of binary data, from a string encoding of that
 * data, to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val binary-encoded string field to add
 * @param count size in bytes of original binary data
 * @param length size in chars of encoded binary
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 * @param endian endian value of binary data, may be CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *               CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if string cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, val or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER, or
 *                            endian != CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *                            CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed, or binary-encoded text is not base64 format
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
static int addBinaryFromString(void *vmsg, const char *name, const char *val, int count,
                               int length, int place, int isSystem, int endian) {
  payloadItem *item;
  char *s;
  int ok, len, textLen, totalLen, numBytes, debug=0;
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);
  if (place < 0 &&
      place != CMSG_CP_END &&
      place != CMSG_CP_MARKER)                    return(CMSG_BAD_ARGUMENT);
  if (!goodFieldName(name, isSystem))             return(CMSG_BAD_FORMAT);
  if (nameExists(vmsg, name))                     return(CMSG_ALREADY_EXISTS);
  if (isSystem) isSystem = 1; /* force it to be = 1, need to it be 1 digit */
  if ((endian != CMSG_ENDIAN_BIG)   && (endian != CMSG_ENDIAN_LITTLE)   &&
      (endian != CMSG_ENDIAN_LOCAL) && (endian != CMSG_ENDIAN_NOTLOCAL))  {
      return(CMSG_BAD_ARGUMENT);
  }
  else {
    int ndian;
    if (endian == CMSG_ENDIAN_LOCAL) {
        if (cMsgLocalByteOrder(&ndian) != CMSG_OK) {
          return CMSG_ERROR;
        }
        if (ndian == CMSG_ENDIAN_BIG) {
            endian = CMSG_ENDIAN_BIG;
        }
        else {
            endian = CMSG_ENDIAN_LITTLE;
        }
    }
    /* set to opposite of local endian value */
    else if (endian == CMSG_ENDIAN_NOTLOCAL) {
        if (cMsgLocalByteOrder(&ndian) != CMSG_OK) {
            return CMSG_ERROR;
        }
        if (ndian == CMSG_ENDIAN_BIG) {
            endian = CMSG_ENDIAN_LITTLE;
        }
        else {
            endian = CMSG_ENDIAN_BIG;
        }
    }
  }
  
  
  if (isSystem && strcmp(name, "cMsgBinary") == 0) {
    /* Place binary data into message's binary array - not in the payload.
       First remove any existing array (only free byte array if it was copied
       into the msg). */
    if ((msg->byteArray != NULL) && ((msg->bits & CMSG_BYTE_ARRAY_IS_COPIED) > 0)) {
      free(msg->byteArray);
    }
    msg->byteArrayOffset = 0;
    msg->byteArrayLength = count;
    msg->byteArray = (char *) malloc(count);
    if (msg->byteArray == NULL) {
      return (CMSG_OUT_OF_MEMORY);
    }
    
    numBytes = cMsg_b64_decode(val, length, msg->byteArray);
    if (numBytes < 0 || numBytes != count) {
if (debug) printf("addBinaryFromString: decoded string len = %d, should be %d\n", numBytes, count);
      return(CMSG_BAD_FORMAT);
    }
    msg->bits |= CMSG_BYTE_ARRAY_IS_COPIED; /* byte array IS copied */
    
    /* set to big endian */
    if (endian == CMSG_ENDIAN_BIG) {
        msg->info |= CMSG_IS_BIG_ENDIAN;
    }
    /* set to little endian */
    else if (endian == CMSG_ENDIAN_LITTLE) {
        msg->info &= ~CMSG_IS_BIG_ENDIAN;
    }
    return(CMSG_OK);
  }
  
  
  /* payload item */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);

  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }

  item->type   = CMSG_CP_BIN;
  item->count  = count;
  item->endian = endian;

  /* create space for binary array */
if (debug) printf("addBinaryFromString: will reserve %d bytes, calculation shows %d bytes\n", count,
        cMsg_b64_decode_len(val, length));
  item->array = (void *)malloc(count);
  if (item->array == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }

  /* decode text into binary */
  numBytes = cMsg_b64_decode(val, length, (char *)item->array);
  if (numBytes < 0) {
    payloadItemFree(item);
    free(item);
    return(CMSG_BAD_FORMAT);
  }
  else if (numBytes != count) {
if (debug) printf("addBinaryFromString: decoded string len = %d, should be %d\n", numBytes, count);
    payloadItemFree(item);
    free(item);
    return(CMSG_BAD_FORMAT);
  }
  
  /* Create string to hold all data to be transferred over
   * the network for this item. */
     
  textLen = numDigits(length, 0) + 1 + 
            length + 
            3; /* 1 space, 2 newlines */
  item->noHeaderLen = textLen;
            
  /* length of first line of text representation + textLen */
  totalLen = strlen(name) +
             2 + /* 2 digit type */
             numDigits(item->count, 0) +
             1 + /* 1 digit isSystem */
             numDigits(textLen, 0) + 
             6 + /* 4 spaces, 1 newline, 1 null term */
             textLen;
            
  s = item->text = (char *) malloc(totalLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  s[totalLen-1] = '\0';
  
  sprintf(s, "%s %d %d %d %d\n%d %d\n%n", name, item->type, item->count,
                                         isSystem, textLen, length, endian, &len);
  s += len;
  memcpy(s, val, length);
  s += length;
  sprintf(s, "\n");
  
  item->length = strlen(item->text);

  /* place payload item in msg's linked list */
  ok = insertItem(msg, item, place);
  if (!ok) {
    payloadItemFree(item);
    free(item);
    return(CMSG_ERROR);  
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named field of binary data to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param src pointer to binary data to add
 * @param size size in bytes of data to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 * @param endian endian value of binary data, may be CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *               CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if string cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, src or name is NULL, size < 1, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER, or
 *                            endian != CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *                            CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddBinary(void *vmsg, const char *name, const char *src,
                  size_t size, int place, int endian) {
  return addBinary(vmsg, name, src, size, place, 0, endian);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds any kind of real number field to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val float or double to add
 * @param type type of real number to add (CMSG_CP_FLT or CMSG_CP_DBL)
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if int cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER, or
 *                            wrong type being added
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
static int addReal(void *vmsg, const char *name, double val, int type, int place, int isSystem) {
  payloadItem *item;
  int ok, textLen, totalLen, numLen;
  char num[24];
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL)    return(CMSG_BAD_ARGUMENT);
  if (place < 0 &&
      place != CMSG_CP_END &&
      place != CMSG_CP_MARKER)        return(CMSG_BAD_ARGUMENT);
  if (!goodFieldName(name, isSystem)) return(CMSG_BAD_FORMAT);
  if (nameExists(vmsg, name))         return(CMSG_ALREADY_EXISTS);
  if (type != CMSG_CP_FLT  &&
      type != CMSG_CP_DBL)            return(CMSG_BAD_ARGUMENT);
  if (isSystem) isSystem = 1; /* force it to be = 1, need to it be 1 digit */
  
  /* payload item */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);
  
  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type  = type;
  item->count = 1;
  
  /* convert float/double to string */
  memset((void *)num, 0, 23);
  if (type == CMSG_CP_FLT) {
    sprintf(num,"%.7g",(float)val);
    numLen = strlen(num);
  }
  else {
    sprintf(num,"%.16lg", val);
    numLen = strlen(num);
  }
  /* Create string to hold all data to be transferred over
   * the network for this item. */
   
  /* length of string to contain all data */
  textLen = numLen + 2; /* 1 newline, 1 null terminator */
  item->noHeaderLen = textLen;
  
  totalLen = strlen(name) +
             2 + /* 2 digit type */
             numDigits(item->count, 0) +
             1 + /* isSystem */
             numDigits(textLen, 0) +
             5+ /* 4 spaces, 1 newline */
             textLen;
  
  item->text = (char *) calloc(1, totalLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  sprintf(item->text, "%s %d %d %d %d\n%s\n", name, item->type, item->count, isSystem, textLen, num);
  
  item->length = strlen(item->text);
  item->dval   = val;

  ok = insertItem(msg, item, place);
  if (!ok) {
    payloadItemFree(item);
    free(item);
    return(CMSG_ERROR);  
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, float field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val float to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if int cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddFloat(void *vmsg, const char *name, float val, int place) {
  return addReal(vmsg, name, val, CMSG_CP_FLT, place, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, double field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val double to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if int cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddDouble(void *vmsg, const char *name, double val, int place) {
  return addReal(vmsg, name, val, CMSG_CP_DBL, place, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a float or double array field to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of floats or doubles to add
 * @param type type of array to add (CMSG_CP_FLT_A, or CMSG_CP_DBL_A)
 * @param len number of values from array to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if array cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
static int addRealArray(void *vmsg, const char *name, const double *vals,
                        int type, int len, int place, int isSystem) {
  payloadItem *item;
  int i, ok, cLen, totalLen, valLen=0, textLen=0;
  void *array;
  char *s, numbers[len][24];
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || vals == NULL) return(CMSG_BAD_ARGUMENT);
  if (place < 0 &&
      place != CMSG_CP_END &&
      place != CMSG_CP_MARKER)                     return(CMSG_BAD_ARGUMENT);
  if (!goodFieldName(name, isSystem))              return(CMSG_BAD_FORMAT);
  if (nameExists(vmsg, name))                      return(CMSG_ALREADY_EXISTS);
  if (type != CMSG_CP_FLT_A &&
      type != CMSG_CP_DBL_A)                       return(CMSG_BAD_ARGUMENT);
  if (isSystem) isSystem = 1; /* force it to be = 1, need to it be 1 digit */
  
  /* payload item */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);
  
  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type  = type;
  item->count = len;
  
  /* Create string to hold all data to be transferred over
   * the network for this item. */
         
  /* convert floats or doubles to strings */
  memset((void *)numbers, 0, len*23);
  for (i=0; i<len; i++) {
    if (type == CMSG_CP_DBL_A) {
      sprintf(numbers[i],"%.16lg", vals[i]);
    }
    else {
      sprintf(numbers[i],"%.7g", ((float *)vals)[i]);
    }
    valLen += strlen(numbers[i]);
  }
   
  /* length of string to contain all data */
  textLen = valLen + len + 1; /* len-1 spaces, 1 newline, 1 null terminator */
  item->noHeaderLen = textLen;
  
  totalLen = strlen(name) +
             2 + /* 2 digit type */
             numDigits(item->count, 0) +
             1 + /* isSystem */
             numDigits(textLen, 0) +
             5 + /* 4 spaces, 1 newline */
             textLen;

  s = item->text = (char *) calloc(1, totalLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  sprintf(s, "%s %d %d %d %d\n%n", name, item->type, item->count, isSystem, textLen, &cLen);
  s += cLen;
  
  /* write numbers into text string */
  for (i=0; i<len; i++) {
    if (i < len-1) {
      sprintf(s, "%s %n", numbers[i], &cLen);
    } else {
      sprintf(s, "%s\n%n", numbers[i], &cLen);
    }
    s += cLen;
  }
  item->length = strlen(item->text);
  
  /* store number values */
  if (type == CMSG_CP_FLT_A) {
    array = calloc(len, sizeof(float));
    if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
    for (i=0; i<len; i++) ((float *)array)[i] = ((float *)vals)[i];
  }
  else {         
    array = calloc(len, sizeof(double));
    if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
    for (i=0; i<len; i++) ((double *)array)[i] = ((double *)vals)[i];
  }
   
  item->array = (void *)array;
  
  ok = insertItem(msg, item, place);
  if (!ok) {
    payloadItemFree(item);
    free(item);
    return(CMSG_ERROR);  
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, float array field to the compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of floats to add
 * @param len number of floats from array to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if array cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddFloatArray(void *vmsg, const char *name, const float vals[], int len, int place) {
   return addRealArray(vmsg, name, (double *)vals, CMSG_CP_FLT_A, len, place, 0); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, double array field to the compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of doubles to add
 * @param len number of doubles from array to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if array cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddDoubleArray(void *vmsg, const char *name, const double vals[], int len, int place) {
   return addRealArray(vmsg, name, vals, CMSG_CP_DBL_A, len, place, 0); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds any kind of int field to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val int to add
 * @param type type of int to add (CMSG_CP_INT32, etc)
 * @param position position of field in payload item order (0 = first), 
 *                 CMSG_CP_END if placed at the end, or
 *                 CMSG_CP_MARKER if placed after marker
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if int cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER, or
 *                            wrong type being added
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
static int addInt(void *vmsg, const char *name, int64_t val, int type, int position, int isSystem) {
  payloadItem *item;
  int ok, totalLen, textLen=0;
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL)    return(CMSG_BAD_ARGUMENT);
  if (position < 0 &&
      position != CMSG_CP_END &&
      position != CMSG_CP_MARKER)     return(CMSG_BAD_ARGUMENT);
  if (!goodFieldName(name, isSystem)) return(CMSG_BAD_FORMAT);
  if (nameExists(vmsg, name))         return(CMSG_ALREADY_EXISTS);
  if (isSystem) isSystem = 1;

  if (type != CMSG_CP_INT8   &&
      type != CMSG_CP_INT16  &&
      type != CMSG_CP_INT32  &&
      type != CMSG_CP_INT64  &&
      type != CMSG_CP_UINT8  &&
      type != CMSG_CP_UINT16 &&
      type != CMSG_CP_UINT32 &&
      type != CMSG_CP_UINT64)  {
      return(CMSG_BAD_ARGUMENT);
  }
  
  /* payload item */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);
  
  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type  = type;
  item->count = 1;
    
  /* Create string to hold all data to be transferred over
   * the network for this item. */
   
  textLen = 2; /* 1 newline, 1 null terminator */
  if (type == CMSG_CP_UINT64) {
    textLen += numDigits(val, 1);
  }
  else {
    textLen += numDigits(val, 0);
  }
  item->noHeaderLen = textLen;
  
  totalLen = strlen(name) +
             2 + /* 2 digit type */
             numDigits(item->count, 0) +
             1 + /* isSystem */
             numDigits(textLen, 0) +
             5 + /* 4 spaces, 1 newline */
             textLen;

  item->text = (char *) calloc(1, totalLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  if (type == CMSG_CP_UINT64) {
    sprintf(item->text, "%s %d %d %d %d\n%llu\n", name, item->type, item->count,
                                                  isSystem, textLen, (uint64_t)val);
  }
  else {
    sprintf(item->text, "%s %d %d %d %d\n%lld\n", name, item->type, item->count,
                                                  isSystem, textLen, val);
  }

  item->length = strlen(item->text);
  item->val    = val;

  ok = insertItem(msg, item, position);
  if (!ok) {
    payloadItemFree(item);
    free(item);
    return(CMSG_ERROR);  
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 8-bit, signed int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val value of 8-bit, signed int to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if int cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt8(void *vmsg, const char *name, int8_t val, int place) {
  return addInt(vmsg, name, val, CMSG_CP_INT8, place, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 16-bit, signed int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val value of 16-bit, signed int to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if int cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt16(void *vmsg, const char *name, int16_t val, int place) {
  return addInt(vmsg, name, val, CMSG_CP_INT16, place, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 32-bit, signed int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val value of 32-bit, signed int to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if int cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt32(void *vmsg, const char *name, int32_t val, int place) {
  return addInt(vmsg, name, val, CMSG_CP_INT32, place, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 64-bit, signed int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val value of 64-bit, signed int to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if int cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt64(void *vmsg, const char *name, int64_t val, int place) {
  return addInt(vmsg, name, val, CMSG_CP_INT64, place, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 8-bit, unsigned int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val value of 8-bit, unsigned int to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if int cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint8(void *vmsg, const char *name, uint8_t val, int place) {
  return addInt(vmsg, name, val, CMSG_CP_UINT8, place, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 16-bit, unsigned int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val value of 16-bit, unsigned int to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if int cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint16(void *vmsg, const char *name, uint16_t val, int place) {
  return addInt(vmsg, name, val, CMSG_CP_UINT16, place, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 32-bit, unsigned int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val value of 32-bit, unsigned int to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if int cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint32(void *vmsg, const char *name, uint32_t val, int place) {
  return addInt(vmsg, name, val, CMSG_CP_UINT32, place, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 64-bit, unsigned int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val value of 64-bit, unsigned int to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if int cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint64(void *vmsg, const char *name, uint64_t val, int place) {
  return addInt(vmsg, name, (int64_t)val, CMSG_CP_UINT64, place, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds any kind of int array field to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of ints to add
 * @param type type of int array to add (CMSG_CP_INT32_A, etc)
 * @param len number of ints from array to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if array cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, vals or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
static int addIntArray(void *vmsg, const char *name, const int *vals,
                       int type, int len, int place, int isSystem) {
  payloadItem *item;
  int i, ok, cLen, totalLen, valLen=0, textLen=0;
  void *array=NULL;
  char *s;
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || vals == NULL) return(CMSG_BAD_ARGUMENT);
  if (place < 0 &&
      place != CMSG_CP_END &&
      place != CMSG_CP_MARKER)                     return(CMSG_BAD_ARGUMENT);
  if (!goodFieldName(name, isSystem))              return(CMSG_BAD_FORMAT);
  if (nameExists(vmsg, name))                      return(CMSG_ALREADY_EXISTS);
  
  if (type != CMSG_CP_INT8_A   && type != CMSG_CP_INT16_A  &&
      type != CMSG_CP_INT32_A  && type != CMSG_CP_INT64_A  &&
      type != CMSG_CP_UINT8_A  && type != CMSG_CP_UINT16_A &&
      type != CMSG_CP_UINT32_A && type != CMSG_CP_UINT64_A)  {
      return(CMSG_BAD_ARGUMENT);
  }
  if (isSystem) isSystem = 1;
  
  /* payload item */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);
  
  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type  = type;
  item->count = len;
  
  /* Create string to hold all data to be transferred over
   * the network for this item. */
         
  switch (type) {
      case CMSG_CP_INT8_A:
            {int8_t *p = (int8_t *)vals; for (i=0; i<len; i++) valLen += numDigits(p[i], 0);}
            break;
      case CMSG_CP_INT16_A:
            {int16_t *p = (int16_t *)vals; for (i=0; i<len; i++) valLen += numDigits(p[i], 0);}
            break;
      case CMSG_CP_INT32_A:
            {int32_t *p = (int32_t *)vals; for (i=0; i<len; i++) valLen += numDigits(p[i], 0);}
            break;
      case CMSG_CP_INT64_A:
            {int64_t *p = (int64_t *)vals; for (i=0; i<len; i++) valLen += numDigits(p[i], 0);}
            break;
      case CMSG_CP_UINT8_A:
            {uint8_t *p = (uint8_t *)vals; for (i=0; i<len; i++) valLen += numDigits(p[i], 0);}
            break;
      case CMSG_CP_UINT16_A:
            {uint16_t *p = (uint16_t *)vals; for (i=0; i<len; i++) valLen += numDigits(p[i], 0);}
            break;
      case CMSG_CP_UINT32_A:
            {uint32_t *p = (uint32_t *)vals; for (i=0; i<len; i++) valLen += numDigits(p[i], 0);}
            break;
      case CMSG_CP_UINT64_A:
            {uint64_t *p = (uint64_t *)vals; for (i=0; i<len; i++) valLen += numDigits(p[i], 1);}
  }
   
  textLen  = valLen + len + 1; /* len-1 spaces, 1 newline, 1 null terminator */
  item->noHeaderLen = textLen;
  
  totalLen = strlen(name) +
             2 + /* 2 digit type */
             numDigits(item->count, 0) +
             1 + /* isSystem */
             numDigits(textLen, 0) +
             5 + /* 4 spaces, 1 newline */
             textLen;

  s = item->text = (char *) calloc(1, totalLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  sprintf(s, "%s %d %d %d %d\n%n", name, item->type, item->count, isSystem, textLen, &cLen);
  s += cLen;
      
  switch (type) {
      case CMSG_CP_INT8_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(s, "%d %n", ((int8_t *)vals)[i], &cLen);
              } else {
                sprintf(s, "%d\n%n", ((int8_t *)vals)[i], &cLen);
              }
              s += cLen;
            }
            /* Store the values */
            array = calloc(len, sizeof(int8_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++) ((int8_t *)array)[i] = ((int8_t *)vals)[i];
            break;
           
      case CMSG_CP_INT16_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(s, "%hd %n", ((int16_t *)vals)[i], &cLen);
              } else {
                sprintf(s, "%hd\n%n", ((int16_t *)vals)[i], &cLen);
              }
              s += cLen;
            }
            /* Store the values */
            array = calloc(len, sizeof(int16_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++) ((int16_t *)array)[i] = ((int16_t *)vals)[i];
            break;
            
      case CMSG_CP_INT32_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(s, "%d %n", ((int32_t *)vals)[i], &cLen);
              } else {
                sprintf(s, "%d\n%n", ((int32_t *)vals)[i], &cLen);
              }
              s += cLen;
            }
            /* Store the values */
            array = calloc(len, sizeof(int32_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++) ((int32_t *)array)[i] = ((int32_t *)vals)[i];
            break;
            
      case CMSG_CP_INT64_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(s, "%lld %n", ((int64_t *)vals)[i], &cLen);
              } else {
                sprintf(s, "%lld\n%n", ((int64_t *)vals)[i], &cLen);
              }
              s += cLen;
            }
            /* Store the values */
            array = calloc(len, sizeof(int64_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++) ((int64_t *)array)[i] = ((int64_t *)vals)[i];
            break;
            
      case CMSG_CP_UINT8_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(s, "%d %n", ((uint8_t *)vals)[i], &cLen);
              } else {
                sprintf(s, "%d\n%n", ((uint8_t *)vals)[i], &cLen);
              }
              s += cLen;
            }
            /* Store the values */
            array = calloc(len, sizeof(uint8_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++) ((uint8_t *)array)[i] = ((uint8_t *)vals)[i];
            break;
            
      case CMSG_CP_UINT16_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(s, "%hu %n", ((uint16_t *)vals)[i], &cLen);
              } else {
                sprintf(s, "%hu\n%n", ((uint16_t *)vals)[i], &cLen);
              }
              s += cLen;
            }
            /* Store the values */
            array = calloc(len, sizeof(uint16_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++)((uint16_t *)array)[i] = ((uint16_t *)vals)[i];
            break;
            
      case CMSG_CP_UINT32_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(s, "%u %n", ((uint32_t *)vals)[i], &cLen);
              } else {
                sprintf(s, "%u\n%n", ((uint32_t *)vals)[i], &cLen);
              }
              s += cLen;
            }
            /* Store the values */
            array = calloc(len, sizeof(uint32_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++) ((uint32_t *)array)[i] = ((uint32_t *)vals)[i];
            break;
            
      case CMSG_CP_UINT64_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(s, "%llu %n", ((uint64_t *)vals)[i], &cLen);
              } else {
                sprintf(s, "%llu\n%n", ((uint64_t *)vals)[i], &cLen);
              }
              s += cLen;
            }
            /* Store the values */
            array = calloc(len, sizeof(uint64_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++) ((uint64_t *)array)[i] = ((uint64_t *)vals)[i];
  }
   
  item->array = (void *)array;
  item->length = strlen(item->text);
  
  ok = insertItem(msg, item, place);
  if (!ok) {
    payloadItemFree(item);
    free(item);
    return(CMSG_ERROR);  
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 8-bit, signed int array field to the compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 8-bit, signed ints to add
 * @param len number of ints from array to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if array cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, vals or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt8Array(void *vmsg, const char *name, const int8_t vals[], int len, int place) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_INT8_A, len, place, 0); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 16-bit, signed int array field to the compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 16-bit, signed ints to add
 * @param len number of ints from array to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if array cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, vals or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt16Array(void *vmsg, const char *name, const int16_t vals[], int len, int place) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_INT16_A, len, place, 0); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 32-bit, signed int array field to the compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 32-bit, signed ints to add
 * @param len number of ints from array to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if array cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, vals or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt32Array(void *vmsg, const char *name, const int32_t vals[], int len, int place) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_INT32_A, len, place, 0); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 64-bit, signed int array field to the compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 364-bit, signed ints to add
 * @param len number of ints from array to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if array cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, vals or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt64Array(void *vmsg, const char *name, const int64_t vals[], int len, int place) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_INT64_A, len, place, 0); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 8-bit, unsigned int array field to the compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 8-bit, unsigned ints to add
 * @param len number of ints from array to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if array cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, vals or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint8Array(void *vmsg, const char *name, const uint8_t vals[], int len, int place) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_UINT8_A, len, place, 0); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 16-bit, unsigned int array field to the compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 16-bit, unsigned ints to add
 * @param len number of ints from array to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if array cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, vals or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint16Array(void *vmsg, const char *name, const uint16_t vals[], int len, int place) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_UINT16_A, len, place, 0); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 32-bit, unsigned int array field to the compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 32-bit, unsigned ints to add
 * @param len number of ints from array to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if array cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, vals or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint32Array(void *vmsg, const char *name, const uint32_t vals[], int len, int place) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_UINT32_A, len, place, 0); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 64-bit, unsigned int array field to the compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 64-bit, unsigned ints to add
 * @param len number of ints from array to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if array cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, vals or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint64Array(void *vmsg, const char *name, const uint64_t vals[], int len, int place) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_UINT64_A, len, place, 0); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named string field to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val string field to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 * @param isSystem if true allows using names starting with "cmsg", else not
 * @param copy if true, copy the string "val", else record the pointer and assume ownership
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if string cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, val or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
static int addString(void *vmsg, const char *name, const char *val, int place, int isSystem, int copy) {
  payloadItem *item;
  int ok, totalLen, textLen, valLen;
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);
  if (place < 0 &&
      place != CMSG_CP_END &&
      place != CMSG_CP_MARKER)                    return(CMSG_BAD_ARGUMENT);
  if (!goodFieldName(name, isSystem))             return(CMSG_BAD_FORMAT);
  if (nameExists(vmsg, name))                     return(CMSG_ALREADY_EXISTS);
  if (isSystem) isSystem = 1;
  
  /* payload item */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);
  
  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  if (copy) {
    item->array = (void *)strdup(val);
    if (item->array == NULL) {
      payloadItemFree(item);
      free(item);
      return(CMSG_OUT_OF_MEMORY);
    }
  }
  else {
    item->array = (void *)val;
  }
  item->type  = CMSG_CP_STR;
  item->count = 1;
  
  /* Create string to hold all data to be transferred over
   * the network for this item. */
  valLen = strlen(val);
   
  textLen  = numDigits(valLen, 0) +
             valLen +
             2; /* 2 newlines*/
  item->noHeaderLen = textLen;
  
  totalLen = strlen(name) +
             2 + /* 2 digit type */
             numDigits(item->count, 0) +
             1 + /* isSystem */
             numDigits(textLen, 0) +
             6 + /* 4 spaces, 1 newline, 1 null term */
             textLen;

  item->text = (char *) calloc(1, totalLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  sprintf(item->text, "%s %d %d %d %d\n%d\n%s\n", name, item->type, item->count,
                                                  isSystem, textLen, valLen, val);
  item->length = strlen(item->text);

  /* place payload item in msg's linked list */
  ok = insertItem(msg, item, place);
  if (!ok) {
    payloadItemFree(item);
    free(item);
    return(CMSG_ERROR);  
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named string field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val string to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if string cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, val or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddString(void *vmsg, const char *name, const char *val, int place) {
  return addString(vmsg, name, val, place, 0, 1);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named string array field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val strings to add
 * @param len number of strings to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 * @param copy if true, copy the strings in "vals", else record the pointer and assume ownership
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if array cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, vals or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
static int addStringArray(void *vmsg, const char *name, const char **vals, int len,
                          int place, int isSystem, int copy) {
  int i, ok, cLen, totalLen, textLen=0;
  payloadItem *item;
  char *s;
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || vals == NULL) return(CMSG_BAD_ARGUMENT);
  if (place < 0 &&
      place != CMSG_CP_END &&
      place != CMSG_CP_MARKER)                     return(CMSG_BAD_ARGUMENT);
  if (!goodFieldName(name, isSystem))              return(CMSG_BAD_FORMAT);
  if (nameExists(vmsg, name))                      return(CMSG_ALREADY_EXISTS);
  if (isSystem) isSystem = 1;
  
  /* payload item */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);
  
  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  if (copy) {  
    /* allocate memory to store "len" number of pointers */
    item->array = calloc(len, sizeof(char *));
    if (item->array == NULL) {
      payloadItemFree(item);
      free(item);
      return(CMSG_OUT_OF_MEMORY);
    }

    /* copy all strings for storage in message */
    for (i=0; i < len; i++) {
        ((char **)(item->array))[i] = strdup(vals[i]);
    }
  }
  else {
    item->array = (void *)vals;
  }
  
  item->type  = CMSG_CP_STR_A;
  item->count = len;
  
  /* Create string to hold all data to be transferred over
   * the network for this item. */
   
  for (i=0; i<len; i++) {
     /* digits in length + length of string + 2 newlines */
     textLen += numDigits(strlen(vals[i]), 0) + strlen(vals[i]) + 2;
  }
   
  item->noHeaderLen = textLen;
  
  totalLen = strlen(name) +
             2 + /* 2 digit type */
             numDigits(item->count, 0) +
             1 + /* isSystem */
             numDigits(textLen, 0) +
             6 + /* 4 spaces, 1 newline, 1 null term */
             textLen;

  s = item->text = (char *) calloc(1, totalLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  sprintf(s, "%s %d %d %d %d\n%n", name, item->type, item->count, isSystem, textLen, &cLen);
  s += cLen;
  
  for (i=0; i<len; i++) {
    sprintf(s, "%d\n%s\n%n", strlen(vals[i]), vals[i], &cLen);
    s += cLen;
  }
  item->length = strlen(item->text);

  /* place payload item in msg's linked list */
  ok = insertItem(msg, item, place);
  if (!ok) {
    payloadItemFree(item);
    free(item);
    return(CMSG_ERROR);  
  }
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named string array field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val strings to add
 * @param len number of strings to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if array cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, vals or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddStringArray(void *vmsg, const char *name, const char **vals, int len, int place) {
  return addStringArray(vmsg, name, vals, len, place, 0, 1);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named cMsg message field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 * The string representation of the message is the same format as that used for 
 * a complete compound payload.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param message cMsg message to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if string cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, src or name is NULL, size < 1, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_BAD_FORMAT if name is not properly formed,
 *                          or if error in binary-to-text transformation
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 *
 */   
static int addMessage(void *vmsg, char *name, const void *vmessage, int place, int isSystem) {
  char *s;
  int ok, len, binLen=0, count=0;
  int textLen=0, totalLen=0, length[12], numChars;
  payloadItem *item, *pItem;  
  cMsgMessage_t *msg     = (cMsgMessage_t *)vmsg;
  cMsgMessage_t *message = (cMsgMessage_t *)vmessage;

  
  if (msg  == NULL ||
      name == NULL ||
      message == NULL)                return(CMSG_BAD_ARGUMENT);
  if (place < 0 &&
      place != CMSG_CP_END &&
      place != CMSG_CP_MARKER)        return(CMSG_BAD_ARGUMENT);
  if (!goodFieldName(name, isSystem)) return(CMSG_BAD_FORMAT);
  if (nameExists(vmsg, name))         return(CMSG_ALREADY_EXISTS);
  if (isSystem) isSystem = 1;
  
  /* payload item to add to msg */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);

  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type  = CMSG_CP_MSG;
  item->count = 1;
  
  /* store original data */
  item->array = (void *)cMsgCopyMessage(vmessage);
  if (item->array == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* *************************************************** */
  /* First do some counting and find out how much space  */
  /* we need to store everything in message as a string. */
  /* *************************************************** */

  /* find length of payload */
  pItem = message->payload;
  while (pItem != NULL) {
    textLen += pItem->length;
    pItem = pItem->next;
    count++;
  }
  
  /* *************************************************** */
  /* add up to 9 strings: domain, subject, type, text,   */
  /* creator, sender, senderHost, reciever, receiverHost */
  /* *************************************************** */
  
  /* add length of "domain" member as a string */
  if (message->domain != NULL) {
    count++;
    /* length of text following header line, for this string item */
    length[0] = strlen(message->domain)
                + numDigits(strlen(message->domain), 0)
                + 2 /* 2 newlines */;
    textLen += strlen("cMsgDomain")
               + 9 /* 2-digit type, 1-digit count (=1), 1-digit isSystem, 4 spaces, and 1 newline */
               + numDigits(length[0], 0) /* # chars in following, nonheader text */
               + length[0];  /* this item's nonheader text */
  }
  
  /* add length of "subject" member as a string */
  if (message->subject != NULL) {
    count++;
    length[1] = strlen(message->subject) + numDigits(strlen(message->subject), 0) + 2;
    textLen += strlen("cMsgSubject") + 9 + numDigits(length[1], 0) + length[1];
  }
  
  /* add length of "type" member as a string */
  if (message->type != NULL) {
    count++;
    length[2] = strlen(message->type) + numDigits(strlen(message->type), 0) + 2;
    textLen += strlen("cMsgType") + 9 + numDigits(length[2], 0) + length[2] ;
  }
  
  /* add length of "text" member as a string */
  if (message->text != NULL) {
    count++;
    length[3] = strlen(message->text) + numDigits(strlen(message->text), 0) + 2;
    textLen += strlen("cMsgText") + 9 + numDigits(length[3], 0) + length[3] ;
  }
  
  /* add length of "creator" member as a string */
  if (message->creator != NULL) {
    count++;
    length[4] = strlen(message->creator) + numDigits(strlen(message->creator), 0) + 2;
    textLen += strlen("cMsgCreator") + 9 + numDigits(length[4], 0) + length[4] ;
  }
  
  /* add length of "sender" member as a string */
  if (message->sender != NULL) {
    count++;
    length[5] = strlen(message->sender) + numDigits(strlen(message->sender), 0) + 2;
    textLen += strlen("cMsgSender") + 9 + numDigits(length[5], 0) + length[5] ;
  }
  
  /* add length of "senderHost" member as a string */
  if (message->senderHost != NULL) {
    count++;
    length[6] = strlen(message->senderHost) + numDigits(strlen(message->senderHost), 0) + 2;
    textLen += strlen("cMsgSenderHost") + 9 + numDigits(length[6], 0) + length[6] ;
  }
  
  /* add length of "receiver" member as a string */
  if (message->receiver != NULL) {
    count++;
    length[7] = strlen(message->receiver) + numDigits(strlen(message->receiver), 0) + 2;
    textLen += strlen("cMsgReceiver") + 9 + numDigits(length[7], 0) + length[7] ;
  }
  
  /* add length of "receiverHost" member as a string */
  if (message->receiverHost != NULL) {
    count++;
    length[8] = strlen(message->receiverHost) + numDigits(strlen(message->receiverHost), 0) + 2;
    textLen += strlen("cMsgReceiverHost") + 9 + numDigits(length[8], 0) + length[8] ;
  }
  
  /* ************************************************************************************** */
  /* add length of 1 array of 5 ints: version, info, reserved, byteArrayLength, and userInt */
  /* ************************************************************************************** */
  
  /* add length of string to contain ints */
  length[9] = numDigits(message->version,0) + numDigits(message->info,0) + numDigits(message->reserved,0) +
              numDigits(message->byteArrayLength,0) + numDigits(message->userInt,0) + 5 /* 4 sp, 1 nl */;
  textLen += strlen("cMsgInts") + 2 + 1 + 1 + numDigits(length[9], 0) + length[9] +
             5; /* 4 sp, 1 nl */
  count++;

  /* ************************************************************************************** */
  /* add length of 1 array of 6 64-bit ints for 3 times: userTime, senderTime, receiverTime */
  /* ************************************************************************************** */
  
  /* add length of string to contain userTime */
  length[10] = numDigits(message->userTime.tv_sec,0) + numDigits(message->userTime.tv_nsec,0) +
               numDigits(message->senderTime.tv_sec,0) + numDigits(message->senderTime.tv_nsec,0) +
               numDigits(message->receiverTime.tv_sec,0) + numDigits(message->receiverTime.tv_nsec,0) +
               6 /* 5 sp, 1 nl */;
  textLen += strlen("cMsgTimes") + 2 + 1 + 1 +  numDigits(length[10], 0) + length[10] +
             5; /* 4 sp, 1 nl */
  count++;

  /* ************************** */
  /* add length of binary field */
  /* ************************** */
  if (message->byteArray != NULL) {
    /* find size of text-encoded binary data (exact) */
    binLen = cMsg_b64_encode_len(message->byteArray + message->byteArrayOffset, message->byteArrayLength);
    length[11] = binLen + numDigits(binLen, 0) + 2 /* 2 newlines */;
    
    textLen += strlen("cMsgBinary") + 2 + numDigits(message->byteArrayLength, 0) + 1 +
               numDigits(length[11], 0) + length[11] + 5; /* 4 spaces, 1 newline */
    count++;
  }
  
  /* *************************************************************** */
  /* string representing compound payload is (# of fields) + newline */
  /* *************************************************************** */
  
  textLen += numDigits(count, 0) + 1;
  
  /* ************************************* */
  /* add length of header to message field */
  /* ************************************* */
  /* header of message field in msg's payload is "name type count isSystem? length\n" */
  totalLen += strlen(name) + 2 /* type len */ + 1 /* 1 digit in count = 1 */
              + 1 /* isSys? */ + numDigits(textLen, 0) + textLen + 5; /* 4 spaces, 1 newline */
 
  totalLen += 1; /* for null terminator */
  
  item->noHeaderLen = textLen;
  
  s = item->text = (char *) malloc(totalLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* 1st, write header of message field */
  sprintf(s, "%s %d 1 %d %d\n%n", name, CMSG_CP_MSG, isSystem, textLen, &len);
  s+= len;
  
  /* From here on it's the same format used as for sending payload over network */
  /* 2nd, write how many fields there are */
  sprintf(s, "%d\n%n",count, &len);
  s+= len;
  
  /* next write strings */
  
  /* add message's domain member as string */
  if (message->domain != NULL) {
    sprintf(s, "%s %d 1 %d %d\n%d\n%s\n%n", "cMsgDomain", CMSG_CP_STR, isSystem, length[0],
                                             strlen(message->domain), message->domain, &len);
    s += len;
  }
  
  /* add message's subject member as string */
  if (message->subject != NULL) {
    sprintf(s, "%s %d 1 %d %d\n%d\n%s\n%n", "cMsgSubject", CMSG_CP_STR, isSystem, length[1],
                                            strlen(message->subject), message->subject, &len);
    s += len;
  }
  
  /* add message's type member as string */
  if (message->type != NULL) {
    sprintf(s, "%s %d 1 %d %d\n%d\n%s\n%n", "cMsgType", CMSG_CP_STR, isSystem, length[2],
                                             strlen(message->type), message->type, &len);
    s += len;
  }
  
  /* add message's text member as string */
  if (message->text != NULL) {
    sprintf(s, "%s %d 1 %d %d\n%d\n%s\n%n", "cMsgText", CMSG_CP_STR, isSystem, length[3],
                                            strlen(message->text), message->text, &len);
    s += len;
  }

  /* add message's creator member as string */
  if (message->creator != NULL) {
    sprintf(s, "%s %d 1 %d %d\n%d\n%s\n%n", "cMsgCreator", CMSG_CP_STR, isSystem, length[4],
                                            strlen(message->creator), message->creator, &len);
    s += len;
  }

  /* add message's sender member as string */
  if (message->sender != NULL) {
    sprintf(s, "%s %d 1 %d %d\n%d\n%s\n%n", "cMsgSender", CMSG_CP_STR, isSystem, length[5],
                                             strlen(message->sender), message->sender, &len);
    s += len;
  }

  /* add message's senderHost member as string */
  if (message->senderHost != NULL) {
    sprintf(s, "%s %d 1 %d %d\n%d\n%s\n%n", "cMsgSenderHost", CMSG_CP_STR, isSystem, length[6],
                                            strlen(message->senderHost), message->senderHost, &len);
    s += len;
  }

  /* add message's receiver member as string */
  if (message->receiver != NULL) {
    sprintf(s, "%s %d 1 %d %d\n%d\n%s\n%n", "cMsgReceiver", CMSG_CP_STR, isSystem, length[7],
                                            strlen(message->receiver), message->receiver, &len);
    s += len;
  }

  /* add message's receiverHost member as string */
  if (message->receiverHost != NULL) {
    sprintf(s, "%s %d 1 %d %d\n%d\n%s\n%n", "cMsgReceiverHost", CMSG_CP_STR, isSystem, length[8],
                                            strlen(message->receiverHost), message->receiverHost, &len);
    s += len;
  }

  /* next write 5 ints */
  sprintf(s, "%s %d 5 %d %d\n%n", "cMsgInts", CMSG_CP_INT32_A, isSystem, length[9], &len);
  s+= len;
  sprintf(s, "%d %d %d %d %d\n%n", message->version, message->info, message->reserved,
                                   message->byteArrayLength, message->userInt, &len);
  s+= len;

  /* next write 6 64-bit ints */
  sprintf(s, "%s %d 6 %d %d\n%n", "cMsgTimes", CMSG_CP_INT64_A, isSystem, length[10], &len);
  s+= len;
  sprintf(s, "%ld %ld %ld %ld %ld %ld\n%n", message->userTime.tv_sec, message->userTime.tv_nsec,
                                            message->senderTime.tv_sec, message->senderTime.tv_nsec,
                                            message->receiverTime.tv_sec, message->receiverTime.tv_nsec, &len);
  s+= len;
  
  if (message->byteArray != NULL) {
    /* write first line and stop */
    sprintf(s, "%s %d %d %d %d\n%n", "cMsgBinary", CMSG_CP_BIN, message->byteArrayLength,
                                     isSystem, length[11], &len);
    s+= len;
    
    /* write the length */
    sprintf(s, "%u\n%n", length[11], &len);
    s+= len;

    /* write the binary-encoded text */
    numChars = cMsg_b64_encode(message->byteArray + message->byteArrayOffset, message->byteArrayLength, s);
    s += numChars;
    if (binLen != numChars) {
      payloadItemFree(item);
      free(item);
      return(CMSG_BAD_FORMAT);  
    }
    
    /* add newline */
    sprintf(s++, "\n");
  }
    
  /* add payload fields */
  pItem = message->payload;
  while (pItem != NULL) {
    sprintf(s, "%s%n", pItem->text, &len);
    pItem = pItem->next;
    s += len;
  }
  
  /* add null terminator */
  s[0] = '\0';
  
  /* store length of text */
  item->length = strlen(item->text);
  
  /* place payload item in msg's linked list */
  ok = insertItem(msg, item, place);
  if (!ok) {
    payloadItemFree(item);
    free(item);
    return(CMSG_ERROR);  
  }
    
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named cMsg message field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 * The string representation of the message is the same format as that used for 
 * a complete compound payload.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param message cMsg message to add
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if string cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, src or name is NULL, size < 1, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_BAD_FORMAT if name is not properly formed,
 *                          or if error in binary-to-text transformation
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 *
 */   
int cMsgAddMessage(void *vmsg, char *name, const void *vmessage, int place) {
  return addMessage(vmsg, name, vmessage, place, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named cMsg message field to the compound payload of a message.
 * In this case, the message doesn't need to be copied but is owned by payload item
 * being created.  The text representation of the msg must be copied in (but doesn't
 * need to be generated from the given message).
 * The string representation of the message is the same format as that used for 
 * a complete compound payload.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param message cMsg message to add
 * @param msgText text form of cMsg message to add
 * @param textLength length (in chars) of msgText arg
 * @param place place of field in payload item order (0 = first), 
 *              CMSG_CP_END if placed at the end, or
 *              CMSG_CP_MARKER if placed after marker
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if string cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message, src or name is NULL, size < 1, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_BAD_FORMAT if name is not properly formed,
 *                          or if error in binary-to-text transformation
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_ALREADY_EXISTS if name is being used already

 */   
static int addMessageFromText(void *vmsg, char *name, void *vmessage, char *msgText,
                              int textLength, int place, int isSystem) {
  char *s;
  int ok;
  payloadItem *item;  
  cMsgMessage_t *msg     = (cMsgMessage_t *)vmsg;
  cMsgMessage_t *message = (cMsgMessage_t *)vmessage;

  
  if (msg == NULL     ||
      name == NULL    ||
      message == NULL ||
      msgText == NULL)                return(CMSG_BAD_ARGUMENT);
  if (place < 0 &&
      place != CMSG_CP_END &&
      place != CMSG_CP_MARKER)        return(CMSG_BAD_ARGUMENT);
  /* The minimum length text rep of a msg is ... */
  /* header -> 7 min + field count -> 2 min + body -> 110 min = 119 min chars */
  if (textLength < 119)               return(CMSG_BAD_ARGUMENT);
  if (!goodFieldName(name, isSystem)) return(CMSG_BAD_FORMAT);
  if (nameExists(vmsg, name))         return(CMSG_ALREADY_EXISTS);
  
  /* payload item to add to msg */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);

  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type  = CMSG_CP_MSG;
  item->count = 1;
  
  /* we now own vmessage */
  item->array = vmessage;
  s = item->text = (char *) malloc(textLength+1);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* copy in text */
  memcpy(s, msgText, textLength);
    
  /* add null terminator */
  s[textLength] = '\0';
  
  /* store length of text */
  item->length = strlen(item->text);
  
  /* place payload item in msg's linked list */
  ok = insertItem(msg, item, place);
  if (!ok) {
    payloadItemFree(item);
    free(item);
    return(CMSG_ERROR);  
  }
    
  return(CMSG_OK);
}

  


/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/






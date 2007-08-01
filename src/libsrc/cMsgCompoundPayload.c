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
 * strings and arrays of all these types can be stored and retrieved from the
 * compound payload. These routines are thread-safe.
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
 * Following is the format:
 *  the first line is the number of fields to follow:
      field_count<newline>
      
 *  for all types except string:
 *    field_name field_type field_count<newline>
 *    value value ... value<newline>
 *
 *  for strings:
 *    field_name field_type field_count<newline>
 *    string_length<newline>
 *    string_characters<newline>
 *     .
 *     .
 *     .
 *    string_length<newline>
 *    string_characters<newline>
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
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <math.h>
#include <inttypes.h>

#include "cMsgPrivate.h"

/** Maximum len in chars for a payload item name. */
#define CMSG_PAYLOAD_NAME_LEN 128

/*-------------------------------------------------------------------*/
/* prototypes of static functions */
static int  setFieldsFromText(void *vmsg, const char *text, int flag);
static int  insertItem(cMsgMessage_t *msg, payloadItem *item, int place);
static int  moveItem(cMsgMessage_t *msg, const char *name, int placeFrom, int placeTo);
static int  removeItem(cMsgMessage_t *msg, const char *name, int place, payloadItem **pitem);
static int  setMarker(cMsgMessage_t *msg, const char *name, int place);

static int  addInt(void *vmsg, const char *name, int64_t val, int type, int place, int isSystem);
static int  addIntArray(void *vmsg, const char *name, const int *vals,
                       int type, int len, int place, int isSystem);
static int  addString(void *vmsg, const char *name, const char *val, int place, int isSystem);
static int  addStringArray(void *vmsg, const char *name, const char *vals[],
                           int len, int place, int isSystem);
static int  addReal(void *vmsg, const char *name, double val, int type, int place, int isSystem);
static int  addRealArray(void *vmsg, const char *name, const double *vals,
                         int type, int len, int place, int isSystem);

static int  getInt(const void *vmsg, int type, int64_t *val);
static int  getReal(const void *vmsg, int type, double *val);
static int  getArray(const void *vmsg, int type, void **vals, int *len);

static int  goodFieldName(const char *s, int isSystem);
static void payloadItemInit(payloadItem *item);
static void payloadItemFree(payloadItem *item);
static int  nameExists(const void *vmsg, const char *name);
static void setPayload(cMsgMessage_t *msg, int hasPayload);
static int  numDigits(int64_t i, int isUint64);
static void grabMutex(void);
static void releaseMutex(void);
static payloadItem *copyPayloadItem(const payloadItem *from);

/*-------------------------------------------------------------------*/

/** Excluded characters from name strings. */
static const char *excludedChars = " \t\n`\'\"";

/*-------------------------------------------------------------------*/

/** Mutex to make the payload linked list thread-safe. */
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/** Routine to grab the pthread mutex used to protect payload linked list. */
static void grabMutex(void) {
  int status = pthread_mutex_lock(&mutex);
  if (status != 0) {
    cmsg_err_abort(status, "Lock linked list Mutex");
  }
}

/** Routine to release the pthread mutex used to protect payload linked list. */
static void releaseMutex(void) {
  int status = pthread_mutex_unlock(&mutex);
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

  item->type   = 0;
  item->count  = 0;
  item->length = 0;
  item->text   = NULL;
  item->name   = NULL;
  item->next   = NULL;
  
  item->array  = NULL;
  item->val    = 0;
  item->dval   = 0.;
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
 * @returns CMSG_BAD_ARGUMENT if vmsg is NULL
 */   
int cMsgHasPayload(const void *vmsg, int *hasPayload) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL) return(CMSG_BAD_ARGUMENT);
  
  *hasPayload = ((msg->info | CMSG_HAS_PAYLOAD) > 0) ? 1 : 0;

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine frees the allocated memory of the given message's entire payload.
 *
 * @param vmsg pointer to message
 */   
void cMsgPayloadFree(void *vmsg) {  
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
  
  releaseMutex();
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
  
  releaseMutex();
}


/*-------------------------------------------------------------------*/


/**
 * This routine returns the number of digits in an integer
 * including a minus sign.
 *
 * @param i integer
 * @returns number of digits in the integer argument
 */   
static int numDigits(int64_t i, int isUint64) {
  int num, negative=0;
  if (i == 0) {
    return 1;
  }
  else if (i < 0) {
    if (!isUint64) {
      i *= -1;
      negative = 1;
    }
  }
  if (isUint64) {
    num = log10((double)((uint64_t) i));
  }
  else {
    num = log10((double) i);
  }
  num++;
  if (negative) num++;
  return(num);
}


/*-------------------------------------------------------------------*/


/**
 * This routine gets the location of the marker.
 *
 * @param vmsg pointer to message
 * @param place pointer which gets filled in with the place of the current field (position of marker)
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if marker cannot be found
 * @returns CMSG_BAD_ARGUMENT if either argument or marker is NULL
 */   
int cMsgGetFieldPosition(const void *vmsg, int *place) {
  int i=0;
  payloadItem *item;
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || place == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  if (msg->marker == NULL) {
    releaseMutex();
    return(CMSG_BAD_ARGUMENT);
  }

  item = msg->payload;
  while (item != NULL) {
    if (item == msg->marker) {
      *place = i;
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
 * This routine places the marker at the named field if its exists.
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
 * This routine places the marker at the numbered field if it exists.
 *
 * @param vmsg pointer to message
 * @param place number of payload item to find (first = 0),
 *              or CMSG_CP_END to find the last item
 *
 * @returns 1 if successful
 * @returns 0 if no such numbered field exists
 */   
int cMsgGoToField(void *vmsg, int place) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(0);
  if (place < 0 && place != CMSG_CP_END) return(0);
  
  return setMarker(msg, NULL, place);  
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine places the marker at the next field if its exists.
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
 * This routine places the marker at the first field if its exists.
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
 * This routine removes the numbered field if it exists.
 *
 * @param vmsg pointer to message
 * @param place place in list of payload items (first = 0) or 
 *              CMSG_CP_END if last item, or CMSG_CP_MARKER if item at marker
 *
 * @returns 1 if successful
 * @returns 0 if no numbered field
 */   
int cMsgRemoveField(void *vmsg, int place) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(0);
  if (place < 0 && place != CMSG_CP_END) return(0);
  
  return removeItem(msg, NULL, place, NULL);
  
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
 * This routine moves the numbered field if it exists.
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
 * This routine sets the marker to a given payload item. If the "name"
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
  payloadItem *prev, *next;

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
static int setFieldsFromText(void *vmsg, const char *text, int flag) {
  char *s, *t, *tt, name[CMSG_PAYLOAD_NAME_LEN+1], *buf;
  int i, j, k, err, len, val, type, count, fields, ignore, isSystem, numChars, debug=0;
  int64_t   int64;
  uint64_t uint64;
  double   dbl;
  float    flt;
  
  /* payloadItem *item, *prev; */
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || text == NULL) return(CMSG_BAD_ARGUMENT);
  
  t = text;
  s = strpbrk(t, "\n");
  if (s == NULL) return(CMSG_BAD_FORMAT);
  
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
  
    /* read line */
    memset(name, 0, CMSG_PAYLOAD_NAME_LEN+1);
    sscanf(t, "%s%d%d", name, &type, &count);
if(debug) printf("FIELD #%d, name = %s, type = %d, count = %d, t = %p\n", i, name, type, count, t);
    
    if (strlen(name) < 1 || count < 1 ||
        type < CMSG_CP_STR || type > CMSG_CP_DBL_A) return(CMSG_BAD_FORMAT);
    
    /* ignore certain fields (system fields start with "cmsg") */
    isSystem = strncasecmp(name, "cmsg", 4) == 0 ? 1 : 0;
    ignore = isSystem;               /* by default ignore a system field, for flag == 1 */
    if (flag == 0) ignore = !ignore; /* only set system fields, for flag = 0*/
    else if (flag == 2) ignore = 0;  /* deal with all fields, for flag = 2 */
        
    /* READ IN STRINGS */
    if (type == CMSG_CP_STR || type == CMSG_CP_STR_A) {

      t = s+1;
      s = strpbrk(t, "\n");
      if (s == NULL) return(CMSG_BAD_FORMAT);
     
      if (ignore) {
        for (j=0; j<count; j++) {
          /* read length of string */
          sscanf(t, "%d", &len);
          if (len < 1) return(CMSG_BAD_FORMAT);
          /* skip over string */
          t = s+1+len+1;
          s = strpbrk(t, "\n");
          if (s == NULL) return(CMSG_BAD_FORMAT);
if(debug) printf("  skipped string\n");
        }
      }
      /* single string */
      else if (type == CMSG_CP_STR) {
          /* read length of string */
          sscanf(t, "%d", &len);
          if (len < 1) return(CMSG_BAD_FORMAT);
if(debug) printf("  single string len = %d, t = %p\n", len, t);
          t = s+1;
          buf = (char *) calloc(1, len+1);
          if (buf == NULL) return(CMSG_OUT_OF_MEMORY);
          strncpy(buf, t, len);
if(debug) printf("  single string = %s, length of string = %d, t = %p\n", buf, strlen(buf), t);
          t = s+1+len+1;
          s = strpbrk(t, "\n");
          /* s will be null if it's the very last item */
          if (s == NULL && i != fields-1) return(CMSG_BAD_FORMAT);
if(debug) printf("  skip to t = %p\n", t);
          /* special case where cMsgText payload field is the message text */
          if (isSystem && strcasecmp(name, "cMsgText") == 0) {
              cMsgSetText(vmsg, buf);
          }
          else {
            err = addString(vmsg, name, buf, CMSG_CP_MARKER, 1);
            if (err != CMSG_OK) {
              if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
              return(err);
            }
          }

          free(buf);
      }
      /* array of strings */
      else {
        /* create array in which to store strings */
        char* myArray[count];

        for (j=0; j<count; j++) {
          /* read length of string */
          sscanf(t, "%d", &len);
          if (len < 1) return(CMSG_BAD_FORMAT);
if(debug) printf("  string[%d] len = %d, t = %p\n", j, len, t);
          t = s+1;
          buf = (char *) calloc(1, len+1);
          if (buf == NULL) {
            for (k=j-1; k>=0; k--) free(myArray[k]);
            return(CMSG_OUT_OF_MEMORY);
          }
          strncpy(buf, t, len);
if(debug) printf("  string[%d] = %s, length of string = %d, t = %p\n", j, buf, strlen(buf), t);
          myArray[j] = buf;
          t = s+1+len+1;
          s = strpbrk(t, "\n");
          /* s will be null if it's the very last item */
          if (s == NULL && i != fields-1  && j != count-1) return(CMSG_BAD_FORMAT);
        }

        /* add array to payload (cast to avoid compiler warning )*/
        err = addStringArray(vmsg, name, (const char **)myArray, count, CMSG_CP_MARKER, 1);
        if (err != CMSG_OK) {
          if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
          return(err);
        }

        /* free up mem we just allocated */
        for (j=0; j<count; j++) free(myArray[j]);
      }
    }
    
    /* READ IN NUMBERS */
    else {
      
      /* move to next line to read number(s) */
      t = s+1;
      s = strpbrk(t, "\n");
      if (s == NULL) return(CMSG_BAD_FORMAT);
      
      if (ignore) {
        /* skip over numbers (go to next newline) */
if(debug) printf("  skipping over number(s)\n");
      }

      /* reals */
      else if (type == CMSG_CP_FLT || type == CMSG_CP_DBL) {
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

        /* add array to payload */
        err = addIntArray(vmsg, name, (const int *)myArray, type, count, CMSG_CP_MARKER, 1); 
        if (err != CMSG_OK) {
          if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
          return(err);
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

        /* add array to payload */
        err = addIntArray(vmsg, name, (const int *)myArray, type, count, CMSG_CP_MARKER, 1); 
        if (err != CMSG_OK) {
          if (err == CMSG_BAD_ARGUMENT) err = CMSG_BAD_FORMAT;
          return(err);
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
  
  item->type = from->type;
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


/**
 * This routine returns a pointer to the string representation of the
 * whole compound payload and the hidden system fields of the message
 * (which currently is only the "text") as it gets sent over the network.
 * Memory is allocated for the returned string so it must be freed by the user.
 *
 * @param vmsg pointer to message
 *
 * @returns NULL if no payload exists or no memory
 * @returns text if payload exists (must be freed by user)
 */   
char *cMsgGetPayloadText(const void *vmsg) {
  char *s;
  int totalLen=0, count=0;
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  grabMutex();
  
  if (msg == NULL || msg->payload == NULL) {
    releaseMutex();
    return(NULL);
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
    totalLen += strlen("cMsgText") +
                2 + /* 2 digit type */
                numDigits(1, 0) +
                numDigits(strlen(msg->text), 0) +
                strlen(msg->text) + 
                5; /* 2 spaces, 3 newlines */
  }
  
  totalLen += numDigits(count, 0) + 1; /* send count & newline first */
  totalLen += 1; /* for null terminator */
  
  s = (char *) calloc(1, totalLen);
  if (s == NULL) {
    releaseMutex();
    return(NULL);
  }
  
  /* first item is number of fields to come (count) & newline */
  sprintf(s, "%d\n", count);
  
  /* add message text is there is one */
  if (msg->text != NULL) {
    sprintf(s + strlen(s), "%s %d 1\n%d\n%s\n", "cMsgText", CMSG_CP_STR, strlen(msg->text), msg->text);
  }
  
  /* add payload fields */
  item = msg->payload;
  while (item != NULL) {
    sprintf(s + strlen(s), "%s", item->text);
    item = item->next;
  }
  
  releaseMutex();
  return(s);
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
const char *cMsgGetFieldDescription(const void *vmsg) {
  static char s[64];
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  int count;

  if (msg == NULL || msg->marker == NULL) return(NULL);
  
  count = msg->marker->count;
  
  switch (msg->marker->type) {
    case CMSG_CP_STR:
        if (count > 1) sprintf(s, "string");
        else sprintf(s, "string");
        break;
    case CMSG_CP_INT8:
        if (count > 1) sprintf(s, "8 bit int");
        else sprintf(s, "8 bit int");
        break;
    case CMSG_CP_INT16:
        if (count > 1) sprintf(s, "16 bit int");
        else sprintf(s, "16 bit int");
        break;
    case CMSG_CP_INT32:
        if (count > 1) sprintf(s, "32 bit int");
        else sprintf(s, "32 bit int");
        break;
    case CMSG_CP_INT64:
        if (count > 1) sprintf(s, "64 bit int");
        else sprintf(s, "64 bit int");
        break;
    case CMSG_CP_UINT8:
        if (count > 1) sprintf(s, "8 bit unsigned int");
        else sprintf(s, "8 bit unsigned int");
        break;
    case CMSG_CP_UINT16:
        if (count > 1) sprintf(s, "16 bit unsigned int");
        else sprintf(s, "16 bit unsigned int");
        break;
    case CMSG_CP_UINT32:
        if (count > 1) sprintf(s, "32 bit unsigned int");
        else sprintf(s, "32 bit unsigned int");
        break;
    case CMSG_CP_UINT64:
        if (count > 1) sprintf(s, "64 bit unsigned int");
        else sprintf(s, "64 bit unsigned int");
        break;
    case CMSG_CP_FLT:
        if (count > 1) sprintf(s, "32 bit float");
        else sprintf(s, "32 bit float");
        break;
    case CMSG_CP_DBL:
        if (count > 1) sprintf(s, "64 bit double");
        else sprintf(s, "64 bit double");
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
void cMsgPayloadPrintout(void *msg) {
  int type, ok, j, len, place;
  
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

  do {
    printf("FIELD %s", cMsgGetFieldName(msg));
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
        {int8_t *i; ok=cMsgGetInt8Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("  int8[%d] = %d\n", j,i[j]);} break;
      case CMSG_CP_INT16_A:
        {int16_t *i; ok=cMsgGetInt16Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("  int16[%d] = %hd\n", j,i[j]);} break;
      case CMSG_CP_INT32_A:
        {int32_t *i; ok=cMsgGetInt32Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("  int32[%d] = %d\n", j,i[j]);} break;
      case CMSG_CP_INT64_A:
        {int64_t *i; ok=cMsgGetInt64Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("  int64[%d] = %lld\n", j,i[j]);} break;
      case CMSG_CP_UINT8_A:
        {uint8_t *i; ok=cMsgGetUint8Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("  uint8[%d] = %u\n", j,i[j]);} break;
      case CMSG_CP_UINT16_A:
        {uint16_t *i; ok=cMsgGetUint16Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("  uint16[%d] = %hu\n", j,i[j]);} break;
      case CMSG_CP_UINT32_A:
        {uint32_t *i; ok=cMsgGetUint32Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("  uint8[%d] = %u\n", j,i[j]);} break;
      case CMSG_CP_UINT64_A:
        {uint64_t *i; ok=cMsgGetUint64Array(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("  uint64[%d] = %llu\n", j,i[j]);} break;
      case CMSG_CP_DBL_A:
        {double *i; ok=cMsgGetDoubleArray(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("  double[%d] = %.16lg\n", j,i[j]);} break;
      case CMSG_CP_FLT_A:
        {float *i; ok=cMsgGetFloatArray(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("  float[%d] = %.7g\n", j,i[j]);} break;
      case CMSG_CP_STR_A:
        {char **i; ok=cMsgGetStringArray(msg, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("  string[%d] = %s\n", j,i[j]);} break;
      default:
        printf("\n");
    }
  } while (cMsgNextField(msg));
  
  /* restore marker */
  cMsgGoToField(msg, place);
  
  return;  
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
 * This routine returns a pointer to the string representation of the field.
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


/**
 * This routine takes a pointer to a string representation of the
 * whole compound payload, including the system (hidden) fields of the message,
 * as it gets sent over the network and converts it into the standard message
 * payload. All system information is ignored. This overwrites any existing
 * payload and skips over any fields with names starting with "cMsg"
 * (as they are reserved for system use).
 *
 * @param vmsg pointer to message
 *
 * @returns NULL if no payload exists or no memory
 * @param text string sent over network to be unmarshalled
 */   
int cMsgSetPayloadFromText(void *vmsg, const char *text) {
  return setFieldsFromText(vmsg, text, 1);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine takes a pointer to a string representation of the
 * whole compound payload, including the system (hidden) fields of the message,
 * as it gets sent over the network and converts it into the hidden system fields
 * of the message. All non-system information is ignored. This overwrites any existing
 * system fields.
 *
 * @param vmsg pointer to message
 *
 * @returns NULL if no payload exists or no memory
 * @param text string sent over network to be unmarshalled
 */   
int cMsgSetSystemFieldsFromText(void *vmsg, const char *text) {
  return setFieldsFromText(vmsg, text, 0);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine takes a pointer to a string representation of the
 * whole compound payload, including the system (hidden) fields of the message,
 * as it gets sent over the network and converts it into the hidden system fields
 * and payload of the message. This overwrites any existing system fields and payload.
 *
 * @param vmsg pointer to message
 *
 * @returns NULL if no payload exists or no memory
 * @param text string sent over network to be unmarshalled
 */   
int cMsgSetAllFieldsFromText(void *vmsg, const char *text) {
  return setFieldsFromText(vmsg, text, 2);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the type of the current field.
 *
 * @param vmsg pointer to message
 * @param type pointer that gets filled with type
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_BAD_ARGUMENT if message or marker is NULL
 */   
int cMsgGetFieldType(const void *vmsg, int *type) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  grabMutex();
  if (msg == NULL || msg->marker == NULL) {
    releaseMutex();
    return(CMSG_BAD_ARGUMENT);
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
 * @returns CMSG_BAD_ARGUMENT if message or marker is NULL
 */   
int cMsgGetFieldCount(const void *vmsg, int *count) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  grabMutex();
  if (msg == NULL || msg->marker == NULL) {
    releaseMutex();
    return(CMSG_BAD_ARGUMENT);
  }
  *count = msg->marker->count;
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is array
 * @returns CMSG_BAD_ARGUMENT if vmsg or val is NULL
 */   
static int getReal(const void *vmsg, int type, double *val) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);

  grabMutex();
  
  item = msg->marker;
  
  if (item == NULL || item->type != type || item->count > 1) {
    releaseMutex();
    return(CMSG_ERROR);
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is not array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
 */   
static int getArray(const void *vmsg, int type, void **vals, int *len) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || vals == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  item = msg->marker;
  
  if (item == NULL || item->array == NULL || item->count < 1 || item->type != type) {
    releaseMutex();
    return(CMSG_ERROR);
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is not array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
 */   
int cMsgGetFloatArray(const void *vmsg, float **vals, int *len) {
  int   err;
  void *array;
    
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is not array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
 */   
int cMsgGetDoubleArray(const void *vmsg, double **vals, int *len) {
  int   err;
  void *array;
    
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is array
 * @returns CMSG_BAD_ARGUMENT if vmsg or val is NULL
 */   
static int getInt(const void *vmsg, int type, int64_t *val) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);

  grabMutex();
  
  item = msg->marker;
  
  if (item == NULL || item->type != type || item->count > 1) {
    releaseMutex();
    return(CMSG_ERROR);
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is not array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
 */   
int cMsgGetInt8Array(const void *vmsg, int8_t **vals, int *len) {
  int   err;
  void *array;
    
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is not array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
 */   
int cMsgGetInt16Array(const void *vmsg, int16_t **vals, int *len) {
  int   err;
  void *array;
    
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is not array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
 */   
int cMsgGetInt32Array(const void *vmsg, int32_t **vals, int *len) {
  int   err;
  void *array;
    
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is not array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
 */   
int cMsgGetInt64Array(const void *vmsg, int64_t **vals, int *len) {
  int   err;
  void *array;
    
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is not array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
 */   
int cMsgGetUint8Array(const void *vmsg, uint8_t **vals, int *len) {
  int   err;
  void *array;
    
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is not array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
 */   
int cMsgGetUint16Array(const void *vmsg, uint16_t **vals, int *len) {
  int   err;
  void *array;
    
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is not array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
 */   
int cMsgGetUint32Array(const void *vmsg, uint32_t **vals, int *len) {
  int   err;
  void *array;
    
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is not array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
 */   
int cMsgGetUint64Array(const void *vmsg, uint64_t **vals, int *len) {
  int   err;
  void *array;
    
  err = getArray(vmsg, CMSG_CP_UINT64_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (uint64_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the string current field if its exists.
 *
 * @param vmsg pointer to message
 * @param val pointer filled with field value
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if no current field, field is not right type, or field is array
 * @returns CMSG_BAD_ARGUMENT if message or val is NULL
 */   
int cMsgGetString(const void *vmsg, char **val) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  item = msg->marker;
  
  if (item == NULL || item->count > 1 || item->type != CMSG_CP_STR) {
    releaseMutex();
    return(CMSG_ERROR);
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
 * @returns CMSG_ERROR if no current field, field is not right type, or field is not array
 * @returns CMSG_BAD_ARGUMENT if message or array is NULL
 */   
int cMsgGetStringArray(const void *vmsg, char **array[], int *len) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || array == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  item = msg->marker;
  
  if (item == NULL || item->array == NULL || item->count < 1 || item->type != CMSG_CP_STR_A) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  
  *array = (char **) item->array;
  *len = item->count;
  
  releaseMutex();
  return(CMSG_OK);
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
  int ok, textLen=0, numLen;
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
  textLen = strlen(name) +
            2 + /* 2 digit type */
            numDigits(item->count, 0) +
            numLen +
            5; /* 2 spaces, 2 newlines, 1 null terminator */
  
  item->text = (char *) calloc(1, textLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  sprintf(item->text, "%s %d %d\n%s\n", name, item->type, item->count, num);
  
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
 * Names may not begin with "cmsg" (case insensitive).
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
 * Names may not begin with "cmsg" (case insensitive).
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
  int i, ok, valLen=0, textLen=0;
  void *array;
  char numbers[len][24];
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || vals == NULL) return(CMSG_BAD_ARGUMENT);
  if (place < 0 &&
      place != CMSG_CP_END &&
      place != CMSG_CP_MARKER)                     return(CMSG_BAD_ARGUMENT);
  if (!goodFieldName(name, isSystem))              return(CMSG_BAD_FORMAT);
  if (nameExists(vmsg, name))                      return(CMSG_ALREADY_EXISTS);
  if (type != CMSG_CP_FLT_A &&
      type != CMSG_CP_DBL_A)                       return(CMSG_BAD_ARGUMENT);
  
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
   
  /* length of string to contain all network data */
  textLen = strlen(name) +
            2 + /* 2 digit type */
            numDigits(item->count, 0) +
            valLen +
            len + 4; /* len+1 spaces, 2 newlines, 1 null terminator */

  item->text = (char *) calloc(1, textLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  sprintf(item->text, "%s %d %d\n", name, item->type, item->count);
  
  /* write numbers into text string */
  for (i=0; i<len; i++) {
    if (i < len-1) {
      sprintf(item->text + strlen(item->text), "%s ", numbers[i]);
    } else {
      sprintf(item->text + strlen(item->text), "%s\n", numbers[i]);
    }
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
static int addInt(void *vmsg, const char *name, int64_t val, int type, int place, int isSystem) {
  payloadItem *item;
  int ok, textLen=0;
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL)    return(CMSG_BAD_ARGUMENT);
  if (place < 0 &&
      place != CMSG_CP_END &&
      place != CMSG_CP_MARKER)        return(CMSG_BAD_ARGUMENT);
  if (!goodFieldName(name, isSystem)) return(CMSG_BAD_FORMAT);
  if (nameExists(vmsg, name))         return(CMSG_ALREADY_EXISTS);

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
   
  /* length of string to contain all network data */
  textLen = strlen(name) +
            2 + /* 2 digit type */
            numDigits(item->count, 0) +
            5; /* 2 spaces, 2 newlines, 1 null terminator */
  if (type == CMSG_CP_UINT64) {
    textLen += numDigits(val, 1);
  }
  else {
    textLen += numDigits(val, 0);
  }
  
  item->text = (char *) calloc(1, textLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  if (type == CMSG_CP_UINT64) {
    sprintf(item->text, "%s %d %d\n%llu\n", name, item->type, item->count, (uint64_t)val);
  }
  else {
    sprintf(item->text, "%s %d %d\n%lld\n", name, item->type, item->count, val);
  }

  item->length = strlen(item->text);
  item->val    = val;

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
 * This routine adds a named, 8-bit, signed int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive).
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
 * Names may not begin with "cmsg" (case insensitive).
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
 * Names may not begin with "cmsg" (case insensitive).
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
 * Names may not begin with "cmsg" (case insensitive).
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
 * Names may not begin with "cmsg" (case insensitive).
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
 * Names may not begin with "cmsg" (case insensitive).
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
 * Names may not begin with "cmsg" (case insensitive).
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
 * Names may not begin with "cmsg" (case insensitive).
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
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
static int addIntArray(void *vmsg, const char *name, const int *vals,
                       int type, int len, int place, int isSystem) {
  payloadItem *item;
  int i, ok, valLen=0, textLen=0;
  void *array=NULL;
  
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
   
  /* length of string to contain all network data */
  textLen = strlen(name) +
            2 + /* 2 digit type */
            numDigits(item->count, 0) +
            valLen +
            len + 4; /* len+1 spaces, 2 newlines, 1 null terminator */

  item->text = (char *) calloc(1, textLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  sprintf(item->text, "%s %d %d\n", name, item->type, item->count);
      
  switch (type) {
      case CMSG_CP_INT8_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(item->text + strlen(item->text), "%d ", ((int8_t *)vals)[i]);
              } else {
                sprintf(item->text + strlen(item->text), "%d\n", ((int8_t *)vals)[i]);
              }
            }
            /* Store the values */
            array = calloc(len, sizeof(int8_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++) ((int8_t *)array)[i] = ((int8_t *)vals)[i];
            break;
           
      case CMSG_CP_INT16_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(item->text + strlen(item->text), "%hd ", ((int16_t *)vals)[i]);
              } else {
                sprintf(item->text + strlen(item->text), "%hd\n", ((int16_t *)vals)[i]);
              }
            }
            /* Store the values */
            array = calloc(len, sizeof(int16_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++) ((int16_t *)array)[i] = ((int16_t *)vals)[i];
            break;
            
      case CMSG_CP_INT32_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(item->text + strlen(item->text), "%d ", ((int32_t *)vals)[i]);
              } else {
                sprintf(item->text + strlen(item->text), "%d\n", ((int32_t *)vals)[i]);
              }
            }
            /* Store the values */
            array = calloc(len, sizeof(int32_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++) ((int32_t *)array)[i] = ((int32_t *)vals)[i];
            break;
            
      case CMSG_CP_INT64_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(item->text + strlen(item->text), "%lld ", ((int64_t *)vals)[i]);
              } else {
                sprintf(item->text + strlen(item->text), "%lld\n", ((int64_t *)vals)[i]);
              }
            }
            /* Store the values */
            array = calloc(len, sizeof(int64_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++) ((int64_t *)array)[i] = ((int64_t *)vals)[i];
            break;
            
      case CMSG_CP_UINT8_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(item->text + strlen(item->text), "%d ", ((uint8_t *)vals)[i]);
              } else {
                sprintf(item->text + strlen(item->text), "%d\n", ((uint8_t *)vals)[i]);
              }
            }
            /* Store the values */
            array = calloc(len, sizeof(uint8_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++) ((uint8_t *)array)[i] = ((uint8_t *)vals)[i];
            break;
            
      case CMSG_CP_UINT16_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(item->text + strlen(item->text), "%hu ", ((uint16_t *)vals)[i]);
              } else {
                sprintf(item->text + strlen(item->text), "%hu\n", ((uint16_t *)vals)[i]);
              }
            }
            /* Store the values */
            array = calloc(len, sizeof(uint16_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++)((uint16_t *)array)[i] = ((uint16_t *)vals)[i];
            break;
            
      case CMSG_CP_UINT32_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(item->text + strlen(item->text), "%u ", ((uint32_t *)vals)[i]);
              } else {
                sprintf(item->text + strlen(item->text), "%u\n", ((uint32_t *)vals)[i]);
              }
            }
            /* Store the values */
            array = calloc(len, sizeof(uint32_t));
            if (array == NULL) {payloadItemFree(item); free(item); return(CMSG_OUT_OF_MEMORY);}
            for (i=0; i<len; i++) ((uint32_t *)array)[i] = ((uint32_t *)vals)[i];
            break;
            
      case CMSG_CP_UINT64_A:
            for (i=0; i<len; i++) {
              if (i < len-1) {
                sprintf(item->text + strlen(item->text), "%llu ", ((uint64_t *)vals)[i]);
              } else {
                sprintf(item->text + strlen(item->text), "%llu\n", ((uint64_t *)vals)[i]);
              }
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
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
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
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
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
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
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
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
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
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
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
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
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
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
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
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
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
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 *
 * @returns CMSG_OK if successful
 * @returns CMSG_ERROR if string cannot be placed at the desired location
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
static int addString(void *vmsg, const char *name, const char *val, int place, int isSystem) {
  payloadItem *item;
  int ok, textLen, valLen;
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);
  if (place < 0 &&
      place != CMSG_CP_END &&
      place != CMSG_CP_MARKER)                    return(CMSG_BAD_ARGUMENT);
  if (!goodFieldName(name, isSystem))             return(CMSG_BAD_FORMAT);
  if (nameExists(vmsg, name))                     return(CMSG_ALREADY_EXISTS);
  
  /* payload item */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);
  
  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  item->array = (void *)strdup(val);
  if (item->array == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type = CMSG_CP_STR;
  item->count = 1;
  
  /* Create string to hold all data to be transferred over
   * the network for this item. */
  
  valLen = strlen(val);
   
  /* length of string to contain all network data */
  textLen = strlen(name) +
            2 + /* 2 digit type */
            numDigits(item->count, 0) +
            numDigits(valLen, 0) +
            valLen + 
            6; /* 2 spaces, 3 newlines, 1 null terminator */
  item->text = (char *) calloc(1, textLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  sprintf(item->text, "%s %d %d\n%d\n%s\n", name, item->type, item->count, valLen, val);
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
 * Names may not begin with "cmsg" (case insensitive).
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
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddString(void *vmsg, const char *name, const char *val, int place) {
  return addString(vmsg, name, val, place, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named string array field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive).
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val strings to add
 * @param len number of strings to add
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
static int addStringArray(void *vmsg, const char *name, const char *vals[], int len, int place, int isSystem) {
  int i, ok, textLen=0, valLen=0;
  payloadItem *item;
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || vals == NULL) return(CMSG_BAD_ARGUMENT);
  if (place < 0 &&
      place != CMSG_CP_END &&
      place != CMSG_CP_MARKER)                     return(CMSG_BAD_ARGUMENT);
  if (!goodFieldName(name, isSystem))              return(CMSG_BAD_FORMAT);
  if (nameExists(vmsg, name))                      return(CMSG_ALREADY_EXISTS);
  
  /* payload item */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);
  
  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
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
  
  item->type  = CMSG_CP_STR_A;
  item->count = len;
  
  /* Create string to hold all data to be transferred over
   * the network for this item. */
   
  for (i=0; i<len; i++) {
    valLen += numDigits(strlen(vals[i]), 0) + strlen(vals[i]) + 2; /* length + string + 2 newlines */
  }
   
  /* length of string to contain all network data */
  textLen = strlen(name) +
            2 + /* 2 digit type */
            numDigits(item->count, 0) +
            valLen +
            5; /* 2 spaces, 1 newline, 1 null terminator */
  item->text = (char *) calloc(1, textLen);
  if (item->text == NULL) {
    payloadItemFree(item);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  sprintf(item->text, "%s %d %d\n", name, item->type, item->count);
  for (i=0; i<len; i++) {
    sprintf(item->text + strlen(item->text), "%d\n%s\n", strlen(vals[i]), vals[i]);
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
 * Names may not begin with "cmsg" (case insensitive).
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
 * @returns CMSG_BAD_ARGUMENT if message or name is NULL, or place < 0 and
 *                            place != CMSG_CP_END and place != CMSG_CP_MARKER
 * @returns CMSG_OUT_OF_MEMORY if no more memory
 * @returns CMSG_BAD_FORMAT if name is not properly formed
 * @returns CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddStringArray(void *vmsg, const char *name, const char *vals[], int len, int place) {
  return addStringArray(vmsg, name, vals, len, place, 0);
}



/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/






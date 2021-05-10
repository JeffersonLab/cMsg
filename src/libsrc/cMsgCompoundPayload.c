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
 * <b><H1>Introduction</H1><p>
 * 
 * This file defines the compound payload interface to cMsg messages. In short,
 * the payload allows the text field of the message to store messages of arbitrary
 * length and complexity. All types of ints (1,2,4,8 bytes), 4,8-byte floats,
 * strings, binary, whole messages and arrays of all these types can be stored
 * and retrieved from the compound payload. These methods are thread-safe.<p>
 *
 * Although XML would be a format well-suited to this task, cMsg should stand
 * alone - not requiring an XML parser to work. It takes more memory and time
 * to decode XML than a simple format. Thus, a simple, easy-to-parse format
 * was developed to implement this interface.<p>
 *
 * Following is the text format of a complete compound payload (where [nl] means
 * newline). Each payload consists of a number of items. The very first line is the
 * number of items in the payload. That is followed by the text representation of
 * each item. The first line of each item consists of 5 entries.<p>
 * 
 * Note that there is only 1 space or newline between all entries. The only exception
 * to the 1 space spacing is between the last two entries on each "header" line (the line
 * that contains the item_name). There may be several spaces between the last 2
 * entries on these lines.<p></b>
 *
 *<pre>    item_count[nl]</pre>
 *
 *<p><b><i>for (arrays of) string items:</i></b></p>
 *<pre>    item_name   item_type   item_count   isSystemItem?   item_length[nl]
 *    string_length_1[nl]
 *    string_characters_1[nl]
 *     ~
 *     ~
 *     ~
 *    string_length_N[nl]
 *    string_characters_N</pre>
 *
 *<p><b><i>for (arrays of) binary (converted into text) items:</i></b></p>
 *<pre>    item_name   item_type   item_count   isSystemItem?   item_length[nl]
 *    string_length_1   original_binary_byte_length_1   endian_1[nl]
 *    string_characters_1[nl]
 *     ~
 *     ~
 *     ~
 *    string_length_N   original_binary_byte_length_N   endian_N[nl]
 *    string_characters_N</pre>
 *
 *<p><b><i>for primitive type items:</i></b></p>
 *<pre>    item_name   item_type   item_count   isSystemItem?   item_length[nl]
 *    value_1   value_2   ...   value_N[nl]</pre>
 *
 *<p><b>A cMsg message is formatted as a compound payload. Each message has
 *   a number of fields (payload items).<br>
 *
 *  <i>for message items:</i></b></p>
 *<pre>                                                                            _
 *    item_name   item_type   item_count   isSystemItem?   item_length[nl]   /
 *    message_1_in_compound_payload_text_format[nl]                         &lt;  field_count[nl]
 *        ~                                                                  \ list_of_payload_format_items
 *        ~                                                                   -
 *        ~
 *    message_N_in_compound_payload_text_format[nl]</pre>
 *
 * <b>Notice that this format allows a message to store a message which stores a message
 * which stores a message, ad infinitum. In other words, recursive message storing.
 * The item_length in each case is the length in bytes of the rest of the item (not
 * including the newline at the end).</b>
 */

/* system includes */
#include <strings.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>

#include "cMsgPrivate.h"
#include "cMsgNetwork.h"

/** Maximum len in chars for a payload item name. */
#define CMSG_PAYLOAD_NAME_LEN 128

/*-------------------------------------------------------------------*/
/* prototypes of static functions */

/* Miscellaneous routines. */

static int  numDigits(int64_t i, int isUint64);
static void grabMutex(void);
static void releaseMutex(void);

/* These routines generally deal with payload. */

static int  isValidFieldName(const char *s, int isSystem);
static int  isValidSystemFieldName(const char *s);
static void setPayload(cMsgMessage_t *msg, int hasPayload);
static void payloadItemInit(payloadItem *item);
static void payloadItemFree(payloadItem *item, int freeData);
static payloadItem *copyPayloadItem(const payloadItem *from);
static void payloadPrintout(const void *msg, int level);
static int  createPayloadText(const cMsgMessage_t *msg, payloadItem **pItems,
                              int count, char **pTxt);

/* These routines move payload items in linked list. */

static void addItem(cMsgMessage_t *msg, payloadItem *item);
static int  removeItem(cMsgMessage_t *msg, const char *name, payloadItem **pitem);

/* These routines do the actual work of getting items from a payload. */ 

static int  getInt(const void *vmsg, const char *name, int type, int64_t *val);
static int  getReal(const void *vmsg, const char *name, int type, double *val);
static int  getArray(const void *vmsg, const char *name, int type,
                     const void **vals, int *len);

/* These routines do the actual work of adding items to a payload. */ 

static int  addMessage(void *vmsg, const char *name, const void *vmessage,
                       int isSystem);
static int  addMessageArray(void *vmsg, const char *name, const void *vmessage[],
                            int number, int isSystem);
                            
static int  addBinary(void *vmsg, const char *name, const char *src, int size,
                      int isSystem, int endian);
static int addBinaryArray(void *vmsg, const char *name, const char *src[],
                          int number, const int size[], int isSystem,
                          const int endian[]);
                      
static int  addInt(void *vmsg, const char *name, int64_t val, int type, int isSystem);
static int  addIntArray(void *vmsg, const char *name, const int *vals,
                        int type, int len, int isSystem, int copy);
static int  addInt32(void *vmsg, const char *name, int32_t val, int isSystem);
static int  createIntArrayItem(const char *name, const int *vals, int type,
                               int len, int isSystem, int copy, payloadItem **newItem);

static int  addString(void *vmsg, const char *name, const char *val, int isSystem, int copy);
static int  addStringArray(void *vmsg, const char *name, const char **vals,
                           int len, int isSystem, int copy);
static int  createStringArrayItem(const char *name, const char **vals, int len,
                                  int isSystem, int copy, payloadItem **pItem);

static int  addReal(void *vmsg, const char *name, double val, int type, int isSystem);
static int  addRealArray(void *vmsg, const char *name, const double *vals,
                         int type, int len, int isSystem, int copy);
                         
/* These routines parse text coming over the wire and
 * reconstruct payload items as well as set the system
 * fields of messages. 
 */
static int  setFieldsFromText(void *vmsg, const char *text, int flag, const char **ptr);
 
static int addBinaryFromText      (void *vmsg, char *name, int type, int count, int isSystem,
                                   const char *pVal, const char *pText, int textLen,
                                   int noHeaderLen);
static int addBinaryArrayFromText (void *vmsg, char *name, int type, int count, int isSystem,
                                   const char *pVal, const char *pText, int textLen,
                                   int noHeaderLen);
static int addIntFromText         (void *vmsg, char *name, int type, int count, int isSystem,
                                   const char *pVal, const char *pText, int textLen,
                                   int noHeaderLen);
static int addIntArrayFromText    (void *vmsg, char *name, int type, int count, int isSystem,
                                   const char *pVal, const char *pText, int textLen,
                                   int noHeaderLen);
static int addRealFromText        (void *vmsg, char *name, int type, int count, int isSystem,
                                   const char *pVal, const char *pText, int textLen,
                                   int noHeaderLen);
static int addRealArrayFromText   (void *vmsg, char *name, int type, int count, int isSystem,
                                   const char *pVal, const char *pText, int textLen,
                                   int noHeaderLen);
static int addStringFromText      (void *vmsg, char *name, int type, int count, int isSystem,
                                   const char *pVal, const char *pText, int textLen,
                                   int noHeaderLen);
static int addStringArrayFromText (void *vmsg, char *name, int type, int count, int isSystem,
                                   const char *pVal, const char *pText, int textLen,
                                   int noHeaderLen);
static int addMessagesFromText     (void *vmsg, const char *name, int type, int count, int isSystem,
                                    void *vmessages, const char *pText, int textLen,
                                    int noHeaderLen);

 /** Map the value of a byte (first index) to the hex string value it represents. */
static char toASCII[256][3] =
{"00", "01", "02", "03", "04", "05", "06", "07", "08", "09", "0a", "0b", "0c", "0d", "0e", "0f",
 "10", "11", "12", "13", "14", "15", "16", "17", "18", "19", "1a", "1b", "1c", "1d", "1e", "1f",
 "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "2a", "2b", "2c", "2d", "2e", "2f",
 "30", "31", "32", "33", "34", "35", "36", "37", "38", "39", "3a", "3b", "3c", "3d", "3e", "3f",
 "40", "41", "42", "43", "44", "45", "46", "47", "48", "49", "4a", "4b", "4c", "4d", "4e", "4f",
 "50", "51", "52", "53", "54", "55", "56", "57", "58", "59", "5a", "5b", "5c", "5d", "5e", "5f",
 "60", "61", "62", "63", "64", "65", "66", "67", "68", "69", "6a", "6b", "6c", "6d", "6e", "6f",
 "70", "71", "72", "73", "74", "75", "76", "77", "78", "79", "7a", "7b", "7c", "7d", "7e", "7f",
 "80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "8a", "8b", "8c", "8d", "8e", "8f",
 "90", "91", "92", "93", "94", "95", "96", "97", "98", "99", "9a", "9b", "9c", "9d", "9e", "9f",
 "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7", "a8", "a9", "aa", "ab", "ac", "ad", "ae", "af",
 "b0", "b1", "b2", "b3", "b4", "b5", "b6", "b7", "b8", "b9", "ba", "bb", "bc", "bd", "be", "bf",
 "c0", "c1", "c2", "c3", "c4", "c5", "c6", "c7", "c8", "c9", "ca", "cb", "cc", "cd", "ce", "cf",
 "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "d8", "d9", "da", "db", "dc", "dd", "de", "df",
 "e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7", "e8", "e9", "ea", "eb", "ec", "ed", "ee", "ef",
 "f0", "f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "f9", "fa", "fb", "fc", "fd", "fe", "ff"};
 
 /**
  * Map the value of an ascii character (index) to the numerical
  * value it represents. The only characters of interest are 0-9,a-f
  * for converting hex strings back to numbers.
  */
static int toByte[103] =
{  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /*  0-9  */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /* 10-19 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /* 20-29 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /* 30-39 */
   -1, -1, -1, -1, -1, -1, -1, -1,  0,  1,  /* 40-49 */
    2,  3,  4,  5,  6,  7,  8,  9, -1, -1,  /* 50-59 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /* 60-69 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /* 70-79 */
   -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  /* 80-89 */
   -2, -1, -1, -1, -1, -1, -1, 10, 11, 12,  /* 90-99, Z maps to 90 */
   13, 14, 15}; /* 100-102 */
  
/*-------------------------------------------------------------------*/

/** Union defined to help in skirting optimization problems with gcc. */
typedef union u1 {
   float f;
   uint32_t i;
} intFloatUnion;

/** Union defined to help in skirting optimization problems with gcc. */
typedef union u2 {
   double d;
   uint64_t i;
} intDoubleUnion;

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
 * This routine returns the number of digits in an integer
 * including a minus sign.
 *
 * @param number integer
 * @param isUint64 is number an unsigned 64 bit integer (0=no)
 * @return number of digits in the integer argument including a minus sign
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

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a unique string representation of a int in the form
 * of 8 hex chars. String points to allocated memory which needs to be freed
 * by caller to avoid a memory leak.
 *
 * @param i int value to convert
 * @return string of hex characters rep of 32 bit unsigned int, needs to be
 *         freed by caller
 */   
char *cMsgIntChars(uint32_t i) {
    int byte;    
    char *s = (char *)malloc(9);
    if (s == NULL) return NULL;
      
    byte = i>>24 & 0xff;
    s[0] = toASCII[byte][0];
    s[1] = toASCII[byte][1];
    byte = i>>16 & 0xff;
    s[2] = toASCII[byte][0];
    s[3] = toASCII[byte][1];
    byte = i>>8 & 0xff;
    s[4] = toASCII[byte][0];
    s[5] = toASCII[byte][1];
    byte = i & 0xff;
    s[6] = toASCII[byte][0];
    s[7] = toASCII[byte][1];
    s[8] = '\0';
    return s;
}
    
/**
 * This routine returns a string representation of a float in the form
 * of 8 hex chars of the IEEE754 representation. String points to 
 * internal static character array.
 *
 * @param f float value to convert
 * @return string of hex characters rep of IEEE765 bytes
 */   
char *cMsgFloatChars(float f) {
    int byte;
    uint32_t j32;
    intFloatUnion floater;
    static char flt[9];
      
    floater.f = f;
    j32 = floater.i;
    byte = j32>>24 & 0xff;
    flt[0] = toASCII[byte][0];
    flt[1] = toASCII[byte][1];
    byte = j32>>16 & 0xff;
    flt[2] = toASCII[byte][0];
    flt[3] = toASCII[byte][1];
    byte = j32>>8 & 0xff;
    flt[4] = toASCII[byte][0];
    flt[5] = toASCII[byte][1];
    byte = j32 & 0xff;
    flt[6] = toASCII[byte][0];
    flt[7] = toASCII[byte][1];
    flt[8] = '\0';
    return flt;
}
    
/**
 * This routine returns a string representation of a double in the form
 * of 16 hex chars of the IEEE754 representation. String points to 
 * internal static character array.
 *
 * @param d double value to convert
 * @return string of hex characters rep of IEEE765 bytes
 */   
char *cMsgDoubleChars(double d) {

    uint64_t byte, j64;
    intDoubleUnion doubler;
    static char dbl[17];
    
    doubler.d = d;
    j64 = doubler.i;
    byte = j64>>56 & 0xffL;
    dbl[0] = toASCII[byte][0];
    dbl[1] = toASCII[byte][1];
    byte = j64>>48 & 0xffL;
    dbl[2] = toASCII[byte][0];
    dbl[3] = toASCII[byte][1];
    byte = j64>>40 & 0xffL;
    dbl[4] = toASCII[byte][0];
    dbl[5] = toASCII[byte][1];
    byte = j64>>32 & 0xffL;
    dbl[6] = toASCII[byte][0];
    dbl[7] = toASCII[byte][1];
    
    byte = j64>>24 & 0xffL;
    dbl[8] = toASCII[byte][0];
    dbl[9] = toASCII[byte][1];
    byte = j64>>16 & 0xffL;
    dbl[10] = toASCII[byte][0];
    dbl[11] = toASCII[byte][1];
    byte = j64>>8 & 0xffL;
    dbl[12] = toASCII[byte][0];
    dbl[13] = toASCII[byte][1];
    byte = j64 & 0xffL;
    dbl[14] = toASCII[byte][0];
    dbl[15] = toASCII[byte][1];
    dbl[16] = '\0';
    return dbl;
}


/*-------------------------------------------------------------------*/


/**
 * This routine checks a string to see if it is a valid field name.
 * It returns 0 if it is not, or a 1 if it is. A check is made to see if
 * it contains an unprintable character or any character from a list of
 * excluded characters. All names starting with "cmsg", independent of case,
 * are reserved for use by the cMsg system itself. Names may not be
 * longer than CMSG_PAYLOAD_NAME_LEN characters (including null terminator).
 *
 * @param s string to check
 * @param isSystem if true, allows names starting with "cmsg", else not
 *
 * @return 1 if string is OK
 * @return 0 if string contains illegal characters
 */   
static int isValidFieldName(const char *s, int isSystem) {

  int i, len;

  if (s == NULL) return(0);
  len = (int)strlen(s);

  /* check for printable character */
  for (i=0; i<len; i++) {
    if (isprint((int)s[i]) == 0) return(0);
  }

  /* check for excluded chars */
  if (strpbrk(s, excludedChars) != NULL) return(0);
  
  /* check for length */
  if (strlen(s) > CMSG_PAYLOAD_NAME_LEN) return(0);
  
  /* check for starting with cmsg  */
  if (!isSystem) {
    if (strncasecmp(s, "cmsg", 4) == 0) return(0);
  }
  
  /* string ok */
  return(1);
}


/*-------------------------------------------------------------------*/


/**
 * This routine checks a string to see if it is a valid system field name.
 * It returns 0 if it is not, or a 1 if it is. A check is made to see if
 * it contains an unprintable character or any character from a list of
 * excluded characters. All names starting with "cmsg", independent of case,
 * are valid system names. Names may not be longer than CMSG_PAYLOAD_NAME_LEN
 * characters (including null terminator).
 *
 * @param s string to check
 *
 * @return 1 if string is OK
 * @return 0 if string contains illegal characters
 */   
static int isValidSystemFieldName(const char *s) {

  int i, len;

  if (s == NULL) return(0);
  len = (int)strlen(s);

  /* check for printable character */
  for (i=0; i<len; i++) {
    if (isprint((int)s[i]) == 0) return(0);
  }

  /* check for excluded chars */
  if (strpbrk(s, excludedChars) != NULL) return(0);
  
  /* check for length */
  if (strlen(s) > CMSG_PAYLOAD_NAME_LEN) return(0);
  
  /* check for starting with cmsg  */
  if (strncasecmp(s, "cmsg", 4) == 0) return(1);
  
  /* string not system name */
  return(0);
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
  item->count       = 1;
  item->length      = 0;
  item->noHeaderLen = 0;
  item->size        = 0;
  item->endian      = CMSG_ENDIAN_BIG;
  item->endians = NULL;
  item->sizes   = NULL;
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
 * @param freeData if true, free data held in payload item
 */
static void payloadItemFree(payloadItem *item, int freeData) {
  if (item == NULL) return;

  if (item->text != NULL) {free(item->text);  item->text  = NULL;}
  if (item->name != NULL) {free(item->name);  item->name  = NULL;}
 
  if (!freeData || item->array == NULL) return;

  if (item->type == CMSG_CP_STR_A) {
      /* with string array, first free individual strings, then array */
      int i;
      for (i=0; i<item->count; i++) {
          free( ((char **)(item->array))[i] );
      }
      free(item->array);
      item->array = NULL;
  }
  else if (item->type == CMSG_CP_BIN_A) {
      int i;
      char **myArray = (char **)item->array;

      for (i=0; i<item->count; i++) {
          free(myArray[i]);
      }
      free(myArray);
      if (item->sizes   != NULL) free(item->sizes);
      if (item->endians != NULL) free(item->endians);
      item->array = NULL;
  }
  else if (item->type == CMSG_CP_MSG) {
      cMsgFreeMessage_r(&item->array); item->array = NULL;
  }
  else if (item->type == CMSG_CP_MSG_A) {
      int i;
      void **myArray = (void **)item->array;

      for (i=0; i<item->count; i++) {
        cMsgFreeMessage_r(&myArray[i]);
      }
      free(myArray);
      item->array = NULL;
  }
  else {
      free(item->array);
      item->array = NULL;
  }
}


/*-------------------------------------------------------------------*/


/**
 * This routine resets the payload to its initial condition (no payload),
 * but in a way which avoids mutex deadlock when recursively freeing a
 * payload's cMsgMessage items (ie. doesn't call {@link grabMutex()}).
 *
 * @param vmsg pointer to message
 */   
void cMsgPayloadReset_r(void *vmsg) {
  payloadItem *item, *next;
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || msg->payload == NULL)  {
    releaseMutex();
    return;
  }

  item = msg->payload;
  while (item != NULL) {
    next = item->next;
    payloadItemFree(item, 1);
    free(item);
    item = next;
  }
  msg->payload = NULL;
  
  if (msg->payloadText != NULL) {
      free(msg->payloadText);
      msg->payloadText = NULL;
  }
  
  msg->payloadCount = 0;
  
  /* write that we no longer have a payload */
  setPayload(msg, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine resets the payload to its initial condition (no payload).
 * It frees the allocated memory of the given message's entire payload
 * and then initializes the payload components of the message. 
 *
 * @param vmsg pointer to message
 */   
void cMsgPayloadReset(void *vmsg) {
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
    payloadItemFree(item, 1);
    free(item);
    item = next;
  }
  msg->payload = NULL;
  
  if (msg->payloadText != NULL) {
      free(msg->payloadText);
      msg->payloadText = NULL;
  }
  
  msg->payloadCount = 0;
  
  /* write that we no longer have a payload */
  setPayload(msg, 0);
  
  releaseMutex();
}


/*-------------------------------------------------------------------*/


/**
 * This routine removes all the user-added items in the given message's payload.
 * The payload may still contain fields added by the cMsg system.
 * If there are no items left in the payload, this routine is equivalent to
 * {@link cMsgPayloadReset}. 
 *
 * @param vmsg pointer to message
 */   

void cMsgPayloadClear(void *vmsg) {
    int sysFieldCount=0, firstFound=0;
    payloadItem *item, *next, *first, *lastSysItem=NULL;
    cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

    grabMutex();

    if (msg == NULL || msg->payload == NULL) {
        releaseMutex();
        return;
    }
 
    first = item = msg->payload;
    while (item != NULL) {
        next = item->next;
        if (isValidSystemFieldName(item->name)) {
            if (!firstFound) {
                first = item;
                firstFound++;
            }
            else {
                lastSysItem->next = item;
            }
            lastSysItem = item;   
            item = next;
            sysFieldCount++;
            continue;
        }
        payloadItemFree(item, 1);
        free(item);
        item = next;
    }
    
    /* if payload is truly empty, init things */
    if (sysFieldCount == 0) {
        msg->payloadCount = 0;
        msg->payload = NULL;
        if (msg->payloadText != NULL) {
            free(msg->payloadText);
            msg->payloadText = NULL;
        }
        /* write that we no longer have a payload */
        setPayload(msg, 0);
    }
    /* else recalculate the text representation of payload */
    else {
        msg->payloadCount = sysFieldCount;
        msg->payload = first;
        cMsgPayloadUpdateText(vmsg);
    }
 
    releaseMutex();
}


/*-------------------------------------------------------------------*/


/**
 * This routine sets the "has-a-compound-payload" field of a message.
 * This is only called while mutex is grabbed.
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
 * It returns 0 if there is no payload and the number of items in the
 * payload is there is one.
 *
 * @param vmsg pointer to message
 * @param hasPayload pointer which gets filled with the number of items
 *                   if msg has compound payload, else 0
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgHasPayload(const void *vmsg, int *hasPayload) {  
  return cMsgPayloadGetCount(vmsg, hasPayload);
}


/*-------------------------------------------------------------------*/


/**
 * This routine returns the number of payload items a message has. 
 *
 * @param vmsg pointer to message
 * @param count pointer which gets filled with the number of payload
 *                      items (0 for no payload)
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if either arg is NULL
 */   
int cMsgPayloadGetCount(const void *vmsg, int *count) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || count == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex(); 
  *count = msg->payloadCount;
  releaseMutex();

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine checks to see if a name is already in use
 * by an existing field in the payload. 
 *
 * @param vmsg pointer to message
 * @param name name to check
 *
 * @return 0 if name does not exist or there is no payload
 * @return 1 if name exists
 */   
int cMsgPayloadContainsName(const void *vmsg, const char *name) {  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  payloadItem *item;
  
  if (msg == NULL || name == NULL) {
    return(0);
  }

  grabMutex();
  
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
 * This routine returns the type of data associated with the payload
 * field given by the name argument. The returned type may have the
 * following values:
 * <UL>
 * <LI>CMSG_CP_STR          for a   String
 * <LI>CMSG_CP_FLT          for a   4 byte float
 * <LI>CMSG_CP_DBL          for an  8 byte float
 * <LI>CMSG_CP_INT8         for an  8 bit int
 * <LI>CMSG_CP_INT16        for a  16 bit int
 * <LI>CMSG_CP_INT32        for a  32 bit int
 * <LI>CMSG_CP_INT64        for a  64 bit int
 * <LI>CMSG_CP_UINT8        for an unsigned  8 bit int
 * <LI>CMSG_CP_UINT16       for an unsigned 16 bit int
 * <LI>CMSG_CP_UINT32       for an unsigned 32 bit int
 * <LI>CMSG_CP_UINT64       for an unsigned 64 bit int
 * <LI>CMSG_CP_MSG          for a  cMsg message
 * <LI>CMSG_CP_BIN          for    binary

 * <LI>CMSG_CP_STR_A        for a   String array
 * <LI>CMSG_CP_FLT_A        for a   4 byte float array
 * <LI>CMSG_CP_DBL_A        for an  8 byte float array
 * <LI>CMSG_CP_INT8_A       for an  8 bit int array
 * <LI>CMSG_CP_INT16_A      for a  16 bit int array
 * <LI>CMSG_CP_INT32_A      for a  32 bit int array
 * <LI>CMSG_CP_INT64_A      for a  64 bit int array
 * <LI>CMSG_CP_UINT8_A      for an unsigned  8 bit int array
 * <LI>CMSG_CP_UINT16_A     for an unsigned 16 bit int array
 * <LI>CMSG_CP_UINT32_A     for an unsigned 32 bit int array
 * <LI>CMSG_CP_UINT64_A     for an unsigned 64 bit int array
 * <LI>CMSG_CP_MSG_A        for a  cMsg message array
 * <LI>CMSG_CP_BIN_A        for an array of binary items
 * </UL>
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param type pointer to int gets filled with type of data associated with field
 *             given by name
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no field of the given name is found
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgPayloadGetType(const void *vmsg, const char *name, int *type) {  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  payloadItem *item;
  
  if (msg == NULL || msg->payload == NULL || type == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }

  grabMutex();
    
  item = msg->payload;
  while (item != NULL) {
    if (strcmp(item->name, name) == 0) {
      *type = item->type;
      releaseMutex();
      return(CMSG_OK);
    }
    item = item->next;
  }
 
  releaseMutex();
  return(CMSG_ERROR);
}


/*-------------------------------------------------------------------*/


/**
 * This routine fills 2 arrays provided by the caller. One contains all
 * the names of the items in the payload, and the second contains the
 * corresponding data types of those items. Each element of the array
 * of characters points to a string in the message itself which must
 * not be freed or written to. The difference between this routine and
 * {@link cMsgPayloadGetInfo} is that this routine allocates no memory
 * so nothing needs to be freed.
 *
 * @param vmsg pointer to message
 * @param names pointer which gets filled with the array of names in a payload
 * @param types pointer to an array of ints which gets filled with type of data
 *              associated with each field name in "names"
 * @param len length of each of the given arrays, if arrays are different lengths
 *             give the smallest of the lengths
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload is found
 * @return CMSG_BAD_ARGUMENT if any arg is NULL or of improper value
 * @return CMSG_LIMIT_EXCEEDED if len < the number of items, but len number of valid values
 *                             are still returned in names and types
 */   
int cMsgPayloadGet(const void *vmsg, char **names, int *types, int len) {  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  payloadItem *item;
  int count=0, err=CMSG_OK;
  
  if (msg == NULL || names == NULL || types == NULL || len < 1) {
    return(CMSG_BAD_ARGUMENT);
  }

  if (msg->payload == NULL) {
    return(CMSG_ERROR);
  }

  grabMutex();
    
  /* Warn caller, not enough room to place all the items */
  if (msg->payloadCount > len) err = CMSG_LIMIT_EXCEEDED;
    
  item = msg->payload;
  while ((item != NULL) && (len > 0)) {
    names[count]   = item->name;
    types[count++] = item->type;
    item = item->next;
    len--;
  }
   
  releaseMutex();
  return(err);
}


/*-------------------------------------------------------------------*/


/**
 * This routine returns 2 arrays. One contains all the names of the items
 * in the payload, and the second contains the corresponding data types
 * of those items. It also returns the length of both arrays. Both arrays
 * use allocated memory and must be freed by the caller. Each element of
 * the array of characters points to a string in the message itself which
 * must not be freed or written to. The difference between this routine and
 * {@link cMsgPayloadGet} is that the other routine allocates no memory
 * so nothing needs to be freed.
 *
 * @param vmsg pointer to message
 * @param names pointer which gets filled with the array of names in a payload
 * @param types pointer to an array of ints which gets filled with type of data
 *              associated with each field name in "names"
 * @param len pointer to int which gives the length of the returned arrays
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload is found
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 * @return CMSG_OUT_OF_MEMORY if out of memory
 */   
int cMsgPayloadGetInfo(const void *vmsg, char ***names, int **types, int *len) {  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  payloadItem *item;
  char **_names;
  int count=0, *_types;
  
  if (msg == NULL || names == NULL || types == NULL || len == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  if (msg->payload == NULL) {
    return(CMSG_ERROR);
  }

  grabMutex();
  
  /* first find out how many items we have */
  item = msg->payload;
  while (item != NULL) {
    count++;
    item = item->next;
  }
  
  /* allocate some memory */
  _names = (char **) malloc(count*sizeof(char *));
  if (_names == NULL) {
    releaseMutex();
    return(CMSG_OUT_OF_MEMORY);
  }
  
  _types = (int *) malloc(count*sizeof(int));
  if (_types == NULL) {
    releaseMutex();
    free(_names);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  count = 0;
  item = msg->payload;
  while (item != NULL) {
    _names[count]   = item->name;
    _types[count++] = item->type;
    item = item->next;
  }
 
  *names = _names;
  *types = _types;
  *len   = count;
  
  releaseMutex();
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine removes the named field if it exists.
 *
 * @param vmsg pointer to message
 * @param name name of field to remove
 *
 * @return 1 if successful
 * @return 0 if no field with that name was found
 */   
int cMsgPayloadRemove(void *vmsg, const char *name) {
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL) return(0);
  
  return removeItem(msg, name, NULL);
}


/*-------------------------------------------------------------------*/


/**
 * This routine takes a given payload item and places it into an
 * existing message's payload at the front of the linked list.
 *
 * @param msg message handle
 * @param item payload item to add
 */   
static void addItem(cMsgMessage_t *msg, payloadItem *item) {
  payloadItem *next=NULL;

  /* placing payload item in msg's linked list requires mutex protection */
  grabMutex();
  
  /* put it in front since that's fastest */
  if (msg->payload == NULL) {
    msg->payload = item;
  }
  else {
    next = msg->payload;
    msg->payload = item;
    item->next = next;
  }
  
  /* increment payload item count */
  msg->payloadCount++;
  
  /* store in msg struct that this msg has a compound payload */
  setPayload(msg, 1);
  
  /* update text representation of msg */
  cMsgPayloadUpdateText((const void *)msg);
 
  releaseMutex();
  return;
}


/*-------------------------------------------------------------------*/


/**
 * This routine finds a given payload item and removes it from an existing
 * message's payload.
 * Error checking of arguments done by calling routines.
 *
 * @param msg message handle
 * @param name name of payload item to remove
 * @param pItem if not NULL, it is filled with removed item, else the removed item is freed
 *
 * @return 1 if successful
 * @return 0 if item doesn't exist
 */   
static int removeItem(cMsgMessage_t *msg, const char *name, payloadItem **pItem) {
  int spot=0;
  payloadItem *item, *prev=NULL, *next;
  
  grabMutex();
  
  if (msg->payload == NULL) {
    releaseMutex();
    return(0);
  }

  /* find "name" if there is such an item */
  item = msg->payload;

  while (item != NULL) {
    if (strcmp(item->name, name) == 0) {
      /* remove first item ... */
      if (spot == 0) {
        next = item->next;
        msg->payload = next;
      }
      /* remove from elsewhere ... */
      else {
        next = item->next;
        prev->next = next;
      }

      /* free allocated memory or return item */
      if (pItem == NULL) {
        payloadItemFree(item, 1);
        free(item);
      } else {
        *pItem = item;
      }
      
      /* decrement payload item count */
      msg->payloadCount--;

      break;
    }

    spot++;
    prev = item;
    item = item->next;
  }
  
  /* store in msg struct that this msg does NOT have a compound payload anymore */
  if (msg->payload == NULL) setPayload(msg, 0);
  /* update text representation of msg */
  cMsgPayloadUpdateText((const void *)msg);
 
  releaseMutex();
  return(1);
}

/*-------------------------------------------------------------------*/
/* users do not need access to this */
/*-------------------------------------------------------------------*/


/**
 * This routine updates the text representation of a message's payload.
 * This routine is used internally and does not need to be called by
 * the cMsg user.
 *
 * @param vmsg pointer to message
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if arg is NULL
 * @return CMSG_OUT_OF_MEMORY if out of memory
 */
int cMsgPayloadUpdateText(const void *vmsg) {
  int err;
  char *txt;
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (vmsg == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }
  
  /* This routine is only called by routines that already have the mutex locked,
   * so don't bother with grabbing and releasing mutex. */ 
  
  err = createPayloadText(vmsg, NULL, 0, &txt);
  if (err != CMSG_OK) return err;
  
  /* clear out old payload text */
  if (msg->payloadText != NULL) {
      free(msg->payloadText);
  }
  
  msg->payloadText = txt;
  return(CMSG_OK);    
}

/*-------------------------------------------------------------------*/

/**
 * This routine returns a text representation of a message's payload -
 * including payload items given as an argument. If there already are
 * items of those names in the payload, those existing items are omitted
 * (those given in the arg list are used instead).
 *
 * @param msg message handle
 * @param pItems array of pointers to payload items to be included in text rep of msg payload
 * @param count number of items in pItems array
 * @param pTxt pointer that gets filled in with generated payload text (memory allocated)
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if arg is NULL
 * @return CMSG_OUT_OF_MEMORY if out of memory
 */
static int createPayloadText(const cMsgMessage_t *msg, payloadItem **pItems, int count, char **pTxt)
{
    char *s, *pBuf;
    int i, len, skip, gotMsg=1, gotItems=1, totalCount=0;
    size_t totalLen=0;
    payloadItem *item;
  
    if (msg == NULL || msg->payload == NULL) {
        gotMsg = 0;
    }
  
    if (pItems == NULL) {
        gotItems = 0;
        count    = 0;
    }

    /* nothing to make text out of */
    if (!gotMsg && !gotItems) {
        *pTxt = NULL;
        return(CMSG_OK);
    }
      
    /* add each msg payload item's length to total */
    if (gotMsg) {
        item = msg->payload;
        while (item != NULL) {
            /* ignore duplicates (eg history items) */
            skip = 0;
            if (gotItems) {
                for (i=0; i<count; i++) {
/*printf("createPayloadText: compare in len (%s) to adding (%s)\n", item->name, pItems[i]->name);*/
                    if (strcmp(item->name, pItems[i]->name) == 0) {
/*printf("                 : skipping existing payload item in len (%s)\n", item->name);*/
                        skip++;
                        break;
                    }
                }
            }
            
            if (!skip) {
                totalLen += item->length;
                totalCount++;
            }
            
            item = item->next;
        }
    }
  
    /* include additional payload item's length in total */
    if (gotItems) {
        for (i=0; i<count; i++) {
            totalLen += pItems[i]->length;
            totalCount++;
        }
    }
  
    /*totalCount = msg->payloadCount + count;*/
    totalLen += numDigits(totalCount, 0) + 1; /* send count & newline first */
    totalLen += 1; /* for null terminator */

    /* allocate memory for string */
    pBuf = s = (char *) malloc(totalLen);
    if (s == NULL) {
        return(CMSG_OUT_OF_MEMORY);
    }
    s[totalLen-1] = '\0';
  
    /* first item is number of fields to come (count) & newline */
    sprintf(s, "%d\n%n", totalCount, &len);
    s += len;
    
    /* add message payload fields */
    if (gotMsg) {
        item = msg->payload;
        while (item != NULL) {
            /* ignore duplicates (eg history items) */
            skip = 0;
            if (gotItems) {
                for (i=0; i<count; i++) {
/*printf("createPayloadText: compare in text (%s) to adding (%s)\n", item->name, pItems[i]->name);*/
                    if (strcmp(item->name, pItems[i]->name) == 0) {
/*printf("createPayloadText: skipping existing payload item in text (%s)\n", item->name);*/
                        skip++;
                        break;
                    }
                }
            }
            
            if (!skip) {
                sprintf(s, "%s%n", item->text, &len);
                s += len;
            }
            
            item = item->next;
        }
    }
  
    /* add extra fields */
    if (gotItems) {
        for (i=0; i<count; i++) {
/*printf("createPayloadText: adding payload item in text (%s)\n", pItems[i]->name);*/
            sprintf(s, "%s%n", pItems[i]->text, &len);
            s += len;
        }
    }
        
    *pTxt = pBuf;
  
    return(CMSG_OK);
}

/*-------------------------------------------------------------------*/

/**
 * Adds arguments to the history of senders, senderHosts, and senderTimes
 * of this message (in the payload).
 * This method only keeps cMsgMessage_t.historyLengthMax number of
 * the most recent names.
 * This method is reserved for system use only.<p>
 * When a client sends the same message over and over again, we do <b>NOT</b> want the history
 * to change. To ensure this, we follow a simple principle: the sender history needs to go
 * into the sent message (ie. over the wire), but must not be added to the local one.
 * Thus, if I send a message, its local sender history will not change.
 *
 * @param vmsg pointer to message
 * @param name name of sender to add to the history of senders
 * @param host name of sender host to add to the history of hosts
 * @param time sender time to add to the history of times
 * @param pTxt pointer filled with text representation of payload with the history items added
 *             (memory is allocated)
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if error in internal details of existing history payload items
 * @return CMSG_OUT_OF_MEMORY if out of memory
 */
int cMsgAddHistoryToPayloadText(void *vmsg, char *name, char *host,
                                int64_t time, char **pTxt) {

    cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
    payloadItem *newItems[3];
    int err, len, len1, len2, len3, i, index, exists=1;
    const char **nameArray, **hostArray;
    const int64_t *timeArray;
    const int64_t times[] = { time };
    const char *names[]   = { name };
    const char *hosts[]   = { host };
    int64_t *newTimes=NULL;
    const char **newNames=NULL, **newHosts=NULL;

    *pTxt = NULL;
        
    /* first add creator if necessary */
    if (!cMsgPayloadContainsName(vmsg, "cMsgCreator")) {
        err = addString(vmsg, "cMsgCreator", name, 1, 1);
        if (err != CMSG_OK) {
            return(err); /* only possible error is out-of-mem */
        }
    }

    /* set max history length if it isn't set and is NOT the default */
    if (!cMsgPayloadContainsName(vmsg, "cMsgHistoryLengthMax") &&
         msg->historyLengthMax != CMSG_HISTORY_LENGTH_MAX) {
        err = addInt32(vmsg, "cMsgHistoryLengthMax", msg->historyLengthMax, 1);
        if (err != CMSG_OK) {
            return(err);
        }
    }

    /* if set not to record history, just return */
    if (msg->historyLengthMax < 1) {
        if (msg->payloadText != NULL) {
            *pTxt = strdup(msg->payloadText);
        }
        return(CMSG_OK);
    }

    /* is there a history already? */
    cMsgGetInt64Array(vmsg, "cMsgSenderTimeHistory", &timeArray, &len1);
    cMsgGetStringArray(vmsg, "cMsgSenderHostHistory", &hostArray, &len2);
    err = cMsgGetStringArray(vmsg, "cMsgSenderNameHistory", &nameArray, &len3);
    if (err == CMSG_ERROR) {
        exists = 0;
    }
    else if (err != CMSG_OK) {
        return(err);
    }
    else if (len1 != len2 || len1 != len3) {
        return(CMSG_ERROR);
    }
    len = len1;

    if (!exists) {
/*printf("cMsgAddHistoryToPayloadText: no history exists!\n");*/
        err = createStringArrayItem("cMsgSenderNameHistory", names, 1, 1, 0, &newItems[0]);
        if (err != CMSG_OK) {return(err);}

        err = createStringArrayItem("cMsgSenderHostHistory", hosts, 1, 1, 0, &newItems[1]);
        if (err != CMSG_OK) {
            payloadItemFree(newItems[0], 0);
            free(newItems[0]);
            return(err);
        }

        err = createIntArrayItem("cMsgSenderTimeHistory", (int *)times,
                                 CMSG_CP_INT64_A, 1, 1, 0, &newItems[2]);
        if (err != CMSG_OK) {
            payloadItemFree(newItems[0], 0);
            payloadItemFree(newItems[1], 0);
            free(newItems[0]);
            free(newItems[1]);
            return(err);
        }
    }
    else {
/*printf("cMsgAddHistoryToPayloadText: history exists so add to it!\n");*/
        /* keep only historyLength number of the latest names */
        index = 0;
        if (len1 >= msg->historyLengthMax) {
            len = msg->historyLengthMax - 1;
            index = len1 - len;
        }

        /* create space for list of names */
        newNames = (const char **) calloc(1, (len+1)*sizeof(char *));
        if (newNames == NULL) {
            return(CMSG_OUT_OF_MEMORY);
        }

        /* create space for list of hosts */
        newHosts = (const char **) calloc(1, (len+1)*sizeof(char *));
        if (newHosts == NULL) {
            free(newNames);
            return(CMSG_OUT_OF_MEMORY);
        }

        /* create space for list of times */
        newTimes = (int64_t *) calloc(1, (len+1)*sizeof(int64_t));
        if (newTimes == NULL) {
            free(newNames); free(newHosts);
            return(CMSG_OUT_OF_MEMORY);
        }

        /* copy over old names/hosts/times */
        for (i=index; i<len+index; i++) {
            newNames[i-index] = nameArray[i];
            newHosts[i-index] = hostArray[i];
            newTimes[i-index] = timeArray[i];
        }

        /* add new name/host/time to end (don't copy strings) */
        newTimes[len] = time;
        newNames[len] = name;
        newHosts[len] = host;

        /* create payload new items, but don't copy data in, just copy pointer */
        err = createStringArrayItem("cMsgSenderNameHistory", newNames, len+1, 1, 0, &newItems[0]);
        if (err != CMSG_OK) {
            free(newNames); free(newHosts); free(newTimes);
            return(err);
        }

        err = createStringArrayItem("cMsgSenderHostHistory", newHosts, len+1, 1, 0, &newItems[1]);
        if (err != CMSG_OK) {
            free(newNames); free(newHosts); free(newTimes);
            /* free payload item but not data it contains */
            payloadItemFree(newItems[0], 0);
            free(newItems[0]);
            return(err);
        }

        err = createIntArrayItem("cMsgSenderTimeHistory", (const int *)newTimes,
                                 CMSG_CP_INT64_A, len+1, 1, 0, &newItems[2]);
        if (err != CMSG_OK) {
            free(newNames); free(newHosts); free(newTimes);
            /* free payload item but not data it contains */
            payloadItemFree(newItems[0], 0);
            payloadItemFree(newItems[1], 0);
            free(newItems[0]);
            free(newItems[1]);
            return(err);
        }
    }

    /* create a string from these 3 items plus existing payload items (mem allocated) */
    err = createPayloadText(msg, newItems, 3, pTxt);

    /* free up memory */
    payloadItemFree(newItems[0], 0);
    payloadItemFree(newItems[1], 0);
    payloadItemFree(newItems[2], 0);
    free(newItems[0]);
    free(newItems[1]);
    free(newItems[2]);
    if (newTimes != NULL) free(newTimes);
    if (newNames != NULL) free(newNames);
    if (newHosts != NULL) free(newHosts);

    return(err);
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
 * @return CMSG_OK             if successful
 * @return CMSG_OUT_OF_MEMORY  if out of memory
 * @return CMSG_BAD_ARGUMENT   if the msg and/or text argument is NULL
 * @return CMSG_ALREADY_EXISTS if text contains name that is being used already
 * @return CMSG_BAD_FORMAT     if the text is in the wrong format or contains values
 *                              that don't make sense
 */   
static int setFieldsFromText(void *vmsg, const char *text, int flag, const char **ptr) {
  const char *t, *pmsgTxt;
  char *s, name[CMSG_PAYLOAD_NAME_LEN+1];
  int i, j, err, type, count, fields, ignore;
  int noHeaderLen, isSystem, msgTxtLen, debug=0, headerLen;
  
  /* payloadItem *item, *prev; */
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || text == NULL) return(CMSG_BAD_ARGUMENT);
  
  t = text;
  /* place pointer before next line feed */
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
  cMsgPayloadReset(vmsg);
  
  for (i=0; i<fields; i++) {
    /* read line */
    memset(name, 0, CMSG_PAYLOAD_NAME_LEN+1);
    sscanf(t, "%s%d%d%d%d%n", name, &type, &count, &isSystem, &noHeaderLen, &headerLen);
    
if(debug) printf("FIELD #%d, name = %s, type = %d, count = %d, isSys = %d, noHeadLen = %d, t = %p\n",
                 i, name, type, count, isSystem, noHeaderLen, t);
    
    /* store to get the text so it doesn't need to be regenerated */
    pmsgTxt = t;
    msgTxtLen = headerLen + 1 + noHeaderLen; /* 1 newline */
    
    if (strlen(name) < 1 || count < 1 || noHeaderLen < 1 ||
        type < CMSG_CP_STR || type > CMSG_CP_BIN_A) return(CMSG_BAD_FORMAT);
    
    /* ignore certain fields (by convention, system fields start with "cmsg") */
    /* isSystem = strncasecmp(name, "cmsg", 4) == 0 ? 1 : 0; */
    ignore = isSystem;               /* by default ignore a system field, for flag == 1 */
    if (flag == 0) ignore = !ignore; /* only set system fields, for flag = 0*/
    else if (flag == 2) ignore = 0;  /* deal with all fields, for flag = 2 */
    
    /* skip over fields to be ignored */
    if (ignore) {
      for (j=0; j<count; j++) {
        /* skip over field */
        t += msgTxtLen;
        s = strpbrk(t, "\n");
        if (s == NULL && i != fields-1) return(CMSG_BAD_FORMAT);
if(debug) printf("  skipped field\n");
      }
      continue;
    }
    
    /* move pointer past header to beginning of value part */
    t = s+1;
    
    /* string */
    if (type == CMSG_CP_STR) {
      err = addStringFromText(vmsg, name, type, count, isSystem,
                              t, pmsgTxt, msgTxtLen, noHeaderLen);
    }
    
    /* string array */
    else if (type == CMSG_CP_STR_A) {
      err = addStringArrayFromText(vmsg, name, type, count, isSystem,
                                   t, pmsgTxt, msgTxtLen, noHeaderLen);
    }
    
    /* binary data */
    else if (type == CMSG_CP_BIN) {
      err = addBinaryFromText(vmsg, name, type, count, isSystem,
                              t, pmsgTxt, msgTxtLen, noHeaderLen);
    }
        
    /* array of binary data */
    else if (type == CMSG_CP_BIN_A) {
        err = addBinaryArrayFromText(vmsg, name, type, count, isSystem,
                                     t, pmsgTxt, msgTxtLen, noHeaderLen);
    }
        
    /* double or float */
    else if (type == CMSG_CP_DBL || type == CMSG_CP_FLT) {
        err = addRealFromText(vmsg, name, type, count, isSystem,
                              t, pmsgTxt, msgTxtLen, noHeaderLen);
    }

    /* double or float array */
    else if (type == CMSG_CP_DBL_A || type == CMSG_CP_FLT_A) {          
      err = addRealArrayFromText(vmsg, name, type, count, isSystem,
                                 t, pmsgTxt, msgTxtLen, noHeaderLen);
    }

    /* all ints */
    else if (type == CMSG_CP_INT8   || type == CMSG_CP_INT16  ||
             type == CMSG_CP_INT32  || type == CMSG_CP_INT64  ||
             type == CMSG_CP_UINT8  || type == CMSG_CP_UINT16 ||
             type == CMSG_CP_UINT32 || type == CMSG_CP_UINT64)  {
      
      err = addIntFromText(vmsg, name, type, count, isSystem,
                           t, pmsgTxt, msgTxtLen, noHeaderLen);
    }
            
    /* all int arrays */
    else if (type == CMSG_CP_INT8_A   || type == CMSG_CP_INT16_A  ||
             type == CMSG_CP_INT32_A  || type == CMSG_CP_INT64_A  ||
             type == CMSG_CP_UINT8_A  || type == CMSG_CP_UINT16_A ||
             type == CMSG_CP_UINT32_A || type == CMSG_CP_UINT64_A)  {
      err = addIntArrayFromText(vmsg, name, type, count, isSystem,
                                t, pmsgTxt, msgTxtLen, noHeaderLen);
    }
    
    /* cMsg message */
    else if (type == CMSG_CP_MSG) {
      const char *endptr;
      void *newMsg;
      
      /* create a single message */
      newMsg = cMsgCreateMessage();
      if (newMsg == NULL) return(CMSG_OUT_OF_MEMORY);

      /* recursive call to setFieldsFromText to fill msg's fields */
      err = setFieldsFromText(newMsg, t, CMSG_BOTH_FIELDS, &endptr);
      if (err != CMSG_OK) {
/*printf("err setting fields for msg in payload\n");*/
          return(CMSG_BAD_FORMAT);
      }
      if (endptr == t) {
        /* nothing more to parse */
      }

if(debug) {
  /* read only "len" # of chars */
  char txt[msgTxtLen+1];
  memcpy(txt, pmsgTxt, (size_t)msgTxtLen);
  txt[msgTxtLen] = '\0';
  printf("msg as string =\n%s\nlength = %d, t = %p\n", txt, (int)strlen(txt), t);
  cMsgPayloadPrint(newMsg);
}
      err = addMessagesFromText(vmsg, name, type, count, isSystem,
                                newMsg, pmsgTxt, msgTxtLen, noHeaderLen);
    }
    
    /* cMsg message array */
    else if (type == CMSG_CP_MSG_A) {
      const char *endptr, *ptext;      
      void **myArray;
      
      myArray = (void **)malloc(count*sizeof(void *));
      if (myArray == NULL) return(CMSG_OUT_OF_MEMORY);
      
      endptr = t;

      for (j=0; j<count; j++) {
          /* beginning pointer */
          ptext = endptr;

          /* create a single message */
          myArray[j] = cMsgCreateMessage();
          if (myArray[j] == NULL) return(CMSG_OUT_OF_MEMORY);

          /* recursive call to setFieldsFromText to fill msg's fields */
          err = setFieldsFromText(myArray[j], ptext, CMSG_BOTH_FIELDS, &endptr);
          if (err != CMSG_OK) {
/*printf("err setting fields for msg in payload\n");*/
              return(CMSG_BAD_FORMAT);
          }
          if (endptr == t) {
              /* nothing more to parse */
          }
      }
      
if(debug) {
    /* read only "len" # of chars */
    char txt[msgTxtLen+1];
    memcpy(txt, pmsgTxt,(size_t) msgTxtLen);
    txt[msgTxtLen] = '\0';
    printf("msg as string =\n%s\nlength = %d, t = %p\n", txt, (int)strlen(txt), t);
    for (j=0; j<count; j++) {
      printf("\nMsg[%d]:\n", j);
      cMsgPayloadPrint(myArray[j]);
    }
}
      
      err = addMessagesFromText(vmsg, name, type, count, isSystem,
                                (void *)myArray, pmsgTxt, msgTxtLen,
                                noHeaderLen);
    }

    else {
      return(CMSG_BAD_FORMAT);
    }
            
    if (err != CMSG_OK) {
      return(err);
    }

    /* go to the next line */
    t += noHeaderLen;
    s = strpbrk(t, "\n");
    /* s will be null if it's the very last item */
    if (s == NULL && i != fields-1) return(CMSG_BAD_FORMAT);
      
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
 * @return pointer to copy of payload item if successful
 * @return NULL if argument is null or memory cannot be allocated
 */   
static payloadItem *copyPayloadItem(const payloadItem *from) {
  int i, j, size, count;
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
    payloadItemFree(item, 1);
    free(item);
    return(NULL);
  }
  
  item->length        = from->length;
  item->noHeaderLen   = from->noHeaderLen;
  item->type          = from->type;
  item->endian        = from->endian;
  size  = item->size  = from->size;
  count = item->count = from->count;

  /* item->next is not set here. That doesn't make sense since
   * item will be in a different linked list */
  
  /* set the value */
  switch (from->type) {
    case CMSG_CP_STR:
      item->array = (void *)strdup((char *)from->array);
      if (item->array == NULL) {payloadItemFree(item, 1); free(item); return(NULL);}
      break;
    case CMSG_CP_STR_A:
      item->array = malloc(count*sizeof(char *));
      if (item->array == NULL) {payloadItemFree(item, 1); free(item); return(NULL);}
      /* copy all strings */
      for (i=0; i < count; i++) {
          s = strdup( ((char **)from->array)[i] );
          /* being lazy here, should free strings allocated - possible mem leak */
          if (s == NULL) {payloadItemFree(item, 1); free(item); return(NULL);}
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
      
      
    case CMSG_CP_UINT8_A:
    case CMSG_CP_INT8_A:
      item->array = malloc(count*sizeof(int8_t));
      if (item->array == NULL) {payloadItemFree(item, 1); free(item); return(NULL);}
      memcpy(item->array, (const void *)from->array, count*sizeof(int8_t));
      break;
    case CMSG_CP_UINT16_A:
    case CMSG_CP_INT16_A:
      item->array = malloc(count*sizeof(int16_t));
      if (item->array == NULL) {payloadItemFree(item, 1); free(item); return(NULL);}
      memcpy(item->array, (const void *)from->array, count*sizeof(int16_t));
      break;
    case CMSG_CP_UINT32_A:
    case CMSG_CP_INT32_A:
      item->array = malloc(count*sizeof(int32_t));
      if (item->array == NULL) {payloadItemFree(item, 1); free(item); return(NULL);}
      memcpy(item->array, (const void *)from->array, count*sizeof(int32_t));
      break;
    case CMSG_CP_UINT64_A:
    case CMSG_CP_INT64_A:
      item->array = malloc(count*sizeof(int64_t));
      if (item->array == NULL) {payloadItemFree(item, 1); free(item); return(NULL);}
      memcpy(item->array, (const void *)from->array, count*sizeof(int64_t));
      break;
    case CMSG_CP_FLT_A:
      item->array = malloc(count*sizeof(float));
      if (item->array == NULL) {payloadItemFree(item, 1); free(item); return(NULL);}
      memcpy(item->array, (const void *)from->array, count*sizeof(float));
      break;
    case CMSG_CP_DBL_A:
      item->array = malloc(count*sizeof(double));
      if (item->array == NULL) {payloadItemFree(item, 1); free(item); return(NULL);}
      memcpy(item->array, (const void *)from->array, count*sizeof(double));
      break;
      
      
    case CMSG_CP_BIN:
      if (from->array == NULL) break;
      item->array = malloc((size_t)size);
      if (item->array == NULL) {payloadItemFree(item, 1); free(item); return(NULL);}
      item->array = memcpy(item->array, from->array, (size_t)size);
      break;
    case CMSG_CP_BIN_A:
      if (from->array == NULL) break;
      item->array   =        malloc(from->count*sizeof(char *));
      item->sizes   = (int *)malloc(from->count*sizeof(int));
      item->endians = (int *)malloc(from->count*sizeof(int));
      if (item->array == NULL || item->sizes == NULL || item->endians == NULL) {
          payloadItemFree(item, 1); free(item); return(NULL);
      }
      memcpy((void *)item->sizes,   (void *)from->sizes,   from->count*sizeof(int));
      memcpy((void *)item->endians, (void *)from->endians, from->count*sizeof(int));
      for (i=0; i < count; i++) {
          s = (char *)malloc((size_t)item->sizes[i]);
          if (s == NULL) {payloadItemFree(item, 1); free(item); return(NULL);}
          memcpy((void *)s, (void *)((char **)from->array)[i], (size_t)item->sizes[i]);
          ((char **)(item->array))[i] = s;
      }
      break;
      
      
    case CMSG_CP_MSG:
      if (from->array == NULL) break;
      item->array = cMsgCopyMessage(from->array);
      if (item->array == NULL) {payloadItemFree(item, 1); free(item); return(NULL);}
      break;
    case CMSG_CP_MSG_A:
      if (from->array == NULL) break;
      {
        void **msgs, **msgArray;
        msgArray = (void **) malloc(count*sizeof(void *));
        if (msgArray == NULL) {
          return(NULL);
        }
        item->array = (void *)msgArray;
        msgs = (void **)from->array;
        
        for (i=0; i<count; i++) {
          msgArray[i] = cMsgCopyMessage(msgs[i]);
          if (msgArray[i] == NULL) {
            for (j=0; j<i; j++) {
              cMsgFreeMessage(&msgArray[j]);
            }
            free(msgArray);
            return(NULL);
          }
        }
      }
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
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if either argument is null
 * @return CMSG_OUT_OF_MEMORY if operating system out of memory
 */   
int cMsgPayloadCopy(const void *vmsgFrom, void *vmsgTo) {
  
  payloadItem *item, *next, *copiedItem, *prevCopied=NULL, *firstCopied=NULL;
  cMsgMessage_t *msgFrom = (cMsgMessage_t *)vmsgFrom;
  cMsgMessage_t *msgTo   = (cMsgMessage_t *)vmsgTo;
  char **toArray, **fromArray;
  int64_t *toIntArray, *fromIntArray;
  int i, first=1, err, index, len;
  int lenFrom, lenTo = msgTo->historyLengthMax;
 
  if (msgTo == NULL || msgFrom == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }

  /*grabMutex();*/  /* this routine is used recursively, so no mutex grabbing */
  
  if (msgFrom->payload == NULL) {
    if (msgFrom->payloadText != NULL) {
        free(msgFrom->payloadText);
        msgFrom->payloadText = NULL;
    }
    
    /* inline version of  cMsgPayloadWipeout(vmsgTo) */
    item = msgTo->payload;
    while (item != NULL) {
      next = item->next;
      payloadItemFree(item, 1);
      free(item);
      item = next;
    }
    msgTo->payload = NULL;
    
    if (msgTo->payloadText != NULL) {
        free(msgTo->payloadText);
        msgTo->payloadText = NULL;
    }
    msgTo->payloadCount = 0;
    /* write that we no longer have a payload */
    setPayload(msgTo, 0);
    /* releaseMutex(); */
    return(CMSG_OK);
  }
  
  /* copy linked list of payload items one-by-one */
  item = msgFrom->payload;
  while (item != NULL) {
    /* If the item is a system (hidden) field that stores history info,
     * only copy over at most msgTo->historyLengthMax elements of the array. */
    if ((strcmp(item->name, "cMsgSenderNameHistory") == 0 ||
         strcmp(item->name, "cMsgSenderHostHistory") == 0)  &&
        (item->count > msgTo->historyLengthMax)) {
        
        index = 0;
        len = lenFrom = item->count;
        if (lenFrom > lenTo) {
            len   = lenTo;
            index = lenFrom - len;
        }
        
        fromArray = (char **) item->array;
        toArray   = (char **) calloc(1, len*sizeof(char *));
        if (toArray == NULL) {
            /*releaseMutex();*/
            payloadItemFree(firstCopied, 1);
            return(CMSG_OUT_OF_MEMORY);          
        }
        
        for (i=index; i<len+index; i++) {
            toArray[i-index] = fromArray[i];
        }
        /* changed from copy to assume-ownership) */
        err = createStringArrayItem(item->name, (const char **)toArray, len, 1, 0, &copiedItem);
        if (err != CMSG_OK) {
            /*releaseMutex();*/
            payloadItemFree(firstCopied, 1);
            return(CMSG_OUT_OF_MEMORY);                      
        }
    }
    else if ((strcmp(item->name, "cMsgSenderTimeHistory") == 0) &&
             (item->count > msgTo->historyLengthMax)) {
        
        index = 0;
        len = lenFrom = item->count;
        if (lenFrom > lenTo) {
            len   = lenTo;
            index = lenFrom - len;
        }
        
        fromIntArray = (int64_t *) item->array;
        toIntArray   = (int64_t *) calloc(1, len*sizeof(int64_t));
        if (toIntArray == NULL) {
            /*releaseMutex();*/
            payloadItemFree(firstCopied, 1);
            return(CMSG_OUT_OF_MEMORY);
        }
        
        for (i=index; i<len+index; i++) {
            toIntArray[i-index] = fromIntArray[i];
        }
        
        err = createIntArrayItem(item->name, (const int *)toIntArray,
                                 CMSG_CP_INT64_A, len, 1, 0, &copiedItem);
        if (err != CMSG_OK) {
            /*releaseMutex();*/
            payloadItemFree(firstCopied, 1);
            return(CMSG_OUT_OF_MEMORY);
        }
     }
     else {
        copiedItem = copyPayloadItem(item);       
        if (copiedItem == NULL) {
            /*releaseMutex();*/
            payloadItemFree(firstCopied, 1);
            return(CMSG_OUT_OF_MEMORY);
        }
    }
    
    if (first) {
        firstCopied = prevCopied = copiedItem;
        first = 0;
    }
    else {
        prevCopied->next = copiedItem;
        prevCopied = copiedItem;
    }
    
    item = item->next;
  }
  
  /* Clear msgTo's list and replace it with the copied list.
   * (inline version of  cMsgPayloadWipeout(vmsgTo)) */
  item = msgTo->payload;
  while (item != NULL) {
    next = item->next;
    payloadItemFree(item, 1);
    free(item);
    item = next;
  }
  
  if (msgTo->payloadText != NULL) {
      free(msgTo->payloadText);
      msgTo->payloadText = NULL;
  } 
  msgTo->payload = firstCopied;
  
  /* copy over text representation */
  if (msgFrom->payloadText != NULL)
      msgTo->payloadText = strdup(msgFrom->payloadText);
  
  /* don't forget to copy the record of the number of payload items */
  msgTo->payloadCount = msgFrom->payloadCount;
  
  /* write that we have a payload */
  setPayload(msgTo, 1);
  
  /*releaseMutex();*/
  return(CMSG_OK);
}
  
/*-------------------------------------------------------------------*/

/**
 * This routine returns a description of the given field name.
 * Do NOT write to this location in memory.
 *
 * @param vmsg pointer to message
 * @param name name of field to describe
 *
 * @return NULL if no payload exists
 * @return field name if field exists
 */   
const char *cMsgPayloadFieldDescription(const void *vmsg, const char *name) {
  static char s[64];
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  int type=0;

  if (msg == NULL || name == NULL) return(NULL);
  
  cMsgPayloadGetType(vmsg, name, &type);
  
  switch (type) {
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
        sprintf(s, "string array");
        break;
    case CMSG_CP_INT8_A:
        sprintf(s, "8 bit int array");
        break;
    case CMSG_CP_INT16_A:
        sprintf(s, "16 bit int array");
        break;
    case CMSG_CP_INT32_A:
        sprintf(s, "32 bit int array");
        break;
    case CMSG_CP_INT64_A:
        sprintf(s, "64 bit int array");
        break;
    case CMSG_CP_UINT8_A:
        sprintf(s, "8 bit unsigned int array");
        break;
    case CMSG_CP_UINT16_A:
        sprintf(s, "16 bit unsigned int array");
        break;
    case CMSG_CP_UINT32_A:
        sprintf(s, "32 bit unsigned int array");
        break;
    case CMSG_CP_UINT64_A:
        sprintf(s, "64 bit unsigned int array");
        break;
    case CMSG_CP_FLT_A:
        sprintf(s, "32 bit float array");
        break;
    case CMSG_CP_DBL_A:
        sprintf(s, "64 bit double array");
        break;
    case CMSG_CP_MSG:
        sprintf(s, "cMsg message");
        break;
    case CMSG_CP_MSG_A:
        sprintf(s, "cMsg message array");
        break;
    case CMSG_CP_BIN:
        sprintf(s, "binary data (byte array)");
        break;
    case CMSG_CP_BIN_A:
        sprintf(s, "binary data array (array of byte arrays)");
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
 * @param vmsg pointer to message
 */
void cMsgPayloadPrint(const void *vmsg) {
  payloadPrintout(vmsg,0);  
}

  
/*-------------------------------------------------------------------*/

/**
 * This routine prints out the message payload in a readable form.
 *
 * @param msg pointer to message
 */
static void payloadPrintout(const void *msg, int level) {
  int ok, j, k, len, *types, namesLen=0;
  char *indent, *name, **names;
  
  /* create the indent since a message may contain a message, etc. */
  if (level < 1) {
    indent = "";
  }
  else {
    indent = (char *)malloc((size_t)(level*5+1));
    for (j=0; j<level*5; j++) { /* indent by 5 spaces for each level */
      indent[j] = '\040';       /* ASCII space = char #32 (40 octal) */
    }
    indent[level*5] = '\0';
  }

  /* get all name & type info */
  ok = cMsgPayloadGetInfo(msg, &names, &types, &namesLen);
  if (ok != CMSG_OK) {
    if (level > 0) free(indent); return;
  }
  
  for (k=0; k<namesLen; k++) {
          
    name = names[k];
    printf("%sFIELD %s", indent, name);
    
    switch (types[k]) {
      case CMSG_CP_INT8:
        {int8_t i;   ok=cMsgGetInt8(msg, name, &i);   if(ok==CMSG_OK) printf(" (int8): %d\n", i);}       break;
      case CMSG_CP_INT16:
        {int16_t i;  ok=cMsgGetInt16(msg, name, &i);  if(ok==CMSG_OK) printf(" (int16): %hd\n", i);}     break;
      case CMSG_CP_INT32:
        {int32_t i;  ok=cMsgGetInt32(msg, name, &i);  if(ok==CMSG_OK) printf(" (int32): %d\n", i);}      break;
      case CMSG_CP_INT64:
        {int64_t i;  ok=cMsgGetInt64(msg, name, &i);  if(ok==CMSG_OK) printf(" (int64): %lld\n", i);}    break;
      case CMSG_CP_UINT8:
        {uint8_t i;  ok=cMsgGetUint8(msg, name, &i);  if(ok==CMSG_OK) printf(" (uint8): %u\n", i);}      break;
      case CMSG_CP_UINT16:
        {uint16_t i; ok=cMsgGetUint16(msg, name, &i); if(ok==CMSG_OK) printf(" (uint16): %hu\n", i);}    break;
      case CMSG_CP_UINT32:
        {uint32_t i; ok=cMsgGetUint32(msg, name, &i); if(ok==CMSG_OK) printf(" (uint32): %u\n", i);}     break;
      case CMSG_CP_UINT64:
        {uint64_t i; ok=cMsgGetUint64(msg, name, &i); if(ok==CMSG_OK) printf(" (uint64): %llu\n", i);}   break;
      case CMSG_CP_DBL:
        {double d;   ok=cMsgGetDouble(msg, name, &d); if(ok==CMSG_OK) printf(" (double): %.17g\n", d);} break;
      case CMSG_CP_FLT:
        {float f;    ok=cMsgGetFloat(msg, name, &f);  if(ok==CMSG_OK) printf(" (float): %.8g\n", f);}    break;
      case CMSG_CP_STR:
        {const char *s; ok=cMsgGetString(msg, name, &s); if(ok==CMSG_OK) printf(" (string): %s\n", s);}     break;
      case CMSG_CP_INT8_A:
        {const int8_t *i; ok=cMsgGetInt8Array(msg, name, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  int8[%d] = %d\n", indent, j,i[j]);} break;
      case CMSG_CP_INT16_A:
        {const int16_t *i; ok=cMsgGetInt16Array(msg, name, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  int16[%d] = %hd\n", indent, j,i[j]);} break;
      case CMSG_CP_INT32_A:
        {const int32_t *i; ok=cMsgGetInt32Array(msg, name, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  int32[%d] = %d\n", indent, j,i[j]);} break;
      case CMSG_CP_INT64_A:
        {const int64_t *i; ok=cMsgGetInt64Array(msg, name, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  int64[%d] = %lld\n", indent, j,i[j]);} break;
      case CMSG_CP_UINT8_A:
        {const uint8_t *i; ok=cMsgGetUint8Array(msg, name, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  uint8[%d] = %u\n", indent, j,i[j]);} break;
      case CMSG_CP_UINT16_A:
        {const uint16_t *i; ok=cMsgGetUint16Array(msg, name, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  uint16[%d] = %hu\n", indent, j,i[j]);} break;
      case CMSG_CP_UINT32_A:
        {const uint32_t *i; ok=cMsgGetUint32Array(msg, name, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  uint8[%d] = %u\n", indent, j,i[j]);} break;
      case CMSG_CP_UINT64_A:
        {const uint64_t *i; ok=cMsgGetUint64Array(msg, name, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  uint64[%d] = %llu\n", indent, j,i[j]);} break;
      case CMSG_CP_DBL_A:
        {const double *i; ok=cMsgGetDoubleArray(msg, name, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  double[%d] = %.17g\n", indent, j,i[j]);} break;
      case CMSG_CP_FLT_A:
        {const float *i; ok=cMsgGetFloatArray(msg, name, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  float[%d] = %.8g\n", indent, j,i[j]);} break;
      case CMSG_CP_STR_A:
        {const char **i; ok=cMsgGetStringArray(msg, name, &i, &len); if(ok!=CMSG_OK) break; printf(":\n");
         for(j=0; j<len;j++) printf("%s  string[%d] = %s\n", indent, j,i[j]);} break;
         
      case CMSG_CP_BIN:
        {const char *b; char *enc; size_t sb; unsigned int se; int sz, end;
        ok=cMsgGetBinary(msg, name, &b, &sz, &end); if(ok!=CMSG_OK) break;
        /* only print up to 1kB */
        sb = (size_t)sz; if (sb > 1024) {sb = 1024;}
        se = cMsg_b64_encode_len(b, (unsigned int)sb, 1);
        enc = (char *)malloc(se+1); if (enc == NULL) break;
        enc[se] = '\0';
        cMsg_b64_encode(b, (unsigned int)sb, enc, 1);
        if (end == CMSG_ENDIAN_BIG) printf(" (binary, big endian):\n%s%s\n", indent, enc);
        else printf(" (binary, little endian):\n%s%s\n", indent, enc);
        if (sz > sb) {printf("%s... %u bytes more binary not printed here ...\n", indent, (uint32_t)(sz-sb));}
        free(enc);
        } break;
        
      case CMSG_CP_BIN_A:
        {const char **b; char *enc; size_t sb; unsigned int se; int *szs, *ends, cnt, i;
        ok=cMsgGetBinaryArray(msg, name, &b, &szs, &ends, &cnt); if(ok!=CMSG_OK) break;
        printf(" (binary arrays):\n");
        /* only print up to 1kB for each array */
        for (i=0; i<cnt; i++) {
          sb = (size_t)szs[i]; if (sb > 1024) {sb = 1024;}
          se = cMsg_b64_encode_len(b[i], (unsigned int)sb, 1);
          enc = (char *)malloc(se+1); if (enc == NULL) break;
          enc[se] = '\0';
          cMsg_b64_encode(b[i], (unsigned int)sb, enc, 1);
          if (ends[i] == CMSG_ENDIAN_BIG) printf("%s  (array #%d, big endian):\n%s    %s\n",
                                                 indent, i, indent, enc);
          else printf("%s  (array #%d, little endian):\n%s    %s\n", indent, i, indent, enc);
          if (szs[i] > sb)
            {printf("%s... %u bytes more binary not printed here ...\n", indent, (uint32_t)(szs[i]-sb));}
          free(enc);
        }
        } break;
        
      case CMSG_CP_MSG:
        {const void *v; ok=cMsgGetMessage(msg, name, &v); if(ok!=CMSG_OK) break;
         printf(" (cMsg message):\n");
         payloadPrintout(v, level+1);
        } break;
        
      case CMSG_CP_MSG_A:
        {const void **v; ok=cMsgGetMessageArray(msg, name, &v, &len); if(ok!=CMSG_OK) break;
          printf(":\n");
          for (j=0; j<len; j++) {
           printf("%s  message[%d] =\n", indent, j);
           payloadPrintout(v[j], level+1);
         }
        } break;
        
      default:
        printf("\n");
    }
  }
  
  free(names);
  free(types);
  if (level > 0) free(indent);
    
  return;  
}


/*-------------------------------------------------------------------*/


/**
 * This routine returns a pointer to the string representation of the given field.
 * Do NOT write to this location in memory.
 *
 * @param vmsg pointer to message
 * @param name name of field
 * @param val pointer to pointer which is set to string representation of field
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgPayloadGetFieldText(const void *vmsg, const char *name, const char **val) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }

  grabMutex();
  
  item = msg->payload;
  while (item != NULL) {
    if (strcmp(item->name, name) == 0) {
      releaseMutex();
      *val = item->text;
      return(CMSG_OK);
    }
    item = item->next;
  }
  
  releaseMutex();
  return(CMSG_ERROR);
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
 * @return NULL if no payload exists or no memory
 */   
int cMsgPayloadSetFromText(void *vmsg, const char *text) {
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
 * @return NULL if no payload exists or no memory
 */   
int cMsgPayloadSetSystemFieldsFromText(void *vmsg, const char *text) {
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
 * @return NULL if no payload exists or no memory
 */   
int cMsgPayloadSetAllFieldsFromText(void *vmsg, const char *text) {
  return setFieldsFromText(vmsg, text, 2, NULL);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the user pointer of the given field.
 * Used to implement C++ interface to compound payload.
 *
 * @param vmsg pointer to message
 * @param name name of payload item
 * @param p pointer that gets filled with user pointer
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgPayloadGetFieldPointer(const void *vmsg, const char *name, void **p) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || name == NULL || p == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }

  grabMutex();

  item = msg->payload;
  while (item != NULL) {
    if (strcmp(item->name, name) == 0) {
      break;
    }
    item = item->next;
  }
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }

  *p = item->pointer;
  
  releaseMutex();
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine sets the user pointer of the given field.
 * Used to implement C++ interface to compound payload.
 *
 * @param vmsg pointer to message
 * @param name name of payload item
 * @param p user pointer value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgPayloadSetFieldPointer(const void *vmsg, const char *name, void *p) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  if (msg == NULL || name == NULL || p == NULL) {
    return(CMSG_BAD_ARGUMENT);
  }

  grabMutex();

  item = msg->payload;
  while (item != NULL) {
    if (strcmp(item->name, name) == 0) {
      break;
    }
    item = item->next;
  }
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }

  item->pointer = p;
  
  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the value of the given field as binary
 * (byte array) if it exists.
 * Do NOT write into the returned pointer's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param val pointer filled with field value
 * @param size pointer filled with number of bytes in binary array
 * @param endian pointer filled with endian of data (CMSG_ENDIAN_BIG/LITTLE)
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetBinary(const void *vmsg, const char *name, const char **val,
                  int *size, int *endian) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL ||
      val == NULL || size == NULL || endian == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  item = msg->payload;
  while (item != NULL) {
    if (strcmp(item->name, name) == 0) {
      break;
    }
    item = item->next;
  }
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  
  if (item->type != CMSG_CP_BIN || item->size < 1 || item->array == NULL) {
    releaseMutex();
    return(CMSG_BAD_FORMAT);
  }
  
  *val = (char *)item->array;
  *size = item->size;
  *endian = item->endian;
  
  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the value of the given field as an array of
 * binary data (array of byte arrays) if it exists.
 * Do NOT write into the returned pointer's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param vals pointer filled with array of byte arrays
 * @param sizes pointer filled with array of number of bytes in byte arrays
 * @param endians pointer filled with array of endian of data in byte arrays
 *               (CMSG_ENDIAN_BIG/LITTLE)
 * @param count pointer filled with number of element in each returned array
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */
int cMsgGetBinaryArray(const void *vmsg, const char *name, const char ***vals,
                       int **sizes, int **endians, int *count) {
    payloadItem *item;
    cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

    if (msg == NULL || name == NULL || vals == NULL ||
        count == NULL || sizes == NULL || endians == NULL) return(CMSG_BAD_ARGUMENT);
  
    grabMutex();
  
    item = msg->payload;
    while (item != NULL) {
        if (strcmp(item->name, name) == 0) {
            break;
        }
        item = item->next;
    }
  
    if (item == NULL) {
        releaseMutex();
        return(CMSG_ERROR);
    }
  
    if (item->type != CMSG_CP_BIN_A || item->count < 1 || item->array == NULL) {
        releaseMutex();
        return(CMSG_BAD_FORMAT);
    }
  
    *vals = (const char **)item->array;
    *count = item->count;
    *sizes = item->sizes;
    *endians = item->endians;

    releaseMutex();
    return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the value of the given field as a cMsg message if it exists.
 * Do NOT write into the returned pointer's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param val pointer filled with field value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetMessage(const void *vmsg, const char *name, const void **val) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  item = msg->payload;
  while (item != NULL) {
    if (strcmp(item->name, name) == 0) {
      break;
    }
    item = item->next;
  }
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  
  if (item->type != CMSG_CP_MSG || item->count != 1 || item->array == NULL) {
    releaseMutex();
    return(CMSG_BAD_FORMAT);
  }
    
  *val = item->array;
  
  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the value of the given field as an array of
 * cMsg messages if it exists.
 * Do NOT write into the returned pointer's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param val pointer filled with array value
 * @param len pointer to int which gets filled with the number of elements in array
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetMessageArray(const void *vmsg, const char *name, const void ***val, int *len) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  item = msg->payload;
  while (item != NULL) {
    if (strcmp(item->name, name) == 0) {
      break;
    }
    item = item->next;
  }
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  
  if (item->type != CMSG_CP_MSG_A || item->count < 1 || item->array == NULL) {
    releaseMutex();
    return(CMSG_BAD_FORMAT);
  }
    
  *val = (const void **)item->array;
  *len = item->count;
  
  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a real value of the given field if it exists.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param type type of real to get (CMSG_CP_DBL or CMSG_CP_FLT)
 * @param val pointer filled with field value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any pointer arg is NULL
 */   
static int getReal(const void *vmsg, const char *name, int type, double *val) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);

  grabMutex();
  
  item = msg->payload;
  while (item != NULL) {
    if (strcmp(item->name, name) == 0) {
      break;
    }
    item = item->next;
  }
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  
  if (item->type != type || item->count != 1) {
    releaseMutex();
    return(CMSG_BAD_FORMAT);
  }
  
  *val = item->dval;

  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a float of the given field if it exists.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param val pointer filled with field value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetFloat(const void *vmsg, const char *name, float *val) {
  int err;
  double dbl;
  
  err = getReal(vmsg, name, CMSG_CP_FLT, &dbl);
  if (err != CMSG_OK) return(err);
  *val = (float) dbl;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the double given field if it exists.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param val pointer filled with field value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetDouble(const void *vmsg, const char *name, double *val) {
  return getReal(vmsg, name, CMSG_CP_DBL, val);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns an array of the given field if it exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param type type of array to get (CMSG_CP_DBL_A, CMSG_CP_INT32_A, etc.)
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any pointer arg is NULL
 */   
static int getArray(const void *vmsg, const char *name, int type,
                    const void **vals, int *len) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || vals == NULL || len == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  item = msg->payload;
  while (item != NULL) {
    if (strcmp(item->name, name) == 0) {
      break;
    }
    item = item->next;
  }
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  
  if (item->type != type || item->count < 1 || item->array == NULL) {
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
 * This routine returns the float array of the given field if it exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetFloatArray(const void *vmsg, const char *name, const float **vals, int *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, name, CMSG_CP_FLT_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (float *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the double array of the given field if it exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetDoubleArray(const void *vmsg, const char *name, const double **vals, int *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, name, CMSG_CP_DBL_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (double *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns an integer value of the given field if it exists.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param type type of integer to get (e.g. CMSG_CP_INT32)
 * @param val pointer filled with field value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any pointer arg is NULL
 */   
static int getInt(const void *vmsg, const char *name, int type, int64_t *val) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);

  grabMutex();
  
  item = msg->payload;
  while (item != NULL) {
    if (strcmp(item->name, name) == 0) {
      break;
    }
    item = item->next;
  }
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  
  if (item->type != type || item->count > 1) {
    releaseMutex();
    return(CMSG_BAD_FORMAT);
  }
  
  *val = item->val;
  
  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns an 8 bit, signed integer given field if it exists.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param val pointer filled with field value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetInt8(const void *vmsg, const char *name, int8_t *val) {
  int err;
  int64_t int64;
  
  err = getInt(vmsg, name, CMSG_CP_INT8, &int64);
  if (err != CMSG_OK) return(err);
  *val = (int8_t) int64;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a 16 bit, signed integer given field if it exists.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param val pointer filled with field value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetInt16(const void *vmsg, const char *name, int16_t *val) {
  int err;
  int64_t int64;
  
  err = getInt(vmsg, name, CMSG_CP_INT16, &int64);
  if (err != CMSG_OK) return(err);
  *val = (int16_t) int64;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a 32 bit, signed integer given field if it exists.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param val pointer filled with field value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetInt32(const void *vmsg, const char *name, int32_t *val) {
  int err;
  int64_t int64;
  
  err = getInt(vmsg, name, CMSG_CP_INT32, &int64);
  if (err != CMSG_OK) return(err);
  *val = (int32_t) int64;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a 64 bit, signed integer given field if it exists.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param val pointer filled with field value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetInt64(const void *vmsg, const char *name, int64_t *val) {  
  return getInt(vmsg, name, CMSG_CP_INT64, val);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns an 8 bit, unsigned integer given field if it exists.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param val pointer filled with field value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetUint8(const void *vmsg, const char *name, uint8_t *val) {
  int err;
  int64_t int64;
  
  err = getInt(vmsg, name, CMSG_CP_UINT8, &int64);
  if (err != CMSG_OK) return(err);
  *val = (uint8_t) int64;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a 16 bit, unsigned integer given field if it exists.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param val pointer filled with field value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetUint16(const void *vmsg, const char *name, uint16_t *val) {
  int err;
  int64_t int64;
  
  err = getInt(vmsg, name, CMSG_CP_UINT16, &int64);
  if (err != CMSG_OK) return(err);
  *val = (uint16_t) int64;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a 32 bit, unsigned integer given field if it exists.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param val pointer filled with field value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetUint32(const void *vmsg, const char *name, uint32_t *val) {
  int err;
  int64_t int64;
  
  err = getInt(vmsg, name, CMSG_CP_UINT32, &int64);
  if (err != CMSG_OK) return(err);
  *val = (uint32_t) int64;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a 64 bit, unsigned integer given field if it exists.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param val pointer filled with field value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetUint64(const void *vmsg, const char *name, uint64_t *val) {
  int err;
  int64_t int64;
  
  err = getInt(vmsg, name, CMSG_CP_UINT64, &int64);
  if (err != CMSG_OK) return(err);
  *val = (uint64_t) int64;
  
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns an 8 bit, signed integer array of the given field if it exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetInt8Array(const void *vmsg, const char *name, const int8_t **vals, int *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, name, CMSG_CP_INT8_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (int8_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a 16 bit, signed integer array of the given field if it exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetInt16Array(const void *vmsg, const char *name, const int16_t **vals, int *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, name, CMSG_CP_INT16_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (int16_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a 32 bit, signed integer array of the given field if it exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetInt32Array(const void *vmsg, const char *name, const int32_t **vals, int *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, name, CMSG_CP_INT32_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (int32_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a 64 bit, signed integer array of the given field if it exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetInt64Array(const void *vmsg, const char *name, const int64_t **vals, int *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, name, CMSG_CP_INT64_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (int64_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns an 8 bit, unsigned integer array of the given field if it exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetUint8Array(const void *vmsg, const char *name, const uint8_t **vals, int *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, name, CMSG_CP_UINT8_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (uint8_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a 16 bit, unsigned integer array of the given field if it exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetUint16Array(const void *vmsg, const char *name, const uint16_t **vals, int *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, name, CMSG_CP_UINT16_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (uint16_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a 32 bit, unsigned integer array of the given field if it exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetUint32Array(const void *vmsg, const char *name, const uint32_t **vals, int *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, name, CMSG_CP_UINT32_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (uint32_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns a 64 bit, unsigned integer array of the given field if it exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param vals pointer filled with field array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetUint64Array(const void *vmsg, const char *name, const uint64_t **vals, int *len) {
  int   err;
  const void *array;
    
  err = getArray(vmsg, name, CMSG_CP_UINT64_A, &array, len);
  if (err != CMSG_OK) return(err);
  *vals = (uint64_t *)array;

  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the value of the given field as a string if it exists.
 * Do NOT write into the returned pointer's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param val pointer filled with field value
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetString(const void *vmsg, const char *name, const char **val) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  item = msg->payload;
  while (item != NULL) {
    if (strcmp(item->name, name) == 0) {
      break;
    }
    item = item->next;
  }
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  
  if (item->type != CMSG_CP_STR || item->count != 1 || item->array == NULL) {
    releaseMutex();
    return(CMSG_BAD_FORMAT);
  }
  
  *val = (char *)item->array;
  
  releaseMutex();
  return(CMSG_OK);
}

  
/*-------------------------------------------------------------------*/


/**
 * This routine returns the string array of the given field if it exists.
 * Do NOT write into the returned array's memory location.
 *
 * @param vmsg pointer to message
 * @param name name of payload field
 * @param array pointer to array of pointers which gets filled with string array
 * @param len pointer int which gets filled with the number of elements in array
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if no payload or field of that name exists
 * @return CMSG_BAD_FORMAT field is not right type or contains error
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 */   
int cMsgGetStringArray(const void *vmsg, const char *name, const char ***array, int *len) {
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || array == NULL || len == NULL) return(CMSG_BAD_ARGUMENT);
  
  grabMutex();
  
  item = msg->payload;
  while (item != NULL) {
    if (strcmp(item->name, name) == 0) {
      break;
    }
    item = item->next;
  }
  
  if (item == NULL) {
    releaseMutex();
    return(CMSG_ERROR);
  }
  
  if (item->type != CMSG_CP_STR_A || item->count < 1 || item->array == NULL) {
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
 * This routine adds/overwrites a named field of binary data to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param src pointer to binary data to add
 * @param size size in bytes of data to add
 * @param isSystem if = 0, is not a system field, else is (name starts with "cmsg")
 * @param endian endian value of binary data, may be CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *               CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if cannot find local endian
 * @return CMSG_BAD_ARGUMENT if message, src or name is NULL, size < 1, or
 *                            endian != CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *                            CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed,
 *                          or if error in binary-to-text transformation
 */
static int addBinary(void *vmsg, const char *name, const char *src, int size,
                     int isSystem, int endian) {
  payloadItem *item;
  int len, textLen, totalLen;
  unsigned int lenBin, numChars;
  char *s;
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL ||
      src == NULL || size < 1)              return(CMSG_BAD_ARGUMENT);
  if (!isValidFieldName(name, isSystem))    return(CMSG_BAD_FORMAT);
  if (isSystem) isSystem = 1; /* force it to be = 1, need to it be 1 digit */
  
  if ((endian != CMSG_ENDIAN_BIG)   && (endian != CMSG_ENDIAN_LITTLE)   &&
      (endian != CMSG_ENDIAN_LOCAL) && (endian != CMSG_ENDIAN_NOTLOCAL))  {
      return(CMSG_BAD_ARGUMENT);
  }
  else {
    int ndian;
    if (endian == CMSG_ENDIAN_LOCAL) {
        if (cMsgNetLocalByteOrder(&ndian) != CMSG_OK) {
          return CMSG_ERROR;
        }
        endian = ndian;
    }
    /* set to opposite of local endian value */
    else if (endian == CMSG_ENDIAN_NOTLOCAL) {
        if (cMsgNetLocalByteOrder(&ndian) != CMSG_OK) {
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
  item->size   = size;
  item->count  = 1;
  item->endian = endian;
  
  /* store original data */
  item->array = (void *) malloc((size_t)size);
  if (item->array == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  memcpy(item->array, src, (size_t)size);
    
  /* Create a string to hold all data to be transferred over the network.
   * That means converting binary to text */

  /* first find size of text-encoded binary data (including newline at end) */
  lenBin = cMsg_b64_encode_len(src, (unsigned int)size, 1);
 
  /* length of string to contain all text representation except first line */
  textLen = numDigits(lenBin, 0) + numDigits(size, 0) +
            lenBin + 4; /* 4 = endian + 2 spaces + 1 newline */
  item->noHeaderLen = textLen;
            
  /* length of first line of text representation + textLen + null */
  totalLen = (int) (strlen(name) +
             2 + /* 2 digit type */
             numDigits(item->count, 0) +
             1 + /* 1 digit isSystem */
             numDigits(textLen, 0) + 
             6 + /* 4 spaces, 1 newline, 1 null term */
             textLen);
                
/*printf("addBinary: encoded bin len = %u, allocate = %d\n", lenBin, totalLen);*/
  s = item->text = (char *) malloc((size_t)totalLen);
  /*memset(s, 255, totalLen);*/
  
  if (item->text == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->text[totalLen-1] = '\0';

  /* write first line & length */
  sprintf(s, "%s %d %d %d %d\n%u %d %d\n%n", name, item->type, item->count,
                              isSystem, textLen, lenBin, size, endian, &len);
  s += len;
  
  /* write the binary-encoded text */
  numChars = cMsg_b64_encode(src, (unsigned int)size, s, 1);
  s += numChars;
  if (lenBin != numChars) {
      printf("addBinary: error\n");
      payloadItemFree(item, 1);
      free(item);
      return(CMSG_BAD_FORMAT);  
  }
/*printf("addBinary: actually add bytes = %u\n", numChars);*/
/*printf("addBinary: total text rep =\n%s", item->text);*/
    
  item->length = (int)strlen(item->text);
/*printf("addBinary: total string len = %d\n", item->length);*/
  /* remove any existing item with that name */
  if (cMsgPayloadContainsName(vmsg, name)) {
      removeItem(msg, name, NULL);
  }
  
  /* place payload item in msg's linked list */
  addItem(msg, item);

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
 * @param endian endian value of binary data, may be CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *               CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if cannot find local endian
 * @return CMSG_BAD_ARGUMENT if message, src or name is NULL, size < 1, or
 *                            endian != CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *                            CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddBinary(void *vmsg, const char *name, const char *src,
                  int size, int endian) {
  return addBinary(vmsg, name, src, size, 0, endian);
}


/**
 * This routine adds/overwrites a named field of binary data to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param src pointer to array of binary data arrays to add
 * @param number number of arrays of binary data to add
 * @param size array of sizes in bytes of binary data arrays to add
 * @param isSystem if = 0, is not a system field, else is (name starts with "cmsg")
 * @param endian array of endian values of binary data arrays, may be
 *               CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *               CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if cannot find local endian
 * @return CMSG_BAD_ARGUMENT if message, src or name is NULL, size < 1, or
 *                            endian != CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *                            CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed,
 *                          or if error in binary-to-text transformation
 */
static int addBinaryArray(void *vmsg, const char *name, const char *src[],
                          int number, const int size[], int isSystem,
                          const int endian[]) {
    payloadItem *item;
    int i, len, totalLen, *sizes, *endians, textLen=0, totalBytes=0;
    unsigned int lenBin[number], numChars;
    char *s, **array;
    
    cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

    if (msg == NULL || name == NULL ||
        src == NULL || number < 1)            return(CMSG_BAD_ARGUMENT);
    if (!isValidFieldName(name, isSystem))    return(CMSG_BAD_FORMAT);
    if (isSystem) isSystem = 1; /* force it to be = 1, need to it be 1 digit */

    endians = (int *) malloc(number*sizeof(int));
    if (endians == NULL) {
        return(CMSG_OUT_OF_MEMORY);
    }
    memcpy((void *)endians, (const void *)endian, number*sizeof(int));
    
    sizes = (int *) malloc(number*sizeof(int));
    if (sizes == NULL) {
        free(endians);
        return(CMSG_OUT_OF_MEMORY);
    }
    memcpy((void *)sizes, (const void *)size, number*sizeof(int));

    for (i=0; i<number; i++) {
        totalBytes += sizes[i];
        
        if (((endians[i] != CMSG_ENDIAN_BIG)    &&
             (endians[i] != CMSG_ENDIAN_LITTLE) &&
             (endians[i] != CMSG_ENDIAN_LOCAL)  &&
             (endians[i] != CMSG_ENDIAN_NOTLOCAL)) ||
             (sizes[i] < 1)) {
            free(endians);free(sizes);
            return(CMSG_BAD_ARGUMENT);
        }
        else {
            int ndian;
            if (endians[i] == CMSG_ENDIAN_LOCAL) {
                if (cMsgNetLocalByteOrder(&ndian) != CMSG_OK) {
                    free(endians);free(sizes);
                    return CMSG_ERROR;
                }
                endians[i] = ndian;
            }
            /* set to opposite of local endian value */
            else if (endians[i] == CMSG_ENDIAN_NOTLOCAL) {
                if (cMsgNetLocalByteOrder(&ndian) != CMSG_OK) {
                    free(endians);free(sizes);
                    return CMSG_ERROR;
                }
                if (ndian == CMSG_ENDIAN_BIG) {
                    endians[i] = CMSG_ENDIAN_LITTLE;
                }
                else {
                    endians[i] = CMSG_ENDIAN_BIG;
                }
            }
        }
    }

    /* payload item */
    item = (payloadItem *) calloc(1, sizeof(payloadItem));
    if (item == NULL) {
        free(endians);free(sizes);
        return(CMSG_OUT_OF_MEMORY);
    }
    payloadItemInit(item);

    item->type    = CMSG_CP_BIN_A;
    item->count   = number;
    item->sizes   = sizes;
    item->endians = endians;
    item->name    = strdup(name);
    if (item->name == NULL) {
        free(item);
        return(CMSG_OUT_OF_MEMORY);
    }

    /* store original data */
    array = (char **) malloc(number*sizeof(char *));
    item->array = (void *) array;
    if (item->array == NULL) {
        payloadItemFree(item, 1);
        free(item);
        return(CMSG_OUT_OF_MEMORY);
    }
    
    for (i=0; i<number; i++) {
        array[i] = (char *) malloc((size_t)size[i]);
        if (array[i] == NULL) {
            payloadItemFree(item, 1);
            free(item);
            return(CMSG_OUT_OF_MEMORY);
        }
        memcpy((void *)array[i], (void *)src[i], (size_t)sizes[i]);
    }

    /* Create a string to hold all data to be transferred over the network.
     * That means converting binary to text */

    for (i=0; i<number; i++) {
        /* first find size of text-encoded binary data (including newline at end) */
        lenBin[i] = cMsg_b64_encode_len(src[i], (unsigned int)size[i], 1);
/*printf("addBinaryArray: encoded bin len[%d] = %u\n", i, lenBin[i]);*/

        /* length of string to contain all text representation except first line */
        textLen += numDigits(lenBin[i], 0) + numDigits(size[i], 0) +
                   lenBin[i] + 4; /* 4 = 1 sizeof endian + 2 spaces + 1 newline */
    }
    item->noHeaderLen = textLen;
/*printf("addBinaryArray: total noheader text len = %d\n", textLen);*/

    /* length of first line of text representation + textLen + null */
    totalLen = (int) (strlen(name) +
            2 + /* 2 digit type */
            numDigits(number, 0) +
            1 + /* 1 digit isSystem */
            numDigits(textLen, 0) +
            6 + /* 4 spaces, 1 newline, 1 null term */
            textLen);
/*printf("addBinaryArray: total text rep len = %d\n", totalLen);*/

    s = item->text = (char *) malloc((size_t)totalLen);
    if (item->text == NULL) {
        payloadItemFree(item, 1);
        free(item);
        return(CMSG_OUT_OF_MEMORY);
    }
    item->text[totalLen-1] = '\0';

    /* write first line */
    sprintf(s, "%s %d %d %d %d\n%n", name, item->type, number, isSystem, textLen, &len);
/*printf("addBinaryArray: text =\n%s", s);*/
    s += len;

    /* write all other lines */
    for (i=0; i<number; i++) {
        /* Length of text rep is increased by 1 to include its following newline,
         * so it's compatible with the Java version that automatically adds a
         * newline to the end (which is ignored when parsing).
         */
        sprintf(s, "%u %d %d\n%n", lenBin[i], size[i], endian[i], &len);
        s += len;
        
        /* write the binary-encoded text */
        numChars = cMsg_b64_encode(src[i], (unsigned int)size[i], s, 1);
        s += numChars;
        if (lenBin[i] != numChars) {
            payloadItemFree(item, 1);
            free(item);
            return(CMSG_BAD_FORMAT);
        }
        /*printf("addBinaryArray: actually add bytes = %u\n", numChars);*/
    }

/*printf("addBinaryArray: text =\n%s", item->text);*/

    item->length = (int)strlen(item->text);
/*printf("addBinaryArray: total string len = %d\n", item->length);*/
    /* remove any existing item with that name */
    if (cMsgPayloadContainsName(vmsg, name)) {
        removeItem(msg, name, NULL);
    }

    /* place payload item in msg's linked list */
    addItem(msg, item);

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
 * @param src pointer to array of binary data arrays to add
 * @param number number of arrays of binary data to add
 * @param size array of sizes in bytes of binary data arrays to add
 * @param endian array of endian values of binary data arrays, may be
 *               CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *               CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 *
 * @return CMSG_OK if successful
 * @return CMSG_ERROR if cannot find local endian
 * @return CMSG_BAD_ARGUMENT if message, src or name is NULL, size < 1, or
 *                            endian != CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *                            CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed,
 *                          or if error in binary-to-text transformation
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */
int cMsgAddBinaryArray(void *vmsg, const char *name, const char *src[],
                       int number, const int size[], const int endian[]) {
    return addBinaryArray(vmsg, name, src, number, size, 0, endian);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds/overwrites any kind of real number field to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val float or double to add
 * @param type type of real number to add (CMSG_CP_FLT or CMSG_CP_DBL)
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message or name is NULL, or
 *                            wrong type being added
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 */
static int addReal(void *vmsg, const char *name, double val, int type, int isSystem) {
  payloadItem *item;
  int byte, textLen, len;
  size_t totalLen;
  char *s;
  uint32_t j32;
  uint64_t j64;
  intFloatUnion  floater;
  intDoubleUnion doubler;
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL)         return(CMSG_BAD_ARGUMENT);
  if (!isValidFieldName(name, isSystem))   return(CMSG_BAD_FORMAT);
  if (type != CMSG_CP_FLT  &&
      type != CMSG_CP_DBL)                 return(CMSG_BAD_ARGUMENT);
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
  
  /* length of string to contain all data */
  if (type == CMSG_CP_FLT) {
    textLen = 8 + 1;   /* 1 newline */
  }
  else {
    textLen = 16 + 1; /* 1 newline */
  }
  item->noHeaderLen = textLen;
  
  /* Create string to hold all data to be transferred over
   * the network for this item. */
  totalLen = strlen(name) +
             2 + /* 2 digit type */
             numDigits(item->count, 0) +
             1 + /* isSystem */
             numDigits(textLen, 0) +
             6 + /* 4 spaces, 1 newline, 1 null terminator */
             textLen;
  
  s = item->text = (char *) calloc(1, totalLen);
  if (item->text == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  sprintf(s, "%s %d %d %d %d\n%n", name, item->type, item->count, isSystem, textLen, &len);
  s += len;

  if (type == CMSG_CP_FLT) {
    floater.f = (float)val;
    j32 = floater.i;
    byte = j32>>24 & 0xff;
    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
    byte = j32>>16 & 0xff;
    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
    byte = j32>>8 & 0xff;
    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
    byte = j32 & 0xff;
    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
    *s++ = '\n';
  }
  else {
    doubler.d = val;
    j64 = doubler.i;
    byte = (int) (j64>>56 & 0xffL);
    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
    byte = (int) (j64>>48 & 0xffL);
    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
    byte = (int) (j64>>40 & 0xffL);
    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
    byte = (int) (j64>>32 & 0xffL);
    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
    byte = (int) (j64>>24 & 0xffL);
    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
    byte = (int) (j64>>16 & 0xffL);
    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
    byte = (int) (j64>>8 & 0xffL);
    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
    byte = (int) (j64 & 0xffL);
    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
    *s++ = '\n';
  }

  item->length = (int)strlen(item->text);
  item->dval   = val;

  if (cMsgPayloadContainsName(vmsg, name)) {
      removeItem(msg, name, NULL);
  }
  
  addItem(msg, item);
  
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
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message or name is NULL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddFloat(void *vmsg, const char *name, float val) {
  return addReal(vmsg, name, val, CMSG_CP_FLT, 0);
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
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message or name is NULL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddDouble(void *vmsg, const char *name, double val) {
  return addReal(vmsg, name, val, CMSG_CP_DBL, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds/overwrites a float or double array field to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 * Zero suppression is implemented where contiguous zeros are replaced by one string
 * of hex starting with Z and the rest of the chars containing the number of zeros.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of floats or doubles to add
 * @param type type of array to add (CMSG_CP_FLT_A, or CMSG_CP_DBL_A)
 * @param len number of values from array to add
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 * @param copy if true, copy the array in "vals", else record the pointer and assume ownership
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, name, or vals is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 */
static int addRealArray(void *vmsg, const char *name, const double *vals,
                        int type, int len, int isSystem, int copy) {
  payloadItem *item;
  int i, byte, cLen, textLen=0, thisOneZero=0;
  size_t totalLen;
  void *array;
  char *s;
  uint32_t j32, zeros=0, suppressed=0;
  uint64_t j64;
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL  || name == NULL ||
      vals == NULL || len < 1)               return(CMSG_BAD_ARGUMENT);
  if (!isValidFieldName(name, isSystem))     return(CMSG_BAD_FORMAT);
  if (type != CMSG_CP_FLT_A &&
      type != CMSG_CP_DBL_A)                 return(CMSG_BAD_ARGUMENT);
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
  if (type == CMSG_CP_FLT_A) {
    textLen += 8*len;
  }
  else {
    textLen += 16*len;
  }
  
  /* length of string to contain all data */
  textLen += len; /* len-1 spaces, 1 newline */
  item->noHeaderLen = textLen; 
  
  totalLen = strlen(name) +
             2 +  /* 2 digit type */
             numDigits(item->count, 0) +
             1 +  /* isSystem */
             10 + /* 10 char length (space padding) */
             6 +  /* 4 spaces, 1 newline, 1 null terminator */
             textLen;

  s = item->text = (char *) calloc(1, totalLen);
  if (item->text == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* If zeroes are suppressed, this line may need to be re-written since
   * textLen changes. To allow this, we pad the len so it always takes
   * 10 chars, allowing the maximum integer of 2,147,483,647 to be used
   * for a length.
   * Zeroes are compressed into a 16 char string for doubles, into a 8
   * char string for floats. The compressed zero has the string form
   * Zfffffff where f represents a hex char and Z means that the rest
   * of the chars are how many zeros are next. Thus for 32 bits, the
   * max = Zffffff = 268,435,455 zeros compressed into an 8 char string.
   * And for 64 bits the max = Zffffffffffffff (= some really big decimal).
   * Since I don't forsee transferring this many numbers in a message, I
   * think the limits will not hinder us. In any case, many of these 
   * zero-suppression-strings can be placed next to each other.
   */
  sprintf(s, "%s %d %d %d %10d\n%n", name, item->type, item->count, isSystem, textLen, &cLen);
  s += cLen;
  
  if (type == CMSG_CP_FLT_A) {
    for (i=0; i<len; i++) {
        j32 = ((uint32_t *)vals)[i];
        
        /* both values are zero in IEEE754 */
        if (j32 == 0 || j32 == 0x80000000) {
          if ((++zeros < 0xfffffff) && (i < len-1)) {
            continue;
          }
          thisOneZero++;
        }
        
        if (zeros) {
          /* how many floats did we not write? */
          suppressed += zeros-1;
/*printf("SUPPRESSED %u\n",suppressed);*/
          /* don't use 'Z' for only 1 zero */
          if (zeros == 1) {
            s[0] = '0'; s[1] = '0'; s[2] = '0'; s[3] = '0';
            s[4] = '0'; s[5] = '0'; s[6] = '0'; s[7] = '0';
            s += 8;
          }
          else {
              byte = zeros>>24 & 0xff;
              *s++ = 'Z'; *s++ = toASCII[byte][1];
              byte = zeros>>16 & 0xff;
              *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
              byte = zeros>>8 & 0xff;
              *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
              byte = zeros & 0xff;
              *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
          }
          if (thisOneZero) {
            if (i < len-1) {
              *s++ = ' ';
              zeros = 0;
              thisOneZero = 0;
              continue;
            }
            else {
              *s++ = '\n';
              break;
            }
          }
          /* this one is NOT zero, just wrote out the accumumlated zeros */
          *s++ = ' ';
          zeros = 0;
          thisOneZero = 0;
        }
        
        byte = j32>>24 & 0xff;
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = j32>>16 & 0xff;
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = j32>>8 & 0xff;
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = j32 & 0xff;
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        if (i < len-1) {
          *s++ = ' ';
        } else {
          *s++ = '\n';
        }
    }
    
    /* rewrite header if necessary */
    if (suppressed) {
      textLen -= suppressed * 9; /* 9 chars for each skipped */
      item->noHeaderLen = textLen; 
      sprintf(item->text, "%s %d %d %d %10d%n", name, item->type, item->count, isSystem, textLen, &cLen);
      /* get rid of trailing NULL just introduced */
      (item->text + cLen)[0] = '\n';
    }
    
    /* Store the values */
    if (copy) {
      array = malloc(len*sizeof(float));
      if (array == NULL) {payloadItemFree(item, 1); free(item); return(CMSG_OUT_OF_MEMORY);}
      memcpy(array, vals, len*sizeof(float));
      item->array = (void *)array;
    }
    else {
      item->array = (void *)vals;
    }
  }
  
  /* else we got an array of doubles ... */
  else {
    for (i=0; i<len; i++) {
        j64 = ((uint64_t *)vals)[i];
        
        /* both values are zero in IEEE754 */
        if (j64 == 0 || j64 == 0x8000000000000000LL) {
          if ((++zeros < 0xfffffff) && (i < len-1)) {
            continue;
          }
          thisOneZero++;
        }
        
        if (zeros) {
          /* how many doubles did we not write? */
          suppressed += zeros-1;
/*printf("SUPPRESSED %u\n",suppressed);*/

          /* don't use 'Z' for only 1 zero */
          if (zeros == 1) {
            s[ 0] = '0'; s[ 1] = '0'; s[ 2] = '0'; s[ 3] = '0';
            s[ 4] = '0'; s[ 5] = '0'; s[ 6] = '0'; s[ 7] = '0';
            s[ 8] = '0'; s[ 9] = '0'; s[10] = '0'; s[11] = '0';
            s[12] = '0'; s[13] = '0'; s[14] = '0'; s[15] = '0';
            s += 16;
          }
          else {
            s[0] = 'Z'; s[1] = '0'; s[2] = '0'; s[3] = '0';
            s[4] = '0'; s[5] = '0'; s[6] = '0'; s[7] = '0';
            s += 8;

            byte = zeros>>24 & 0xff;
            *s++ = '0'; *s++ = toASCII[byte][1];
            byte = zeros>>16 & 0xff;
            *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
            byte = zeros>>8 & 0xff;
            *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
            byte = zeros & 0xff;
            *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
          }
          
          if (thisOneZero) {
            if (i < len-1) {
              *s++ = ' ';
              zeros = 0;
              thisOneZero = 0;
              continue;
            }
            else {
              *s++ = '\n';
              break;
            }
          }
          /* this one is NOT zero, just wrote out the accumumlated zeros */
          *s++ = ' ';
          zeros = 0;
          thisOneZero = 0;
        }
        
        byte = (int) (j64>>56 & 0xffL);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = (int) (j64>>48 & 0xffL);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = (int) (j64>>40 & 0xffL);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = (int) (j64>>32 & 0xffL);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = (int) (j64>>24 & 0xffL);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = (int) (j64>>16 & 0xffL);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = (int) (j64>>8 & 0xffL);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = (int) (j64 & 0xffL);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        if (i < len-1) {
          *s++ = ' ';
        } else {
          *s++ = '\n';
        }
    }
              
    /* rewrite header if necessary */
    if (suppressed) {
      textLen -= suppressed * 17; /* 17 chars for each skipped */
      item->noHeaderLen = textLen; 
      sprintf(item->text, "%s %d %d %d %10d%n", name, item->type, item->count, isSystem, textLen, &cLen);
      /* get rid of trailing NULL just introduced */
      (item->text + cLen)[0] = '\n';
    }
    
    /* Store the values */
    if (copy) {
      array = malloc(len*sizeof(double));
      if (array == NULL) {payloadItemFree(item, 1); free(item); return(CMSG_OUT_OF_MEMORY);}
      memcpy(array, vals, len*sizeof(double));
      item->array = (void *)array;
    }
    else {
      item->array = (void *)vals;
    }
  }
  
  item->length = (int)strlen(item->text);
/*printf("Real array txt =\n%s\n", item->text);*/
  if (cMsgPayloadContainsName(vmsg, name)) {
      removeItem(msg, name, NULL);
  }
  
  addItem(msg, item);
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, float array field to the compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of floats to add (copy)
 * @param len number of floats from array to add
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, name, or vals is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddFloatArray(void *vmsg, const char *name, const float vals[], int len) {
   return addRealArray(vmsg, name, (double *)vals, CMSG_CP_FLT_A, len, 0, 1); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, double array field to the compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of doubles to add (copy)
 * @param len number of doubles from array to add
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, name, or vals is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddDoubleArray(void *vmsg, const char *name, const double vals[], int len) {
   return addRealArray(vmsg, name, vals, CMSG_CP_DBL_A, len, 0, 1); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds/overwrites any kind of int field to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val int to add
 * @param type type of int to add (CMSG_CP_INT32, etc)
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message or name is NULL, or
 *                            wrong type being added
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 */
static int addInt(void *vmsg, const char *name, int64_t val, int type, int isSystem) {
  payloadItem *item;
  size_t totalLen;
  int textLen=0;
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL)       return(CMSG_BAD_ARGUMENT);
  if (!isValidFieldName(name, isSystem)) return(CMSG_BAD_FORMAT);
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
   
  textLen = 1; /* 1 newline */
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
             6 + /* 4 spaces, 1 newline, 1 terminator */
             textLen;

  item->text = (char *) calloc(1, totalLen);
  if (item->text == NULL) {
    payloadItemFree(item, 1);
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

  item->length = (int) strlen(item->text);
  item->val    = val;

  if (cMsgPayloadContainsName(vmsg, name)) {
      removeItem(msg, name, NULL);
  }
  
  addItem(msg, item);
  
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
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message or name is NULL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt8(void *vmsg, const char *name, int8_t val) {
  return addInt(vmsg, name, val, CMSG_CP_INT8, 0);
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
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message or name is NULL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt16(void *vmsg, const char *name, int16_t val) {
  return addInt(vmsg, name, val, CMSG_CP_INT16, 0);
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
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message or name is NULL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt32(void *vmsg, const char *name, int32_t val) {
  return addInt(vmsg, name, val, CMSG_CP_INT32, 0);
}


/**
 * This routine adds a named, 32-bit, signed int field to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 * Names may not be longer than CMSG_PAYLOAD_NAME_LEN, or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val value of 32-bit, signed int to add
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message or name is NULL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
static int addInt32(void *vmsg, const char *name, int32_t val, int isSystem) {
  return addInt(vmsg, name, val, CMSG_CP_INT32, isSystem);
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
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message or name is NULL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt64(void *vmsg, const char *name, int64_t val) {
  return addInt(vmsg, name, val, CMSG_CP_INT64, 0);
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
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message or name is NULL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint8(void *vmsg, const char *name, uint8_t val) {
  return addInt(vmsg, name, val, CMSG_CP_UINT8, 0);
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
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message or name is NULL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint16(void *vmsg, const char *name, uint16_t val) {
  return addInt(vmsg, name, val, CMSG_CP_UINT16, 0);
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
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message or name is NULL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint32(void *vmsg, const char *name, uint32_t val) {
  return addInt(vmsg, name, val, CMSG_CP_UINT32, 0);
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
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message or name is NULL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint64(void *vmsg, const char *name, uint64_t val) {
  return addInt(vmsg, name, (int64_t)val, CMSG_CP_UINT64, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds/overwrites any kind of int array field to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of ints to add
 * @param type type of int array to add (CMSG_CP_INT32_A, etc)
 * @param len number of ints from array to add
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 * @param copy if true, copy the array in "vals", else record the pointer and assume ownership
 *
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, vals, or name is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 */
static int addIntArray(void *vmsg, const char *name, const int *vals,
                       int type, int len, int isSystem, int copy) {
  int err;
  payloadItem *item;
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL  || name == NULL ||
      vals == NULL || len < 1)              return(CMSG_BAD_ARGUMENT);
  if (!isValidFieldName(name, isSystem))    return(CMSG_BAD_FORMAT);

  if (type != CMSG_CP_INT8_A   && type != CMSG_CP_INT16_A  &&
      type != CMSG_CP_INT32_A  && type != CMSG_CP_INT64_A  &&
      type != CMSG_CP_UINT8_A  && type != CMSG_CP_UINT16_A &&
      type != CMSG_CP_UINT32_A && type != CMSG_CP_UINT64_A)  {
      return(CMSG_BAD_ARGUMENT);
  }
  if (isSystem) isSystem = 1;
  
  err = createIntArrayItem(name, vals, type, len, isSystem, copy, &item);
  if (err != CMSG_OK) return err;
  
  if (cMsgPayloadContainsName(vmsg, name)) {
      removeItem(msg, name, NULL);
  }
  
  addItem(msg, item);
  return(CMSG_OK);
}


/**
 * This routine creates any kind of int array payload item.
 * Used internally with control over adding fields with names starting with "cmsg".
 *
 * @param name name of field to add
 * @param vals array of ints to add
 * @param type type of int array to add (CMSG_CP_INT32_A, etc)
 * @param len number of ints from array to add
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 * @param copy if true, copy the array in "vals", else record the pointer and assume ownership
 * @param newItem pointer filled in with created payload item
 *
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, vals, name, or newItem is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
static int createIntArrayItem(const char *name, const int *vals, int type,
                              int len, int isSystem, int copy, payloadItem **newItem) {
  payloadItem *item;
  int       i, byte, cLen, valLen=0, textLen=0, thisOneZero=0;
  size_t totalLen;
  char  *s, j8;
  uint16_t  j16;
  uint32_t  j32, zeros=0, suppressed=0;
  uint64_t  j64;
  void *array=NULL;
  
  
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
      case CMSG_CP_UINT8_A:
            {valLen += 2*len;}
            break;
      case CMSG_CP_INT16_A:
      case CMSG_CP_UINT16_A:
            {valLen += 4*len;}
            break;
      case CMSG_CP_INT32_A:
      case CMSG_CP_UINT32_A:
            {valLen += 8*len;}
            break;
      case CMSG_CP_INT64_A:
      case CMSG_CP_UINT64_A:
            {valLen += 16*len;}
  }
   
  textLen  = valLen + len; /* len-1 spaces, 1 newline */
  item->noHeaderLen = textLen;
  
  totalLen = strlen(name) +
             2 + /* 2 digit type */
             numDigits(item->count, 0) +
             1  + /* isSystem */
             10 + /* 10 chars of textLen (including padding) */
             6  + /* 4 spaces, 1 newline, 1 null terminator */
             textLen;

  s = item->text = (char *) calloc(1, totalLen);
  if (item->text == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  sprintf(s, "%s %d %d %d %10d\n%n", name, item->type, item->count, isSystem, textLen, &cLen);
  s += cLen;
      
  switch (type) {
      case CMSG_CP_UINT8_A:
      case CMSG_CP_INT8_A:
            for (i=0; i<len; i++) {
                j8 = ((int8_t *)vals)[i];
                byte = j8 & 0xff;
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
              if (i < len-1) {
                *s++ = ' ';
              } else {
                *s++ = '\n';
              }
            }
            /* Store the values */
            if (copy) {
              array = malloc(len*sizeof(int8_t));
              if (array == NULL) {payloadItemFree(item, 1); free(item); return(CMSG_OUT_OF_MEMORY);}
              memcpy(array, vals, len*sizeof(int8_t));
              item->array = (void *)array;
            }
            else {
              item->array = (void *)vals;
            }
            break;
          
      case CMSG_CP_UINT16_A:
      case CMSG_CP_INT16_A:
            for (i=0; i<len; i++) {
                j16 = ((int16_t *)vals)[i];

                if (j16 == 0) {
                  if ((++zeros < 0xfff) && (i < len-1)) {
                    continue;
                  }
                  thisOneZero++;
                }

                if (zeros) {
                  /* how many floats did we not write? */
                  suppressed += zeros-1;
        /*printf("SUPPRESSED %u\n",suppressed);*/
                  /* don't use 'Z' for only 1 zero */
                  if (zeros == 1) {
                    s[0] = '0'; s[1] = '0'; s[2] = '0'; s[3] = '0';
                    s += 4;
                  }
                  else {
                    byte = zeros>>8 & 0xff;
                    *s++ = 'Z'; *s++ = toASCII[byte][1];
                    byte = zeros & 0xff;
                    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                  }
                  
                  if (thisOneZero) {
                    if (i < len-1) {
                      *s++ = ' ';
                      zeros = 0;
                      thisOneZero = 0;
                      continue;
                    }
                    else {
                      *s++ = '\n';
                      break;
                    }
                  }
                  /* this one is NOT zero, just wrote out the accumumlated zeros */
                  *s++ = ' ';
                  zeros = 0;
                  thisOneZero = 0;
                }

                byte = j16>>8 & 0xff;
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                byte = j16 & 0xff;
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                if (i < len-1) {
                  *s++ = ' ';
                } else {
                  *s++ = '\n';
                }
            }
            
            /* rewrite header if necessary */
            if (suppressed) {
              textLen -= suppressed * 5; /* 5 chars for each skipped */
              item->noHeaderLen = textLen; 
              sprintf(item->text, "%s %d %d %d %10d%n", name, item->type, item->count, isSystem, textLen, &cLen);
              /* get rid of trailing NULL just introduced */
              (item->text + cLen)[0] = '\n';
            }


            /* Store the values */
            if (copy) {
              array = malloc(len*sizeof(int16_t));
              if (array == NULL) {payloadItemFree(item, 1); free(item); return(CMSG_OUT_OF_MEMORY);}
              memcpy(array, vals, len*sizeof(int16_t));
              item->array = (void *)array;
            }
            else {
              item->array = (void *)vals;
            }
            break;
            
      case CMSG_CP_UINT32_A:
      case CMSG_CP_INT32_A:
            for (i=0; i<len; i++) {
                j32 = ((uint32_t *)vals)[i];

                if (j32 == 0) {
                  if ((++zeros < 0xfffffff) && (i < len-1)) {
                    continue;
                  }
                  thisOneZero++;
                }

                if (zeros) {
                  /* how many floats did we not write? */
                  suppressed += zeros-1;
        /*printf("SUPPRESSED %u\n",suppressed);*/
                  /* don't use 'Z' for only 1 zero */
                  if (zeros == 1) {
                    s[0] = '0'; s[1] = '0'; s[2] = '0'; s[3] = '0';
                    s[4] = '0'; s[5] = '0'; s[6] = '0'; s[7] = '0';
                    s += 8;
                  }
                  else {
                    byte = zeros>>24 & 0xff;
                    *s++ = 'Z'; *s++ = toASCII[byte][1];
                    byte = zeros>>16 & 0xff;
                    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                    byte = zeros>>8 & 0xff;
                    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                    byte = zeros & 0xff;
                    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                  }
                  
                  if (thisOneZero) {
                    if (i < len-1) {
                      *s++ = ' ';
                      zeros = 0;
                      thisOneZero = 0;
                      continue;
                    }
                    else {
                      *s++ = '\n';
                      break;
                    }
                  }
                  /* this one is NOT zero, just wrote out the accumumlated zeros */
                  *s++ = ' ';
                  zeros = 0;
                  thisOneZero = 0;
                }

                byte = j32>>24 & 0xff;
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                byte = j32>>16 & 0xff;
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                byte = j32>>8 & 0xff;
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                byte = j32 & 0xff;
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                if (i < len-1) {
                  *s++ = ' ';
                } else {
                  *s++ = '\n';
                }
            }
            
            /* rewrite header if necessary */
            if (suppressed) {
              textLen -= suppressed * 9; /* 9 chars for each skipped */
              item->noHeaderLen = textLen; 
              sprintf(item->text, "%s %d %d %d %10d%n", name, item->type, item->count, isSystem, textLen, &cLen);
              /* get rid of trailing NULL just introduced */
              (item->text + cLen)[0] = '\n';
            }

/*printf("Text for Int array = %s\n", item->text);*/
            /* Store the values */
            if (copy) {
              array = malloc(len*sizeof(int32_t));
              if (array == NULL) {payloadItemFree(item, 1); free(item); return(CMSG_OUT_OF_MEMORY);}
              memcpy(array, vals, len*sizeof(int32_t));
              item->array = (void *)array;
            }
            else {
              item->array = (void *)vals;
            }
            break;
            
      case CMSG_CP_UINT64_A:
      case CMSG_CP_INT64_A:
           for (i=0; i<len; i++) {
                j64 = ((uint64_t *)vals)[i];

                if (j64 == 0) {
                  if ((++zeros < 0xfffffff) && (i < len-1)) {
                    continue;
                  }
                  thisOneZero++;
                }

                if (zeros) {
                  /* how many doubles did we not write? */
                  suppressed += zeros-1;
        /*printf("SUPPRESSED %u\n",suppressed);*/          
                  /* don't use 'Z' for only 1 zero */
                  if (zeros == 1) {
                    s[ 0] = '0'; s[ 1] = '0'; s[ 2] = '0'; s[ 3] = '0';
                    s[ 4] = '0'; s[ 5] = '0'; s[ 6] = '0'; s[ 7] = '0';
                    s[ 8] = '0'; s[ 9] = '0'; s[10] = '0'; s[11] = '0';
                    s[12] = '0'; s[13] = '0'; s[14] = '0'; s[15] = '0';
                    s += 16;
                  }
                  else {
                    s[0] = 'Z'; s[1] = '0'; s[2] = '0'; s[3] = '0';
                    s[4] = '0'; s[5] = '0'; s[6] = '0'; s[7] = '0';
                    s += 8;

                    byte = zeros>>24 & 0xff;
                    *s++ = '0'; *s++ = toASCII[byte][1];
                    byte = zeros>>16 & 0xff;
                    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                    byte = zeros>>8 & 0xff;
                    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                    byte = zeros & 0xff;
                    *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                  }
                  
                  if (thisOneZero) {
                    if (i < len-1) {
                      *s++ = ' ';
                      zeros = 0;
                      thisOneZero = 0;
                      continue;
                    }
                    else {
                      *s++ = '\n';
                      break;
                    }
                  }
                  /* this one is NOT zero, just wrote out the accumumlated zeros */
                  *s++ = ' ';
                  zeros = 0;
                  thisOneZero = 0;
                }

                byte = (int) (j64>>56 & 0xffL);
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                byte = (int) (j64>>48 & 0xffL);
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                byte = (int) (j64>>40 & 0xffL);
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                byte = (int) (j64>>32 & 0xffL);
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                byte = (int) (j64>>24 & 0xffL);
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                byte = (int) (j64>>16 & 0xffL);
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                byte = (int) (j64>>8 & 0xffL);
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                byte = (int) (j64 & 0xffL);
                *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
                if (i < len-1) {
                  *s++ = ' ';
                } else {
                  *s++ = '\n';
                }
            }

            /* rewrite header if necessary */
            if (suppressed) {
              textLen -= suppressed * 17; /* 17 chars for each skipped */
              item->noHeaderLen = textLen; 
              sprintf(item->text, "%s %d %d %d %10d%n", name, item->type, item->count, isSystem, textLen, &cLen);
              /* get rid of trailing NULL just introduced */
              (item->text + cLen)[0] = '\n';
            }
            
            /* Store the values */
            if (copy) {
              array = malloc(len*sizeof(uint64_t));
              if (array == NULL) {payloadItemFree(item, 1); free(item); return(CMSG_OUT_OF_MEMORY);}
              memcpy(array, vals, len*sizeof(uint64_t));
              item->array = (void *)array;
            }
            else {
              item->array = (void *)vals;
            }
           break;
  }
   
  item->length = (int)strlen(item->text);
  *newItem = item;

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 8-bit, signed int array field to the
 * compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 8-bit, signed ints to add (copy)
 * @param len number of ints from array to add
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, vals, or name is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt8Array(void *vmsg, const char *name, const int8_t vals[], int len) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_INT8_A, len, 0, 1); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 16-bit, signed int array field to the
 * compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 16-bit, signed ints to add (copy)
 * @param len number of ints from array to add
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, vals, or name is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt16Array(void *vmsg, const char *name, const int16_t vals[], int len) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_INT16_A, len, 0, 1); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 32-bit, signed int array field to the
 * compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 32-bit, signed ints to add (copy)
 * @param len number of ints from array to add
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, vals, or name is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt32Array(void *vmsg, const char *name, const int32_t vals[], int len) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_INT32_A, len, 0, 1); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 64-bit, signed int array field to the
 * compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 64-bit, signed ints to add (copy)
 * @param len number of ints from array to add
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, vals, or name is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddInt64Array(void *vmsg, const char *name, const int64_t vals[], int len) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_INT64_A, len, 0, 1); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 8-bit, unsigned int array field to the
 * compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 8-bit, unsigned ints to add (copy)
 * @param len number of ints from array to add
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, vals, or name is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint8Array(void *vmsg, const char *name, const uint8_t vals[], int len) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_UINT8_A, len, 0, 1); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 16-bit, unsigned int array field to the
 * compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 16-bit, unsigned ints to add (copy)
 * @param len number of ints from array to add
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, vals, or name is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint16Array(void *vmsg, const char *name, const uint16_t vals[], int len) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_UINT16_A, len, 0, 1); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 32-bit, unsigned int array field to the
 * compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 32-bit, unsigned ints to add (copy)
 * @param len number of ints from array to add
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, vals, or name is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint32Array(void *vmsg, const char *name, const uint32_t vals[], int len) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_UINT32_A, len, 0, 1); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named, 64-bit, unsigned int array field to the
 * compound payload of a message.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vals array of 64-bit, unsigned ints to add (copy)
 * @param len number of ints from array to add
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, vals, or name is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddUint64Array(void *vmsg, const char *name, const uint64_t vals[], int len) {
   return addIntArray(vmsg, name, (const int *)vals, CMSG_CP_UINT64_A, len, 0, 1); 
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds/overwrites a named string field to the compound payload of a message.
 * Used internally with control over adding fields with names starting with "cmsg".
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val string field to add
 * @param isSystem if true allows using names starting with "cmsg", else not
 * @param copy if true, copy the string "val", else record the pointer and assume ownership
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, val or name is NULL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 */
static int addString(void *vmsg, const char *name, const char *val, int isSystem, int copy) {
  payloadItem *item;
  int textLen, valLen;
  size_t totalLen;
  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL || name == NULL || val == NULL) return(CMSG_BAD_ARGUMENT);
  if (!isValidFieldName(name, isSystem))          return(CMSG_BAD_FORMAT);
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
      payloadItemFree(item, 1);
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
  valLen = (int)strlen(val);
   
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
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  sprintf(item->text, "%s %d %d %d %d\n%d\n%s\n", name, item->type, item->count,
                                                  isSystem, textLen, valLen, val);
  item->length = (int)strlen(item->text);

  /* remove any existing item with that name */
  if (cMsgPayloadContainsName(vmsg, name)) {
      removeItem(msg, name, NULL);
  }
  
  /* place payload item in msg's linked list */
  addItem(msg, item);
  
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
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, val or name is NULL
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddString(void *vmsg, const char *name, const char *val) {
  return addString(vmsg, name, val, 0, 1);
}


/*-------------------------------------------------------------------*/


/**
 * This routine creates a payloadItem of the string array variety.
 * Names may not be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of item to create
 * @param val array of strings 
 * @param len number of strings to add
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 * @param copy if true, copy the strings in "vals", else record the pointer and assume ownership
 * @param pItem pointer to pointer to payloadItem which gets filled in with the created item
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, vals, or name is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 */   
static int createStringArrayItem(const char *name, const char **vals, int len,
                                 int isSystem, int copy, payloadItem **pItem) {
  int i, cLen, textLen=0;
  size_t totalLen;
  payloadItem *item;
  char *s;

  if (name == NULL || vals == NULL || len < 1) return(CMSG_BAD_ARGUMENT);
  if (!isValidFieldName(name, isSystem)) return(CMSG_BAD_FORMAT);         
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
    item->array = malloc(len*sizeof(char *));
    if (item->array == NULL) {
      payloadItemFree(item, 1);
      free(item);
      return(CMSG_OUT_OF_MEMORY);
    }

    /* copy all strings for storage */
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
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  sprintf(s, "%s %d %d %d %d\n%n", name, item->type, item->count, isSystem, textLen, &cLen);
  s += cLen;
  
  for (i=0; i<len; i++) {
    sprintf(s, "%d\n%s\n%n", (int)strlen(vals[i]), vals[i], &cLen);
    s += cLen;
  }
  item->length = (int)strlen(item->text);
  *pItem = item;
  
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds/overwrites a named string array field to the compound payload
 * of a message. Names may not be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param val strings to add
 * @param len number of strings to add
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 * @param copy if true, copy the strings in "vals", else record the pointer and assume ownership
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, vals, or name is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 */
static int addStringArray(void *vmsg, const char *name, const char **vals, int len,
                          int isSystem, int copy) {
  int err;
  payloadItem *item;
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  if (msg == NULL) return(CMSG_BAD_ARGUMENT);

  /* payload item */
  err = createStringArrayItem(name, vals, len, isSystem, copy, &item);
  if (err != CMSG_OK) return(err);
  
  /* remove any existing item with that name */
  if (cMsgPayloadContainsName(vmsg, name)) {
      removeItem(msg, name, NULL);
  }
  
  /* place payload item in msg's linked list */
  addItem(msg, item);
  
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
 * @param vals strings to add
 * @param len number of strings to add
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message, vals, or name is NULL; len < 1
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_BAD_FORMAT if name is not properly formed
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddStringArray(void *vmsg, const char *name, const char **vals, int len) {
  return addStringArray(vmsg, name, vals, len, 0, 1);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds/overwrites a named cMsg message field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 * The string representation of the message is the same format as that used for 
 * a complete compound payload.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vmessage cMsg message to add
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 * @return CMSG_BAD_FORMAT if name is not properly formed,
 *                          or if error in binary-to-text transformation
 * @return CMSG_OUT_OF_MEMORY if no more memory
 *
 */   
static int addMessage(void *vmsg, const char *name, const void *vmessage,
                      int isSystem) {
  char *s;
  int i, byte, len, lenBin=0, count=0, endian;
  int textLen=0, totalLen=0, length[11], numChars;
  int32_t j32[5];
  int64_t j64[6];
  payloadItem *item, *pItem;  
  cMsgMessage_t *msg     = (cMsgMessage_t *)vmsg;
  cMsgMessage_t *message = (cMsgMessage_t *)vmessage;

  if (msg  == NULL ||
      name == NULL ||
      message == NULL)                      return(CMSG_BAD_ARGUMENT);
  if (!isValidFieldName(name, isSystem))    return(CMSG_BAD_FORMAT);
  if (isSystem) isSystem = 1;

  memset((void *)length, 0, 11*sizeof(int));
  
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
  item->array = cMsgCopyMessage(vmessage);
  if (item->array == NULL) {
    payloadItemFree(item, 1);
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
  /* add up to 8 strings: domain, subject, type, text,   */
  /* sender, senderHost, reciever, receiverHost          */
  /* *************************************************** */
 
  /* add length of "domain" member as a string */
  if (message->domain != NULL) {
    count++;
    /* length of text following header line, for this string item */
    length[0] = (int)strlen(message->domain)
                + numDigits(strlen(message->domain), 0)
                + 2 /* 2 newlines */;
    textLen += (int)strlen("cMsgDomain")
               + 9 /* 2-digit type, 1-digit count (=1), 1-digit isSystem, 4 spaces, and 1 newline */
               + numDigits(length[0], 0) /* # chars in following, nonheader text */
               + length[0];  /* this item's nonheader text */
  }
  
  /* add length of "subject" member as a string */
  if (message->subject != NULL) {
    count++;
    length[1] = (int)strlen(message->subject) + numDigits(strlen(message->subject), 0) + 2;
    textLen += (int)strlen("cMsgSubject") + 9 + numDigits(length[1], 0) + length[1];
  }
  
  /* add length of "type" member as a string */
  if (message->type != NULL) {
    count++;
    length[2] = (int)strlen(message->type) + numDigits(strlen(message->type), 0) + 2;
    textLen += (int)strlen("cMsgType") + 9 + numDigits(length[2], 0) + length[2] ;
  }
  
  /* add length of "text" member as a string */
  if (message->text != NULL) {
    count++;
    length[3] = (int)strlen(message->text) + numDigits(strlen(message->text), 0) + 2;
    textLen += (int)strlen("cMsgText") + 9 + numDigits(length[3], 0) + length[3] ;
  }
    
  /* add length of "sender" member as a string */
  if (message->sender != NULL) {
    count++;
    length[4] = (int)strlen(message->sender) + numDigits(strlen(message->sender), 0) + 2;
    textLen += (int)strlen("cMsgSender") + 9 + numDigits(length[4], 0) + length[4] ;
  }
  
  /* add length of "senderHost" member as a string */
  if (message->senderHost != NULL) {
    count++;
    length[5] = (int)strlen(message->senderHost) + numDigits(strlen(message->senderHost), 0) + 2;
    textLen += (int)strlen("cMsgSenderHost") + 9 + numDigits(length[5], 0) + length[5] ;
  }
  
  /* add length of "receiver" member as a string */
  if (message->receiver != NULL) {
    count++;
    length[6] = (int)strlen(message->receiver) + numDigits(strlen(message->receiver), 0) + 2;
    textLen += (int)strlen("cMsgReceiver") + 9 + numDigits(length[6], 0) + length[6] ;
  }
  
  /* add length of "receiverHost" member as a string */
  if (message->receiverHost != NULL) {
    count++;
    length[7] = (int)strlen(message->receiverHost) + numDigits(strlen(message->receiverHost), 0) + 2;
    textLen += (int)strlen("cMsgReceiverHost") + 9 + numDigits(length[7], 0) + length[7] ;
  }
  
  /* ************************************************************************************** */
  /* add length of 1 array of 5 ints: version, info, reserved, byteArrayLength, and userInt */
  /* ************************************************************************************** */
  
  /* length of string to contain ints */
  length[8] = 5*8 + 5 /* 4 sp, 1 nl */;
  textLen += (int)strlen("cMsgInts") + 2 + 1 + 1 + numDigits(length[8], 0) + length[8] +
             5; /* 4 sp, 1 nl */
  count++;

  /* ************************************************************************************** */
  /* add length of 1 array of 6 64-bit ints for 3 times: userTime, senderTime, receiverTime */
  /* ************************************************************************************** */
  
  /* length of string to contain times */
  length[9] = 6*16 + 6 /* 5 sp, 1 nl */;
  textLen += (int)strlen("cMsgTimes") + 2 + 1 + 1 +  numDigits(length[9], 0) + length[9] +
             5; /* 4 sp, 1 nl */
  count++;

  /* ************************** */
  /* add length of binary field */
  /* ************************** */
  if (message->byteArray != NULL) {
      /* find (exact) size of text-encoded binary data (including ending newline) */
    lenBin = cMsg_b64_encode_len(message->byteArray + message->byteArrayOffset,
                                 (unsigned int)message->byteArrayLength, 1);
    length[10] = lenBin + numDigits(lenBin, 0) +
                 numDigits(message->byteArrayLength, 0) + 4;
                 /* 1 endian, 2 spaces, 1 newline */
    
    textLen += (int)strlen("cMsgBinary") + 2 + 1 /* 1 item */ + 1 /* is system */ +
               numDigits(length[10], 0) + length[10] + 5; /* 4 spaces, 1 newline */
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
  totalLen += (int)strlen(name) + 2 /* type len */ + 1 /* 1 digit in count = 1 */
              + 1 /* isSys? */ + numDigits(textLen, 0) + textLen + 5; /* 4 spaces, 1 newline */
 
  totalLen += 1; /* for null terminator */
  
  item->noHeaderLen = textLen;
 
  s = item->text = (char *) malloc((size_t)totalLen);
  if (item->text == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }

  /* 1st, write header of message field */
  sprintf(s, "%s %d 1 %d %d\n%n", name, CMSG_CP_MSG, isSystem, textLen, &len);
  s += len;
  
  /* From here on it's the same format used as for sending payload over network */
  /* 2nd, write how many fields there are */
  sprintf(s, "%d\n%n",count, &len);
  s += len;
  
  /* next write strings */
 
  /* add message's domain member as string */
  if (message->domain != NULL) {
    sprintf(s, "%s %d 1 1 %d\n%d\n%s\n%n", "cMsgDomain", CMSG_CP_STR, length[0],
            (int)strlen(message->domain), message->domain, &len);
    s += len;
  }
  
  /* add message's subject member as string */
  if (message->subject != NULL) {
    sprintf(s, "%s %d 1 1 %d\n%d\n%s\n%n", "cMsgSubject", CMSG_CP_STR, length[1],
            (int)strlen(message->subject), message->subject, &len);
    s += len;
  }
  
  /* add message's type member as string */
  if (message->type != NULL) {
    sprintf(s, "%s %d 1 1 %d\n%d\n%s\n%n", "cMsgType", CMSG_CP_STR, length[2],
            (int)strlen(message->type), message->type, &len);
    s += len;
  }
  
  /* add message's text member as string */
  if (message->text != NULL) {
    sprintf(s, "%s %d 1 1 %d\n%d\n%s\n%n", "cMsgText", CMSG_CP_STR, length[3],
            (int)strlen(message->text), message->text, &len);
    s += len;
  }

  /* add message's sender member as string */
  if (message->sender != NULL) {
    sprintf(s, "%s %d 1 1 %d\n%d\n%s\n%n", "cMsgSender", CMSG_CP_STR, length[4],
            (int)strlen(message->sender), message->sender, &len);
    s += len;
  }

  /* add message's senderHost member as string */
  if (message->senderHost != NULL) {
    sprintf(s, "%s %d 1 1 %d\n%d\n%s\n%n", "cMsgSenderHost", CMSG_CP_STR, length[5],
            (int)strlen(message->senderHost), message->senderHost, &len);
    s += len;
  }

  /* add message's receiver member as string */
  if (message->receiver != NULL) {
    sprintf(s, "%s %d 1 %d %d\n%d\n%s\n%n", "cMsgReceiver", CMSG_CP_STR, isSystem, length[6],
            (int)strlen(message->receiver), message->receiver, &len);
    s += len;
  }

  /* add message's receiverHost member as string */
  if (message->receiverHost != NULL) {
    sprintf(s, "%s %d 1 %d %d\n%d\n%s\n%n", "cMsgReceiverHost", CMSG_CP_STR, isSystem, length[7],
            (int)strlen(message->receiverHost), message->receiverHost, &len);
    s += len;
  }

  /* next write 5 ints */
  sprintf(s, "%s %d 5 1 %d\n%n", "cMsgInts", CMSG_CP_INT32_A, length[8], &len);
  s+= len;
  j32[0] = message->version;
  j32[1] = message->info;
  j32[2] = message->reserved;
  j32[3] = message->byteArrayLength;
  j32[4] = message->userInt;
  for (i=0; i<5; i++) {
      byte = j32[i]>>24 & 0xff;
      *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
      byte = j32[i]>>16 & 0xff;
      *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
      byte = j32[i]>>8 & 0xff;
      *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
      byte = j32[i] & 0xff;
      *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
    if (i < 4) {
      *s++ = ' ';
    } else {
      *s++ = '\n';
    }
  }

  /* next write 6 64-bit ints */
  sprintf(s, "%s %d 6 1 %d\n%n", "cMsgTimes", CMSG_CP_INT64_A, length[9], &len);
  s+= len;
  j64[0] = message->userTime.tv_sec;
  j64[1] = message->userTime.tv_nsec;
  j64[2] = message->senderTime.tv_sec;
  j64[3] = message->senderTime.tv_nsec;
  j64[4] = message->receiverTime.tv_sec;
  j64[5] = message->receiverTime.tv_nsec;
  for (i=0; i<6; i++) {
      byte = (int) (j64[i]>>56 & 0xff);
      *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
      byte = (int) (j64[i]>>48 & 0xff);
      *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
      byte = (int) (j64[i]>>40 & 0xff);
      *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
      byte = (int) (j64[i]>>32 & 0xff);
      *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
      byte = (int) (j64[i]>>24 & 0xff);
      *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
      byte = (int) (j64[i]>>16 & 0xff);
      *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
      byte = (int) (j64[i]>>8 & 0xff);
      *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
      byte = (int) (j64[i] & 0xff);
      *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
      if (i < 5) {
        *s++ = ' ';
      } else {
        *s++ = '\n';
      }
  }            

  if (message->byteArray != NULL) {
    /* write first line and stop */
    sprintf(s, "%s %d 1 1 %d\n%n", "cMsgBinary", CMSG_CP_BIN, length[10], &len);
    s += len;
    
    /* write the length */
    cMsgGetByteArrayEndian(vmessage, &endian);
    sprintf(s, "%d %d %d\n%n", lenBin, message->byteArrayLength, endian, &len);
    s += len;

    /* write the binary-encoded text */
    numChars = cMsg_b64_encode(message->byteArray + message->byteArrayOffset,
                               (unsigned int)message->byteArrayLength, s, 1);
    s += numChars;
    if (lenBin != numChars) {
      payloadItemFree(item, 1);
      free(item);
      return(CMSG_BAD_FORMAT);  
    }
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
  item->length = (int)strlen(item->text);

  /* remove any existing item with that name */
  if (cMsgPayloadContainsName(vmsg, name)) {
      removeItem(msg, name, NULL);
  }
  
  /* place payload item in msg's linked list */
  addItem(msg, item);

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
 * @param vmessage cMsg message to add
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if any arg is NULL
 * @return CMSG_BAD_FORMAT if name is not properly formed,
 *                          or if error in binary-to-text transformation
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_ALREADY_EXISTS if name is being used already
 *
 */   
int cMsgAddMessage(void *vmsg, const char *name, const void *vmessage) {
  return addMessage(vmsg, name, vmessage, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds/overwrites a named field of an array of cMsg messages to the
 * compound payload of a message. Names may not begin with "cmsg" (case insensitive),
 * be longer than CMSG_PAYLOAD_NAME_LEN, or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vmessage array of cMsg messages to add
 * @param number number of messages from array to add
 * @param isSystem if = 1 allows using names starting with "cmsg", else not
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if any arg is NULL or number < 1
 * @return CMSG_BAD_FORMAT if name is not properly formed,
 *                          or if error in binary-to-text transformation
 * @return CMSG_OUT_OF_MEMORY if no more memory
 *
 */   
static int addMessageArray(void *vmsg, const char *name, const void *vmessage[],
                           int number, int isSystem) {
  char *s;
  int i, j, byte, len, lenBin[number], count[number], endian;
  int textLen=0, totalLen=0, length[number][11], numChars;
  int32_t j32[5];
  int64_t j64[6];
  void **msgArray;
  payloadItem *item, *pItem;  
  cMsgMessage_t  *message, *msg = (cMsgMessage_t *)vmsg;

  
  if (msg == NULL || name == NULL ||
      vmessage == NULL || number < 1)      return(CMSG_BAD_ARGUMENT);
  if (!isValidFieldName(name, isSystem))   return(CMSG_BAD_FORMAT);
  if (isSystem) isSystem = 1;
  
  /* payload item to add to msg */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);

  item->name = strdup(name);
  if (item->name == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type  = CMSG_CP_MSG_A;
  item->count = number;
  
  /* store original data */
  msgArray = (void **) malloc(number*sizeof(void *));
  if (msgArray == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->array = (void *)msgArray;
  for (i=0; i<number; i++) {
    msgArray[i] = cMsgCopyMessage(vmessage[i]);
    if (msgArray[i] == NULL) {
      for (j=0; j<i; j++) {
        cMsgFreeMessage(&msgArray[j]);
      }
      free(msgArray);
      payloadItemFree(item, 1);
      free(item);
      return(CMSG_OUT_OF_MEMORY);
    }
  }
  
  /* *************************************************** */
  /* First do some counting and find out how much space  */
  /* we need to store everything in message as a string. */
  /* *************************************************** */
  
  for (i=0; i<number; i++) {
      count[i] = 0;
      message  = (cMsgMessage_t *) vmessage[i];
           
      /* find length of payload */
      pItem = message->payload;
      while (pItem != NULL) {
        textLen += pItem->length;
        pItem = pItem->next;
        count[i]++;
      }

      /* *************************************************** */
      /* add up to 8 strings: domain, subject, type, text,   */
      /* sender, senderHost, reciever, receiverHost          */
      /* *************************************************** */

      /* add length of "domain" member as a string */
      if (message->domain != NULL) {
        count[i]++;
        /* length of text following header line, for this string item */
        length[i][0] = (int)strlen(message->domain)
                      + numDigits(strlen(message->domain), 0)
                      + 2 /* 2 newlines */;
        textLen += strlen("cMsgDomain")
                   + 9 /* 2-digit type, 1-digit count (=1), 1-digit isSystem, 4 spaces, and 1 newline */
                   + numDigits(length[i][0], 0) /* # chars in following, nonheader text */
                   + length[i][0];  /* this item's nonheader text */
      }

      /* add length of "subject" member as a string */
      if (message->subject != NULL) {
        count[i]++;
        length[i][1] = (int)strlen(message->subject) + numDigits(strlen(message->subject), 0) + 2;
        textLen += strlen("cMsgSubject") + 9 + numDigits(length[i][1], 0) + length[i][1];
      }

      /* add length of "type" member as a string */
      if (message->type != NULL) {
        count[i]++;
        length[i][2] = (int)strlen(message->type) + numDigits(strlen(message->type), 0) + 2;
        textLen += strlen("cMsgType") + 9 + numDigits(length[i][2], 0) + length[i][2] ;
      }

      /* add length of "text" member as a string */
      if (message->text != NULL) {
        count[i]++;
        length[i][3] = (int)strlen(message->text) + numDigits(strlen(message->text), 0) + 2;
        textLen += strlen("cMsgText") + 9 + numDigits(length[i][3], 0) + length[i][3] ;
      }

      /* add length of "sender" member as a string */
      if (message->sender != NULL) {
        count[i]++;
        length[i][4] = (int)strlen(message->sender) + numDigits(strlen(message->sender), 0) + 2;
        textLen += strlen("cMsgSender") + 9 + numDigits(length[i][4], 0) + length[i][4] ;
      }

      /* add length of "senderHost" member as a string */
      if (message->senderHost != NULL) {
        count[i]++;
        length[i][5] = (int)strlen(message->senderHost) + numDigits(strlen(message->senderHost), 0) + 2;
        textLen += strlen("cMsgSenderHost") + 9 + numDigits(length[i][5], 0) + length[i][5] ;
      }

      /* add length of "receiver" member as a string */
      if (message->receiver != NULL) {
        count[i]++;
        length[i][6] = (int)strlen(message->receiver) + numDigits(strlen(message->receiver), 0) + 2;
        textLen += strlen("cMsgReceiver") + 9 + numDigits(length[i][6], 0) + length[i][6] ;
      }

      /* add length of "receiverHost" member as a string */
      if (message->receiverHost != NULL) {
        count[i]++;
        length[i][7] = (int)strlen(message->receiverHost) + numDigits(strlen(message->receiverHost), 0) + 2;
        textLen += strlen("cMsgReceiverHost") + 9 + numDigits(length[i][7], 0) + length[i][7] ;
      }

      /* ************************************************************************************** */
      /* add length of 1 array of 5 ints: version, info, reserved, byteArrayLength, and userInt */
      /* ************************************************************************************** */

      /* length of string to contain ints */
      length[i][8] = 5*8 + 5 /* 4 sp, 1 nl */;
      textLen += strlen("cMsgInts") + 2 + 1 + 1 + numDigits(length[i][8], 0) + length[i][8] +
                 5; /* 4 sp, 1 nl */
      count[i]++;

      /* ************************************************************************************** */
      /* add length of 1 array of 6 64-bit ints for 3 times: userTime, senderTime, receiverTime */
      /* ************************************************************************************** */

      /* length of string to contain times */
      length[i][9] = 6*16 + 6 /* 5 sp, 1 nl */;
      textLen += strlen("cMsgTimes") + 2 + 1 + 1 +  numDigits(length[i][9], 0) + length[i][9] +
                 5; /* 4 sp, 1 nl */
      count[i]++;
      
      /* ************************** */
      /* add length of binary field */
      /* ************************** */
      if (message->byteArray != NULL) {
        /* find (exact) size of text-encoded binary data (including ending newline) */
        lenBin[i] = cMsg_b64_encode_len(message->byteArray + message->byteArrayOffset,
                                        (unsigned int)message->byteArrayLength, 1);
        length[i][10] = lenBin[i] + numDigits(lenBin[i], 0) +
                        numDigits(message->byteArrayLength, 0) + 4;
                        /* 1 endian, 1 space, 2 newlines */

        textLen += strlen("cMsgBinary") + 2 + 1 /* 1 item */ + 1 /* is system */ +
                   numDigits(length[i][10], 0) + length[i][10] + 5; /* 4 spaces, 1 newline */
        count[i]++;
      }

      /* ************************************************************ */
      /* string representing compound payload (#-of-fields + newline) */
      /* ************************************************************ */

      textLen += numDigits(count[i], 0) + 1;
  }
  
  /* ******************************************* */
  /* add length of header to message array field */
  /* ******************************************* */
  /* header of message field in msg's payload is "name type count isSystem? length\n" */
  totalLen += strlen(name) + 2 /* type len */ + 1 /* 1 digit in count = 1 */
              + 1 /* isSys? */ + numDigits(textLen, 0) + textLen + 5; /* 4 spaces, 1 newline */

  totalLen += 1; /* for null terminator */
  
  item->noHeaderLen = textLen;
  
  s = item->text = (char *) malloc((size_t)totalLen);
  if (item->text == NULL) {
    for (j=0; j<number; j++) {
      cMsgFreeMessage(&msgArray[j]);
    }
    free(msgArray);
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* 1st, write header of message field */
  sprintf(s, "%s %d %d %d %d\n%n", name, CMSG_CP_MSG_A, number, isSystem, textLen, &len);
  s += len;
  
  /* for each message ... */
  for (i=0; i<number; i++) {  
    message = (cMsgMessage_t *) vmessage[i];
    
    /* From here on it's the same format used as for sending payload over network. */
    /* Write how many fields there are. */
    sprintf(s, "%d\n%n",count[i], &len);
    s += len;

    /* next write strings */

    /* add message's domain member as string */
    if (message->domain != NULL) {
      sprintf(s, "%s %d 1 1 %d\n%d\n%s\n%n", "cMsgDomain", CMSG_CP_STR, length[i][0],
              (int)strlen(message->domain), message->domain, &len);
      s += len;
    }

    /* add message's subject member as string */
    if (message->subject != NULL) {
      sprintf(s, "%s %d 1 1 %d\n%d\n%s\n%n", "cMsgSubject", CMSG_CP_STR, length[i][1],
              (int)strlen(message->subject), message->subject, &len);
      s += len;
    }

    /* add message's type member as string */
    if (message->type != NULL) {
      sprintf(s, "%s %d 1 1 %d\n%d\n%s\n%n", "cMsgType", CMSG_CP_STR, length[i][2],
              (int)strlen(message->type), message->type, &len);
      s += len;
    }

    /* add message's text member as string */
    if (message->text != NULL) {
      sprintf(s, "%s %d 1 1 %d\n%d\n%s\n%n", "cMsgText", CMSG_CP_STR, length[i][3],
              (int)strlen(message->text), message->text, &len);
      s += len;
    }

    /* add message's sender member as string */
    if (message->sender != NULL) {
      sprintf(s, "%s %d 1 1 %d\n%d\n%s\n%n", "cMsgSender", CMSG_CP_STR, length[i][4],
              (int)strlen(message->sender), message->sender, &len);
      s += len;
    }

    /* add message's senderHost member as string */
    if (message->senderHost != NULL) {
      sprintf(s, "%s %d 1 1 %d\n%d\n%s\n%n", "cMsgSenderHost", CMSG_CP_STR, length[i][5],
              (int)strlen(message->senderHost), message->senderHost, &len);
      s += len;
    }

    /* add message's receiver member as string */
    if (message->receiver != NULL) {
      sprintf(s, "%s %d 1 %d %d\n%d\n%s\n%n", "cMsgReceiver", CMSG_CP_STR, isSystem, length[i][6],
              (int)strlen(message->receiver), message->receiver, &len);
      s += len;
    }

    /* add message's receiverHost member as string */
    if (message->receiverHost != NULL) {
      sprintf(s, "%s %d 1 %d %d\n%d\n%s\n%n", "cMsgReceiverHost", CMSG_CP_STR, isSystem, length[i][7],
              (int)strlen(message->receiverHost), message->receiverHost, &len);
      s += len;
    }

    /* next write 5 ints */
    sprintf(s, "%s %d 5 1 %d\n%n", "cMsgInts", CMSG_CP_INT32_A, length[i][8], &len);
    s+= len;
    j32[0] = message->version;
    j32[1] = message->info;
    j32[2] = message->reserved;
    j32[3] = message->byteArrayLength;
    j32[4] = message->userInt;
    for (j=0; j<5; j++) {
        byte = j32[j]>>24 & 0xff;
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = j32[j]>>16 & 0xff;
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = j32[j]>>8 & 0xff;
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = j32[j] & 0xff;
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
      if (j < 4) {
        *s++ = ' ';
      } else {
        *s++ = '\n';
      }
    }

    /* next write 6 64-bit ints */
    sprintf(s, "%s %d 6 1 %d\n%n", "cMsgTimes", CMSG_CP_INT64_A, length[i][9], &len);
    s+= len;
    j64[0] = message->userTime.tv_sec;
    j64[1] = message->userTime.tv_nsec;
    j64[2] = message->senderTime.tv_sec;
    j64[3] = message->senderTime.tv_nsec;
    j64[4] = message->receiverTime.tv_sec;
    j64[5] = message->receiverTime.tv_nsec;
    for (j=0; j<6; j++) {
        byte = (int) (j64[j]>>56 & 0xff);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = (int) (j64[j]>>48 & 0xff);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = (int) (j64[j]>>40 & 0xff);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = (int) (j64[j]>>32 & 0xff);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = (int) (j64[j]>>24 & 0xff);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = (int) (j64[j]>>16 & 0xff);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = (int) (j64[j]>>8 & 0xff);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        byte = (int) (j64[j] & 0xff);
        *s++ = toASCII[byte][0]; *s++ = toASCII[byte][1];
        if (j < 5) {
          *s++ = ' ';
        } else {
          *s++ = '\n';
        }
    }            
    
    if (message->byteArray != NULL) {
      /* write first line and stop */
      sprintf(s, "%s %d 1 1 %d\n%n", "cMsgBinary", CMSG_CP_BIN, length[i][10], &len);
      s += len;

      /* write the lengths */
      cMsgGetByteArrayEndian(vmessage[i], &endian);
      sprintf(s, "%d %d %d\n%n", lenBin[i], message->byteArrayLength, endian, &len);
      s += len;

      /* write the binary-encoded text */
      numChars = cMsg_b64_encode(message->byteArray + message->byteArrayOffset,
                                 (unsigned int)message->byteArrayLength, s, 1);
      s += numChars;
      if (lenBin[i] != numChars) {
        for (j=0; j<number; j++) {
          cMsgFreeMessage(&msgArray[j]);
        }
        free(msgArray);
        payloadItemFree(item, 1);
        free(item);
        return(CMSG_BAD_FORMAT);  
      }
    }

    /* add payload fields */
    pItem = message->payload;
    while (pItem != NULL) {
      sprintf(s, "%s%n", pItem->text, &len);
      pItem = pItem->next;
      s += len;
    }
    
  } /* for each message ... */
  
  /* add null terminator */
  s[0] = '\0';
  
  /* store length of text */
  item->length = (int)strlen(item->text);
/*printf("MSG ARRAY TXT:\n%s", item->text); */ 
  
  /* remove any existing item with that name */
  if (cMsgPayloadContainsName(vmsg, name)) {
      removeItem(msg, name, NULL);
  }
  
  /* place payload item in msg's linked list */
  addItem(msg, item);
    
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named field of an array of cMsg messages to the
 * compound payload of a message. Names may not begin with "cmsg" (case insensitive),
 * be longer than CMSG_PAYLOAD_NAME_LEN, or contain white space or quotes.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param vmessage array of cMsg messages to add
 * @param len number of messages from array to add
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_ARGUMENT if message array, src, or name is NULL; len < 1
 * @return CMSG_BAD_FORMAT if name is not properly formed,
 *                          or if error in binary-to-text transformation
 * @return CMSG_OUT_OF_MEMORY if no more memory
 * @return CMSG_ALREADY_EXISTS if name is being used already
 */   
int cMsgAddMessageArray(void *vmsg, const char *name, const void *vmessage[], int len) {
  return addMessageArray(vmsg, name, vmessage, len, 0);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named field of binary data, from a string encoding of that
 * data, to the compound payload of a message. The text representation of the
 * payload item is copied in (doesn't need to be generated).
 * Since this routine is only used internally, there are no argument checks.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param type type of field to add
 * @param count number of binary data items
 * @param isSystem if = 0, add item to payload, else set system parameters
 * @param pVals pointer to "value" part of text (after header)
 * @param pText pointer to all of text (before header)
 * @param textLen length (in chars) of all text
 * @param noHeaderLen length (in chars) of text without header line
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_FORMAT if text being parsed is in wrong format
 * @return CMSG_OUT_OF_MEMORY if no more memory
 *
 */
static int addBinaryFromText(void *vmsg, char *name, int type, int count, int isSystem,
                             const char *pVal, const char *pText, int textLen,
                             int noHeaderLen) {
  char *s;
  int numBytes, debug=0, lenEncoded, lenBin, endian;
  payloadItem *item;
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  
  
  /* read length of string (encoded binary), length of original binary, & endian */
  sscanf(pVal, "%d %d %d", &lenEncoded, &lenBin, &endian);
  if (lenEncoded < 1 || lenBin < 1) return(CMSG_BAD_FORMAT);
if(debug) printf("  encoded len = %d, bin len = %d, endian = %d\n", lenEncoded, lenBin, endian);
  
  /* go to encoded txt */
  s = strpbrk(pVal, "\n");
  if (s == NULL) return(CMSG_BAD_FORMAT);
  pVal = s+1;

  /* Setting the regular binary array in a msg */
  if (isSystem && strcmp(name, "cMsgBinary") == 0) {
    /* Place binary data into message's binary array - not in the payload.
       First use or remove any existing array (only if it was copied into msg). */
    if ((msg->byteArray != NULL) && ((msg->bits & CMSG_BYTE_ARRAY_IS_COPIED) > 0)) {
      /* if there is not enough space available, allocate mem */
      if (msg->byteArrayLength < lenBin) {
        free(msg->byteArray);
        msg->byteArray = (char *) malloc((size_t)lenBin);
        if (msg->byteArray == NULL) {
          return (CMSG_OUT_OF_MEMORY);
        }
      }
    }
    else {
      msg->byteArray = (char *) malloc((size_t)lenBin);
      if (msg->byteArray == NULL) {
        return (CMSG_OUT_OF_MEMORY);
      }
    }
    msg->byteArrayOffset = 0;
    msg->byteArrayLength = lenBin;
    msg->byteArrayLengthFull = lenBin;

    numBytes = cMsg_b64_decode(pVal, (unsigned int)lenEncoded, msg->byteArray);
    if (numBytes < 0 || numBytes != lenBin) {
if (debug) printf("addBinaryFromString: decoded string len = %d, should be %d\n", numBytes, lenBin);
      free(msg->byteArray);
      return(CMSG_BAD_FORMAT);
    }
    msg->bits |= CMSG_BYTE_ARRAY_IS_COPIED; /* byte array IS copied */
    cMsgSetByteArrayEndian(vmsg, endian);

    return(CMSG_OK);
  }
  
  /* Setting a compound binary array in a msg */
  
  /* payload item */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);

  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }

  item->type   = type;
  item->size   = lenBin;
  item->count  = count;
  item->endian = endian;
  item->noHeaderLen = noHeaderLen;

  /* create space for binary array */
if (debug) printf("addBinaryFromString: will reserve %d bytes, calculation shows %d bytes\n",
                   lenBin, cMsg_b64_decode_len(pVal,(unsigned int) lenEncoded));
                   
  item->array = malloc((size_t)lenBin);
  if (item->array == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }

  /* decode text into binary */
  numBytes = cMsg_b64_decode(pVal, (unsigned int)lenEncoded, (char *)item->array);
  if (numBytes < 0 || numBytes != lenBin) {
    if (debug && numBytes != lenBin)
        printf("addBinaryFromString: decoded string len = %d, should be %d\n",
               numBytes, lenBin);
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_BAD_FORMAT);
  }

  /* space for text rep */
  s = item->text = (char *) malloc((size_t)(textLen+1));
  if (item->text == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* copy in text */
  memcpy(s, pText, (size_t)textLen);
  
  /* add null terminator */
  s[textLen] = '\0';
  
  /* store length of text */
  item->length = (int)strlen(item->text);

  /* place payload item in msg's linked list */
  addItem(msg, item);

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named field of an array of binary data items,
 * from a string encoding of that
 * data, to the compound payload of a message. The text representation of the
 * payload item is copied in (doesn't need to be generated).
 * Since this routine is only used internally, there are no argument checks.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param type type of field to add
 * @param count size in bytes of original binary data
 * @param isSystem if = 0, add item to payload, else set system parameters
 * @param pVals pointer to "value" part of text (after header)
 * @param pText pointer to all of text (before header)
 * @param textLen length (in chars) of all text
 * @param noHeaderLen length (in chars) of text without header line
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_FORMAT if text being parsed is in wrong format
 * @return CMSG_OUT_OF_MEMORY if no more memory
 *
 */
static int addBinaryArrayFromText(void *vmsg, char *name, int type, int count, int isSystem,
                                  const char *pVal, const char *pText, int textLen,
                                  int noHeaderLen) {
  char *s, **binArray;
  const char *t;
  int i, j, numBytes, debug=0, lenEncoded, lenBin, endian, *endians, *sizes;
  payloadItem *item;
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  /* start after header, first item is length */
  t = pVal;
  s = strpbrk(t, "\n");
  if (s == NULL) return(CMSG_BAD_FORMAT);

  binArray = (char **) malloc(count*sizeof(char *));
  if (binArray == NULL) return(CMSG_OUT_OF_MEMORY);

  endians = (int *) malloc(count*sizeof(int));
  if (endians == NULL) {
      free(binArray);
      return(CMSG_OUT_OF_MEMORY);
  }

  sizes = (int *) malloc(count*sizeof(int));
  if (sizes == NULL) {
      free(binArray);free(endians);
      return(CMSG_OUT_OF_MEMORY);
  }

  for (j=0; j<count; j++) {
      /* read length of string (encoded binary), length of original binary, & endian */
      sscanf(t, "%d %d %d", &lenEncoded, &lenBin, &endian);
/*printf("addBinArrayFromTxt: lenEncoded = %d, lenBin = %d\n", lenEncoded, lenBin);*/
      if (lenEncoded < 1 || lenBin < 1) {
          for (i=0; i<j; i++) {
              free(binArray[i]);
          }
          free(binArray);free(endians);free(sizes);
          return(CMSG_BAD_FORMAT);
      }
      if (debug) printf("  encoded len = %d, bin len = %d, endian = %d\n", lenEncoded, lenBin, endian);
      t = s+1;

      /* create space for binary array */
      binArray[j] = (char *)malloc((size_t)lenBin);
      if (binArray[j] == NULL) {
          for (i=0; i<j; i++) {
              free(binArray[i]);
          }
          free(binArray);free(endians);free(sizes);
          return(CMSG_OUT_OF_MEMORY);
      }

      if (debug) printf("addBinArrayFromTxt: will reserve %d bytes, calculation shows %d bytes\n",
                        lenBin, cMsg_b64_decode_len(t, (unsigned int)lenEncoded));
    
      /* decode text into binary */
      numBytes = cMsg_b64_decode(t, (unsigned int)lenEncoded, binArray[j]);
      if (numBytes < 0 || numBytes != lenBin) {
if (debug && numBytes != lenBin)
    printf("addBinaryArrayFromText: decoded string len = %d, should be %d\n", numBytes, lenBin);
          for (i=0; i<=j; i++) {
              free(binArray[i]);
          }
          free(binArray);free(endians);free(sizes);
          return(CMSG_BAD_FORMAT);
      }

      sizes[j]   = lenBin;
      endians[j] = endian;

      /* lenEncoded includes the following newline */
      t += lenEncoded;
      s = strpbrk(t, "\n");
      /* s will be null if it's the very last item */
      if (s == NULL  && j != count-1) return(CMSG_BAD_FORMAT);
  }

  /* payload item to add to msg */
  item = (payloadItem *) malloc(sizeof(payloadItem));
  if (item == NULL) {
      for (i=0; i<count; i++) {
          free(binArray[i]);
      }
      free(binArray);free(endians);free(sizes);
      return(CMSG_OUT_OF_MEMORY);
  }
  payloadItemInit(item);
  
  /* store strings in payload */
  item->array = binArray;
  item->type  = type;
  item->sizes = sizes;
  item->count = count;
  item->endians = endians;
  item->noHeaderLen = noHeaderLen;
  item->name = strdup(name);
  if (item->name == NULL) {
      payloadItemFree(item, 1);
      free(item);
      return(CMSG_OUT_OF_MEMORY);
  }
 
  s = item->text = (char *) malloc((size_t)(textLen+1));
  if (item->text == NULL) {
      payloadItemFree(item, 1);
      free(item);
      return(CMSG_OUT_OF_MEMORY);
  }
  
  /* copy in text */
  memcpy(s, pText, (size_t)textLen);
    
  /* add null terminator */
  s[textLen] = '\0';
  
  /* store length of text */
  item->length = (int)strlen(item->text);
  
  /* place payload item in msg's linked list */
  addItem(msg, item);

  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named field of an integer to the compound payload
 * of a message. The text representation of the payload item is copied in
 * (doesn't need to be generated).
 * Since this routine is only used internally, there are no argument checks.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param type type of field to add
 * @param count number of elements if field is array
 * @param isSystem if = 0, add item to payload, else set system parameters
 * @param pVal pointer to "value" part of text (after header)
 * @param pText text form of cMsg message to add
 * @param textLen length (in chars) of pText arg
 * @param noHeaderLen length (in chars) of pText without header line
 *
 * @return CMSG_OK if successful
 * @return CMSG_OUT_OF_MEMORY if no more memory

 */   
static int addIntFromText(void *vmsg, char *name, int type, int count, int isSystem,
                          const char *pVal, const char *pText, int textLen, int noHeaderLen) {
  char *s;
  int debug=0;
  int64_t   int64;
  uint64_t uint64;
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  /* Since this routine is used only internally, all the normal arg checks are gone. */  
  /* payload item to add to msg */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);

  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type  = type;
  item->count = count;
  item->noHeaderLen = noHeaderLen;
  
  if (type == CMSG_CP_UINT64) {
    sscanf(pVal, "%llu", &uint64);
    item->val = (int64_t)uint64;
if(debug) printf("read int as %llu\n", uint64);
  }
  else {
    sscanf(pVal, "%lld", &int64);
    item->val = int64;
if(debug) printf("read int as %lld\n", int64);
  }

  /* system field used to set the max # of history entries in msg */
  if (isSystem && strcmp(name, "cMsgHistoryLengthMax") == 0) {
    msg->historyLengthMax = (int) item->val;
  }

  s = item->text = (char *) malloc((size_t)(textLen+1));
  if (item->text == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* copy in text */
  memcpy(s, pText, (size_t)textLen);
    
  /* add null terminator */
  s[textLen] = '\0';
  
  /* store length of text */
  item->length = (int)strlen(item->text);
  
  /* place payload item in msg's linked list */
  addItem(msg, item);
    
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named field of an array of integers to the compound payload
 * of a message. The text representation of the array is copied in (doesn't
 * need to be generated).
 * Since this routine is only used internally, there are no argument checks.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param type type of field to add
 * @param count number of elements if field is array
 * @param isSystem if = 0, add item to payload, else set system parameters
 * @param pVals pointer to "value" part of text (after header)
 * @param pText pointer to all of text (before header)
 * @param textLen length (in chars) of all text
 * @param noHeaderLen length (in chars) of text without header line
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_FORMAT if text being parsed is in wrong format
 * @return CMSG_OUT_OF_MEMORY if no more memory
 *
 */
static int addIntArrayFromText(void *vmsg, char *name, int type, int count, int isSystem,
                               const char *pVal, const char *pText, int textLen, int noHeaderLen) {
  char *s;
  int32_t j, k, debug=0, zeros=0;
  payloadItem *item=NULL;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  /* payload item to add to msg */
  if (!isSystem || (strcmp(name, "cMsgInts") != 0 &&
                    strcmp(name, "cMsgTimes") != 0)) {
    item = (payloadItem *) calloc(1, sizeof(payloadItem));
    if (item == NULL) return(CMSG_OUT_OF_MEMORY);
    payloadItemInit(item);

    item->name = strdup(name);
    if (item->name == NULL) {
      free(item);
      return(CMSG_OUT_OF_MEMORY);
    }
    item->type  = type;
    item->count = count;
    item->noHeaderLen = noHeaderLen;
  }
  
  /* 8-bit int arrays */
  if (type == CMSG_CP_INT8_A || type == CMSG_CP_UINT8_A) {
    uint8_t *myArray;
    myArray = (uint8_t *)malloc(count*sizeof(uint8_t));
    if (myArray == NULL) {
      if (item != NULL) {
        payloadItemFree(item, 1);
        free(item);
      }
      return(CMSG_OUT_OF_MEMORY);
    }

    for (j=0; j<count; j++) {
      myArray[j] = (uint8_t)((toByte[(int)(*pVal)]<<4) | (toByte[(int)(*(pVal+1))]));
      pVal += 3;
if(debug) {
  if (type == CMSG_CP_UINT8_A)
    printf("  uint8[%d] = %d\n", j, (int)myArray[j]);
  else
    printf("  int8[%d] = %d\n", j, (int)((int8_t)myArray[j]));
}
    }
    item->array = (void *)myArray;
  }
  
  /* 16-bit int arrays */
  else if (type == CMSG_CP_INT16_A || type == CMSG_CP_UINT16_A) {
    uint16_t *myArray;
    myArray = (uint16_t *)malloc(count*sizeof(uint16_t));
    if (myArray == NULL) {
      if (item != NULL) {
        payloadItemFree(item, 1);
        free(item);
      }
      return(CMSG_OUT_OF_MEMORY);
    }

    for (j=0; j<count; j++) {
      /* Convert from 8 chars (representing hex) to int.
       * if we got a Z to start with, uncompress all the zeros. */
      if ( toByte[(int)(*pVal)] == -2 ) {
        zeros = 0;
        zeros = ((toByte[(int)(*(pVal+1))]<<8 ) |
                 (toByte[(int)(*(pVal+2))]<<4 ) |
                 (toByte[(int)(*(pVal+3))]    ));
if(debug) printf("  unpacking %d zeros\n", zeros);
        /* expand zeros */
        for (k=0; k<zeros; k++) {
          myArray[j+k] = 0;
if(debug) {
  if (type == CMSG_CP_UINT8_A)
    printf("  uint16[%d] = %hu\n", j+k, myArray[j+k]);
  else
    printf("  int16[%d] = %hd\n", j+k, (int16_t)myArray[j+k]);
}
        }
        j += zeros - 1;
        pVal += 5;
        continue;
      }

      myArray[j] = (uint8_t)((toByte[(int)(*(pVal  ))]<<12) |
                             (toByte[(int)(*(pVal+1))]<<8 ) |
                             (toByte[(int)(*(pVal+2))]<<4 ) |
                             (toByte[(int)(*(pVal+3))]    ));
      pVal += 5;
if(debug) {
  if (type == CMSG_CP_UINT8_A)
    printf("  uint16[%d] = %hu\n", j, myArray[j]);
  else
    printf("  int16[%d] = %hd\n", j, (int16_t)myArray[j]);
}

    }
    item->array = (void *)myArray;
  }

  /* 32-bit, int array */
  else if (type == CMSG_CP_INT32_A || type == CMSG_CP_UINT32_A) {
    int32_t *myArray;
    myArray = (int32_t *)malloc(count*sizeof(int32_t));
    if (myArray == NULL) {
      if (item != NULL) {
        payloadItemFree(item, 1);
        free(item);
      }
      return(CMSG_OUT_OF_MEMORY);
    }

    for (j=0; j<count; j++) {
      /* Convert from 8 chars (representing hex) to int.
       * if we got a Z to start with, uncompress all the zeros. */
      if ( toByte[(int)(*pVal)] == -2 ) {
        zeros = 0;
        zeros = ((toByte[(int)(*(pVal+1))]<<24) |
                 (toByte[(int)(*(pVal+2))]<<20) |
                 (toByte[(int)(*(pVal+3))]<<16) |
                 (toByte[(int)(*(pVal+4))]<<12) |
                 (toByte[(int)(*(pVal+5))]<<8 ) |
                 (toByte[(int)(*(pVal+6))]<<4 ) |
                 (toByte[(int)(*(pVal+7))]    ));
if(debug) printf("  unpacking %d zeros\n", zeros);
        /* expand zeros */
        for (k=0; k<zeros; k++) {
          myArray[j+k] = 0;
if(debug) {
  if (type == CMSG_CP_UINT32_A)
    printf("  uint32[%d] = %u\n", j+k, (uint32_t)myArray[j+k]);
  else
    printf("  int32[%d] = %d\n", j+k, myArray[j+k]);
}
        }
        j += zeros - 1;
        pVal += 9;
        continue;
      }

      myArray[j] = ((toByte[(int)(*pVal)]    <<28) |
                    (toByte[(int)(*(pVal+1))]<<24) |
                    (toByte[(int)(*(pVal+2))]<<20) |
                    (toByte[(int)(*(pVal+3))]<<16) |
                    (toByte[(int)(*(pVal+4))]<<12) |
                    (toByte[(int)(*(pVal+5))]<<8 ) |
                    (toByte[(int)(*(pVal+6))]<<4 ) |
                    (toByte[(int)(*(pVal+7))]    ));
      pVal+=9;
if(debug) {
  if (type == CMSG_CP_UINT32_A)
    printf("  uint32[%d] = %u\n", j, (uint32_t)myArray[j]);
  else
    printf("  int32[%d] = %d\n", j, myArray[j]);
}

    }

    if (isSystem && strcmp(name, "cMsgInts") == 0) {
      if (count != 5) {
        free(myArray);
        return(CMSG_BAD_FORMAT);
      }
      msg->version         = myArray[0];
      msg->info            = myArray[1];
      msg->reserved        = myArray[2];
      msg->byteArrayLength = myArray[3];
      msg->byteArrayLengthFull = myArray[3];
      msg->userInt         = myArray[4];
      free(myArray);
      
      return(CMSG_OK);
    }

    item->array = (void *)myArray;
  }

  /* 64-bit, int array */
  else if (type == CMSG_CP_INT64_A || type == CMSG_CP_UINT64_A) {
    int64_t *myArray;
    myArray = (int64_t *)malloc(count*sizeof(int64_t));
    if (myArray == NULL) {
      if (item != NULL) {
        payloadItemFree(item, 1);
        free(item);
      }
      return(CMSG_OUT_OF_MEMORY);
    }

    for (j=0; j<count; j++) {
      /* Convert from 16 chars (representing hex) to 64 bit int.
       * if we got a Z to start with, uncompress all the zeros. */
      if ( toByte[(int)(*pVal)] == -2 ) {
        zeros = 0;
        zeros = (int)(((int64_t)toByte[(int)(*(pVal+1))] <<56) |
                 ((int64_t)toByte[(int)(*(pVal+2))] <<52) |
                 ((int64_t)toByte[(int)(*(pVal+3))] <<48) |
                 ((int64_t)toByte[(int)(*(pVal+4))] <<44) |
                 ((int64_t)toByte[(int)(*(pVal+5))] <<40) |
                 ((int64_t)toByte[(int)(*(pVal+6))] <<36) |
                 ((int64_t)toByte[(int)(*(pVal+7))] <<32) |
                 ((int64_t)toByte[(int)(*(pVal+8))] <<28) |
                 ((int64_t)toByte[(int)(*(pVal+9))] <<24) |
                 ((int64_t)toByte[(int)(*(pVal+10))]<<20) |
                 ((int64_t)toByte[(int)(*(pVal+11))]<<16) |
                 ((int64_t)toByte[(int)(*(pVal+12))]<<12) |
                 ((int64_t)toByte[(int)(*(pVal+13))]<<8)  |
                 ((int64_t)toByte[(int)(*(pVal+14))]<<4)  |
                 ((int64_t)toByte[(int)(*(pVal+15))]   ));

      /* expand zeros */
      for (k=0; k<zeros; k++) {
        myArray[j+k] = 0;
if(debug) {
  if (type == CMSG_CP_UINT64_A)
    printf("  int64[%d] = %llu\n", j+k, (uint64_t)myArray[j+k]);
  else
    printf("  int64[%d] = %lld\n", j+k, myArray[j+k]);
}
      }
        j += zeros - 1;
        pVal += 17;
        continue;
      }

      /* convert from 16 chars (representing hex) to 64 bit int */
      myArray[j] = (((int64_t)toByte[(int)(*pVal)]     <<60) |
                    ((int64_t)toByte[(int)(*(pVal+1))] <<56) |
                    ((int64_t)toByte[(int)(*(pVal+2))] <<52) |
                    ((int64_t)toByte[(int)(*(pVal+3))] <<48) |
                    ((int64_t)toByte[(int)(*(pVal+4))] <<44) |
                    ((int64_t)toByte[(int)(*(pVal+5))] <<40) |
                    ((int64_t)toByte[(int)(*(pVal+6))] <<36) |
                    ((int64_t)toByte[(int)(*(pVal+7))] <<32) |
                    ((int64_t)toByte[(int)(*(pVal+8))] <<28) |
                    ((int64_t)toByte[(int)(*(pVal+9))] <<24) |
                    ((int64_t)toByte[(int)(*(pVal+10))]<<20) |
                    ((int64_t)toByte[(int)(*(pVal+11))]<<16) |
                    ((int64_t)toByte[(int)(*(pVal+12))]<<12) |
                    ((int64_t)toByte[(int)(*(pVal+13))]<<8)  |
                    ((int64_t)toByte[(int)(*(pVal+14))]<<4)  |
                    ((int64_t)toByte[(int)(*(pVal+15))]   ));
      pVal += 17;
if(debug) {
  if (type == CMSG_CP_UINT64_A)
    printf("  int64[%d] = %llu\n", j, (uint64_t)myArray[j]);
  else
    printf("  int64[%d] = %lld\n", j, myArray[j]);
}
    }

    if (isSystem && strcmp(name, "cMsgTimes") == 0) {
      if (count != 6) {
        free(myArray);
        return(CMSG_BAD_FORMAT);
      }
      msg->userTime.tv_sec      = myArray[0];
      msg->userTime.tv_nsec     = myArray[1];
      msg->senderTime.tv_sec    = myArray[2];
      msg->senderTime.tv_nsec   = myArray[3];
      msg->receiverTime.tv_sec  = myArray[4];
      msg->receiverTime.tv_nsec = myArray[5];
      free(myArray);
      
      return(CMSG_OK);
    }
           
    item->array = (void *)myArray;
  } /* 64 bit array */

  s = item->text = (char *) malloc((size_t)textLen+1);
  if (item->text == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }

  /* copy in text */
  memcpy(s, pText, (size_t)textLen);

  /* add null terminator */
  s[textLen] = '\0';

  /* store length of text */
  item->length = (int)strlen(item->text);

  /* place payload item in msg's linked list */
  addItem(msg, item);
    
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named field of a real number to the compound payload
 * of a message. The text representation of the payload item is copied in
 * (doesn't need to be generated).
 * Since this routine is only used internally, there are no argument checks.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param type type of field to add
 * @param count number of elements if field is array
 * @param isSystem if = 0, add item to payload, else set system parameters
 * @param pVal pointer to "value" part of text (after header)
 * @param pText text form of cMsg message to add
 * @param textLen length (in chars) of msgText arg
 * @param noHeaderLen length (in chars) of msgText without header line
 *
 * @return CMSG_OK if successful
 * @return CMSG_OUT_OF_MEMORY if no more memory

 */   
static int addRealFromText(void *vmsg, char *name, int type, int count, int isSystem,
                           const char *pVal, const char *pText, int textLen, int noHeaderLen) {
  char *s;
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  intFloatUnion  fun;
  intDoubleUnion dun;
  
  /* payload item to add to msg */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);

  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type  = type;
  item->count = count;
  item->noHeaderLen = noHeaderLen;
  
  if (type == CMSG_CP_DBL) {
    /* convert from 16 chars (representing hex) to double */
    dun.i = (((uint64_t)toByte[(int)(*pVal)]     <<60) |
             ((uint64_t)toByte[(int)(*(pVal+1))] <<56) |
             ((uint64_t)toByte[(int)(*(pVal+2))] <<52) |
             ((uint64_t)toByte[(int)(*(pVal+3))] <<48) |
             ((uint64_t)toByte[(int)(*(pVal+4))] <<44) |
             ((uint64_t)toByte[(int)(*(pVal+5))] <<40) |
             ((uint64_t)toByte[(int)(*(pVal+6))] <<36) |
             ((uint64_t)toByte[(int)(*(pVal+7))] <<32) |
             ((uint64_t)toByte[(int)(*(pVal+8))] <<28) |
             ((uint64_t)toByte[(int)(*(pVal+9))] <<24) |
             ((uint64_t)toByte[(int)(*(pVal+10))]<<20) |
             ((uint64_t)toByte[(int)(*(pVal+11))]<<16) |
             ((uint64_t)toByte[(int)(*(pVal+12))]<<12) |
             ((uint64_t)toByte[(int)(*(pVal+13))]<<8)  |
             ((uint64_t)toByte[(int)(*(pVal+14))]<<4)  |
             ((uint64_t)toByte[(int)(*(pVal+15))]   ));
    item->dval = dun.d;
  }
  else {
    float flt;
    /* convert from 8 chars (representing hex) to float */
    fun.i = (uint32_t)((toByte[(int)(*pVal)]    <<28) |
             (toByte[(int)(*(pVal+1))]<<24) |
             (toByte[(int)(*(pVal+2))]<<20) |
             (toByte[(int)(*(pVal+3))]<<16) |
             (toByte[(int)(*(pVal+4))]<<12) |
             (toByte[(int)(*(pVal+5))]<<8 ) |
             (toByte[(int)(*(pVal+6))]<<4 ) |
             (toByte[(int)(*(pVal+7))]    ));
    flt = fun.f;
    item->dval = (double) flt;
  }
  
  s = item->text = (char *) malloc((size_t)(textLen+1));
  if (item->text == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* copy in text */
  memcpy(s, pText, (size_t)textLen);
    
  /* add null terminator */
  s[textLen] = '\0';
  
  /* store length of text */
  item->length = (int)strlen(item->text);
  
  /* place payload item in msg's linked list */
  addItem(msg, item);
    
  return(CMSG_OK);
}
  

/*-------------------------------------------------------------------*/


/**
 * This routine adds a named field of an array of reals to the compound payload
 * of a message. The text representation of the array is copied in (doesn't
 * need to be generated). Zero suppression is implemented where contiguous
 * zeros are replaced by one string of hex starting with Z and the rest of
 * the chars containing the number of zeros.
 * Since this routine is only used internally, there are no argument checks.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param type type of field to add
 * @param count number of elements if field is array
 * @param isSystem if = 0, add item to payload, else set system parameters
 * @param pVals pointer to "value" part of text (after header)
 * @param pText pointer to all of text (before header)
 * @param textLen length (in chars) of all text
 * @param noHeaderLen length (in chars) of text without header line
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_FORMAT if text being parsed is in wrong format
 * @return CMSG_OUT_OF_MEMORY if no more memory
 *
 */
static int addRealArrayFromText(void *vmsg, char *name, int type, int count, int isSystem,
                                const char *pVal, const char *pText, int textLen, int noHeaderLen) {
  char *s;
  int32_t int32, j, k, debug=0;
  int64_t int64;
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;
  intFloatUnion  fun;
  intDoubleUnion dun;

  /* payload item to add to msg */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);

  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type  = type;
  item->count = count;
  item->noHeaderLen = noHeaderLen;
  
  if (type == CMSG_CP_DBL_A) {          
    double *myArray;
    myArray = (double *)malloc(count*sizeof(double));
    if (myArray == NULL) {
      payloadItemFree(item, 1);
      free(item);
      return(CMSG_OUT_OF_MEMORY);
    }

    for (j=0; j<count; j++) {
      /* Convert from 16 chars (representing hex) to 64 bit int.
       * if we got a Z to start with, uncompress all the zeros. */
      if ( toByte[(int)(*pVal)] == -2 ) {
        int64 = 0;
        int64 = (((int64_t)toByte[(int)(*(pVal+1))] <<56) |
                 ((int64_t)toByte[(int)(*(pVal+2))] <<52) |
                 ((int64_t)toByte[(int)(*(pVal+3))] <<48) |
                 ((int64_t)toByte[(int)(*(pVal+4))] <<44) |
                 ((int64_t)toByte[(int)(*(pVal+5))] <<40) |
                 ((int64_t)toByte[(int)(*(pVal+6))] <<36) |
                 ((int64_t)toByte[(int)(*(pVal+7))] <<32) |
                 ((int64_t)toByte[(int)(*(pVal+8))] <<28) |
                 ((int64_t)toByte[(int)(*(pVal+9))] <<24) |
                 ((int64_t)toByte[(int)(*(pVal+10))]<<20) |
                 ((int64_t)toByte[(int)(*(pVal+11))]<<16) |
                 ((int64_t)toByte[(int)(*(pVal+12))]<<12) |
                 ((int64_t)toByte[(int)(*(pVal+13))]<<8)  |
                 ((int64_t)toByte[(int)(*(pVal+14))]<<4)  |
                 ((int64_t)toByte[(int)(*(pVal+15))]   ));
                 
        /* we have int64 number of zeros */
        for (k=0; k<int64; k++) {
          myArray[j+k] = 0.;
if(debug) printf("  double[%d] = %.17g\n", j+k, myArray[j+k]);
        }
        j += int64 - 1;
        pVal += 17;
        continue;
      }

      /* convert from 16 chars (representing hex) to double */
      dun.i = (uint64_t)(((int64_t)toByte[(int)(*pVal)]     <<60) |
                         ((int64_t)toByte[(int)(*(pVal+1))] <<56) |
                         ((int64_t)toByte[(int)(*(pVal+2))] <<52) |
                         ((int64_t)toByte[(int)(*(pVal+3))] <<48) |
                         ((int64_t)toByte[(int)(*(pVal+4))] <<44) |
                         ((int64_t)toByte[(int)(*(pVal+5))] <<40) |
                         ((int64_t)toByte[(int)(*(pVal+6))] <<36) |
                         ((int64_t)toByte[(int)(*(pVal+7))] <<32) |
                         ((int64_t)toByte[(int)(*(pVal+8))] <<28) |
                         ((int64_t)toByte[(int)(*(pVal+9))] <<24) |
                         ((int64_t)toByte[(int)(*(pVal+10))]<<20) |
                         ((int64_t)toByte[(int)(*(pVal+11))]<<16) |
                         ((int64_t)toByte[(int)(*(pVal+12))]<<12) |
                         ((int64_t)toByte[(int)(*(pVal+13))]<<8)  |
                         ((int64_t)toByte[(int)(*(pVal+14))]<<4)  |
                         ((int64_t)toByte[(int)(*(pVal+15))]   ));
      myArray[j] = dun.d;
      pVal += 17;
if(debug) printf("  double[%d] = %.17g\n", j, myArray[j]);
    }
    
    item->array = (void *)myArray;
  }

  /* float array */
  else if (type == CMSG_CP_FLT_A) {          
    float *myArray;
    myArray = (float *)malloc(count*sizeof(float));
    if (myArray == NULL) {
      payloadItemFree(item, 1);
      free(item);
      return(CMSG_OUT_OF_MEMORY);
    }

    for (j=0; j<count; j++) {
      /* Convert from 8 chars (representing hex) to int.
       * if we got a Z to start with, uncompress all the zeros. */
      if ( toByte[(int)(*pVal)] == -2 ) {
        int32 = 0;
        int32 = ((toByte[(int)(*(pVal+1))]<<24) |
                 (toByte[(int)(*(pVal+2))]<<20) |
                 (toByte[(int)(*(pVal+3))]<<16) |
                 (toByte[(int)(*(pVal+4))]<<12) |
                 (toByte[(int)(*(pVal+5))]<<8 ) |
                 (toByte[(int)(*(pVal+6))]<<4 ) |
                 (toByte[(int)(*(pVal+7))]    ));
if(debug) printf("  unpacking %d zeros\n", int32);
        /* we have int32 number of zeros */
        for (k=0; k<int32; k++) {
          myArray[j+k] = 0.F;
if(debug) printf("  float[%d] = %.8g\n", j+k, myArray[j+k]);
        }
        j += int32 - 1;
        pVal += 9;
        continue;
      }

      fun.i = (uint32_t) ((toByte[(int)(*pVal)]    <<28) |
                          (toByte[(int)(*(pVal+1))]<<24) |
                          (toByte[(int)(*(pVal+2))]<<20) |
                          (toByte[(int)(*(pVal+3))]<<16) |
                          (toByte[(int)(*(pVal+4))]<<12) |
                          (toByte[(int)(*(pVal+5))]<<8 ) |
                          (toByte[(int)(*(pVal+6))]<<4 ) |
                          (toByte[(int)(*(pVal+7))]    ));
      myArray[j] = fun.f;
      pVal+=9;
if(debug) printf("  float[%d] = %.8g\n", j, myArray[j]);
    }

    item->array = (void *)myArray;
  }

  s = item->text = (char *) malloc((size_t)(textLen+1));
  if (item->text == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* copy in text */
  memcpy(s, pText, (size_t)textLen);
    
  /* add null terminator */
  s[textLen] = '\0';
  
  /* store length of text */
  item->length = (int)strlen(item->text);
  
  /* place payload item in msg's linked list */
  addItem(msg, item);
    
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named field of a string to the compound payload
 * of a message. The text representation of the payload item is copied in
 * (doesn't need to be generated).
 * Since this routine is only used internally, there are no argument checks.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param type type of field to add
 * @param count number of elements if field is array
 * @param isSystem if = 0, add item to payload, else set system parameters
 * @param pVal pointer to "value" part of text (after header)
 * @param pText pointer to all of text (before header)
 * @param textLen length (in chars) of all text
 * @param noHeaderLen length (in chars) of text without header line
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_FORMAT if text being parsed is in wrong format
 * @return CMSG_OUT_OF_MEMORY if no more memory
 *
 */
static int addStringFromText(void *vmsg, char *name, int type, int count, int isSystem,
                             const char *pVal, const char *pText, int textLen, int noHeaderLen) {
  const char *t;
  char *s, *str;
  int len;
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  
  /* start after header, first item is length */
  t = pVal;
  s = strpbrk(t, "\n");
  if (s == NULL) return(CMSG_BAD_FORMAT);
  
  /* read length of string */
  sscanf(t, "%d", &len);
  if (len < 0) return(CMSG_BAD_FORMAT);
  t = s+1;
  
  /* allocate memory to hold string */
  str = (char *) malloc((size_t)(len+1));
  if (str == NULL) return(CMSG_OUT_OF_MEMORY);
  
  /* copy string into memory */
  memcpy(str, t, (size_t)len);
  str[len] = '\0';
  
  /* is regular field in msg */
  if (isSystem) {
    int putInPayload = 0;
    
    if (strcmp(name, "cMsgText") == 0) {
      if (msg->text != NULL) free(msg->text);
      msg->text = str;
    }
    else if (strcmp(name, "cMsgSubject") == 0) {
      if (msg->subject != NULL) free(msg->subject);
      msg->subject = str;
    }
    else if (strcmp(name, "cMsgType") == 0) {
      if (msg->type != NULL) free(msg->type);
      msg->type = str;
    }
    else if (strcmp(name, "cMsgDomain") == 0) {
      if (msg->domain != NULL) free(msg->domain);
      msg->domain = str;
    }
    else if (strcmp(name, "cMsgSender") == 0) {
      if (msg->sender != NULL) free(msg->sender);
      msg->sender = str;
    }
    else if (strcmp(name, "cMsgSenderHost") == 0) {
      if (msg->senderHost != NULL) free(msg->senderHost);
      msg->senderHost = str;
    }
    else if (strcmp(name, "cMsgReceiver") == 0) {
      if (msg->receiver != NULL) free(msg->receiver);
      msg->receiver = str;
    }
    else if (strcmp(name, "cMsgReceiverHost") == 0) {
      if (msg->receiverHost != NULL) free(msg->receiverHost);
      msg->receiverHost = str;
    }
    else {
      putInPayload = 1;
    }
    
    if (!putInPayload) return(CMSG_OK);
  }
  
  /* payload item to add to msg */
  item = (payloadItem *) malloc(sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);
  /* store string in payload */
  item->array = str;

  item->name = strdup(name);
  if (item->name == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type  = type;
  item->count = count;
  item->noHeaderLen = noHeaderLen;
  
  s = item->text = (char *) malloc((size_t)(textLen+1));
  if (item->text == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* copy in text */
  memcpy(s, pText,(size_t) textLen);
    
  /* add null terminator */
  s[textLen] = '\0';
  
  /* store length of text */
  item->length = (int)strlen(item->text);
  
  /* place payload item in msg's linked list */
  addItem(msg, item);
    
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/


/**
 * This routine adds a named field of an array of strings to the compound payload
 * of a message. The text representation of the array is copied in (doesn't
 * need to be generated).
 * Since this routine is only used internally, there are no argument checks.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param type type of field to add
 * @param count number of elements if field is array
 * @param isSystem if = 0, add item to payload, else set system parameters
 * @param pVals pointer to "value" part of text (after header)
 * @param pText pointer to all of text (before header)
 * @param textLen length (in chars) of all text
 * @param noHeaderLen length (in chars) of text without header line
 *
 * @return CMSG_OK if successful
 * @return CMSG_BAD_FORMAT if text being parsed is in wrong format
 * @return CMSG_OUT_OF_MEMORY if no more memory
 *
 */
static int addStringArrayFromText(void *vmsg, char *name, int type, int count, int isSystem,
                                  const char *pVal, const char *pText, int textLen, int noHeaderLen) {
  const char *t;
  char *s, **txtArray;
  int i, j, len;
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  
  /* start after header, first item is length */
  t = pVal;
  s = strpbrk(t, "\n");
  if (s == NULL) return(CMSG_BAD_FORMAT);
  
  txtArray = (char **) malloc(count*sizeof(char *));
  if (txtArray == NULL) return(CMSG_OUT_OF_MEMORY);

  for (j=0; j<count; j++) {
    /* read length of string */
    sscanf(t, "%d", &len);
    if (len < 1) return(CMSG_BAD_FORMAT);
    t = s+1;          

    txtArray[j] = (char *)malloc((size_t)(len+1));
    if (txtArray[j] == NULL) {
      for (i=0; i<j; i++) {
        free(txtArray[i]);
      }
      free(txtArray);
      return(CMSG_OUT_OF_MEMORY);
    }
    memcpy(txtArray[j], t, (size_t)len);
    txtArray[j][len] = '\0';

    t = s+1+len+1;
    s = strpbrk(t, "\n");
    /* s will be null if it's the very last item */
    if (s == NULL  && j != count-1) return(CMSG_BAD_FORMAT);
  }
  
  /* payload item to add to msg */
  item = (payloadItem *) malloc(sizeof(payloadItem));
  if (item == NULL) {
    for (i=0; i<count; i++) {
      free(txtArray[i]);
    }
    free(txtArray);
    return(CMSG_OUT_OF_MEMORY);
  }
  payloadItemInit(item);
  /* store strings in payload */
  item->array = txtArray;
  
  item->name = strdup(name);
  if (item->name == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type  = type;
  item->count = count;
  item->noHeaderLen = noHeaderLen;
  
  s = item->text = (char *) malloc((size_t)(textLen+1));
  if (item->text == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* copy in text */
  memcpy(s, pText, (size_t)textLen);
    
  /* add null terminator */
  s[textLen] = '\0';
  
  /* store length of text */
  item->length = (int)strlen(item->text);
  
  /* place payload item in msg's linked list */
  addItem(msg, item);
    
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/



/**
 * This routine adds a named field of a cMsg message or an array of cMsg
 * messages to the compound payload of a message. The text representation
 * of the payload item is copied in (doesn't need to be generated).
 * Since this routine is only used internally, there are no argument checks.
 *
 * @param vmsg pointer to message
 * @param name name of field to add
 * @param type type of field to add
 * @param count number of elements if field is array
 * @param isSystem if = 0, add item to payload, else set system parameters
 * @param vmessages cMsg message or array of cMsg messages to add
 * @param pText text form of cMsg message to add
 * @param textLen length (in chars) of msgText arg
 * @param noHeaderLen length (in chars) of msgText without header line
 *
 * @return CMSG_OK if successful
 * @return CMSG_OUT_OF_MEMORY if no more memory

 */   
static int addMessagesFromText(void *vmsg, const char *name, int type, int count, int isSystem,
                               void *vmessages, const char *pText, int textLen, int noHeaderLen) {
  char *s;
  payloadItem *item;  
  cMsgMessage_t *msg = (cMsgMessage_t *)vmsg;

  
  /* payload item to add to msg */
  item = (payloadItem *) calloc(1, sizeof(payloadItem));
  if (item == NULL) return(CMSG_OUT_OF_MEMORY);
  payloadItemInit(item);

  item->name = strdup(name);
  if (item->name == NULL) {
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  item->type  = type;
  item->count = count;
  item->noHeaderLen = noHeaderLen;
  
  /* we now own the message(s) */
  item->array = vmessages;
  s = item->text = (char *) malloc((size_t)(textLen+1));
  if (item->text == NULL) {
    payloadItemFree(item, 1);
    free(item);
    return(CMSG_OUT_OF_MEMORY);
  }
  
  /* copy in text */
  memcpy(s, pText, (size_t)textLen);
    
  /* add null terminator */
  s[textLen] = '\0';
  
  /* store length of text */
  item->length = (int)strlen(item->text);

  /* place payload item in msg's linked list */
  addItem(msg, item);
    
  return(CMSG_OK);
}


/*-------------------------------------------------------------------*/
/*-------------------------------------------------------------------*/






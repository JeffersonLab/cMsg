/*----------------------------------------------------------------------------*
 *  Copyright (c) 2004        Southeastern Universities Research Association, *
 *                            Thomas Jefferson National Accelerator Facility  *
 *                                                                            *
 *    This software was developed under a United States Government license    *
 *    described in the NOTICE file included as part of this distribution.     *
 *                                                                            *
 *    C. Timmer, 7-Sep-2004, Jefferson Lab                                    *
 *                                                                            *
 *     Author: Carl Timmer                                                    *
 *             timmer@jlab.org                   Jefferson Lab, MS-6B         *
 *             Phone: (757) 269-5130             12000 Jefferson Ave.         *
 *             Fax:   (757) 269-5800             Newport News, VA 23606       *
 *                                                                            *
 *----------------------------------------------------------------------------*/
 
#ifdef VXWORKS
#include <vxWorks.h>
#else
#include <strings.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#include "cMsg.h"


/******************************************************************/
static void usage() {
  printf("Usage:  payload [-s <size> | -b <size>] -u <UDL>\n");
  printf("                  -s sets the byte size for text data, or\n");
  printf("                  -b sets the byte size for binary data\n");
  printf("                  -u sets the connection UDL\n");
}


/******************************************************************/
int main(int argc,char **argv) {  

  int   i, err, len, debug=1, hasPayload=0;
  int8_t  ivals[11], *ivals2;
  int16_t  ivals16[11];
  int32_t  ivals32[11];
  double   dvals[11];
  char *s, *vals[3];
  const char *t;
  const char **array;
  void *msg, *newMsg, *newMsg2;
      
 
  if (debug) {
    printf("Running the payload in debug mode\n");
  }
  
  /* create message to be sent */
  msg = cMsgCreateMessage();    /* allocating mem here */
  cMsgSetSubject(msg, "subbie");
  cMsgSetType   (msg, "typie");
  
  cMsgHasPayload(msg, &hasPayload);
  if (hasPayload) printf("I GOTTA PAYLOAD\n");
  else printf("I DONT GOTTA PAYLOAD\n");
  
  cMsgAddInt32  (msg, "fred",   1234);
  cMsgAddInt32  (msg, "ginger", 5678);
  cMsgAddString (msg, "str1", "first string");
  cMsgAddString (msg, "str2", "second string");
  err = cMsgAddInt32  (msg, "fred",   9876);
  if (err == CMSG_OK) {
    printf("tried to add another fred !!!\n");  
  }
  else if (err == CMSG_ALREADY_EXISTS) {
    printf("tried to add another fred but it already exists\n");
  }
  
  err = cMsgAddInt32  (msg, "fred with spaces", 9876);
  if (err == CMSG_OK) {
    printf("added \"fred with spaces\"!!!\n");  
  }
  else if (err == CMSG_BAD_FORMAT) {
    printf("tried to add \"fred with spaces\" but it wasn't allowed\n");
  }

  err = cMsgAddInt32  (msg, "cmsg_blah", 9876);
  if (err == CMSG_OK) {
    printf("added \"cmsg_blah\"!!!\n");  
  }
  else if (err == CMSG_BAD_FORMAT) {
    printf("tried to add \"cmsg_blah\" but it wasn't allowed\n");
  }

  printf("added fred (1234) and ginger (5678)\n");

  err = cMsgGetInt32(msg,"ginger", &i);
  printf("ginger value = %d\n",i);
  
  err = cMsgGetString(msg, "str2", &s);
  printf("str2 value = \"%s\"\n",s);
  
  vals[0] = "zero";
  vals[1] = "one";
  vals[2] = "two";
  
  cMsgAddStringArray(msg, "heyho", (const char **) vals, 3);
  
  err = cMsgGetStringArray(msg, "heyho", &array, &len);
  if (err != CMSG_OK) {
    printf("Error getting string array\n"); 
  }
  for (i=0; i<len; i++) {
    printf("  str array[%d] = %s\n",i, array[i]);
  }
  
  dvals[0] = -2147483647;
  dvals[1] = 0x10;
  dvals[2] = 0x20;
  dvals[3] = 1.234567890112235e+123;
  dvals[4] = 126;
  dvals[5] = 22222.33;
  dvals[6] = -2147483647;
  dvals[7] = 2;
  dvals[8] = -1.23456789e-66;
  dvals[9] = 3.14159;
  dvals[10] = 2147483647;
  
  cMsgAddDoubleArray(msg, "myDoubleArray", dvals, 11);
  
  ivals32[0] = -2147483647;
  ivals32[1] = 0x10;
  ivals32[2] = 0x20;
  ivals32[3] = 2147483647;
  ivals32[4] = 126;
  ivals32[5] = 22222;
  ivals32[6] = -2147483647;
  ivals32[7] = 2;
  ivals32[8] = 0;
  ivals32[9] = 0;
  ivals32[10] = 2147483647;
  
  cMsgAddInt32Array(msg, "myInt32Array", ivals32, 11);
  
  ivals16[0] = -32767;
  ivals16[1] = 0x10;
  ivals16[2] = 0x20;
  ivals16[3] = 127;
  ivals16[4] = 126;
  ivals16[5] = 22222;
  ivals16[6] = -12345;
  ivals16[7] = 2;
  ivals16[8] = 44;
  ivals16[9] = 0;
  ivals16[10] = 32767;
  
  cMsgAddInt16Array(msg, "myInt16Array", ivals16, 11);
  
  ivals[0] = -127;
  ivals[1] = 0x10;
  ivals[2] = 0x20;
  ivals[3] = 127;
  ivals[4] = 126;
  ivals[5] = 125;
  ivals[6] = 124;
  ivals[7] = 2;
  ivals[8] = 44;
  ivals[9] = 0;
  ivals[10] = 66;
  
  cMsgAddInt8Array(msg, "myInt8Array", ivals, 11);
  
  err = cMsgGetInt8Array(msg, "myInt8Array", (const int8_t **)&ivals2, &len);
  if (err != CMSG_OK) {
    printf("Error getting int array\n"); 
  }
  for (i=0; i<len; i++) {
    printf("  int8 array[%d] = %d\n",i, ivals2[i]);
  }


  printf("Set MESSAGE text as TEXXXXT:\n");
  cMsgSetText(msg, "TEXXXXT");

  cMsgPayloadGetFieldText(msg, "heyho", &t);
  printf("\nPrint text for heyho:\n%s", t);
     
  printf("Create a new message1:\n");
  newMsg = cMsgCreateMessage();
    
  printf("Set fields with wire text:\n");
  err = cMsgPayloadSetAllFieldsFromText(newMsg, s);
  if (err != CMSG_OK) printf("%s\n", cMsgPerror(err));  
  free(s);
  
  printf("Create a new message2:\n");
  newMsg2 = cMsgCreateMessage();

  printf("Copy payload from msg to message2:\n");
  err = cMsgPayloadCopy(msg, newMsg2);
  if (err != CMSG_OK) {
    printf("Error copying payload\n");
  }
  
  err = cMsgToString(msg, &s, 1);
  if (err != CMSG_OK) {
    printf("Error copying getting xml string: %s\n", cMsgPerror(err));
  }
  else {
    printf("XML REPRESENTATION:\n%s", s);
    free(s);
  }
  
  cMsgHasPayload(msg, &hasPayload);
  if (hasPayload) printf("I GOTTA PAYLOAD\n");
  else printf("I DONT GOTTA PAYLOAD\n");
  
printf("Now clear the payload\n");
  cMsgPayloadClear(msg);
  
  cMsgHasPayload(msg, &hasPayload);
  if (hasPayload) printf("I GOTTA PAYLOAD\n");
  else printf("I DONT GOTTA PAYLOAD\n");
  
  cMsgWasSent(msg, &hasPayload);
  if (hasPayload) printf("I WAS SENT\n");
  else printf("I WAS NOT SENT\n");
  
  
  cMsgFreeMessage(&msg);
  cMsgFreeMessage(&newMsg);
 
  return(0);
}

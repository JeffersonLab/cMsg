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

#include "cMsg.hxx"

using namespace std;
using namespace cmsg;


/******************************************************************/
// static void usage() {
//   printf("Usage:  payload [-s <size> | -b <size>] -u <UDL>\n");
//   printf("                  -s sets the byte size for text data, or\n");
//   printf("                  -b sets the byte size for binary data\n");
//   printf("                  -u sets the connection UDL\n");
// }


/******************************************************************/
int main(int argc,char **argv) {  

  int   err, debug=1, hasPayload=0;
  size_t ii;
  int8_t  ivals[11];
  int16_t  ivals16[11];
  int32_t  ivals32[11];
  double   dvals[11];
  char *s;
  const char *vals[3];
      
 
  if (debug) {
    printf("Running the payload in debug mode\n");
  }
  
  /* create message to be sent */
  cMsgMessage msg;
  msg.setSubject("subbie");
  msg.setType("typie");
  
  bool b = msg.hasPayload();
  if (b) printf("I GOTTA PAYLOAD\n");
  else printf("I DONT GOTTA PAYLOAD\n");
  
  msg.add("fred", 1234);
  msg.add("str1", "first string");

  printf("added fred (1234) and ginger (5678)\n");
  
  vals[0] = "zero";
  vals[1] = "one";
  vals[2] = "two";
  
  msg.add("heyho", (const char **) vals, 3);
  
  vector<string> *v = msg.getStringVector("heyho");
  for (ii=0; ii<v->size(); ii++) {
    printf("  str array[%zu] = %s\n",ii, (*v)[ii].c_str());
  }
  delete(v);
  
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
  
  cMsgAddDoubleArray(msg.myMsgPointer, "myDoubleArray", dvals, 11);
  
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
  
  cMsgAddInt32Array(msg.myMsgPointer, "myInt32Array", ivals32, 11);
  
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
  
  cMsgAddInt16Array(msg.myMsgPointer, "myInt16Array", ivals16, 11);
  
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
  
  cMsgAddInt8Array(msg.myMsgPointer, "myInt8Array", ivals, 11);
    
  vector<int8_t> *vec = msg.getInt8Vector("myInt8Array");
  for (ii=0; ii<vec->size(); ii++) {
    printf("  int8 array vec[%d] = %d\n", (int)ii, (*vec)[ii]);
  }
  delete(vec);
  
  vector<double> *dvec = msg.getDoubleVector("myDoubleArray");
  for (ii=0; ii<dvec->size(); ii++) {
    printf("  dbl array vec[%d] = %.16lg\n", (int)ii, (*dvec)[ii]);
  }
  delete(dvec);

  vector<string> *svec = msg.getStringVector("heyho");
  for (ii=0; ii<svec->size(); ii++) {
    printf("  str array vec[%d] = %s\n", (int)ii, (*svec)[ii].c_str());
  }
  delete(svec);

  printf("Set MESSAGE text as TEXXXXT:\n");
  msg.setText("TEXXXXT");
  
      
  err = cMsgToString(msg.myMsgPointer, &s);
  if (err != CMSG_OK) {
    printf("Error copying getting xml string: %s\n", cMsgPerror(err));
  }
  else {
    printf("XML REPRESENTATION:\n%s", s);
    free(s);
  }
  
  if (msg.hasPayload()) printf("I GOTTA PAYLOAD\n");
  else printf("I DONT GOTTA PAYLOAD\n");
  
printf("Now clear the payload\n");
  msg.payloadClear();
  
  if (msg.hasPayload()) printf("I GOTTA PAYLOAD\n");
  else printf("I DONT GOTTA PAYLOAD\n");
  
  cMsgWasSent(msg.myMsgPointer, &hasPayload);
  if (hasPayload) printf("I WAS SENT\n");
  else printf("I WAS NOT SENT\n");
  
  
  cMsgFreeMessage(&msg.myMsgPointer);
 
  return(0);
}

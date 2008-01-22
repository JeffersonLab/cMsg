/*---------------------------------------------------------------------------*
*  Copyright (c) 2007        Jefferson Science Associates,                   *
*                            Thomas Jefferson National Accelerator Facility  *
*                                                                            *
*    This software was developed under a United States Government license    *
*    described in the NOTICE file included as part of this distribution.     *
*                                                                            *
*    C.Timmer, 3-Aug-2007, Jefferson Lab                                     *
*                                                                            *
*    Authors: Carl Timmer                                                    *
*             timmer@jlab.org                   Jefferson Lab, MS-12B3       *
*             Phone: (757) 269-5130             12000 Jefferson Ave.         *
*             Fax:   (757) 269-6248             Newport News, VA 23606       *
*                                                                            *
*----------------------------------------------------------------------------*/


#include <cMsg.hxx>
#include <cMsgPrivate.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <vector>
#include <map>
#include <sstream>

using namespace std;
using namespace cmsg;

namespace cmsg {

//-----------------------------------------------------------------------------
//  cMsgPayload methods
//-----------------------------------------------------------------------------



//-----------------------------------------------------------------------------

/**
 * This method frees the allocated memory of the given message's entire payload
 * and then initializes the payload components of the message. 
 */   
void cMsgMessage::payloadClear(void) {
    cMsgPayloadClear(myMsgPointer);
}

//-------------------------------------------------------------------

/**
 * This method prints out the message payload in a readable form.
 */
void cMsgMessage::payloadPrint(void) {
    cMsgPayloadPrint(myMsgPointer);
}

//-------------------------------------------------------------------
// users should not have access to this !!!

/**
 * This method takes a string representation of the
 * whole compound payload, including the system (hidden) fields of the message,
 * as it gets sent over the network and converts it into the standard message
 * payload. All system information is ignored. This overwrites any existing
 * payload and skips over any fields with names starting with "cMsg"
 * (as they are reserved for system use).
 *
 * @param text string sent over network to be unmarshalled
 * @throws cMsgException if no payload exists or no memory
 */   
void cMsgMessage::payloadSetFromText(const string &text) throw(cMsgException) {
  int err = cMsgPayloadSetFromText(myMsgPointer, text.c_str());
  if (err != CMSG_OK) {
    throw(cMsgException(cMsgPerror(err),err));
  }
}

//-------------------------------------------------------------------
// users should not have access to this !!!

/**
 * This method takes a string representation of the
 * whole compound payload, including the system (hidden) fields of the message,
 * as it gets sent over the network and converts it into the hidden system fields
 * of the message. All non-system information is ignored. This overwrites any existing
 * system fields.
 *
 * @param text string sent over network to be unmarshalled
 * @throws cMsgException if no payload exists or no memory
 */   
void cMsgMessage::payloadSetSystemFieldsFromText(const string &text) throw(cMsgException) {
  int err = cMsgPayloadSetSystemFieldsFromText(myMsgPointer, text.c_str());
  if (err != CMSG_OK) {
    throw(cMsgException(cMsgPerror(err),err));
  }
}

//-------------------------------------------------------------------
// users should not have access to this !!!

/**
 * This method takes a string representation of the
 * whole compound payload, including the system (hidden) fields of the message,
 * as it gets sent over the network and converts it into the hidden system fields
 * and payload of the message. This overwrites any existing system fields and payload.
 *
 * @param text string sent over network to be unmarshalled
 * @throws cMsgException if no payload exists or no memory
 */   
void cMsgMessage::payloadSetAllFieldsFromText(const string &text) throw(cMsgException) {
  int err = cMsgPayloadSetAllFieldsFromText(myMsgPointer, text.c_str());
  if (err != CMSG_OK) {
    throw(cMsgException(cMsgPerror(err),err));
  }
}

//-------------------------------------------------------------------
// users should not have access to this !!!

/**
 * This method returns a pointer to the string representation of the
 * whole compound payload and the hidden system fields of the message
 * (which currently is only the "text") as it gets sent over the network.
 * Memory is allocated for the returned string so it must be freed by the user.
 *
 * @throws cMsgException text if no payload exists or no memory
 */   
string cMsgMessage::payloadGetText() const throw(cMsgException) {
  char *text;
  int ok = cMsgPayloadGetText(myMsgPointer, &text, NULL, 0, NULL);
  if (ok != CMSG_OK) {
    throw(cMsgException("Either no payload exists, or no more memory"));
  }
  string s(text);
  free(text);
  return s;
}

//-------------------------------------------------------------------

/**
 * This method returns whether a message has a compound payload or not. 
 *
 * @returns true if message has a payload, else false
 */   
bool cMsgMessage::hasPayload() const {
  int hasPayload;
  cMsgHasPayload(myMsgPointer, &hasPayload);
  return hasPayload ? true : false;
}

//-------------------------------------------------------------------

/**
 * This method returns whether an item in the payload has the given name or not. 
 *
 * @param name name of field to look for
 * @returns true if an item in the payload has the given name, else false
 */   
bool cMsgMessage::payloadContainsName(const string &name) const {
  return cMsgPayloadContainsName(myMsgPointer, name.c_str()) ? true : false;
}

//-------------------------------------------------------------------

/**
 * This method returns the number of payload items a message has. 
 *
 * @returns number of payload items a message has
 */   
int cMsgMessage::payloadGetCount() const {
  int count;
  cMsgPayloadGetCount(myMsgPointer, &count);
  return count;
}

//-------------------------------------------------------------------

/**
 * This method returns the string representation of the given field. 
 *
 * @param name name of field to look for
 * @returns string representation of the given field
 * @throws cMsgException if no payload/field exists, or if name is NULL
 */   
string cMsgMessage::payloadGetFieldText(const string &name) const throw(cMsgException) {
  const char *val;
  int err = cMsgPayloadGetFieldText(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_ARGUMENT) throw(cMsgException("Name is null")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  return string(val);
}

//-------------------------------------------------------------------

/**
 * This method returns the type of data associated with the payload
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
 * </UL>
 *
 * @param name name of field to find type for
 *
 * @returns the type of data associated with the payload
 *          field given by the name argument
 * @throws cMsgException if no payload/field exists, or if name is NULL
 */   
int cMsgMessage::payloadGetType(const string &name) const throw(cMsgException) {
  int err, type;
  err = cMsgPayloadGetType(myMsgPointer, name.c_str(), &type);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_ARGUMENT) throw(cMsgException("Name is null")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  return type;
}

//-------------------------------------------------------------------

/**
 * This method copies the payload from another message.
 * The original payload is overwritten.
 *
 * @param reference to message to copy payload from
 * @throws cMsgException if no memory
 */   
void cMsgMessage::payloadCopy(cMsgMessage &msg) throw(cMsgException) {
  int err = cMsgPayloadCopy(msg.myMsgPointer, myMsgPointer);
  if (err!= CMSG_OK) {
    throw(cMsgException(cMsgPerror(err),err));
  }
}

//-------------------------------------------------------------------

/**
 * This method returns the current field name.
 *
 * @throws cMsgException if no payload
 */   
string cMsgMessage::payloadGetFieldDescription(const string &name) const throw(cMsgException) {
  const char *field = cMsgPayloadFieldDescription(myMsgPointer, name.c_str());
  if (field == NULL) {
    string err("No such field as ");
    err += field;
    throw(cMsgException(err));
  }
  string s(field);
  return s;
}

//-------------------------------------------------------------------

/**
 * This method returns a pointer to a map containing all name/type pairs
 * of the payload. A field's name is the key and type is the value.
 * The map must be deleted to avoid a memory leak.
 *
 * @returns a pointer to a map containing all name/type pairs of the payload
 * @throws cMsgException if no payload exists, or if name is NULL
 */   
map<string,int> *cMsgMessage::payloadGet() const throw(cMsgException) {
  char **names;
  int *types, len;
  
  int err = cMsgPayloadGetInfo(myMsgPointer, &names, &types, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_ARGUMENT) throw(cMsgException("Name is null")); 
    else if (err == CMSG_ERROR) throw(cMsgException("No payload exists")); 
    else throw(cMsgException("Out of memory")); 
  }
  
  map<string,int> *mp = new map<string,int>;
  for (int i=0; i<len; i++) {
    (*mp)[names[i]] = types[i];
  }
  return mp;
}

//-------------------------------------------------------------------

/**
 * This method removes the named field if its exists.
 *
 * @param name name of field to remove
 *
 * @returns true if successful
 * @returns false if no field with that name was found
 */   
bool cMsgMessage::payloadRemoveField(const string &name) {
  return cMsgPayloadRemove(myMsgPointer, name.c_str()) == 0 ? false : true;
}


//-------------------------------------------------------------------
// GET VALUE METHODS
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// BINARY
//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a binary array if it exists.
 *
 * @param name name of field to add
 * @param val address of pointer to data which sets the pointer to converted binary
 * @param len int reference which gets set to the number of bytes in binary array
 * @param endian int reference which gets set to endian of data (CMSG_ENDIAN_BIG/LITTLE)
 *
 * @throws cMsgException if no payload/field exists or field is not right type,
 *                        or if any arg is NULL
 */   
void cMsgMessage::getBinary(string name, char **val, int &len, int &endian) const throw(cMsgException) {
  int err = cMsgGetBinary(myMsgPointer, name.c_str(), val, &len, &endian);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  return;
}

//-------------------------------------------------------------------
// CMSG MESSAGES
//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a cMsgMessage
 * object pointer if its exists. This object must be deleted to avoid
 * a memory leak.
 *
 * @param name name of field to add
 *
 * @returns field's value as cMag message
 * @throws cMsgException if no payload/field exists or field is not right type (single string)
 */   
cMsgMessage *cMsgMessage::getMessage(string name) const throw(cMsgException) {
  void *val;
  int err = cMsgGetMessage(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  return new cMsgMessage(val);
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a pointer to a
 * vector of cMsgMessage objects if its exists. The vector pointer
 * must be deleted by caller to avoid a memory leak.
 *
 * @param name name of field to add
 *
 * @returns field's value as vector of cMsgMessage objects
 * @throws cMsgException if no payload/field exists or field is not right type (message array)
 */   
vector<cMsgMessage> *cMsgMessage::getMessageVector(string name) const throw(cMsgException) {
  int len;
  void **vals;
  int err = cMsgGetMessageArray(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  
  // put array values into a vector
  vector<cMsgMessage> *msgs = new vector<cMsgMessage>(len);
  for (int i=0; i<len; i++) {
    //msgs->push_back(new cMsgMessage(cMsgCopyMessage(vals[i])));
    // theoretically, 1st creation of message only copies pointer,
    // when added to vector, copy constructor gets called so we should be OK
    msgs->push_back(cMsgMessage(vals[i]));
  }
  return msgs;
}

//-------------------------------------------------------------------
// STRINGS
//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a string if its exists.
 *
 * @param name name of field to add
 *
 * @returns field's value as string
 * @throws cMsgException if no payload/field exists or field is not right type (single string)
 */   
string cMsgMessage::getString(string name) const throw(cMsgException) {
  char *val;
  int err = cMsgGetString(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  string s(val);
  return s;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a pointer to a
 * vector of strings if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to add
 *
 * @returns field's value as vector of strings
 * @throws cMsgException if no payload/field exists or field is not right type (string array)
 */   
vector<string> *cMsgMessage::getStringVector(string name) const throw(cMsgException) {
  int len;
  const char **vals;
  int err = cMsgGetStringArray(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  
  // copy array values and put into a vector
  vector<string> *strs = new vector<string>(len);
  for (int i=0; i<len; i++) {
    strs->push_back(string(vals[i]));
  }
  return strs;
}

//-------------------------------------------------------------------
// REALS
//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a float if its exists.
 *
 * @param name name of field to add
 *
 * @throws cMsgException if no payload/field exists or field is not right type (float)
 */   
float cMsgMessage::getFloat(string name) const throw(cMsgException) {
  float val;
  int err = cMsgGetFloat(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a double if its exists.
 *
 * @param name name of field to add
 *
 * @throws cMsgException if no payload/field exists or field is not right type (double)
 */   
double cMsgMessage::getDouble(string name) const throw(cMsgException) {
  double val;
  int err = cMsgGetDouble(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  return val;
}

//-------------------------------------------------------------------
// REAL ARRAYS
//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to a
 * vector of floats if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to add
 *
 * @returns field's value as vector of floats
 * @throws cMsgException if no payload/field exists or field is not right type (float array)
 */   
vector<float> *cMsgMessage::getFloatVector(string name) const throw(cMsgException) {
  int len;
  const float *vals;
  int err = cMsgGetFloatArray(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  // put array values into a vector
  vector<float> *flts = new vector<float>(len);
  for (int i=0; i<len; i++) {
    flts->push_back(vals[i]);
  }
  return flts;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a pointer to a
 * vector of doubles if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to add
 *
 * @returns field's value as vector of doubles
 * @throws cMsgException if no payload/field exists or field is not right type (double array)
 */   
vector<double> *cMsgMessage::getDoubleVector(string name) const throw(cMsgException) {
  int len;
  const double *vals;
  int err = cMsgGetDoubleArray(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  // put array values into a vector
  vector<double> *dbls = new vector<double>(len);
  for (int i=0; i<len; i++) {
    dbls->push_back(vals[i]);
  }
  return dbls;
}

//-------------------------------------------------------------------
// INTS
//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 8 bit, signed integer if its exists.
 *
 * @param name name of field to add
 *
 * @throws cMsgException if no payload/field exists or field is not right type (8 bit int)
 */   
int8_t cMsgMessage::getInt8(string name) const throw(cMsgException) {
  int8_t val;
  int err = cMsgGetInt8(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 16 bit, signed integer if its exists.
 *
 * @param name name of field to add
 *
 * @throws cMsgException if no payload/field exists or field is not right type (16 bit int)
 */   
int16_t cMsgMessage::getInt16(string name) const throw(cMsgException) {
  int16_t val;
  int err = cMsgGetInt16(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 32 bit, signed integer if its exists.
 *
 * @param name name of field to add
 *
 * @throws cMsgException if no payload/field exists or field is not right type (32 bit int)
 */   
int32_t cMsgMessage::getInt32(string name) const throw(cMsgException) {
  int32_t val;
  int err = cMsgGetInt32(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 64 bit, signed integer if its exists.
 *
 * @param name name of field to add
 *
 * @throws cMsgException if no payload/field exists or field is not right type (64 bit int)
 */   
int64_t cMsgMessage::getInt64(string name) const throw(cMsgException) {
  int64_t val;
  int err = cMsgGetInt64(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 8 bit, unsigned integer if its exists.
 *
 * @param name name of field to add
 *
 * @throws cMsgException if no payload/field exists or field is not right type (8 bit unsigned int)
 */   
uint8_t cMsgMessage::getUint8(string name) const throw(cMsgException) {
  uint8_t val;
  int err = cMsgGetUint8(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 16 bit, unsigned integer if its exists.
 *
 * @param name name of field to add
 *
 * @throws cMsgException if no payload/field exists or field is not right type (16 bit unsigned int)
 */   
uint16_t cMsgMessage::getUint16(string name) const throw(cMsgException) {
  uint16_t val;
  int err = cMsgGetUint16(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 32 bit, unsigned integer if its exists.
 *
 * @param name name of field to add
 *
 * @throws cMsgException if no payload/field exists or field is not right type (32 bit unsigned int)
 */   
uint32_t cMsgMessage::getUint32(string name) const throw(cMsgException) {
  uint32_t val;
  int err = cMsgGetUint32(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 64 bit, unsigned integer if its exists.
 *
 * @param name name of field to add
 *
 * @throws cMsgException if no payload/field exists or field is not right type (64 bit unsigned int)
 */   
uint64_t cMsgMessage::getUint64(string name) const throw(cMsgException) {
  uint64_t val;
  int err = cMsgGetUint64(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  return val;
}

//-------------------------------------------------------------------
// INT ARRAYS & VECTORS
//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a pointer to a
 * vector of 8-bit, signed ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to add
 *
 * @returns field's value as vector of 8 bit, signed ints
 * @throws cMsgException if no payload/field exists or field is not right type (8 bit, signed ints)
 */   
vector<int8_t> *cMsgMessage::getInt8Vector(string name) const throw(cMsgException) {
  int len;
  const int8_t *vals;
  int err = cMsgGetInt8Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  // put array values into a vector
  vector<int8_t> *ints = new vector<int8_t>(len);
  for (int i=0; i<len; i++) {
    ints->push_back(vals[i]);
  }
  return ints;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a pointer to a
 * vector of 16-bit, signed ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to add
 *
 * @returns field's value as vector of 16 bit, signed ints
 * @throws cMsgException if no payload/field exists or field is not right type (16 bit, signed ints)
 */   
vector<int16_t> *cMsgMessage::getInt16Vector(string name) const throw(cMsgException) {
  int len;
  const int16_t *vals;
  int err = cMsgGetInt16Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  // put array values into a vector
  vector<int16_t> *ints = new vector<int16_t>(len);
  for (int i=0; i<len; i++) {
    ints->push_back(vals[i]);
  }
  return ints;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a pointer to a
 * vector of 32-bit, signed ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to add
 *
 * @returns field's value as vector of 32 bit, signed ints
 * @throws cMsgException if no payload/field exists or field is not right type (32 bit, signed ints)
 */   
vector<int32_t> *cMsgMessage::getInt32Vector(string name) const throw(cMsgException) {
  int len;
  const int32_t *vals;
  int err = cMsgGetInt32Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  // put array values into a vector
  vector<int32_t> *ints = new vector<int32_t>(len);
  for (int i=0; i<len; i++) {
    ints->push_back(vals[i]);
  }
  return ints;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a pointer to a
 * vector of 64-bit, signed ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to add
 *
 * @returns field's value as vector of 64 bit, signed ints
 * @throws cMsgException if no payload/field exists or field is not right type (64 bit, signed ints)
 */   
vector<int64_t> *cMsgMessage::getInt64Vector(string name) const throw(cMsgException) {
  int len;
  const int64_t *vals;
  int err = cMsgGetInt64Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  // put array values into a vector
  vector<int64_t> *ints = new vector<int64_t>(len);
  for (int i=0; i<len; i++) {
    ints->push_back(vals[i]);
  }
  return ints;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a pointer to a
 * vector of 8-bit, unsigned ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to add
 *
 * @returns field's value as vector of 8 bit, unsigned ints
 * @throws cMsgException if no payload/field exists or field is not right type (8 bit, unsigned ints)
 */   
vector<uint8_t> *cMsgMessage::getUint8Vector(string name) const throw(cMsgException) {
  int len;
  const uint8_t *vals;
  int err = cMsgGetUint8Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  // put array values into a vector
  vector<uint8_t> *ints = new vector<uint8_t>(len);
  for (int i=0; i<len; i++) {
    ints->push_back(vals[i]);
  }
  return ints;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a pointer to a
 * vector of 16-bit, unsigned ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to add
 *
 * @returns field's value as vector of 16 bit, unsigned ints
 * @throws cMsgException if no payload/field exists or field is not right type (16 bit, unsigned ints)
 */   
vector<uint16_t> *cMsgMessage::getUint16Vector(string name) const throw(cMsgException) {
  int len;
  const uint16_t *vals;
  int err = cMsgGetUint16Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  // put array values into a vector
  vector<uint16_t> *ints = new vector<uint16_t>(len);
  for (int i=0; i<len; i++) {
    ints->push_back(vals[i]);
  }
  return ints;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a pointer to a
 * vector of 32-bit, unsigned ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to add
 *
 * @returns field's value as vector of 32 bit, unsigned ints
 * @throws cMsgException if no payload/field exists or field is not right type (32 bit, unsigned ints)
 */   
vector<uint32_t> *cMsgMessage::getUint32Vector(string name) const throw(cMsgException) {
  int len;
  const uint32_t *vals;
  int err = cMsgGetUint32Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  // put array values into a vector
  vector<uint32_t> *ints = new vector<uint32_t>(len);
  for (int i=0; i<len; i++) {
    ints->push_back(vals[i]);
  }
  return ints;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a pointer to a
 * vector of 64-bit, unsigned ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to add
 *
 * @returns field's value as vector of 64 bit, unsigned ints
 * @throws cMsgException if no payload/field exists or field is not right type (64 bit, unsigned ints)
 */   
vector<uint64_t> *cMsgMessage::getUint64Vector(string name) const throw(cMsgException) {
  int len;
  const uint64_t *vals;
  int err = cMsgGetUint64Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException("No payload item of that name")); 
  }
  // put array values into a vector
  vector<uint64_t> *ints = new vector<uint64_t>(len);
  for (int i=0; i<len; i++) {
    ints->push_back(vals[i]);
  }
  return ints;
}

//-------------------------------------------------------------------
// ADD METHODS
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// BINARY
//-------------------------------------------------------------------

/**
 * This method adds a named binary field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param src  pointer to binary data to add
 * @param size size in bytes of data to add
 * @param endian endian value of binary data, may be CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *               CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 *
 * @throws cMsgException if no memory, error in binary-to-text conversion, name already used,
 *                       improper name, src is null, size < 1, or endian improper value
 */   
void cMsgMessage::addBinary(string name, const char *src, int size, int endian) {
  int err = cMsgAddBinary(myMsgPointer, name.c_str(), src, size, endian);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT)          throw(cMsgException("Improper name or if error in binary-to-text conversion"));
    else if (err == CMSG_BAD_ARGUMENT)   throw(cMsgException("src or name null, size < 1, or endian improper value")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------
// CMSG STRINGS
//-------------------------------------------------------------------

/**
 * This method adds a string to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param s string to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addString(string name, string s) {
    int err = cMsgAddString(myMsgPointer, name.c_str(), s.c_str());
    if (err != CMSG_OK) {
             if (err == CMSG_BAD_FORMAT)     throw(cMsgException("Improper name"));
        else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used"));
        else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available"));
        else throw(cMsgException("Error"));
    }
}

//-------------------------------------------------------------------

/**
 * This method adds an array of strings to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param strs array of C-style strings to add
 * @param len number of strings from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, strs is null, len < 1
 */
void cMsgMessage::addStringArray(string name, const char **strs, int len) {
    if (strs == NULL) throw (cMsgException("strs arg is null"));
    if (len < 1) throw (cMsgException("len < 1"));

	int err = cMsgAddStringArray(myMsgPointer, name.c_str(), strs, len);
	if (err != CMSG_OK) {
		if (err == CMSG_BAD_FORMAT)          throw(cMsgException("Improper name"));
		else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used"));
		else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available"));
		else throw(cMsgException("Error"));
	}
}

//-------------------------------------------------------------------

/**
 * This method adds an array of strings to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param strs array of strings to add
 * @param len number of strings from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, strs is null, len < 1
 */
void cMsgMessage::addStringArray(string name, string *strs, int len) {
  if (strs == NULL) throw (cMsgException("strs arg is null"));
  if (len < 1) throw (cMsgException("len < 1"));

  const char *strings[len];
  for (int i=0; i<len; i++) {
    strings[i] = strs[i].c_str();
  }

  int err = cMsgAddStringArray(myMsgPointer, name.c_str(), strings, len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT)          throw(cMsgException("Improper name"));
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used"));
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available"));
    else throw(cMsgException("Error"));
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a vector of strings to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param strs vector of strings to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */
void cMsgMessage::addStringVector(string name, vector<string> &strs) {
  const char *strings[strs.size()];
  for (vector<string>::size_type i=0; i < strs.size(); i++) {
    strings[i] = strs[i].c_str();
  }

  int err = cMsgAddStringArray(myMsgPointer, name.c_str(), strings, strs.size());
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT)          throw(cMsgException("Improper name"));
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used"));
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available"));
    else throw(cMsgException("Error"));
  }
}

//-------------------------------------------------------------------
// CMSG MESSAGES
//-------------------------------------------------------------------

/**
 * This method adds a cMsg message to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param msg  cMsgMessage object to add
 *
 * @throws cMsgException if no memory, name already used, improper name
 */
void cMsgMessage::addMessage(string name, cMsgMessage &msg) {
	int err = cMsgAddMessage(myMsgPointer, name.c_str(), msg.myMsgPointer);
	if (err != CMSG_OK) {
		if (err == CMSG_BAD_FORMAT)          throw(cMsgException("Improper name"));
		else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used"));
		else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available"));
		else throw(cMsgException("Error"));
	}
}

//-------------------------------------------------------------------

/**
 * This method adds an array of cMsg messages to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param msg  array of cMsgMessage objects to add
 * @param len number of objects from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, msg is null, len < 1
 */   
void cMsgMessage::addMessageArray(string name, cMsgMessage *msg, int len) {
  if (msg == NULL) throw (cMsgException("msg arg is null"));
  if (len < 1) throw (cMsgException("len < 1"));
  
  const void *msgs[len];
  for (int i=0; i<len; i++) {
    msgs[i] = msg[i].myMsgPointer;
  }

  int err = cMsgAddMessageArray(myMsgPointer, name.c_str(), msgs, len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT)          throw(cMsgException("Improper name"));
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used"));
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named vector of cMsgMessage objects to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param msg vector of cMsgMessage objects to add (copy)
 *
 * @throws cMsgException if no memory, name already used, improper name
 */   
void cMsgMessage::addMessageVector(string name, vector<cMsgMessage> &msg) {
  const void *msgs[msg.size()];
  
  for (vector<cMsgMessage>::size_type i=0; i < msg.size(); i++) {
    msgs[i] = msg[i].myMsgPointer;
  }
  
  int err = cMsgAddMessageArray(myMsgPointer, name.c_str(), msgs, msg.size());
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT)          throw(cMsgException("Improper name"));
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used"));
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------
// REALS
//-------------------------------------------------------------------

/**
 * This method adds a named float field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param val value of float to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addFloat(string name, float val) {
  int err = cMsgAddFloat(myMsgPointer, name.c_str(), val);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named double field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param val value of double to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addDouble(string name, double val) {
  int err = cMsgAddDouble(myMsgPointer, name.c_str(), val);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------
// REAL ARRAYS
//-------------------------------------------------------------------

/**
 * This method adds a named array of floats to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals array of floats to add (copy)
 * @param len number of floats from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, or vals is null
 */   
void cMsgMessage::addFloatArray(string name, float *vals, int len) {
  int err = cMsgAddFloatArray(myMsgPointer, name.c_str(), vals, len);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name or vals is null"));
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named vector of floats to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals vector of floats to add (copy)
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addFloatVector(string name, vector<float> &vals) {
  int err = cMsgAddFloatArray(myMsgPointer, name.c_str(), &vals[0], vals.size());
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named array of doubles to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals array of doubles to add (copy)
 * @param len number of doubles from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, or vals is null
 */   
void cMsgMessage::addDoubleArray(string name, double *vals, int len) {
  int err = cMsgAddDoubleArray(myMsgPointer, name.c_str(), vals, len);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name or vals is null")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named vector of doubles to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals vector of doubles to add (copy)
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addDoubleVector(string name, vector<double> &vals) {
  int err = cMsgAddDoubleArray(myMsgPointer, name.c_str(), &vals[0], vals.size());
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------
// INTS
//-------------------------------------------------------------------

/**
 * This method adds a named, 8-bit, signed int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param val value of 8-bit, signed int to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addInt8(string name, int8_t val) {
  int err = cMsgAddInt8(myMsgPointer, name.c_str(), val);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named, 16-bit, signed int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param val value of 16-bit, signed int to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addInt16(string name, int16_t val) {
  int err = cMsgAddInt16(myMsgPointer, name.c_str(), val);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named, 32-bit, signed int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param val value of 32-bit, signed int to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addInt32(string name, int32_t val) {
  int err = cMsgAddInt32(myMsgPointer, name.c_str(), val);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named, 64-bit, signed int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param val value of 64-bit, signed int to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addInt64(string name, int64_t val) {
  int err = cMsgAddInt64(myMsgPointer, name.c_str(), val);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named, 8-bit, unsigned int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param val value of 8-bit, unsigned int to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addUint8(string name, uint8_t val) {
  int err = cMsgAddUint8(myMsgPointer, name.c_str(), val);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named, 16-bit, unsigned int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param val value of 16-bit, unsigned int to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addUint16(string name, uint16_t val) {
  int err = cMsgAddUint16(myMsgPointer, name.c_str(), val);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named, 32-bit, unsigned int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param val value of 32-bit, unsigned int to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addUint32(string name, uint32_t val) {
  int err = cMsgAddUint32(myMsgPointer, name.c_str(), val);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named, 64-bit, unsigned int field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param val value of 64-bit, unsigned int to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addUint64(string name, uint64_t val) {
  int err = cMsgAddUint64(myMsgPointer, name.c_str(), val);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------
// INT ARRAYS
//-------------------------------------------------------------------

/**
 * This method adds a named array of 8-bit, signed ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals array of 8-bit, signed ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, or vals is null
 */   
void cMsgMessage::addInt8Array(string name, int8_t *vals, int len) {
  int err = cMsgAddInt8Array(myMsgPointer, name.c_str(), vals, len);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name or vals is null")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------
/**
 * This method adds a named vector of 8-bit, signed ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals vector of 8-bit, signed ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addInt8Vector(string name, vector<int8_t> &vals) {
  
  // Can transform vector into array since STL standard mandates continguous
  // memory for storage of vector data (ie it's a standard C++ technique).
  int err = cMsgAddInt8Array(myMsgPointer, name.c_str(), &vals[0], vals.size());
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named array of 16-bit, signed ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals array of 16-bit, signed ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, or vals is null
 */   
void cMsgMessage::addInt16Array(string name, int16_t *vals, int len) {
  int err = cMsgAddInt16Array(myMsgPointer, name.c_str(), vals, len);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name or vals is null")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------
/**
 * This method adds a named vector of 16-bit, signed ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals vector of 16-bit, signed ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addInt16Vector(string name, vector<int16_t> &vals) {
  
  // Can transform vector into array since STL standard mandates continguous
  // memory for storage of vector data (ie it's a standard C++ technique).
  int err = cMsgAddInt16Array(myMsgPointer, name.c_str(), &vals[0], vals.size());
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named array of 32-bit, signed ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals array of 32-bit, signed ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, or vals is null
 */   
void cMsgMessage::addInt32Array(string name, int32_t *vals, int len) {
  int err = cMsgAddInt32Array(myMsgPointer, name.c_str(), vals, len);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name or vals is null")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------
/**
 * This method adds a named vector of 32-bit, signed ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals vector of 32-bit, signed ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addInt32Vector(string name, vector<int32_t> &vals) {
  
  // Can transform vector into array since STL standard mandates continguous
  // memory for storage of vector data (ie it's a standard C++ technique).
  int err = cMsgAddInt32Array(myMsgPointer, name.c_str(), &vals[0], vals.size());
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named array of 64-bit, signed ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals array of 64-bit, signed ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, or vals is null
 */   
void cMsgMessage::addInt64Array(string name, int64_t *vals, int len) {
  int err = cMsgAddInt64Array(myMsgPointer, name.c_str(), vals, len);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name or vals is null")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------
/**
 * This method adds a named vector of 64-bit, signed ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals vector of 64-bit, signed ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addInt64Vector(string name, vector<int64_t> &vals) {
  
  // Can transform vector into array since STL standard mandates continguous
  // memory for storage of vector data (ie it's a standard C++ technique).
  int err = cMsgAddInt64Array(myMsgPointer, name.c_str(), &vals[0], vals.size());
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------
//-------------------------------------------------------------------

/**
 * This method adds a named array of 8-bit, unsigned ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals array of 8-bit, unsigned ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, or vals is null
 */   
void cMsgMessage::addUint8Array(string name, uint8_t *vals, int len) {
  int err = cMsgAddUint8Array(myMsgPointer, name.c_str(), vals, len);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name or vals is null")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------
/**
 * This method adds a named vector of 8-bit, unsigned ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals vector of 8-bit, unsigned ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addUint8Vector(string name, vector<uint8_t> &vals) {
  
  // Can transform vector into array since STL standard mandates continguous
  // memory for storage of vector data (ie it's a standard C++ technique).
  int err = cMsgAddUint8Array(myMsgPointer, name.c_str(), &vals[0], vals.size());
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named array of 16-bit, unsigned ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals array of 16-bit, unsigned ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, or vals is null
 */   
void cMsgMessage::addUint16Array(string name, uint16_t *vals, int len) {
  int err = cMsgAddUint16Array(myMsgPointer, name.c_str(), vals, len);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name or vals is null")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------
/**
 * This method adds a named vector of 16-bit, unsigned ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals vector of 16-bit, unsigned ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addUint16Vector(string name, vector<uint16_t> &vals) {
  
  // Can transform vector into array since STL standard mandates continguous
  // memory for storage of vector data (ie it's a standard C++ technique).
  int err = cMsgAddUint16Array(myMsgPointer, name.c_str(), &vals[0], vals.size());
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named array of 32-bit, unsigned ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals array of 32-bit, unsigned ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, or vals is null
 */   
void cMsgMessage::addUint32Array(string name, uint32_t *vals, int len) {
  int err = cMsgAddUint32Array(myMsgPointer, name.c_str(), vals, len);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name or vals is null")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------
/**
 * This method adds a named vector of 32-bit, unsigned ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals vector of 32-bit, unsigned ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addUint32Vector(string name, vector<uint32_t> &vals) {
  
  // Can transform vector into array since STL standard mandates continguous
  // memory for storage of vector data (ie it's a standard C++ technique).
  int err = cMsgAddUint32Array(myMsgPointer, name.c_str(), &vals[0], vals.size());
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------

/**
 * This method adds a named array of 64-bit, unsigned ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals array of 64-bit, unsigned ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, or vals is null
 */   
void cMsgMessage::addUint64Array(string name, uint64_t *vals, int len) {
  int err = cMsgAddUint64Array(myMsgPointer, name.c_str(), vals, len);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name or vals is null")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------
/**
 * This method adds a named vector of 64-bit, unsigned ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals vector of 64-bit, unsigned ints to add (copy)
 * @param len number of ints from array to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::addUint64Vector(string name, vector<uint64_t> &vals) {
  
  // Can transform vector into array since STL standard mandates continguous
  // memory for storage of vector data (ie it's a standard C++ technique).
  int err = cMsgAddUint64Array(myMsgPointer, name.c_str(), &vals[0], vals.size());
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT ||
             err == CMSG_BAD_ARGUMENT)   throw(cMsgException("Improper name")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

//-------------------------------------------------------------------


} // namespace cmsg
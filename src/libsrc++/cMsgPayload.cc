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
     * This method removes all the user-added items in the payload.
     * The payload may still contain fields added by the cMsg system.
     */
    void cMsgMessage::payloadClear(void) {
        cMsgPayloadClear(myMsgPointer);
    }

    //-----------------------------------------------------------------------------

    /**
     * This method removes all items (including those added by the cMsg system)
     * in the payload.
     * This method frees the allocated memory of the given message's entire payload
     * and then initializes the payload components of the message.
     */
    void cMsgMessage::payloadReset(void) {
        cMsgPayloadReset(myMsgPointer);
    }

    //-------------------------------------------------------------------

    /**
     * This method prints out the message payload in a readable form.
     */
    void cMsgMessage::payloadPrint(void) const {
        cMsgPayloadPrint(myMsgPointer);
    }

    //-------------------------------------------------------------------

    /**
     * This method returns whether a message has a compound payload or not. 
     *
     * @return true if message has a payload, else false
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
     * @return true if an item in the payload has the given name, else false
     */
    bool cMsgMessage::payloadContainsName(const string &name) const {
        return cMsgPayloadContainsName(myMsgPointer, name.c_str()) ? true : false;
    }

    //-------------------------------------------------------------------

    /**
     * This method returns the number of payload items a message has. 
     *
     * @return number of payload items a message has
     */
    int cMsgMessage::payloadGetCount() const {
        int count;
        cMsgPayloadGetCount(myMsgPointer, &count);
        return count;
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
     * @return the type of data associated with the payload
     *          field given by the name argument
     * @throws cMsgException if no payload/field exists, or if name is NULL
     */
    int cMsgMessage::payloadGetType(const string &name) const
            throw(cMsgException) {
        int err, type;
        err = cMsgPayloadGetType(myMsgPointer, name.c_str(), &type);
        if (err != CMSG_OK) {
            if (err == CMSG_BAD_ARGUMENT)
                throw(cMsgException("Name is null"));
            else
              throw(cMsgException(string("No payload item named ") + name));
        }
        return type;
    }

    //-------------------------------------------------------------------

    /**
     * This method copies the payload from another message.
     * The original payload is overwritten.
     *
     * @param msg reference to message to copy payload from
     * @throws cMsgException if no memory
     */
    void cMsgMessage::payloadCopy(const cMsgMessage &msg) throw(cMsgException) {
        int err = cMsgPayloadCopy(msg.myMsgPointer, myMsgPointer);
        if (err!= CMSG_OK) {
            throw(cMsgException(cMsgPerror(err),err));
        }
    }

    //-------------------------------------------------------------------

    /**
     * This method returns a text representation of the payload. Its format
     * is proprietary - not XML.
     *
     * @return payload representation string
     */
    string cMsgMessage::payloadGetText() const {
        const char *txt;
        cMsgGetPayloadText(myMsgPointer, &txt);
        string s(txt);
        return s;
    }

    //-------------------------------------------------------------------

    /**
     * This method takes a string returned from {@link #payloadGetText} as an
     * argument and constructs a payload out of it. Any existing payload is
     * overwritten.
     *
     * @param txt string representing payload
     */
    void cMsgMessage::payloadSetFromText(const string &txt)
          throw(cMsgException) {
        int err = cMsgPayloadSetAllFieldsFromText(myMsgPointer, txt.c_str());
        if (err != CMSG_OK) {
            throw(cMsgException(cMsgPerror(err),err));
        }
        return;
    }

    //-------------------------------------------------------------------

    /**
     * This method returns a description of the given field name in the payload.
     *
     * @return field description string
     * @throws cMsgException if no such field in the payload
     */
    string cMsgMessage::payloadGetFieldDescription(const string &name) const
            throw(cMsgException) {
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
     * @return a pointer to a map containing all name/type pairs of the payload
     * @throws cMsgException if no payload exists, or if name is NULL
     */
    map<string,int> *cMsgMessage::payloadGet() const throw(cMsgException) {
        char **names;
        int *types, len;

        int err = cMsgPayloadGetInfo(myMsgPointer, &names, &types, &len);
        if (err != CMSG_OK) {
            if (err == CMSG_BAD_ARGUMENT)
                throw(cMsgException("Name is null"));
            else if (err == CMSG_ERROR)
                throw(cMsgException("No payload exists"));
            else
                throw(cMsgException("Out of memory"));
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
     * @return true if successful
     * @return false if no field with that name was found
     */
    bool cMsgMessage::payloadRemoveField(const string &name) {
        return cMsgPayloadRemove(myMsgPointer, name.c_str()) == 0 ? false : true;
    }


    //-------------------------------------------------------------------

    /**
     * This method sets the maximum number of entries this message keeps
     * of its history of various parameters (sender's name, host, time).
     *
     * @param len max number of entries this message keeps
     *            of its history of various parameters
     *
     * @throws cMsgException if len < 0 or > CMSG_HISTORY_LENGTH_ABS_MAX
     */
    void cMsgMessage::setHistoryLengthMax(int len) const throw(cMsgException) {
        int err = cMsgSetHistoryLengthMax(myMsgPointer, len);
        if (err != CMSG_OK) throw (cMsgException("len must be >= 0 and < CMSG_HISTORY_LENGTH_ABS_MAX"));
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
 * @param name name of field to get
 * @param val address of pointer to data which sets the pointer to binary
 * @param len int reference which gets set to the number of bytes in binary array
 * @param endian int reference which gets set to endian of data (CMSG_ENDIAN_BIG/LITTLE)
 *
 * @throws cMsgException if no payload/field exists or field is not right type,
 *                        or if any arg is NULL
 */
void cMsgMessage::getBinary(const string &name, const char **val, int &len, int &endian)
        const throw(cMsgException) {
    int err = cMsgGetBinary(myMsgPointer, name.c_str(), val, &len, &endian);
    if (err != CMSG_OK) {
        if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type"));
        else throw(cMsgException(string("No payload item named ") + name));
    }
    return;
}

/**
 * This method returns the value of the given field as a binary array if it exists.
 *
 * @param name name of field to get
 * @param vals address of array of binary arrays which sets the array pointer
 * @param lens pointer to array filled with the number of bytes in each binary array
 * @param endians pointer to array filled in with the endianness of data
 *                (CMSG_ENDIAN_BIG/LITTLE) in each binary array
 * @param count int reference which gets set to number of binary arrays returned
 *
 * @throws cMsgException if no payload/field exists or field is not right type,
 *                        or if any arg is NULL
 */
void cMsgMessage::getBinaryArray(const string &name, const char ***vals,
                                 int **lens, int **endians, int &count)
        const throw(cMsgException) {
    int err = cMsgGetBinaryArray(myMsgPointer, name.c_str(), vals, lens, endians, &count);
    if (err != CMSG_OK) {
        if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type"));
        else throw(cMsgException(string("No payload item named ") + name));
    }
    return;
}

//-------------------------------------------------------------------
// CMSG MESSAGES
//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a cMsgMessage
 * object pointer if its exists.  A copy of the message is made, so the
 * pointer must be deleted to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as cMsg message
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
cMsgMessage *cMsgMessage::getMessage(const string &name) const throw(cMsgException) {
  const void *val;
  int err = cMsgGetMessage(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type"));
    else throw(cMsgException(string("No payload item named ") + name));
  }

  // copy message
  void *newMsgPointer=cMsgCopyMessage(val);
  if(newMsgPointer==NULL)
    throw(cMsgException("?cMsgMessage::getMessage...unable to create new message from message contents",CMSG_ERROR));

  return(new cMsgMessage(newMsgPointer));
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to a
 * vector of pointers to cMsgMessage objects, if its exists. The vector pointer
 * must be deleted by caller to avoid a memory leak, as do all the messages it 
 * contains.
 *
 * @param name name of field to get
 * @return field's value as vector of pointers to cMsgMessage objects
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
vector<cMsgMessage*> *cMsgMessage::getMessagePVector(const string &name) const throw(cMsgException) {
  int len;
  const void **vals;
  int err = cMsgGetMessageArray(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  

  // fill new vector with message copies
  vector<cMsgMessage*> *msgPVec = new vector<cMsgMessage*>;
  for (int i=0; i<len; i++) msgPVec->push_back(new cMsgMessage(cMsgCopyMessage(vals[i])));

  return(msgPVec);
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to a
 * vector of cMsgMessage objects, if its exists. The vector pointer
 * must be deleted by caller to avoid a memory leak, as do all the messages it 
 * contains.
 *
 * @param name name of field to get
 * @return field's value as vector of cMsgMessage objects
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
vector<cMsgMessage> *cMsgMessage::getMessageVector(const string &name) const throw(cMsgException) {
  int len;
  const void **vals;
  int err = cMsgGetMessageArray(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  

  // fill new vector with message copies
  vector<cMsgMessage> *msgVec = new vector<cMsgMessage>;
  for (int i=0; i<len; i++) msgVec->push_back(cMsgMessage(cMsgCopyMessage(vals[i])));

  return(msgVec);
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to an
 * array of pointers to cMsgMessage objects, if it exists. The array
 * must be deleted by caller to avoid a memory leak, as do all the messages it 
 * contains.
 *
 * @param name name of field to get
 * @return field's value as array of pointers to cMsgMessage objects
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
cMsgMessage* *cMsgMessage::getMessagePArray(const string &name) const throw(cMsgException) {
  int len;
  const void **vals;
  int err = cMsgGetMessageArray(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  
  // create and fill array with pointers to message copies
  cMsgMessage* *msgPArray = new cMsgMessage*[len];
  for (int i=0; i<len; i++) msgPArray[i]=(new cMsgMessage(cMsgCopyMessage(vals[i])));

  return(msgPArray);
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to an
 * array of cMsgMessage objects, if it exists. The array
 * must be deleted by caller to avoid a memory leak, as do all the messages it 
 * contains.
 *
 * @param name name of field to get
 * @return field's value as array of cMsgMessage objects
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
cMsgMessage *cMsgMessage::getMessageArray(const string &name) const throw(cMsgException) {
  int len;
  const void **vals;
  int err = cMsgGetMessageArray(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  
  // create and fill array with message copies
  cMsgMessage *msgArray = new cMsgMessage[len];
  for (int i=0; i<len; i++) msgArray[i]=(cMsgMessage(cMsgCopyMessage(vals[i])));

  return(msgArray);
}


//-------------------------------------------------------------------
// STRINGS
//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a string if its exists.
 *
 * @param name name of field to get
 * @return field's value as string
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
string cMsgMessage::getString(const string &name) const throw(cMsgException) {
  const char *val;
  int err = cMsgGetString(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  string s(val);
  return s;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to a
 * vector of strings if its exists. The vector must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as vector of strings
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
vector<string> *cMsgMessage::getStringVector(const string &name) const throw(cMsgException) {
  int len;
  const char **vals;
  int err = cMsgGetStringArray(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  
  // copy array values and put into a vector
  vector<string> *strs = new vector<string>;
  for (int i=0; i<len; i++) strs->push_back(string(vals[i]));
  return strs;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to an
 * array of strings if its exists. The array must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as vector of strings
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
string *cMsgMessage::getStringArray(const string &name) const throw(cMsgException) {
  int len;
  const char **vals;
  int err = cMsgGetStringArray(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  
  // place strings into new array
  string *strs = new string[len];
  for (int i=0; i<len; i++) strs[i]=string(vals[i]);
  return strs;
}


//-------------------------------------------------------------------
// REALS
//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a float if its exists.
 *
 * @param name name of field to get
 * @return field's value as a float
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
float cMsgMessage::getFloat(const string &name) const throw(cMsgException) {
  float val;
  int err = cMsgGetFloat(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as a double if its exists.
 *
 * @param name name of field to get
 * @return field's value as a double
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
double cMsgMessage::getDouble(const string &name) const throw(cMsgException) {
  double val;
  int err = cMsgGetDouble(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
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
 * @param name name of field to get
 * @return field's value as vector of floats
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
vector<float> *cMsgMessage::getFloatVector(const string &name) const throw(cMsgException) {
  int len;
  const float *vals;
  int err = cMsgGetFloatArray(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put array values into a vector
  vector<float> *flts = new vector<float>;
  for (int i=0; i<len; i++) flts->push_back(vals[i]);
  return flts;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to an
 * array of floats if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as array of floats
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
float *cMsgMessage::getFloatArray(const string &name) const throw(cMsgException) {
  int len;
  const float *vals;
  int err = cMsgGetFloatArray(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }

  // copy values into a new array
  float *flts = new float[len];
  for (int i=0; i<len; i++) flts[i]=vals[i];
  return flts;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to a
 * vector of doubles if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as vector of doubles
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
vector<double> *cMsgMessage::getDoubleVector(const string &name) const throw(cMsgException) {
  int len;
  const double *vals;
  int err = cMsgGetDoubleArray(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type"));
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put array values into a vector
  vector<double> *dbls = new vector<double>;
  for (int i=0; i<len; i++) dbls->push_back(vals[i]);
  return dbls;
}



//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as an array
 * of doubles if its exists. The array must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as array of doubles
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
double *cMsgMessage::getDoubleArray(const string &name) const throw(cMsgException) {
  int len;
  const double *vals;
  int err = cMsgGetDoubleArray(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type"));
    else throw(cMsgException(string("No payload item named ") + name));
  }

  // put values into new array
  double *dbls = new double[len];
  for (int i=0; i<len; i++) dbls[i]=vals[i];
  return dbls;
}


//-------------------------------------------------------------------
// INTS
//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 8 bit, signed integer if its exists.
 *
 * @param name name of field to get
 * @return field's value as an 8-bit, signed integer
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
int8_t cMsgMessage::getInt8(const string &name) const throw(cMsgException) {
  int8_t val;
  int err = cMsgGetInt8(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 16 bit, signed integer if its exists.
 *
 * @param name name of field to get
 * @return field's value as an 16-bit, signed integer
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
int16_t cMsgMessage::getInt16(const string &name) const throw(cMsgException) {
  int16_t val;
  int err = cMsgGetInt16(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 32 bit, signed integer if its exists.
 *
 * @param name name of field to get
 * @return field's value as an 32-bit, signed integer
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
int32_t cMsgMessage::getInt32(const string &name) const throw(cMsgException) {
  int32_t val;
  int err = cMsgGetInt32(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 64 bit, signed integer if its exists.
 *
 * @param name name of field to get
 * @return field's value as an 64-bit, signed integer
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
int64_t cMsgMessage::getInt64(const string &name) const throw(cMsgException) {
  int64_t val;
  int err = cMsgGetInt64(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 8 bit, unsigned integer if its exists.
 *
 * @param name name of field to get
 * @return field's value as an 8-bit, unsigned integer
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
uint8_t cMsgMessage::getUint8(const string &name) const throw(cMsgException) {
  uint8_t val;
  int err = cMsgGetUint8(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 16 bit, unsigned integer if its exists.
 *
 * @param name name of field to get
 * @return field's value as an 16-bit, unsigned integer
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
uint16_t cMsgMessage::getUint16(const string &name) const throw(cMsgException) {
  uint16_t val;
  int err = cMsgGetUint16(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 32 bit, unsigned integer if its exists.
 *
 * @param name name of field to get
 * @return field's value as an 32-bit, unsigned integer
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
uint32_t cMsgMessage::getUint32(const string &name) const throw(cMsgException) {
  uint32_t val;
  int err = cMsgGetUint32(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  return val;
}

//-------------------------------------------------------------------

/**
 * This method returns the value of the given field as an 64 bit, unsigned integer if its exists.
 *
 * @param name name of field to get
 * @return field's value as an 64-bit, unsigned integer
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
uint64_t cMsgMessage::getUint64(const string &name) const throw(cMsgException) {
  uint64_t val;
  int err = cMsgGetUint64(myMsgPointer, name.c_str(), &val);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
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
 * @param name name of field to get
 * @return field's value as vector of 8 bit, signed ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
vector<int8_t> *cMsgMessage::getInt8Vector(const string &name) const throw(cMsgException) {
  int len;
  const int8_t *vals;
  int err = cMsgGetInt8Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put array values into a vector
  vector<int8_t> *ints = new vector<int8_t>;
  for (int i=0; i<len; i++) ints->push_back(vals[i]);
  return ints;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as an
 * array of 8-bit, signed ints if its exists. The array must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as vector of 8 bit, signed ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
int8_t *cMsgMessage::getInt8Array(const string &name) const throw(cMsgException) {
  int len;
  const int8_t *vals;
  int err = cMsgGetInt8Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put values into new array
  int8_t *ints = new int8_t[len];
  for (int i=0; i<len; i++) ints[i]=vals[i];
  return ints;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to a
 * vector of 16-bit, signed ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as vector of 16 bit, signed ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
vector<int16_t> *cMsgMessage::getInt16Vector(const string &name) const throw(cMsgException) {
  int len;
  const int16_t *vals;
  int err = cMsgGetInt16Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put array values into a vector
  vector<int16_t> *ints = new vector<int16_t>;
  for (int i=0; i<len; i++) ints->push_back(vals[i]);
  return ints;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as an array
 * of 16-bit, signed ints if its exists. The array must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as array of 16 bit, signed ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
int16_t *cMsgMessage::getInt16Array(const string &name) const throw(cMsgException) {
  int len;
  const int16_t *vals;
  int err = cMsgGetInt16Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put values into new array
  int16_t *ints = new int16_t[len];
  for (int i=0; i<len; i++) ints[i]=vals[i];
  return ints;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to a
 * vector of 32-bit, signed ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as vector of 32 bit, signed ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
vector<int32_t> *cMsgMessage::getInt32Vector(const string &name) const throw(cMsgException) {
  int len;
  const int32_t *vals;
  int err = cMsgGetInt32Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put array values into a vector
  vector<int32_t> *ints = new vector<int32_t>;
  for (int i=0; i<len; i++) ints->push_back(vals[i]);
  return ints;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as an array
 * of 32-bit, signed ints if its exists. The array must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as array of 32 bit, signed ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
int32_t *cMsgMessage::getInt32Array(const string &name) const throw(cMsgException) {
  int len;
  const int32_t *vals;
  int err = cMsgGetInt32Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put values into new array
  int32_t *ints = new int32_t[len];
  for (int i=0; i<len; i++) ints[i]=vals[i];
  return ints;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to a
 * vector of 64-bit, signed ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as vector of 64 bit, signed ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
vector<int64_t> *cMsgMessage::getInt64Vector(const string &name) const throw(cMsgException) {
  int len;
  const int64_t *vals;
  int err = cMsgGetInt64Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put array values into a vector
  vector<int64_t> *ints = new vector<int64_t>;
  for (int i=0; i<len; i++) ints->push_back(vals[i]);
  return ints;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as an array
 * of 64-bit, signed ints if its exists. The array must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as array of 64 bit, signed ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
int64_t *cMsgMessage::getInt64Array(const string &name) const throw(cMsgException) {
  int len;
  const int64_t *vals;
  int err = cMsgGetInt64Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put values into new array 
  int64_t *ints = new int64_t[len];
  for (int i=0; i<len; i++) ints[i]=vals[i];
  return ints;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to a
 * vector of 8-bit, unsigned ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 *
 * @return field's value as vector of 8 bit, unsigned ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
vector<uint8_t> *cMsgMessage::getUint8Vector(const string &name) const throw(cMsgException) {
  int len;
  const uint8_t *vals;
  int err = cMsgGetUint8Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put array values into a vector
  vector<uint8_t> *ints = new vector<uint8_t>;
  for (int i=0; i<len; i++) ints->push_back(vals[i]);
  return ints;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as an array
 * of 8-bit, unsigned ints if its exists. The array must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 *
 * @return field's value as array of 8 bit, unsigned ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
uint8_t *cMsgMessage::getUint8Array(const string &name) const throw(cMsgException) {
  int len;
  const uint8_t *vals;
  int err = cMsgGetUint8Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put values into new array
  uint8_t *ints = new uint8_t[len];
  for (int i=0; i<len; i++) ints[i]=vals[i];
  return ints;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to a
 * vector of 16-bit, unsigned ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 *
 * @return field's value as vector of 16 bit, unsigned ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
vector<uint16_t> *cMsgMessage::getUint16Vector(const string &name) const throw(cMsgException) {
  int len;
  const uint16_t *vals;
  int err = cMsgGetUint16Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put array values into a vector
  vector<uint16_t> *ints = new vector<uint16_t>;
  for (int i=0; i<len; i++) ints->push_back(vals[i]);
  return ints;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as an array
 * of 16-bit, unsigned ints if its exists. The array must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 *
 * @return field's value as array of 16 bit, unsigned ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
uint16_t *cMsgMessage::getUint16Array(const string &name) const throw(cMsgException) {
  int len;
  const uint16_t *vals;
  int err = cMsgGetUint16Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put values into new array
  uint16_t *ints = new uint16_t[len];
  for (int i=0; i<len; i++) ints[i]=vals[i];
  return ints;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to a
 * vector of 32-bit, unsigned ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as vector of 32 bit, unsigned ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
vector<uint32_t> *cMsgMessage::getUint32Vector(const string &name) const throw(cMsgException) {
  int len;
  const uint32_t *vals;
  int err = cMsgGetUint32Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put array values into a vector
  vector<uint32_t> *ints = new vector<uint32_t>;
  for (int i=0; i<len; i++) ints->push_back(vals[i]);
  return ints;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a array
 * of 32-bit, unsigned ints if its exists. The array must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as array of 32 bit, unsigned ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
uint32_t *cMsgMessage::getUint32Array(const string &name) const throw(cMsgException) {
  int len;
  const uint32_t *vals;
  int err = cMsgGetUint32Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put values into new array
  uint32_t *ints = new uint32_t[len];
  for (int i=0; i<len; i++) ints[i]=vals[i];
  return ints;
}


//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as a pointer to a
 * vector of 64-bit, unsigned ints if its exists. The vector pointer must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as vector of 64 bit, unsigned ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
vector<uint64_t> *cMsgMessage::getUint64Vector(const string &name) const throw(cMsgException) {
  int len;
  const uint64_t *vals;
  int err = cMsgGetUint64Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put array values into a vector
  vector<uint64_t> *ints = new vector<uint64_t>;
  for (int i=0; i<len; i++) ints->push_back(vals[i]);
  return ints;
}



//-------------------------------------------------------------------


/**
 * This method returns the value of the given field as an array
 * of 64-bit, unsigned ints if its exists. The array must be deleted
 * by caller to avoid a memory leak.
 *
 * @param name name of field to get
 * @return field's value as array of 64 bit, unsigned ints
 * @throws cMsgException if no payload/field exists or field is not right type
 */   
uint64_t *cMsgMessage::getUint64Array(const string &name) const throw(cMsgException) {
  int len;
  const uint64_t *vals;
  int err = cMsgGetUint64Array(myMsgPointer, name.c_str(), &vals, &len);
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT) throw(cMsgException("Wrong field type")); 
    else throw(cMsgException(string("No payload item named ") + name));
  }
  // put values into new array 
  uint64_t *ints = new uint64_t[len];
  for (int i=0; i<len; i++) ints[i]=vals[i];
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
 * Names may not begin with "cmsg" (case insensitive), be longer than
 * CMSG_PAYLOAD_NAME_LEN, or contain white space or quotes.
 *
 * @param name name of field to add
 * @param src  pointer to binary data to add
 * @param size size in bytes of data to add
 * @param endian endian value of binary data, may be CMSG_ENDIAN_BIG,
 *               CMSG_ENDIAN_LITTLE, CMSG_ENDIAN_LOCAL, or
 *               CMSG_ENDIAN_NOTLOCAL
 *
 * @throws cMsgException if no memory, error in binary-to-text conversion, name already used,
 *                       improper name, src is null, size < 1, or endian improper value
 */   
void cMsgMessage::add(const string &name, const char *src, int size, int endian) {
  int err = cMsgAddBinary(myMsgPointer, name.c_str(), src, size, endian);
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT)     throw(cMsgException("Improper name or if error in binary-to-text conversion"));
    else if (err == CMSG_BAD_ARGUMENT)   throw(cMsgException("src or name null, size < 1, or endian improper value")); 
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used")); 
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
}

/**
 * This method adds a named binary field to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than
 * CMSG_PAYLOAD_NAME_LEN, or contain white space or quotes.
 *
 * @param name name of field to add
 * @param srcs  pointer to array of binary data arrays to add
 * @param number number of arrays of binary data to add
 * @param sizes array of sizes in bytes of binary data arrays to add
 * @param endians array of endian values of binary data arrays, may be
 *               CMSG_ENDIAN_BIG, CMSG_ENDIAN_LITTLE,
 *               CMSG_ENDIAN_LOCAL, or CMSG_ENDIAN_NOTLOCAL
 *
 * @throws cMsgException if no memory, error in binary-to-text conversion, name already used,
 *                       improper name, src is null, size < 1, or endian improper value
 */
void cMsgMessage::add(const string &name, const char **srcs, int number,
                      const int sizes[], const int endians[]) {
    int err = cMsgAddBinaryArray(myMsgPointer, name.c_str(), srcs, number, sizes, endians);
    if (err != CMSG_OK) {
        if (err == CMSG_BAD_FORMAT) throw(cMsgException("Improper name or if error in binary-to-text conversion"));
        else if (err == CMSG_BAD_ARGUMENT)   throw(cMsgException("srcs or name null, sizes < 1, or endians improper value"));
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
 * Names may not begin with "cmsg" (case insensitive), be longer than
 * CMSG_PAYLOAD_NAME_LEN, or contain white space or quotes.
 *
 * @param name name of field to add
 * @param s string to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const string &s) {
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
 * This method adds a string to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than
 * CMSG_PAYLOAD_NAME_LEN, or contain white space or quotes.
 *
 * @param name name of field to add
 * @param s pointer to string to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const string *s) {
  add(name,*s);
}

//-------------------------------------------------------------------

/**
 * This method adds an array of strings to the compound payload of a message.
  * Names may not begin with "cmsg" (case insensitive), be longer than
 * CMSG_PAYLOAD_NAME_LEN, or contain white space or quotes.
 *
 * @param name name of field to add
 * @param strs array of C-style strings to add
 * @param len number of strings from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, strs is null, len < 1
 */
void cMsgMessage::add(const string &name, const char **strs, int len) {
    if (strs == NULL) throw (cMsgException("strs arg is null"));
    if (len < 1) throw (cMsgException("string array len < 1"));

	int err = cMsgAddStringArray(myMsgPointer, name.c_str(), strs, len);
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
 * Names may not begin with "cmsg" (case insensitive), be longer than
 * CMSG_PAYLOAD_NAME_LEN, or contain white space or quotes.
 *
 * @param name name of field to add
 * @param strs array of strings to add
 * @param len number of strings from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, strs is null, len < 1
 */
void cMsgMessage::add(const string &name, const string *strs, int len) {
  if (strs == NULL) throw (cMsgException("strs arg is null"));
  if (len < 1) throw (cMsgException("string array len < 1"));

#ifdef linux
  const char *strings[len];
#else
  const char **strings = (const char **)malloc(len*sizeof(char *));
  if (strings == NULL) throw(cMsgException("No memory available"));
#endif
  
  for (int i=0; i<len; i++) {
    strings[i] = strs[i].c_str();
  }

  int err = cMsgAddStringArray(myMsgPointer, name.c_str(), strings, len);
#ifndef linux
  free(strings);
#endif
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT)     throw(cMsgException("Improper name"));
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used"));
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available"));
    else throw(cMsgException("Error"));
  }
 
}

//-------------------------------------------------------------------

/**
 * This method adds a vector of strings to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than
 * CMSG_PAYLOAD_NAME_LEN, or contain white space or quotes.
 *
 * @param name name of field to add
 * @param strs vector of strings to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */
void cMsgMessage::add(const string &name, const vector<string> &strs) {
    if (strs.size() < 1) throw(cMsgException("Zero length vector"));
#ifdef linux
    const char *strings[strs.size()];
#else
    const char **strings = (const char **)malloc(strs.size()*sizeof(char *));
    if (strings == NULL) throw(cMsgException("No memory available"));
#endif

  for (vector<string>::size_type i=0; i < strs.size(); i++) {
    strings[i] = strs[i].c_str();
  }

  int err = cMsgAddStringArray(myMsgPointer, name.c_str(), strings, strs.size());
#ifndef linux
  free(strings);
#endif
  if (err != CMSG_OK) {
         if (err == CMSG_BAD_FORMAT)     throw(cMsgException("Improper name"));
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used"));
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available"));
    else throw(cMsgException("Error"));
  }
}

/**
 * This method adds a vector of strings to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than
 * CMSG_PAYLOAD_NAME_LEN, or contain white space or quotes.
 *
 * @param name name of field to add
 * @param strs pointer to vector of strings to add
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */
void cMsgMessage::add(const string &name, const vector<string> *strs) {
  add(name,*strs);
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
void cMsgMessage::add(const string &name, const cMsgMessage &msg) {
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
 * This method adds a cMsg message to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param msg  pointer to cMsgMessage object to add
 *
 * @throws cMsgException if no memory, name already used, improper name
 */
void cMsgMessage::add(const string &name, const cMsgMessage *msg) {
  add(name,*msg);
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
void cMsgMessage::add(const string &name, const cMsgMessage *msg, int len) {
  if (msg == NULL) throw (cMsgException("msg arg is null"));
  if (len < 1) throw (cMsgException("cmsg message array len < 1"));
  
#ifdef linux
  const void *msgs[len];
#else
  const void **msgs = (const void **)malloc(len*sizeof(void *));
  if (msgs == NULL) throw(cMsgException("No memory available"));
#endif

  for (int i=0; i<len; i++) {
    msgs[i] = msg[i].myMsgPointer;
  }

  int err = cMsgAddMessageArray(myMsgPointer, name.c_str(), msgs, len);
#ifndef linux
  free(msgs);
#endif
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
 * @param msg  array of pointers to cMsgMessage objects to add
 * @param len number of objects from array to add
 *
 * @throws cMsgException if no memory, name already used, improper name, msg is null, len < 1
 */   
void cMsgMessage::add(const string &name, const cMsgMessage* *msg, int len) {
  if (msg == NULL) throw (cMsgException("msg arg is null"));
  if (len < 1) throw (cMsgException("cmsg message array len < 1"));
  
#ifdef linux
  const void *msgs[len];
#else
  const void **msgs = (const void **)malloc(len*sizeof(void *));
  if (msgs == NULL) throw(cMsgException("No memory available"));
#endif

  for (int i=0; i<len; i++) {
    msgs[i] = msg[i]->myMsgPointer;
  }

  int err = cMsgAddMessageArray(myMsgPointer, name.c_str(), msgs, len);
#ifndef linux
  free(msgs);
#endif
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT)          throw(cMsgException("Improper name"));
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used"));
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
  
}


//-------------------------------------------------------------------


/**
 * This method adds a named vector of pointers to cMsgMessage objects to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param msgPVec vector of pointers to cMsgMessage to add (copy)
 *
 * @throws cMsgException if no memory, name already used, improper name
 */   
void cMsgMessage::add(const string &name, const vector<cMsgMessage*> &msgPVec) {
    if (msgPVec.size() < 1) throw(cMsgException("Zero length vector"));
#ifdef linux
    const void *msgs[msgPVec.size()];
#else
    const void **msgs = (const void **)malloc(msgPVec.size()*sizeof(void *));
    if (msgs == NULL) throw(cMsgException("No memory available"));
#endif
  
  for (vector<cMsgMessage*>::size_type i=0; i < msgPVec.size(); i++) {
    msgs[i] = msgPVec[i]->myMsgPointer;
  }
  
  int err = cMsgAddMessageArray(myMsgPointer, name.c_str(), msgs, msgPVec.size());
#ifndef linux
  free(msgs);
#endif
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
 * @param msgVec vector of cMsgMessage objects to add (copy)
 *
 * @throws cMsgException if no memory, name already used, improper name
 */   
void cMsgMessage::add(const string &name, const vector<cMsgMessage> &msgVec) {
    if (msgVec.size() < 1) throw(cMsgException("Zero length vector"));
#ifdef linux
    const void *msgs[msgVec.size()];
#else
    const void **msgs = (const void **)malloc(msgVec.size()*sizeof(void *));
    if (msgs == NULL) throw(cMsgException("No memory available"));
#endif
  
  for (vector<cMsgMessage>::size_type i=0; i < msgVec.size(); i++) {
    msgs[i] = msgVec[i].myMsgPointer;
  }
  
  int err = cMsgAddMessageArray(myMsgPointer, name.c_str(), msgs, msgVec.size());
#ifndef linux
  free(msgs);
#endif
  if (err != CMSG_OK) {
    if (err == CMSG_BAD_FORMAT)          throw(cMsgException("Improper name"));
    else if (err == CMSG_ALREADY_EXISTS) throw(cMsgException("Name being used"));
    else if (err == CMSG_OUT_OF_MEMORY)  throw(cMsgException("No memory available")); 
    else throw(cMsgException("Error")); 
  }
  
}


//-------------------------------------------------------------------


/**
 * This method adds a named vector of pointers to cMsgMessage objects to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param msgPVec pointer to vector of pointers to cMsgMessage to add (copy)
 *
 * @throws cMsgException if no memory, name already used, improper name
 */   
void cMsgMessage::add(const string &name, const vector<cMsgMessage*> *msgPVec) {
  add(name,*msgPVec);
}


//-------------------------------------------------------------------


/**
 * This method adds a named vector of cMsgMessage objects to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param msgVec pointer to vector of cMsgMessage objects to add (copy)
 *
 * @throws cMsgException if no memory, name already used, improper name
 */   
void cMsgMessage::add(const string &name, const vector<cMsgMessage> *msgVec) {
  add(name,*msgVec);
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
void cMsgMessage::add(const string &name, float val) {
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
void cMsgMessage::add(const string &name, double val) {
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
void cMsgMessage::add(const string &name, const float *vals, int len) {
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
void cMsgMessage::add(const string &name, const vector<float> &vals) {
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
 * This method adds a named vector of floats to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals pointer to vector of floats to add (copy)
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<float> *vals) {
  add(name,*vals);
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
void cMsgMessage::add(const string &name, const double *vals, int len) {
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
void cMsgMessage::add(const string &name, const vector<double> &vals) {
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

/**
 * This method adds a named vector of doubles to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals pointer to vector of doubles to add (copy)
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<double> *vals) {
  add(name,*vals);
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
void cMsgMessage::add(const string &name, int8_t val) {
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
void cMsgMessage::add(const string &name, int16_t val) {
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
void cMsgMessage::add(const string &name, int32_t val) {
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
void cMsgMessage::add(const string &name, int64_t val) {
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
void cMsgMessage::add(const string &name, uint8_t val) {
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
void cMsgMessage::add(const string &name, uint16_t val) {
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
void cMsgMessage::add(const string &name, uint32_t val) {
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
void cMsgMessage::add(const string &name, uint64_t val) {
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
void cMsgMessage::add(const string &name, const int8_t *vals, int len) {
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
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<int8_t> &vals) {
  
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
 * This method adds a named vector of 8-bit, signed ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals pointer to vector of 8-bit, signed ints to add (copy)
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<int8_t> *vals) {
  add(name,*vals);
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
void cMsgMessage::add(const string &name, const int16_t *vals, int len) {
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
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<int16_t> &vals) {
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
 * This method adds a named vector of 16-bit, signed ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals pointer to vector of 16-bit, signed ints to add (copy)
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<int16_t> *vals) {
  add(name,*vals);
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
void cMsgMessage::add(const string &name, const int32_t *vals, int len) {
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
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<int32_t> &vals) {
  
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
 * This method adds a named vector of 32-bit, signed ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals pointer to vector of 32-bit, signed ints to add (copy)
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<int32_t> *vals) {
  add(name,*vals);
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
void cMsgMessage::add(const string &name, const int64_t *vals, int len) {
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
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<int64_t> &vals) {
  
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

/**
 * This method adds a named vector of 64-bit, signed ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals pointer to vector of 64-bit, signed ints to add (copy)
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<int64_t> *vals) {
  add(name,*vals);
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
void cMsgMessage::add(const string &name, const uint8_t *vals, int len) {
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
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<uint8_t> &vals) {
  
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
 * This method adds a named vector of 8-bit, unsigned ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals pointer to vector of 8-bit, unsigned ints to add (copy)
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<uint8_t> *vals) {
  add(name,*vals);
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
void cMsgMessage::add(const string &name, const uint16_t *vals, int len) {
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
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<uint16_t> &vals) {
  
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
 * This method adds a named vector of 16-bit, unsigned ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals pointer to vector of 16-bit, unsigned ints to add (copy)
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<uint16_t> *vals) {
  add(name,*vals);
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
void cMsgMessage::add(const string &name, const uint32_t *vals, int len) {
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
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<uint32_t> &vals) {
  
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
 * This method adds a named vector of 32-bit, unsigned ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals pointer to vector of 32-bit, unsigned ints to add (copy)
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<uint32_t> *vals) {
  add(name,*vals);
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
void cMsgMessage::add(const string &name, const uint64_t *vals, int len) {
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
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<uint64_t> &vals) {
  
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

/**
 * This method adds a named vector of 64-bit, unsigned ints to the compound payload of a message.
 * Names may not begin with "cmsg" (case insensitive), be longer than CMSG_PAYLOAD_NAME_LEN,
 * or contain white space or quotes.
 *
 * @param name name of field to add
 * @param vals pointer to vector of 64-bit, unsigned ints to add (copy)
 *
 * @throws cMsgException if no memory, name already used, or improper name
 */   
void cMsgMessage::add(const string &name, const vector<uint64_t> *vals) {
  add(name,*vals);
}

//-------------------------------------------------------------------


} // namespace cmsg

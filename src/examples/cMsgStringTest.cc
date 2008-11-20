//  cMsgStringTest.cc
//
//  tests string representation
//
//  E.Wolin, 18-nov-2008



// system includes
using namespace std;
#include <iostream>
#include <unistd.h>


// for cMsg
#include <cMsg.hxx>
#ifndef VXWORKS
#include <strings.h> // for strncasecmp
#endif

using namespace cmsg;


// message parameters
static string subject;
static string type;
static string text;
static int userInt;


// prototypes
void decodeCommandLine(int argc, char **argv);



//-----------------------------------------------------------------------------


int main(int argc, char **argv) {


  char bytes[16];


  subject       = "mySubject";
  type          = "myType";
  userInt       = 1;
  text          = "hello world";
  for(int i=0; i<16; i++) bytes[i]=i;


  // decode command line parameters
  decodeCommandLine(argc,argv);


  try {

    cMsgMessage m;
    m.setSubject(subject);
    m.setType(type);
    m.setUserInt(userInt);
    m.setText(text);
    m.setByteArray(bytes);
    m.setByteArrayLength(sizeof(bytes));
    

    // add payload
    m.add("anInt",123);
    m.add("aFloat",float(234.0));
    m.add("aDouble",double(345.0));
    m.add("aString","this is a payload string");
    m.add("binaryBytes",(int8_t*)bytes,16);
    m.add("binaryShorts",(int16_t*)bytes,8);
    m.add("binaryInts",(int32_t*)bytes,4);
    m.add("binaryLongs",(int64_t*)bytes,2);


    // create copy of message and add to payload
    cMsgMessage m1(m);
    m.add("copy_of_message",m1);

//     m.add("anotherInt",321);
//     m.add("theMessage",m);


    // print message in xml
    cout << "message toString: " << endl << m.toString() << endl;


    // print payload in internal format
    cout << endl << "payload in internal format: " << endl << m.payloadGetText() << endl;


  } catch (cMsgException e) {
    cerr << endl << e.toString() << endl << endl;
    exit(EXIT_FAILURE);
  } catch (...) {
    cerr << "?Unknown exception" << endl;
    exit(EXIT_FAILURE);
  }
  

  exit(EXIT_SUCCESS);
}


//-----------------------------------------------------------------------------


void decodeCommandLine(int argc, char **argv) {
  

  const char *help = 
    "\nusage:\n\n   cMsgStringTest [-s subject] [-type type] [-i userInt] [-text text]\n\n";
  
  

  // loop over arguments
  int i=1;
  while (i<argc) {
    if (strncasecmp(argv[i],"-h",2)==0) {
      cout << help << endl;
      exit(EXIT_SUCCESS);

    } else if (strncasecmp(argv[i],"-type",5)==0) {
      type=argv[i+1];
      i=i+2;

    } else if (strncasecmp(argv[i],"-text",5)==0) {
      text=argv[i+1];
      i=i+2;

    } else if (strncasecmp(argv[i],"-s",2)==0) {
      subject=argv[i+1];
      i=i+2;

    } else if (strncasecmp(argv[i],"-i",2)==0) {
      userInt=atoi(argv[i+1]);
      i=i+2;
    }
  }

}


//-----------------------------------------------------------------------------

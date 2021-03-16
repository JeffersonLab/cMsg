//  cMsgStringTest.cc
//
//  Tests various things
//
//  E.Wolin, 18-nov-2008



// system includes
#include <iostream>
#include <unistd.h>

using namespace std;

// for cMsg
#include "cMsg.hxx"
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
//     m.setByteArray(bytes, sizeof(bytes));


    // add payload
    m.add("anInt",123);
    m.add("aFloat",float(234.0));
    m.add("aDouble",double(345.0));
    m.add("aString","this is a payload string");

    uint16_t inta[] = {1,2,3,4,5,6,7,8,9,10};
    m.add("array",inta,sizeof(inta)/sizeof(uint16_t));

    uint16_t *ia = m.getUint16Array(string("array"));
    cout << "print out unsigned short array:\n";
    for(int i=0; i<10; i++) cout << ia[i] << "   ";
    cout << endl;


    double d1 = m.getDouble("dbl");
    cout << d1 << endl;


    double d[] = {1.1,2.2,3.3,4.4,5.5,6.6,7.7,8.8,9.9,10.10};
    m.add("dbl",d,sizeof(d)/sizeof(double));

    double *dd = m.getDoubleArray(string("dbl"));
    for(int i=0; i<10; i++) cout << dd[i] << "   ";
    cout << endl;
    
    vector<double> *vd = m.getDoubleVector("dbl");
    for(int i=0; i<10; i++) cout << (*vd)[i] << "   ";
    cout << endl;

    string s[] = {"hello","there"};
    m.add("sarray",s,2);

    string *ss = m.getStringArray("sarray");
    for(int i=0; i<2; i++) cout << ss[i] << "   ";
    cout << endl;

    vector<string> vs;
    vs.push_back("goodbye");
    vs.push_back("now");
    m.add("vs",vs);

    vector<string> *vss = m.getStringVector("vs");
    for(int i=0; i<2; i++) cout << (*vss)[i] << "   ";
    cout << endl;


//     m.add("binaryBytes",(int8_t*)bytes,16);
//     m.add("binaryShorts",(int16_t*)bytes,8);
//     m.add("binaryInts",(int32_t*)bytes,4);
//     m.add("binaryLongs",(int64_t*)bytes,2);


//     // create copy of message and add to payload
//     cMsgMessage m1(m);
//     m.add("copy_of_message",m1);


//     m.add("anotherInt",321);
//     m.add("theMessage",m);


    // print message in xml
    cout << endl << "Message toString: " << endl << m.toString() << endl;


//     // print payload in internal format
//     cout << endl << "payload in internal format: " << endl << m.payloadGetText() << endl;


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

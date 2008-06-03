//  cMsgPayloadTest.cc
//
//  E.Wolin, 3-jun-2008



// system includes
using namespace std;
#include <iostream>
#include <unistd.h>


// for cMsg
#include <cMsg.hxx>

using namespace cmsg;


// connection parameters
static string udl;
static string name;
static string description;


// subscription parameters
static string subject;
static string type;



// prototypes
void decodeCommandLine(int argc, char **argv);



//-----------------------------------------------------------------------------


// callback class
class myCallbackObject : public cMsgCallback {

  void callback(cMsgMessage *msg, void* userObject) {
    cout << msg->toString() << endl;
    
    cout << endl << endl << "raw payload has " << msg->payloadGetCount() << " items: " << endl << endl 
         << msg->payloadGetText() << endl;
    
    delete(msg);
    exit(EXIT_SUCCESS);
  }
};


//-----------------------------------------------------------------------------


int main(int argc, char **argv) {


  // set defaults
  udl           = "cMsg://broadcast/cMsg/test";
  name          = "cMsgTest";
  description   = "cMsg tester";
  subject       = "*";
  type          = "*";


  // decode command line parameters
  decodeCommandLine(argc,argv);


  // connect to cMsg server
  cMsg c(udl,name,description);
  c.connect();
  

  //  subscribe and start dispatching to callback
  try {
    myCallbackObject *cbo = new myCallbackObject();
    c.subscribe(subject,type,cbo,NULL);
    c.start();
  } catch (cMsgException e) {
    cerr << e.toString();
    exit(EXIT_FAILURE);
  }


  // send a message
    cMsgMessage m;
    m.setSubject("mySubject");
    m.setType("myType");
    m.setUserInt(1);
    m.setText("hello world");

    m.add("payload_float", 1.2345);

    m.add("payload_string", "this is a payload string");
    m.add("payload_int", 12345);

    unsigned int a[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    vector<unsigned int> v(a,a+15);
    m.add("payload_vector",v);

    c.send(m);
    c.flush();


  // wait forever for messages
  while(true) {
    sleep(1);
  }
  
  return(EXIT_SUCCESS);

}


//-----------------------------------------------------------------------------


void decodeCommandLine(int argc, char **argv) {
  

  const char *help = 
    "\nusage:\n\n   cMsgTest [-udl udl] [-n name] [-d description] [-s subject] [-t type]\n\n";
  
  

  // loop over arguments
  int i=1;
  while (i<argc) {
    if (strncasecmp(argv[i],"-h",2)==0) {
      cout << help << endl;
      exit(EXIT_SUCCESS);

    } else if (strncasecmp(argv[i],"-t",2)==0) {
      type=argv[i+1];
      i=i+2;

    } else if (strncasecmp(argv[i],"-udl",4)==0) {
      udl=argv[i+1];
      i=i+2;

    } else if (strncasecmp(argv[i],"-n",2)==0) {
      name=argv[i+1];
      i=i+2;

    } else if (strncasecmp(argv[i],"-d",2)==0) {
      description=argv[i+1];
      i=i+2;

    } else if (strncasecmp(argv[i],"-s",2)==0) {
      subject=argv[i+1];
      i=i+2;
    }
  }

}


//-----------------------------------------------------------------------------

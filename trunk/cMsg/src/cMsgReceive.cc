//  cMsgReceive.cc
//
//  receives cMsg message based on command line params
//
//  E.Wolin, 28-apr-2005



// system includes
using namespace std;
#include <iostream>
#include <unistd.h>


// for cMsg
#include <cMsg.hxx>


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
class myCallbackObject:public cMsgCallbackAdapter {

  void callback(cMsgMessage msg, void* userObject) {
    cout << "subject is:   " << msg.getSubject() << endl;
    cout << "type is:      " << msg.getType() << endl;
    cout << "userInt is:   " << msg.getUserInt() << endl;
    cout << "text is:      " << msg.getText() << endl;
    cout << endl;
  }

};


//-----------------------------------------------------------------------------


main(int argc, char **argv) {


  // set defaults
  udl           = "cMsg://ollie:3456/cMsg/vmeTest";       // universal domain locator
  name          = "cMsgReceive";                          // unique name
  description   = "cMsgReceiveutility ";                  // description is arbitrary
  subject       = "mySubject";
  type          = "myType";


  // decode command line parameters
  decodeCommandLine(argc,argv);


  // connect to cMsg server
  cMsg c(udl,name,description);
  c.connect();
  

  //  subscribe and start dispatching to callback
  c.subscribe(subject,type,new myCallbackObject(),NULL);
  c.start();
    
    
  // wait forever for messages
  while(true) {
    sleep(1);
  }

}


//-----------------------------------------------------------------------------


void decodeCommandLine(int argc, char **argv) {
  

  const char *help = 
    "\nusage:\n\n   cMsgCommand [-u udl] [-n name] [-d description] [-s subject] [-type type]\n\n";
  
  

  // loop over arguments
  int i=1;
  while (i<argc) {
    if (strncasecmp(argv[i],"-h",2)==0) {
      cout << help << endl;
      exit(EXIT_SUCCESS);

    } else if (strncasecmp(argv[i],"-type",5)==0) {
      type=argv[i+1];
      i=i+2;

    } else if (strncasecmp(argv[i],"-u",2)==0) {
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

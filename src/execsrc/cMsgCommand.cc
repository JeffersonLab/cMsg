//  cMsgCommand.cc
//
//  sends cMsg message based on command line params
//
//  E.Wolin, 27-apr-2005



// system includes
using namespace std;
#include <iostream>
#include <unistd.h>
#include <sstream>


// for cMsg
#include <cMsg.hxx>
#ifndef VXWORKS
#include <strings.h> // for strncasecmp
#endif

using namespace cmsg;


// connection parameters
static string udl;
static string name = "";
static string description;


// message parameters
static string subject;
static string type;
static string text;
static int userInt;


// misc parameters
static int sleepTime  = 100000;  // units are micro-sec
static int sendAndGet = false;
static int timeout    = 3;       // units are seconds


// prototypes
void decodeCommandLine(int argc, char **argv);



//-----------------------------------------------------------------------------


int main(int argc, char **argv) {


  // set defaults
  udl           = "cMsg://localhost/cMsg";
  name          = "";
  description   = "cMsgCommand utility";
  subject       = "mySubject";
  type          = "myType";
  userInt       = 0;
  text          = "hello world";


  // decode command line parameters
  decodeCommandLine(argc,argv);


  // generate name if not set
  if(name.size()<=0) {
    char namec[128];
    int pid = getpid();
    gethostname(namec,sizeof(namec));
    stringstream ss;
    ss << "cMsgCommand@" << string(namec) << ":" << pid << ends;
    name=ss.str();
  }


  // connect to cMsg server
  cMsg c(udl,name,description);

  try {
    c.connect();
    
    
  // create and fill message
    cMsgMessage m;
    m.setSubject(subject);
    m.setType(type);
    m.setUserInt(userInt);
    m.setText(text);
    
    
    // send message
    if(sendAndGet) {
      try {
        timespec ts = {timeout,0};
        cMsgMessage *response = c.sendAndGet(m,&ts);
        if(response!=NULL) {
          cout << endl << "Response: " <<  endl << response->toString() << endl;
        } else {
          cerr << endl << "?No sendAndGet response within " << timeout << " seconds" << endl << endl;
        }
      } catch (cMsgException e) {
        cerr << endl << "?No sendAndGet response within " << timeout << " seconds" << endl << endl;
      }

    } else {
      try {
        c.send(m);
        c.flush();
        usleep(sleepTime);
      } catch (cMsgException e) {
        cerr << endl << "?unable to send message" << endl << endl;
      }
    }

  } catch (cMsgException e) {
    cerr << endl << e.toString() << endl << endl;
    exit(EXIT_FAILURE);
  }
  
  
  // done
  c.disconnect();
  exit(EXIT_SUCCESS);
}


//-----------------------------------------------------------------------------


void decodeCommandLine(int argc, char **argv) {
  

  const char *help = 
    "\nusage:\n\n   cMsgCommand [-u udl] [-n name] [-d description] [-sleep sleepTime]\n"
    "              [-s subject] [-type type] [-i userInt] [-text text] [-sendAndGet] [-timeout timeout]\n\n";
  
  

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

    } else if (strncasecmp(argv[i],"-sleep",6)==0) {
      sleepTime=atoi(argv[i+1]);
      i=i+2;

    } else if (strncasecmp(argv[i],"-sendAndGet",11)==0) {
      sendAndGet=true;
      i=i+1;

    } else if (strncasecmp(argv[i],"-timeout",8)==0) {
      timeout=atoi(argv[i+1]);
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

    } else if (strncasecmp(argv[i],"-i",2)==0) {
      userInt=atoi(argv[i+1]);
      i=i+2;
    }
  }

}


//-----------------------------------------------------------------------------

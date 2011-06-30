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
#include <vector>

#include <boost/numeric/conversion/cast.hpp> 
#include <boost/lexical_cast.hpp>


// for cMsg
#include <cMsg.hxx>
#ifndef VXWORKS
#include <strings.h> // for strncasecmp
#endif

using namespace cmsg;
using namespace boost;


// connection parameters
static string udl;
static string name = "";
static string description;


// message parameters
static string subject;
static string theType;
static string text;
static int userInt;


// misc parameters
static int sleepTime  = 100;     // units are msec
static int sendAndGet = false;
static int timeout    = 3;       // units are seconds


// for storing payload items
static vector< pair<string,string> > payloadVector;


// prototypes
void decodeCommandLine(int argc, char **argv);



//-----------------------------------------------------------------------------


int main(int argc, char **argv) {


  // set defaults
  udl           = "cMsg://localhost/cMsg";
  name          = "";
  description   = "cMsgCommand utility";
  subject       = "mySubject";
  theType          = "myType";
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
    ss << "cMsgCommand@" << string(namec) << "-" << pid << ends;
    name=ss.str();
  }


  // connect to cMsg server
  cMsg c(udl,name,description);

  try {
    c.connect();
    
    
  // create and fill message
    cMsgMessage m;
    m.setSubject(subject);
    m.setType(theType);
    m.setUserInt(userInt);
    m.setText(text);
    
    
    // add payload items to message
    vector< pair<string,string> >::iterator iter;
    for(iter=payloadVector.begin();iter!=payloadVector.end();iter++) {
	string type = (*iter).first;
	string txt  = (*iter).second;

	if(type=="int8") {
	   string name = txt.substr(0,txt.find("="));
	   string sval = txt.substr(txt.find("=")+1);
	   int8_t val = numeric_cast<int8_t>(lexical_cast<int>(sval));
  	   m.add(name,val);
	   
	} else if (type=="int16") {
	   string name = txt.substr(0,txt.find("="));
	   string sval = txt.substr(txt.find("=")+1);
	   int16_t val = numeric_cast<int16_t>(lexical_cast<int>(sval));
  	   m.add(name,val);
	   
	} else if (type=="int32") {
	   string name = txt.substr(0,txt.find("="));
	   string sval = txt.substr(txt.find("=")+1);
	   int32_t val = numeric_cast<int32_t>(lexical_cast<int>(sval));
  	   m.add(name,val);
	   
	} else if (type=="int64") {
	   string name = txt.substr(0,txt.find("="));
	   string sval = txt.substr(txt.find("=")+1);
	   int64_t val = numeric_cast<int64_t>(lexical_cast<long long>(sval));
  	   m.add(name,val);
	   
	} else if(type=="uint8") {
	   string name = txt.substr(0,txt.find("="));
	   string sval = txt.substr(txt.find("=")+1);
	   uint8_t val = numeric_cast<uint8_t>(lexical_cast<unsigned int>(sval));
  	   m.add(name,val);
	   
	} else if (type=="uint16") {
	   string name = txt.substr(0,txt.find("="));
	   string sval = txt.substr(txt.find("=")+1);
	   uint16_t val = numeric_cast<uint16_t>(lexical_cast<unsigned int>(sval));
  	   m.add(name,val);
	   
	} else if (type=="uint32") {
	   string name = txt.substr(0,txt.find("="));
	   string sval = txt.substr(txt.find("=")+1);
	   uint32_t val = numeric_cast<uint32_t>(lexical_cast<unsigned int>(sval));
  	   m.add(name,val);
	   
	} else if (type=="uint64") {
	   string name = txt.substr(0,txt.find("="));
	   string sval = txt.substr(txt.find("=")+1);
	   uint64_t val = numeric_cast<uint64_t>(lexical_cast<unsigned long long>(sval));
  	   m.add(name,val);
	   
	} else if (type=="double") {
	   string name = txt.substr(0,txt.find("="));
	   string sval = txt.substr(txt.find("=")+1);
	   double val = lexical_cast<double>(sval);
  	   m.add(name,val);
	
	
	} else if (type=="float") {
	   string name = txt.substr(0,txt.find("="));
	   string sval = txt.substr(txt.find("=")+1);
	   float val = lexical_cast<float>(sval);
  	   m.add(name,val);	
	
	} else if (type=="string") {
	   string name = txt.substr(0,txt.find("="));
	   string sval = txt.substr(txt.find("=")+1);
  	   m.add(name,sval);
	
	}

    }
    
    
    
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
        usleep(sleepTime*1000);
      } catch (cMsgException e) {
        cerr << endl << "?unable to send message" << endl << endl;
      }
    }

  } catch (cMsgException e) {

    cerr << endl << "?unable to connect, udl = " << udl << ",  name = " << name << endl << endl 
         << e.toString() << endl << endl;
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
    "               [-s subject] [-type type] [-i userInt] [-text text] [-sendAndGet] [-timeout timeout]\n"
    "               [-int8 payloaditem] [-int16 payloaditem] [-int32 payloaditem] [-int64 payloaditem]\n"
    "               [-uint8 payloaditem] [-uint16 payloaditem] [-uint32 payloaditem] [-uint64 payloaditem]\n"
    "               [-double payloaditem] [-float payloaditem] [-string payloaditem]\n\n";
  
  

  // loop over arguments
  int i=1;
  while (i<argc) {
    if (strncasecmp(argv[i],"-h",2)==0) {
      cout << help << endl;
      exit(EXIT_SUCCESS);

    } else if (strncasecmp(argv[i],"-type",5)==0) {
      theType=argv[i+1];
      i=i+2;

    } else if (strncasecmp(argv[i],"-text",5)==0) {
      text=argv[i+1];
      i=i+2;

    } else if (strncasecmp(argv[i],"-int8",5)==0) {
      payloadVector.push_back(pair<string,string>("int8",string(argv[i+1])));
      i=i+2;
      
    } else if (strncasecmp(argv[i],"-int16",6)==0) {
      payloadVector.push_back(pair<string,string>("int16",string(argv[i+1])));
      i=i+2;
      
    } else if (strncasecmp(argv[i],"-int32",6)==0) {
      payloadVector.push_back(pair<string,string>("int32",string(argv[i+1])));
      i=i+2;
      
    } else if (strncasecmp(argv[i],"-int64",6)==0) {
      payloadVector.push_back(pair<string,string>("int64",string(argv[i+1])));
      i=i+2;
      
    } else if (strncasecmp(argv[i],"-uint8",6)==0) {
      payloadVector.push_back(pair<string,string>("uint8",string(argv[i+1])));
      i=i+2;
      
    } else if (strncasecmp(argv[i],"-uint16",7)==0) {
      payloadVector.push_back(pair<string,string>("uint16",string(argv[i+1])));
      i=i+2;
      
    } else if (strncasecmp(argv[i],"-uint32",7)==0) {
      payloadVector.push_back(pair<string,string>("uint32",string(argv[i+1])));
      i=i+2;
      
    } else if (strncasecmp(argv[i],"-uint64",7)==0) {
      payloadVector.push_back(pair<string,string>("uint64",string(argv[i+1])));
      i=i+2;
      
    } else if (strncasecmp(argv[i],"-double",7)==0) {
      payloadVector.push_back(pair<string,string>("double",string(argv[i+1])));
      i=i+2;
      
    } else if (strncasecmp(argv[i],"-float",6)==0) {
      payloadVector.push_back(pair<string,string>("float",string(argv[i+1])));
      i=i+2;
      
    } else if (strncasecmp(argv[i],"-string",7)==0) {
      payloadVector.push_back(pair<string,string>("string",string(argv[i+1])));
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

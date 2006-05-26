#
# cMsg top level Makefile
#
#

MAKEFILE = Makefile.unix
INSTALL  = $(CMSG_DIR)/install-sh -c

# operating system, platform, processor bits we are using
OSNAME   := $(shell uname)
PLATFORM := $(shell uname -p)
CMSG_DIR := $(shell pwd)

# Look to see if the environmental variable CMSG_USE64BITS is defined.
# If so, then compile everything for 64 bits. Be sure to do a 
# "make cClean" when switching between 32 & 64 bit compiling.
ifeq ($(findstring CMSG_USE64BITS, $(shell env | grep CMSG_USE64BITS)), CMSG_USE64BITS) 
CMSG_BIT_ARCH = 64
BITS = 64
else
CMSG_BIT_ARCH =
BITS = 32
endif

# if ARCH is defined, do a vxWorks build
ifeq ($(ARCH),VXWORKSPPC)
MAKEFILE = Makefile.vxworks
OSNAME   = vxworks
PLATFORM =
CMSG_BIT_ARCH = 
endif

INC_DIR = $(CMSG_DIR)/inc
LIB_DIR = $(CMSG_DIR)/lib/$(OSNAME)/$(PLATFORM)/$(CMSG_BIT_ARCH)
BIN_DIR = $(CMSG_DIR)/bin/$(OSNAME)/$(PLATFORM)/$(CMSG_BIT_ARCH)

# send this down to lower level makefiles
export INSTALL
export OSNAME
export PLATFORM
export INC_DIR
export LIB_DIR
export BIN_DIR
export CMSG_DIR
export CMSG_BIT_ARCH

echoarch:
	@echo
	@echo "Make for $(OSNAME) on $(PLATFORM), $(BITS) bits"
	@echo

all: echoarch mkdirs cCode java jarfile

mkdirs:
	-mkdir ./lib
	-mkdir ./bin
	-mkdir ./inc
	-mkdir $(CMSG_DIR)/lib/$(OSNAME)
	-mkdir $(CMSG_DIR)/lib/$(OSNAME)/$(PLATFORM)
	-mkdir $(LIB_DIR)
	-mkdir $(CMSG_DIR)/bin/$(OSNAME)
	-mkdir $(CMSG_DIR)/bin/$(OSNAME)/$(PLATFORM)
	-mkdir $(BIN_DIR)
	-mkdir ./doc/doxygen/C
	-mkdir ./doc/doxygen/CC

cCode: echoarch
	cd ./src/regexp;  $(MAKE) -f $(MAKEFILE) install;
	cd ./src/C;  $(MAKE) -f $(MAKEFILE) install;
	cd ./src/CC; $(MAKE) -f $(MAKEFILE) install;


tarfile:
###	make clean
	$(RM) cMsg-1.0.tar.gz; tar cvX tarexclude -f cMsg-1.0.tar.gz -z .


jarfile:
	jar cf jar/cMsg-1.0.jar `find org/ -type f | grep -v SCCS | grep -v CVS | grep class`


java:
	cd ./org/jlab/coda/cMsg; $(MAKE);
	cd ./org/jlab/coda/cMsg/cMsgDomain/client;  $(MAKE);
	cd ./org/jlab/coda/cMsg/cMsgDomain/server;  $(MAKE);
	cd ./org/jlab/coda/cMsg/subdomains;  $(MAKE);
	cd ./org/jlab/coda/cMsg/apps;  $(MAKE);
	cd ./org/jlab/coda/cMsg/FileDomain;  $(MAKE);
	cd ./org/jlab/coda/cMsg/CADomain;  $(MAKE);
	cd ./org/jlab/coda/cMsg/RCServerDomain;  $(MAKE);
	cd ./org/jlab/coda/cMsg/RCBroadcastDomain;  $(MAKE);
	cd ./;


doc:  javadoc doxygen

javadoc:
	javadoc -package -d doc/javadoc org.jlab.coda.cMsg

doxygen:
	doxygen ./doc/doxygen/DoxyfileC
	doxygen ./doc/doxygen/DoxyfileCC



clean: cClean jClean tarClean jarClean

distClean: clean bClean

bClean:
	-$(RM) -f $(LIB_DIR)/* $(BIN_DIR)/*

cClean: 
	-$(RM) -f core *~ *.o *.so *.a *.class
	cd ./src/regexp;  $(MAKE) -f $(MAKEFILE) clean;
	cd ./src/C;  $(MAKE) -f $(MAKEFILE) clean;
	cd ./src/CC; $(MAKE) -f $(MAKEFILE) clean;

jClean: 
	cd ./org/jlab/coda/cMsg;             $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/cMsgDomain/client;  $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/cMsgDomain/server;  $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/subdomains;  $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/apps;        $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/FileDomain;  $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/CADomain;    $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/RCServerDomain;     $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/RCBroadcastDomain;  $(MAKE) clean;

tarClean:
	-$(RM) *.tar.gz

jarClean:
	-$(RM) jar/cMsg*.jar



# SUFFIX RULES

.SUFFIXES: .java .class

.java.class:
	java $< $(JAVAC_FLAGS)

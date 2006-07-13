#
# cMsg top level Makefile
#
#     Set the env var CMSG_INSTALL to the directory in which
#     you want the libs, executables,and docs placed. If not
#     set this defaults to this (top cMsg) directory.
#
#     Set the env var CMSG_USE64BITS if you want to compile
#     64 bit libs and executables and are running on a 64
#     bit machine.
#

MAKEFILE = Makefile.unix

# operating system, platform, # of processor bits we are using
OSNAME   := $(shell uname)
PLATFORM := $(shell uname -m)
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

# Define the installation directory.
# By default use the cMsg directory,
# else we the directory specified in the
# CMSG_INSTALL environmental variable.
ifneq ($(findstring CMSG_INSTALL, $(shell env | grep CMSG_INSTALL)), CMSG_INSTALL) 
CMSG_INSTALL = $(CMSG_DIR)
endif

MESSAGE = "Make for $(OSNAME) on $(PLATFORM), $(BITS) bits"

# if ARCH is defined, do a vxWorks build
ifeq ($(ARCH),VXWORKSPPC)
MAKEFILE = Makefile.vxworks
OSNAME   = vxworks
MESSAGE  = "Make for $(OSNAME) on $(shell uname), 32 bits"
PLATFORM =
CMSG_BIT_ARCH = 
endif

INC_DIR = $(CMSG_INSTALL)/inc
LIB_DIR = $(CMSG_INSTALL)/arch/$(OSNAME)/$(PLATFORM)/$(CMSG_BIT_ARCH)/lib
BIN_DIR = $(CMSG_INSTALL)/arch/$(OSNAME)/$(PLATFORM)/$(CMSG_BIT_ARCH)/bin

# send these definitions down to lower level makefiles
export OSNAME
export PLATFORM
export INC_DIR
export LIB_DIR
export BIN_DIR
export CMSG_DIR
export CMSG_BIT_ARCH

# needed directories
DIRS =  $(INC_DIR) \
	$(CMSG_INSTALL)/doc \
	$(CMSG_INSTALL)/doc/doxygen \
	$(CMSG_INSTALL)/doc/doxygen/C \
	$(CMSG_INSTALL)/doc/doxygen/CC \
	$(CMSG_INSTALL)/arch \
	$(CMSG_INSTALL)/arch/$(OSNAME) \
	$(CMSG_INSTALL)/arch/$(OSNAME)/$(PLATFORM) \
	$(CMSG_INSTALL)/arch/$(OSNAME)/$(PLATFORM)/$(CMSG_BIT_ARCH) \
	$(LIB_DIR) \
	$(BIN_DIR) \
	./src/C/.$(OSNAME) \
	./src/C/.$(OSNAME)/$(PLATFORM) \
	./src/C/.$(OSNAME)/$(PLATFORM)/$(CMSG_BIT_ARCH) \
	./src/CC/.$(OSNAME) \
	./src/CC/.$(OSNAME)/$(PLATFORM) \
	./src/CC/.$(OSNAME)/$(PLATFORM)/$(CMSG_BIT_ARCH) \
	./src/regexp/.$(OSNAME) \
	./src/regexp/.$(OSNAME)/$(PLATFORM) \
	./src/regexp/.$(OSNAME)/$(PLATFORM)/$(CMSG_BIT_ARCH)

all: echoarch mkdirs cCode java jarfile

echoarch:
	@echo
	@echo $(MESSAGE)
	@echo

mkdirs: mkinstalldirs
	@echo "Creating directories"
	./mkinstalldirs $(DIRS)
	@echo

cCode: echoarch mkdirs
	cd ./src/regexp;  $(MAKE) -f $(MAKEFILE) install;
	cd ./src/C;       $(MAKE) -f $(MAKEFILE) install;
	cd ./src/CC;      $(MAKE) -f $(MAKEFILE) install;

tarfile:
###	make clean
	$(RM) cMsg-1.0.tar.gz;
	cd ..; tar cvX ./cMsg/tarexclude -f cMsg-1.0.tar.gz -z ./cMsg
	-mv ../cMsg-1.0.tar.gz .

jarfile:
	jar cf jar/cMsg-1.0.jar `find org/ -type f | grep -v SCCS | grep -v CVS | grep class`

java:
	cd ./org/jlab/coda/cMsg;                    $(MAKE);
	cd ./org/jlab/coda/cMsg/cMsgDomain/client;  $(MAKE);
	cd ./org/jlab/coda/cMsg/cMsgDomain/server;  $(MAKE);
	cd ./org/jlab/coda/cMsg/subdomains;         $(MAKE);
	cd ./org/jlab/coda/cMsg/apps;               $(MAKE);
	cd ./org/jlab/coda/cMsg/FileDomain;         $(MAKE);
	cd ./org/jlab/coda/cMsg/CADomain;           $(MAKE);
	cd ./org/jlab/coda/cMsg/RCServerDomain;     $(MAKE);
	cd ./org/jlab/coda/cMsg/RCBroadcastDomain;  $(MAKE);

doc:  javadoc doxygen

javadoc:
	javadoc -package -d $(CMSG_INSTALL)/doc/javadoc org.jlab.coda.cMsg

doxygen:
	@echo installing documentation in $(CMSG_INSTALL)
	@echo
	export CMSG_INSTALL=$(CMSG_INSTALL) ; doxygen ./doc/doxygen/DoxyfileC
	export CMSG_INSTALL=$(CMSG_INSTALL) ; doxygen ./doc/doxygen/DoxyfileCC

clean: cClean jClean tarClean jarClean

distClean: clean bClean

bClean:
	-$(RM) -f $(LIB_DIR)/* $(BIN_DIR)/*

cClean: 
	-$(RM) -f core *~ *.o *.so *.a *.class
	cd ./src/regexp;  $(MAKE) -f $(MAKEFILE) clean;
	cd ./src/C;       $(MAKE) -f $(MAKEFILE) clean;
	cd ./src/CC;      $(MAKE) -f $(MAKEFILE) clean;

jClean: 
	cd ./org/jlab/coda/cMsg;                    $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/cMsgDomain/client;  $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/cMsgDomain/server;  $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/subdomains;         $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/apps;               $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/FileDomain;         $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/CADomain;           $(MAKE) clean;
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

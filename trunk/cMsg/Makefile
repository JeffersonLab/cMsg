#
# cMsg top level Makefile
#
#
# Warning!!
# cMsg will not compile properly for vxWorks under Linux cross compiler
#

# Operating system we are using
OSNAME := $(shell uname)
MAKEFILE = Makefile.unix

ifeq ($(OSNAME),SunOS)
LIB_DIR = ./lib/SunOS
BIN_DIR = ./bin/SunOS

ifeq ($(ARCH),VXWORKSPPC)
LIB_DIR = ./lib/vxworks
MAKEFILE = Makefile.vxworks
endif

endif


ifeq ($(OSNAME),Linux)
LIB_DIR = ./lib/Linux
BIN_DIR = ./bin/Linux
endif

# send this down to lower level makefiles
export OSNAME

all: mkdirs cCode java doc

mkdirs:
	-mkdir ./lib
	-mkdir ./bin
	-mkdir ./inc
	-mkdir $(LIB_DIR)
	-mkdir $(BIN_DIR)

cCode:
	cd ./src/regexp;  $(MAKE) -f $(MAKEFILE) install;
	cd ./src;  $(MAKE) -f $(MAKEFILE) install;

jar:
	jar cf cMsg.jar `find org/ -type f | grep -v SCCS`

java:
	cd ./org/jlab/coda/cMsg; $(MAKE);
	cd ./org/jlab/coda/cMsg/cMsgDomain;  $(MAKE);
	cd ./org/jlab/coda/cMsg/subdomains;  $(MAKE);
	cd ./org/jlab/coda/cMsg/apps;  $(MAKE);
	cd ./org/jlab/coda/cMsg/FileDomain;  $(MAKE);
	cd ./org/jlab/coda/cMsg/CADomain;  $(MAKE);
	cd ./;

doc:  javadoc doxygen

javadoc:
	javadoc -package -d doc/javadoc org.jlab.coda.cMsg

doxygen:
	doxygen ./doc/doxygen/Doxyfile

clean: 
	rm -f core *~ *.o *.so *.a *.class
	cd ./src/regexp;  $(MAKE) -f $(MAKEFILE) clean;
	cd ./src;  $(MAKE) -f $(MAKEFILE) clean;
jclean: 
	cd ./org/jlab/coda/cMsg;  $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/cMsgDomain;  $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/subdomains;  $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/apps;  $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/FileDomain;  $(MAKE) clean;
	cd ./org/jlab/coda/cMsg/CADomain;  $(MAKE) clean;

# SUFFIX RULES

.SUFFIXES: .java .class

.java.class:
	java $< $(JAVAC_FLAGS)

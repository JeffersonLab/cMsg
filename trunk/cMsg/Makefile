#
# cMsg top level Makefile
#
#
# Warning!!
# cMsg will not compile properly for vxWorks under Linux cross compiler
#

# Operating system we are using
OSNAME := $(shell uname)

ifeq ($(OSNAME),SunOS)
LIB_DIR = ./lib/SunOS
BIN_DIR = ./bin/SunOS

ifeq ($(ARCH),VXWORKSPPC)
OSNAME = vxworks
LIB_DIR = ./lib/vxworks
endif

endif


ifeq ($(OSNAME),Linux)
LIB_DIR = ./lib/Linux
BIN_DIR = ./bin/Linux
endif

all: $(OSNAME) java doc

Linux:
	cd ./src/regexp; make -f Makefile.Linux install;
	cd ./src; make -f Makefile.Linux install;

SunOS:
	cd ./src/regexp; make -f Makefile.SunOS install;
	cd ./src; make -f Makefile.SunOS install;

vxworks:
	cd ./src; make -f Makefile.vxworks;
	cd ./src/regexp; make -f Makefile.vxworks;

jar:
	jar cf cMsg.jar `find org/ -type f | grep -v SCCS`

java:
	cd ./org/jlab/coda/cMsg; make;
	cd ./org/jlab/coda/cMsg/cMsgDomain; make;
	cd ./org/jlab/coda/cMsg/subdomains; make;
	cd ./org/jlab/coda/cMsg/apps; make;
	cd ./org/jlab/coda/cMsg/FileDomain; make;
	cd ./org/jlab/coda/cMsg/CADomain; make;
	cd ./;

doc:  javadoc doxygen

javadoc:
	javadoc -package -d doc/javadoc org.jlab.coda.cMsg

doxygen:
	doxygen doc/doxygen/doxygenConfig
#	doxygen doc/doxygen/Doxyfile

clean: 
	rm -f core *~ *.o *.so *.a *.class
	cd ./src; make clean;
jclean: 
	cd ./org/jlab/coda/cMsg; make clean;
	cd ./org/jlab/coda/cMsg/cMsgDomain; make clean;
	cd ./org/jlab/coda/cMsg/subdomains; make clean;
	cd ./org/jlab/coda/cMsg/apps; make clean;
	cd ./org/jlab/coda/cMsg/FileDomain; make clean;
	cd ./org/jlab/coda/cMsg/CADomain; make clean;

# SUFFIX RULES

.SUFFIXES: .java .class

.java.class:
	$(JAVAC) $< $(JAVAC_FLAGS)

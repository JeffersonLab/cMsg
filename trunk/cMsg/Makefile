# Generated automatically by Carl Timmer.
#
#
CC  = $(PURE) gcc
CXX = g++
LD  = ld
AR  = ar
RANLIB = ranlib
INSTALL  = ./install-sh -c

LIB_DIR = ./
INC_DIR = ./
BIN_DIR = ./

# C stuff
AC_FLAGS  = -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS
CFLAGS    = -O2 -fPIC -I./ $(AC_FLAGS)
CXXFLAGS  = $(CFLAGS)
SHLIB_LD  = ld -G

LIBNAM      = libcmsg
LIB_STATIC  = $(LIBNAM).a
LIB_DYNAMIC = $(LIBNAM).so

CMSG_LIBS = $(LIB_STATIC) $(LIB_DYNAMIC)
LIBS    = -lieee -lrt -lpthread -lm -lnsl -lresolv -ldl

OBJS =  rwlock.o \
	cMsgNetwork.o \
	cMsg.o \
	cMsgDomainListenThread.o \
	cMsgDomain.o

HFILES = cMsgNetwork.h cMsgPrivate.h cMsg.h cMsgDomain.h rwlock.h errors.h

PROGS = producer consumer getConsumer

all: $(CMSG_LIBS) $(PROGS)

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
	javadoc -d doc/javadoc org.jlab.coda.cMsg
##	javah -package org.jlab.coda.cMsg
##	javah -package org.jlab.coda.cMsg.cMsgDomain
##	javah -package org.jlab.coda.cMsg.cMsgDomain.subdomains
##	javah -package org.jlab.coda.cMsg.apps


doxygen:
	doxygen doc/doxygen/Doxyfile


install: install-lib install-inc install-bin

install-lib: $(CMSG_LIBS)
	@echo "Installing CMSG libraries in $(LIB_DIR)"
	@for i in $(CMSG_LIBS); do \
	   echo "  installing $$i"; \
	   $(INSTALL) $$i $(LIB_DIR)/$$i; \
           done;

install-inc: $(HFILES)
	@echo "Installing includes in $(INC_DIR)"
	@for i in $(HFILES); do \
	   echo "  installing $$i"; \
	   $(INSTALL) $$i $(INC_DIR)/$$i; \
           done;

install-bin: $(PROGS)
	@echo "Installing executables in $(BIN_DIR)"
	@for i in $(PROGS); do \
	   echo "  installing $$i"; \
	   $(INSTALL) $$i $(BIN_DIR)/$$i; \
           done;

$(PROGS) : % : %.c  ./cMsg.h
	$(CC) -o $@ $(CFLAGS) $< -L./ -lcmsg $(LIBS)

# libraries
$(LIB_STATIC): $(OBJS)
	$(AR) cr $@ $?
	$(RANLIB) $@

$(LIB_DYNAMIC): $(OBJS)
	$(SHLIB_LD) $(OBJS) -o $@

# file dependencies
$(OBJS) : $(HFILES)

clean: 
	rm -f core *~ *.o *.so *.a *.class $(PROGS)
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
.c:
	$(CC) -o $* $(CFLAGS) $< -L./ -lcmsg $(LIBS)
.cc:
	$(CXX) -o $* $(CFLAGS) $< -L./ -lcmsg $(LIBS)
.c.o:
	$(CC) -c $(CFLAGS) $<

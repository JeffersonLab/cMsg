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
#CFLAGS    = -O2 -fPIC -I./ $(AC_FLAGS)
CFLAGS    = -fPIC -I./ $(AC_FLAGS)
CXXFLAGS  = $(CFLAGS)
SHLIB_LD  = ld -G

LIBNAM      = libcmsg
LIB_STATIC  = $(LIBNAM).a
LIB_DYNAMIC = $(LIBNAM).so

CMSG_LIBS = $(LIB_STATIC) $(LIB_DYNAMIC)
LIBS    = -lieee -lm -lpthread -lm -lnsl -lresolv -ldl

OBJS =  cMsgNetwork.o \
	cMsg.o \
	cMsgServer.o \
	cMsgDomain_CODA.o

HFILES = cMsgNetwork.h cMsgPrivate.h cMsg.h cMsg_CODA.h

PROGS = client client2

all: $(CMSG_LIBS) $(PROGS)

java:
	cd ./org/jlab/coda/cMsg; make;
	cd ./org/jlab/coda/cMsg/coda; make;
	cd ./org/jlab/coda/cMsg/apps; make;
	cd ./org/jlab/coda/cMsg/plugins; make;
	cd ./;

doc:
	javah -package org.jlab.coda.cMsg
	javah -package org.jlab.coda.cMsg.cMsg
	javah -package org.jlab.coda.cMsg.apps

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
	cd ./org/jlab/coda/cMsg/coda; make clean;
	cd ./org/jlab/coda/cMsg/apps; make clean;

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

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

AC_FLAGS  = -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS
CFLAGS    = -O2 -fPIC -I./ $(AC_FLAGS)
CXXFLAGS  = $(CFLAGS)
SHLIB_LD  = ld -G

LIBNAM      = libcmsg
LIB_STATIC  = $(LIBNAM).a
LIB_DYNAMIC = $(LIBNAM).so

CMSG_LIBS = $(LIB_STATIC) $(LIB_DYNAMIC)
LIBS    = -lieee -lm -lpthread -lm -lnsl -lresolv -ldl

OBJS =  cMsgNetwork.o \
	cMsgServer.o \
	cMsg.o

HFILES = cMsgNetwork.h cMsgPrivate.h cMsg.h

PROGS =

all: $(CMSG_LIBS) $(PROGS)

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

# libraries
$(LIB_STATIC): $(OBJS)
	$(AR) cr $@ $?
	$(RANLIB) $@

$(LIB_DYNAMIC): $(OBJS)
	$(SHLIB_LD) $(OBJS) -o $@

$(LIB_REMOTE): $(REMOTE_OBJS)
	$(SHLIB_LD) $(REMOTE_OBJS) -o $@

# file dependencies
$(OBJS) : $(HFILES)

clean: 
	rm -f core *~ *.o *.so *.a $(PROGS)

.c:
	$(CC) -o $* $(CFLAGS) $< -L./ $(LIBS)
.cc:
	$(CXX) -o $* $(CFLAGS) $< -L./ $(LIBS)
.c.o:
	$(CC) -c $(CFLAGS) $<

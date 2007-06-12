#
# $Header$
#

ifndef BMS_HOME
  $(error "Need to define BMS_HOME")
endif

# Setting TOPLEVEL tells Makefile.common where to find Makefile.local
# and to include it if make is run from this dir or the top-level package
# directory.
TOPLEVEL = ../..

# use cMsg libraries
PACKAGES = cMsg

# only here for print out
MODULE_NAME = cmsg

CFLAGS   += -I../libsrc -I../libsrc++
CXXFLAGS += -I../libsrc -I../libsrc++

include $(BMS_HOME)/Makefile.common

include $(BMS_HOME)/Makefile.bin

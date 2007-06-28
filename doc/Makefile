#
# cMsg documentation installation
#

ifndef BMS_HOME
  $(error "Need to define BMS_HOME")
endif

# include Makefile.local
TOPLEVEL = ..

#include Makefile.common
include $(BMS_HOME)/Makefile.common

# The top-level et makefile doesn't know about INSTALL_DIR so
# include above 2 makefiles to get INSTALL_DIR.
# At this point, "make doc" has already been done one level above
# and so all we have to do is copy things to its final resting place.

all: doc install

install:
	@mkdir -p $(INSTALL_DIR)/doc/cMsg/doxygen/C/html
	@mkdir -p $(INSTALL_DIR)/doc/cMsg/doxygen/CC/html
	@mkdir -p $(INSTALL_DIR)/doc/cMsg/javadoc
	cp -rp javadoc         $(INSTALL_DIR)/doc/cMsg
	cp -rp doxygen/C/html  $(INSTALL_DIR)/doc/cMsg/doxygen/C
	cp -rp doxygen/CC/html $(INSTALL_DIR)/doc/cMsg/doxygen/CC

doc:
	cd $(TOPLEVEL); ant javadoc;
	export TOPLEVEL=$(TOPLEVEL) ; doxygen doxygen/DoxyfileC
	export TOPLEVEL=$(TOPLEVEL) ; doxygen doxygen/DoxyfileCC

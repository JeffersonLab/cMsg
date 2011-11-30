#
# cMsg top level Makefile
#

MAKEFILE = Makefile

# if using vxworks, use different set of the lowest level makefiles
ifeq ($(BMS_OS), vxworks)
  ifdef BMS_ARCH
    MAKEFILE = Makefile.$(BMS_OS)-$(BMS_ARCH)
  else
    $(error "Need to define BMS_ARCH if using BMS_OS = vxworks")
  endif
endif

LOCAL_DIR = $(notdir $(shell pwd))

# define TOPLEVEL for use in making doxygen docs
TOPLEVEL = .

# list directories in which there are makefiles to be run (relative to this one)
SRC_DIRS = src/regexp src/libsrc src/libsrc++ src/execsrc src/examples

# declaring a target phony skips the implicit rule search and saves time
.PHONY : first help java javaClean javaDistClean doc tar


first: all

help:
	@echo "make [option]"
	@echo "      env           - list env variables"
	@echo "      mkdirs        - make necessary directories for C,C++"
	@echo "      install       - install all headers and compiled files for C,C++"
	@echo "      uninstall     - uninstall all headers and compiled files for C,C++"
	@echo "      relink        - delete libs and executables, and relink object files"
	@echo "      doc           - generate javadoc and doxygen documentation"
	@echo "      tar           - create a tar file of the cMsg top level directory"
	@echo "      clean         - delete all exec, library, object, and dependency files"
	@echo "      distClean     - clean and remove hidden OS directory"
	@echo "      execClean     - delete all exec and library files"
	@echo "      java          - make java code and put jar file in build/lib dir"
	@echo "      javaClean     - remove all class files from build/classes dir"
	@echo "      javaDistClean - remove all generated files including class, doc, and jar files"

java:
	ant jar;

javaClean:
	ant clean;

javaDistClean:
	ant cleanall;

doc:
	ant javadoc;
#	export TOPLEVEL=$(TOPLEVEL); doxygen doc/doxygen/DoxyfileC
#	export TOPLEVEL=$(TOPLEVEL); doxygen doc/doxygen/DoxyfileCC
	cd doc; $(MAKE) -f $(MAKEFILE);

tar:
	-$(RM) tar/cMsg-3.3.tar.gz;
	tar -X tar/tarexclude -C .. -c -z -f tar/cMsg-3.3.tar.gz $(LOCAL_DIR)

# Use this pattern rule for all other targets
%:
	@for i in $(SRC_DIRS); do \
	   $(MAKE) -C $$i -f $(MAKEFILE) $@; \
	done;

#
# cMsg top level Makefile
#

MAKEFILE = Makefile

# if using vxworks, use different set of the lowest level makefiles
ifeq ($(OS), vxworks)
  ifdef ARCH
    MAKEFILE = Makefile.$(OS)-$(ARCH)
  endif
endif

# define TOPLEVEL for use in making doxygen docs
TOPLEVEL = .

.PHONY : all src env mkdirs install uninstall relink clean distClean execClean java tar doc


all: src

src:
	cd src/regexp;   $(MAKE) -f $(MAKEFILE);
	cd src/libsrc;   $(MAKE) -f $(MAKEFILE);
	cd src/libsrc++; $(MAKE) -f $(MAKEFILE);
	cd src/execsrc;  $(MAKE) -f $(MAKEFILE);
	cd src/examples; $(MAKE) -f $(MAKEFILE);

env:
	cd src/regexp;   $(MAKE) -f $(MAKEFILE) env;
	cd src/libsrc;   $(MAKE) -f $(MAKEFILE) env;
	cd src/libsrc++; $(MAKE) -f $(MAKEFILE) env;
	cd src/execsrc;  $(MAKE) -f $(MAKEFILE) env;
	cd src/examples; $(MAKE) -f $(MAKEFILE) env;

mkdirs:
	cd src/regexp;   $(MAKE) -f $(MAKEFILE) mkdirs;
	cd src/libsrc;   $(MAKE) -f $(MAKEFILE) mkdirs;
	cd src/libsrc++; $(MAKE) -f $(MAKEFILE) mkdirs;
	cd src/execsrc;  $(MAKE) -f $(MAKEFILE) mkdirs;
	cd src/examples; $(MAKE) -f $(MAKEFILE) mkdirs;
	ant prepare;

install:
	cd src/regexp;   $(MAKE) -f $(MAKEFILE) install;
	cd src/libsrc;   $(MAKE) -f $(MAKEFILE) install;
	cd src/libsrc++; $(MAKE) -f $(MAKEFILE) install;
	cd src/execsrc;  $(MAKE) -f $(MAKEFILE) install;
	cd src/examples; $(MAKE) -f $(MAKEFILE) install;

uninstall: 
	cd src/regexp;   $(MAKE) -f $(MAKEFILE) uninstall;
	cd src/libsrc;   $(MAKE) -f $(MAKEFILE) uninstall;
	cd src/libsrc++; $(MAKE) -f $(MAKEFILE) uninstall;
	cd src/execsrc;  $(MAKE) -f $(MAKEFILE) uninstall;
	cd src/examples; $(MAKE) -f $(MAKEFILE) uninstall;

relink:
	cd src/regexp;   $(MAKE) -f $(MAKEFILE) relink;
	cd src/libsrc;   $(MAKE) -f $(MAKEFILE) relink;
	cd src/libsrc++; $(MAKE) -f $(MAKEFILE) relink;
	cd src/execsrc;  $(MAKE) -f $(MAKEFILE) relink;
	cd src/examples; $(MAKE) -f $(MAKEFILE) relink;

clean:
	cd src/regexp;   $(MAKE) -f $(MAKEFILE) clean;
	cd src/libsrc;   $(MAKE) -f $(MAKEFILE) clean;
	cd src/libsrc++; $(MAKE) -f $(MAKEFILE) clean;
	cd src/execsrc;  $(MAKE) -f $(MAKEFILE) clean;
	cd src/examples; $(MAKE) -f $(MAKEFILE) clean;
	ant clean;

distClean:
	cd src/regexp;   $(MAKE) -f $(MAKEFILE) distClean;
	cd src/libsrc;   $(MAKE) -f $(MAKEFILE) distClean;
	cd src/libsrc++; $(MAKE) -f $(MAKEFILE) distClean;
	cd src/execsrc;  $(MAKE) -f $(MAKEFILE) distClean;
	cd src/examples; $(MAKE) -f $(MAKEFILE) distClean;
	ant cleanall;

execClean:
	cd src/regexp;   $(MAKE) -f $(MAKEFILE) execClean;
	cd src/libsrc;   $(MAKE) -f $(MAKEFILE) execClean;
	cd src/libsrc++; $(MAKE) -f $(MAKEFILE) execClean;
	cd src/execsrc;  $(MAKE) -f $(MAKEFILE) execClean;
	cd src/examples; $(MAKE) -f $(MAKEFILE) execClean;

java:
	ant;

doc:
	ant javadoc;
	export TOPLEVEL=$(TOPLEVEL); doxygen doc/doxygen/DoxyfileC
	export TOPLEVEL=$(TOPLEVEL); doxygen doc/doxygen/DoxyfileCC
	cd doc; $(MAKE) -f $(MAKEFILE);

tar:
	-$(RM) tar/cMsg-1.0.tar.gz;
	tar -X tar/tarexclude -C .. -c -z -f tar/cMsg-1.0.tar.gz cMsg

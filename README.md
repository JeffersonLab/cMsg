----------------------------
# **cMsg 5.2 SOFTWARE PACKAGE**
----------------------------

cMsg stands for CODA Message.

The cMsg package is designed to provide client programs with a uniform interface
to an underlying messaging system via an API powerful enough to encompass
asynchronous publish/subscribe and synchronous peer-to-peer messaging.
The advantage of using the cMsg API is that client programs need not
change if the underlying messaging system is modified or replaced.

But cMsg provides much more than a simple API.
The package includes a number of built-in messaging systems,
including a complete, stand-alone, asynchronous publish/subscribe
and synchronous peer-to-peer messaging system, as well as a persistent
network queuing system.  Although cMsg is highly customizable and extendable,
most users will simply use one of the built-in messaging systems.
In addition, a number of useful utilities and examples are provided.

There is a C library as well as a C++ library which is a wrapper on the C.
There are a few utility programs as well as test and examples included.
The Java version is not only a client library but also contains implementations
of underlying messaging systems.

If you only plan to run C/C++ clients and there's a cMsg server running,
you can skip the Java installation. If you only plan to use Java applications,
you can skip the C/C++ installation.

**The home page is:**

  https://coda.jlab.org/drupal/content/coda-messaging-cmsg/

**All code is contained in the github repository linked below (cMsg-5.2 branch).**

  https://github.com/JeffersonLab/cMsg/tree/cMsg-5.2
  
**to clone it:**

    git clone -b cMsg-5.2 https://github.com/JeffersonLab/cMsg.git

-----------------------------
## **Documentation**

Documentation is contained in the repository but may also be accessed at the home site:

Documentation Type | Link
------------ | -------------
PDF User's Guide | https://coda.jlab.org/drupal/content/cmsg-52-users-guide
Javadoc | https://coda.jlab.org/drupal/content/cmsg-52-javadoc
Doxygen doc for C | https://coda.jlab.org/drupal/content/cmsg-52-doxygen-c
Doxygen doc for C++ | https://coda.jlab.org/drupal/content/cmsg-52-doxygen-c-0


----------------------------
# **C/C++ Compilation**
----------------------------

-----------------------------
## **Regular Expression Library**

A regular expression library in C, libcmsgRegex, is compiled
since at the time of cMsg development, there was no such commonly available library.

-----------------------------
## **C++ Library**
The C++ library is called libcmsgxx.
This is a simple wrapper of the C library.

-----------------------------
## **C Library**
The C library is called libcmsg.
It consists of clients of the various underlying messaging systems.


There are 2 different methods to build the C/C++ libraries and executables.
The first uses scons, a Python-based build software package which is available at https://scons.org.
The second uses cmake and make. Linux and MacOS are supported.


### Scons

To get a listing of all the local options available to the scons command,
run _**scons -h**_ in the top-level directory to get this output:
    
        -D                       build from subdirectory of package
        local scons OPTIONS:
        --C                      compile C code only
        --dbg                    compile with debug flag
        --32bits                 compile 32bit libs & executables on 64bit system
        --prefix=<dir>           use base directory <dir> when doing install
        --incdir=<dir>           copy header files to directory <dir> when doing install
        --libdir=<dir>           copy library files to directory <dir> when doing install
        --bindir=<dir>           copy binary files to directory <dir> when doing install 
        install                  install libs, headers, and binaries
        install -c               uninstall libs, headers, and binaries
        doc                      create doxygen and javadoc (in ./doc)
        undoc                    remove doxygen and javadoc (in ./doc)
        tar                      create tar file (in ./tar)
        
        Use scons -H for help about command-line options.


Although this is fairly self-explanatory, executing:
    
    1. cd <cMsg dir>
    2. scons install
        
Note that for C/C++, only Linux and Darwin (Mac OSX) operating systems are supported.
By default, the libraries and executables are placed into the _**$CODA/[arch]/lib**_ and _**bin**_ subdirectories
(eg. Linux-x86_64/lib). If the command line options
–prefix, --incdir, --libdir, or –bindir are used, they take priority.
Be sure to change your LD_LIBRARY_PATH environmental variable to include the correct lib directory.

    
To compile a debug version, execute:
    
    scons install --dbg 
   
### CMake

cMsg can also be compiled with cmake using the included CMakeLists.txt file.
To build the C and C++ libraries and executables on the Mac:
    
    1. cd <cMsg dir>
    2. mkdir build
    3. cd build
    4. cmake .. –DCMAKE_BUILD_TYPE=Release
    5. make
        
To build only C code, place –DC_ONLY=1 on the cmake command line.
In order to compile all the examples as well, place –DMAKE_EXAMPLES=1 on the cmake command line.
The above commands will place everything in the current _**build**_ directory and will keep generated
files from mixing with the source and config files.
    
In addition to a having a copy in the build directory, installing the library, binary and include
files can be done by calling cmake in 2 ways:
    
    1. cmake .. –DCMAKE_BUILD_TYPE=Release –DCODA_INSTALL=<install dir>
    2. make install
        
or
        
    1. cmake .. –DCMAKE_BUILD_TYPE=Release
    2. make install
 
The first option explicitly sets the installation directory. The second option installs in the directory
given in the CODA environmental variable. If neither are defined, an error is given.
The libraries and executables are placed into the _**build/lib**_ and _**build/bin**_ subdirectories.
When doing an install, they are also placed into the _**[install dir]/[arch]/lib**_ and _**bin**_ subdirectories
(eg. Darwin-x86_64/lib). If cmake was run previously, remove the CMakeCache.txt file so
new values are generated and used.
    
To uninstall simply do:
    
        make uninstall
        

----------------------------
# **Java**
----------------------------

The jar files necessary to compile an cMsg jar file are in the java/jars directory.
The et and cMsg jars are compiled with Java 8, the others are probably compiled with
an earlier version. In addition, there are 2 subdirectories:

* java8, which contains all such jars compiled with Java 8, and
* java15 which contains all jars compiled with Java 15.

If a jar file is not available in Java 15 use the Java 8 version.

A pre-compiled _**cMsg-5.2.jar**_ file is found in each of these subdirectories.
Using these allows the user to skip over all the following compilation instructions.


###Building

The java evio uses ant to compile. To get a listing of all the options available to the ant command,
run _**ant help**_ in the evio top level directory to get this output:

    help:
        [echo] Usage: ant [ant options] <target1> [target2 | target3 | ...]
    
        [echo]      targets:
        [echo]      help        - print out usage
        [echo]      env         - print out build file variables' values
        [echo]      compile     - compile java files
        [echo]      clean       - remove class files
        [echo]      cleanall    - remove all generated files
        [echo]      jar         - compile and create jar file
        [echo]      install     - create jar file and install into 'prefix'
        [echo]                    if given on command line by -Dprefix=dir',
        [echo]                    else install into CODA if defined
        [echo]      uninstall   - remove jar file previously installed into 'prefix'
        [echo]                    if given on command line by -Dprefix=dir',
        [echo]                    else installed into CODA if defined
        [echo]      all         - clean, compile and create jar file
        [echo]      javadoc     - create javadoc documentation
        [echo]      developdoc  - create javadoc documentation for developer
        [echo]      undoc       - remove all javadoc documentation
        [echo]      prepare     - create necessary directories


Although this is fairly self-explanatory, executing _**ant**_ is the same as ant compile.
That will compile all the java. All compiled code is placed in the generated _**build**_ directory.
If the user wants a jar file, execute _**ant jar**_ to place the resulting file in the _**build/lib**_ directory.
The java command in the user’s path will be the one used to do the compilation.

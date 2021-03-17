# To use this, do the following the first time:
#   % mkdir build
#   % cd build
#   % cmake .. -DCMAKE_BUILD_TYPE=release
#   % make (install)
#
# To only build C libraries and executables, replace the 3rd cammand above with
#   % cmake .. -DCMAKE_BUILD_TYPE=release -DC_ONLY=1
#
# After doing this once can do the following to recompile
#  % cd <top level evio dir>
#  % cmake --build build (--target install)
#
#
# The installation will default to the CODA env variable if it's defined
# This can be overridden on command line (in build dir) with:
#   % cmake -DCODA_INSTALL=<my_dir> ..
#   % make install


# This call must be placed BEFORE "project" command
cmake_minimum_required(VERSION 3.3)


project(cMsg VERSION 6.0)


set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_DEBUG_POSTFIX -dbg)


# Place libs & binaries in build/lib and bin (this is not for installation)
set(LIBRARY_OUTPUT_PATH    ${CMAKE_BINARY_DIR}/lib)
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)


# Set architecture-related string for installation consistent with scons file
set(ARCH ${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR})
message(STATUS "ARCH = " ${ARCH})


# Install into $CODA_INSTALL directory
if (DEFINED CODA_INSTALL)
    message(STATUS "CODA_INSTALL = " ${CODA_INSTALL})
    set(INSTALL_DIR_DEFINED 1)
    set(CMAKE_INSTALL_PREFIX ${CODA_INSTALL}/${ARCH})
    set(CMAKE_INSTALL_INCLUDEDIR ${CODA_INSTALL}/common/include)
    message(STATUS "CMAKE_INSTALL_PREFIX reset to " ${CMAKE_INSTALL_PREFIX})
# Install into $CODA directory
elseif (DEFINED ENV{CODA})
    message(STATUS "CODA = " $ENV{CODA})
    set(INSTALL_DIR_DEFINED 1)
    set(CMAKE_INSTALL_PREFIX $ENV{CODA}/${ARCH})
    set(CMAKE_INSTALL_INCLUDEDIR $ENV{CODA}/common/include)
    message(STATUS "CMAKE_INSTALL_PREFIX reset to " ${CMAKE_INSTALL_PREFIX})
else()
    message(STATUS "Set the \"CODA\" env var or specify -DCODA_INSTALL=... on the command line or no installation possible!")
endif()



set(C_EXAMPLE_FILES
        src/examples/consumer.c
        src/examples/dummy.c
        src/examples/emuProducer.c
        src/examples/getConsumer.c
        src/examples/getResponder.c
        src/examples/monitor.c
        src/examples/producer.c
        src/examples/shutdowner.c
        )



set(REGEX_HEADER_FILES
        src/regexp/cMsgRegex.h
        src/regexp/engine.h
        src/regexp/regcomp.h
        src/regexp/regerror.h
        src/regexp/regex2.h
        src/regexp/utils.h
        )

set(REGEX_LIB_FILES
        src/regexp/regcomp.c
        src/regexp/regerror.c
        src/regexp/regexec.c
        src/regexp/regfree.c
        )



set(C_HEADER_FILES
        src/libsrc/cMsg.h
        src/libsrc/cMsgCommonNetwork.h
        src/libsrc/cMsgConstants.h
        src/libsrc/cMsgDomain.h
        src/libsrc/cMsgNetwork.h
        src/libsrc/cMsgPrivate.h
        src/libsrc/hash.h
        src/libsrc/polarssl_aes.h
        src/libsrc/polarssl_config.h
        src/libsrc/polarssl_padlock.h
        src/libsrc/rwlock.h
        )

set(C_LIB_FILES
        src/libsrc/cMsg.c
        src/libsrc/cMsgCommonNetwork.c
        src/libsrc/cMsgCompoundPayload.c
        src/libsrc/cMsgDomain.c
        src/libsrc/cMsgDomainListenThread.c
        src/libsrc/cMsgDomainUtil.c
        src/libsrc/cMsgMatching.c
        src/libsrc/dummyDomain.c
        src/libsrc/emuDomain.c
        src/libsrc/encode.c
        src/libsrc/fileDomain.c
        src/libsrc/hash.c
        src/libsrc/polarssl_aes.c
        src/libsrc/polarssl_padlock.c
        src/libsrc/rcDomain.c
        src/libsrc/rcDomainListenThread.c
        src/libsrc/rwlock.c
        )



set(CPP_HEADER_FILES
        src/libsrc++/cMsg.hxx
        src/libsrc++/cMsgPrivate.hxx
        )

set(CPP_LIB_FILES
        src/libsrc++/cMsgPayload.cc
        src/libsrc++/cMsgWrapper.cc
        )


set(CPP_TEST
        src/test/cMsgPayloadTest.cc
        src/test/cMsgReceive.cc
        src/test/cMsgStringTest.cc
        src/test/cpptest.cc
        )


set(C_TEST
        src/test/rcClient.c
        )



find_package(Doxygen)

if( DOXYGEN_FOUND )
    message(STATUS "Doxygen found")

    # Change these values from cmake defaults in the Doxyfile
    set(DOXYGEN_GENERATE_TREEVIEW YES)
    set(DOXYGEN_REFERENCED_BY_RELATION YES)
    set(DOXYGEN_EXCLUDE_PATTERNS */.* */CMakeFiles/* */_CPack_Packages/* CMakeLists.txt CMakeCache.txt)
    set(DOXYGEN_FILE_PATTERNS *.cpp *.h)
    #set(DOXYGEN_WARN_LOGFILE doxyerrors.log)
    set(DOXYGEN_SORT_MEMBERS_CTORS_1ST YES)
    set(DOXYGEN_SHOW_GROUPED_MEMB_INC YES)
    set(DOXYGEN_INLINE_IHERITED_MEMB YES)
    set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
    set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/doc/doxygen/CC)

    # Add target of "docs"
    doxygen_add_docs(docs src/libsrc )
elseif(NOT DEFINED C_ONLY)
    message(FATAL_ERROR "Doxygen NOT found, cmake will exit." )
endif()


# Shared regexp C library
add_library(cmsgRegex SHARED ${REGEX_LIB_FILES})
include_directories(cmsgRegex PUBLIC src/regexp)


# Shared cMsg C library
add_library(cMsg SHARED ${C_LIB_FILES})
target_link_libraries(cMsg cmsgRegex)
include_directories(cMsg PUBLIC src/libsrc src/regexp)


# If we want to compile the C++ stuff ...
if (NOT DEFINED C_ONLY)
    # Shared cMsg C++ library
    add_library(cMsgxx SHARED ${CPP_LIB_FILES})
    target_link_libraries(cMsgxx cMsg cmsgRegex)
    include_directories(cMsgxx PUBLIC src/libsrc++)

    # Exec Executables
    add_executable(cMsgMonitor src/execsrc/cMsgMonitor.cc)
    # Put debug extension on if applicable
    set_target_properties(cMsgMonitor PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
    target_link_libraries(cMsgMonitor cMsgxx cMsg cmsgRegex pthread dl)
    include_directories(cMsgMonitor PUBLIC src/libsrc++ src/libsrc)

    add_executable(cMsgCommand src/execsrc/cMsgCommand.cc)
    set_target_properties(cMsgCommand PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
    target_link_libraries(cMsgCommand cMsgxx cMsg cmsgRegex pthread dl)
    include_directories(cMsgCommand PUBLIC src/libsrc++ src/libsrc)

#    # Contrib Executables (this needs extra work as EPICS headers are necessary)
#    add_executable(cMsgCAGateway src/contrib/cMsgCAGateway.cc)
#    set_target_properties(cMsgCAGateway PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
#    target_link_libraries(cMsgCAGateway cMsgxx cMsg cmsgRegex pthread dl)
#
#    add_executable(epics_server src/contrib/epics_server.cc)
#    include_directories(epics_server PUBLIC src/libsrc src/libsrc++ src/regexp src/contrib)
#    set_target_properties(epics_server PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
#    target_link_libraries(epics_server cMsgxx cMsg cmsgRegex pthread dl)

    # CC Test programs
    foreach(fileName ${CPP_TEST})
        # Get file name with no directory or extension as executable name
        get_filename_component(execName ${fileName} NAME_WE)
        # Create executable from file
        add_executable(${execName} ${fileName})
        # Put debug extension on if applicable
        set_target_properties(${execName} PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
        # Needs these libs
        target_link_libraries(${execName} cMsgxx cMsg cmsgRegex pthread)

        # Only install if installation directory has been defined
        if (DEFINED INSTALL_DIR_DEFINED)
            # Install into bin/testC dir
            install(TARGETS ${execName} RUNTIME DESTINATION bin/test)
        endif()
    endforeach(fileName)

else()
    message(STATUS "NOT compiling C++ evio library!" )
endif()


# C Test programs
foreach(fileName ${C_TEST})
    # Get file name with no directory or extension as executable name
    get_filename_component(execName ${fileName} NAME_WE)
    # Create executable from file
    add_executable(${execName} ${fileName})
    # Put debug extension on if applicable
    set_target_properties(${execName} PROPERTIES DEBUG_POSTFIX ${CMAKE_DEBUG_POSTFIX})
    # Needs these libs
    target_link_libraries(${execName} cMsg cmsgRegex pthread)

    # Only install if installation directory has been defined
    if (DEFINED INSTALL_DIR_DEFINED)
        # Install into bin/testC dir
        install(TARGETS ${execName} RUNTIME DESTINATION bin/test)
    endif()
endforeach(fileName)


# Only install if installation directory has been defined
if (DEFINED INSTALL_DIR_DEFINED)
    # Installation defaulting to ${CMAKE_INSTALL_PREFIX}/lib or include
    install(TARGETS cMsg cmsgRegex LIBRARY DESTINATION lib)
    install(FILES ${C_HEADER_FILES} DESTINATION include)

    if (NOT DEFINED C_ONLY)
        install(TARGETS cMsgxx LIBRARY DESTINATION lib)
        install(FILES ${CPP_HEADER_FILES} DESTINATION include)
        install(FILES ${CPP_HEADER_FILES} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endif()
endif()


# Uninstall target
if(NOT TARGET uninstall)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake" IMMEDIATE @ONLY)

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()


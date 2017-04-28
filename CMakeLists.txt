cmake_minimum_required(VERSION 3.2)
project(cMsg_6_0)

include_directories($ENV{CODA}/common/include)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(SOURCE_FILES
    src/contrib/epics_server.hxx
    src/contrib/cMsgCAGateway.cc
    src/contrib/epics_server.cc

    src/examples/consumer.c
    src/examples/dummy.c
    src/examples/emuProducer.c
    src/examples/getConsumer.c
    src/examples/getResponder.c
    src/examples/monitor.c
    src/examples/producer.c
    src/examples/shutdowner.c

    src/execsrc/cMsgCommand.cc
    src/execsrc/cMsgMonitor.cc

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

    src/libsrc++/cMsg.hxx
    src/libsrc++/cMsgPrivate.hxx
    src/libsrc++/cMsgPayload.cc
    src/libsrc++/cMsgWrapper.cc

    src/regexp/cMsgRegex.h
    src/regexp/engine.h
    src/regexp/regcomp.h
    src/regexp/regerror.h
    src/regexp/regex2.h
    src/regexp/utils.h

    src/regexp/engine.c
    src/regexp/regcomp.c
    src/regexp/regerror.c
    src/regexp/regexec.c
    src/regexp/regfree.c

    src/test/cMsgPayloadTest.cc
    src/test/cMsgReceive.cc
    src/test/cMsgStringTest.cc
    src/test/cpptest.cc
    src/test/rcClient.c)

add_executable(cMsg_6_0 ${SOURCE_FILES})
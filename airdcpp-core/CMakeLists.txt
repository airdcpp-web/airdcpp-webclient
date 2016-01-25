project (airdcpp)
include_directories(${PROJECT_BINARY_DIR})
cmake_minimum_required (VERSION 2.6)

aux_source_directory(${PROJECT_SOURCE_DIR}/airdcpp airdcpp_srcs)
file (GLOB airdcpp_hdrs ${PROJECT_SOURCE_DIR}/airdcpp/*.h)

set (AIRDCPP_SRCS ${airdcpp_srcs} PARENT_SCOPE)
set (AIRDCPP_HDRS ${airdcpp_hdrs} PARENT_SCOPE)

include_directories (${Boost_INCLUDE_DIR} ${BZIP2_INCLUDE_DIR} ${ZLIB_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR} 
			${LevelDB_INCLUDE_DIR} ${LibNatpmp_INCLUDE_DIR})

if (WIN32)
  set (LINK STATIC)
else (WIN32)
  list (REMOVE_ITEM airdcpp_hdrs ${PROJECT_SOURCE_DIR}/airdcpp/ZipFile.h)
  list (REMOVE_ITEM airdcpp_srcs ${PROJECT_SOURCE_DIR}/airdcpp/ZipFile.cpp)
  list (REMOVE_ITEM airdcpp_srcs ${PROJECT_SOURCE_DIR}/airdcpp/Updater.cpp)  

  if (NOT CMAKE_CROSSCOMPILING)
    set (PTHREADS "pthread")
  endif (NOT CMAKE_CROSSCOMPILING)
endif (WIN32)

if (NOT LINK)
  set (LINK SHARED)
endif (NOT LINK)

if (APPLE)
  set (APPLE_LIBS "iconv")
endif (APPLE)

#set (UPNP "extra")

add_library (airdcpp ${LINK} ${airdcpp_srcs})

#if (USE_MINIUPNP)
#    set_property(SOURCE ${PROJECT_SOURCE_DIR}/DCPlusPlus.cpp ${PROJECT_SOURCE_DIR}/UPnPManager.cpp  PROPERTY COMPILE_DEFINITIONS USE_MINIUPNP )
#endif()


set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/StringDefs.cpp APPEND_STRING PROPERTY COMPILE_FLAGS " -fno-var-tracking ")
set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/Updater.h PROPERTY COMPILE_DEFINITIONS NO_CLIENT_UPDATER)

add_definitions (-DNO_CLIENT_UPDATER)

if (ICONV_SECOND_ARGUMENT_IS_CONST)
    set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/Text.cpp PROPERTY COMPILE_DEFINITIONS ICONV_CONST=const)
endif (ICONV_SECOND_ARGUMENT_IS_CONST)

if (HAVE_IFADDRS_H)
  set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/AirUtil.cpp PROPERTY COMPILE_DEFINITIONS HAVE_IFADDRS_H APPEND)
endif (HAVE_IFADDRS_H)

if (WIN32)
   set_property(TARGET airdcpp PROPERTY COMPILE_FLAGS)
else(WIN32)
   set_property(TARGET airdcpp PROPERTY COMPILE_FLAGS -fPIC)
endif (WIN32)

target_link_libraries (airdcpp ${PTHREADS} ${BZIP2_LIBRARIES} ${ZLIB_LIBRARIES} ${MINIUPNP_LIBRARY}
${OPENSSL_LIBRARIES} ${ICONV_LIBRARIES} ${APPLE_LIBS} ${UPNP} ${Boost_LIBRARIES} ${LevelDB_LIBRARY} ${LibGeoIP_LIBRARY}
${TBB_LIBRARIES} )

set_target_properties(airdcpp PROPERTIES VERSION ${SOVERSION} OUTPUT_NAME "airdcpp")

if (APPLE)
  set (LIBDIR1 .)
  set (LIBDIR ${PROJECT_NAME_GLOBAL}.app/Contents/MacOS)
endif(APPLE)

if (LINK STREQUAL SHARED)
install (TARGETS airdcpp
            LIBRARY DESTINATION ${LIBDIR} NAMELINK_SKIP
            BUNDLE DESTINATION ${LIBDIR1})
endif (LINK STREQUAL SHARED)

if (WITH_DEV_FILES)
    if (NOT AIRDCPP_INCLUDE_DIR)
        set (AIRDCPP_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include/airdcpp")
    endif (NOT AIRDCPP_INCLUDE_DIR)
    message (STATUS "Headers of libairdcpp will be installed to: ${AIRDCPP_INCLUDE_DIR}")
    install (FILES ${airdcpp_hdrs} DESTINATION ${AIRDCPP_INCLUDE_DIR}/airdcpp/)
endif (WITH_DEV_FILES)

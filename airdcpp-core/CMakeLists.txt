project (airdcpp)
include_directories(${PROJECT_BINARY_DIR})
cmake_minimum_required (VERSION 3.0.2)

aux_source_directory(${PROJECT_SOURCE_DIR}/airdcpp airdcpp_srcs)
file (GLOB airdcpp_hdrs ${PROJECT_SOURCE_DIR}/airdcpp/*.h)

set (AIRDCPP_SRCS ${airdcpp_srcs} PARENT_SCOPE)
set (AIRDCPP_HDRS ${airdcpp_hdrs} PARENT_SCOPE)

include_directories (${Boost_INCLUDE_DIR} ${BZIP2_INCLUDE_DIR} ${ZLIB_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR}
			${LevelDB_INCLUDE_DIR})


# WINDOWS
if (NOT WIN32)
  list (REMOVE_ITEM airdcpp_hdrs ${PROJECT_SOURCE_DIR}/airdcpp/ZipFile.h)
  list (REMOVE_ITEM airdcpp_srcs ${PROJECT_SOURCE_DIR}/airdcpp/ZipFile.cpp)
  list (REMOVE_ITEM airdcpp_srcs ${PROJECT_SOURCE_DIR}/airdcpp/Updater.cpp)
  list (REMOVE_ITEM airdcpp_srcs ${PROJECT_SOURCE_DIR}/airdcpp/Mapper_WinUPnP.cpp)

  if (NOT CMAKE_CROSSCOMPILING)
    set (PTHREADS "pthread")
  endif (NOT CMAKE_CROSSCOMPILING)

  set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/Util.cpp APPEND PROPERTY COMPILE_DEFINITIONS RESOURCE_DIRECTORY="${RESOURCE_DIRECTORY}")
  set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/Util.cpp APPEND PROPERTY COMPILE_DEFINITIONS GLOBAL_CONFIG_DIRECTORY="${GLOBAL_CONFIG_DIRECTORY}")

  set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/Updater.h PROPERTY COMPILE_DEFINITIONS NO_CLIENT_UPDATER)
  add_definitions (-DNO_CLIENT_UPDATER)
endif ()


# APPLE
if (APPLE)
  list (APPEND airdcpp_extra_libs ${ICONV_LIBRARIES})
endif (APPLE)


# OPTIONAL LIBRARIES
if (TBB_FOUND)
  set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/concurrency.h PROPERTY COMPILE_DEFINITIONS HAVE_INTEL_TBB)
  list (APPEND airdcpp_extra_libs ${TBB_LIBRARIES})
endif (TBB_FOUND)

if (LibNatpmp_FOUND )
  set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/MappingManager.cpp PROPERTY COMPILE_DEFINITIONS HAVE_NATPMP_H)

  list (APPEND airdcpp_extra_libs ${LibNatpmp_LIBRARY})
else (LibNatpmp_FOUND )
  list (REMOVE_ITEM airdcpp_srcs ${PROJECT_SOURCE_DIR}/airdcpp/Mapper_NATPMP.cpp)
endif (LibNatpmp_FOUND )

#if (SNAPPY_FOUND)
#  list (APPEND airdcpp_extra_libs ${SNAPPY_LIBRARIES})
#endif (SNAPPY_FOUND)

# GENERIC COMPILING PROPERTIES
if (CMAKE_BUILD_TYPE STREQUAL Debug)
  add_definitions(-D_DEBUG)
endif()

set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/StringDefs.cpp APPEND_STRING PROPERTY COMPILE_FLAGS " -fno-var-tracking ")

if (ICONV_SECOND_ARGUMENT_IS_CONST)
  set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/Text.cpp PROPERTY COMPILE_DEFINITIONS ICONV_CONST=const)
endif (ICONV_SECOND_ARGUMENT_IS_CONST)

if (HAVE_IFADDRS_H)
    set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/AirUtil.cpp PROPERTY COMPILE_DEFINITIONS HAVE_IFADDRS_H APPEND)
endif (HAVE_IFADDRS_H)

if (HAVE_MNTENT_H)
    set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/File.cpp PROPERTY COMPILE_DEFINITIONS HAVE_MNTENT_H APPEND)
endif (HAVE_MNTENT_H)

if (HAVE_POSIX_FADVISE)
    add_definitions (-DHAVE_POSIX_FADVISE)
endif (HAVE_POSIX_FADVISE)



# LINKING
add_library (airdcpp ${airdcpp_srcs})

target_link_libraries (airdcpp ${PTHREADS} ${BZIP2_LIBRARIES} ${ZLIB_LIBRARIES} ${MINIUPNP_LIBRARY}
${OPENSSL_LIBRARIES} ${ICONV_LIBRARIES} ${airdcpp_extra_libs} ${UPNP} ${Boost_LIBRARIES} ${LevelDB_LIBRARY} ${SNAPPY_LIBRARIES} ${LIBMAXMINDDB_LIBRARY} )

set_target_properties(airdcpp PROPERTIES VERSION ${SOVERSION} OUTPUT_NAME "airdcpp")

set_target_properties(airdcpp PROPERTIES COTIRE_CXX_PREFIX_HEADER_INIT "airdcpp/stdinc.h")
if (COMMAND cotire)
  cotire(airdcpp)
endif()



#if (WIN32)
#   set_property(TARGET airdcpp PROPERTY COMPILE_FLAGS)
#else(WIN32)
#   set_property(TARGET airdcpp PROPERTY COMPILE_FLAGS -fPIC)
#endif (WIN32)


# INSTALLATION
if (APPLE)
  set (LIBDIR1 .)
  set (LIBDIR ${PROJECT_NAME_GLOBAL}.app/Contents/MacOS)
endif(APPLE)

if (BUILD_SHARED_LIBS)
  install (TARGETS airdcpp
            LIBRARY DESTINATION "${LIBDIR}" NAMELINK_SKIP
            BUNDLE DESTINATION ${LIBDIR1})
endif ()

if (WITH_DEV_FILES)
    if (NOT AIRDCPP_INCLUDE_DIR)
        set (AIRDCPP_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include/airdcpp")
    endif (NOT AIRDCPP_INCLUDE_DIR)
    message (STATUS "Headers of libairdcpp will be installed to: ${AIRDCPP_INCLUDE_DIR}")
    install (FILES ${airdcpp_hdrs} DESTINATION ${AIRDCPP_INCLUDE_DIR}/airdcpp/)
endif (WITH_DEV_FILES)

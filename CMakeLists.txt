project (airdcpp-webclient)
cmake_minimum_required (VERSION 2.8.6)

if (APPLE)
  set (PROJECT_NAME_GLOBAL AirDC++ Web Client)
else (APPLE)
  set (PROJECT_NAME_GLOBAL airdcpp-webclient)
endif (APPLE)

set (PROJECT_NAME "AirDC++ Web Client")
set (TAG_APPLICATION "AirDC++w")
set (VERSION "2.0.0b")
set (SOVERSION "2.0.0b" )

set (GNUCXX_MINIMUM_VERSION "4.8")

include (GNUInstallDirs)
include (CheckIncludeFile)
include (CheckIncludeFiles)
include (CheckFunctionExists)
include (FindPkgConfig)
include (CheckCXXSourceCompiles)
include (cmake/CheckAtomic.cmake)



# PRE-CHECKS
CHECK_FUNCTION_EXISTS(posix_fadvise HAVE_POSIX_FADVISE)
CHECK_FUNCTION_EXISTS(mallinfo HAVE_MALLINFO)
CHECK_FUNCTION_EXISTS(malloc_stats HAVE_MALLOC_STATS)
CHECK_FUNCTION_EXISTS(malloc_trim HAVE_MALLOC_TRIM)
CHECK_INCLUDE_FILES ("mntent.h" HAVE_MNTENT_H)
CHECK_INCLUDE_FILES ("malloc.h;dlfcn.h;inttypes.h;memory.h;stdlib.h;strings.h;sys/stat.h;limits.h;unistd.h;" FUNCTION_H)
CHECK_INCLUDE_FILES ("sys/socket.h;net/if.h;ifaddrs.h;sys/types.h" HAVE_IFADDRS_H)
CHECK_INCLUDE_FILES ("sys/types.h;sys/statvfs.h;limits.h;stdbool.h;stdint.h" FS_USAGE_C)

set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake")

if (CMAKE_COMPILER_IS_GNUCXX)
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS GNUCXX_MINIMUM_VERSION)
    message (FATAL_ERROR "GCC version must be at least ${GNUCXX_MINIMUM_VERSION}!")
  endif()
#else()
#  message (FATAL_ERROR "Invalid compiler!")
endif()



# OPTIONS
option (INSTALL_RUNTIME_PATH "Install rpath" OFF)
option (LOCAL_BOOST "Use local boost headers" OFF)
option (INSTALL_WEB_UI "Download and install the Web UI package" ON)
option (OPENSSL_MSVC "Use MSVC build openssl (only for Windows)" OFF)
option (WITH_ASAN "Enable address sanitizer" OFF) # With clang: http://clang.llvm.org/docs/AddressSanitizer.html



# STATIC/SHARED
if (NOT DEFINED BUILD_SHARED_LIBS AND NOT WIN32)
  set(BUILD_SHARED_LIBS ON)
elseif (NOT BUILD_SHARED_LIBS)
  # http://cmake.3232098.n2.nabble.com/Howto-compile-static-executable-td5580269.html
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
  set_property(GLOBAL PROPERTY LINK_SEARCH_START_STATIC ON)
  set_property(GLOBAL PROPERTY LINK_SEARCH_END_STATIC ON)

  set(Boost_USE_STATIC_RUNTIME ON)
  set(Boost_USE_STATIC_LIBS ON)

  #message(STATUS "CMAKE_EXE_LINKER_FLAGS: ${CMAKE_EXE_LINKER_FLAGS}")
  #message(STATUS "CMAKE_EXE_LINK_DYNAMIC_C_FLAGS: ${CMAKE_EXE_LINK_DYNAMIC_C_FLAGS}")
  #message(STATUS "CMAKE_EXE_LINK_DYNAMIC_CXX_FLAGS: ${CMAKE_EXE_LINK_DYNAMIC_CXX_FLAGS}")
  #message(STATUS "CMAKE_SHARED_LIBRARY_C_FLAGS: ${CMAKE_SHARED_LIBRARY_C_FLAGS}")
  #message(STATUS "CMAKE_SHARED_LIBRARY_CXX_FLAGS: ${CMAKE_SHARED_LIBRARY_CXX_FLAGS}")
  #message(STATUS "CMAKE_SHARED_LIBRARY_LINK_C_FLAGS: ${CMAKE_SHARED_LIBRARY_LINK_C_FLAGS}")
  #message(STATUS "CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS: ${CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS}")

  set(CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS} "-static-libgcc -static-libstdc++ -static")
  #set(CMAKE_EXE_LINKER_FLAGS ${CMAKE_EXE_LINKER_FLAGS} "-static-libgcc -static-libstdc++")

  set(CMAKE_EXE_LINK_DYNAMIC_C_FLAGS)
  set(CMAKE_EXE_LINK_DYNAMIC_CXX_FLAGS)
  set(CMAKE_SHARED_LIBRARY_C_FLAGS)
  set(CMAKE_SHARED_LIBRARY_CXX_FLAGS)
  set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)
  set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)
endif()


## DEPENDENCIES
find_package (GeoIP REQUIRED)
find_package (PkgConfig)
find_package (BZip2 REQUIRED)
find_package (ZLIB REQUIRED)
find_package (OpenSSL REQUIRED)
find_package (Threads REQUIRED)
find_package (Iconv REQUIRED)
find_package (Miniupnpc REQUIRED)
find_package (LibNatpmp)
find_package (Git)
find_package (LevelDB REQUIRED)
find_package (websocketpp REQUIRED)
find_package (Backtrace)
find_package (TBB)


if (TBB_FOUND)
  add_definitions ( -DHAVE_INTEL_TBB )
endif(TBB_FOUND)

message (STATUS "Building with UPNP support (miniupnpc)")
set (MINIUPNP_INCLUDE_DIR)
set (MINIUPNP_LIBRARY)
if (LOCAL_MINIUPNP)
  add_subdirectory(upnp)
  set (MINIUPNP_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/miniupnpc")
  set (MINIUPNP_LIBRARY "miniupnpc")
else (LOCAL_MINIUPNP)
  find_package (Miniupnpc REQUIRED)
endif (LOCAL_MINIUPNP)

set(Boost_USE_MULTITHREADED ON)
find_package(Boost 1.54.0 COMPONENTS regex system thread REQUIRED)

set (Boost_INCLUDE_DIR)


# PATHS
if (NOT LIBDIR)
  set (LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
endif (NOT LIBDIR)
message (STATUS "Library directory: ${LIBDIR}")

if (INSTALL_RUNTIME_PATH OR NOT DEFINED INSTALL_RUNTIME_PATH)
  set (CMAKE_INSTALL_RPATH "${LIBDIR}")
endif ()

if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE "RelWithDebInfo")
endif (NOT CMAKE_BUILD_TYPE)

if (NOT RESOURCE_DIRECTORY)
  set (RESOURCE_DIRECTORY "${CMAKE_INSTALL_FULL_DATAROOTDIR}/airdcpp/")
endif (NOT RESOURCE_DIRECTORY)
message (STATUS "Resource directory: ${RESOURCE_DIRECTORY}")

if (NOT GLOBAL_CONFIG_DIRECTORY)
  set (GLOBAL_CONFIG_DIRECTORY "${CMAKE_INSTALL_FULL_SYSCONFDIR}/airdcpp/")
endif (NOT GLOBAL_CONFIG_DIRECTORY)
message (STATUS "Startup config directory: ${GLOBAL_CONFIG_DIRECTORY}")


if (APPLE)
    add_definitions(-DAPPLE)
endif (APPLE)

if (WIN32 OR HAIKU)
    set (BINDIR .)
elseif (UNIX)
    set (BINDIR bin)
    if (APPLE)
        set (BUNDLEDIR .)
    endif (APPLE)
endif (WIN32 OR HAIKU)


#find_program(GIT_EXECUTABLE git PATHS /sw/bin)

#if (GIT_EXECUTABLE)
#	# Version number for the library naming and UI installation
#	execute_process (COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
#									 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
#									 OUTPUT_VARIABLE GIT_TAG
#									 ERROR_VARIABLE DUMMYERROR
#									 OUTPUT_STRIP_TRAILING_WHITESPACE)
#	if (GIT_TAG)
#		message (STATUS "-- Git tag: ${GIT_TAG}")
#		set (SOVERSION ${GIT_TAG})
#	endif(GIT_TAG)
#endif (GIT_EXECUTABLE)


# SCRIPTS

# Version information for the client
execute_process (COMMAND sh generate_version.sh ./airdcpp-core/airdcpp/version.inc ${VERSION} ${TAG_APPLICATION} 
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            RESULT_VARIABLE SCRIPT_RESULT)

if(NOT "${SCRIPT_RESULT}" STREQUAL "0")
    message(FATAL_ERROR "Failed to generate version file")
endif()

# Generate StringDefs.cpp

# 1.0.0 and 1.0.1 searched for a wrong command
unset(PYTHON_EXECUTABLE CACHE)

find_program(PYTHON_EXECUTABLE python PATHS /sw/bin)
if(NOT PYTHON_EXECUTABLE)
  message(FATAL_ERROR "Could not find 'python' executable. Please install python")
endif(NOT PYTHON_EXECUTABLE)

execute_process (COMMAND ${PYTHON_EXECUTABLE} generate_stringdefs.py airdcpp-core/airdcpp/
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            RESULT_VARIABLE SCRIPT_RESULT)

if(NOT "${SCRIPT_RESULT}" STREQUAL "0")
    message(FATAL_ERROR "Failed to generate string definitions")
endif()


# COMPILING DEFINITIONS
add_definitions (-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -D_REENTRANT
                 -D_DATADIR="${CLIENT_DATA_DIR}" -DLOCALE_DIR="${LOCALE_DIR}" -DBUILDING_AIRDCPP)

if (CMAKE_CXX_COMPILER_ID STREQUAL GNU OR CMAKE_CXX_COMPILER_ID STREQUAL Clang)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x -pipe -Wformat -Werror=format-security -fpermissive")
    if (USE_GOLD)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fuse-linker-plugin")
        set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fuse-linker-plugin")
    endif()
    if (NOT APPLE)
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--as-needed")
    endif ()
endif ()

if (WITH_ASAN)
    # llvm-symbolizer must be on the path.
    # Ubuntu: ASAN_SYMBOLIZER_PATH=/usr/lib/llvm-3.8/bin/llvm-symbolizer ASAN_OPTIONS=suppressions=MyASan.supp
    # MyASan.supp: interceptor_via_lib:libminiupnpc.so
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-optimize-sibling-calls -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
    message(STATUS "Address sanitizer is enabled")
endif()

if (NOT CMAKE_BUILD_TYPE MATCHES "Debug" AND NOT ARM_OPTIMIZE_FULL)
  if (CMAKE_SYSTEM_PROCESSOR STREQUAL "arm" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7l" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "armv6l")
    set (CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -Os")
    set (CMAKE_CXX_FLAGS_RELWITHDEBINFO "-DNDEBUG -Os -g")

    message (STATUS "---------------------------------------------------------------------------------------------------------")
    message (STATUS "IMPORTANT")
    message (STATUS "Certain code optimizations are disabled by default on ARM architectures due to possible stability issues")
    message (STATUS "If you want to compile the client with full optimizations enabled, run cmake with the option -DARM_OPTIMIZE_FULL=1 (please report back your findings if there are no issues)")
    message (STATUS "More information: https://github.com/airdcpp-web/airdcpp-webclient/issues/141#issuecomment-233186424")
    message (STATUS "---------------------------------------------------------------------------------------------------------")
  endif()
endif()



# FILES TO COMPILE
include_directories(${airdcpp_BINARY_DIR}
   ${CMAKE_CURRENT_SOURCE_DIR}/airdcpp-core
   ${CMAKE_CURRENT_SOURCE_DIR}/airdcpp-webapi
   ${OPENSSL_INCLUDE_DIR}
   )

add_subdirectory (airdcpp-core)
add_subdirectory (airdcpp-webapi)
add_subdirectory (airdcppd)



# REPORT
message(STATUS "Build shared libraries: ${BUILD_SHARED_LIBS}")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_SYSTEM: ${CMAKE_SYSTEM}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "CMAKE_C_COMPILER_ID: ${CMAKE_C_COMPILER_ID}")
message(STATUS "CMAKE will use this libs (and headers) during build:
    miniupnpc (headers):${MINIUPNP_LIBRARY} (${MINIUPNP_INCLUDE_DIR})
    boost (headers): ${Boost_LIBRARIES} (${Boost_INCLUDE_DIR})
    bzip2 (headers): ${BZIP2_LIBRARIES} (${BZIP2_INCLUDE_DIR})
    zlib (headers): ${ZLIB_LIBRARIES} (${ZLIB_INCLUDE_DIR})
    openssl (headers): ${OPENSSL_LIBRARIES} (${OPENSSL_INCLUDE_DIR})
    iconv (headers): ${ICONV_LIBRARIES} (${ICONV_INCLUDE_DIR})
    geoip (headers): ${LibGeoIP_LIBRARY} (${LibGeoIP_INCLUDE_DIR})
    leveldb (headers): ${LevelDB_LIBRARY} (${LevelDB_INCLUDE_DIR})
    websocketpp (headers): (${WEBSOCKETPP_INCLUDE_DIR})
    natpmp (headers): ${LibNatpmp_LIBRARY} (${LibNatpmp_INCLUDE_DIR})
    tbb (headers): ${TBB_LIBRARIES} (${TBB_INCLUDE_DIRS})
    ")



# WEB UI
if (INSTALL_WEB_UI)
  find_program(NPM_EXECUTABLE npm PATHS /sw/bin)
  if(NOT NPM_EXECUTABLE)
    message(FATAL_ERROR "Could not find 'npm' executable. Please install npm")
  endif()

  message(STATUS "Installing Web UI")
  execute_process (COMMAND sh install_webui.sh ${SOVERSION}
                   WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                   RESULT_VARIABLE SCRIPT_RESULT)

  if(NOT "${SCRIPT_RESULT}" STREQUAL "0")
    message(FATAL_ERROR "Failed to install Web UI")
  endif()
else (INSTALL_WEB_UI)
  message(STATUS "Skipping Web UI installation")
endif (INSTALL_WEB_UI)



CONFIGURE_FILE(
  "${PROJECT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
  "${PROJECT_BINARY_DIR}/cmake_uninstall.cmake"
  IMMEDIATE @ONLY)
ADD_CUSTOM_TARGET(uninstall
  "${CMAKE_COMMAND}" -P "${PROJECT_BINARY_DIR}/cmake_uninstall.cmake")

if(INSTALL_WEB_UI)
  install (DIRECTORY node_modules/airdcpp-webui/dist/ DESTINATION ${RESOURCE_DIRECTORY}web-resources)
endif (INSTALL_WEB_UI)

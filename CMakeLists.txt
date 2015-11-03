project (airdcpp-webclient)
cmake_minimum_required (VERSION 2.6.3)

if (APPLE)
  set (PROJECT_NAME_GLOBAL AirDC++ Web Client)
else (APPLE)
  set (PROJECT_NAME_GLOBAL airdcpp-webclient)
endif (APPLE)

set (PROJECT_NAME "AirDC++ Web Client")
set (VERSION "0.0.1")
set (SOVERSION "0.0.1" )
set (VERSION_SUFFIX "GIT")

include (CheckIncludeFile)
include (CheckIncludeFiles)
include (CheckFunctionExists)
include (FindPkgConfig)
include (CheckCXXSourceCompiles)

#IF (GPTOOLS_CPU AND GPTOOLS_MEM)
#    SET (GPTOOLS_CPU FALSE)
#    SET (GPTOOLS_MEM FALSE)
#    SET (GPTOOLS_CPU_AND_MEM TRUE)
#ENDIF (GPTOOLS_CPU AND GPTOOLS_MEM)
#IF (GPTOOLS_MEM)
#    MESSAGE (STATUS "Enabled memory profiling via Google perftools")
#    SET (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -ltcmalloc")
#    SET (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -ltcmalloc")
#ENDIF (GPTOOLS_MEM)
#IF (GPTOOLS_CPU)
#    MESSAGE (STATUS "Enabled CPU profiling via Google perftools")
#    SET (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lprofiler")
#    SET (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -lprofiler")
#ENDIF (GPTOOLS_CPU)
#IF (GPTOOLS_CPU_AND_MEM)
#    MESSAGE (STATUS "Enabled both CPU and memory profiling via Google perftools")
#    SET (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -ltcmalloc_and_profiler")
#    SET (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -ltcmalloc_and_profiler")
#ENDIF (GPTOOLS_CPU_AND_MEM)

CHECK_FUNCTION_EXISTS(mallinfo HAVE_MALLINFO)
CHECK_FUNCTION_EXISTS(malloc_stats HAVE_MALLOC_STATS)
CHECK_FUNCTION_EXISTS(malloc_trim HAVE_MALLOC_TRIM)
CHECK_INCLUDE_FILES ("malloc.h;dlfcn.h;inttypes.h;memory.h;stdlib.h;strings.h;sys/stat.h;limits.h;unistd.h;" FUNCTION_H)
CHECK_INCLUDE_FILES ("sys/socket.h;net/if.h;ifaddrs.h;sys/types.h" HAVE_IFADDRS_H)
CHECK_INCLUDE_FILES ("sys/types.h;sys/statvfs.h;limits.h;stdbool.h;stdint.h" FS_USAGE_C)

set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake")

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
find_package (Backtrace)

#option (ENABLE_STACKTRACE "Show stacktrace when program receive SIGSEGV" ON)
option (INSTALL_RUNTIME_PATH "Install rpath" OFF)
option (LOCAL_BOOST "Use local boost headers" OFF)
option (OPENSSL_MSVC "Use MSVC build openssl (only for Windows)" OFF)

#if (DO_NOT_USE_MUTEX OR HAIKU OR APPLE)
#  add_definitions ( -DDO_NOT_USE_MUTEX )
#endif ()

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
  
set(Boost_USE_STATIC_LIBS OFF) 
set(Boost_USE_MULTITHREADED ON)  
set(Boost_USE_STATIC_RUNTIME OFF) 
find_package(Boost 1.54.0 COMPONENTS regex system thread REQUIRED) 

set (Boost_INCLUDE_DIR)

if (LIB_INSTALL_DIR)
  set (LIBDIR ${LIB_INSTALL_DIR})
endif (LIB_INSTALL_DIR)

if (NOT LIBDIR)
  set (LIBDIR "lib")
endif (NOT LIBDIR)

message (STATUS "Library directory: ${CMAKE_INSTALL_PREFIX}/${LIBDIR}")

if (INSTALL_RUNTIME_PATH)
    set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${LIBDIR}")
endif (INSTALL_RUNTIME_PATH)

if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE "RelWithDebInfo")
endif (NOT CMAKE_BUILD_TYPE)

if (NOT SHARE_DIR)
set (SHARE_DIR "share")
endif (NOT SHARE_DIR)

if (APPLE)
    add_definitions(-DAPPLE)
endif (APPLE)

if (APPLE)
  set (CLIENT_DATA_DIR "${PROJECT_NAME_GLOBAL}.app")
elseif (WIN32)
  set (CLIENT_DATA_DIR "${SHARE_DIR}")
else (APPLE)
  set (CLIENT_DATA_DIR "${CMAKE_INSTALL_PREFIX}/${SHARE_DIR}/${PROJECT_NAME_GLOBAL}")
endif (APPLE)

message (STATUS "Data directory: ${CLIENT_DATA_DIR}")

#if (AIRDCPP_REVISION)
#    set (airdcpp_WC_REVISION ${AIRDCPP_REVISION})
#    message (STATUS "AIRDCPP_REVISION is set to: ${airdcpp_WC_REVISION}")
#elseif (GIT_FOUND)
    execute_process (COMMAND sh generate_version.sh ./airdcpp-core/airdcpp/version.inc WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
#elseif (NOT AIRDCPP_REVISION)
#    set (airdcpp_WC_REVISION "${VERSION_SUFFIX}")
#endif ()

message (STATUS "Downloading Web UI")
execute_process (COMMAND sh install_webui.sh latest
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

execute_process (COMMAND nodejs generate_stringdefs.js airdcpp-core/airdcpp/
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})


#configure_file(${PROJECT_SOURCE_DIR}/Version.h.in ${PROJECT_BINARY_DIR}/VersionGlobal.h)

if (UNIX)
add_definitions (-D_GNU_SOURCE)
find_program (GZIP_CMD gzip)
mark_as_advanced(GZIP_CMD)
endif (UNIX)

#set (CLIENT_ICONS_DEST "${CLIENT_DATA_DIR}/icons")

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

include_directories(${airdcpp_BINARY_DIR}
   ${CMAKE_CURRENT_SOURCE_DIR}/airdcpp-core
   ${CMAKE_CURRENT_SOURCE_DIR}/airdcpp-webapi
   ${OPENSSL_INCLUDE_DIR}
   )

if (WIN32 OR HAIKU)
    set (BINDIR .)
elseif (UNIX)
    set (BINDIR bin)
    if (APPLE)
        set (BUNDLEDIR .)
    endif (APPLE)
endif (WIN32 OR HAIKU)

add_subdirectory (airdcpp-core)
add_subdirectory (airdcpp-webapi)
add_subdirectory (airdcppd)

message(STATUS "CMAKE_SYSTEM: ${CMAKE_SYSTEM}")
message(STATUS "CMAKE_SYSTEM_PROCESSOR: ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CMAKE_CXX_COMPILER_ID: ${CMAKE_CXX_COMPILER_ID}")
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
    natpmp (headers): ${LibNatpmp_LIBRARY} (${LibNatpmp_INCLUDE_DIR})
    ")

CONFIGURE_FILE(
  "${PROJECT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
  "${PROJECT_BINARY_DIR}/cmake_uninstall.cmake"
  IMMEDIATE @ONLY)
ADD_CUSTOM_TARGET(uninstall
  "${CMAKE_COMMAND}" -P "${PROJECT_BINARY_DIR}/cmake_uninstall.cmake")


cmake_minimum_required (VERSION 3.16)
project (airdcpp-webclient)

if (APPLE)
  set (PROJECT_NAME_GLOBAL AirDC++ Web Client)
else (APPLE)
  set (PROJECT_NAME_GLOBAL airdcpp-webclient)
endif (APPLE)

set (PROJECT_NAME "AirDC++ Web Client")
set (TAG_APPLICATION "AirDC++w")
set (APPLICATION_ID "airdcpp-web")
set (VERSION "2.13.0") # NOTE: the minor version must match the lastest UI version
set (SOVERSION "2.13.0" )

set (GNUCXX_MINIMUM_VERSION "12.0")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED YES)

if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE "RelWithDebInfo")
endif (NOT CMAKE_BUILD_TYPE)

include (GNUInstallDirs)
include (CheckIncludeFile)
include (CheckIncludeFiles)
include (CheckFunctionExists)
include (FindPkgConfig)
include (CheckCXXSourceCompiles)
include (cmake/CheckAtomic.cmake)


# PRE-CHECKS
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake")

if (CMAKE_COMPILER_IS_GNUCXX)
  if (CMAKE_CXX_COMPILER_VERSION VERSION_LESS GNUCXX_MINIMUM_VERSION)
    message (FATAL_ERROR "GCC version must be at least ${GNUCXX_MINIMUM_VERSION}!")
  endif()
endif()



# OPTIONS
#OPTION(ENABLE_NATPMP "Enable support for the NAT-PMP protocol via libnatpmp"
#  ON)
#OPTION(ENABLE_TBB "Enable support of the TBB library to improve performance"
#  ON)

# 1.x had a different default value
unset(INSTALL_RUNTIME_PATH CACHE)

option (INSTALL_RUNTIME_PATH "Install rpath" ON)
option (STRIP "Strip debugging symbols to a separate file" OFF)
option (INSTALL_WEB_UI "Download and install the Web UI package" ON)
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

  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static")

  set(CMAKE_EXE_LINK_DYNAMIC_C_FLAGS)
  set(CMAKE_EXE_LINK_DYNAMIC_CXX_FLAGS)
  set(CMAKE_SHARED_LIBRARY_C_FLAGS)
  set(CMAKE_SHARED_LIBRARY_CXX_FLAGS)
  set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)
  set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)

  # Boost UUID uses getrandom() kernel call, which is not supported on older platforms (which can't be detected by Boost with static builds)
  # https://github.com/boostorg/uuid/issues/91
  add_definitions ( -DBOOST_UUID_RANDOM_PROVIDER_FORCE_POSIX )
endif()


## DEPENDENCIES
find_package (PkgConfig)


# PATHS
if (NOT LIBDIR)
  set (LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
endif (NOT LIBDIR)
message (STATUS "Library directory: ${LIBDIR}")

if (INSTALL_RUNTIME_PATH AND BUILD_SHARED_LIBS)
  set (CMAKE_INSTALL_RPATH "${LIBDIR}")
endif ()

if (NOT RESOURCE_DIRECTORY)
  set (RESOURCE_DIRECTORY "${CMAKE_INSTALL_FULL_DATAROOTDIR}/airdcpp/")
endif (NOT RESOURCE_DIRECTORY)
message (STATUS "Resource directory: ${RESOURCE_DIRECTORY}")

if (NOT GLOBAL_CONFIG_DIRECTORY)
  set (GLOBAL_CONFIG_DIRECTORY "${CMAKE_INSTALL_FULL_SYSCONFDIR}/airdcpp/")
endif (NOT GLOBAL_CONFIG_DIRECTORY)
message (STATUS "Startup config directory: ${GLOBAL_CONFIG_DIRECTORY}")


if (WIN32 OR HAIKU)
    set (BINDIR .)
elseif (UNIX)
    set (BINDIR bin)
    if (APPLE)
        set (BUNDLEDIR .)
    endif (APPLE)
endif (WIN32 OR HAIKU)


# COMPILING DEFINITIONS

if (APPLE)
    add_definitions(-DAPPLE)
endif (APPLE)

# Support files > 2 GB
add_definitions (-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64)


# Flags
if (CMAKE_CXX_COMPILER_ID STREQUAL GNU OR CMAKE_CXX_COMPILER_ID MATCHES Clang)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pipe")
    #if (USE_GOLD)
    #    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fuse-linker-plugin")
    #    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fuse-linker-plugin")
    #endif()
    if (NOT APPLE)
        set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--as-needed")
    endif ()
endif ()

if (CMAKE_CXX_COMPILER_ID MATCHES Clang)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexperimental-library")
endif ()

if (CMAKE_CXX_COMPILER_ID STREQUAL GNU)
    # Disable ABI change notification spam
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-psabi")
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


# FILES TO COMPILE

# Remove possible old generated files
execute_process(COMMAND rm airdcpp-core/airdcpp/StringDefs.cpp OUTPUT_QUIET ERROR_QUIET)
execute_process(COMMAND rm airdcpp-core/airdcpp/version.inc OUTPUT_QUIET ERROR_QUIET)

add_subdirectory (airdcpp-core)
add_subdirectory (airdcpp-webapi)
add_subdirectory (airdcppd)


# WEB UI
if (INSTALL_WEB_UI)
  find_program(NPM_EXECUTABLE npm PATHS /sw/bin)
  if(NOT NPM_EXECUTABLE)
    message(FATAL_ERROR "Could not find 'npm' executable. Please install npm")
  endif()

  message(STATUS "Installing Web UI")
  execute_process (COMMAND sh scripts/install_webui.sh ${SOVERSION} scripts/parse_webui_version.sh
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

project (
	"airdcpp"
	DESCRIPTION "AirDC++ Core"
)
cmake_minimum_required (VERSION 3.16)

if (POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW) # The FindBoost module is removed.
endif()


# Options
OPTION(ENABLE_NATPMP "Enable support for the NAT-PMP protocol via libnatpmp" ON)

if (WIN32)
  OPTION(BUILD_CORE_MODULES "Build optional core modules" ON)
  OPTION(ENABLE_TBB "Enable support of the TBB library to improve performance" OFF) # MSVC provides the same features
else ()
  OPTION(BUILD_CORE_MODULES "Build optional core modules" OFF)
  OPTION(ENABLE_TBB "Enable support of the TBB library to improve performance" ON)
endif ()


# Get files
file (GLOB_RECURSE airdcpp_hdrs ${PROJECT_SOURCE_DIR}/airdcpp/*.h)
file (GLOB_RECURSE airdcpp_srcs ${PROJECT_SOURCE_DIR}/airdcpp/*.cpp ${PROJECT_SOURCE_DIR}/*.c)

# Project file generation
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${airdcpp_srcs})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${airdcpp_hdrs})


# Remove non-wanted files
if (NOT BUILD_CORE_MODULES)
  list(FILTER airdcpp_srcs EXCLUDE REGEX "${PROJECT_SOURCE_DIR}/airdcpp/modules/.*" )
  list(FILTER airdcpp_hdrs EXCLUDE REGEX "${PROJECT_SOURCE_DIR}/airdcpp/modules/.*" )
endif ()

if (NOT WIN32)
  list (REMOVE_ITEM airdcpp_hdrs ${PROJECT_SOURCE_DIR}/airdcpp/core/io/compress/ZipFile.h)
  list (REMOVE_ITEM airdcpp_srcs ${PROJECT_SOURCE_DIR}/airdcpp/core/io/compress/ZipFile.cpp)
  list (REMOVE_ITEM airdcpp_srcs ${PROJECT_SOURCE_DIR}/airdcpp/connectivity/mappers/Mapper_WinUPnP.cpp)
  
  list (REMOVE_ITEM airdcpp_srcs ${PROJECT_SOURCE_DIR}/airdcpp/core/update/UpdateDownloader.cpp)
  list(FILTER airdcpp_srcs EXCLUDE REGEX "${PROJECT_SOURCE_DIR}/airdcpp/core/update/updater/.*" )
endif ()

# Library
add_library (${PROJECT_NAME} ${airdcpp_srcs} ${airdcpp_hdrs})

# Dependencies
find_package (BZip2 REQUIRED)
find_package (ZLIB REQUIRED)
find_package (OpenSSL REQUIRED)
find_package (miniupnpc REQUIRED)
find_package (leveldb REQUIRED)
find_package (maxminddb REQUIRED)
find_package (Boost 1.67.0 COMPONENTS regex system thread REQUIRED)

if (NOT BUILD_SHARED_LIBS)
  # Shared LevelDB libraries are linked against Snappy already
  # We need to link against Snappy manually when making portable builds
  # (old hash databases wouldn't work otherwise)
  find_package (Snappy REQUIRED)
  
  if (SNAPPY_FOUND)
    list (APPEND airdcpp_extra_libs Snappy::snappy)
  endif ()
endif ()

if (NOT WIN32)
  find_package (Threads REQUIRED)
  find_package (Iconv REQUIRED)
    
  if (NOT CMAKE_CROSSCOMPILING)
	list (APPEND airdcpp_extra_libs pthread)
    list (APPEND airdcpp_extra_libs Iconv::Iconv)
  endif (NOT CMAKE_CROSSCOMPILING)
else ()
  find_package (unofficial-minizip REQUIRED)
  list (APPEND airdcpp_extra_libs unofficial::minizip::minizip)
endif()


# OPTIONAL LIBRARIES
if(ENABLE_TBB)
  find_package (TBB)
  if (TBB_FOUND)
    add_definitions ( -DHAVE_INTEL_TBB )
    list (APPEND airdcpp_extra_libs TBB::tbb)
  endif (TBB_FOUND)
endif()

if(ENABLE_NATPMP)
  find_package (natpmp)
endif()

if (natpmp_FOUND)
  set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/connectivity/MappingManager.cpp PROPERTY COMPILE_DEFINITIONS HAVE_NATPMP_H)

  list (APPEND airdcpp_extra_libs natpmp)
else ()
  list (REMOVE_ITEM airdcpp_srcs ${PROJECT_SOURCE_DIR}/airdcpp/connectivity/mappers/Mapper_NATPMP.cpp)
endif ()


# PYTHON SCRIPTS

find_program(PYTHON_EXECUTABLE NAMES python3 python PATHS /sw/bin)
if(NOT PYTHON_EXECUTABLE)
  message(FATAL_ERROR "Could not find 'python' executable. Please install python")
endif(NOT PYTHON_EXECUTABLE)

# Version information for the client
find_package (Git)

execute_process (COMMAND ${PYTHON_EXECUTABLE} scripts/generate_version.py ./airdcpp/core/version.inc ${VERSION} ${TAG_APPLICATION} ${APPLICATION_ID}
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            RESULT_VARIABLE SCRIPT_RESULT)

if(NOT "${SCRIPT_RESULT}" STREQUAL "0")
    message(FATAL_ERROR "Failed to generate version file")
endif()

# Stringdefs
execute_process (COMMAND ${PYTHON_EXECUTABLE} scripts/generate_stringdefs.py ./airdcpp/core/localization/
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            RESULT_VARIABLE SCRIPT_RESULT)

if(NOT "${SCRIPT_RESULT}" STREQUAL "0")
    message(FATAL_ERROR "Failed to generate string definitions")
endif()

# GENERIC COMPILING PROPERTIES
if (CMAKE_BUILD_TYPE STREQUAL Debug)
  add_definitions(-D_DEBUG)
endif()

set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/StringDefs.cpp APPEND_STRING PROPERTY COMPILE_FLAGS " -fno-var-tracking ")

if (HAVE_IFADDRS_H)
  set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/util/AirUtil.cpp PROPERTY COMPILE_DEFINITIONS HAVE_IFADDRS_H APPEND)
endif (HAVE_IFADDRS_H)

if (HAVE_MNTENT_H)
  set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/core/io/File.cpp PROPERTY COMPILE_DEFINITIONS HAVE_MNTENT_H APPEND)
endif (HAVE_MNTENT_H)

if (HAVE_POSIX_FADVISE)
  add_definitions (-DHAVE_POSIX_FADVISE)
endif (HAVE_POSIX_FADVISE)

if (WIN32)
  target_compile_definitions(${PROJECT_NAME} PRIVATE "_UNICODE" "UNICODE")
else()
  target_compile_definitions(${PROJECT_NAME} PRIVATE "NO_CLIENT_UPDATER")

  set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/util/AppUtil.cpp APPEND PROPERTY COMPILE_DEFINITIONS RESOURCE_DIRECTORY="${RESOURCE_DIRECTORY}")
  set_property(SOURCE ${PROJECT_SOURCE_DIR}/airdcpp/util/AppUtil.cpp APPEND PROPERTY COMPILE_DEFINITIONS GLOBAL_CONFIG_DIRECTORY="${GLOBAL_CONFIG_DIRECTORY}")
endif()


target_sources(${PROJECT_NAME}
    PRIVATE
        ${airdcpp_srcs}
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/airdcpp
    PUBLIC
        # where top-level project will look for the library's public headers
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        # where external projects will look for the library's public headers
        #$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)


# LINKING

target_link_libraries (${PROJECT_NAME} 
	BZip2::BZip2
	ZLIB::ZLIB
	OpenSSL::SSL
	OpenSSL::Crypto
	miniupnpc::miniupnpc
	leveldb::leveldb
	maxminddb::maxminddb
	
	Boost::thread
	Boost::system
	Boost::regex

	${airdcpp_extra_libs} 
)

set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${VERSION} OUTPUT_NAME "airdcpp")

target_precompile_headers(${PROJECT_NAME} PUBLIC
  "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/airdcpp/stdinc.h>"
)


# INSTALLATION
if (APPLE)
  set (LIBDIR1 .)
  set (LIBDIR ${PROJECT_NAME_GLOBAL}.app/Contents/MacOS)
endif(APPLE)

if (BUILD_SHARED_LIBS)
  install (TARGETS ${PROJECT_NAME}
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

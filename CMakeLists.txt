project(airdcpp-webapi)
cmake_minimum_required(VERSION 2.6)
# ######### General setup ##########

file (GLOB webapi_hdrs ${PROJECT_SOURCE_DIR}/*.h)
file(GLOB_RECURSE webapi_srcs ${PROJECT_SOURCE_DIR}/*.cpp ${PROJECT_SOURCE_DIR}/*.c)

set (WEBAPI_SRCS ${webapi_srcs} PARENT_SCOPE)
set (WEBAPI_HDRS ${webapi_hdrs} PARENT_SCOPE)

include_directories(AIRDCPP_HDRS ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR} ${Boost_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIRS})

include_directories(${PROJECT_SOURCE_DIR}/json)
include_directories(${WEBSOCKETPP_INCLUDE_DIR})

add_library (airdcpp-webapi ${webapi_srcs})

if (CMAKE_BUILD_TYPE STREQUAL Debug)
  add_definitions(-D_DEBUG)
endif()


target_link_libraries (airdcpp-webapi airdcpp ${OPENSSL_LIBRARIES} ${Boost_LIBRARIES})
set_target_properties(airdcpp-webapi PROPERTIES VERSION ${SOVERSION} OUTPUT_NAME "airdcpp-webapi")

set_target_properties(airdcpp-webapi PROPERTIES COTIRE_CXX_PREFIX_HEADER_INIT "stdinc.h")
if (COMMAND cotire)
  cotire(airdcpp-webapi)
endif()

if (APPLE)
  set (LIBDIR1 .)
  set (LIBDIR ${PROJECT_NAME_GLOBAL}.app/Contents/MacOS)
endif(APPLE)


if (BUILD_SHARED_LIBS)
  install (TARGETS airdcpp-webapi
            LIBRARY DESTINATION "${LIBDIR}" NAMELINK_SKIP
            BUNDLE DESTINATION ${LIBDIR1})
endif ()

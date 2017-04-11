project(airdcpp-webapi)
cmake_minimum_required(VERSION 2.6)
# ######### General setup ##########

aux_source_directory(${PROJECT_SOURCE_DIR}/web-server webapi_srcs)
file (GLOB webapi_hdrs ${PROJECT_SOURCE_DIR}/web-server/*.h)

file (GLOB_RECURSE webapi_hdrs ${PROJECT_SOURCE_DIR}/api *.h)
file (GLOB_RECURSE webapi_srcs ${PROJECT_SOURCE_DIR}/api *.cpp)

set (WEBAPI_SRCS ${webapi_srcs} PARENT_SCOPE)
set (WEBAPI_HDRS ${webapi_hdrs} PARENT_SCOPE)

include_directories(AIRDCPP_HDRS ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR} ${Boost_INCLUDE_DIRS} ${OPENSSL_INCLUDE_DIRS})

include_directories(${PROJECT_SOURCE_DIR}/json)
include_directories(${WEBSOCKETPP_INCLUDE_DIR})

add_definitions(-DSSL_OP_NO_COMPRESSION)

add_library (airdcpp-webapi ${webapi_srcs})

if (CMAKE_BUILD_TYPE STREQUAL Debug)
  add_definitions(-D_DEBUG)
endif()


target_link_libraries (airdcpp-webapi airdcpp ${OPENSSL_LIBRARIES} ${Boost_LIBRARIES})
set_target_properties(airdcpp-webapi PROPERTIES VERSION ${SOVERSION} OUTPUT_NAME "airdcpp-webapi")


if (APPLE)
  set (LIBDIR1 .)
  set (LIBDIR ${PROJECT_NAME_GLOBAL}.app/Contents/MacOS)
endif(APPLE)


if (BUILD_SHARED_LIBS)
  install (TARGETS airdcpp-webapi
            LIBRARY DESTINATION "${LIBDIR}" NAMELINK_SKIP
            BUNDLE DESTINATION ${LIBDIR1})
endif ()

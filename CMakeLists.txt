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

if (NOT LINK)
  set (LINK SHARED)
endif (NOT LINK)

#if (APPLE)
#  set (APPLE_LIBS "iconv")
#endif (APPLE)

add_library (airdcpp-webapi ${LINK} ${webapi_srcs})


#list(APPEND LIBS
#        ${Boost_LIBRARIES}
#        ${ICONV_LIBRARIES}
#        )

target_link_libraries (${PROJECT_NAME} airdcpp ${LIBS})

if (CMAKE_BUILD_TYPE STREQUAL Debug)
    add_definitions(-D_DEBUG)
endif()

target_link_libraries (airdcpp-webapi airdcpp ${OPENSSL_LIBRARIES} ${Boost_LIBRARIES})
set_target_properties(airdcpp-webapi PROPERTIES VERSION ${SOVERSION} OUTPUT_NAME "airdcpp-webapi")

if (APPLE)
  set (LIBDIR1 .)
  set (LIBDIR ${PROJECT_NAME_GLOBAL}.app/Contents/MacOS)
endif(APPLE)

if (LINK STREQUAL SHARED)
install (TARGETS airdcpp-webapi
            LIBRARY DESTINATION "${LIBDIR}" NAMELINK_SKIP
            BUNDLE DESTINATION ${LIBDIR1})
endif (LINK STREQUAL SHARED)

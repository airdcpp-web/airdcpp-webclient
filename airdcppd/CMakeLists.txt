project(airdcppd)
cmake_minimum_required(VERSION 2.6)

aux_source_directory(${PROJECT_SOURCE_DIR} airdcppd_SRCS)

if (Backtrace_FOUND)
    set_property(SOURCE ${PROJECT_SOURCE_DIR}/main.cpp ${PROJECT_SOURCE_DIR}/stacktrace.cpp PROPERTY COMPILE_DEFINITIONS USE_STACKTRACE)
	message (STATUS "Stacktrace enabled")
endif (Backtrace_FOUND)

# actual target:
add_executable (${PROJECT_NAME} MACOSX_BUNDLE WIN32
                ${airdcppd_SRCS}
                )

#list(APPEND LIBS
#        ${XMLRPCLIBS}
#        ${JSONRPCLIBS}
#        ${Boost_LIBRARIES}
#        ${ICONV_LIBRARIES}
#        )

target_link_libraries (${PROJECT_NAME} airdcpp airdcpp-webapi ${LIBS})

if (CMAKE_BUILD_TYPE STREQUAL Debug)
    add_definitions(-D_DEBUG)
endif()

install (TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ../${BINDIR}
    BUNDLE DESTINATION ../${BUNDLEDIR})

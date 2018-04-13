project(airdcppd)
cmake_minimum_required(VERSION 2.6)

aux_source_directory(${PROJECT_SOURCE_DIR} airdcppd_SRCS)
include_directories(${Boost_INCLUDE_DIRS})

if (Backtrace_FOUND)
  set_property(SOURCE ${PROJECT_SOURCE_DIR}/main.cpp PROPERTY COMPILE_DEFINITIONS USE_STACKTRACE)
  message (STATUS "Stacktrace enabled")

  find_program(ADDR2LINE_EXECUTABLE addr2line)
  if(ADDR2LINE_EXECUTABLE)
    set_property(SOURCE ${PROJECT_SOURCE_DIR}/stacktrace.cpp PROPERTY COMPILE_DEFINITIONS USE_ADDR2LINE)
    message (STATUS "addr2line enabled")
  endif(ADDR2LINE_EXECUTABLE)
else (Backtrace_FOUND)
  list (REMOVE_ITEM airdcppd_SRCS ${PROJECT_SOURCE_DIR}/stacktrace.cpp)
endif (Backtrace_FOUND)

# actual target:
add_executable (${PROJECT_NAME} MACOSX_BUNDLE WIN32
                ${airdcppd_SRCS}
                )

target_link_libraries (${PROJECT_NAME} ${LIBS} airdcpp airdcpp-webapi ${Backtrace_LIBRARIES})


if (CMAKE_BUILD_TYPE STREQUAL Debug)
    add_definitions(-D_DEBUG)
endif()

if (STRIP)
  find_program(OBJCOPY_CMD objcopy PATHS /sw/bin)
  if (OBJCOPY_CMD)
    message(STATUS "OBJCOPY_CMD: ${OBJCOPY_CMD}")
    ADD_CUSTOM_COMMAND(
        TARGET airdcppd
        POST_BUILD
        COMMAND sh ../scripts/strip-symbols.sh ${PROJECT_SOURCE_DIR}/airdcppd ${OBJCOPY_CMD}
    )
  else (OBJCOPY_CMD)
    message (FATAL_ERROR "objcopy was not found, can not strip the binary")
  endif()
endif()

set_property (TARGET ${PROJECT_NAME} PROPERTY OUTPUT_NAME ${PROJECT_NAME})
install (TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${BINDIR}
    BUNDLE DESTINATION ${BUNDLEDIR})

#if (APPLE)
#  install (FILES ${PROJECT_NAME}.desktop DESTINATION ${PROJECT_NAME_GLOBAL}.app/applications)
#else (APPLE)
#  if (UNIX)
#    install (FILES ${PROJECT_NAME}.desktop DESTINATION ${SHARE_DIR}/applications)
#  endif (UNIX)
#endif (APPLE)


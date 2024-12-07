project(airdcppd)
cmake_minimum_required(VERSION 3.16)


# Sources
file (GLOB_RECURSE airdcppd_hdrs ${PROJECT_SOURCE_DIR}/*.h)
aux_source_directory(${PROJECT_SOURCE_DIR} airdcppd_SRCS)


# Backtrace
find_package (Backtrace)

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

# Target
add_executable (${PROJECT_NAME} MACOSX_BUNDLE WIN32
                 ${airdcppd_SRCS} ${airdcppd_hdrs}
               )

target_link_libraries (${PROJECT_NAME} airdcpp airdcpp-webapi)

if (Backtrace_FOUND)
  target_link_libraries (${PROJECT_NAME} ${Backtrace_LIBRARIES})
endif (Backtrace_FOUND)

if (CMAKE_BUILD_TYPE STREQUAL Debug)
    add_definitions(-D_DEBUG)
endif()

if (STRIP)
  if (NOT CMAKE_OBJCOPY)
    message (FATAL_ERROR "CMAKE_OBJCOPY is not set")
  endif()

  message(STATUS "CMAKE_OBJCOPY: ${CMAKE_OBJCOPY}")
  ADD_CUSTOM_COMMAND(
      TARGET airdcppd
      POST_BUILD
      COMMAND sh ../scripts/strip-symbols.sh ${PROJECT_SOURCE_DIR}/airdcppd ${CMAKE_OBJCOPY}
  )
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


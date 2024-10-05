project(
	"airdcpp-webapi"
	DESCRIPTION "AirDC++ Web API"
)
cmake_minimum_required(VERSION 3.16)
#cmake_policy(SET CMP0167 NEW) # The FindBoost module is removed.

# ######### General setup ##########


find_package (websocketpp REQUIRED)
find_package (nlohmann_json 3.0.0 REQUIRED)


file (GLOB_RECURSE webapi_hdrs ${PROJECT_SOURCE_DIR}/*.h)
file (GLOB_RECURSE webapi_srcs ${PROJECT_SOURCE_DIR}/*.cpp ${PROJECT_SOURCE_DIR}/*.c)

add_library (${PROJECT_NAME} ${webapi_srcs} ${webapi_hdrs} )

# Project file generation
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${webapi_srcs})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${webapi_hdrs})


target_include_directories(${PROJECT_NAME}
    #PUBLIC
    #    ${CMAKE_CURRENT_SOURCE_DIR}
    PUBLIC
        # where top-level project will look for the library's public headers
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        # where external projects will look for the library's public headers
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if (CMAKE_BUILD_TYPE STREQUAL Debug)
  add_definitions(-D_DEBUG)
endif()

if (WIN32)
	target_compile_definitions(${PROJECT_NAME}
		PRIVATE
			"_UNICODE"
			"UNICODE"
	)
		
	target_link_libraries (${PROJECT_NAME} 
		bcrypt # for Boost::uuid
		# ${OPENSSL_LIBRARIES} 
		# ${Boost_LIBRARIES}
	)

	# C1128	number of sections exceeded object file format limit: compile with /bigobj (WebServerManager.cpp)
	target_compile_options(${PROJECT_NAME} PRIVATE "/bigobj")
endif()


target_link_libraries (${PROJECT_NAME} 
	airdcpp 
)
set_target_properties(${PROJECT_NAME} PROPERTIES VERSION ${VERSION} OUTPUT_NAME "airdcpp-webapi")

target_precompile_headers(${PROJECT_NAME} PUBLIC
  "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/stdinc.h>"
  "$<$<COMPILE_LANGUAGE:CXX>:<airdcpp/stdinc.h$<ANGLE-R>>"
)

if (APPLE)
  set (LIBDIR1 .)
  set (LIBDIR ${PROJECT_NAME_GLOBAL}.app/Contents/MacOS)
endif(APPLE)


if (BUILD_SHARED_LIBS)
  install (TARGETS ${PROJECT_NAME}
            LIBRARY DESTINATION "${LIBDIR}" NAMELINK_SKIP
            BUNDLE DESTINATION ${LIBDIR1})
endif ()

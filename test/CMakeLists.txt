cmake_minimum_required(VERSION 3.16)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(airdcpp-webclient-tests LANGUAGES CXX)
  enable_testing()
endif()

add_executable(npm_version_test NpmVersionTest.cpp)
target_compile_features(npm_version_test PRIVATE cxx_std_20)
target_include_directories(npm_version_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../airdcpp-webapi")

if(TARGET nlohmann_json::nlohmann_json)
  target_link_libraries(npm_version_test PRIVATE nlohmann_json::nlohmann_json)
else()
  # Allow focused tests without the daemon's networking/database dependencies.
  find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp)
  if(NOT NLOHMANN_JSON_INCLUDE_DIR)
    message(FATAL_ERROR "nlohmann/json.hpp is required; set NLOHMANN_JSON_INCLUDE_DIR")
  endif()
  target_include_directories(npm_version_test PRIVATE "${NLOHMANN_JSON_INCLUDE_DIR}")
endif()

add_test(NAME npm_version_selection COMMAND npm_version_test)

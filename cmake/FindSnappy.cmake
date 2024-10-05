#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# Tries to find Snappy headers and libraries.
#
# Usage of this module as follows:
#
#  find_package(Snappy)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  SNAPPY_ROOT_DIR  Set this variable to the root installation of
#                    Snappy if the module has problems finding
#                    the proper installation path.
#
# Variables defined by this module:
#
#  SNAPPY_FOUND              System has Snappy libs/headers
#  SNAPPY_LIBRARIES          The Snappy libraries
#  SNAPPY_INCLUDE_DIRS       The location of Snappy headers

find_path(SNAPPY_INCLUDE_DIRS
    NAMES snappy.h
    HINTS ${SNAPPY_ROOT_DIR}/include)

find_library(SNAPPY_LIBRARIES
    NAMES snappy
    HINTS ${SNAPPY_ROOT_DIR}/lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Snappy DEFAULT_MSG
    SNAPPY_LIBRARIES
    SNAPPY_INCLUDE_DIRS)

mark_as_advanced(
    SNAPPY_ROOT_DIR
    SNAPPY_LIBRARIES
    SNAPPY_INCLUDE_DIRS)

if(SNAPPY_FOUND)
    message(STATUS "Found Snappy  (include: ${SNAPPY_INCLUDE_DIR}, library: ${SNAPPY_LIBRARIES})")
    if(NOT TARGET Snappy::snappy)
      add_library(Snappy::snappy UNKNOWN IMPORTED)
      set_target_properties(Snappy::snappy PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${SNAPPY_INCLUDE_DIRS}")

      set_property(TARGET Snappy::snappy APPEND PROPERTY
        IMPORTED_LOCATION "${SNAPPY_LIBRARIES}")
    endif()
endif()

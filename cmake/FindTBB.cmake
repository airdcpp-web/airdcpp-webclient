# SPDX-FileCopyrightInfo: Copyright © DUNE Project contributors, see file LICENSE.md in module root
# SPDX-License-Identifier: LicenseRef-GPL-2.0-only-with-DUNE-exception

#[=======================================================================[.rst:
FindTBB
-------

Finds the Threading Building Blocks (TBB) library.

This is a fallback implementation in case the TBB library does not provide
itself a corresponding TBBConfig.cmake file.

Imported Targets
^^^^^^^^^^^^^^^^

This module provides the following imported targets, if found:

``TBB::tbb``
  Imported library to link against if TBB should be used.

Result Variables
^^^^^^^^^^^^^^^^

This will define the following variables:

``TBB_FOUND``
  True if the TBB library was found.

Finding the TBB library
^^^^^^^^^^^^^^^^^^^^^^^

Two strategies are implemented for finding the TBB library:

1. Searching for the TBB cmake config file, typically named
   ``TBBConfig.cmake``. In recent TBB versions, this file can be
   created using a script provided by TBB itself. Simply set the
   variable ``TBB_DIR`` to the directory containing the config file
   in order to find TBB.

2. Using pkg-config to configure TBB. Therefore it is necessary
   to find the ``tbb.pc`` file. Several distributions provide this file
   directly. In order to point pkg-config to the location of that file,
   simply set the environmental variable ``PKG_CONFIG_PATH`` to include
   the directory containing the .pc file, or add this path to the
   ``CMAKE_PREFIX_PATH``.

#]=======================================================================]


# text for feature summary
include(FeatureSummary)
set_package_properties("TBB" PROPERTIES
  DESCRIPTION "Intel's Threading Building Blocks"
  URL "https://github.com/oneapi-src/oneTBB"
)

# first, try to find TBBs cmake configuration
find_package(TBB ${TBB_FIND_VERSION} QUIET CONFIG)
if(TBB_FOUND AND TARGET TBB::tbb)
  message(STATUS "Found TBB: using configuration from TBB_DIR=${TBB_DIR} (found version \"${TBB_VERSION}\")")
  return()
endif()

# Add a backport of cmakes FindPkgConfig module
if(${CMAKE_VERSION} VERSION_LESS "3.19.4")
  list(INSERT CMAKE_MODULE_PATH 0 "${CMAKE_CURRENT_LIST_DIR}/FindPkgConfig")
endif()

# second, try to find TBBs pkg-config file
find_package(PkgConfig)
if(PkgConfig_FOUND)
  if(TBB_FIND_VERSION)
    pkg_check_modules(PkgConfigTBB tbb>=${TBB_FIND_VERSION} QUIET IMPORTED_TARGET GLOBAL)
  else()
    pkg_check_modules(PkgConfigTBB tbb QUIET IMPORTED_TARGET GLOBAL)
  endif()
endif()

# check whether the static library was found
if(PkgConfigTBB_STATIC_FOUND)
  set(_tbb PkgConfigTBB_STATIC)
else()
  set(_tbb PkgConfigTBB)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args("TBB"
  REQUIRED_VARS
    ${_tbb}_LINK_LIBRARIES ${_tbb}_FOUND PkgConfig_FOUND
  VERSION_VAR
    ${_tbb}_VERSION
  FAIL_MESSAGE "Could NOT find TBB (set TBB_DIR to path containing TBBConfig.cmake or set PKG_CONFIG_PATH to include the location of the tbb.pc file)"
)

if(${_tbb}_FOUND AND NOT TARGET TBB::tbb)
  add_library(TBB::tbb ALIAS PkgConfig::PkgConfigTBB)
endif()


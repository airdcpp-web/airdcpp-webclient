#find_path(MINIUPNP_INCLUDE_DIR miniupnpc/miniupnpc.h
#   PATH_SUFFIXES miniupnpc)
#find_library(MINIUPNP_LIBRARY miniupnpc)

#INCLUDE(FindPackageHandleStandardArgs)
#FIND_PACKAGE_HANDLE_STANDARD_ARGS(Miniupnpc DEFAULT_MSG MINIUPNP_LIBRARY MINIUPNP_INCLUDE_DIR)

#MARK_AS_ADVANCED(
#  MINIUPNP_INCLUDE_DIR
#  MINIUPNP_LIBRARY
#)

if(UNIX)
    find_package(PkgConfig QUIET)
    pkg_check_modules(_MINIUPNPC QUIET libminiupnpc)
endif()

find_path(MINIUPNPC_INCLUDE_DIR
    NAMES miniupnpc/miniupnpc.h
    HINTS ${_MINIUPNPC_INCLUDEDIR})
find_library(MINIUPNPC_LIBRARY
    NAMES
        miniupnpc
        libminiupnpc
    HINTS ${_MINIUPNPC_LIBDIR})

#set(MINIUPNPC_INCLUDE_DIRS ${MINIUPNPC_INCLUDE_DIR})
#set(MINIUPNPC_LIBRARIES ${MINIUPNPC_LIBRARY})

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(miniupnpc
    REQUIRED_VARS
        MINIUPNPC_LIBRARY
        MINIUPNPC_INCLUDE_DIR)

mark_as_advanced(MINIUPNPC_INCLUDE_DIR MINIUPNPC_LIBRARY)

if(MINIUPNPC_FOUND)
    message(STATUS "Found miniupnpc  (include: ${MINIUPNPC_INCLUDE_DIR}, library: ${MINIUPNPC_LIBRARY})")
    if(NOT TARGET miniupnpc::miniupnpc)
      add_library(miniupnpc::miniupnpc UNKNOWN IMPORTED)
      set_target_properties(miniupnpc::miniupnpc PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${MINIUPNPC_INCLUDE_DIR}")

        set_property(TARGET miniupnpc::miniupnpc APPEND PROPERTY
          IMPORTED_LOCATION "${MINIUPNPC_LIBRARY}")
    endif()
endif()

# Find libnatpmp.a - port mapper

find_path(LibNatpmp_INCLUDE_DIR NAMES natpmp.h)
#find_library(LibNatpmp_LIBRARY NAMES libnatpmp.a libnatpmp.lib)
find_library(LibNatpmp_LIBRARY natpmp)

if(LibNatpmp_INCLUDE_DIR AND LibNatpmp_LIBRARY)
  set(LibNatpmp_FOUND TRUE)
endif(LibNatpmp_INCLUDE_DIR AND LibNatpmp_LIBRARY)

if(LibNatpmp_FOUND)
  if(NOT LibNatpmp_FIND_QUIETLY)
    message(STATUS "Found libnatpmp: ${LibNatpmp_LIBRARY}")
  endif(NOT LibNatpmp_FIND_QUIETLY)
else(LibNatpmp_FOUND)
  if(LibNatpmp_FIND_REQUIRED)
    message(FATAL_ERROR "Could not find natpmp library.")
  endif(LibNatpmp_FIND_REQUIRED)
endif(LibNatpmp_FOUND)


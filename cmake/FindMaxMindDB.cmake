# - Try to find MaxMindDB headers and libraries
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  LIBMAXMINDDB_ROOT_DIR     Set this variable to the root installation of
#                            libmaxminddb if the module has problems finding the
#                            proper installation path.
#
# Variables defined by this module:
#
#  LIBMAXMINDDB_FOUND                   System has GeoIP libraries and headers
#  LIBMAXMINDDB_LIBRARY                 The GeoIP library
#  LIBMAXMINDDB_INCLUDE_DIR             The location of GeoIP headers

find_path(LIBMAXMINDDB_INCLUDE_DIR
    NAMES maxminddb.h
    HINTS ${LIBMAXMINDDB_ROOT_DIR}/include
)

# We are just going to prefer static linking for this plugin.
set(libmaxminddb_names maxminddb libmaxminddb.a )

find_library(LIBMAXMINDDB_LIBRARY
    NAMES ${libmaxminddb_names}
    HINTS ${LIBMAXMINDDB_ROOT_DIR}/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LibMaxMindDB DEFAULT_MSG
    LIBMAXMINDDB_LIBRARY
    LIBMAXMINDDB_INCLUDE_DIR
)

#if (LIBMAXMINDDB_FOUND)
#    include(CheckCXXSourceCompiles)
#    set(CMAKE_REQUIRED_INCLUDES ${LIBMAXMINDDB_INCLUDE_DIR})
#    check_cxx_source_compiles("
#    #include "maxminddb.h"
#    int main() { GEOIP_COUNTRY_EDITION_V6; return 0; }
#    " HAVE_GEOIP_COUNTRY_EDITION_V6)
#    check_cxx_source_compiles("
#    #include <GeoIPCity.h>
#    int main() { GEOIP_CITY_EDITION_REV0_V6; return 0; }
#    " HAVE_GEOIP_CITY_EDITION_REV0_V6)
#    set(CMAKE_REQUIRED_INCLUDES)
#endif ()

mark_as_advanced(
    LibMaxMindDB_ROOT_DIR
    LIBMAXMINDDB_LIBRARY
    LIBMAXMINDDB_INCLUDE_DIR
)


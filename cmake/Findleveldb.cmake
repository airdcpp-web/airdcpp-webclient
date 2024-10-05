# Find libleveldb.a - key/value storage system

#find_path(LevelDB_INCLUDE_DIR NAMES leveldb/db.h)
#find_library(LevelDB_LIBRARY NAMES libleveldb.a libleveldb.lib)
#find_library(LevelDB_LIBRARY leveldb)

#if(LevelDB_INCLUDE_DIR AND LevelDB_LIBRARY)
#  set(LevelDB_FOUND TRUE)
#endif(LevelDB_INCLUDE_DIR AND LevelDB_LIBRARY)

#if(LevelDB_FOUND)
#  if(NOT LevelDB_FIND_QUIETLY)
#    message(STATUS "Found LevelDB: ${LevelDB_LIBRARY}")
#  endif(NOT LevelDB_FIND_QUIETLY)
#else(LevelDB_FOUND)
#  if(LevelDB_FIND_REQUIRED)
#    message(FATAL_ERROR "Could not find leveldb library.")
#  endif(LevelDB_FIND_REQUIRED)
#endif(LevelDB_FOUND)


# - Find LevelDB
#
#  LevelDB_INCLUDES  - List of LevelDB includes
#  LevelDB_LIBRARIES - List of libraries when using LevelDB.
#  LevelDB_FOUND     - True if LevelDB found.

# Look for the header file.
find_path(LEVELDB_INCLUDE_DIRS NAMES leveldb/db.h
                          PATHS $ENV{LEVELDB_ROOT}/include /opt/local/include /usr/local/include /usr/include
                          DOC "Path in which the file leveldb/db.h is located." )

# Look for the library.
find_library(LEVELDB_LIBRARIES NAMES leveldb
                             PATHS /usr/lib $ENV{LEVELDB_ROOT}/lib
                             DOC "Path to leveldb library." )

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(leveldb DEFAULT_MSG LEVELDB_INCLUDE_DIRS LEVELDB_LIBRARIES)

if(LEVELDB_FOUND)
  message(STATUS "Found LevelDB (include: ${LEVELDB_INCLUDE_DIRS}, library: ${LEVELDB_LIBRARIES})")
  mark_as_advanced(LEVELDB_INCLUDE_DIRS LEVELDB_LIBRARIES)

  if(EXISTS "${LEVELDB_INCLUDE_DIRS}/leveldb/db.h")
    file(STRINGS "${LEVELDB_INCLUDE_DIRS}/leveldb/db.h" __version_lines
           REGEX "static const int k[^V]+Version[ \t]+=[ \t]+[0-9]+;")

    foreach(__line ${__version_lines})
      if(__line MATCHES "[^k]+kMajorVersion[ \t]+=[ \t]+([0-9]+);")
        set(LEVELDB_VERSION_MAJOR ${CMAKE_MATCH_1})
      elseif(__line MATCHES "[^k]+kMinorVersion[ \t]+=[ \t]+([0-9]+);")
        set(LEVELDB_VERSION_MINOR ${CMAKE_MATCH_1})
      endif()
    endforeach()

    if(LEVELDB_VERSION_MAJOR AND LEVELDB_VERSION_MINOR)
      set(LEVELDB_VERSION "${LEVELDB_VERSION_MAJOR}.${LEVELDB_VERSION_MINOR}")
    endif()

    # caffe_clear_vars(__line __version_lines)

    if(NOT TARGET leveldb::leveldb)
      add_library(leveldb::leveldb UNKNOWN IMPORTED)
      set_target_properties(leveldb::leveldb PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${LEVELDB_INCLUDE_DIRS}")

        set_property(TARGET leveldb::leveldb APPEND PROPERTY
          IMPORTED_LOCATION "${LEVELDB_LIBRARIES}")
    endif()
  endif()
endif()

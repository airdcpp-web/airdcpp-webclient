# Find libleveldb.a - key/value storage system

find_path(LevelDB_INCLUDE_DIR NAMES leveldb/db.h)
#find_library(LevelDB_LIBRARY NAMES libleveldb.a libleveldb.lib)
find_library(LevelDB_LIBRARY leveldb)

if(LevelDB_INCLUDE_DIR AND LevelDB_LIBRARY)
  set(LevelDB_FOUND TRUE)
endif(LevelDB_INCLUDE_DIR AND LevelDB_LIBRARY)

if(LevelDB_FOUND)
  if(NOT LevelDB_FIND_QUIETLY)
    message(STATUS "Found LevelDB: ${LevelDB_LIBRARY}")
  endif(NOT LevelDB_FIND_QUIETLY)
else(LevelDB_FOUND)
  if(LevelDB_FIND_REQUIRED)
    message(FATAL_ERROR "Could not find leveldb library.")
  endif(LevelDB_FIND_REQUIRED)
endif(LevelDB_FOUND)


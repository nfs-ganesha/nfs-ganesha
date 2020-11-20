# - Find RGW
# Find the Linux Trace Toolkit - next generation with associated includes path.
# See http://ceph.org/
#
# This module accepts the following optional variables:
#    RGW_PREFIX   = A hint on RGW install path.
#
# This module defines the following variables:
#    RGW_FOUND       = Was RGW found or not?
#    RGW_LIBRARIES   = The list of libraries to link to when using RGW
#    RGW_INCLUDE_DIR = The path to RGW include directory
#
# On can set RGW_PREFIX before using find_package(RGW) and the
# module with use the PATH as a hint to find RGW.
#
# The hint can be given on the command line too:
#   cmake -DRGW_PREFIX=/DATA/ERIC/RGW /path/to/source

if(RGW_PREFIX)
  message(STATUS "FindRGW: using PATH HINT: ${RGW_PREFIX}")
  # Try to make the prefix override the normal paths
  find_path(RGW_INCLUDE_DIR
    NAMES include/rados/librgw.h
    PATHS ${RGW_PREFIX}
    NO_DEFAULT_PATH
    DOC "The RGW include headers")
    message("RGW_INCLUDE_DIR ${RGW_INCLUDE_DIR}")

  find_path(RGW_LIBRARY_DIR
    NAMES librgw.so
    PATHS ${RGW_PREFIX}
    PATH_SUFFIXES lib/${CMAKE_LIBRARY_ARCHITECTURE} lib lib64
    NO_DEFAULT_PATH
    DOC "The RGW libraries")
endif()

if (NOT RGW_INCLUDE_DIR)
  find_path(RGW_INCLUDE_DIR
    NAMES include/rados/librgw.h
    PATHS ${RGW_PREFIX}
    DOC "The RGW include headers")
endif (NOT RGW_INCLUDE_DIR)

if (NOT RGW_LIBRARY_DIR)
  find_path(RGW_LIBRARY_DIR
    NAMES librgw.so
    PATHS ${RGW_PREFIX}
    PATH_SUFFIXES lib/${CMAKE_LIBRARY_ARCHITECTURE} lib lib64
    DOC "The RGW libraries")
endif (NOT RGW_LIBRARY_DIR)

find_library(RGW_LIBRARY rgw PATHS ${RGW_LIBRARY_DIR} NO_DEFAULT_PATH)
check_library_exists(rgw rgw_mount ${RGW_LIBRARY_DIR} RGWLIB)
if (NOT RGWLIB)
  unset(RGW_LIBRARY_DIR CACHE)
  unset(RGW_INCLUDE_DIR CACHE)
else (NOT RGWLIB)
  check_library_exists(rgw rgw_mount2 ${RGW_LIBRARY_DIR} RGW_MOUNT2)
  if(NOT RGW_MOUNT2)
    message("Cannot find rgw_mount2. Fallback to use rgw_mount")
    set(USE_FSAL_RGW_MOUNT2 OFF)
  else(RGW_MOUNT2)
    set(USE_FSAL_RGW_MOUNT2 ON)
  endif(NOT RGW_MOUNT2)
  check_library_exists(rgw rgw_getxattrs ${RGW_LIBRARY_DIR} RGW_XATTRS)
  if(NOT RGW_XATTRS)
    message("Cannot find xattrs")
    set(USE_FSAL_RGW_XATTRS OFF)
  else(RGW_XATTRS)
    set(USE_FSAL_RGW_XATTRS ON)
  endif(NOT RGW_XATTRS)
endif (NOT RGWLIB)

set(RGW_LIBRARIES ${RGW_LIBRARY})
message(STATUS "Found rgw libraries: ${RGW_LIBRARIES}")

set(RGW_FILE_HEADER "${RGW_INCLUDE_DIR}/include/rados/rgw_file.h")
if (EXISTS ${RGW_FILE_HEADER})
  file(STRINGS ${RGW_FILE_HEADER} RGW_MAJOR REGEX
    "LIBRGW_FILE_VER_MAJOR (\\d*).*$")
  string(REGEX REPLACE ".+LIBRGW_FILE_VER_MAJOR (\\d*)" "\\1" RGW_MAJOR
    "${RGW_MAJOR}")

  file(STRINGS ${RGW_FILE_HEADER} RGW_MINOR REGEX
    "LIBRGW_FILE_VER_MINOR (\\d*).*$")
  string(REGEX REPLACE ".+LIBRGW_FILE_VER_MINOR (\\d*)" "\\1" RGW_MINOR
    "${RGW_MINOR}")

  file(STRINGS ${RGW_FILE_HEADER} RGW_EXTRA REGEX
    "LIBRGW_FILE_VER_EXTRA (\\d*).*$")
  string(REGEX REPLACE ".+LIBRGW_FILE_VER_EXTRA (\\d*)" "\\1" RGW_EXTRA
    "${RGW_EXTRA}")

  set(RGW_FILE_VERSION "${RGW_MAJOR}.${RGW_MINOR}.${RGW_EXTRA}")
else()
  set(RGW_FILE_VERSION "0.0.0")
endif()

# handle the QUIETLY and REQUIRED arguments and set PRELUDE_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(RGW
  REQUIRED_VARS RGW_INCLUDE_DIR RGW_LIBRARY_DIR
  VERSION_VAR RGW_FILE_VERSION
  )
# VERSION FPHSA options not handled by CMake version < 2.8.2)
#                                  VERSION_VAR)

mark_as_advanced(RGW_INCLUDE_DIR)
mark_as_advanced(RGW_LIBRARY_DIR)

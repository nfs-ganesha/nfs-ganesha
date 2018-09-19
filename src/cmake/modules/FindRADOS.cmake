# - Find RADOS
# This module accepts the following optional variables:
#    RADOS_PREFIX   = A hint on RADOS install path.
#
# This module defines the following variables:
#    RADOS_FOUND       = Was RADOS found or not?
#    RADOS_LIBRARIES   = The list of libraries to link to when using RADOS
#    RADOS_INCLUDE_DIR = The path to RADOS include directory
#
# On can set RADOS_PREFIX before using find_package(RADOS) and the
# module with use the PATH as a hint to find RADOS.
#
# The hint can be given on the command line too:
#   cmake -DRADOS_PREFIX=/DATA/ERIC/RADOS /path/to/source

if(RADOS_PREFIX)
  message(STATUS "FindRADOS: using PATH HINT: ${RADOS_PREFIX}")
  # Try to make the prefix override the normal paths
  find_path(RADOS_INCLUDE_DIR
    NAMES rados/librados.h
    PATHS ${RADOS_PREFIX}
    PATH_SUFFIXES include
    NO_DEFAULT_PATH
    DOC "The RADOS include headers")

  find_path(RADOS_LIBRARY_DIR
    NAMES librados.so
    PATHS ${RADOS_PREFIX}
    PATH_SUFFIXES lib/${CMAKE_LIBRARY_ARCHITECTURE} lib lib64
    NO_DEFAULT_PATH
    DOC "The RADOS libraries")
endif(RADOS_PREFIX)


if (NOT RADOS_INCLUDE_DIR)
  find_path(RADOS_INCLUDE_DIR
    NAMES rados/librados.h
    PATHS ${RADOS_PREFIX}
    PATH_SUFFIXES include
    DOC "The RADOS include headers")
endif (NOT RADOS_INCLUDE_DIR)

if (NOT RADOS_LIBRARY_DIR)
  find_path(RADOS_LIBRARY_DIR
    NAMES librados.so
    PATHS ${RADOS_PREFIX}
    PATH_SUFFIXES lib/${CMAKE_LIBRARY_ARCHITECTURE} lib lib64
    DOC "The RADOS libraries")
endif (NOT RADOS_LIBRARY_DIR)

find_library(RADOS_LIBRARY rados PATHS ${RADOS_LIBRARY_DIR} NO_DEFAULT_PATH)
check_library_exists(rados rados_read_op_omap_get_vals2 ${RADOS_LIBRARY_DIR} RADOSLIB)
if (NOT RADOSLIB)
  unset(RADOS_LIBRARY_DIR CACHE)
  unset(RADOS_INCLUDE_DIR CACHE)
endif (NOT RADOSLIB)

set(RADOS_LIBRARIES ${RADOS_LIBRARY})
message(STATUS "Found rados libraries: ${RADOS_LIBRARIES}")

# handle the QUIETLY and REQUIRED arguments and set PRELUDE_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(RADOS
  REQUIRED_VARS RADOS_INCLUDE_DIR RADOS_LIBRARY_DIR
  )
# VERSION FPHSA options not handled by CMake version < 2.8.2)
#                                  VERSION_VAR)

mark_as_advanced(RADOS_INCLUDE_DIR)
mark_as_advanced(RADOS_LIBRARY_DIR)

# - Find NTIRPC
# Find the New TIRPC RPC library
#
# This module accepts the following optional variables:
#    NTIRPC_PATH_HINT   = A hint on NTIRPC install path.
#
# This module defines the following variables:
#    NTIRPC_FOUND       = Was NTIRPC found or not?
#    NTIRPC_LIBRARY   = The list of libraries to link to when using NTIRPC
#    NTIRPC_INCLUDE_DIR = The path to NTIRPC include directory(s)
#
# On can set NTIRPC_PATH_HINT before using find_package(NTIRPC) and the
# module with use the PATH as a hint to find NTIRPC.
#
# The hint can be given on the command line too:
#   cmake -DNTIRPC_PATH_HINT=/DATA/ERIC/NTIRPC /path/to/source

include(LibFindMacros)

libfind_pkg_detect(NTIRPC libntirpc FIND_PATH netconfig.h PATH_SUFFIXES ntirpc FIND_LIBRARY ntirpc)

find_library(NTIRPC_TRACEPOINTS ntirpc_tracepoints)
find_library(NTIRPC_LTTNG ntirpc_lttng)

if (NTIRPC_LIBRARY)
	libfind_version_header(NTIRPC version.h NTIRPC_VERSION)
endif (NTIRPC_LIBRARY)

# handle the QUIETLY and REQUIRED arguments and set PRELUDE_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(NTIRPC
                                  REQUIRED_VARS NTIRPC_INCLUDE_DIR NTIRPC_LIBRARY
				  VERSION_VAR NTIRPC_VERSION)
# VERSION FPHSA options not handled by CMake version < 2.8.2)
#                                  VERSION_VAR)
mark_as_advanced(NTIRPC_INCLUDE_DIR)
mark_as_advanced(NTIRPC_LIBRARY)

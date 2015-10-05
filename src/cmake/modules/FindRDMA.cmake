# - Find RDMA
# Find the New TIRPC RPC library
#
# This module accepts the following optional variables:
#    RDMA_PATH_HINT   = A hint on RDMA install path.
#
# This module defines the following variables:
#    RDMA_FOUND       = Was RDMA found or not?
#    RDMA_LIBRARY   = The list of libraries to link to when using RDMA
#    RDMA_INCLUDE_DIR = The path to RDMA include directory(s)
#
# One can set RDMA_PATH_HINT before using find_package(RDMA) and the
# module with use the PATH as a hint to find RDMA.
# Alternatively, one can set LIBIBVERBS_PREFIX and LIBRDMACM_PREFIX to the individual
# hints for those libraries.
#
# The hint can be given on the command line too:
#   cmake -DRDMA_PATH_HINT=/DATA/ERIC/RDMA /path/to/source

include(LibFindMacros)

# ibverbs
if (LIBIBVERBS_PREFIX)
	set(IBVERBS_PKGCONF_INCLUDE_DIRS ${LIBIBVERBS_PREFIX}/include)
	set(IBVERBS_PKGCONF_LIBRARY_DIRS ${LIBIBVERBS_PREFIX}/lib64 ${LIBIBVERBS_PREFIX}/lib)
else (LIBIBVERBS_PREFIX)
	set(IBVERBS_PKGCONF_INCLUDE_DIRS ${RDMA_PATH_HINT}/include)
	set(IBVERBS_PKGCONF_LIBRARY_DIRS ${RDMA_PATH_HINT}/lib64 ${RDMA_PATH_HINT}/lib)
endif (LIBIBVERBS_PREFIX)
libfind_pkg_detect(IBVERBS libibverbs FIND_PATH infiniband/verbs.h FIND_LIBRARY ibverbs)
libfind_process(IBVERBS)

# rdmacm
if (LIBRDMACM_PREFIX)
	set(RDMACM_PKGCONF_INCLUDE_DIRS ${LIBRDMACM_PREFIX}/include)
	set(RDMACM_PKGCONF_LIBRARY_DIRS ${LIBRDMACM_PREFIX}/lib64 ${LIBRDMACM_PREFIX}/lib)
else (LIBRDMACM_PREFIX)
	set(RDMACM_PKGCONF_INCLUDE_DIRS ${RDMA_PATH_HINT}/include)
	set(RDMACM_PKGCONF_LIBRARY_DIRS ${RDMA_PATH_HINT}/lib64 ${RDMA_PATH_HINT}/lib)
endif (LIBRDMACM_PREFIX)
libfind_pkg_detect(RDMACM librdmacm FIND_PATH rdma/rdma_cma.h FIND_LIBRARY rdmacm)
libfind_process(RDMACM)

if (IBVERBS_FOUND AND RDMACM_FOUND)
	set(RDMA_FOUND true)
	set(RDMA_LIBRARY ${IBVERBS_LIBRARY} ${RDMACM_LIBRARY})
	set(RDMA_INCLUDE_DIR ${IBVERBS_INCLUDE_DIR} ${RDMACM_INCLUDE_DIR})
else (IBVERBS_FOUND AND RDMACM_FOUND)
	set(RDMA_NOTFOUND true)
endif (IBVERBS_FOUND AND RDMACM_FOUND)

#if (RDMA_LIBRARY)
	#libfind_version_header(RDMA version.h RDMA_VERSION)
#endif (RDMA_LIBRARY)

# handle the QUIETLY and REQUIRED arguments and set PRELUDE_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(RDMA
                                  REQUIRED_VARS RDMA_INCLUDE_DIR RDMA_LIBRARY
                                  VERSION_VAR RDMA_VERSION)
# VERSION FPHSA options not handled by CMake version < 2.8.2)
#                                  VERSION_VAR)
mark_as_advanced(RDMA_INCLUDE_DIR)
mark_as_advanced(RDMA_LIBRARY)

# SPDX-License-Identifier: BSD-3-Clause
#-------------------------------------------------------------------------------
#
# Copyright Panasas, 2012
# Contributor: Jim Lieb <jlieb@panasas.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#
#-------------------------------------------------------------------------------
# - Find NTIRPC
# Find the New TIRPC RPC library
#
# This module accepts the following optional variables:
#    NTIRPC_PREFIX   = A hint on NTIRPC install path.
#
# This module defines the following variables:
#    NTIRPC_FOUND       = Was NTIRPC found or not?
#    NTIRPC_LIBRARY   = The list of libraries to link to when using NTIRPC
#    NTIRPC_INCLUDE_DIR = The path to NTIRPC include directory(s)
#
# On can set NTIRPC_PREFIX before using find_package(NTIRPC) and the
# module with use the PATH as a hint to find NTIRPC.
#
# The hint can be given on the command line too:
#   cmake -DNTIRPC_PREFIX=/DATA/ERIC/NTIRPC /path/to/source

include(LibFindMacros)

if(NTIRPC_PREFIX)
	message(STATUS "FindNTIRPC: using PATH HINT: ${NTIRPC_PREFIX}")
  # Try to make the prefix override the normal paths
	find_path(NTIRPC_INCLUDE_DIR
    NAMES rpc/xdr.h
		PATHS ${NTIRPC_PREFIX}
    PATH_SUFFIXES include/ntirpc
    NO_DEFAULT_PATH
		DOC "The NTIRPC include headers")
	message("NTIRPC_INCLUDE_DIR ${NTIRPC_INCLUDE_DIR}")

	find_path(NTIRPC_LIBRARY_DIR
    NAMES libntirpc.so
		PATHS ${NTIRPC_PREFIX}
    PATH_SUFFIXES lib/${CMAKE_LIBRARY_ARCHITECTURE} lib lib64
    NO_DEFAULT_PATH
		DOC "The NTIRPC libraries")
endif()

if (NOT NTIRPC_INCLUDE_DIR)
	find_path(NTIRPC_INCLUDE_DIR
    NAMES rpc/xdr.h
		PATHS ${NTIRPC_PREFIX}
    PATH_SUFFIXES include/ntirpc
		DOC "The NTIRPC include headers")
endif (NOT NTIRPC_INCLUDE_DIR)

if (NOT NTIRPC_LIBRARY_DIR)
	find_path(NTIRPC_LIBRARY_DIR
    NAMES libntirpc.so
		PATHS ${NTIRPC_PREFIX}
    PATH_SUFFIXES lib/${CMAKE_LIBRARY_ARCHITECTURE} lib lib64
		DOC "The NTIRPC libraries")
endif (NOT NTIRPC_LIBRARY_DIR)

find_library(NTIRPC_LIBRARY ntirpc PATHS ${NTIRPC_LIBRARY_DIR} NO_DEFAULT_PATH)
find_library(NTIRPC_TRACEPOINTS ntirpc_tracepoints PATHS ${NTIRPC_LIBRARY_DIR} NO_DEFAULT_PATH)
find_library(NTIRPC_LTTNG ntirpc_lttng PATHS ${NTIRPC_LIBRARY_DIR} NO_DEFAULT_PATH)

set(NTIRPC_VERSION_HEADER "${NTIRPC_INCLUDE_DIR}/version.h")
if (EXISTS ${NTIRPC_VERSION_HEADER})
	file(READ "${NTIRPC_VERSION_HEADER}" header)
	string(REGEX REPLACE ".*#[ \t]*define[ \t]*NTIRPC_VERSION[ \t]*\"([^\n]*)\".*" "\\1" match "${header}")
	set(NTIRPC_VERSION "${match}")
else()
	set(NTIRPC_VERSION "0.0.0")
endif()

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

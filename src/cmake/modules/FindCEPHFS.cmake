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
# - Find CephFS
# Find the Linux Trace Toolkit - next generation with associated includes path.
# See http://ceph.org/
#
# This module accepts the following optional variables:
#    CEPH_PREFIX   = A hint on CEPHFS install path.
#
# This module defines the following variables:
#    CEPHFS_FOUND       = Was CephFS found or not?
#    CEPHFS_LIBRARIES   = The list of libraries to link to when using CephFS
#    CEPHFS_INCLUDE_DIR = The path to CephFS include directory
#
# On can set CEPH_PREFIX before using find_package(CephFS) and the
# module with use the PATH as a hint to find CephFS.
#
# The hint can be given on the command line too:
#   cmake -DCEPH_PREFIX=/DATA/ERIC/CephFS /path/to/source

if(CEPH_PREFIX)
  message(STATUS "FindCephFS: using PATH HINT: ${CEPH_PREFIX}")
  # Try to make the prefix override the normal paths
  find_path(CEPHFS_INCLUDE_DIR
    NAMES cephfs/libcephfs.h
    PATHS ${CEPH_PREFIX}
    PATH_SUFFIXES include
    NO_DEFAULT_PATH
    DOC "The CephFS include headers")

  find_path(CEPHFS_LIBRARY_DIR
    NAMES libcephfs.so
    PATHS ${CEPH_PREFIX}
    PATH_SUFFIXES lib/${CMAKE_LIBRARY_ARCHITECTURE} lib lib64
    NO_DEFAULT_PATH
    DOC "The CephFS libraries")
endif(CEPH_PREFIX)

if (NOT CEPHFS_INCLUDE_DIR)
  find_path(CEPHFS_INCLUDE_DIR
    NAMES cephfs/libcephfs.h
    PATHS ${CEPH_PREFIX}
    PATH_SUFFIXES include
    DOC "The CephFS include headers")
endif (NOT CEPHFS_INCLUDE_DIR)

if (NOT CEPHFS_LIBRARY_DIR)
  find_path(CEPHFS_LIBRARY_DIR
    NAMES libcephfs.so
    PATHS ${CEPH_PREFIX}
    PATH_SUFFIXES lib/${CMAKE_LIBRARY_ARCHITECTURE} lib lib64
    DOC "The CephFS libraries")
endif (NOT CEPHFS_LIBRARY_DIR)

find_library(CEPHFS_LIBRARY cephfs PATHS ${CEPHFS_LIBRARY_DIR} NO_DEFAULT_PATH)
check_library_exists(cephfs ceph_ll_lookup ${CEPHFS_LIBRARY_DIR} CEPH_FS)
if (NOT CEPH_FS)
  unset(CEPHFS_LIBRARY_DIR CACHE)
  unset(CEPHFS_INCLUDE_DIR CACHE)
else (NOT CEPH_FS)
  check_library_exists(cephfs ceph_ll_mknod ${CEPHFS_LIBRARY_DIR} CEPH_FS_MKNOD)
  if(NOT CEPH_FS_MKNOD)
    message("Cannot find ceph_ll_mknod.  Disabling CEPH fsal mknod method")
    set(USE_FSAL_CEPH_MKNOD OFF)
  else(CEPH_FS_MKNOD)
    set(USE_FSAL_CEPH_MKNOD ON)
  endif(NOT CEPH_FS_MKNOD)
  check_library_exists(cephfs ceph_ll_setlk ${CEPHFS_LIBRARY_DIR} CEPH_FS_SETLK)
  if(NOT CEPH_FS_SETLK)
    message("Cannot find ceph_ll_setlk.  Disabling CEPH fsal lock2 method")
    set(USE_FSAL_CEPH_SETLK OFF)
  else(CEPH_FS_SETLK)
    set(USE_FSAL_CEPH_SETLK ON)
  endif(NOT CEPH_FS_SETLK)
  check_library_exists(cephfs ceph_ll_lookup_root ${CEPHFS_LIBRARY_DIR} CEPH_FS_LOOKUP_ROOT)
  if(NOT CEPH_FS_LOOKUP_ROOT)
    message("Cannot find ceph_ll_lookup_root. Working around it...")
    set(USE_FSAL_CEPH_LL_LOOKUP_ROOT OFF)
  else(NOT CEPH_FS_LOOKUP_ROOT)
    set(USE_FSAL_CEPH_LL_LOOKUP_ROOT ON)
  endif(NOT CEPH_FS_LOOKUP_ROOT)

  check_library_exists(cephfs ceph_ll_delegation ${CEPHFS_LIBRARY_DIR} CEPH_FS_DELEGATION)
  if(NOT CEPH_FS_DELEGATION)
    message("Cannot find ceph_ll_delegation. Disabling support for delegations.")
    set(USE_FSAL_CEPH_LL_DELEGATION OFF)
  else(NOT CEPH_FS_DELEGATION)
    set(USE_FSAL_CEPH_LL_DELEGATION ON)
  endif(NOT CEPH_FS_DELEGATION)

  check_library_exists(cephfs ceph_ll_sync_inode ${CEPHFS_LIBRARY_DIR} CEPH_FS_SYNC_INODE)
  if(NOT CEPH_FS_SYNC_INODE)
    message("Cannot find ceph_ll_sync_inode. SETATTR requests may be cached!")
    set(USE_FSAL_CEPH_LL_SYNC_INODE OFF)
  else(NOT CEPH_FS_SYNC_INODE)
    set(USE_FSAL_CEPH_LL_SYNC_INODE ON)
  endif(NOT CEPH_FS_SYNC_INODE)

  check_library_exists(cephfs ceph_ll_fallocate ${CEPHFS_LIBRARY_DIR} CEPH_FALLOCATE)
  if(NOT CEPH_FALLOCATE)
    message("Cannot find ceph_ll_fallocate. No ALLOCATE or DEALLOCATE support!")
    set(USE_CEPH_FALLOCATE OFF)
  else(NOT CEPH_FALLOCATE)
    set(USE_CEPH_LL_FALLOCATE ON)
  endif(NOT CEPH_FALLOCATE)

  check_library_exists(cephfs ceph_abort_conn ${CEPHFS_LIBRARY_DIR} CEPH_FS_ABORT_CONN)
  if(NOT CEPH_FS_ABORT_CONN)
	  message("Cannot find ceph_abort_conn. FSAL_CEPH will not leave session intact on clean shutdown.")
	  set(USE_FSAL_CEPH_ABORT_CONN OFF)
  else(NOT CEPH_FS_ABORT_CONN)
	  set(USE_FSAL_CEPH_ABORT_CONN ON)
  endif(NOT CEPH_FS_ABORT_CONN)

  check_library_exists(cephfs ceph_start_reclaim ${CEPHFS_LIBRARY_DIR} CEPH_FS_RECLAIM_RESET)
  if(NOT CEPH_FS_RECLAIM_RESET)
	  message("Cannot find ceph_start_reclaim. FSAL_CEPH will not kill off old sessions.")
	  set(USE_FSAL_CEPH_RECLAIM_RESET OFF)
  else(NOT CEPH_FS_RECLAIM_RESET)
	  set(USE_FSAL_CEPH_RECLAIM_RESET ON)
  endif(NOT CEPH_FS_RECLAIM_RESET)

  check_library_exists(cephfs ceph_select_filesystem ${CEPHFS_LIBRARY_DIR} CEPH_FS_GET_FS_CID)
  if(NOT CEPH_FS_GET_FS_CID)
	  message("Cannot find ceph_set_filesystem. FSAL_CEPH will only mount the default filesystem.")
	  set(USE_FSAL_CEPH_GET_FS_CID OFF)
  else(NOT CEPH_FS_GET_FS_CID)
	  set(USE_FSAL_CEPH_GET_FS_CID ON)
  endif(NOT CEPH_FS_GET_FS_CID)

  check_library_exists(cephfs ceph_ll_register_callbacks ${CEPHFS_LIBRARY_DIR} CEPH_FS_REGISTER_CALLBACKS)
  if(NOT CEPH_FS_REGISTER_CALLBACKS)
	  message("Cannot find ceph_ll_register_callbacks. FSAL_CEPH will not respond to cache pressure requests from the MDS.")
	  set(USE_FSAL_CEPH_REGISTER_CALLBACKS OFF)
  else(NOT CEPH_FS_REGISTER_CALLBACKS)
	  set(USE_FSAL_CEPH_REGISTER_CALLBACKS ON)
  endif(NOT CEPH_FS_REGISTER_CALLBACKS)

  check_library_exists(cephfs ceph_ll_lookup_vino ${CEPHFS_LIBRARY_DIR} CEPH_FS_LOOKUP_VINO)
  if(NOT CEPH_FS_LOOKUP_VINO)
	  message("Cannot find ceph_ll_lookup_vino. FSAL_CEPH will not be able to reliably look up snap inodes by handle.")
	  set(USE_FSAL_CEPH_LOOKUP_VINO OFF)
  else(NOT CEPH_FS_LOOKUP_VINO)
	  set(USE_FSAL_CEPH_LOOKUP_VINO ON)
  endif(NOT CEPH_FS_LOOKUP_VINO)

  set(CMAKE_REQUIRED_INCLUDES ${CEPHFS_INCLUDE_DIR})
  if (CMAKE_MAJOR_VERSION VERSION_EQUAL 3 AND CMAKE_MINOR_VERSION VERSION_GREATER 14)
    include(CheckSymbolExists)
  endif(CMAKE_MAJOR_VERSION VERSION_EQUAL 3 AND CMAKE_MINOR_VERSION VERSION_GREATER 14)
  check_symbol_exists(CEPH_STATX_INO "cephfs/libcephfs.h" CEPH_FS_CEPH_STATX)
  if(NOT CEPH_FS_CEPH_STATX)
    message("Cannot find CEPH_STATX_INO. Enabling backward compatibility for pre-ceph_statx APIs.")
    set(USE_FSAL_CEPH_STATX OFF)
  else(NOT CEPH_FS_CEPH_STATX)
    set(USE_FSAL_CEPH_STATX ON)
  endif(NOT CEPH_FS_CEPH_STATX)
endif (NOT CEPH_FS)

set(CEPHFS_LIBRARIES ${CEPHFS_LIBRARY})
message(STATUS "Found cephfs libraries: ${CEPHFS_LIBRARIES}")

# handle the QUIETLY and REQUIRED arguments and set PRELUDE_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(CEPHFS
  REQUIRED_VARS CEPHFS_INCLUDE_DIR CEPHFS_LIBRARY_DIR)
# VERSION FPHSA options not handled by CMake version < 2.8.2)
#                                  VERSION_VAR)
mark_as_advanced(CEPHFS_INCLUDE_DIR)
mark_as_advanced(CEPHFS_LIBRARY_DIR)
mark_as_advanced(USE_FSAL_CEPH_MKNOD)
mark_as_advanced(USE_FSAL_CEPH_SETLK)
mark_as_advanced(USE_FSAL_CEPH_LL_LOOKUP_ROOT)
mark_as_advanced(USE_FSAL_CEPH_STATX)

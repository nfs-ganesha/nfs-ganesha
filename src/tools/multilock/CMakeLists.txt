# SPDX-License-Identifier: LGPL-3.0-or-later
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
if(CEPH_FS_CEPH_STATX)
  add_definitions(
    -D_FILE_OFFSET_BITS=64
  )
  include_directories(${CEPHFS_INCLUDE_DIR})
endif(CEPH_FS_CEPH_STATX)

SET(multilock_SRCS
  ml_functions.c
  multilock.h
)

add_executable(ml_console
  ml_console.c
  ${multilock_SRCS}
)

target_link_libraries(ml_console m ${SYSTEM_LIBRARIES})

add_executable(ml_posix_client
  ml_posix_client.c
  ${multilock_SRCS}
)

target_link_libraries(ml_posix_client m pthread ${SYSTEM_LIBRARIES})

if(CEPH_FS_CEPH_STATX)
  add_executable(ml_cephfs_client
    ml_cephfs_client.c
    ${multilock_SRCS}
  )

  target_link_libraries(ml_cephfs_client m pthread ${CEPHFS_LIBRARIES} ${SYSTEM_LIBRARIES})
endif(CEPH_FS_CEPH_STATX)

if(USE_FSAL_GLUSTER AND USE_LKOWNER)
  include_directories(${GFAPI_INCLUDE_DIRS})

  add_executable(ml_glusterfs_client
    ml_glusterfs_client.c
    ${multilock_SRCS}
  )

  target_link_libraries(ml_glusterfs_client m pthread ${GFAPI_LIBRARIES} ${SYSTEM_LIBRARIES})
endif(USE_FSAL_GLUSTER AND USE_LKOWNER)


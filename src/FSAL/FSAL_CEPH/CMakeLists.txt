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
add_definitions(
  -D_FILE_OFFSET_BITS=64
)

SET(fsalceph_LIB_SRCS
   main.c
   export.c
   handle.c
   mds.c
   ds.c
   internal.c
   internal.h
   statx_compat.h
)

if (NOT CEPH_FS_CEPH_STATX)
  SET(fsalceph_LIB_SRCS
	${fsalceph_LIB_SRCS}
	statx_compat.c
     )
endif(NOT CEPH_FS_CEPH_STATX)

include_directories(${CEPHFS_INCLUDE_DIR})

add_library(fsalceph MODULE ${fsalceph_LIB_SRCS})
add_sanitizers(fsalceph)

target_link_libraries(fsalceph
  ganesha_nfsd
  ${CEPHFS_LIBRARIES}
  ${SYSTEM_LIBRARIES}
  ${LTTNG_LIBRARIES}
  ${LDFLAG_DISALLOW_UNDEF}
)

set_target_properties(fsalceph PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalceph COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )

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

########### next target ###############

IF(KVSFS_PREFIX)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -I${KVSNS_PREFIX}/include")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -L${KVSNS_PREFIX}/lib")
ENDIF()

SET(fsalkvsfs_LIB_SRCS
   kvsfs_fsal_internal.c
   kvsfs_main.c
   kvsfs_export.c
   kvsfs_handle.c
   kvsfs_file.c
   kvsfs_xattrs.c
   kvsfs_mds.c
   kvsfs_ds.c
)

add_library(fsalkvsfs MODULE ${fsalkvsfs_LIB_SRCS})
add_sanitizers(fsalceph)

target_link_libraries(fsalkvsfs
  kvsns hiredis
  ${SYSTEM_LIBRARIES}
)

set_target_properties(fsalkvsfs PROPERTIES VERSION 4.2.0 SOVERSION 4)

########### install files ###############

install(TARGETS fsalkvsfs COMPONENT fsal DESTINATION  ${FSAL_DESTINATION} )

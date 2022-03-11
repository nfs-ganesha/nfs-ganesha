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
if(USE_DBUS)
  include_directories(
    ${DBUS_INCLUDE_DIRS}
    )
endif(USE_DBUS)

include_directories("${PROJECT_SOURCE_DIR}/FSAL/FSAL_GPFS")

########### next target ###############

SET(fsalgpfs_LIB_SRCS
  main.c
  export.c
  handle.c
  file.c
  fsal_up.c
  fsal_ds.c
  fsal_mds.c
  fsal_unlink.c
  fsal_symlinks.c
  fsal_rename.c
  fsal_create.c
  fsal_fileop.c
  fsal_attrs.c
  fsal_lock.c
  fsal_lookup.c
  fsal_convert.c
  fsal_internal.c
  gpfsext.c
  fsal_stats_gpfs.c
)

add_library(fsalgpfs MODULE ${fsalgpfs_LIB_SRCS})
add_sanitizers(fsalgpfs)

target_link_libraries(fsalgpfs
  ganesha_nfsd
  ${SYSTEM_LIBRARIES}
  ${LDFLAG_DISALLOW_UNDEF}
)

set_target_properties(fsalgpfs PROPERTIES VERSION 4.2.0 SOVERSION 4)

########### install files ###############

install(TARGETS fsalgpfs COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )

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
  -D__USE_GNU
  ${LIZARDFS_CFLAGS}
)

set( LIB_PREFIX 64)

########### next target ###############

SET(fsallizardfs_LIB_SRCS
   context_wrap.c
   context_wrap.h
   ds.c
   export.c
   handle.c
   lzfs_acl.c
   lzfs_internal.c
   lzfs_internal.h
   main.c
   mds_export.c
   mds_handle.c
)

add_library(fsallizardfs MODULE ${fsallizardfs_LIB_SRCS})
add_sanitizers(fsallizardfs)

target_link_libraries(fsallizardfs
  ${SYSTEM_LIBRARIES}
  ${LIZARDFS_CLIENT_LIB}
)

set_target_properties(fsallizardfs PROPERTIES VERSION 3.12.0 SOVERSION 3)
install(TARGETS fsallizardfs COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )

# SPDX-License-Identifier: LGPL-3.0-or-later
#-------------------------------------------------------------------------------
#
# Copyright Panasas, 2012
# Contributor: Jim Lieb <jlieb@panasas.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#
#-------------------------------------------------------------------------------
add_definitions(-D__USE_MISC)

set( LIB_PREFIX 64)

SET(fsalsaunafs_LIB_SRCS
   context_wrap.c
   context_wrap.h
   ds.c
   export.c
   fileinfo_cache.c
   fileinfo_cache.h
   handle.c
   main.c
   mds_export.c
   mds_handle.c
   saunafs_acl.c
   saunafs_fsal_types.h
   saunafs_internal.c
   saunafs_internal.h
)

add_library(fsalsaunafs MODULE ${fsalsaunafs_LIB_SRCS})
add_sanitizers(fsalsaunafs)

target_link_libraries(fsalsaunafs ${SAUNAFS_CLIENT_LIB})

set_target_properties(fsalsaunafs PROPERTIES VERSION 4.0.0 SOVERSION 4)
install(TARGETS fsalsaunafs COMPONENT fsal DESTINATION ${FSAL_DESTINATION})

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
)

set( LIB_PREFIX 64)

########### next target ###############

SET(fsalnull_LIB_SRCS
   handle.c
   file.c
   xattrs.c
   nullfs_methods.h
   main.c
   export.c
)

add_library(fsalnull MODULE ${fsalnull_LIB_SRCS})
add_sanitizers(fsalnull)

target_link_libraries(fsalnull
  ganesha_nfsd
  ${LDFLAG_DISALLOW_UNDEF}
)

set_target_properties(fsalnull PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalnull COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )

########### install files ###############

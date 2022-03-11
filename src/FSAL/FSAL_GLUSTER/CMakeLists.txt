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
  ${GFAPI_CFLAGS}
)

set( LIB_PREFIX 64)

#if(USE_FSAL_XFS)
#    add_subdirectory(xfs)
#endif(USE_FSAL_XFS)

########### next target ###############

SET(fsalgluster_LIB_SRCS
   main.c
   export.c
   handle.c
   fsal_up.c
   gluster_internal.h
   gluster_internal.c
   mds.c
   ds.c
)

add_library(fsalgluster MODULE ${fsalgluster_LIB_SRCS})
add_sanitizers(fsalgluster)

target_link_libraries(fsalgluster
  ganesha_nfsd
  ${SYSTEM_LIBRARIES}
  ${GFAPI_LIBRARIES}
  ${LTTNG_LIBRARIES}
  ${LDFLAG_DISALLOW_UNDEF}
)

set_target_properties(fsalgluster PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalgluster COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )

########### install files ###############

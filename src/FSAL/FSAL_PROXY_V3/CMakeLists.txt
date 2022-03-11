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

########### next target ###############

SET(fsalproxy_v3_LIB_SRCS
  main.c
  nlm.c
  rpc.c
  utils.c
)

add_library(fsalproxy_v3 MODULE ${fsalproxy_v3_LIB_SRCS} $<TARGET_OBJECTS:nfs_mnt_xdr>)
add_sanitizers(fsalproxy_v3)

target_link_libraries(fsalproxy_v3
  ganesha_nfsd
  ${SYSTEM_LIBRARIES}
  ${LDFLAG_DISALLOW_UNDEF}
)

set_target_properties(fsalproxy_v3 PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalproxy_v3 COMPONENT fsal DESTINATION  ${FSAL_DESTINATION} )

########### install files ###############

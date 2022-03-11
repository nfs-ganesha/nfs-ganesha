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

SET(fsalrgw_LIB_SRCS
  up.c
  main.c
  export.c
  handle.c
  internal.c
  internal.h
)

message("RGW_INCLUDE_DIR ${RGW_INCLUDE_DIR}")
include_directories(${RGW_INCLUDE_DIR})

add_library(fsalrgw MODULE ${fsalrgw_LIB_SRCS})
add_sanitizers(fsalrgw)

target_link_libraries(fsalrgw
  ganesha_nfsd
  ${RGW_LIBRARIES}
  ${SYSTEM_LIBRARIES}
  ${LDFLAG_DISALLOW_UNDEF}
)

set_target_properties(fsalrgw PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalrgw COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )

########### install files ###############

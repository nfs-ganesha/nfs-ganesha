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
include_directories(
  ${LTTNG_INCLUDE_DIR}
)

set(ganesha_trace_LIB_SRCS
  fsal_ceph.c
  fsal_mem.c
  logger.c
  mdcache.c
  nfs4.c
  nfs_rpc.c
  state.c
  fsal_gluster.c
)

add_library(ganesha_trace MODULE ${ganesha_trace_LIB_SRCS})
add_sanitizers(ganesha_trace)

target_link_libraries(ganesha_trace
  ${LTTNG_LIBRARIES}
)

install(TARGETS ganesha_trace COMPONENT tracing DESTINATION ${FSAL_DESTINATION} )

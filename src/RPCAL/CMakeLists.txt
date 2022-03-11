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

SET(rpcal_STAT_SRCS
   nfs_dupreq.c
   rpc_tools.c
)

if(_HAVE_GSSAPI)
  set(rpcal_STAT_SRCS ${rpcal_STAT_SRCS} gss_credcache.c gss_extra.c)
endif(_HAVE_GSSAPI)

add_library(rpcal OBJECT ${rpcal_STAT_SRCS})
add_sanitizers(rpcal)
set_target_properties(rpcal PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############

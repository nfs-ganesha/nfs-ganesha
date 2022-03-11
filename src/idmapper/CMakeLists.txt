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
if(_MSPAC_SUPPORT)
  include_directories(
    ${WBCLIENT_INCLUDE_DIR}
    )
endif(_MSPAC_SUPPORT)

if(USE_DBUS)
  include_directories(
    ${DBUS_INCLUDE_DIRS}
    )
endif(USE_DBUS)

########### next target ###############

SET(idmap_STAT_SRCS
   idmapper.c
   idmapper_cache.c
)

add_library(idmap OBJECT ${idmap_STAT_SRCS})
add_sanitizers(idmap)
set_target_properties(idmap PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############

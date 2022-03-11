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

if(USE_DBUS)
  include_directories(
    ${DBUS_INCLUDE_DIRS}
    )
endif(USE_DBUS)

set( LIB_PREFIX 64)

########### next target ###############

SET(fsalmdcache_LIB_SRCS
	mdcache_avl.h
	mdcache_ext.h
	mdcache_int.h
	mdcache_hash.h
	mdcache_lru.h
	mdcache_handle.c
	mdcache_file.c
	mdcache_xattrs.c
	mdcache_main.c
	mdcache_export.c
	mdcache_helpers.c
	mdcache_lru.c
	mdcache_hash.c
	mdcache_avl.c
	mdcache_read_conf.c
	mdcache_up.c
	)

add_library(fsalmdcache OBJECT ${fsalmdcache_LIB_SRCS})
add_sanitizers(fsalmdcache)
set_target_properties(fsalmdcache PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############

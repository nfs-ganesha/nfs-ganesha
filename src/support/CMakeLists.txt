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
if(USE_DBUS)
  include_directories(
    ${DBUS_INCLUDE_DIRS}
    )
endif(USE_DBUS)

# string utilities.  This should eventually go into src/os
# because BSD has them and Linux doesn't

set(string_utils_STAT_SRCS
   strlcpy.c
   strnlen.c
   refstr.c
)

add_library(string_utils OBJECT ${string_utils_STAT_SRCS})
add_sanitizers(string_utils)
set_target_properties(string_utils PROPERTIES COMPILE_FLAGS "-fPIC")

# hash function libraries

set(hash_SRCS
   murmur3.c
   city.c
)

add_library(hash OBJECT ${hash_SRCS})
add_sanitizers(hash)
set_target_properties(hash PROPERTIES COMPILE_FLAGS "-fPIC")

# uid2grp mapper
set( uid2grp_SRCS
     uid2grp.c
     uid2grp_cache.c
)

add_library(uid2grp OBJECT ${uid2grp_SRCS})
add_sanitizers(uid2grp)
set_target_properties(uid2grp PROPERTIES COMPILE_FLAGS "-fPIC")

# netgroup cache
set(netgroup_cache_SRCS
    netgroup_cache.c
)
add_library(netgroup_cache OBJECT ${netgroup_cache_SRCS})
add_sanitizers(netgroup_cache)
set_target_properties(netgroup_cache PROPERTIES COMPILE_FLAGS "-fPIC")

########### next target ###############

SET(support_STAT_SRCS
   nfs4_acls.c
   nfs_creds.c
   nfs_filehandle_mgmt.c
   nfs_read_conf.c
   nfs_convert.c
   nfs_ip_name.c
   ds.c
   exports.c
   fridgethr.c
   delayed_exec.c
   bsd-base64.c
   server_stats.c
   export_mgr.c
   nfs4_fs_locations.c
)

if(ERROR_INJECTION)
  set(support_STAT_SRCS
    ${support_STAT_SRCS}
    err_inject.c
    )
endif(ERROR_INJECTION)

add_library(support OBJECT ${support_STAT_SRCS})
add_sanitizers(support)
set_target_properties(support PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############

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
# All we need to do here is control the
# build of chosen platform

if(FREEBSD)
  SET(gos_STAT_SRCS
      freebsd/atsyscalls.c
      freebsd/mntent_compat.c
      freebsd/subr.c
      freebsd/xattr.c
  )
endif(FREEBSD)

if(DARWIN)
  SET(gos_STAT_SRCS
      darwin/sys_resource.c
  )
endif(DARWIN)

if(LINUX)
  SET(gos_STAT_SRCS
      linux/acl.c
      linux/subr.c
  )
endif(LINUX)

add_library(gos OBJECT ${gos_STAT_SRCS})
add_sanitizers(gos)
set_target_properties(gos PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############

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
  SET(fsal_os_STAT_SRCS
      freebsd/handle_syscalls.c
  )
endif(FREEBSD)

if(LINUX)
  SET(fsal_os_STAT_SRCS
      linux/handle_syscalls.c
  )
endif(LINUX)

add_library(fsal_os OBJECT ${fsal_os_STAT_SRCS})
add_sanitizers(fsal_os)
set_target_properties(fsal_os PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############

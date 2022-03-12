# SPDX-License-Identifier: BSD-3-Clause
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

# where `make install` will place files
set(CMAKE_PREFIX_PATH "/usr/")

# FSAL's to build
set(USE_FSAL_GPFS ON)
set(USE_FSAL_VFS ON)
set(USE_FSAL_PROXY_V4 ON)

set(USE_DBUS ON)

# Disable FSAL's we don't use
set(USE_FSAL_CEPH OFF)
set(_MSPAC_SUPPORT OFF)
set(USE_9P OFF)

message(STATUS "Building gpfs_vfs_pnfs_only configuration")

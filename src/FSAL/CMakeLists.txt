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
# Add the directory for stackable FSALs
add_subdirectory(Stackable_FSALs)

# Add the directory for Pseudo FSAL
add_subdirectory(FSAL_PSEUDO)

# All we need to do here is control the
# build of chosen fsals
if(USE_FSAL_PROXY_V4)
  add_subdirectory(FSAL_PROXY_V4)
endif(USE_FSAL_PROXY_V4)

if(USE_FSAL_PROXY_V3)
  add_subdirectory(FSAL_PROXY_V3)
endif(USE_FSAL_PROXY_V3)

if(USE_FSAL_CEPH)
  add_subdirectory(FSAL_CEPH)
endif(USE_FSAL_CEPH)

if(USE_FSAL_RGW)
  add_subdirectory(FSAL_RGW)
endif(USE_FSAL_RGW)

if(USE_FSAL_GPFS)
  add_subdirectory(FSAL_GPFS)
endif(USE_FSAL_GPFS)

if(USE_FSAL_VFS OR USE_FSAL_LUSTRE OR USE_FSAL_XFS)
  add_subdirectory(FSAL_VFS)
endif(USE_FSAL_VFS OR USE_FSAL_LUSTRE OR USE_FSAL_XFS)

if(USE_FSAL_GLUSTER)
  add_subdirectory(FSAL_GLUSTER)
endif(USE_FSAL_GLUSTER)

if(USE_FSAL_LIZARDFS)
  add_subdirectory(FSAL_LIZARDFS)
endif(USE_FSAL_LIZARDFS)

if(USE_FSAL_KVSFS)
  add_subdirectory(FSAL_KVSFS)
endif(USE_FSAL_KVSFS)

if(USE_FSAL_MEM)
  add_subdirectory(FSAL_MEM)
endif(USE_FSAL_MEM)

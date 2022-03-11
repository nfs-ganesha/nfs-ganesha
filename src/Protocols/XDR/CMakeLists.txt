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

SET(nfs_mnt_xdr_STAT_SRCS
   xdr_mount.c
   xdr_nfs23.c
   xdr_nfsv41.c
   xdr_nlm4.c
   xdr_nsm.c
   xdr_nfsacl.c
)

if(USE_RQUOTA)
  SET(nfs_mnt_xdr_STAT_SRCS
    ${nfs_mnt_xdr_STAT_SRCS}
    xdr_rquota.c
  )
endif(USE_RQUOTA)

add_library(nfs_mnt_xdr OBJECT ${nfs_mnt_xdr_STAT_SRCS})
add_sanitizers(nfs_mnt_xdr)
set_target_properties(nfs_mnt_xdr PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############

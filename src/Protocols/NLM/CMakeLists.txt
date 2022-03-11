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

SET(nlm_STAT_SRCS
   nlm_Cancel.c
   nlm_Free_All.c
   nlm_Granted_Res.c
   nlm_Lock.c
   nlm_Null.c
   nlm_Share.c
   nlm_Sm_Notify.c
   nlm_Test.c
   nlm_Unlock.c
   nlm_Unshare.c
   nlm_async.c
   nlm_util.c
   nsm.c
)

add_library(nlm OBJECT ${nlm_STAT_SRCS})
add_sanitizers(nlm)
set_target_properties(nlm PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############

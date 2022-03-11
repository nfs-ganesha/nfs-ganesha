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

SET(cidr_STAT_SRCS
   cidr_addr.c
   cidr_compare.c
   cidr_from_str.c
   cidr_get.c
   cidr_inaddr.c
   cidr_mem.c
   cidr_misc.c
   cidr_net.c
   cidr_num.c
   cidr_to_str.c
   cidr_pow2_p.h
)

add_library(cidr OBJECT ${cidr_STAT_SRCS})
add_sanitizers(cidr)
set_target_properties(cidr PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############

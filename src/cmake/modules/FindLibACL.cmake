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

check_include_files("sys/acl.h" HAVE_SYS_ACL_H)
check_include_files("acl/libacl.h" HAVE_ACL_LIBACL_H)

find_library(LIBACL_LIBRARY NAMES acl)

if(HAVE_ACL_LIBACL_H)
  # Partially repeats above check, but is a useful sanity check
  check_library_exists(acl acl_get_file "" HAVE_LIBACL)
endif(HAVE_ACL_LIBACL_H)

if(HAVE_SYS_ACL_H)
  # Available on FreeBSD (and perhaps others) - replace on Linux
  check_symbol_exists(acl_get_fd_np sys/acl.h HAVE_ACL_GET_FD_NP)
  check_symbol_exists(acl_set_fd_np sys/acl.h HAVE_ACL_SET_FD_NP)
endif(HAVE_SYS_ACL_H)

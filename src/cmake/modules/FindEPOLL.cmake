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
# - Find EPOLL
#
# This module defines the following variables:
#    EPOLL_FOUND       = Was EPOLL found or not?
#
# On can set EPOLL_PATH_HINT before using find_package(EPOLL) and the
# module with use the PATH as a hint to find EPOLL.
#
# The hint can be given on the command line too:
#   cmake -DEPOLL_PATH_HINT=/DATA/ERIC/EPOLL /path/to/source

# epoll is emulated on FreeBSD
if (BSDBASED)
    set (EPOLL_FOUND ON)
    return ()
endif (BSDBASED)

include(CheckIncludeFiles)
include(CheckFunctionExists)

check_include_files("sys/epoll.h" EPOLL_HEADER)
check_function_exists(epoll_create EPOLL_FUNC)

# handle the QUIETLY and REQUIRED arguments and set PRELUDE_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(EPOLL REQUIRED_VARS EPOLL_HEADER EPOLL_FUNC)

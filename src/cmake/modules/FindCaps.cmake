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
# Tries to find Capabilities libraries
#
# Usage of this module as follows:
#
#     find_package(Caps)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  CAPS_PREFIX  Set this variable to the root installation of
#                       Caps if the module has problems finding
#                       the proper installation path.
#
# Variables defined by this module:
#
#  CAPS_FOUND              System has Caps libs/headers
#  CAPS_LIBRARIES          The Caps libraries (tcmalloc & profiler)
#  CAPS_INCLUDE_DIR        The location of Caps headers

find_library(CAPS NAMES cap PATHS "${CAPS_PREFIX}")
check_library_exists(
	cap
	cap_set_proc
	""
	HAVE_SET_PROC
	)

find_path(CAPS_INCLUDE_DIR NAMES sys/capability.h HINTS ${CAPS_PREFIX}/include)

if (HAVE_SET_PROC)
  set(CAPS_LIBRARIES ${CAPS})
endif (HAVE_SET_PROC)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  Caps
  DEFAULT_MSG
  CAPS_LIBRARIES
  CAPS_INCLUDE_DIR)

mark_as_advanced(
  CAPS_PREFIX
  CAPS_LIBRARIES
  CAPS_INCLUDE_DIR)

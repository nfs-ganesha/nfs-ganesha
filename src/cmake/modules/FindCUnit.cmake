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
# Tries to find CUnit
#
# Usage of this module as follows:
#
#     find_package(CUnit)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  CUNIT_PREFIX  Set this variable to the root installation of
#                       CUnit if the module has problems finding
#                       the proper installation path.
#
# Variables defined by this module:
#
#  CUNIT_FOUND              System has CUnit libs/headers
#  CUNIT_LIBRARIES          The CUnit libraries (tcmalloc & profiler)
#  CUNIT_INCLUDE_DIR        The location of CUnit headers

find_library(CUNIT_LIBRARIES NAMES cunit PATHS "${CUNIT_PREFIX}")

find_path(CUNIT_INCLUDE_DIR NAMES CUnit/Basic.h HINTS ${CUNIT_PREFIX}/include)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  CUnit
  DEFAULT_MSG
  CUNIT_LIBRARIES
  CUNIT_INCLUDE_DIR)

mark_as_advanced(
  CUNIT_PREFIX
  CUNIT_LIBRARIES
  CUNIT_INCLUDE_DIR)

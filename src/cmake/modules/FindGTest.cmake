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
# Tries to find GTest.
#
# Usage of this module as follows:
#
#     find_package(GTest)
#
# Variables used by this module, they can change the default behaviour and need
# to be set before calling find_package:
#
#  GTEST_PREFIX  Set this variable to the root installation of
#                       GTest if the module has problems finding
#                       the proper installation path.
#
# Variables defined by this module:
#
#  GTEST_FOUND              System has GTest libs/headers
#  GTEST_LIBRARIES          The GTest libraries (tcmalloc & profiler)
#  GTEST_INCLUDE_DIR        The location of GTest headers

find_library(GTEST NAMES gtest PATHS "${GTEST_PREFIX}")
find_library(GTEST_MAIN NAMES gtest_main PATHS "${GTEST_PREFIX}")

find_path(GTEST_INCLUDE_DIR NAMES gtest/gtest.h HINTS ${GTEST_PREFIX}/include)

set(GTEST_LIBRARIES ${GTEST} ${GTEST_MAIN})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  GTest
  DEFAULT_MSG
  GTEST_LIBRARIES
  GTEST_INCLUDE_DIR)

mark_as_advanced(
  GTEST_PREFIX
  GTEST_LIBRARIES
  GTEST_INCLUDE_DIR)

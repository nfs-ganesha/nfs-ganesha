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
FIND_PATH(EXECINFO_INCLUDE_DIR execinfo.h)
FIND_LIBRARY(EXECINFO_LIBRARY NAMES execinfo)

IF (EXECINFO_INCLUDE_DIR AND EXECINFO_LIBRARY)
  SET(EXECINFO_FOUND TRUE)
ENDIF (EXECINFO_INCLUDE_DIR AND EXECINFO_LIBRARY)

IF (EXECINFO_FOUND)
  IF (NOT EXECINFO_FIND_QUIETLY)
    MESSAGE(STATUS "Found execinfo library: ${EXECINFO_LIBRARY}")
  ENDIF (NOT EXECINFO_FIND_QUIETLY)
ELSE (EXECINFO_FOUND)
  IF (ExecInfo_FIND_REQUIRED)
    MESSAGE(FATAL_ERROR "Could not find libexecinfo")
  ENDIF (ExecInfo_FIND_REQUIRED)
ENDIF (EXECINFO_FOUND)

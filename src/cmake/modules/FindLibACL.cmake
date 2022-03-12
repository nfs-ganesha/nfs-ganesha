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
FIND_PATH(LIBACL_INCLUDE_DIR acl/libacl.h)
FIND_LIBRARY(LIBACL_LIBRARY NAMES acl)

IF (LIBACL_INCLUDE_DIR AND LIBACL_LIBRARY)
  SET(LIBACL_FOUND TRUE)
ENDIF (LIBACL_INCLUDE_DIR AND LIBACL_LIBRARY)

IF (LIBACL_FOUND)
  IF (NOT LIBACL_FIND_QUIETLY)
    MESSAGE(STATUS "Found ACL library: ${LIBACL_LIBRARY}")
  ENDIF (NOT LIBACL_FIND_QUIETLY)
ELSE (LIBACL_FOUND)
  IF (LibACL_FIND_REQUIRED)
    MESSAGE(FATAL_ERROR "Could not find libacl")
  ENDIF (LibACL_FIND_REQUIRED)
ENDIF (LIBACL_FOUND)

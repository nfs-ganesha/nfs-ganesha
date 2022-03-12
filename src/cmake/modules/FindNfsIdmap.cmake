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
FIND_PATH(NFSIDMAP_INCLUDE_DIR nfsidmap.h)
FIND_LIBRARY(NFSIDMAP_LIBRARY NAMES nfsidmap)

IF (NFSIDMAP_INCLUDE_DIR AND NFSIDMAP_LIBRARY)
  SET(NFSIDMAP_FOUND TRUE)
ENDIF (NFSIDMAP_INCLUDE_DIR AND NFSIDMAP_LIBRARY)

IF (NFSIDMAP_FOUND)
  IF (NOT NFSIDMAP_FIND_QUIETLY)
    MESSAGE(STATUS "Found nfs idmap library: ${NFSIDMAP_LIBRARY}")
  ENDIF (NOT NFSIDMAP_FIND_QUIETLY)
ELSE (NFSIDMAP_FOUND)
  IF (NfsIdmap_FIND_REQUIRED)
    MESSAGE(FATAL_ERROR "Could not find libnfsidmap")
  ENDIF (NfsIdmap_FIND_REQUIRED)
ENDIF (NFSIDMAP_FOUND)

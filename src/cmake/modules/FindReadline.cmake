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
FIND_PATH(READLINE_INCLUDE_DIR readline/readline.h)
FIND_LIBRARY(READLINE_LIBRARY NAMES readline)

IF (READLINE_INCLUDE_DIR AND READLINE_LIBRARY)
SET(READLINE_FOUND TRUE)
ENDIF (READLINE_INCLUDE_DIR AND READLINE_LIBRARY)

IF (READLINE_FOUND)
IF (NOT Readline_FIND_QUIETLY)
MESSAGE(STATUS "Found GNU readline: ${READLINE_LIBRARY}")
ENDIF (NOT Readline_FIND_QUIETLY)
ELSE (READLINE_FOUND)
IF (Readline_FIND_REQUIRED)
MESSAGE(FATAL_ERROR "Could not find GNU readline")
ENDIF (Readline_FIND_REQUIRED)
ENDIF (READLINE_FOUND)

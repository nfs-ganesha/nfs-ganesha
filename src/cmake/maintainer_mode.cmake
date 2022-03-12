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
SET( CMAKE_CXX_FLAGS_MAINTAINER "-Wall" CACHE STRING
    "Flags used by the C++ compiler during maintainer builds."
    FORCE )
SET( CMAKE_C_FLAGS_MAINTAINER "-Werror -Wall -Wimplicit -Wformat -Wmissing-braces -Wreturn-type -Wunused-variable -Wuninitialized -Wno-pointer-sign -Wno-strict-aliasing"
  CACHE STRING
    "Flags used by the C compiler during maintainer builds."
    FORCE )

SET( CMAKE_EXE_LINKER_FLAGS_MAINTAINER CACHE STRING
    "Flags used for linking binaries during maintainer builds."
    FORCE )
SET( CMAKE_SHARED_LINKER_FLAGS_MAINTAINER CACHE STRING
    "Flags used by the shared libraries linker during maintainer builds."
    FORCE )
MARK_AS_ADVANCED(
    CMAKE_CXX_FLAGS_MAINTAINER
    CMAKE_C_FLAGS_MAINTAINER
    CMAKE_EXE_LINKER_FLAGS_MAINTAINER
    CMAKE_SHARED_LINKER_FLAGS_MAINTAINER )

# Debug wants the same flags, plus -g
SET( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_MAINTAINER} -g" CACHE STRING
     "Debug CXX flags" FORCE )
SET( CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_MAINTAINER} -g" CACHE STRING
     "Debug C FLAGS" FORCE )
SET( CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_MAINTAINER}"
     CACHE STRING "Debug exe linker flags" FORCE )
SET( CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_MAINTAINER}"
     CACHE STRING "Debug exe linker flags" FORCE )
MARK_AS_ADVANCED(
    CMAKE_CXX_FLAGS_DEBUG
    CMAKE_C_FLAGS_DEBUG
    CMAKE_EXE_LINKER_FLAGS_DEBUG
    CMAKE_SHARED_LINKER_FLAGS_DEBUG )

SET(ALLOWED_BUILD_TYPES None Debug Release RelWithDebInfo MinSizeRel Maintainer)
STRING(REGEX REPLACE ";" " " ALLOWED_BUILD_TYPES_PRETTY "${ALLOWED_BUILD_TYPES}")

# Update the documentation string of CMAKE_BUILD_TYPE for GUIs
SET( CMAKE_BUILD_TYPE "${CMAKE_BUILD_TYPE}" CACHE STRING
    "Choose the type of build, options are: ${ALLOWED_BUILD_TYPES_PRETTY}."
    FORCE )

if( CMAKE_BUILD_TYPE STREQUAL "" )
	message( WARNING "CMAKE_BUILD_TYPE is not set, defaulting to Debug" )
	set( CMAKE_BUILD_TYPE "Debug" )
endif( CMAKE_BUILD_TYPE STREQUAL "" )

list(FIND ALLOWED_BUILD_TYPES ${CMAKE_BUILD_TYPE} BUILD_TYPE_INDEX)

if (BUILD_TYPE_INDEX EQUAL -1)
	message(SEND_ERROR "${CMAKE_BUILD_TYPE} is not a valid build type.")
endif()

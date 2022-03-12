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

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  if(${CMAKE_CXX_COMPILER_ID} MATCHES "MSVC")
    set(MSVC ON)
  endif(${CMAKE_CXX_COMPILER_ID} MATCHES "MSVC")
endif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")

if(UNIX)

  execute_process(
    COMMAND ld -V
    OUTPUT_VARIABLE LINKER_VERS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE LINKER_VERS_RESULT
    )

  if("${LINKER_VERS_RESULT}" MATCHES "^0$")
    if("${LINKER_VERS}" MATCHES "GNU gold")
      set(GOLD_LINKER ON)
    else("${LINKER_VERS}" MATCHES "GNU gold")
    endif("${LINKER_VERS}" MATCHES "GNU gold")
  endif("${LINKER_VERS_RESULT}" MATCHES "^0$")

endif(UNIX)

message(STATUS "toolchain options processed")

# SPDX-License-Identifier: BSD-3-Clause
#
# - Find Linux Standard Base Release Tools
# This module defines the following variables:
#  LSB_RELEASE_EXECUTABLE        - path to lsb_release program
#  LSB_RELEASE_VERSION_SHORT     - Output of "lsb_release -vs"
#  LSB_RELEASE_ID_SHORT          - Output of "lsb_release -is"
#  LSB_RELEASE_DESCRIPTION_SHORT - Output of "lsb_release -ds"
#  LSB_RELEASE_RELEASE_SHORT     - Output of "lsb_release -rs"
#  LSB_RELEASE_CODENAME_SHORT    - Output of "lsb_release -cs"
#

#----------------------------------------------------------------------------
# Copyright (c) 2012, Ben Morgan, University of Warwick
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#    * Neither the name of Ben Morgan, or the University of Warwick nor the
#      names of its contributors may be used to endorse or promote products
#      derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.
#----------------------------------------------------------------------------


find_program(LSB_RELEASE_EXECUTABLE lsb_release
  DOC "Linux Standard Base and Distribution command line query client")
mark_as_advanced(LSB_RELEASE_EXECUTABLE)

if(LSB_RELEASE_EXECUTABLE)
  # Extract the standard information in short format into CMake variables
  # - Version (strictly a colon separated list, kept as string for now)
  execute_process(COMMAND ${LSB_RELEASE_EXECUTABLE} -vs
    OUTPUT_VARIABLE LSB_RELEASE_VERSION_SHORT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )

  # - Distributor ID
  execute_process(COMMAND ${LSB_RELEASE_EXECUTABLE} -is
    OUTPUT_VARIABLE LSB_RELEASE_ID_SHORT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )

  # - Description
  execute_process(COMMAND ${LSB_RELEASE_EXECUTABLE} -ds
    OUTPUT_VARIABLE LSB_RELEASE_DESCRIPTION_SHORT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )
  # Description might be quoted, so strip out if they're there
  string(REPLACE
    "\"" ""
    LSB_RELEASE_DESCRIPTION_SHORT
    "${LSB_RELEASE_DESCRIPTION_SHORT}")

  # - Release
  execute_process(COMMAND ${LSB_RELEASE_EXECUTABLE} -rs
    OUTPUT_VARIABLE LSB_RELEASE_RELEASE_SHORT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )

  # - Codename
  execute_process(COMMAND ${LSB_RELEASE_EXECUTABLE} -cs
    OUTPUT_VARIABLE LSB_RELEASE_CODENAME_SHORT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LSB_RELEASE DEFAULT_MSG LSB_RELEASE_EXECUTABLE)

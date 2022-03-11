# SPDX-License-Identifier: LGPL-3.0-or-later
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
add_definitions(
  -D__USE_GNU
)

if(USE_DBUS)
  include_directories(
    ${DBUS_INCLUDE_DIRS}
    )
endif(USE_DBUS)

if(USE_LTTNG)
  include_directories(
    ${LTTNG_INCLUDE_DIR}
  )
endif(USE_LTTNG)

########### next target ###############

SET(log_STAT_SRCS
   display.c
   log_functions.c
)

add_library(log OBJECT ${log_STAT_SRCS})
add_sanitizers(log)
set_target_properties(log PROPERTIES COMPILE_FLAGS "-fPIC")

########### next target ###############

SET(test_liblog_SRCS
   test_display.c
)

########### install files ###############

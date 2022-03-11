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

SET(fsalxfs_LIB_SRCS
   main.c
   ../export.c
   ../handle.c
   handle_syscalls.c
   ../file.c
   ../xattrs.c
   ../state.c
   ../vfs_methods.h
   ../empty_check_hsm.c
   subfsal_xfs.c
  )

add_library(fsalxfs MODULE ${fsalxfs_LIB_SRCS})
add_sanitizers(fsalxfs)
if(PATH_LIBHANDLE)
add_library(handle SHARED IMPORTED)
set_target_properties(handle PROPERTIES IMPORTED_LOCATION ${PATH_LIBHANDLE})
endif(PATH_LIBHANDLE)

target_link_libraries(fsalxfs
  ganesha_nfsd
  ${SYSTEM_LIBRARIES}
  ${LDFLAG_DISALLOW_UNDEF}
)
target_link_libraries(fsalxfs handle)

set_target_properties(fsalxfs PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalxfs COMPONENT fsal DESTINATION  ${FSAL_DESTINATION} )

########### install files ###############

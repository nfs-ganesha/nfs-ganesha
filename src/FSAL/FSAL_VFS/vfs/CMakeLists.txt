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

SET(fsalvfs_LIB_SRCS_common
   ../export.c
   ../handle.c
   ../handle_syscalls.c
   ../file.c
   ../xattrs.c
   ../vfs_methods.h
   ../state.c
   ../subfsal_helpers.c
   subfsal_vfs.c
   attrs.c
)

if(USE_FSAL_VFS)

    set(FSAL_LUSTRE_VFS_NAME "VFS")
    configure_file(main-c.in.cmake main.c)
    set(fsalvfs_LIB_SRCS
        ${fsalvfs_LIB_SRCS_common}
        ../empty_check_hsm.c
        main.c
    )

    add_library(fsalvfs MODULE ${fsalvfs_LIB_SRCS} $<TARGET_OBJECTS:fsal_os>)
    add_sanitizers(fsalvfs)
    set(fsalvfs_TGT_LINK_LIB
      ${SYSTEM_LIBRARIES}
    )

    target_link_libraries(fsalvfs
      ganesha_nfsd
      ${fsalvfs_TGT_LINK_LIB}
      ${LDFLAG_DISALLOW_UNDEF})
    set_target_properties(fsalvfs PROPERTIES VERSION 4.2.0 SOVERSION 4)
    install(TARGETS fsalvfs COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )
endif(USE_FSAL_VFS)

#FSAL_LUSTRE OVER FSAL_VFS
if(USE_FSAL_LUSTRE)
    if(USE_LLAPI)
    #TRUE FSAL_LUSTRE
        set(FSAL_LUSTRE_VFS_NAME "LUSTRE")
        configure_file(main-c.in.cmake lustre_main.c)
        set(fsallustre_LIB_SRCS
          ${fsalvfs_LIB_SRCS_common}
          lustre_main.c
          llapi_check_hsm.c
        )

        add_library(fsallustre MODULE ${fsallustre_LIB_SRCS} $<TARGET_OBJECTS:fsal_os>)
        add_sanitizers(fsallustre)
        target_link_libraries(fsallustre
          ganesha_nfsd
          ${fsalvfs_TGT_LINK_LIB}
          lustreapi
          ${LDFLAG_DISALLOW_UNDEF}
        )

        set_target_properties(fsallustre PROPERTIES
          VERSION 4.2.0
          SOVERSION 4
        )

        install(TARGETS fsallustre
          COMPONENT fsal
          DESTINATION ${FSAL_DESTINATION}
        )

    else(USE_LLAPI)
    #FSAL_DUMMYLUSTRE
    #Even if we lack a good version of lustreapi, we continue to build and link
    # a dummy FSAL_LUSTRE to test the build path.
        set(FSAL_LUSTRE_VFS_NAME "DUMMYLUSTRE")
        configure_file(main-c.in.cmake dummy_lustre_main.c)
        set(fsallustre_LIB_SRCS
          ${fsalvfs_LIB_SRCS_common}
          dummy_lustre_main.c
          llapi_check_hsm.c
        )

        add_library(fsaldummylustre MODULE ${fsallustre_LIB_SRCS} $<TARGET_OBJECTS:fsal_os>)
        add_sanitizers(fsaldummylustre)
        target_link_libraries(fsaldummylustre
          ganesha_nfsd
          ${fsalvfs_TGT_LINK_LIB}
          ${LDFLAG_DISALLOW_UNDEF}
        )

        set_target_properties(fsaldummylustre PROPERTIES
          VERSION 4.2.0
          SOVERSION 4
        )

        install(TARGETS fsaldummylustre
          COMPONENT fsal
          DESTINATION ${FSAL_DESTINATION}
        )

    endif(USE_LLAPI)
endif(USE_FSAL_LUSTRE)

########### install files ###############

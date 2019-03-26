add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
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

    add_library(fsalvfs MODULE ${fsalvfs_LIB_SRCS})
    add_sanitizers(fsalvfs)
    set(fsalvfs_TGT_LINK_LIB
      ganesha_nfsd
      fsal_os
      ${SYSTEM_LIBRARIES}
    )

    target_link_libraries(fsalvfs ${fsalvfs_TGT_LINK_LIB})
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

        add_library(fsallustre MODULE ${fsallustre_LIB_SRCS})
        add_sanitizers(fsallustre)
        target_link_libraries(fsallustre
            ${fsalvfs_TGT_LINK_LIB}
            lustreapi
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

        add_library(fsaldummylustre MODULE ${fsallustre_LIB_SRCS})
        add_sanitizers(fsaldummylustre)
        target_link_libraries(fsaldummylustre
            ${fsalvfs_TGT_LINK_LIB}
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


########### next target ###############

SET(fsallustre_LIB_SRCS
   fsal_internal.c
   fsal_internal.h
   main.c
   export.c
   handle.c
   file.c
   xattrs.c
   mds.c
   ds.c
)

add_library(fsallustre SHARED ${fsallustre_LIB_SRCS})

target_link_libraries(fsallustre lustreapi ${SYSTEM_LIBRARIES})

set_target_properties(fsallustre PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsallustre COMPONENT fsal DESTINATION  ${FSAL_DESTINATION} )


########### next target ###############

if(USE_FSAL_SHOOK)
  SET(fsalshook_LIB_SRCS
    fsal_internal.c
    fsal_internal.h
    lustre_extended_types.h
    main.c
    export.c
    handle.c
    file.c
    xattrs.c
    shook_glue.c
    mds.c
    ds.c
    )

  add_library(fsalshook SHARED ${fsalshook_LIB_SRCS})

  target_link_libraries(fsalshook lustreapi shook ${SYSTEM_LIBRARIES})

  set_target_properties(fsalshook PROPERTIES VERSION 4.2.0 SOVERSION 4)
  install(TARGETS fsalshook COMPONENT fsal DESTINATION  ${FSAL_DESTINATION} )
endif(USE_FSAL_SHOOK)

########### install files ###############

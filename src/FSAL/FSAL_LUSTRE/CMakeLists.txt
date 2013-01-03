
########### next target ###############

SET(fsallustre_LIB_SRCS
   fsal_convert.c
   fsal_internal.c
   fsal_internal.h
   main.c
   export.c
   handle.c
   file.c
   xattrs.c
)

add_library(fsallustre SHARED ${fsallustre_LIB_SRCS})

target_link_libraries(fsallustre)

set_target_properties(fsallustre PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsallustre COMPONENT fsal DESTINATION lib)


########### next target ###############

if(USE_FSAL_SHOOK)
  SET(fsalshook_LIB_SRCS
    fsal_convert.c
    fsal_internal.c
    fsal_internal.h
    main.c
    export.c
    handle.c
    file.c
    xattrs.c
    )

  add_library(fsalshook SHARED ${fsalshook_LIB_SRCS})

  target_link_libraries(fsalshook)

  set_target_properties(fsalshook PROPERTIES VERSION 4.2.0 SOVERSION 4)
  install(TARGETS fsalshook COMPONENT fsal DESTINATION lib)
endif(USE_FSAL_SHOOK)

########### install files ###############

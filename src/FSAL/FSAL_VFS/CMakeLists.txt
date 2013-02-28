add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

set( LIB_PREFIX 64)

add_subdirectory(pnfs_panfs)

if(USE_FSAL_XFS)
    add_subdirectory(xfs)
endif(USE_FSAL_XFS)

########### next target ###############

SET(fsalvfs_LIB_SRCS
   fsal_convert.c
   main.c
   export.c
   handle.c
   file.c
   xattrs.c
   vfs_methods.h
)

add_library(fsalvfs SHARED ${fsalvfs_LIB_SRCS})

target_link_libraries(fsalvfs
  gos
  pnfs_panfs
  ${SYSTEM_LIBRARIES}
)

set_target_properties(fsalvfs PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalvfs COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )


########### install files ###############

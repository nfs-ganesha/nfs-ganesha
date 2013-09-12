add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
  -I/usr/include/glusterfs/api/
)

set( LIB_PREFIX 64)

#add_subdirectory(pnfs_panfs)

#if(USE_FSAL_XFS)
#    add_subdirectory(xfs)
#endif(USE_FSAL_XFS)

########### next target ###############

SET(fsalgluster_LIB_SRCS
   main.c
   export.c
   handle.c
   gluster_internal.h
   gluster_internal.c
)

add_library(fsalgluster SHARED ${fsalgluster_LIB_SRCS})

target_link_libraries(fsalgluster
  ${SYSTEM_LIBRARIES}
  gfapi
)

set_target_properties(fsalgluster PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalgluster COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )


########### install files ###############

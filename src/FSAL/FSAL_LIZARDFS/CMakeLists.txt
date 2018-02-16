add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
  ${LIZARDFS_CFLAGS}
)

set( LIB_PREFIX 64)

########### next target ###############

SET(fsallizardfs_LIB_SRCS
   context_wrap.c
   context_wrap.h
   ds.c
   export.c
   handle.c
   lzfs_acl.c
   lzfs_internal.c
   lzfs_internal.h
   main.c
   mds_export.c
   mds_handle.c
)

add_library(fsallizardfs MODULE ${fsallizardfs_LIB_SRCS})
add_sanitizers(fsallizardfs)

target_link_libraries(fsallizardfs
  ${SYSTEM_LIBRARIES}
  ${LIZARDFS_CLIENT_LIB}
)

set_target_properties(fsallizardfs PROPERTIES VERSION 3.12.0 SOVERSION 3)
install(TARGETS fsallizardfs COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )

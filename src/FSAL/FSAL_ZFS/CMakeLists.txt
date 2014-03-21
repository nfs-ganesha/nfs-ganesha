
########### next target ###############

SET(fsalzfs_LIB_SRCS
   fsal_internal.c
   main.c
   export.c
   handle.c
   file.c
   xattrs.c
)

add_library(fsalzfs SHARED ${fsalzfs_LIB_SRCS})

target_link_libraries(fsalzfs
  zfswrap
  ${SYSTEM_LIBRARIES}
)

set_target_properties(fsalzfs PROPERTIES VERSION 4.2.0 SOVERSION 4)

########### install files ###############

install(TARGETS fsalzfs COMPONENT fsal DESTINATION  ${FSAL_DESTINATION} )

add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

SET(fsalpanfs_LIB_SRCS
   main.c
   mds.c
   panfs_um_pnfs.c
   ../export.c
   ../handle.c
   ../handle_syscalls.c
   ../file.c
   ../xattrs.c
   ../state.c
   ../vfs_methods.h
   ../empty_check_hsm.c
   subfsal_panfs.c
   attrs.c
   handle.c
  )

add_library(fsalpanfs MODULE ${fsalpanfs_LIB_SRCS})
add_sanitizers(fsalpanfs)

target_link_libraries(fsalpanfs
  ganesha_nfsd
  fsal_os
  ${SYSTEM_LIBRARIES}
)

set_target_properties(fsalpanfs PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalpanfs COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )
set_target_properties(fsalpanfs PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############

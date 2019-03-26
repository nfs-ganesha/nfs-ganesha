add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
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
add_sanitizers(handle)
set_target_properties(handle PROPERTIES IMPORTED_LOCATION ${PATH_LIBHANDLE})
endif(PATH_LIBHANDLE)

target_link_libraries(fsalxfs
  ganesha_nfsd
  ${SYSTEM_LIBRARIES}
)
target_link_libraries(fsalxfs handle)

set_target_properties(fsalxfs PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalxfs COMPONENT fsal DESTINATION  ${FSAL_DESTINATION} )

########### install files ###############

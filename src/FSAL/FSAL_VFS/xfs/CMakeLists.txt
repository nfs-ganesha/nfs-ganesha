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
   subfsal_xfs.c
  )

add_library(fsalxfs SHARED ${fsalxfs_LIB_SRCS})

target_link_libraries(fsalxfs
  gos
  ${SYSTEM_LIBRARIES}
)
target_link_libraries(fsalxfs handle)

set_target_properties(fsalxfs PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalxfs COMPONENT fsal DESTINATION  ${FSAL_DESTINATION} )

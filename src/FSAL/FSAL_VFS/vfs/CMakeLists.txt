add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

SET(fsalvfs_LIB_SRCS
   main.c
   ../export.c
   ../handle.c
   ../handle_syscalls.c
   ../file.c
   ../xattrs.c
   ../vfs_methods.h
   subfsal_vfs.c
  )

add_library(fsalvfs SHARED ${fsalvfs_LIB_SRCS})

target_link_libraries(fsalvfs
  gos
  fsal_os
  ${SYSTEM_LIBRARIES}
)

set_target_properties(fsalvfs PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalvfs COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )

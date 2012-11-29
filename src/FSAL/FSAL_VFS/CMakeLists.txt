add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

add_subdirectory(pnfs_panfs)

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
  pnfs_panfs
)

set_target_properties(fsalvfs PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalvfs DESTINATION lib)


########### install files ###############

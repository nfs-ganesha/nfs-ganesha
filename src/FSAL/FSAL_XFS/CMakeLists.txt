add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

########### next target ###############

SET(fsalxfs_LIB_SRCS
   fsal_convert.c
   main.c
   export.c
   handle.c
   file.c
   xfs_fsal.h
)

add_library(fsalxfs SHARED ${fsalxfs_LIB_SRCS})

target_link_libraries(fsalxfs)

set_target_properties(fsalxfs PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalxfs DESTINATION lib)


########### install files ###############


include_directories(
  "${PROJECT_SOURCE_DIR}/NodeDB/nodedb")

########### next target ###############

SET(fsalposix_LIB_SRCS
   fsal_convert.c
   fsal_internal.c
   main.c
   export.c
   handle.c
   file.c
   xattrs.c
)

add_library(fsalposix MODULE ${fsalposix_LIB_SRCS})

target_link_libraries(fsalposix
  nodedb
  ${CMAKE_THREAD_LIBS_INIT}
)

set_target_properties(fsalposix PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalposix DESTINATION lib)


########### install files ###############

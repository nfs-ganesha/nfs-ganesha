
include_directories(
  "${PROJECT_SOURCE_DIR}/NodeDB")

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
  ${SYSTEM_LIBRARIES}
)

set_target_properties(fsalposix PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalposix COMPONENT fsal DESTINATION  ${FSAL_DESTINATION} )


########### install files ###############

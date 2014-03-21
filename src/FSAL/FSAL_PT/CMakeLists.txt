include_directories("${PROJECT_SOURCE_DIR}/FSAL/FSAL_PT")

########### next target ###############

SET(fsalpt_LIB_SRCS
  fsal_attrs.c
  fsal_close_listener.c
  fsal_convert.c
  fsal_create.c
  fsal_fileop.c
  fsal_internal.c
  fsal_internal.h
  fsal_lookup.c
  fsal_rename.c
  fsal_symlinks.c
  fsal_truncate.c
  fsal_unlink.c
  fsi_ipc_ccl.h
  pt_ganesha.c
  pt_ganesha.h
  pt_methods.h
  export.c
  handle.c
  main.c
  file.c
)

add_library(fsalpt SHARED ${fsalpt_LIB_SRCS})

target_link_libraries(fsalpt ${SYSTEM_LIBRARIES})

set_target_properties(fsalpt PROPERTIES VERSION 4.2.0 SOVERSION 4)
set(RELEASE 11)


########### install files ###############

install(TARGETS fsalpt COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )

if(USE_DBUS)
  include_directories(
    ${DBUS_INCLUDE_DIRS}
    )
endif(USE_DBUS)

include_directories("${PROJECT_SOURCE_DIR}/FSAL/FSAL_GPFS")

########### next target ###############

SET(fsalgpfs_LIB_SRCS
  main.c
  export.c
  handle.c
  file.c
  fsal_up.c
  fsal_ds.c
  fsal_mds.c
  fsal_unlink.c
  fsal_symlinks.c
  fsal_rename.c
  fsal_create.c
  fsal_fileop.c
  fsal_attrs.c
  fsal_lock.c
  fsal_lookup.c
  fsal_convert.c
  fsal_internal.c
  gpfsext.c
  fsal_stats_gpfs.c
)

add_library(fsalgpfs MODULE ${fsalgpfs_LIB_SRCS})
add_sanitizers(fsalgpfs)

target_link_libraries(fsalgpfs ${SYSTEM_LIBRARIES})

set_target_properties(fsalgpfs PROPERTIES VERSION 4.2.0 SOVERSION 4)

########### install files ###############

install(TARGETS fsalgpfs COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )

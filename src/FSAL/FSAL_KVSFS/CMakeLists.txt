
########### next target ###############

IF(KVSFS_PREFIX)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -I${KVSNS_PREFIX}/include")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -L${KVSNS_PREFIX}/lib")
ENDIF()

SET(fsalkvsfs_LIB_SRCS
   kvsfs_fsal_internal.c
   kvsfs_main.c
   kvsfs_export.c
   kvsfs_handle.c
   kvsfs_file.c
   kvsfs_xattrs.c
   kvsfs_mds.c
   kvsfs_ds.c
)

add_library(fsalkvsfs MODULE ${fsalkvsfs_LIB_SRCS})
add_sanitizers(fsalceph)

target_link_libraries(fsalkvsfs
  kvsns hiredis
  ${SYSTEM_LIBRARIES}
)

set_target_properties(fsalkvsfs PROPERTIES VERSION 4.2.0 SOVERSION 4)

########### install files ###############

install(TARGETS fsalkvsfs COMPONENT fsal DESTINATION  ${FSAL_DESTINATION} )

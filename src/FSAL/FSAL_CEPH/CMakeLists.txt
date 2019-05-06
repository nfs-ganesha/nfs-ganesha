add_definitions(
  -D_FILE_OFFSET_BITS=64
)

SET(fsalceph_LIB_SRCS
   main.c
   export.c
   handle.c
   mds.c
   ds.c
   internal.c
   internal.h
   statx_compat.h
)

if (NOT CEPH_FS_CEPH_STATX)
  SET(fsalceph_LIB_SRCS
	${fsalceph_LIB_SRCS}
	statx_compat.c
     )
endif(NOT CEPH_FS_CEPH_STATX)

include_directories(${CEPHFS_INCLUDE_DIR})

add_library(fsalceph MODULE ${fsalceph_LIB_SRCS})
add_sanitizers(fsalceph)

target_link_libraries(fsalceph
  ganesha_nfsd
  ${CEPHFS_LIBRARIES}
  ${SYSTEM_LIBRARIES}
  "-Wl,--no-undefined"
)

set_target_properties(fsalceph PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalceph COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )

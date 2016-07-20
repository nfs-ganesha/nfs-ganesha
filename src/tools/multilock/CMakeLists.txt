if(CEPH_FS_SETLK)
  add_definitions(
    -D_FILE_OFFSET_BITS=64
  )
endif(CEPH_FS_SETLK)

if(CEPH_PREFIX)
  include_directories("${CEPH_PREFIX}/include")
  link_directories(${CEPH_PREFIX}/lib)
endif(CEPH_PREFIX)

SET(multilock_SRCS
  ml_functions.c
  multilock.h
)

add_executable(ml_console
  ml_console.c
  ${multilock_SRCS}
)

target_link_libraries(ml_console m ${SYSTEM_LIBRARIES})

add_executable(ml_posix_client
  ml_posix_client.c
  ${multilock_SRCS}
)

target_link_libraries(ml_posix_client m pthread ${SYSTEM_LIBRARIES})

if(CEPH_FS_SETLK)
  add_executable(ml_cephfs_client
    ml_cephfs_client.c
    ${multilock_SRCS}
  )

  target_link_libraries(ml_cephfs_client m pthread cephfs ${SYSTEM_LIBRARIES})
endif(CEPH_FS_SETLK)

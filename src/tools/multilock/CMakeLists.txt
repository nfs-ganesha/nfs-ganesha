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

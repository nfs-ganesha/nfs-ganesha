# Set CMAKE_CXX_FLAGS, for gtest presently
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=gnu++14 -Wnon-virtual-dtor -Wno-invalid-offsetof")

set(UNITTEST_LIBS ${GTEST_LIBRARIES} boost_program_options boost_system ${PTHREAD_LIBS})
set(UNITTEST_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${GTEST_INCLUDE_DIR} -fno-strict-aliasing")

set(GANESHA_LIBRARIES
  rpcal
  log
  support
  hash
  config_parsing
  MainServices
  ${PROTOCOLS}
  ${GANESHA_CORE}
  # the following double mention of fsalpseudo and FsalCore
  # static libs is NOT a typo
  fsalpseudo
  FsalCore
  fsalpseudo
  FsalCore
  ${LIBTIRPC_LIBRARIES}
  ${SYSTEM_LIBRARIES}
  )

# generic test
set(test_example_SRCS
  test_example.cc
)
add_executable(test_example EXCLUDE_FROM_ALL
  ${test_example_SRCS})
add_sanitizers(test_example)

target_link_libraries(test_example
  ${CMAKE_THREAD_LIBS_INIT}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  pthread
  )
set_target_properties(test_example PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")

# Test using ganesha internals
set(test_ci_hash_dist1_SRCS
  test_ci_hash_dist1.cc
  )

add_executable(test_ci_hash_dist1
  ${test_ci_hash_dist1_SRCS})
add_sanitizers(test_ci_hash_dist1)

target_link_libraries(test_ci_hash_dist1
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_ci_hash_dist1 PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_lookup_latency_SRCS
  test_lookup_latency.cc
  )

add_executable(test_lookup_latency
  ${test_lookup_latency_SRCS})
add_sanitizers(test_lookup_latency)

target_link_libraries(test_lookup_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_lookup_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_readlink_latency_SRCS
  test_readlink_latency.cc
  )

add_executable(test_readlink_latency
  ${test_readlink_latency_SRCS})
add_sanitizers(test_readlink_latency)

target_link_libraries(test_readlink_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_readlink_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_mkdir_latency_SRCS
  test_mkdir_latency.cc
  )

add_executable(test_mkdir_latency
  ${test_mkdir_latency_SRCS})
add_sanitizers(test_mkdir_latency)

target_link_libraries(test_mkdir_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_mkdir_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_symlink_latency_SRCS
  test_symlink_latency.cc
  )

add_executable(test_symlink_latency
  ${test_symlink_latency_SRCS})
add_sanitizers(test_symlink_latency)

target_link_libraries(test_symlink_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_symlink_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_link_latency_SRCS
  test_link_latency.cc
  )

add_executable(test_link_latency
  ${test_link_latency_SRCS})
add_sanitizers(test_link_latency)

target_link_libraries(test_link_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_link_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_unlink_latency_SRCS
  test_unlink_latency.cc
  )

add_executable(test_unlink_latency
  ${test_unlink_latency_SRCS})
add_sanitizers(test_unlink_latency)

target_link_libraries(test_unlink_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_unlink_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_rename_latency_SRCS
  test_rename_latency.cc
  )

add_executable(test_rename_latency
  ${test_rename_latency_SRCS})
add_sanitizers(test_rename_latency)

target_link_libraries(test_rename_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_rename_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")

set(test_rbt_SRCS
  test_rbt.cc
  )

add_executable(test_rbt
  ${test_rbt_SRCS})
add_sanitizers(test_rbt)

target_link_libraries(test_rbt
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_rbt PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")

set(test_getattrs_latency_SRCS
  test_getattrs_latency.cc
  )

add_executable(test_getattrs_latency
  ${test_getattrs_latency_SRCS})
add_sanitizers(test_getattrs_latency)

target_link_libraries(test_getattrs_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_getattrs_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_close_latency_SRCS
  test_close_latency.cc
  )

add_executable(test_close_latency
  ${test_close_latency_SRCS})
add_sanitizers(test_close_latency)

target_link_libraries(test_close_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_close_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_commit2_latency_SRCS
  test_commit2_latency.cc
  )

add_executable(test_commit2_latency
  ${test_commit2_latency_SRCS})
add_sanitizers(test_commit2_latency)

target_link_libraries(test_commit2_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_commit2_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_write2_latency_SRCS
  test_write2_latency.cc
  )

add_executable(test_write2_latency
  ${test_write2_latency_SRCS})
add_sanitizers(test_write2_latency)

target_link_libraries(test_write2_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_write2_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_read2_latency_SRCS
  test_read2_latency.cc
  )

add_executable(test_read2_latency
  ${test_read2_latency_SRCS})
add_sanitizers(test_read2_latency)

target_link_libraries(test_read2_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_read2_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")

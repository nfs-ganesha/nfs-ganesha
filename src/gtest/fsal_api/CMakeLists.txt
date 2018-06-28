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


set(test_open2_latency_SRCS
  test_open2_latency.cc
  )

add_executable(test_open2_latency
  ${test_open2_latency_SRCS})
add_sanitizers(test_open2_latency)

target_link_libraries(test_open2_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_open2_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_close2_latency_SRCS
  test_close2_latency.cc
  )

add_executable(test_close2_latency
  ${test_close2_latency_SRCS})
add_sanitizers(test_close2_latency)

target_link_libraries(test_close2_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_close2_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_reopen2_latency_SRCS
  test_reopen2_latency.cc
  )

add_executable(test_reopen2_latency
  ${test_reopen2_latency_SRCS})
add_sanitizers(test_reopen2_latency)

target_link_libraries(test_reopen2_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_reopen2_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_setattr2_latency_SRCS
  test_setattr2_latency.cc
  )

add_executable(test_setattr2_latency
  ${test_setattr2_latency_SRCS})
add_sanitizers(test_setattr2_latency)

target_link_libraries(test_setattr2_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_setattr2_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_readdir_latency_SRCS
  test_readdir_latency.cc
  )

add_executable(test_readdir_latency
  ${test_readdir_latency_SRCS})
add_sanitizers(test_readdir_latency)

target_link_libraries(test_readdir_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_readdir_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_mknode_latency_SRCS
  test_mknode_latency.cc
  )

add_executable(test_mknode_latency
  ${test_mknode_latency_SRCS})
add_sanitizers(test_mknode_latency)

target_link_libraries(test_mknode_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_mknode_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_lock_op2_latency_SRCS
  test_lock_op2_latency.cc
  )

add_executable(test_lock_op2_latency
  ${test_lock_op2_latency_SRCS})
add_sanitizers(test_lock_op2_latency)

target_link_libraries(test_lock_op2_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_lock_op2_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_handle_to_key_latency_SRCS
  test_handle_to_key_latency.cc
  )

add_executable(test_handle_to_key_latency
  ${test_handle_to_key_latency_SRCS})
add_sanitizers(test_handle_to_key_latency)

target_link_libraries(test_handle_to_key_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_handle_to_key_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_release_latency_SRCS
  test_release_latency.cc
  )

add_executable(test_release_latency
  ${test_release_latency_SRCS})
add_sanitizers(test_release_latency)

target_link_libraries(test_release_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_release_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_handle_to_wire_latency_SRCS
  test_handle_to_wire_latency.cc
  )

add_executable(test_handle_to_wire_latency
  ${test_handle_to_wire_latency_SRCS})
add_sanitizers(test_handle_to_wire_latency)

target_link_libraries(test_handle_to_wire_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_handle_to_wire_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_readdir_correctness_SRCS
  test_readdir_correctness.cc
  )

add_executable(test_readdir_correctness
  ${test_readdir_correctness_SRCS})
add_sanitizers(test_readdir_correctness)

target_link_libraries(test_readdir_correctness
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_readdir_correctness PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")

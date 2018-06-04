set(test_nfs4_lookup_latency_SRCS
  test_nfs4_lookup_latency.cc
  )

add_executable(test_nfs4_lookup_latency
  ${test_nfs4_lookup_latency_SRCS})
add_sanitizers(test_nfs4_lookup_latency)

target_link_libraries(test_nfs4_lookup_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_nfs4_lookup_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_nfs4_putfh_latency_SRCS
  test_nfs4_putfh_latency.cc
  )

add_executable(test_nfs4_putfh_latency
  ${test_nfs4_putfh_latency_SRCS})
add_sanitizers(test_nfs4_putfh_latency)

target_link_libraries(test_nfs4_putfh_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_nfs4_putfh_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_nfs4_rename_latency_SRCS
  test_nfs4_rename_latency.cc
  )

add_executable(test_nfs4_rename_latency
  ${test_nfs4_rename_latency_SRCS})
add_sanitizers(test_nfs4_rename_latency)

target_link_libraries(test_nfs4_rename_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_nfs4_rename_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


set(test_nfs4_link_latency_SRCS
  test_nfs4_link_latency.cc
  )

add_executable(test_nfs4_link_latency
  ${test_nfs4_link_latency_SRCS})
add_sanitizers(test_nfs4_link_latency)

target_link_libraries(test_nfs4_link_latency
  ${GANESHA_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_nfs4_link_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")

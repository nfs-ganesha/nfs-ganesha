# Set CMAKE_CXX_FLAGS, for gtest presently
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=gnu++14 -Wnon-virtual-dtor -Wno-invalid-offsetof")

set(UNITTEST_LIBS ${GTEST_MAIN} ${GTEST} boost_program_options boost_system ${PTHREAD_LIBS})
set(UNITTEST_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${GTEST_PREFIX}/include -fno-strict-aliasing")

set(GTEST_LIBRARIES
  MainServices
  ${PROTOCOLS}
  ${GANESHA_CORE}
  fsalpseudo
  FsalCore
  fsalpseudo
  FsalCore
  config_parsing
  ${LIBTIRPC_LIBRARIES}
  ${SYSTEM_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
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
  ${GTEST_LIBRARIES}
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
  ${GTEST_LIBRARIES}
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
  ${GTEST_LIBRARIES}
  )
set_target_properties(test_readlink_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")

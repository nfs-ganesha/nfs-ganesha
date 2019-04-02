# Set CMAKE_CXX_FLAGS, for gtest presently
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=gnu++14 -Wnon-virtual-dtor -Wno-invalid-offsetof")

set(UNITTEST_LIBS ${GTEST_LIBRARIES} boost_program_options boost_system ${PTHREAD_LIBS})
set(UNITTEST_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${PROJECT_SOURCE_DIR}/gtest -I${GTEST_INCLUDE_DIR} -fno-strict-aliasing")

add_subdirectory(fsal_api)
add_subdirectory(nfs4)

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
  ganesha_nfsd
  ${LIBTIRPC_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_ci_hash_dist1 PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")

set(test_rbt_SRCS
  test_rbt.cc
  )

add_executable(test_rbt
  ${test_rbt_SRCS})
add_sanitizers(test_rbt)

target_link_libraries(test_rbt
  ganesha_nfsd
  ${LIBTIRPC_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_rbt PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")


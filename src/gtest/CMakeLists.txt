# Set CMAKE_CXX_FLAGS, for gtest presently
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=gnu++14 -Wnon-virtual-dtor -Wno-invalid-offsetof")

set(UNITTEST_LIBS ${GTEST_MAIN} ${GTEST} boost_program_options boost_system ${PTHREAD_LIBS})
set(UNITTEST_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${GTEST_PREFIX}/include -fno-strict-aliasing")

# generic test
set(test_example_SRCS
  test_example.cc
)
add_executable(test_example EXCLUDE_FROM_ALL
  ${test_example_SRCS})
target_link_libraries(test_example
  ${CMAKE_THREAD_LIBS_INIT}
  ${UNITTEST_LIBS}
  pthread
  )
set_target_properties(test_example PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")

# Test using ganesha internals
set(test_ci_hash_dist1_SRCS
  test_ci_hash_dist1.cc
  )

message( STATUS "fsal_CORE_SRCS: ${fsal_CORE_SRCS}" )

add_executable(test_ci_hash_dist1
  ${test_ci_hash_dist1_SRCS})

target_link_libraries(test_ci_hash_dist1
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
  )
set_target_properties(test_ci_hash_dist1 PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")

# SPDX-License-Identifier: LGPL-3.0-or-later
#-------------------------------------------------------------------------------
#
# Copyright Panasas, 2012
# Contributor: Jim Lieb <jlieb@panasas.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#
#-------------------------------------------------------------------------------
# Set CMAKE_CXX_FLAGS, for gtest presently
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=gnu++11 -fpermissive -Wnon-virtual-dtor -Wno-invalid-offsetof")

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


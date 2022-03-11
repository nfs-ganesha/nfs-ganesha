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
set(test_nfs4_lookup_latency_SRCS
  test_nfs4_lookup_latency.cc
  )

add_executable(test_nfs4_lookup_latency
  ${test_nfs4_lookup_latency_SRCS})
add_sanitizers(test_nfs4_lookup_latency)

target_link_libraries(test_nfs4_lookup_latency
  ganesha_nfsd
  ${LIBTIRPC_LIBRARIES}
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
  ganesha_nfsd
  ${LIBTIRPC_LIBRARIES}
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
  ganesha_nfsd
  ${LIBTIRPC_LIBRARIES}
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
  ganesha_nfsd
  ${LIBTIRPC_LIBRARIES}
  ${UNITTEST_LIBS}
  ${LTTNG_LIBRARIES}
  ${LTTNG_CTL_LIBRARIES}
  ${GPERFTOOLS_LIBRARIES}
  )
set_target_properties(test_nfs4_link_latency PROPERTIES COMPILE_FLAGS
  "${UNITTEST_CXX_FLAGS}")

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
if(USE_CUNIT)
  SET(test_avl_SRCS
    test_avl.c
    )
  add_executable(test_avl EXCLUDE_FROM_ALL ${test_avl_SRCS})
  target_link_libraries(test_avl ganesha_nfsd ${CMAKE_THREAD_LIBS_INIT})

  SET(test_mh_avl_SRCS
    test_mh_avl.c
    ../support/murmur3.c
    )
  add_executable(test_mh_avl EXCLUDE_FROM_ALL ${test_mh_avl_SRCS})
  target_link_libraries(test_mh_avl ganesha_nfsd ${CMAKE_THREAD_LIBS_INIT})
endif(USE_CUNIT)

SET(test_glist_SRCS
   test_glist.c
)
add_executable(test_glist EXCLUDE_FROM_ALL ${test_glist_SRCS})
target_link_libraries(test_glist ganesha_nfsd ${CMAKE_THREAD_LIBS_INIT})

SET(test_url_regex_SRCS
  test_url_regex.c
  )
add_executable(test_url_regex EXCLUDE_FROM_ALL ${test_url_regex_SRCS})
target_link_libraries(test_url_regex ganesha_nfsd ${CMAKE_THREAD_LIBS_INIT})

SET(test_cidr_SRCS
   test_cidr.c
)

add_executable(test_cidr EXCLUDE_FROM_ALL ${test_cidr_SRCS})
target_link_libraries(test_cidr ganesha_nfsd)

SET(test_recovery_tag_SRCS
   test_recovery_tag.c
)

add_executable(test_recovery_tag EXCLUDE_FROM_ALL ${test_recovery_tag_SRCS})
target_link_libraries(test_recovery_tag ganesha_nfsd ${CMAKE_THREAD_LIBS_INIT})

SET(test_gsh_gethostname_SRCS
   test_gsh_gethostname.c
)

add_executable(test_gsh_gethostname EXCLUDE_FROM_ALL ${test_gsh_gethostname_SRCS})
target_link_libraries(test_gsh_gethostname ganesha_nfsd)


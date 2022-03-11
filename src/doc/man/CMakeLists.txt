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
set(sphinx_output_dir ${CMAKE_BINARY_DIR}/doc)

set(man_srcs
    ganesha-config.rst
    ganesha-log-config.rst
    ganesha-cache-config.rst
    ganesha-export-config.rst
    ganesha-core-config.rst)

if(USE_9P)
   list(APPEND man_srcs
        ganesha-9p-config.rst)
endif()

if(USE_FSAL_CEPH)
   list(APPEND man_srcs
        ganesha-ceph-config.rst)
endif()

if(USE_FSAL_RGW)
   list(APPEND man_srcs
        ganesha-rgw-config.rst)
endif()

if(USE_FSAL_XFS)
   list(APPEND man_srcs
        ganesha-xfs-config.rst)
endif()

if(USE_FSAL_GLUSTER)
   list(APPEND man_srcs
        ganesha-gluster-config.rst)
endif()

if(USE_FSAL_VFS)
   list(APPEND man_srcs
        ganesha-vfs-config.rst)
endif()

if(USE_FSAL_LUSTRE)
   list(APPEND man_srcs
        ganesha-lustre-config.rst)
endif()

if(USE_FSAL_PROXY_V4)
   list(APPEND man_srcs
        ganesha-proxy-v4-config.rst)
endif()

if(USE_FSAL_PROXY_V3)
    list(APPEND man_srcs
        ganesha-proxy-v3-config.rst)
endif()

if(USE_FSAL_GPFS)
   list(APPEND man_srcs
        ganesha-gpfs-config.rst)
endif()

if(USE_RADOS_RECOV)
	list(APPEND man_srcs
	     ganesha-rados-cluster-design.rst
	     ganesha-rados-grace.rst)
endif()

foreach(man ${man_srcs})
  list(APPEND sphinx_input ${CMAKE_CURRENT_SOURCE_DIR}/${man})
  string(REGEX REPLACE ".rst$" "" cmd ${man})
  list(APPEND sphinx_output ${sphinx_output_dir}/${cmd}.8)
  install(FILES ${sphinx_output_dir}/${cmd}.8
          DESTINATION ${CMAKE_INSTALL_PREFIX}/share/man/man8/)
endforeach()

add_custom_command(
  OUTPUT ${sphinx_output}
  COMMAND ${SPHINX_BUILD} -b man -t man -d ${CMAKE_BINARY_DIR}/doc/doctrees -c ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR} ${sphinx_output_dir}
  DEPENDS ${sphinx_input})

add_custom_target(
  manpages ALL
  DEPENDS ${sphinx_output}
  COMMENT "manpages building")


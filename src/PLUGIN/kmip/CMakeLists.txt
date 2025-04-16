# SPDX-License-Identifier: LGPL-3.0-or-later
#-------------------------------------------------------------------------------
#
# Copyright IBM Corporation, 2025
# Contributor: Marcus Watts <mwatts@ibm.com>
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

add_definitions(
  -D__USE_GNU
)

set(plugin_kmip_SRCS
  ${plugin_kmip_SRCS}
  kmip_fscrypt.c
  )
add_library(kmip_fscrypt MODULE ${plugin_kmip_SRCS})
add_sanitizers(kmip_fscrypt)
target_link_libraries(kmip_fscrypt PRIVATE
  ganesha_nfsd
  ${SYSTEM_LIBRARIES}
  ${LDFLAG_DISALLOW_UNDEF})
# include_directories(${RADOS_INCLUDE_DIR})

target_include_directories(kmip_fscrypt PRIVATE "${CMAKE_SOURCE_DIR}/libkmip")
target_link_libraries(kmip_fscrypt PRIVATE kmip
    OpenSSL::SSL OpenSSL::Crypto)

set_target_properties(kmip_fscrypt PROPERTIES SOVERSION
  "${GANESHA_MAJOR_VERSION}${GANESHA_MINOR_VERSION}")
install(TARGETS kmip_fscrypt LIBRARY COMPONENT plugin DESTINATION ${FSAL_DESTINATION})

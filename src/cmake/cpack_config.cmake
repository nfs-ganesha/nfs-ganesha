# SPDX-License-Identifier: BSD-3-Clause
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
# A few CPack related variables
set(CPACK_PACKAGE_NAME "nfs-ganesha" )
set(CPACK_PACKAGE_VERSION "${GANESHA_VERSION}" )
set(CPACK_PACKAGE_VENDOR "NFS-Ganesha Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "NFS-Ganesha - A NFS Server runnning in user space")

# CPack's debian stuff
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "nfs-ganesha-devel@lists.sourceforge.net") 

set(CPACK_RPM_COMPONENT_INSTALL OFF)
set(CPACK_COMPONENTS_IGNORE_GROUPS "IGNORE")

# Tell CPack the kind of packages to be generated
set(CPACK_GENERATOR "TGZ")
set(CPACK_SOURCE_GENERATOR "TGZ")

set(CPACK_SOURCE_IGNORE_FILES
  "/.git/;/.gitignore/;/.bzr/;~$;${CPACK_SOURCE_IGNORE_FILES}")

set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}")

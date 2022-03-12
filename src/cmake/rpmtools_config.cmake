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
set( RPM_NAME ${PROJECT_NAME} )
set( PACKAGE_VERSION "${${PROJECT_NAME}_MAJOR_VERSION}.${${PROJECT_NAME}_MINOR_VERSION}.${${PROJECT_NAME}_PATCH_LEVEL}" ) 
set( RPM_SUMMARY "NFS-Ganesha is a NFS Server running in user space" )
set( RPM_RELEASE_BASE 1 )
set( RPM_RELEASE ${RPM_RELEASE_BASE}.git${_GIT_HEAD_COMMIT_ABBREV} )
set( RPM_PACKAGE_LICENSE "LGPLv3" )
set( RPM_PACKAGE_GROUP "Applications/System" )
set( RPM_URL "http://nfs-ganesha.sourceforge.net" )
set( RPM_CHANGELOG_FILE ${PROJECT_SOURCE_DIR}/rpm_changelog )

set( RPM_DESCRIPTION 
"NFS-GANESHA is a NFS Server running in user space.
It comes with various back-end modules (called FSALs) provided as shared objects to support different file systems and
name-spaces." )


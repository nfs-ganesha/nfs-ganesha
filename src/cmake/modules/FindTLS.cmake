# SPDX-License-Identifier: LGPL-3.0-or-later
#
# vim:noexpandtab:shiftwidth=8:tabstop=8:
#
# Copyright (C) 2025, IBM . All rights reserved.
# Author: Deeraj Patil <deeraj.patil@ibm.com>
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301 USA.  see <http://www.gnu.org/licenses/
#
#-------------------------------------------------------------------------------
# - Find TLS libraries
# Find the TLS libraries (OpenSSL, GNU-TLS) and headers
#  TLS_INCLUDE_DIRS      - where to find tls headers
#  TLS_LIBRARIES         - List of libraries when using TLS
#  TLS_FOUND             - True if TLS found
#  TLS_BACKEND           - Which TLS backend is being used (OPENSSL, GNUTLS)
# TLS modules may be specified as components for this find module.
# Components include:
#  openssl           - OpenSSL library
#  gnutls            - GNU-TLS library
# Typical usage:
#  FIND_PACKAGE(TLS REQUIRED openssl)

# First check for TLS_PREFIX to find custom installations
IF(TLS_PREFIX)
  FIND_PROGRAM(OPENSSL_CONFIG NAMES openssl
    PATHS ${TLS_PREFIX}/bin
    NO_SYSTEM_ENVIRONMENT_PATH
    NO_DEFAULT_PATH
  )

  FIND_PROGRAM(GNUTLS_CONFIG NAMES gnutls
    PATHS ${TLS_PREFIX}/bin
    NO_SYSTEM_ENVIRONMENT_PATH
    NO_DEFAULT_PATH
  )
ENDIF(TLS_PREFIX)

# Find OpenSSL
FIND_PROGRAM(OPENSSL_CONFIG NAMES openssl)
IF(OPENSSL_CONFIG)
  MESSAGE(STATUS "Found OpenSSL: ${OPENSSL_CONFIG}")
ENDIF(OPENSSL_CONFIG)

# Find GNUTLS
FIND_PROGRAM(GNUTLS_CONFIG NAMES gnutls)
IF(GNUTLS_CONFIG)
  MESSAGE(STATUS "Found GNUTLS: ${GNUTLS_CONFIG}")
ENDIF(GNUTLS_CONFIG)

# Check if we found anything
SET(TLS_FOUND 0)

# Process the requested components
SET(TLS_BACKEND "NONE")

# Check for OpenSSL - compatible with older CMake versions
LIST(FIND TLS_FIND_COMPONENTS "openssl" _openssl_idx)
IF(NOT _openssl_idx EQUAL -1)
  FIND_PACKAGE(OpenSSL)
  IF(OPENSSL_FOUND)
    SET(TLS_FOUND 1)
    SET(TLS_BACKEND "OPENSSL")
    SET(TLS_INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR})
    SET(TLS_LIBRARIES ${OPENSSL_LIBRARIES})
    MESSAGE(STATUS "Using OpenSSL as TLS backend")
    SET(USE_OPENSSL ON)
    add_definitions(-DUSE_TLS -DUSE_OPENSSL)
  ELSE(OPENSSL_FOUND)
    IF(TLS_FIND_REQUIRED_openssl)
      MESSAGE(FATAL_ERROR "OpenSSL requested but not found")
    ELSE(TLS_FIND_REQUIRED_openssl)
      MESSAGE(WARNING "OpenSSL requested but not found")
    ENDIF(TLS_FIND_REQUIRED_openssl)
  ENDIF(OPENSSL_FOUND)
ENDIF(NOT _openssl_idx EQUAL -1)

LIST(FIND TLS_FIND_COMPONENTS "gnutls" _gnutls_idx)
IF(NOT _gnutls_idx EQUAL -1)
  IF(TLS_FOUND)
    MESSAGE(STATUS "TLS_LIB already found ${TLS_BACKEND}")
  ELSE(TLS_FOUND)
    FIND_PACKAGE(GnuTLS)
    IF(GNUTLS_FOUND)
      SET(TLS_FOUND 1)
      SET(TLS_BACKEND "GNUTLS")
      SET(TLS_INCLUDE_DIRS ${GNUTLS_INCLUDE_DIRS})
      SET(TLS_LIBRARIES ${GNUTLS_LIBRARIES})
      MESSAGE(STATUS "Using GnuTLS as TLS backend")
      SET(USE_GNUTLS ON)
      add_definitions(-DUSE_TLS -DUSE_GNUTLS)
    ELSE(GNUTLS_FOUND)
      IF(TLS_FIND_REQUIRED_gnutls)
        MESSAGE(FATAL_ERROR "GnuTLS requested but not found")
      ELSE(TLS_FIND_REQUIRED_gnutls)
        MESSAGE(WARNING "GnuTLS requested but not found")
      ENDIF(TLS_FIND_REQUIRED_gnutls)
    ENDIF(GNUTLS_FOUND)
  ENDIF(TLS_FOUND)
ENDIF(NOT _gnutls_idx EQUAL -1)

# Report the results
IF(NOT TLS_FOUND)
  SET(TLS_DIR_MESSAGE
    "TLS was not found. Make sure the entries TLS_* are set.")
  IF(NOT TLS_FIND_QUIETLY)
    MESSAGE(STATUS "${TLS_DIR_MESSAGE}")
  ELSE(NOT TLS_FIND_QUIETLY)
    IF(TLS_FIND_REQUIRED)
      MESSAGE(FATAL_ERROR "${TLS_DIR_MESSAGE}")
    ENDIF(TLS_FIND_REQUIRED)
  ENDIF(NOT TLS_FIND_QUIETLY)
ELSE(NOT TLS_FOUND)
  MESSAGE(STATUS "Found TLS backend: ${TLS_BACKEND}")
  MESSAGE(STATUS "Found TLS headers: ${TLS_INCLUDE_DIRS}")
  MESSAGE(STATUS "Found TLS libs: ${TLS_LIBRARIES}")
ENDIF(NOT TLS_FOUND)


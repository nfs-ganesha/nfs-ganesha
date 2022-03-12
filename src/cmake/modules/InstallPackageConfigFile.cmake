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
include(InstallClobberImmune)

# This macro can be used to install configuration files which
# users are expected to modify after installation.  It will:
#
#   - If binary packaging is enabled:
#     Install the file in the typical CMake fashion, but append to the
#     INSTALLED_CONFIG_FILES cache variable for use with the Mac package's
#     pre/post install scripts
#
#   - If binary packaging is not enabled:
#     Install the script in a way such that it will check at `make install`
#     time whether the file does not exist.  See InstallClobberImmune.cmake
#
#   - Always create a target "install-example-configs" which installs an
#     example version of the config file.
#
#   _srcfile: the absolute path to the file to install
#   _dstdir: absolute path to the directory in which to install the file
#   _dstfilename: how to (re)name the file inside _dstdir

macro(InstallPackageConfigFile _srcfile _dstdir _dstfilename)
    set(_dstfile ${_dstdir}/${_dstfilename})

    if (BINARY_PACKAGING_MODE)
        # If packaging mode is enabled, always install the distribution's
        # version of the file.  The Mac package's pre/post install scripts
        # or native functionality of RPMs will take care of not clobbering it.
        install(FILES ${_srcfile} DESTINATION ${_dstdir} RENAME ${_dstfilename})
        # This cache variable is what the Mac package pre/post install scripts
        # use to avoid clobbering user-modified config files
        set(INSTALLED_CONFIG_FILES
            "${INSTALLED_CONFIG_FILES} ${_dstfile}" CACHE STRING "" FORCE)

        # Additionally, the Mac PackageMaker packages don't have any automatic
        # handling of configuration file conflicts so install an example file
        # that the post install script will cleanup in the case it's extraneous
        if (APPLE)
            install(FILES ${_srcfile} DESTINATION ${_dstdir}
                    RENAME ${_dstfilename}.example)
        endif ()
    else ()
        # Have `make install` check at run time whether the file does not exist
        InstallClobberImmune(${_srcfile} ${_dstfile})
    endif ()

    if (NOT TARGET install-example-configs)
        add_custom_target(install-example-configs
                          COMMENT "Installed example configuration files")
    endif ()

    # '/' is invalid in target names, so replace w/ '.'
    string(REGEX REPLACE "/" "." _flatsrc ${_srcfile})

    set(_example ${_dstfile}.example)

    add_custom_target(install-example-config-${_flatsrc}
        COMMAND "${CMAKE_COMMAND}" -E copy ${_srcfile} \${DESTDIR}${_example}
        COMMENT "Installing ${_example}")

    add_dependencies(install-example-configs install-example-config-${_flatsrc})

endmacro(InstallPackageConfigFile)

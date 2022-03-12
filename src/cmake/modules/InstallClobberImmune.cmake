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
# Determines at `make install` time if a file, typically a configuration
# file placed in $PREFIX/etc, shouldn't be installed to prevent overwrite
# of an existing file.
#
# _srcfile: the file to install
# _dstfile: the absolute file name after installation

macro(InstallClobberImmune _srcfile _dstfile)
    install(CODE "
        set(_destfile \"${_dstfile}\")
        if (NOT \"\$ENV{DESTDIR}\" STREQUAL \"\")
            # prepend install root prefix with install-time DESTDIR
            set(_destfile \"\$ENV{DESTDIR}/${_dstfile}\")
        endif ()
        if (EXISTS \${_destfile})
            message(STATUS \"Skipping: \${_destfile} (already exists)\")
            execute_process(COMMAND \"${CMAKE_COMMAND}\" -E compare_files
                ${_srcfile} \${_destfile} RESULT_VARIABLE _diff)
            if (NOT \"\${_diff}\" STREQUAL \"0\")
                message(STATUS \"Installing: \${_destfile}.example\")
                configure_file(${_srcfile} \${_destfile}.example COPYONLY)
            endif ()
        else ()
            message(STATUS \"Installing: \${_destfile}\")
            # install() is not scriptable within install(), and
            # configure_file() is the next best thing
            configure_file(${_srcfile} \${_destfile} COPYONLY)
            # TODO: create additional install_manifest files?
        endif ()
    ")
endmacro(InstallClobberImmune)

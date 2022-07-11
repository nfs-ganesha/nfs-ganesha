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

if(USE_FSAL_GPFS)
  if(Python3_FOUND)
    set(SETUP_PY_IN "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in")
    set(SETUP_PY    "${CMAKE_CURRENT_BINARY_DIR}/setup.py")
    set(OUTPUT      "${CMAKE_CURRENT_BINARY_DIR}/build/gpfs_epoch_timestamp")

    set(GPFS_EPOCH_SRCS gpfs-epoch.py)

    # Generate rules to copy command line scripts from src to build,
    # stripping .py along the way
    set(SCRIPTS)
    foreach(src_file ${GPFS_EPOCH_SRCS})
      string(REPLACE ".py" "" script_file ${src_file})
      add_custom_command(
        OUTPUT ${script_file}
	COMMAND mkdir -p build/lib
        COMMAND echo ${script_file}
        COMMAND cp "${CMAKE_CURRENT_SOURCE_DIR}/${src_file}" ${script_file}
        DEPENDS ${src_file}
        )
      list(APPEND SCRIPTS ${script_file})
    endforeach()

    # Build up the string for the configure substitution in setup.py
    set(SCRIPTS_STRING)
    foreach(script_py ${GPFS_EPOCH_SRCS})
      string(REPLACE ".py" "" script ${script_py})
      if("${SCRIPTS_STRING}" STREQUAL "")
        set(SCRIPTS_STRING "'${script}'")
      else()
        set(SCRIPTS_STRING "${SCRIPTS_STRING}, '${script}'")
      endif()
    endforeach()

    configure_file(${SETUP_PY_IN} ${SETUP_PY})

    add_custom_command(
      OUTPUT ${OUTPUT}
      COMMAND ${CMAKE_COMMAND} -E touch __init__.py
      COMMAND ${Python3_EXECUTABLE} "${SETUP_PY}"  build
      COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT}
      DEPENDS ${GPFS_EPOCH_SRCS} ${SCRIPTS}
      )

    add_custom_target(python_gpfs_epoch ALL DEPENDS ${OUTPUT})

    install(
      CODE
      "execute_process(WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} COMMAND ${Python3_EXECUTABLE} ${SETUP_PY} install --skip-build --no-compile --prefix=\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX} --install-scripts=\$ENV{DESTDIR}${LIBEXECDIR}/ganesha)"
      )
  endif(Python3_FOUND)
endif(USE_FSAL_GPFS)

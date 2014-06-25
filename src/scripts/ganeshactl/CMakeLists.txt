# Building and packaging DBUS scripts for ganesha

find_program(PYTHON "python")

# PyQT based class modules
set(GANESHA_SRCS
  Ganesha/__init__.py
  Ganesha/admin.py
  Ganesha/client_mgr.py
  Ganesha/QtUI/clients_table.py
  Ganesha/export_mgr.py
  Ganesha/QtUI/exports_table.py
  Ganesha/log_mgr.py
  Ganesha/QtUI/log_settings.py
  Ganesha/io_stats.py
  )

# Command line scripts
set(SCRIPT_SRC
  fake_recall.py
  get_clientids.py
  grace_period.py
  purge_gids.py
  stats_fast.py
  stats_global.py
  stats_inode.py
  stats_io.py
  stats_pnfs.py
  stats.py
  stats_total.py
  ganeshactl.py
  ganesha-admin.py
  manage_clients.py
  manage_logger.py
  manage_exports.py
  )

# Qt Designer files that get compiled into .py scripts
set(UI_SRC
  Ganesha/QtUI/ui_main_window.ui
  Ganesha/QtUI/ui_log_dialog.ui
  )

if(PYTHON)
  set(SETUP_PY_IN "${CMAKE_CURRENT_SOURCE_DIR}/setup.py.in")
  set(SETUP_PY    "${CMAKE_CURRENT_BINARY_DIR}/setup.py")
  set(OUTPUT      "${CMAKE_CURRENT_BINARY_DIR}/build/timestamp")

  # Generate rules for compiling .ui -> .py
  set(UI_PY_FILES)
  foreach(ui_file ${UI_SRC})
    string(REPLACE ".ui" ".py" py_file ${ui_file})
    add_custom_command(
      OUTPUT ${py_file}
      COMMAND mkdir -p build/lib/Ganesha/QtUI
      COMMAND pyuic4 "${CMAKE_CURRENT_SOURCE_DIR}/${ui_file}" -o "build/lib/${py_file}"
      DEPENDS ${ui_file}
      )
    set(UI_PY_FILES ${UI_PY_FILES} "${py_file}")
    list(APPEND GANESHA_SRCS ${py_file})
  endforeach()

  # Generate rules to copy command line scripts from src to build,
  # stripping .py along the way
  set(SCRIPTS)
  foreach(src_file ${SCRIPT_SRC})
    string(REPLACE ".py" "" script_file ${src_file})
    add_custom_command(
      OUTPUT ${script_file}
      COMMAND cp "${CMAKE_CURRENT_SOURCE_DIR}/${src_file}" ${script_file}
      DEPENDS ${src_file}
      )
    list(APPEND SCRIPTS ${script_file})
  endforeach()

  # Build up the string for the configure substitution in setup.py
  set(SCRIPTS_STRING)
  foreach(script_py ${SCRIPT_SRC})
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
    COMMAND ${PYTHON} "${SETUP_PY}"  build
    COMMAND ${CMAKE_COMMAND} -E touch ${OUTPUT}
    DEPENDS ${GANESHA_SRCS} ${UI_PY_FILES} ${SCRIPTS}
    )

  add_custom_target(target ALL DEPENDS ${OUTPUT})

  install(
    CODE
    "execute_process(WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} COMMAND ${PYTHON} ${SETUP_PY} install --skip-build --prefix=${CMAKE_INSTALL_PREFIX})"
    )
endif(PYTHON)

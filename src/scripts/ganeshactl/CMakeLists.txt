# Building and packaging DBUS scripts for ganesha

find_program(PYTHON "python")

# PyQT based class modules
set(GANESHA_BASE_SRCS
  Ganesha/__init__.py
  Ganesha/admin.py
  Ganesha/client_mgr.py
  Ganesha/QtUI/clients_table.py
  Ganesha/export_mgr.py
  Ganesha/QtUI/exports_table.py
  Ganesha/log_mgr.py
  Ganesha/QtUI/log_settings.py
  Ganesha/io_stats.py
  Ganesha/glib_dbus_stats.py
  Ganesha/ganesha_mgr_utils.py
  Ganesha/config_editor.py
  )

# Command line scripts
set(SCRIPT_SRC
  fake_recall.py
  get_clientids.py
  grace_period.py
  ganesha_mgr.py
  ganesha_stats.py
  ganesha_conf.py
  )

set(GUI_SCRIPT_SRC
  ganeshactl.py
  ganesha-admin.py
  manage_clients.py
  manage_logger.py
  manage_exports.py
  client_stats_9pOps.py
  export_stats_9pOps.py
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
  set(GANESHA_SRCS)
  set(UI_PY_FILES)
  if(USE_GUI_ADMIN_TOOLS)
    set(GANESHA_SRCS ${GANESHA_BASE_SRCS})
    foreach(ui_file ${UI_SRC})
      string(REPLACE ".ui" ".py" py_file ${ui_file})
      add_custom_command(
        OUTPUT ${py_file}
        COMMAND mkdir -p build/lib/Ganesha/QtUI
        COMMAND ${PYUIC} "${CMAKE_CURRENT_SOURCE_DIR}/${ui_file}" -o "build/lib/${py_file}"
        DEPENDS ${ui_file}
        )
      set(UI_PY_FILES ${UI_PY_FILES} "${py_file}")
      list(APPEND GANESHA_SRCS ${py_file})
    endforeach()
  endif()

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

  if(USE_GUI_ADMIN_TOOLS)
    foreach(src_file ${GUI_SCRIPT_SRC})
      string(REPLACE ".py" "" script_file ${src_file})
      add_custom_command(
        OUTPUT ${script_file}
        COMMAND cp "${CMAKE_CURRENT_SOURCE_DIR}/${src_file}" ${script_file}
        DEPENDS ${src_file}
        )
      list(APPEND SCRIPTS ${script_file})
    endforeach()
  endif()

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

  if(USE_GUI_ADMIN_TOOLS)
    foreach(script_py ${GUI_SCRIPT_SRC})
      string(REPLACE ".py" "" script ${script_py})
      if("${SCRIPTS_STRING}" STREQUAL "")
        set(SCRIPTS_STRING "'${script}'")
      else()
        set(SCRIPTS_STRING "${SCRIPTS_STRING}, '${script}'")
      endif()
    endforeach()
  endif()

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
    "execute_process(WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR} COMMAND ${PYTHON} ${SETUP_PY} install --skip-build --prefix=\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX})"
    )
endif(PYTHON)

# Man page
set(man8_file ganesha_conf.man)
set(man8_page ganesha_conf.8)
add_custom_command(
	OUTPUT ${man8_page}
	COMMAND cp "${CMAKE_CURRENT_SOURCE_DIR}/${man8_file}" ${man8_page}
	DEPENDS ${man8_file}
	)
add_custom_target(man ALL DEPENDS ${man8_page})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${man8_page}
	DESTINATION ${CMAKE_INSTALL_PREFIX}/share/man/man8/)

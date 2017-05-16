set(sphinx_output_dir ${CMAKE_BINARY_DIR}/doc)

set(man_srcs
    ganesha-config.rst
    ganesha-9p-config.rst
    ganesha-ceph-config.rst
    ganesha-log-config.rst
    ganesha-gluster-config.rst
    ganesha-gpfs-config.rst
    ganesha-gpfs-config.rst
    ganesha-proxy-config.rst
    ganesha-rgw-config.rst
    ganesha-vfs-config.rst
    ganesha-xfs-config.rst
    ganesha-zfs-config.rst
    ganesha-cache-config.rst
    ganesha-export-config.rst
    ganesha-core-config.rst)

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


add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

########### next target ###############

SET(fsalproxy_v4_LIB_SRCS
   handle.c
   main.c
   export.c
   xattrs.c
)

if(PROXYV4_HANDLE_MAPPING)
  SET(fsalproxy_v4_LIB_SRCS
    ${fsalproxy_LIB_SRCS}
    handle_mapping/handle_mapping.c
    handle_mapping/handle_mapping_db.c
    )
endif(PROXYV4_HANDLE_MAPPING)

add_library(fsalproxy_v4 MODULE ${fsalproxy_v4_LIB_SRCS})
add_sanitizers(fsalproxy_v4)

target_link_libraries(fsalproxy_v4
  ganesha_nfsd
  ${SYSTEM_LIBRARIES}
  "-Wl,--no-undefined"
)

if(PROXYV4_HANDLE_MAPPING)
  target_link_libraries(fsalproxy_v4 sqlite3)
endif(PROXYV4_HANDLE_MAPPING)

set_target_properties(fsalproxy_v4 PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalproxy_v4 COMPONENT fsal DESTINATION ${FSAL_DESTINATION})

########### install files ###############

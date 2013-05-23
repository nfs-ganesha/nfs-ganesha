add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

########### next target ###############

SET(fsalproxy_LIB_SRCS
   handle.c
   proxy.c
   export.c
   xattrs.c
)

if(ENABLE_HANDLE_MAPPING)
  SET(fsalproxy_LIB_SRCS
    ${fsalproxy_LIB_SRCS}
    handle_mapping/handle_mapping.c
    handle_mapping/handle_mapping_db.c
    )
endif(ENABLE_HANDLE_MAPPING)

add_library(fsalproxy SHARED ${fsalproxy_LIB_SRCS})

target_link_libraries(fsalproxy
  ${LIBTIRPC_LIBRARIES}
  ${SYSTEM_LIBRARIES})
if(ENABLE_HANDLE_MAPPING)
  target_link_libraries(fsalproxy sqlite3)
endif(ENABLE_HANDLE_MAPPING)

set_target_properties(fsalproxy PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalproxy COMPONENT fsal DESTINATION  ${FSAL_DESTINATION} )


########### install files ###############

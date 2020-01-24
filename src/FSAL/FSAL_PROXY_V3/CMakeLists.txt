add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

########### next target ###############

SET(fsalproxy_v3_LIB_SRCS
  main.c
  nlm.c
  rpc.c
  utils.c
)

add_library(fsalproxy_v3 MODULE ${fsalproxy_v3_LIB_SRCS} $<TARGET_OBJECTS:nfs_mnt_xdr>)
add_sanitizers(fsalproxy_v3)

target_link_libraries(fsalproxy_v3
  ganesha_nfsd
  ${SYSTEM_LIBRARIES}
  "-Wl,--no-undefined"
)

set_target_properties(fsalproxy_v3 PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalproxy_v3 COMPONENT fsal DESTINATION  ${FSAL_DESTINATION} )

########### install files ###############

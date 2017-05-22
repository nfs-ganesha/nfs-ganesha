add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

set( LIB_PREFIX 64)

########### next target ###############

SET(fsalscality_LIB_SRCS
   handle.c
   file.c
   xattrs.c
   scality_methods.h
   main.c
   export.c
   dbd_rest_client.c
   dbd_rest_client.h
   sproxyd_client.c
   sproxyd_client.h
   redis_client.c
   redis_client.h
   random.c
   random.h
  )

add_library(fsalscality SHARED ${fsalscality_LIB_SRCS})

target_link_libraries(fsalscality
  ${SYSTEM_LIBRARIES}
  -lcurl
  -ljansson
  -lhiredis
)

set_target_properties(fsalscality PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalscality COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )






########### install files ###############

add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

set( LIB_PREFIX 64)

########### next target ###############

SET(fsalmem_LIB_SRCS
   mem_export.c
   mem_handle.c
   mem_int.h
   mem_main.c
   mem_up.c
)

add_library(fsalmem SHARED ${fsalmem_LIB_SRCS})
add_sanitizers(fsalmem)

target_link_libraries(fsalmem
    ganesha_nfsd
    ${SYSTEM_LIBRARIES}
    ${LTTNG_LIBRARIES})

set_target_properties(fsalmem PROPERTIES VERSION 4.2.0 SOVERSION 4)
install(TARGETS fsalmem COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )

########### install files ###############

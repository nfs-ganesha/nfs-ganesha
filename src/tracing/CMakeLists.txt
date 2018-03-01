include_directories(
  ${LTTNG_INCLUDE_DIR}
)

set(ganesha_trace_LIB_SRCS
  fsal_mem.c
  logger.c
  mdcache.c
  nfs4.c
  nfs_rpc.c
  state.c
)

add_library(ganesha_trace MODULE ${ganesha_trace_LIB_SRCS})
add_sanitizers(ganesha_trace)

target_link_libraries(ganesha_trace
  ${LTTNG_LIBRARIES}
)

install(TARGETS ganesha_trace COMPONENT tracing DESTINATION ${FSAL_DESTINATION} )

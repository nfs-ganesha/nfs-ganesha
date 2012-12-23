set(nodedb_SRCS
  connection.c
  connectionpool.c
  encoding.c
  fastdb.c
  interface.c
  nodedb.c
  redblack.c
  scanmount.c
  sockbuf.c
)

include_directories(
    "${PROJECT_SOURCE_DIR}/NodeDB"
)

ADD_CUSTOM_COMMAND(
  OUTPUT    interface.c
  COMMAND   ./marshal
  ARGS      -i ${CMAKE_CURRENT_SOURCE_DIR}/nodedb.c -oc interface.c -oi ${PROJECT_BINARY_DIR}/include/interface.h -oo ${PROJECT_BINARY_DIR}/include/ops.h
  DEPENDS   marshal ${CMAKE_CURRENT_SOURCE_DIR}/nodedb.c
  COMMENT   "Auto-generating marshaling code"
)

ADD_CUSTOM_COMMAND(
  OUTPUT    marshal
  COMMAND "${CMAKE_C_COMPILER}"
  ARGS -o marshal ${CMAKE_CURRENT_SOURCE_DIR}/marshal.c
  DEPENDS   ${CMAKE_CURRENT_SOURCE_DIR}/marshal.c
  COMMENT   "Building marshaller compiler"
)

add_library(nodedb STATIC ${nodedb_SRCS})

# This is GCC specific to force PIC compiles.
# cmake 2.8.9 has a portable POSITION_INDEPENDENT_CODE property that can be
# used when it is available
set_target_properties(nodedb PROPERTIES COMPILE_FLAGS "-fPIC")

set(nodedb_SRCS
  nodedb/connection.c
  nodedb/connectionpool.c
  nodedb/encoding.c
  nodedb/fastdb.c
  interface.c
  nodedb/nodedb.c
  nodedb/redblack.c
  nodedb/scanmount.c
  nodedb/sockbuf.c
)

include_directories(
    "${PROJECT_SOURCE_DIR}/NodeDB/nodedb"
)

ADD_CUSTOM_COMMAND(
  OUTPUT    interface.c
  COMMAND   ./marshal
  ARGS      -i ${CMAKE_CURRENT_SOURCE_DIR}/nodedb/nodedb.c -oc interface.c -oi ${PROJECT_BINARY_DIR}/include/interface.h -oo ${PROJECT_BINARY_DIR}/include/ops.h
  DEPENDS   marshal ${CMAKE_CURRENT_SOURCE_DIR}/nodedb/nodedb.c
  COMMENT   "Auto-generating marshaling code"
)

ADD_CUSTOM_COMMAND(
  OUTPUT    marshal
  COMMAND "${CMAKE_C_COMPILER}"
  ARGS -o marshal ${CMAKE_CURRENT_SOURCE_DIR}/nodedb/marshal.c
  DEPENDS   ${CMAKE_CURRENT_SOURCE_DIR}/nodedb/marshal.c
  COMMENT   "Building marshaller compiler"
)

add_library(nodedb STATIC ${nodedb_SRCS})

# This is GCC specific to force PIC compiles.
# cmake 2.8.9 has a portable POSITION_INDEPENDENT_CODE property that can be
# used when it is available
set_target_properties(nodedb PROPERTIES COMPILE_FLAGS "-fPIC")

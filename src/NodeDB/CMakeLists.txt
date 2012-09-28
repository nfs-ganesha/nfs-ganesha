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

add_library(nodedb STATIC ${nodedb_SRCS})

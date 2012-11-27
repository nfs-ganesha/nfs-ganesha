include_directories(
  ${LIBTIRPC_INCLUDE_DIR}
)

########### next target ###############

SET(idmap_STAT_SRCS
   idmapper.c
   idmapper_cache.c
)

add_library(idmap STATIC ${idmap_STAT_SRCS})

target_link_libraries(idmap
  hash
)


########### install files ###############

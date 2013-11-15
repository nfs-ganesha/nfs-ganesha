include_directories(
  ${LIBTIRPC_INCLUDE_DIR}
)

########### next target ###############

SET(rpcal_STAT_SRCS
   nfs_dupreq.c
   rpc_tools.c
)

if(_HAVE_GSSAPI)
  set(rpcal_STAT_SRCS ${rpcal_STAT_SRCS} gss_credcache.c gss_extra.c)
endif(_HAVE_GSSAPI)

add_library(rpcal STATIC ${rpcal_STAT_SRCS})

########### install files ###############

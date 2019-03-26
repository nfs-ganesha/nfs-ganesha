########### next target ###############

SET(rpcal_STAT_SRCS
   nfs_dupreq.c
   rpc_tools.c
)

if(_HAVE_GSSAPI)
  set(rpcal_STAT_SRCS ${rpcal_STAT_SRCS} gss_credcache.c gss_extra.c)
endif(_HAVE_GSSAPI)

add_library(rpcal OBJECT ${rpcal_STAT_SRCS})
add_sanitizers(rpcal)
set_target_properties(rpcal PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############

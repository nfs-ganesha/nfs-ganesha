include_directories(
  ${LIBTIRPC_INCLUDE_DIR}
)

add_subdirectory(gssd)


########### next target ###############

SET(rpcal_STAT_SRCS
   nfs_dupreq.c
   rpc_tools.c
)

if(_HAVE_GSSAPI)
  set(rpcal_STAT_SRCS ${rpcal_STAT_SRCS} gss_extra.c)
endif(_HAVE_GSSAPI)

add_library(rpcal STATIC ${rpcal_STAT_SRCS})

target_link_libraries(rpcal
  ntirpc
  gssd
  hash
)


########### next target ###############

SET(test_rpctools_SRCS
   test_rpctools.c
)

add_executable(test_rpctools EXCLUDE_FROM_ALL ${test_rpctools_SRCS})

target_link_libraries(test_rpctools rpcal hashtable ${CMAKE_THREAD_LIBS_INIT})


########### install files ###############

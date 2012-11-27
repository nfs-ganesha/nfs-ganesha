include_directories(
  ${LIBTIRPC_INCLUDE_DIR}
)

########### next target ###############

SET(fsalup_STAT_SRCS
   fsal_up_thread.c
   fsal_up_top.c
   fsal_up_utils.c
)

add_library(fsalup STATIC ${fsalup_STAT_SRCS})

target_link_libraries(fsalup
  nfs4callbacks
)

########### install files ###############

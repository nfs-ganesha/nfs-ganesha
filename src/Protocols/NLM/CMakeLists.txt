include_directories(
  ${LIBTIRPC_INCLUDE_DIR}
)

########### next target ###############

SET(nlm_STAT_SRCS
   nlm_Cancel.c
   nlm_Free_All.c
   nlm_Granted_Res.c
   nlm_Lock.c
   nlm_Null.c
   nlm_Share.c
   nlm_Sm_Notify.c
   nlm_Test.c
   nlm_Unlock.c
   nlm_Unshare.c
   nlm_async.c
   nlm_util.c
   nsm.c
)

add_library(nlm STATIC ${nlm_STAT_SRCS})

########### install files ###############

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

add_library(nlm OBJECT ${nlm_STAT_SRCS})
add_sanitizers(nlm)
set_target_properties(nlm PROPERTIES COMPILE_FLAGS "-fPIC")

########### install files ###############

########### next target ###############

SET(sal_STAT_SRCS
   state_async.c
   state_lock.c
   state_share.c
   state_misc.c
   state_layout.c
   state_deleg.c
   nfs4_clientid.c
   nfs4_state.c
   nfs4_state_id.c
   nfs4_lease.c
   nfs4_recovery.c
   nfs41_session_id.c
   nfs4_owner.c
   recovery/recovery_fs.c
   recovery/recovery_fs_ng.c
)

if(USE_NLM)
  set(sal_STAT_SRCS
    ${sal_STAT_SRCS}
    nlm_owner.c
    nlm_state.c
    )
endif(USE_NLM)

if(USE_9P)
  set(sal_STAT_SRCS
    ${sal_STAT_SRCS}
    9p_owner.c
    )
endif(USE_9P)

if(USE_RADOS_RECOV)
  set(sal_STAT_SRCS
    ${sal_STAT_SRCS}
    recovery/recovery_rados_kv.c
    recovery/recovery_rados_ng.c
    recovery/recovery_rados_cluster.c
    )
endif(USE_RADOS_RECOV)

add_library(sal STATIC ${sal_STAT_SRCS})
add_sanitizers(sal)

if(USE_RADOS_RECOV)
  include_directories(${RADOS_INCLUDE_DIR})
  target_link_libraries(sal rados_grace ${RADOS_LIBRARIES})
endif(USE_RADOS_RECOV)

########### install files ###############

include_directories(
  ${LIBTIRPC_INCLUDE_DIR}
)

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
   nlm_owner.c
)

if(USE_9P)
  set(sal_STAT_SRCS
    ${sal_STAT_SRCS}
    9p_owner.c
    )
endif(USE_9P)

add_library(sal STATIC ${sal_STAT_SRCS})


########### install files ###############

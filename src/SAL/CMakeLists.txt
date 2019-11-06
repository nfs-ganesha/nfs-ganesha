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

add_library(sal OBJECT ${sal_STAT_SRCS})
add_sanitizers(sal)
set_target_properties(sal PROPERTIES COMPILE_FLAGS "-fPIC")

if(USE_RADOS_RECOV)
  set(sal_RADOS_SRCS
    recovery/recovery_rados_kv.c
    recovery/recovery_rados_ng.c
    recovery/recovery_rados_cluster.c
    ../support/rados_grace.c
    )
  add_library(ganesha_rados_recov MODULE ${sal_RADOS_SRCS})
  add_sanitizers(ganesha_rados_recov)
  target_link_libraries(ganesha_rados_recov
    ganesha_nfsd
    ${SYSTEM_LIBRARIES}
    ${RADOS_LIBRARIES}
    "-Wl,--no-undefined")
  include_directories(${RADOS_INCLUDE_DIR})
  set_target_properties(ganesha_rados_recov PROPERTIES SOVERSION
    "${GANESHA_MAJOR_VERSION}${GANESHA_MINOR_VERSION}")
  install(TARGETS ganesha_rados_recov LIBRARY DESTINATION ${LIB_INSTALL_DIR})
endif(USE_RADOS_RECOV)

########### install files ###############

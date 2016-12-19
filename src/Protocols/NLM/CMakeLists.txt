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
add_sanitizers(nlm)

########### next target ###############

if(LINUX)
   SET(sm_notify_SRCS
      sm_notify.c
   )

   add_executable(sm_notify.ganesha ${sm_notify_SRCS})
   add_sanitizers(sm_notify.ganesha)

   target_link_libraries(sm_notify.ganesha
      ${PROTOCOLS}
      ${LIBTIRPC_LIBRARIES}
      ${SYSTEM_LIBRARIES}
   )

   #target_link_libraries(sm_notify.ganesha ${CMAKE_THREAD_LIBS_INIT})

   if( USE_ADMIN_TOOLS )
      install(TARGETS sm_notify.ganesha DESTINATION bin)
   endif( USE_ADMIN_TOOLS )
endif(LINUX)

install(TARGETS COMPONENT daemon DESTINATION bin)

########### install files ###############

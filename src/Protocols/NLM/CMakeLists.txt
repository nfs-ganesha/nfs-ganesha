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

########### next target ###############

SET(sm_notify_SRCS
   sm_notify.c
)

add_executable(sm_notify.ganesha ${sm_notify_SRCS})

target_link_libraries(sm_notify.ganesha
  ${PROTOCOLS}
  ${LIBTIRPC_LIBRARIES}
  ${SYSTEM_LIBRARIES}
)

#target_link_libraries(sm_notify.ganesha ${CMAKE_THREAD_LIBS_INIT})

install(TARGETS sm_notify.ganesha COMPONENT daemon DESTINATION bin)

########### install files ###############

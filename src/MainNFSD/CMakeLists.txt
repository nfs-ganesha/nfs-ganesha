add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

if(USE_DBUS)
  include_directories(
    ${DBUS_INCLUDE_DIRS}
    )
endif(USE_DBUS)

########### next target ###############

SET(MainServices_STAT_SRCS
   nfs_admin_thread.c
   nfs_rpc_callback.c
   nfs_worker_thread.c
   nfs_rpc_dispatcher_thread.c
   nfs_rpc_tcp_socket_manager_thread.c
   nfs_init.c
   nfs_lib.c
   nfs_reaper_thread.c
   ../support/client_mgr.c
)

if(USE_9P)
  SET(MainServices_STAT_SRCS
    ${MainServices_STAT_SRCS}
    9p_dispatcher.c)
endif(USE_9P)

if(USE_9P AND USE_9P_RDMA)
  SET(MainServices_STAT_SRCS
    ${MainServices_STAT_SRCS}
    9p_rdma_dispatcher.c
    9p_rdma_callbacks.c)
endif(USE_9P AND USE_9P_RDMA)

if(USE_NFS_RDMA)
  add_definitions(-D_USE_NFS_RDMA)
endif(USE_NFS_RDMA)

if(USE_CB_SIMULATOR)
  SET(MainServices_STAT_SRCS
    ${MainServices_STAT_SRCS}
    nfs_rpc_callback_simulator.c)
endif(USE_CB_SIMULATOR)

if(USE_UPCALL_SIMULATOR)
  SET(MainServices_STAT_SRCS
    ${MainServices_STAT_SRCS}
    fsal_upcall_simulator_thread.c
    )
endif(USE_UPCALL_SIMULATOR)

if(USE_CB_SIMULATOR)
  SET(MainServices_STAT_SRCS
    ${MainServices_STAT_SRCS}
    nfs_rpc_callback_simulator.c
    )
endif(USE_CB_SIMULATOR)

add_library(MainServices STATIC ${MainServices_STAT_SRCS})
add_sanitizers(MainServices)

# FSAL core sources
# fsal_manager and fsal_destroyer are the only objects referenced by the
# core server all the rest are for the common support of fsal plugins.

set(fsal_CORE_SRCS
   ../FSAL/fsal_convert.c
   ../FSAL/commonlib.c
   ../FSAL/fsal_manager.c
   ../FSAL/access_check.c
   ../FSAL/fsal_config.c
   ../FSAL/default_methods.c
   ../FSAL/common_pnfs.c
   ../FSAL/fsal_destroyer.c
   ../FSAL/fsal_helper.c
   ../FSAL_UP/fsal_up_top.c
   ../FSAL_UP/fsal_up_async.c
)

if (USE_ACL_MAPPING)
  set(fsal_CORE_SRCS
      ${fsal_CORE_SRCS}
      ../FSAL/posix_acls.c
  )
endif(USE_ACL_MAPPING)

add_library(FsalCore STATIC ${fsal_CORE_SRCS})
add_sanitizers(FsalCore)

########### next target ###############

SET(ganesha.nfsd_SRCS
   nfs_main.c
)

add_executable(ganesha.nfsd
  ${ganesha.nfsd_SRCS}
  ${fsal_CORE_SRCS}
)
add_sanitizers(ganesha.nfsd)

target_link_libraries(ganesha.nfsd
  MainServices
  ${PROTOCOLS}
  ${GANESHA_CORE}
  config_parsing
  ${LIBTIRPC_LIBRARIES}
  ${SYSTEM_LIBRARIES}
  ${LTTNG_LIBRARIES}
)

install(TARGETS ganesha.nfsd COMPONENT daemon DESTINATION bin)


########### install files ###############

# We are still missing the install of docs and stuff

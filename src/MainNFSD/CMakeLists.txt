add_definitions(
  -D__USE_GNU
  -D_GNU_SOURCE
)

include_directories(
  ${PROJECT_SOURCE_DIR}/RPCAL/gssd
  ${LIBTIRPC_INCLUDE_DIR}
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
   nfs_reaper_thread.c
   ../Protocols/client_mgr.c
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

# FSAL core sources
# fsal_manager and fsal_destroyer are the only objects referenced by the
# core server all the rest are for the common support of fsal plugins.
# we put them here in the core rather than in a lib so that they are always
# present and available for runtime linkage.

set(fsal_CORE_SRCS
   ../FSAL/fsal_errors.c
   ../FSAL/fsal_convert.c
   ../FSAL/fsal_commonlib.c
   ../FSAL/fsal_manager.c
   ../FSAL/access_check.c
   ../FSAL/common_functions.c
   ../FSAL/fsal_config.c
   ../FSAL/default_methods.c
   ../FSAL/common_pnfs.c
   ../FSAL/fsal_destroyer.c
   ../FSAL_UP/fsal_up_top.c
   ../FSAL_UP/fsal_up_async.c
   ../FSAL_UP/fsal_up_utils.c
)

########### next target ###############

SET(ganesha.nfsd_SRCS
   nfs_main.c
)

add_executable(ganesha.nfsd
  ${ganesha.nfsd_SRCS}
  ${fsal_CORE_SRCS}
)

target_link_libraries(ganesha.nfsd
  MainServices
  ${PROTOCOLS}
  ${GANESHA_CORE}
  ConfigParsing
  ${LIBTIRPC_LIBRARIES}
  ${SYSTEM_LIBRARIES}
)

if(USE_9P AND USE_9P_RDMA)
  target_link_libraries(ganesha.nfsd  mooshika ibverbs rdmacm )
endif(USE_9P AND USE_9P_RDMA)


install(TARGETS ganesha.nfsd COMPONENT daemon DESTINATION bin)


########### install files ###############

# We are still missing the install of docs and stuff

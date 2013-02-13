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
    ${DBUS_INCLUDE_DIR}
    ${DBUS_ARCH_INCLUDE_DIR}
    )
endif(USE_DBUS)

########### next target ###############

SET(MainServices_STAT_SRCS
   nfs_admin_thread.c
   nfs_stats_thread.c
   nfs_rpc_callback.c
   nfs_worker_thread.c
   nfs_tcb.c
   nfs_rpc_dispatcher_thread.c
   nfs_rpc_tcp_socket_manager_thread.c
   nfs_init.c
   nfs_tools.c
   nfs_reaper_thread.c
)

if(USE_DBUS)
  SET(MainServices_STAT_SRCS
    ${MainServices_STAT_SRCS}
    ../Protocols/DBUS/dbus_server.c)
endif(USE_DBUS)

if(USE_STAT_EXPORTER)
  SET(MainServices_STAT_SRCS
    ${MainServices_STAT_SRCS}
    nfs_stat_exporter_thread.c)
endif(USE_STAT_EXPORTER)

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

if(USE_SNMP_ADM)
  SET(MainServices_STAT_SRCS
    ${MainServices_STAT_SRCS}
    nfs_stats_snmp.c)
endif(USE_SNMP_ADM)

if(USE_STAT_EXPORTER)
  SET(MainServices_STAT_SRCS
    ${MainServices_STAT_SRCS}
    nfs_stat_exporter_thread.c
    )
endif(USE_STAT_EXPORTER)

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
# fsal_manager is the only object referenced by the core server
# all the rest are for the common support of fsal plugins.
# we put them here in the core rather than in a lib so that they
# are always present and available for runtime linkage.

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
)

########### next target ###############

if(USE_FSAL_FUSE)
  SET(ganeshaNFS_LIB_SRCS
    fuse_binding.c
    ${MainServices_STAT_SRCS}
    )

  add_library(ganeshaNFS SHARED
    ${ganeshaNFS_LIB_SRCS}
    ${fsal_CORE_SRCS}
    )

  target_link_libraries(ganeshaNFS ${CMAKE_THREAD_LIBS_INIT})

  set_target_properties(ganeshaNFS PROPERTIES VERSION 4.2.0 SOVERSION 4)
  install(TARGETS ganeshaNFS COMPONENT daemon DESTINATION lib)
endif(USE_FSAL_FUSE)

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

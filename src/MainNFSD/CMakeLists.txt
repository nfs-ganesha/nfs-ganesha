# SPDX-License-Identifier: LGPL-3.0-or-later
#-------------------------------------------------------------------------------
#
# Copyright Panasas, 2012
# Contributor: Jim Lieb <jlieb@panasas.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 3 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
#
#-------------------------------------------------------------------------------
add_definitions(
  -D__USE_GNU
)

if(USE_DBUS)
  include_directories(
    ${DBUS_INCLUDE_DIRS}
    )
endif(USE_DBUS)

if(RADOS_URLS)
  include_directories(
	  ${RADOS_INCLUDE_DIR}
    )
endif(RADOS_URLS)
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

add_library(MainServices OBJECT ${MainServices_STAT_SRCS})
add_sanitizers(MainServices)
set_target_properties(MainServices PROPERTIES COMPILE_FLAGS "-fPIC")

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

if (GSH_CAN_HOST_LOCAL_FS)
  set(fsal_CORE_SRCS
      ${fsal_CORE_SRCS}
      ../FSAL/localfs.c
  )
endif(GSH_CAN_HOST_LOCAL_FS)

if (USE_ACL_MAPPING)
  set(fsal_CORE_SRCS
      ${fsal_CORE_SRCS}
      ../FSAL/posix_acls.c
  )
endif(USE_ACL_MAPPING)

set(ganesha_nfsd_OBJS
    $<TARGET_OBJECTS:MainServices>
    $<TARGET_OBJECTS:avltree>
    $<TARGET_OBJECTS:cidr>
    $<TARGET_OBJECTS:config_parsing>
    $<TARGET_OBJECTS:hashtable>
    $<TARGET_OBJECTS:idmap>
    $<TARGET_OBJECTS:log>
    $<TARGET_OBJECTS:gos>
    $<TARGET_OBJECTS:rpcal>
    $<TARGET_OBJECTS:sal>
    $<TARGET_OBJECTS:support>
    $<TARGET_OBJECTS:string_utils>
    $<TARGET_OBJECTS:hash>
    $<TARGET_OBJECTS:uid2grp>
    $<TARGET_OBJECTS:netgroup_cache>
    $<TARGET_OBJECTS:nfsproto>
    $<TARGET_OBJECTS:nfs4callbacks>
    $<TARGET_OBJECTS:nfs_mnt_xdr>
    $<TARGET_OBJECTS:fsalpseudo>
    $<TARGET_OBJECTS:fsalmdcache>
)

if(USE_DBUS)
  set(ganesha_nfsd_OBJS
    ${ganesha_nfsd_OBJS}
    $<TARGET_OBJECTS:gshdbus>
  )
endif(USE_DBUS)

if(USE_NLM)
  set(ganesha_nfsd_OBJS
    ${ganesha_nfsd_OBJS}
    $<TARGET_OBJECTS:nlm>
  )
endif(USE_NLM)

if(USE_RQUOTA)
  set(ganesha_nfsd_OBJS
    ${ganesha_nfsd_OBJS}
    $<TARGET_OBJECTS:rquota>
  )
endif(USE_RQUOTA)

if(USE_NFSACL3)
  set(ganesha_nfsd_OBJS
    ${ganesha_nfsd_OBJS}
  $<TARGET_OBJECTS:nfsacl>
  )
endif(USE_NFSACL3)

if(USE_9P)
  set(ganesha_nfsd_OBJS
    ${ganesha_nfsd_OBJS}
    $<TARGET_OBJECTS:9p>
  )
endif(USE_9P)

add_library(ganesha_nfsd SHARED
  ${ganesha_nfsd_OBJS} ${fsal_CORE_SRCS}
)

add_sanitizers(ganesha_nfsd)

target_link_libraries(ganesha_nfsd
  ${LIBTIRPC_LIBRARIES}
  ${SYSTEM_LIBRARIES}
  ${LTTNG_LIBRARIES}
  ${MOOSHIKA_LIBRARIES}
)

if(SANITIZE_ADDRESS)
  set(undef_flags ${LDFLAG_ALLOW_UNDEF})
else(SANITIZE_ADDRESS)
  set(undef_flags ${LDFLAG_DISALLOW_UNDEF})
endif(SANITIZE_ADDRESS)

if(LINUX OR FREEBSD)
  # Apple's /bin/ld does not support the --version-script flag.
  set(ver_script_flags "-Wl,--version-script=${GANESHA_TOP_CMAKE_DIR}/MainNFSD/libganesha_nfsd.ver")
endif(LINUX OR FREEBSD)

set_target_properties(ganesha_nfsd PROPERTIES LINK_FLAGS "${undef_flags} ${ver_script_flags}")

set_target_properties(ganesha_nfsd PROPERTIES
  VERSION "${GANESHA_MAJOR_VERSION}${GANESHA_MINOR_VERSION}"
  SOVERSION "${GANESHA_MAJOR_VERSION}${GANESHA_MINOR_VERSION}"
)
install(TARGETS ganesha_nfsd LIBRARY DESTINATION ${LIB_INSTALL_DIR})

#install(TARGETS ganesha.nfsd COMPONENT daemon DESTINATION bin)

########### install files ###############

# We are still missing the install of docs and stuff

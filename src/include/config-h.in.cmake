/* config.h file expanded by Cmake for build */

#ifndef CONFIG_H
#define CONFIG_H

#define GANESHA_VERSION_MAJOR @GANESHA_MAJOR_VERSION@
#define GANESHA_VERSION_MINOR @GANESHA_MINOR_VERSION@
#define GANESHA_PATCH_LEVEL @GANESHA_PATCH_LEVEL@
#define GANESHA_EXTRA_VERSION @GANESHA_EXTRA_VERSION@
#define GANESHA_VERSION "@GANESHA_VERSION@"
#define GANESHA_BUILD_RELEASE @GANESHA_BUILD_RELEASE@

#define VERSION GANESHA_VERSION
#define VERSION_COMMENT "@VERSION_COMMENT@"
#define _GIT_HEAD_COMMIT "@_GIT_HEAD_COMMIT@"
#define _GIT_DESCRIBE "@_GIT_DESCRIBE@"
#define BUILD_HOST "@BUILD_HOST_NAME@"
#define FSAL_MODULE_LOC "@FSAL_DESTINATION@"
/* Build controls */

#cmakedefine _MSPAC_SUPPORT 1
#cmakedefine USE_NFSIDMAP 1
#cmakedefine USE_DBUS 1
#cmakedefine _USE_CB_SIMULATOR 1
#cmakedefine USE_CAPS 1
#cmakedefine USE_BLKID 1
#cmakedefine PROXY_HANDLE_MAPPING 1
#cmakedefine _USE_9P 1
#cmakedefine _USE_9P_RDMA 1
#cmakedefine _USE_NFS_RDMA 1
#cmakedefine _USE_NFS3 1
#cmakedefine _USE_NLM 1
#cmakedefine DEBUG_SAL 1
#cmakedefine _VALGRIND_MEMCHECK 1
#cmakedefine _NO_TCP_REGISTER 1
#cmakedefine RPCBIND 1
#cmakedefine HAVE_KRB5 1
#cmakedefine HAVE_HEIMDAL 1
#cmakedefine USE_GSS_KRB5_CCACHE_NAME 1
#cmakedefine LINUX 1
#cmakedefine FREEBSD 1
#cmakedefine _HAVE_GSSAPI 1
#cmakedefine HAVE_STRING_H 1
#cmakedefine HAVE_STRNLEN 1
#cmakedefine LITTLEEND 1
#cmakedefine HAVE_DAEMON 1
#cmakedefine USE_LTTNG 1
#cmakedefine ENABLE_VFS_DEBUG_ACL 1
#cmakedefine ENABLE_RFC_ACL 1
#cmakedefine USE_GLUSTER_XREADDIRPLUS 1
#cmakedefine USE_GLUSTER_UPCALL_REGISTER 1
#cmakedefine USE_FSAL_CEPH_MKNOD 1
#cmakedefine USE_FSAL_CEPH_SETLK 1
#cmakedefine USE_FSAL_CEPH_LL_LOOKUP_ROOT 1
#cmakedefine USE_FSAL_CEPH_STATX 1
#cmakedefine USE_FSAL_CEPH_LL_DELEGATION 1
#cmakedefine USE_FSAL_CEPH_LL_SYNC_INODE 1
#cmakedefine USE_CEPH_LL_FALLOCATE 1
#cmakedefine USE_FSAL_CEPH_ABORT_CONN 1
#cmakedefine USE_FSAL_RGW_MOUNT2 1
#cmakedefine ENABLE_LOCKTRACE 1
#cmakedefine SANITIZE_ADDRESS 1
#cmakedefine DEBUG_MDCACHE 1
#cmakedefine USE_RADOS_RECOV 1
#cmakedefine RADOS_URLS 1
#cmakedefine USE_LLAPI 1
#cmakedefine USE_GLUSTER_STAT_FETCH_API 1
#define NFS_GANESHA 1

#define GANESHA_CONFIG_PATH "@SYSCONFDIR@/ganesha/ganesha.conf"
#define GANESHA_PIDFILE_PATH "@SYSSTATEDIR@/run/ganesha.pid"
#define NFS_V4_RECOV_ROOT "@SYSSTATEDIR@/lib/nfs/ganesha"
/**
 * @brief Default value for krb5_param.ccache_dir
 */
#define DEFAULT_NFS_CCACHE_DIR "@SYSSTATEDIR@/run/ganesha"

/* We're LGPL'd */
#define _LGPL_SOURCE 1

#endif /* CONFIG_H */

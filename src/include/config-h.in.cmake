/* config.h file expanded by Cmake for build */

#ifndef CONFIG_H
#define CONFIG_H

#define GANESHA_VERSION_MAJOR @GANESHA_MAJOR_VERSION@
#define GANESHA_VERSION_MINOR @GANESHA_MINOR_VERSION@
#define GANESHA_PATCH_LEVEL @GANESHA_PATCH_LEVEL@
#define GANESHA_EXTRA_VERSION @GANESHA_EXTRA_VERSION@
#define GANESHA_VERSION "@GANESHA_VERSION@"

#define VERSION "@GANESHA_VERSION_MAJOR@.@GANESHA_VERSION_MINOR@@GANESHA_PATCH_LEVEL@@GANESHA_EXTRA_VERSION@"
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
#cmakedefine USE_FSAL_SHOOK 1
#cmakedefine USE_FSAL_LUSTRE_UP 1
#cmakedefine DEBUG_SAL 1
#cmakedefine _VALGRIND_MEMCHECK 1
#cmakedefine _NO_MOUNT_LIST 1
#cmakedefine HAVE_STDBOOL_H 1
#cmakedefine HAVE_KRB5 1
#cmakedefine KRB5_VERSION @KRB5_VERSION@
#cmakedefine HAVE_HEIMDAL 1
#cmakedefine USE_GSS_KRB5_CCACHE_NAME 1
#cmakedefine LINUX 1
#cmakedefine FREEBSD 1
#cmakedefine APPLE 1
#cmakedefine _HAVE_GSSAPI 1
#cmakedefine HAVE_STRING_H 1
#cmakedefine HAVE_STRINGS_H 1
#cmakedefine HAVE_STRNLEN 1
#cmakedefine LITTLEEND 1
#cmakedefine BIGEND 1
#cmakedefine HAVE_XATTR_H 1
#cmakedefine HAVE_INCLUDE_LUSTREAPI_H 1
#cmakedefine HAVE_INCLUDE_LIBLUSTREAPI_H 1
#cmakedefine _LUSTRE_API_HEADER 1
#cmakedefine HAVE_DAEMON 1
#cmakedefine USE_LTTNG 1
#cmakedefine ENABLE_VFS_DEBUG_ACL 1
#cmakedefine ENABLE_RFC_ACL 1
#cmakedefine USE_GLUSTER_SYMLINK_MOUNT 1

#define NFS_GANESHA 1

#define GANESHA_CONFIG_PATH "@SYSCONFDIR@/ganesha/ganesha.conf"
#define GANESHA_PIDFILE_PATH "@SYSSTATEDIR@/run/ganesha.pid"
#define NFS_V4_RECOV_ROOT "@SYSSTATEDIR@/lib/nfs/ganesha"
/**
 * @brief Default value for krb5_param.ccache_dir
 */
#define DEFAULT_NFS_CCACHE_DIR "@SYSSTATEDIR@/run/ganesha"

#endif /* CONFIG_H */

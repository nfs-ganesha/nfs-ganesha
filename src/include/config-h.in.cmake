/* config.h file expanded by Cmake for build */

#ifndef CONFIG_H
#define CONFIG_H

#define GANESHA_VERSION_MAJOR @GANESHA_MAJOR_VERSION@
#define GANESHA_VERSION_MINOR @GANESHA_MINOR_VERSION@
#define GANESHA_PATCH_LEVEL @GANESHA_PATCH_LEVEL@

#define VERSION "${GANESHA_VERSION_MAJOR}.${GANESHA_VERSION_MINOR}.${GANESHA_PATCH_LEVEL}"
#define VERSION_COMMENT "@VERSION_COMMENT@"
#define _GIT_HEAD_COMMIT "@_GIT_HEAD_COMMIT@"
#define _GIT_DESCRIBE "@_GIT_DESCRIBE@"

/* Build controls */

#cmakedefine _MSPAC_SUPPORT 1
#cmakedefine USE_NFSIDMAP 1
#cmakedefine USE_DBUS 1
#cmakedefine _USE_CB_SIMULATOR 1
#cmakedefine USE_DBUS_STATS 1
#cmakedefine _HANDLE_MAPPING 1
#cmakedefine _USE_9P 1
#cmakedefine _USE_9P_RDMA 1
#cmakedefine DEBUG_SAL 1
#cmakedefine USE_NODELIST 1
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
#cmakedefine LITTLEEND 1
#cmakedefine BIGEND 1
#cmakedefine HAVE_XATTR_H 1

#define NFS_GANESHA 1

#endif /* CONFIG_H */

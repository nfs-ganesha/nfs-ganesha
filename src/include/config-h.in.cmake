/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright (C) Panasas Inc., 2011
 * Author: Jim Lieb jlieb@panasas.com
 *
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* config.h file expanded by Cmake for build */

#ifndef CONFIG_H
#define CONFIG_H

#define GSH_CHECK_VERSION(major,minor,micro,libmajor,libminor,libmicro) \
	((libmajor) > (major) || \
	((libmajor) == (major) && (libminor) > (minor)) || \
	((libmajor) == (major) && (libminor) == (minor) && (libmicro) >= (micro)))

#define GANESHA_VERSION_MAJOR @GANESHA_MAJOR_VERSION@
#define GANESHA_VERSION_MINOR @GANESHA_MINOR_VERSION@
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
#cmakedefine PROXYV4_HANDLE_MAPPING 1
#cmakedefine _USE_9P 1
#cmakedefine _USE_9P_RDMA 1
#cmakedefine _USE_NFS_RDMA 1
#cmakedefine _USE_NFS3 1
#cmakedefine _USE_NLM 1
#cmakedefine _USE_RQUOTA 1
#cmakedefine USE_NFSACL3 1
#cmakedefine DEBUG_SAL 1
#cmakedefine _VALGRIND_MEMCHECK 1
#cmakedefine _NO_TCP_REGISTER 1
#cmakedefine RPCBIND 1
#cmakedefine HAVE_KRB5 1
#cmakedefine HAVE_HEIMDAL 1
#cmakedefine USE_GSS_KRB5_CCACHE_NAME 1
#cmakedefine LINUX 1
#cmakedefine FREEBSD 1
#cmakedefine BSDBASED 1
#cmakedefine DARWIN 1
#cmakedefine _HAVE_GSSAPI 1
#cmakedefine HAVE_STRING_H 1
#cmakedefine HAVE_STRNLEN 1
#cmakedefine LITTLEEND 1
#cmakedefine HAVE_DAEMON 1
#cmakedefine USE_LTTNG 1
#cmakedefine HAVE_ACL_GET_FD_NP 1
#cmakedefine HAVE_ACL_SET_FD_NP 1
#cmakedefine ENABLE_VFS_DEBUG_ACL 1
#cmakedefine ENABLE_RFC_ACL 1
#cmakedefine CEPHFS_POSIX_ACL 1
#cmakedefine USE_GLUSTER_XREADDIRPLUS 1
#cmakedefine USE_GLUSTER_UPCALL_REGISTER 1
#cmakedefine USE_GLUSTER_DELEGATION 1
#cmakedefine GSH_CAN_HOST_LOCAL_FS 1
#cmakedefine USE_FSAL_CEPH_MKNOD 1
#cmakedefine USE_FSAL_CEPH_SETLK 1
#cmakedefine USE_FSAL_CEPH_LL_LOOKUP_ROOT 1
#cmakedefine USE_FSAL_CEPH_STATX 1
#cmakedefine USE_FSAL_CEPH_LL_DELEGATION 1
#cmakedefine USE_FSAL_CEPH_LL_SYNC_INODE 1
#cmakedefine USE_CEPH_LL_FALLOCATE 1
#cmakedefine USE_FSAL_CEPH_ABORT_CONN 1
#cmakedefine USE_FSAL_CEPH_RECLAIM_RESET 1
#cmakedefine USE_FSAL_CEPH_GET_FS_CID 1
#cmakedefine USE_FSAL_CEPH_REGISTER_CALLBACKS 1
#cmakedefine USE_FSAL_CEPH_LOOKUP_VINO 1
#cmakedefine USE_FSAL_RGW_MOUNT2 1
#cmakedefine USE_FSAL_RGW_XATTRS 1
#cmakedefine ENABLE_LOCKTRACE 1
#cmakedefine SANITIZE_ADDRESS 1
#cmakedefine DEBUG_MDCACHE 1
#cmakedefine USE_RADOS_RECOV 1
#cmakedefine RADOS_URLS 1
#cmakedefine USE_LLAPI 1
#cmakedefine USE_GLUSTER_STAT_FETCH_API 1
#cmakedefine HAVE_URCU_REF_GET_UNLESS_ZERO 1
#cmakedefine USE_BTRFSUTIL 1
#define NFS_GANESHA 1

#define GANESHA_CONFIG_PATH "@SYSCONFDIR@/ganesha/ganesha.conf"
#define GANESHA_PIDFILE_PATH "@RUNTIMEDIR@/ganesha.pid"
#define NFS_V4_RECOV_ROOT "@SYSSTATEDIR@/lib/nfs/ganesha"
#define NFS_V4_RECOV_DIR "v4recov"
#define NFS_V4_OLD_DIR "v4old"
/**
 * @brief Default value for krb5_param.ccache_dir
 */
#define DEFAULT_NFS_CCACHE_DIR "@RUNTIMEDIR@"

/* We're LGPL'd */
#define _LGPL_SOURCE 1

#endif /* CONFIG_H */

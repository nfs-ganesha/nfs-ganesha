// SPDX-License-Identifier: LGPL-3.0-or-later
/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
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
 * ---------------------------------------
 */

/**
 * @file  nfs_read_conf.c
 * @brief This file tables required for parsing the NFS specific parameters.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h>		/* for having FNDELAY */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include "log.h"
#include "gsh_rpc.h"
#include "fsal.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "nfs_file_handle.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "config_parsing.h"

/**
 * @brief Core configuration parameters
 */

static struct config_item_list udp_listener_type[] = {
	CONFIG_LIST_TOK("false", UDP_LISTENER_NONE),
	CONFIG_LIST_TOK("no", UDP_LISTENER_NONE),
	CONFIG_LIST_TOK("off", UDP_LISTENER_NONE),
	CONFIG_LIST_TOK("true", UDP_LISTENER_ALL),
	CONFIG_LIST_TOK("yes", UDP_LISTENER_ALL),
	CONFIG_LIST_TOK("on", UDP_LISTENER_ALL),
	CONFIG_LIST_TOK("mount", UDP_LISTENER_MOUNT),
	CONFIG_LIST_EOL
};

static struct config_item_list protocols[] = {
	CONFIG_LIST_TOK("none", CORE_OPTION_NONE),
#ifdef _USE_NFS3
	CONFIG_LIST_TOK("3", CORE_OPTION_NFSV3),
	CONFIG_LIST_TOK("v3", CORE_OPTION_NFSV3),
	CONFIG_LIST_TOK("nfs3", CORE_OPTION_NFSV3),
	CONFIG_LIST_TOK("nfsv3", CORE_OPTION_NFSV3),
#endif
	CONFIG_LIST_TOK("4", CORE_OPTION_NFSV4),
	CONFIG_LIST_TOK("v4", CORE_OPTION_NFSV4),
	CONFIG_LIST_TOK("nfs4", CORE_OPTION_NFSV4),
	CONFIG_LIST_TOK("nfsv4", CORE_OPTION_NFSV4),
#ifdef RPC_VSOCK
	CONFIG_LIST_TOK("nfsvsock", CORE_OPTION_NFS_VSOCK),
#endif
#ifdef _USE_NFS_RDMA
	CONFIG_LIST_TOK("nfsrdma", CORE_OPTION_NFS_RDMA),
	CONFIG_LIST_TOK("rpcrdma", CORE_OPTION_NFS_RDMA),
#endif
#ifdef _USE_9P
	CONFIG_LIST_TOK("9p", CORE_OPTION_9P),
#endif
	CONFIG_LIST_EOL
};

/**
 * @brief Support all protocols
 */
#ifdef _USE_NFS3
#define DEFAULT_INCLUDES_NFSV3		CORE_OPTION_NFSV3
#else
#define DEFAULT_INCLUDES_NFSV3		CORE_OPTION_NONE
#endif

#define DEFAULT_INCLUDES_NFSV4		CORE_OPTION_NFSV4

#define DEFAULT_PROTOCOLS  (DEFAULT_INCLUDES_NFSV3 | \
			    DEFAULT_INCLUDES_NFSV4)

static struct config_item core_params[] = {
	CONF_ITEM_UI16("NFS_Port", 0, UINT16_MAX, NFS_PORT,
		       nfs_core_param, port[P_NFS]),
#ifdef _USE_NFS3
	CONF_ITEM_UI16("MNT_Port", 0, UINT16_MAX, 0,
		       nfs_core_param, port[P_MNT]),
#endif
#ifdef _USE_NLM
	CONF_ITEM_UI16("NLM_Port", 0, UINT16_MAX, 0,
		       nfs_core_param, port[P_NLM]),
#endif
#ifdef _USE_RQUOTA
	CONF_ITEM_UI16("Rquota_Port", 0, UINT16_MAX, RQUOTA_PORT,
		       nfs_core_param, port[P_RQUOTA]),
#endif
	CONF_ITEM_IP_ADDR("Bind_Addr", "0.0.0.0",
			  nfs_core_param, bind_addr),
	CONF_ITEM_UI32("NFS_Program", 1, INT32_MAX, NFS_PROGRAM,
		       nfs_core_param, program[P_NFS]),
#ifdef _USE_NFS3
	CONF_ITEM_UI32("MNT_Program", 1, INT32_MAX, MOUNTPROG,
				nfs_core_param, program[P_MNT]),
#endif
#ifdef _USE_NLM
	CONF_ITEM_UI32("NLM_Program", 1, INT32_MAX, NLMPROG,
		       nfs_core_param, program[P_NLM]),
#endif
#ifdef _USE_RQUOTA
	CONF_ITEM_UI32("Rquota_Program", 1, INT32_MAX, RQUOTAPROG,
		       nfs_core_param, program[P_RQUOTA]),
#endif
#ifdef USE_NFSACL3
	CONF_ITEM_UI32("NFSACL_Program", 1, INT32_MAX, NFSACLPROG,
		       nfs_core_param, program[P_NFSACL]),
#endif
	CONF_ITEM_DEPRECATED("Nb_Worker",
			     "This parameter has been replaced with _9P { Nb_Worker}"
			     ),
	CONF_ITEM_BOOL("Drop_IO_Errors", false,
		       nfs_core_param, drop_io_errors),
	CONF_ITEM_BOOL("Drop_Inval_Errors", false,
		       nfs_core_param, drop_inval_errors),
	CONF_ITEM_BOOL("Drop_Delay_Errors", false,
		       nfs_core_param, drop_delay_errors),
	CONF_ITEM_BOOL("DRC_Disabled", false,
		       nfs_core_param, drc.disabled),
	CONF_ITEM_UI32("DRC_TCP_Npart", 1, 20, DRC_TCP_NPART,
		       nfs_core_param, drc.tcp.npart),
	CONF_ITEM_UI32("DRC_TCP_Size", 1, 32767, DRC_TCP_SIZE,
		       nfs_core_param, drc.tcp.size),
	CONF_ITEM_UI32("DRC_TCP_Cachesz", 1, 255, DRC_TCP_CACHESZ,
		       nfs_core_param, drc.tcp.cachesz),
	CONF_ITEM_UI32("DRC_TCP_Hiwat", 1, 256, DRC_TCP_HIWAT,
		       nfs_core_param, drc.tcp.hiwat),
	CONF_ITEM_UI32("DRC_TCP_Recycle_Npart", 1, 20, DRC_TCP_RECYCLE_NPART,
		       nfs_core_param, drc.tcp.recycle_npart),
	CONF_ITEM_UI32("DRC_TCP_Recycle_Expire_S", 0, 60*60, 600,
		       nfs_core_param, drc.tcp.recycle_expire_s),
	CONF_ITEM_BOOL("DRC_TCP_Checksum", DRC_TCP_CHECKSUM,
		       nfs_core_param, drc.tcp.checksum),
	CONF_ITEM_UI32("DRC_UDP_Npart", 1, 100, DRC_UDP_NPART,
		       nfs_core_param, drc.udp.npart),
	CONF_ITEM_UI32("DRC_UDP_Size", 512, 32768, DRC_UDP_SIZE,
		       nfs_core_param, drc.udp.size),
	CONF_ITEM_UI32("DRC_UDP_Cachesz", 1, 2047, DRC_UDP_CACHESZ,
		       nfs_core_param, drc.udp.cachesz),
	CONF_ITEM_UI32("DRC_UDP_Hiwat", 1, 32768, DRC_UDP_HIWAT,
		       nfs_core_param, drc.udp.hiwat),
	CONF_ITEM_BOOL("DRC_UDP_Checksum", DRC_UDP_CHECKSUM,
		       nfs_core_param, drc.udp.checksum),
	CONF_ITEM_UI32("RPC_Max_Connections", 1, 10000, 1024,
		       nfs_core_param, rpc.max_connections),
	CONF_ITEM_UI32("RPC_Idle_Timeout_S", 0, 60*60, 300,
		       nfs_core_param, rpc.idle_timeout_s),
	CONF_ITEM_UI32("MaxRPCSendBufferSize", 1, 1048576*9,
		       NFS_DEFAULT_SEND_BUFFER_SIZE,
		       nfs_core_param, rpc.max_send_buffer_size),
	CONF_ITEM_UI32("MaxRPCRecvBufferSize", 1, 1048576*9,
		       NFS_DEFAULT_RECV_BUFFER_SIZE,
		       nfs_core_param, rpc.max_recv_buffer_size),
	CONF_ITEM_UI32("rpc_ioq_thrdmin", 2, 1024*128, 2,
		       nfs_core_param, rpc.ioq_thrd_min),
	CONF_ITEM_UI32("RPC_Ioq_ThrdMax", 2, 1024*128, 200,
		       nfs_core_param, rpc.ioq_thrd_max),
	CONF_ITEM_UI32("RPC_GSS_Npart", 1, 1021, 13,
		       nfs_core_param, rpc.gss.ctx_hash_partitions),
	CONF_ITEM_UI32("RPC_GSS_Max_Ctx", 1, 1024*1024, 16384,
		       nfs_core_param, rpc.gss.max_ctx),
	CONF_ITEM_UI32("RPC_GSS_Max_GC", 1, 1024*1024, 200,
		       nfs_core_param, rpc.gss.max_gc),
	CONF_ITEM_I64("Blocked_Lock_Poller_Interval", 0, 180, 10,
		      nfs_core_param, blocked_lock_poller_interval),
	CONF_ITEM_LIST("NFS_Protocols", DEFAULT_PROTOCOLS, protocols,
		       nfs_core_param, core_options),
	CONF_ITEM_LIST("Protocols", DEFAULT_PROTOCOLS, protocols,
		       nfs_core_param, core_options),
	CONF_ITEM_BOOL("Clustered", true,
		       nfs_core_param, clustered),
#ifdef _USE_NLM
	CONF_ITEM_BOOL("Enable_NLM", true,
		       nfs_core_param, enable_NLM),
	CONF_ITEM_BOOL("NSM_Use_Caller_Name", false,
		       nfs_core_param, nsm_use_caller_name),
#endif
#ifdef _USE_RQUOTA
	CONF_ITEM_BOOL("Enable_RQUOTA", true,
		       nfs_core_param, enable_RQUOTA),
#endif
#ifdef USE_NFSACL3
	CONF_ITEM_BOOL("Enable_NFSACL", false,
		       nfs_core_param, enable_NFSACL),
#endif
	CONF_ITEM_BOOL("Enable_TCP_keepalive", true,
		       nfs_core_param, enable_tcp_keepalive),
	CONF_ITEM_UI32("TCP_KEEPCNT", 0, 255, 0,
		       nfs_core_param, tcp_keepcnt),
	CONF_ITEM_UI32("TCP_KEEPIDLE", 0, 65535, 0,
		       nfs_core_param, tcp_keepidle),
	CONF_ITEM_UI32("TCP_KEEPINTVL", 0, 65535, 0,
		       nfs_core_param, tcp_keepintvl),
	CONF_ITEM_BOOL("Enable_NFS_Stats", true,
		       nfs_core_param, enable_NFSSTATS),
	CONF_ITEM_BOOL("Enable_Fast_Stats", false,
		       nfs_core_param, enable_FASTSTATS),
	CONF_ITEM_BOOL("Enable_FSAL_Stats", false,
		       nfs_core_param, enable_FSALSTATS),
#ifdef _USE_NFS3
	CONF_ITEM_BOOL("Enable_FULLV3_Stats", false,
		       nfs_core_param, enable_FULLV3STATS),
#endif
	CONF_ITEM_BOOL("Enable_FULLV4_Stats", false,
		       nfs_core_param, enable_FULLV4STATS),
	CONF_ITEM_BOOL("Enable_AUTH_Stats", false,
		       nfs_core_param, enable_AUTHSTATS),
	CONF_ITEM_BOOL("Enable_CLNT_AllOps_Stats", false,
		       nfs_core_param, enable_CLNTALLSTATS),
	CONF_ITEM_BOOL("Short_File_Handle", false,
		       nfs_core_param, short_file_handle),
	CONF_ITEM_I64("Manage_Gids_Expiration", 0, 7*24*60*60, 30*60,
			nfs_core_param, manage_gids_expiration),
	CONF_ITEM_PATH("Plugins_Dir", 1, MAXPATHLEN, FSAL_MODULE_LOC,
		       nfs_core_param, ganesha_modules_loc),
	CONF_ITEM_UI32("heartbeat_freq", 0, 5000, 1000,
		       nfs_core_param, heartbeat_freq),
	CONF_ITEM_BOOL("fsid_device", false,
		       nfs_core_param, fsid_device),
	CONF_ITEM_BOOL("mount_path_pseudo", false,
		       nfs_core_param, mount_path_pseudo),
	CONF_ITEM_ENUM_BITS("Enable_UDP", UDP_LISTENER_ALL, UDP_LISTENER_MASK,
		       udp_listener_type, nfs_core_param, enable_UDP),
	CONF_ITEM_STR("Dbus_Name_Prefix", 1, 255, NULL,
		       nfs_core_param, dbus_name_prefix),
	CONF_ITEM_UI32("Max_Uid_To_Group_Reqs", 0, INT32_MAX, 0,
		       nfs_core_param, max_uid_to_grp_reqs),
	CONF_ITEM_BOOL("Enable_V3fh_Validation_For_V4", false,
		       nfs_core_param, enable_v3_fh_for_v4),
	CONF_ITEM_UI32("Readdir_Res_Size", 4096, 32768, 0,
		       nfs_core_param, readdir_res_size),
	CONFIG_EOL
};

struct config_block nfs_core = {
	.dbus_interface_name = "org.ganesha.nfsd.config.core",
	.blk_desc.name = "NFS_Core_Param",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = core_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

/**
 * @brief Kerberos/GSSAPI parameters
 */
#ifdef _HAVE_GSSAPI
static struct config_item krb5_params[] = {
	CONF_ITEM_STR("PrincipalName", 1, MAXPATHLEN,
		      DEFAULT_NFS_PRINCIPAL,
		      nfs_krb5_param, svc.principal),
	CONF_ITEM_PATH("KeytabPath", 1, MAXPATHLEN,
		       DEFAULT_NFS_KEYTAB,
		       nfs_krb5_param, keytab),
	CONF_ITEM_PATH("CCacheDir", 1, MAXPATHLEN,
		       DEFAULT_NFS_CCACHE_DIR,
		       nfs_krb5_param, ccache_dir),
	CONF_ITEM_BOOL("Active_krb5", true,
		       nfs_krb5_param, active_krb5),
	CONFIG_EOL
};

struct config_block krb5_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.krb5",
	.blk_desc.name = "NFS_KRB5",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = krb5_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};
#endif

#ifdef USE_NFSIDMAP
#define GETPWNAMDEF false
#else
#define GETPWNAMDEF true
#endif

/**
 * @brief NFSv4 specific parameters
 */

static struct config_item_list minor_versions[] = {
	CONFIG_LIST_TOK("0", NFSV4_MINOR_VERSION_ZERO),
	CONFIG_LIST_TOK("1", NFSV4_MINOR_VERSION_ONE),
	CONFIG_LIST_TOK("2", NFSV4_MINOR_VERSION_TWO),
	CONFIG_LIST_EOL
};

static struct config_item_list recovery_backend_types[] = {
	CONFIG_LIST_TOK("fs",			RECOVERY_BACKEND_FS),
	CONFIG_LIST_TOK("fs_ng",		RECOVERY_BACKEND_FS_NG),
	CONFIG_LIST_TOK("rados_kv",		RECOVERY_BACKEND_RADOS_KV),
	CONFIG_LIST_TOK("rados_ng",		RECOVERY_BACKEND_RADOS_NG),
	CONFIG_LIST_TOK("rados_cluster",	RECOVERY_BACKEND_RADOS_CLUSTER),
	CONFIG_LIST_EOL
};

static struct config_item version4_params[] = {
	CONF_ITEM_BOOL("Graceless", false,
		       nfs_version4_parameter, graceless),
	CONF_ITEM_UI32("Lease_Lifetime", 0, 120, LEASE_LIFETIME_DEFAULT,
		       nfs_version4_parameter, lease_lifetime),
	CONF_ITEM_UI32("Grace_Period", 0, 180, GRACE_PERIOD_DEFAULT,
		       nfs_version4_parameter, grace_period),
	CONF_ITEM_STR("Server_Scope", 1, MAXNAMLEN, NULL,
		      nfs_version4_parameter, server_scope),
	CONF_ITEM_STR("DomainName", 1, MAXPATHLEN, DOMAINNAME_DEFAULT,
		      nfs_version4_parameter, domainname),
	CONF_ITEM_PATH("IdmapConf", 1, MAXPATHLEN, IDMAPCONF_DEFAULT,
		       nfs_version4_parameter, idmapconf),
	CONF_ITEM_BOOL("UseGetpwnam", GETPWNAMDEF,
		       nfs_version4_parameter, use_getpwnam),
	CONF_ITEM_BOOL("Allow_Numeric_Owners", true,
		       nfs_version4_parameter, allow_numeric_owners),
	CONF_ITEM_BOOL("Only_Numeric_Owners", false,
		       nfs_version4_parameter, only_numeric_owners),
	CONF_ITEM_BOOL("Delegations", false,
		       nfs_version4_parameter, allow_delegations),
	CONF_ITEM_UI32("Deleg_Recall_Retry_Delay", 0, 10,
			DELEG_RECALL_RETRY_DELAY_DEFAULT,
			nfs_version4_parameter, deleg_recall_retry_delay),
	CONF_ITEM_BOOL("PNFS_MDS", true,
		       nfs_version4_parameter, pnfs_mds),
	CONF_ITEM_BOOL("PNFS_DS", true,
		       nfs_version4_parameter, pnfs_ds),
	CONF_ITEM_TOKEN("RecoveryBackend", RECOVERY_BACKEND_DEFAULT,
			recovery_backend_types, nfs_version4_parameter,
			recovery_backend),
	CONF_ITEM_PATH("RecoveryRoot", 1, MAXPATHLEN, NFS_V4_RECOV_ROOT,
		       nfs_version4_parameter, recov_root),
	CONF_ITEM_PATH("RecoveryDir", 1, MAXNAMLEN, NFS_V4_RECOV_DIR,
		       nfs_version4_parameter, recov_dir),
	CONF_ITEM_PATH("RecoveryOldDir", 1, MAXNAMLEN, NFS_V4_OLD_DIR,
		       nfs_version4_parameter, recov_old_dir),
	CONF_ITEM_LIST("minor_versions", NFSV4_MINOR_VERSION_ALL,
		       minor_versions, nfs_version4_parameter, minor_versions),
	CONF_ITEM_UI32("slot_table_size", 1, 1024, NFS41_NB_SLOTS_DEF,
		       nfs_version4_parameter, nb_slots),
	CONF_ITEM_BOOL("Enforce_UTF8_Validation", false,
		       nfs_version4_parameter, enforce_utf8_vld),
	CONF_ITEM_UI32("Max_Client_Ids", 0, UINT32_MAX, 0,
		       nfs_version4_parameter, max_client_ids),
	CONFIG_EOL
};

struct config_block version4_param = {
	.dbus_interface_name = "org.ganesha.nfsd.config.nfsv4",
	.blk_desc.name = "NFSv4",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = version4_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

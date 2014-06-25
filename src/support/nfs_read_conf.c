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
#include "ganesha_rpc.h"
#include "fsal.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_file_handle.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "config_parsing.h"

/**
 * @brief Core configuration parameters
 */

static struct config_item_list protocols[] = {
	CONFIG_LIST_TOK("3", CORE_OPTION_NFSV3),
	CONFIG_LIST_TOK("4", CORE_OPTION_NFSV4),
	CONFIG_LIST_EOL
};

static struct config_item core_params[] = {
	CONF_ITEM_UI16("NFS_Port", 0, UINT16_MAX, NFS_PORT,
		       nfs_core_param, port[P_NFS]),
	CONF_ITEM_UI16("MNT_Port", 0, UINT16_MAX, 0,
		       nfs_core_param, port[P_MNT]),
	CONF_ITEM_UI16("NLM_Port", 0, UINT16_MAX, 0,
		       nfs_core_param, port[P_NLM]),
	CONF_ITEM_UI16("Rquota_Port", 0, UINT16_MAX, RQUOTA_PORT,
		       nfs_core_param, port[P_RQUOTA]),
	CONF_ITEM_IPV4_ADDR("Bind_Addr", "0.0.0.0",
			    nfs_core_param, bind_addr),
	CONF_ITEM_UI32("NFS_Program", 1, INT32_MAX, NFS_PROGRAM,
		       nfs_core_param, program[P_NFS]),
	CONF_ITEM_UI32("MNT_Program", 1, INT32_MAX, MOUNTPROG,
				nfs_core_param, program[P_MNT]),
	CONF_ITEM_UI32("NLM_Program", 1, INT32_MAX, NLMPROG,
		       nfs_core_param, program[P_NLM]),
	CONF_ITEM_UI32("Rquota_Program", 1, INT32_MAX, RQUOTAPROG,
		       nfs_core_param, program[P_RQUOTA]),
	CONF_ITEM_UI32("Nb_Worker", 1, 1024*128, NB_WORKER_THREAD_DEFAULT,
		       nfs_core_param, nb_worker),
	CONF_ITEM_BOOL("Drop_IO_Errors", false,
		       nfs_core_param, drop_io_errors),
	CONF_ITEM_BOOL("Drop_Inval_Errors", false,
		       nfs_core_param, drop_inval_errors),
	CONF_ITEM_BOOL("Drop_Delay_Errors", false,
		       nfs_core_param, drop_delay_errors),
	CONF_ITEM_UI32("Dispatch_Max_Reqs", 1, 10000, 5000,
		       nfs_core_param, dispatch_max_reqs),
	CONF_ITEM_UI32("Dispatch_Max_Reqs_Xprt", 1, 2048, 512,
		       nfs_core_param, dispatch_max_reqs_xprt),
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
	CONF_ITEM_UI32("RPC_Debug_Flags", 0, UINT32_MAX, TIRPC_DEBUG_FLAGS,
		       nfs_core_param, rpc.debug_flags),
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
	CONF_ITEM_UI32("RPC_Ioq_ThrdMax", 1, 1024*128, 200,
		       nfs_core_param, rpc.ioq_thrd_max),
	CONF_ITEM_I64("Decoder_Fridge_Expiration_Delay", 0, 7200, 600,
		      nfs_core_param, decoder_fridge_expiration_delay),
	CONF_ITEM_I64("Decoder_Fridge_Block_Timeout", 0, 7200, 600,
		      nfs_core_param, decoder_fridge_block_timeout),
	CONF_ITEM_LIST("NFS_Protocols", CORE_OPTION_ALL_VERS, protocols,
		       nfs_core_param, core_options),
	CONF_ITEM_BOOL("NSM_Use_Caller_Name", false,
		       nfs_core_param, nsm_use_caller_name),
	CONF_ITEM_BOOL("Clustered", true,
		       nfs_core_param, clustered),
	CONF_ITEM_BOOL("Enable_NLM", true,
		       nfs_core_param, enable_NLM),
	CONF_ITEM_BOOL("Enable_RQUOTA", true,
		       nfs_core_param, enable_RQUOTA),
	CONF_ITEM_BOOL("Enable_Fast_Stats", false,
		       nfs_core_param, enable_FASTSTATS),
	CONF_ITEM_I64("Manage_Gids_Expiration", 0, 7*24*60*60, 30*60,
			nfs_core_param, manage_gids_expiration),
	CONF_ITEM_PATH("Plugins_Dir", 1, MAXPATHLEN, FSAL_MODULE_LOC,
		       nfs_core_param, ganesha_modules_loc),
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

static struct config_item version4_params[] = {
	CONF_ITEM_BOOL("FSAL_Grace", false,
		       nfs_version4_parameter, fsal_grace),
	CONF_ITEM_BOOL("Graceless", false,
		       nfs_version4_parameter, graceless),
	CONF_ITEM_UI32("Lease_Lifetime", 0, 120, LEASE_LIFETIME_DEFAULT,
		       nfs_version4_parameter, lease_lifetime),
	CONF_ITEM_UI32("Grace_Period", 0, 180, GRACE_PERIOD_DEFAULT,
		       nfs_version4_parameter, grace_period),
	CONF_ITEM_STR("DomainName", 1, MAXPATHLEN, DOMAINNAME_DEFAULT,
		      nfs_version4_parameter, domainname),
	CONF_ITEM_PATH("IdmapConf", 1, MAXPATHLEN, "/etc/idmapd.conf",
		       nfs_version4_parameter, idmapconf),
	CONF_ITEM_BOOL("UseGetpwnam", GETPWNAMDEF,
		       nfs_version4_parameter, use_getpwnam),
	CONF_ITEM_BOOL("Allow_Numeric_Owners", true,
		       nfs_version4_parameter, allow_numeric_owners),
	CONF_ITEM_BOOL("Delegations", false,
		       nfs_version4_parameter, allow_delegations),
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

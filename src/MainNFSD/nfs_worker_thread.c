/*
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
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
 * @file    nfs_worker_thread.c
 * @brief   The file that contain the 'worker_thread' routine for the nfsd.
 */
#include "config.h"
#ifdef FREEBSD
#include <signal.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/file.h> /* for having FNDELAY */
#include <sys/signal.h>
#include <poll.h>
#include "HashTable.h"
#include "abstract_atomic.h"
#include "log.h"
#include "ganesha_rpc.h"
#include "nfs23.h"
#include "nfs4.h"
#include "mount.h"
#include "nlm4.h"
#include "rquota.h"
#include "nfs_core.h"
#include "cache_inode.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_req_queue.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "fridgethr.h"
#include "client_mgr.h"
#include "export_mgr.h"
#include "server_stats.h"

pool_t *request_pool;
pool_t *request_data_pool;
pool_t *dupreq_pool;

static struct fridgethr *worker_fridge;

const nfs_function_desc_t invalid_funcdesc =
{
	.service_function = nfs_Null,
	.free_function = nfs_Null_Free,
	.xdr_decode_func = (xdrproc_t) xdr_void,
	.xdr_encode_func = (xdrproc_t) xdr_void,
	.funcname = "invalid_function",
	.dispatch_behaviour = NOTHING_SPECIAL
};

const nfs_function_desc_t *INVALID_FUNCDESC = &invalid_funcdesc;

const nfs_function_desc_t nfs3_func_desc[] = {
	{
		.service_function = nfs_Null,
		.free_function = nfs_Null_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nfs_Null",
		.dispatch_behaviour = NOTHING_SPECIAL
	},
	{
		.service_function = nfs_Getattr,
		.free_function = nfs_Getattr_Free,
		.xdr_decode_func = (xdrproc_t) xdr_GETATTR3args,
		.xdr_encode_func = (xdrproc_t) xdr_GETATTR3res,
		.funcname = "nfs_Getattr",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS
	},
	{
		.service_function = nfs_Setattr,
		.free_function = nfs_Setattr_Free,
		.xdr_decode_func = (xdrproc_t) xdr_SETATTR3args,
		.xdr_encode_func = (xdrproc_t) xdr_SETATTR3res,
		.funcname = "nfs_Setattr",
		.dispatch_behaviour = (MAKES_WRITE | NEEDS_CRED |
				       NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS)
	},
	{
		.service_function = nfs_Lookup,
		.free_function = nfs3_Lookup_Free,
		.xdr_decode_func = (xdrproc_t) xdr_LOOKUP3args,
		.xdr_encode_func = (xdrproc_t) xdr_LOOKUP3res,
		.funcname = "nfs_Lookup",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS
	},
	{
		.service_function = nfs3_Access,
		.free_function = nfs3_Access_Free,
		.xdr_decode_func = (xdrproc_t) xdr_ACCESS3args,
		.xdr_encode_func = (xdrproc_t) xdr_ACCESS3res,
		.funcname = "nfs3_Access",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS
	},
	{
		.service_function = nfs_Readlink,
		.free_function = nfs3_Readlink_Free,
		.xdr_decode_func = (xdrproc_t) xdr_READLINK3args,
		.xdr_encode_func = (xdrproc_t) xdr_READLINK3res,
		.funcname = "nfs_Readlink",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS
	},
	{
		.service_function = nfs_Read,
		.free_function = nfs3_Read_Free,
		.xdr_decode_func = (xdrproc_t) xdr_READ3args,
		.xdr_encode_func = (xdrproc_t) xdr_READ3res,
		.funcname = "nfs_Read",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS | MAKES_IO
	},
	{
		.service_function = nfs_Write,
		.free_function = nfs_Write_Free,
		.xdr_decode_func = (xdrproc_t) xdr_WRITE3args,
		.xdr_encode_func = (xdrproc_t) xdr_WRITE3res,
		.funcname = "nfs_Write",
		.dispatch_behaviour = (MAKES_WRITE | NEEDS_CRED |
				       NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS | MAKES_IO)
	},
	{
		.service_function = nfs_Create,
		.free_function = nfs_Create_Free,
		.xdr_decode_func = (xdrproc_t) xdr_CREATE3args,
		.xdr_encode_func = (xdrproc_t) xdr_CREATE3res,
		.funcname = "nfs_Create",
		.dispatch_behaviour = (MAKES_WRITE | NEEDS_CRED |
				       NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS)
	},
	{
		.service_function = nfs_Mkdir,
		.free_function = nfs_Mkdir_Free,
		.xdr_decode_func = (xdrproc_t) xdr_MKDIR3args,
		.xdr_encode_func = (xdrproc_t) xdr_MKDIR3res,
		.funcname = "nfs_Mkdir",
		.dispatch_behaviour = (MAKES_WRITE | NEEDS_CRED |
				       NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS)
	},
	{
		.service_function = nfs_Symlink,
		.free_function = nfs_Symlink_Free,
		.xdr_decode_func = (xdrproc_t) xdr_SYMLINK3args,
		.xdr_encode_func = (xdrproc_t) xdr_SYMLINK3res,
		.funcname = "nfs_Symlink",
		.dispatch_behaviour = (MAKES_WRITE | NEEDS_CRED |
				       NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS)
	},
	{
		.service_function = nfs3_Mknod,
		.free_function = nfs3_Mknod_Free,
		.xdr_decode_func = (xdrproc_t) xdr_MKNOD3args,
		.xdr_encode_func = (xdrproc_t) xdr_MKNOD3res,
		.funcname = "nfs3_Mknod",
		.dispatch_behaviour = (MAKES_WRITE | NEEDS_CRED
				       | NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS)
	},
	{
		.service_function = nfs_Remove,
		.free_function = nfs_Remove_Free,
		.xdr_decode_func = (xdrproc_t) xdr_REMOVE3args,
		.xdr_encode_func = (xdrproc_t) xdr_REMOVE3res,
		.funcname = "nfs_Remove",
		.dispatch_behaviour = (MAKES_WRITE | NEEDS_CRED |
				       NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS)
	},
	{
		.service_function = nfs_Rmdir,
		.free_function = nfs_Rmdir_Free,
		.xdr_decode_func = (xdrproc_t) xdr_RMDIR3args,
		.xdr_encode_func = (xdrproc_t) xdr_RMDIR3res,
		.funcname = "nfs_Rmdir",
		.dispatch_behaviour = (MAKES_WRITE | NEEDS_CRED |
				       NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS)
	},
	{
		.service_function = nfs_Rename,
		.free_function = nfs_Rename_Free,
		.xdr_decode_func = (xdrproc_t) xdr_RENAME3args,
		.xdr_encode_func = (xdrproc_t) xdr_RENAME3res,
		.funcname = "nfs_Rename",
		.dispatch_behaviour = (MAKES_WRITE | NEEDS_CRED |
				       NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS)
	},
	{
		.service_function = nfs_Link,
		.free_function = nfs_Link_Free,
		.xdr_decode_func = (xdrproc_t) xdr_LINK3args,
		.xdr_encode_func = (xdrproc_t) xdr_LINK3res,
		.funcname = "nfs_Link",
		.dispatch_behaviour = (MAKES_WRITE | NEEDS_CRED |
				       NEEDS_EXPORT | CAN_BE_DUP | SUPPORTS_GSS)
	},
	{
		.service_function = nfs_Readdir,
		.free_function = nfs3_Readdir_Free,
		.xdr_decode_func = (xdrproc_t) xdr_READDIR3args,
		.xdr_encode_func = (xdrproc_t) xdr_READDIR3res,
		.funcname = "nfs_Readdir",
		.dispatch_behaviour = (NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS)
	},
	{
		.service_function = nfs3_Readdirplus,
		.free_function = nfs3_Readdirplus_Free,
		.xdr_decode_func = (xdrproc_t) xdr_READDIRPLUS3args,
		.xdr_encode_func = (xdrproc_t) xdr_READDIRPLUS3res,
		.funcname = "nfs3_Readdirplus",
		.dispatch_behaviour = (NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS)
	},
	{
		.service_function = nfs_Fsstat,
		.free_function = nfs_Fsstat_Free,
		.xdr_decode_func = (xdrproc_t) xdr_FSSTAT3args,
		.xdr_encode_func = (xdrproc_t) xdr_FSSTAT3res,
		.funcname = "nfs_Fsstat",
		.dispatch_behaviour = (NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS)
	},
	{
		.service_function = nfs3_Fsinfo,
		.free_function = nfs3_Fsinfo_Free,
		.xdr_decode_func = (xdrproc_t) xdr_FSINFO3args,
		.xdr_encode_func = (xdrproc_t) xdr_FSINFO3res,
		.funcname = "nfs3_Fsinfo",
		.dispatch_behaviour = (NEEDS_CRED | NEEDS_EXPORT)
	},
	{
		.service_function = nfs3_Pathconf,
		.free_function = nfs3_Pathconf_Free,
		.xdr_decode_func = (xdrproc_t) xdr_PATHCONF3args,
		.xdr_encode_func = (xdrproc_t) xdr_PATHCONF3res,
		.funcname = "nfs3_Pathconf",
		.dispatch_behaviour = (NEEDS_CRED | NEEDS_EXPORT | SUPPORTS_GSS)
	},
	{
		.service_function = nfs3_Commit,
		.free_function = nfs3_Commit_Free,
		.xdr_decode_func = (xdrproc_t) xdr_COMMIT3args,
		.xdr_encode_func = (xdrproc_t) xdr_COMMIT3res,
		.funcname = "nfs3_Commit",
		.dispatch_behaviour = (MAKES_WRITE | NEEDS_CRED |
				       NEEDS_EXPORT | SUPPORTS_GSS)
	}
};

/* Remeber that NFSv4 manages authentication though junction crossing, and
 * so does it for RO FS management (for each operation) */
const nfs_function_desc_t nfs4_func_desc[] = {
	{
		.service_function = nfs_Null,
		.free_function = nfs_Null_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nfs_Null",
		.dispatch_behaviour = NOTHING_SPECIAL
	},
	{
		.service_function = nfs4_Compound,
		.free_function = nfs4_Compound_Free,
		.xdr_decode_func = (xdrproc_t) xdr_COMPOUND4args,
		.xdr_encode_func = (xdrproc_t) xdr_COMPOUND4res,
		.funcname = "nfs4_Comp",
		.dispatch_behaviour = CAN_BE_DUP
	}
};

const nfs_function_desc_t mnt1_func_desc[] = {
	{
		.service_function = mnt_Null,
		.free_function = mnt_Null_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "mnt_Null",
		.dispatch_behaviour = NOTHING_SPECIAL
	},
	{
		.service_function = mnt_Mnt,
		.free_function = mnt1_Mnt_Free,
		.xdr_decode_func = (xdrproc_t) xdr_dirpath,
		.xdr_encode_func = (xdrproc_t) xdr_fhstatus2,
		.funcname = "mnt_Mnt",
		.dispatch_behaviour = NEEDS_CRED
	},
	{
		.service_function = mnt_Dump,
		.free_function = mnt_Dump_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_mountlist,
		.funcname = "mnt_Dump",
		.dispatch_behaviour = NOTHING_SPECIAL
	},
	{
		.service_function = mnt_Umnt,
		.free_function = mnt_Umnt_Free,
		.xdr_decode_func = (xdrproc_t) xdr_dirpath,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "mnt_Umnt",
		.dispatch_behaviour = NOTHING_SPECIAL
	},
	{
		.service_function = mnt_UmntAll,
		.free_function = mnt_UmntAll_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "mnt_UmntAll",
		.dispatch_behaviour = NOTHING_SPECIAL
	},
	{
		.service_function = mnt_Export,
		.free_function = mnt_Export_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_exports,
		.funcname = "mnt_Export",
		.dispatch_behaviour = NOTHING_SPECIAL
	}
};

const nfs_function_desc_t mnt3_func_desc[] = {
	{
		.service_function = mnt_Null,
		.free_function = mnt_Null_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "mnt_Null",
		.dispatch_behaviour = NOTHING_SPECIAL
	},
	{
		.service_function = mnt_Mnt,
		.free_function = mnt3_Mnt_Free,
		.xdr_decode_func = (xdrproc_t) xdr_dirpath,
		.xdr_encode_func = (xdrproc_t) xdr_mountres3,
		.funcname = "mnt_Mnt",
		.dispatch_behaviour = NOTHING_SPECIAL
	},
	{
		.service_function = mnt_Dump,
		.free_function = mnt_Dump_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_mountlist,
		.funcname = "mnt_Dump",
		.dispatch_behaviour = NOTHING_SPECIAL
	},
	{
		.service_function = mnt_Umnt,
		.free_function = mnt_Umnt_Free,
		.xdr_decode_func = (xdrproc_t) xdr_dirpath,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "mnt_Umnt",
		.dispatch_behaviour = NOTHING_SPECIAL
	},
	{
		.service_function = mnt_UmntAll,
		.free_function = mnt_UmntAll_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "mnt_UmntAll",
		.dispatch_behaviour = NOTHING_SPECIAL
	},
	{
		.service_function = mnt_Export,
		.free_function = mnt_Export_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_exports,
		.funcname = "mnt_Export",
		.dispatch_behaviour = NOTHING_SPECIAL
	}
};

#define nlm4_Unsupported nlm_Null
#define nlm4_Unsupported_Free nlm_Null_Free

const nfs_function_desc_t nlm4_func_desc[] = {
	[NLMPROC4_NULL] = {
		.service_function = nlm_Null,
		.free_function = nlm_Null_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm_Null",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_TEST] = {
		.service_function = nlm4_Test,
		.free_function = nlm4_Test_Free,
		.xdr_decode_func = (xdrproc_t) xdr_nlm4_testargs,
		.xdr_encode_func = (xdrproc_t) xdr_nlm4_testres,
		.funcname = "nlm4_Test",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT},
	[NLMPROC4_LOCK] = {
		.service_function = nlm4_Lock,
		.free_function = nlm4_Lock_Free,
		.xdr_decode_func = (xdrproc_t) xdr_nlm4_lockargs,
		.xdr_encode_func = (xdrproc_t) xdr_nlm4_res,
		.funcname = "nlm4_Lock",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT},
	[NLMPROC4_CANCEL] = {
		.service_function = nlm4_Cancel,
		.free_function = nlm4_Cancel_Free,
		.xdr_decode_func = (xdrproc_t) xdr_nlm4_cancargs,
		.xdr_encode_func = (xdrproc_t) xdr_nlm4_res,
		.funcname = "nlm4_Cancel",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT},
	[NLMPROC4_UNLOCK] = {
		.service_function = nlm4_Unlock,
		.free_function = nlm4_Unlock_Free,
		.xdr_decode_func = (xdrproc_t) xdr_nlm4_unlockargs,
		.xdr_encode_func = (xdrproc_t) xdr_nlm4_res,
		.funcname = "nlm4_Unlock",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT},
	[NLMPROC4_GRANTED] = {
		.service_function = nlm4_Unsupported,
		.free_function = nlm4_Unsupported_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Granted",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_TEST_MSG] = {
		.service_function = nlm4_Test_Message,
		.free_function = nlm4_Test_Free,
		.xdr_decode_func = (xdrproc_t) xdr_nlm4_testargs,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Test_msg",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT},
	[NLMPROC4_LOCK_MSG] = {
		.service_function = nlm4_Lock_Message,
		.free_function = nlm4_Lock_Free,
		.xdr_decode_func = (xdrproc_t) xdr_nlm4_lockargs,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Lock_msg",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT},
	[NLMPROC4_CANCEL_MSG] = {
		.service_function = nlm4_Cancel_Message,
		.free_function = nlm4_Cancel_Free,
		.xdr_decode_func = (xdrproc_t) xdr_nlm4_cancargs,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Cancel_msg",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT},
	[NLMPROC4_UNLOCK_MSG] = {
		.service_function = nlm4_Unlock_Message,
		.free_function = nlm4_Unlock_Free,
		.xdr_decode_func = (xdrproc_t) xdr_nlm4_unlockargs,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Unlock_msg",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT},
	[NLMPROC4_GRANTED_MSG] = {
		.service_function = nlm4_Unsupported,
		.free_function = nlm4_Unsupported_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Granted_msg",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_TEST_RES] = {
		.service_function = nlm4_Unsupported,
		.free_function = nlm4_Unsupported_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Test_res",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_LOCK_RES] = {
		.service_function = nlm4_Unsupported,
		.free_function = nlm4_Unsupported_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Lock_res",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_CANCEL_RES] = {
		.service_function = nlm4_Unsupported,
		.free_function = nlm4_Unsupported_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Cancel_res",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_UNLOCK_RES] = {
		.service_function = nlm4_Unsupported,
		.free_function = nlm4_Unsupported_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Unlock_res",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_GRANTED_RES] = {
		.service_function = nlm4_Granted_Res,
		.free_function = nlm4_Granted_Res_Free,
		.xdr_decode_func = (xdrproc_t) xdr_nlm4_res,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Granted_res",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_SM_NOTIFY] = {
		.service_function = nlm4_Sm_Notify,
		.free_function = nlm4_Sm_Notify_Free,
		.xdr_decode_func = (xdrproc_t) xdr_nlm4_sm_notifyargs,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_sm_notify",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[17] = {
		.service_function = nlm4_Unsupported,
		.free_function = nlm4_Unsupported_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Granted_res",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[18] = {
		.service_function = nlm4_Unsupported,
		.free_function = nlm4_Unsupported_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Granted_res",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[19] = {
		.service_function = nlm4_Unsupported,
		.free_function = nlm4_Unsupported_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Granted_res",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_SHARE] = {
		.service_function = nlm4_Share,
		.free_function = nlm4_Share_Free,
		.xdr_decode_func = (xdrproc_t) xdr_nlm4_shareargs,
		.xdr_encode_func = (xdrproc_t) xdr_nlm4_shareres,
		.funcname = "nlm4_Share",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT},
	[NLMPROC4_UNSHARE] = {
		.service_function = nlm4_Unshare,
		.free_function = nlm4_Unshare_Free,
		.xdr_decode_func = (xdrproc_t) xdr_nlm4_shareargs,
		.xdr_encode_func = (xdrproc_t) xdr_nlm4_shareres,
		.funcname = "nlm4_Unshare",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT},
	[NLMPROC4_NM_LOCK] = {
		/* NLM_NM_LOCK uses the same handling as NLM_LOCK except for
		 * monitoring, nlm4_Lock will make that determination.
		 */
		.service_function = nlm4_Lock,
		.free_function = nlm4_Lock_Free,
		.xdr_decode_func = (xdrproc_t) xdr_nlm4_lockargs,
		.xdr_encode_func = (xdrproc_t) xdr_nlm4_res,
		.funcname = "nlm4_Nm_lock",
		.dispatch_behaviour = NEEDS_CRED | NEEDS_EXPORT},
	[NLMPROC4_FREE_ALL] = {
		.service_function = nlm4_Free_All,
		.free_function = nlm4_Free_All_Free,
		.xdr_decode_func = (xdrproc_t) xdr_nlm4_free_allargs,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "nlm4_Free_all",
		.dispatch_behaviour = NOTHING_SPECIAL},
};

const nfs_function_desc_t rquota1_func_desc[] = {
	[0] = {
		.service_function = rquota_Null,
		.free_function = rquota_Null_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "rquota_Null",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[RQUOTAPROC_GETQUOTA] = {
		.service_function = rquota_getquota,
		.free_function = rquota_getquota_Free,
		.xdr_decode_func = (xdrproc_t) xdr_getquota_args,
		.xdr_encode_func = (xdrproc_t) xdr_getquota_rslt,
		.funcname = "rquota_Getquota",
		.dispatch_behaviour = NEEDS_CRED},
	[RQUOTAPROC_GETACTIVEQUOTA] = {
		.service_function = rquota_getactivequota,
		.free_function = rquota_getactivequota_Free,
		.xdr_decode_func = (xdrproc_t) xdr_getquota_args,
		.xdr_encode_func = (xdrproc_t) xdr_getquota_rslt,
		.funcname = "rquota_Getactivequota",
		.dispatch_behaviour = NEEDS_CRED},
	[RQUOTAPROC_SETQUOTA] = {
		.service_function = rquota_setquota,
		.free_function = rquota_setquota_Free,
		.xdr_decode_func = (xdrproc_t) xdr_setquota_args,
		.xdr_encode_func = (xdrproc_t) xdr_setquota_rslt,
		.funcname = "rquota_Setactivequota",
		.dispatch_behaviour = NEEDS_CRED},
	[RQUOTAPROC_SETACTIVEQUOTA] = {
		.service_function = rquota_setactivequota,
		.free_function = rquota_setactivequota_Free,
		.xdr_decode_func = (xdrproc_t) xdr_setquota_args,
		.xdr_encode_func = (xdrproc_t) xdr_setquota_rslt,
		.funcname = "rquota_Getactivequota",
		.dispatch_behaviour = NEEDS_CRED}
};

const nfs_function_desc_t rquota2_func_desc[] = {
	[0] = {
		.service_function = rquota_Null,
		.free_function = rquota_Null_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "rquota_Null",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[RQUOTAPROC_GETQUOTA] = {
		.service_function = rquota_getquota,
		.free_function = rquota_getquota_Free,
		.xdr_decode_func = (xdrproc_t) xdr_ext_getquota_args,
		.xdr_encode_func = (xdrproc_t) xdr_getquota_rslt,
		.funcname = "rquota_Ext_Getquota",
		.dispatch_behaviour = NEEDS_CRED},
	[RQUOTAPROC_GETACTIVEQUOTA] = {
		.service_function = rquota_getactivequota,
		.free_function = rquota_getactivequota_Free,
		.xdr_decode_func = (xdrproc_t) xdr_ext_getquota_args,
		.xdr_encode_func = (xdrproc_t) xdr_getquota_rslt,
		.funcname = "rquota_Ext_Getactivequota",
		.dispatch_behaviour = NEEDS_CRED},
	[RQUOTAPROC_SETQUOTA] = {
		.service_function = rquota_setquota,
		.free_function = rquota_setquota_Free,
		.xdr_decode_func = (xdrproc_t) xdr_ext_setquota_args,
		.xdr_encode_func = (xdrproc_t) xdr_setquota_rslt,
		.funcname = "rquota_Ext_Setactivequota",
		.dispatch_behaviour = NEEDS_CRED},
	[RQUOTAPROC_SETACTIVEQUOTA] = {
		.service_function = rquota_setactivequota,
		.free_function = rquota_setactivequota_Free,
		.xdr_decode_func = (xdrproc_t) xdr_ext_setquota_args,
		.xdr_encode_func = (xdrproc_t) xdr_setquota_rslt,
		.funcname = "rquota_Ext_Getactivequota",
		.dispatch_behaviour = NEEDS_CRED}
};

/**
 * @brief Extract nfs function descriptor from nfs request.
 *
 * Choose the function descriptor, either a valid one or
 * the default invalid handler.  We have already sanity checked
 * everything so just grab and go.
 *
 * @param[in,out] preqnfs Raw request data
 *
 * @return Function vector for program.
 */
const nfs_function_desc_t *
nfs_rpc_get_funcdesc(nfs_request_data_t *preqnfs)
{
	struct svc_req *req = &preqnfs->req;
	const nfs_function_desc_t *funcdesc = INVALID_FUNCDESC;

	if(req->rq_prog == nfs_param.core_param.program[P_NFS]) {
		funcdesc = (req->rq_vers == NFS_V3) ?
			&nfs3_func_desc[req->rq_proc] :
			&nfs4_func_desc[req->rq_proc];
	} else if(req->rq_prog == nfs_param.core_param.program[P_NLM]) {
		funcdesc = &nlm4_func_desc[req->rq_proc];
	} else if(req->rq_prog == nfs_param.core_param.program[P_MNT]) {
		preqnfs->lookahead.flags |= NFS_LOOKAHEAD_MOUNT;
		funcdesc = (req->rq_vers == MOUNT_V1) ?
			&mnt1_func_desc[req->rq_proc] :
			&mnt3_func_desc[req->rq_proc];
	} else if(req->rq_prog == nfs_param.core_param.program[P_RQUOTA]) {
		funcdesc = (req->rq_vers == RQUOTAVERS) ?
			&rquota1_func_desc[req->rq_proc] :
			&rquota2_func_desc[req->rq_proc];
	}
	return funcdesc;
}

/**
 * @brief Main RPC dispatcher routine
 *
 * @param[in,out] preq        NFS request
 * @param[in,out] worker_data Worker thread context
 *
 */
static void nfs_rpc_execute(request_data_t *preq,
                            nfs_worker_data_t *worker_data)
{
  nfs_request_data_t *preqnfs = preq->r_u.nfs;
  nfs_arg_t *arg_nfs = &preqnfs->arg_nfs;
  nfs_res_t *res_nfs;
  int exportid = -1;
  struct svc_req *req = &preqnfs->req;
  SVCXPRT *xprt = preqnfs->xprt;
  export_perms_t export_perms;
  int protocol_options = 0;
  struct user_cred user_credentials;
  struct req_op_context req_ctx;
  dupreq_status_t dpq_status;
  struct timespec timer_start;
  int port, rc = NFS_REQ_OK;
  enum auth_stat auth_rc;
  bool slocked = false;
  const char *progname = "unknown";

  /* Initialize permissions to allow nothing */
  export_perms.options       = 0;
  export_perms.anonymous_uid = (uid_t) ANON_UID;
  export_perms.anonymous_gid = (gid_t) ANON_GID;

  /* Initialized user_credentials */
  init_credentials(&user_credentials);

      /* set up the request context
       */
  memset(&req_ctx, 0, sizeof(struct req_op_context));
  req_ctx.creds = &user_credentials;
  req_ctx.caller_addr = &worker_data->hostaddr;
  req_ctx.nfs_vers = req->rq_vers;
  req_ctx.req_type = preq->rtype;

  /* XXX must hold lock when calling any TI-RPC channel function,
   * including svc_sendreply2 and the svcerr_* calls */

  /* XXX also, need to check UDP correctness, this may need some more
   * TI-RPC work (for UDP, if we -really needed it-, we needed to
   * capture hostaddr at SVC_RECV).  For TCP, if we intend to use
   * this, we should sprint a buffer once, in when we're setting up
   * xprt private data. */
  if(copy_xprt_addr(req_ctx.caller_addr, xprt) == 0)

/* can I change this to be call by ref instead of copy?
 * the xprt is valid for the lifetime here
 */

    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "copy_xprt_addr failed for Program %d, Version %d, "
                   "Function %d",
                   (int)req->rq_prog, (int)req->rq_vers,
                   (int)req->rq_proc);
      /* XXX move lock wrapper into RPC API */
      DISP_SLOCK(xprt);
      svcerr_systemerr(xprt, req);
      DISP_SUNLOCK(xprt);
      return;
    }

  port = get_port(req_ctx.caller_addr);
  req_ctx.client = get_gsh_client(req_ctx.caller_addr, false);
  if(req_ctx.client == NULL)
    {
      LogDebug(COMPONENT_DISPATCH,
	       "Cannot get client block for Program %d, Version %d, "
	       "Function %d",
	       (int)req->rq_prog, (int)req->rq_vers,
	       (int)req->rq_proc);
    }
  if(isDebug(COMPONENT_DISPATCH))
    {
      LogDebug(COMPONENT_DISPATCH,
               "Request from %s for Program %d, Version %d, Function %d "
               "has xid=%u",
               req_ctx.client->hostaddr_str,
               (int)req->rq_prog, (int)req->rq_vers, (int)req->rq_proc,
               req->rq_xid);
    }

  /* start the processing clock
   * we measure all time stats as intervals (elapsed nsecs) from
   * server boot time.  This gets high precision with simple 64 bit math.
   */
  now(&timer_start);
  req_ctx.start_time = timespec_diff(&ServerBootTime,
				     &timer_start);
  req_ctx.queue_wait = req_ctx.start_time - timespec_diff(&ServerBootTime,
				     &preq->time_queued);

  /* If req is uncacheable, or if req is v41+, nfs_dupreq_start will do
   * nothing but allocate a result object and mark the request (ie, the
   * path is short, lockless, and does no hash/search). */
  dpq_status = nfs_dupreq_start(preqnfs, req);
  res_nfs = preqnfs->res_nfs;
  if(dpq_status == DUPREQ_SUCCESS) {
      /* A new request, continue processing it. */
      LogFullDebug(COMPONENT_DISPATCH, "Current request is not duplicate or "
                   "not cacheable.");
  } else {
    switch(dpq_status)
     {
     case DUPREQ_EXISTS:
      /* Found the request in the dupreq cache.  Send cached reply. */
        LogFullDebug(COMPONENT_DISPATCH,
                     "DUP: DupReq Cache Hit: using previous "
                     "reply, rpcxid=%u",
                     req->rq_xid);

        LogFullDebug(COMPONENT_DISPATCH,
                     "Before svc_sendreply on socket %d (dup req)",
                     xprt->xp_fd);

        DISP_SLOCK(xprt);
        if(svc_sendreply(xprt, req, preqnfs->funcdesc->xdr_encode_func,
            (caddr_t) res_nfs) == false)
          {
              LogDebug(COMPONENT_DISPATCH,
                       "NFS DISPATCHER: FAILURE: Error while calling "
                       "svc_sendreply on a duplicate request. rpcxid=%u "
                       "socket=%d function:%s client:%s program:%d "
                       "nfs version:%d proc:%d xid:%u errno: %d",
                       req->rq_xid, xprt->xp_fd,
                       preqnfs->funcdesc->funcname,
                       req_ctx.client->hostaddr_str, (int)req->rq_prog,
                       (int)req->rq_vers, (int)req->rq_proc, req->rq_xid,
                       errno);
              svcerr_systemerr(xprt, req);
          }
	break;

      /* Another thread owns the request */
    case DUPREQ_BEING_PROCESSED:
      LogFullDebug(COMPONENT_DISPATCH,
                   "DUP: Request xid=%u is already being processed; the "
                   "active thread will reply",
                   req->rq_xid);
      /* Free the arguments */
      DISP_SLOCK(xprt);
      /* Ignore the request, send no error */
      break;

      /* something is very wrong with the duplicate request cache */
    case DUPREQ_ERROR:
      LogCrit(COMPONENT_DISPATCH,
              "DUP: Did not find the request in the duplicate request cache "
              "and couldn't add the request.");
      DISP_SLOCK(xprt);
      svcerr_systemerr(xprt, req);
      break;

      /* oom */
    case DUPREQ_INSERT_MALLOC_ERROR:
      LogCrit(COMPONENT_DISPATCH,
              "DUP: Cannot process request, not enough memory available!");
      DISP_SLOCK(xprt);
      svcerr_systemerr(xprt, req);
      break;

    default:
      LogCrit(COMPONENT_DISPATCH,
              "DUP: Unknown duplicate request cache status. This should never "
              "be reached!");
      DISP_SLOCK(xprt);
      svcerr_systemerr(xprt, req);
      break;
    }
#ifdef USE_DBUS_STATS
    server_stats_nfs_done(&req_ctx, preq, rc, true);
#endif
    goto freeargs;
  }

  /* Don't waste time for null or invalid ops
   * null op code in all valid protos == 0
   * and invalid protos all point to invalid_funcdesc
   * NFS v2 is set to invalid_funcdesc in nfs_rpc_get_funcdesc()
   */

  if(preqnfs->funcdesc == &invalid_funcdesc ||
     req->rq_proc == NFSPROC_NULL)
	  goto null_op;
  /* Get the export entry */
  if(req->rq_prog == nfs_param.core_param.program[P_NFS])
    {
      /* The NFSv3 functions' arguments always begin with the file
       * handle (but not the NULL function).  This hook is used to get the
       * fhandle with the arguments and so determine the export entry to be
       * used.  In NFSv4, junction traversal is managed by the protocol.
       */

      progname = "NFS";
      if(req->rq_vers == NFS_V3)
        {
	   protocol_options |= EXPORT_OPTION_NFSV3;
	   exportid = nfs3_FhandleToExportId((nfs_fh3 *) arg_nfs);
	   if(exportid < 0)
	      goto handle_err;
	   req_ctx.export = get_gsh_export(exportid, true);
	   if(req_ctx.export == NULL ||
	      (req_ctx.export->export.export_perms.options & EXPORT_OPTION_NFSV3) == 0)
	      goto handle_err;
	   /* privileged port only makes sense for V3.  V4 can go thru
	    * firewalls and so all bets are off
	    */
	   if((req_ctx.export->export.export_perms.options & EXPORT_OPTION_PRIVILEGED_PORT) &&
	       (port >= IPPORT_RESERVED))
	      {
		 LogInfo(COMPONENT_DISPATCH,
			 "Port %d is too high for this export entry, rejecting client",
			 port);
		 auth_rc = AUTH_TOOWEAK;
		 goto auth_failure;
	      }

              LogFullDebug(COMPONENT_DISPATCH,
                           "Found export entry for path=%s as exportid=%d",
                           req_ctx.export->export.fullpath, req_ctx.export->export.id);
        }
      else
        { /* NFS V4 gets its own export id from the ops in the compound */
           protocol_options |= EXPORT_OPTION_NFSV4;
	}
    }
  else if(req->rq_prog == nfs_param.core_param.program[P_NLM])
    {
      netobj *pfh3 = NULL;

      protocol_options |= EXPORT_OPTION_NFSV3;
      progname = "NLM";
      switch(req->rq_proc)
        {
          case NLMPROC4_NULL:
		  /* caught above and short circuited */
          case NLMPROC4_TEST_RES:
          case NLMPROC4_LOCK_RES:
          case NLMPROC4_CANCEL_RES:
          case NLMPROC4_UNLOCK_RES:
          case NLMPROC4_GRANTED_RES:
          case NLMPROC4_SM_NOTIFY:
          case NLMPROC4_FREE_ALL:
            break;

          case NLMPROC4_TEST:
          case NLMPROC4_TEST_MSG:
          case NLMPROC4_GRANTED:
          case NLMPROC4_GRANTED_MSG:
            pfh3 = &arg_nfs->arg_nlm4_test.alock.fh;
            break;

          case NLMPROC4_LOCK:
          case NLMPROC4_LOCK_MSG:
          case NLMPROC4_NM_LOCK:
            pfh3 = &arg_nfs->arg_nlm4_lock.alock.fh;
            break;

          case NLMPROC4_CANCEL:
          case NLMPROC4_CANCEL_MSG:
            pfh3 = &arg_nfs->arg_nlm4_cancel.alock.fh;
            break;

          case NLMPROC4_UNLOCK:
          case NLMPROC4_UNLOCK_MSG:
            pfh3 = &arg_nfs->arg_nlm4_unlock.alock.fh;
            break;

          case NLMPROC4_SHARE:
          case NLMPROC4_UNSHARE:
            pfh3 = &arg_nfs->arg_nlm4_share.share.fh;
            break;
        }
      if(pfh3 != NULL)
        {
	  exportid = nlm4_FhandleToExportId(pfh3);
          if(exportid < 0)
	      goto handle_err;
	  req_ctx.export = get_gsh_export(exportid, true);
	  if(req_ctx.export == NULL ||
	     (req_ctx.export->export.export_perms.options & EXPORT_OPTION_NFSV3) == 0)
	      goto handle_err;
          LogFullDebug(COMPONENT_DISPATCH,
                       "Found export entry for dirname=%s as exportid=%d",
                       req_ctx.export->export.fullpath, req_ctx.export->export.id);
        }
    } else if(req->rq_prog == nfs_param.core_param.program[P_MNT]) {
      progname = "MNT";
    }


  /* Only do access check if we have an export. */
  if((preqnfs->funcdesc->dispatch_behaviour & NEEDS_EXPORT) != 0)
    {
      xprt_type_t xprt_type = svc_get_xprt_type(xprt);

      LogFullDebug(COMPONENT_DISPATCH,
                   "nfs_rpc_execute about to call nfs_export_check_access for client %s",
                   req_ctx.client->hostaddr_str);

      nfs_export_check_access(req_ctx.caller_addr,
                              &req_ctx.export->export,
                              &export_perms);

      if(export_perms.options == 0)
        {
          LogInfo(COMPONENT_DISPATCH,
                  "Client %s is not allowed to access Export_Id %d %s, vers=%d, proc=%d",
                  req_ctx.client->hostaddr_str,
                  req_ctx.export->export.id, req_ctx.export->export.fullpath,
                  (int)req->rq_vers, (int)req->rq_proc);

          auth_rc = AUTH_TOOWEAK;
          goto auth_failure;
        }

      /* Check protocol version */
      if((protocol_options & EXPORT_OPTION_PROTOCOLS)== 0)
        {
          LogCrit(COMPONENT_DISPATCH,
                  "Problem, request requires export but does not have a protocol version");

          auth_rc = AUTH_FAILED;
          goto auth_failure;
        }

      if((protocol_options & export_perms.options) == 0)
        {
          LogInfo(COMPONENT_DISPATCH,
                  "%s Version %d not allowed on Export_Id %d %s for client %s",
                  progname, req->rq_vers,
                  req_ctx.export->export.id, req_ctx.export->export.fullpath,
                  req_ctx.client->hostaddr_str);

          auth_rc = AUTH_FAILED;
          goto auth_failure;
        }

      /* Check transport type */
      if(((xprt_type == XPRT_UDP) &&
          ((export_perms.options & EXPORT_OPTION_UDP) == 0)) ||
         ((xprt_type == XPRT_TCP) &&
          ((export_perms.options & EXPORT_OPTION_TCP) == 0)))
        {
          LogInfo(COMPONENT_DISPATCH,
                  "%s Version %d over %s not allowed on Export_Id %d %s for client %s",
                  progname, req->rq_vers, xprt_type_to_str(xprt_type),
                  req_ctx.export->export.id, req_ctx.export->export.fullpath,
                  req_ctx.client->hostaddr_str);

          auth_rc = AUTH_FAILED;
          goto auth_failure;
        }

      /* Test if export allows the authentication provided */
      if(((preqnfs->funcdesc->dispatch_behaviour & SUPPORTS_GSS) != 0) &&
         (nfs_export_check_security(req, &export_perms, &req_ctx.export->export) == FALSE))
        {
          LogInfo(COMPONENT_DISPATCH,
                  "%s Version %d auth not allowed on Export_Id %d %s for client %s",
                  progname, req->rq_vers,
                  req_ctx.export->export.id, req_ctx.export->export.fullpath,
                  req_ctx.client->hostaddr_str);

          auth_rc = AUTH_TOOWEAK;
          goto auth_failure;
        }

      /* Check if client is using a privileged port, but only for NFS protocol */
      if((req->rq_prog == nfs_param.core_param.program[P_NFS]) &&
         ((export_perms.options & EXPORT_OPTION_PRIVILEGED_PORT) != 0) &&
         (port >= IPPORT_RESERVED))
        {
          LogInfo(COMPONENT_DISPATCH,
                  "Non-reserved Port %d is not allowed on Export_Id %d %s for client %s",
                  port, req_ctx.export->export.id, req_ctx.export->export.fullpath,
                  req_ctx.client->hostaddr_str);

          auth_rc = AUTH_TOOWEAK;
          goto auth_failure;
        }
    }

  /* Get user credentials */
  if (preqnfs->funcdesc->dispatch_behaviour & NEEDS_CRED)
    {
      if (get_req_uid_gid(req, &user_credentials) == FALSE)
        {
          LogInfo(COMPONENT_DISPATCH,
                  "could not get uid and gid, rejecting client %s",
                  req_ctx.client->hostaddr_str);

          auth_rc = AUTH_TOOWEAK;
          goto auth_failure;
        }
    }

  /*
   * It is now time for checking if export list allows the machine to perform
   * the request
   */
  if((preqnfs->funcdesc->dispatch_behaviour & MAKES_IO) != 0 &&
     (export_perms.options & EXPORT_OPTION_RW_ACCESS) == 0)
    {
      /* Request of type MDONLY_RO were rejected at the nfs_rpc_dispatcher level
       * This is done by replying EDQUOT (this error is known for not disturbing
       * the client's requests cache
       */
      if(req->rq_prog == nfs_param.core_param.program[P_NFS])
        switch(req->rq_vers)
          {
          case NFS_V3:
            LogDebug(COMPONENT_DISPATCH,
                     "Returning NFS3ERR_DQUOT because request is on an MD Only export");
            res_nfs->res_getattr3.status = NFS3ERR_DQUOT;
            rc = NFS_REQ_OK;
            break;

          default:
            LogDebug(COMPONENT_DISPATCH,
                     "Dropping IO request on an MD Only export");
            rc = NFS_REQ_DROP;
            break;
          }
      else
        {
          LogDebug(COMPONENT_DISPATCH,
                   "Dropping IO request on an MD Only export");
          rc = NFS_REQ_DROP;
        }
    }
  else if((preqnfs->funcdesc->dispatch_behaviour & MAKES_WRITE) != 0 &&
          (export_perms.options & (EXPORT_OPTION_WRITE_ACCESS |
                                     EXPORT_OPTION_MD_WRITE_ACCESS)) == 0)
    {
      if(req->rq_prog == nfs_param.core_param.program[P_NFS])
        switch(req->rq_vers)
          {
          case NFS_V3:
            LogDebug(COMPONENT_DISPATCH,
                     "Returning NFS3ERR_ROFS because request is on a Read Only export");
            res_nfs->res_getattr3.status = NFS3ERR_ROFS;
            rc = NFS_REQ_OK;
            break;

          default:
            LogDebug(COMPONENT_DISPATCH,
                     "Dropping request on a Read Only export");
            rc = NFS_REQ_DROP;
            break;
          }
      else
        {
          LogDebug(COMPONENT_DISPATCH,
                   "Dropping request on a Read Only export");
          rc = NFS_REQ_DROP;
        }
    }
  else if((preqnfs->funcdesc->dispatch_behaviour & NEEDS_EXPORT) != 0 &&
          (export_perms.options & (EXPORT_OPTION_READ_ACCESS |
                                     EXPORT_OPTION_MD_READ_ACCESS)) == 0)
    {
      LogInfo(COMPONENT_DISPATCH,
              "Client %s is not allowed to access Export_Id %d %s, vers=%d, proc=%d",
	      req_ctx.client->hostaddr_str,
              req_ctx.export->export.id, req_ctx.export->export.fullpath,
              (int)req->rq_vers, (int)req->rq_proc);
      auth_rc = AUTH_TOOWEAK;
      goto auth_failure;
    }
  else
    {
      /* Do the authentication stuff, if needed */
      if((preqnfs->funcdesc->dispatch_behaviour &
          (NEEDS_CRED | NEEDS_EXPORT)) == (NEEDS_CRED | NEEDS_EXPORT))
        {
          /* Swap the anonymous uid/gid if the user should be anonymous */
          nfs_check_anon(&export_perms, &req_ctx.export->export, &user_credentials);
        }

      /* processing */

#ifdef _ERROR_INJECTION
      if(worker_delay_time != 0)
        sleep(worker_delay_time);
      else if(next_worker_delay_time != 0)
        {
          sleep(next_worker_delay_time);
          next_worker_delay_time = 0;
        }
#endif

    null_op:
      rc = preqnfs->funcdesc->service_function(
          arg_nfs,
          &req_ctx.export->export,
          &req_ctx,
          worker_data,
          req,
          res_nfs);
    }
#ifdef USE_DBUS_STATS
/* NFSv4 stats are handled in nfs4_compound()
 */
  if(req->rq_vers != NFS_V4)
	  server_stats_nfs_done(&req_ctx, preq, rc, false);
#endif

  /* If request is dropped, no return to the client */
  if(rc == NFS_REQ_DROP)
    {
      /* The request was dropped */
      LogDebug(COMPONENT_DISPATCH,
               "Drop request rpc_xid=%u, program %u, version %u, function %u",
               req->rq_xid, (int)req->rq_prog,
               (int)req->rq_vers, (int)req->rq_proc);

      /* If the request is not normally cached, then the entry will be removed
       * later.  We only remove a reply that is normally cached that has been
       * dropped. */
      if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
        {
          LogCrit(COMPONENT_DISPATCH,
                  "Attempt to delete duplicate request failed on line %d",
                  __LINE__);
        }
        goto freeargs;
    }
  else
    {
      LogFullDebug(COMPONENT_DISPATCH,
                   "Before svc_sendreply on socket %d",
                   xprt->xp_fd);

      DISP_SLOCK(xprt);

      /* encoding the result on xdr output */
      if(svc_sendreply(xprt, req, preqnfs->funcdesc->xdr_encode_func,
                       (caddr_t) res_nfs) == false)
        {
          LogDebug(COMPONENT_DISPATCH,
                  "NFS DISPATCHER: FAILURE: Error while calling "
                   "svc_sendreply on a new request. rpcxid=%u "
                   "socket=%d function:%s client:%s program:%d "
                   "nfs version:%d proc:%d xid:%u errno: %d",
                   req->rq_xid, xprt->xp_fd,
                   preqnfs->funcdesc->funcname,
                   req_ctx.client->hostaddr_str, (int)req->rq_prog,
                   (int)req->rq_vers, (int)req->rq_proc, req->rq_xid,
                   errno);
	  if (xprt->xp_type != XPRT_UDP)
	    svc_destroy(xprt);
          goto freeargs;
        }

      LogFullDebug(COMPONENT_DISPATCH,
                   "After svc_sendreply on socket %d",
                   xprt->xp_fd);

    } /* rc == NFS_REQ_DROP */


  /* Finish any request not already deleted */
  if (dpq_status == DUPREQ_SUCCESS)
      dpq_status = nfs_dupreq_finish(req, res_nfs);
  goto freeargs;

handle_err:
  /* Reject the request for authentication reason (incompatible
   * file handle) */
  if(isInfo(COMPONENT_DISPATCH))
    {
      char dumpfh[1024];
      char *reason;
      if(exportid < 0)
	      reason = "has badly formed handle";
      else if(req_ctx.export == NULL)
	      reason = "has invalid export";
      else
	      reason = "V3 not allowed on this export";
      sprint_fhandle3(dumpfh, (nfs_fh3 *) arg_nfs);
      LogMajor(COMPONENT_DISPATCH,
	       "%s Request from host %s %s, proc=%d, FH=%s",
	       progname,
	       req_ctx.client->hostaddr_str, reason,
	       (int)req->rq_proc, dumpfh);
    }
  auth_rc = AUTH_FAILED;

auth_failure:
  DISP_SLOCK(xprt);
  svcerr_auth(xprt, req, auth_rc);
  /* nb, a no-op when req is uncacheable */
  if (nfs_dupreq_delete(req) != DUPREQ_SUCCESS)
  {
	  LogCrit(COMPONENT_DISPATCH,
		  "Attempt to delete duplicate request failed on "
		  "line %d", __LINE__);
  }

freeargs:
  /* XXX no need for xprt slock across SVC_FREEARGS */
  DISP_SUNLOCK(xprt);

  /* Free the allocated resources once the work is done */
  /* Free the arguments */
  if((preqnfs->req.rq_vers == 2) ||
     (preqnfs->req.rq_vers == 3) ||
     (preqnfs->req.rq_vers == 4)) {
      if(! SVC_FREEARGS(xprt, preqnfs->funcdesc->xdr_decode_func,
                        (caddr_t) arg_nfs))
      {
        LogCrit(COMPONENT_DISPATCH,
                "NFS DISPATCHER: FAILURE: Bad SVC_FREEARGS for %s",
                preqnfs->funcdesc->funcname);
      }   
  }

  /* Finalize the request. */
  if (res_nfs)
	  nfs_dupreq_rele(req, preqnfs->funcdesc);

  if(req_ctx.client != NULL)
	  put_gsh_client(req_ctx.client);
  if(req_ctx.export != NULL)
	  put_gsh_export(req_ctx.export);
  return;
}

#ifdef _USE_9P
/**
 * @brief Execute a 9p request
 *
 * @param[in,out] req9p       9p request
 * @param[in,out] worker_data Worker's specific data
 */
static void _9p_execute( request_data_t *preq, 
                         nfs_worker_data_t *worker_data)
{
  _9p_request_data_t * req9p = &preq->r_u._9p ;  

  if( req9p->pconn->trans_type == _9P_TCP )
    _9p_tcp_process_request( req9p, worker_data ) ;
#ifdef _USE_9P_RDMA
  else if( req9p->pconn->trans_type == _9P_RDMA )
     _9p_rdma_process_request( req9p, worker_data ) ;
#endif

  return ;
} /* _9p_execute */


/**
 * @brief Free resources allocated for a 9p request
 *
 * This does not free the request itself.
 *
 * @param[in] nfsreq 9p request
 */
static void _9p_free_reqdata(_9p_request_data_t * preq9p)
{
  if( preq9p->pconn->trans_type == _9P_TCP )
          gsh_free( preq9p->_9pmsg );

  /* decrease connection refcount */
  atomic_dec_uint32_t(&preq9p->pconn->refcount);
}
#endif

/* XXX include dependency issue prevented declaring in nfs_req_queue.h */
request_data_t *nfs_rpc_dequeue_req(nfs_worker_data_t *worker);

static uint32_t worker_indexer = 0;

/**
 * @brief Initialize a worker thread
 *
 * @param[in] ctx Thread fridge context
 */

static void worker_thread_initializer(struct fridgethr_context *ctx)
{
  struct nfs_worker_data *wd
    = gsh_calloc(sizeof(struct nfs_worker_data), 1);
  char thr_name[32];

  wd->worker_index = atomic_inc_uint32_t(&worker_indexer);
  snprintf(thr_name, sizeof(thr_name), "Worker #%u", wd->worker_index);

  /* Initalize thr waitq */
  init_wait_q_entry(&wd->wqe);
  wd->ctx = ctx;
  ctx->thread_info = wd;
}

/**
 * @brief Finalize a worker thread
 *
 * @param[in] ctx Thread fridge context
 */

static void worker_thread_finalizer(struct fridgethr_context *ctx)
{
  gsh_free(ctx->thread_info);
  ctx->thread_info = NULL;
}

/**
 * @brief The main function for a worker thread
 *
 * This is the body of the worker thread. Its starting arguments are
 * located in global array worker_data. The argument is no pointer but
 * the worker's index.  It then uses this index to address its own
 * worker data in the array.
 *
 * @param[in] ctx Fridge thread context
 */

static void worker_run(struct fridgethr_context *ctx)
{
  struct nfs_worker_data *worker_data = ctx->thread_info;
  request_data_t *nfsreq;
  gsh_xprt_private_t *xu = NULL;
  uint32_t reqcnt;

  /* Worker's loop */
  while (!fridgethr_you_should_break(ctx))
    {
      nfsreq = nfs_rpc_dequeue_req(worker_data);

      if(!nfsreq)
	{
          continue;
	}

/* need to do a getpeername(2) on the socket fd before we dive into the
 * rpc_execute.  9p is messy but we do have the fd....
 */

      switch(nfsreq->rtype)
       {
       case NFS_REQUEST:
           /* check for destroyed xprts */
           xu = (gsh_xprt_private_t *) nfsreq->r_u.nfs->xprt->xp_u1;
           pthread_mutex_lock(&nfsreq->r_u.nfs->xprt->xp_lock);
           if (nfsreq->r_u.nfs->xprt->xp_flags & SVC_XPRT_FLAG_DESTROYED) {
               pthread_mutex_unlock(&nfsreq->r_u.nfs->xprt->xp_lock);
               goto finalize_req;
           }
           reqcnt = xu->req_cnt;
           pthread_mutex_unlock(&nfsreq->r_u.nfs->xprt->xp_lock);
           /* execute */
           LogDebug(COMPONENT_DISPATCH,
                    "NFS protocol request, nfsreq=%p xprt=%p req_cnt=%d",
                    nfsreq,
                    nfsreq->r_u.nfs->xprt,
                    reqcnt);
           nfs_rpc_execute(nfsreq, worker_data);
           break;

       case NFS_CALL:
           /* NFSv4 rpc call (callback) */
           nfs_rpc_dispatch_call(nfsreq->r_u.call, 0 /* XXX flags */);
           break;

#ifdef _USE_9P
       case _9P_REQUEST:
           _9p_execute(nfsreq, worker_data);
           break;
#endif
       }

    finalize_req:
           /* XXX needed? */
           LogFullDebug(COMPONENT_DISPATCH, "Signaling completion of request");

           switch(nfsreq->rtype) {
           case NFS_REQUEST:
               /* adjust req_cnt and return xprt ref */
               gsh_xprt_unref(nfsreq->r_u.nfs->xprt, XPRT_PRIVATE_FLAG_DECREQ,
                              __func__, __LINE__);
               break;
           case NFS_CALL:
               break;
           default:
               break;
           }

      /* Free the req by releasing the entry */
      LogFullDebug(COMPONENT_DISPATCH,
                   "Invalidating processed entry");
   
#ifdef _USE_9P
      if( nfsreq->rtype == _9P_REQUEST)
              _9p_free_reqdata(&nfsreq->r_u._9p);
      else
#endif
          switch(nfsreq->rtype) {
          case NFS_REQUEST:
              pool_free(request_data_pool, nfsreq->r_u.nfs);
              break;
          default:
              break;
          }

      pool_free(request_pool, nfsreq);
    }
}

int worker_init(void)
{
  struct fridgethr_params frp;
  int rc = 0;

  memset(&frp, 0, sizeof(struct fridgethr_params));
  frp.thr_max = nfs_param.core_param.nb_worker;
  frp.thr_min = nfs_param.core_param.nb_worker;
  frp.flavor = fridgethr_flavor_looper;
  frp.thread_initialize = worker_thread_initializer;
  frp.thread_finalize = worker_thread_finalizer;
  frp.wake_threads = nfs_rpc_queue_awaken;
  frp.wake_threads_arg = &nfs_req_st;

  rc = fridgethr_init(&worker_fridge,
		      "Wrk",
		      &frp);
  if (rc != 0)
    {
      LogMajor(COMPONENT_DISPATCH,
	       "Unable to initialize worker fridge: %d", rc);
      return rc;
    }

  rc = frigethr_populate(worker_fridge,
			 worker_run,
			 NULL);

  if (rc != 0)
    {
      LogMajor(COMPONENT_DISPATCH,
	       "Unable to populate worker fridge: %d", rc);
    }

  return rc;
}

int worker_shutdown(void)
{
  int rc = fridgethr_sync_command(worker_fridge,
				  fridgethr_comm_stop,
				  120);

  if (rc == ETIMEDOUT)
    {
      LogMajor(COMPONENT_DISPATCH,
	       "Shutdown timed out, cancelling threads.");
      fridgethr_cancel(worker_fridge);
    }
  else if (rc != 0)
    {
      LogMajor(COMPONENT_DISPATCH,
	       "Failed shutting down worker threads: %d",
	       rc);
    }
  return rc;
}

int worker_pause(void)
{
  int rc = fridgethr_sync_command(worker_fridge,
				  fridgethr_comm_pause,
				  120);

  if (rc != 0)
    {
      LogMajor(COMPONENT_DISPATCH,
	       "Failed pausing worker threads: %d",
	       rc);
    }
  return rc;
}

int worker_resume(void)
{
  int rc = fridgethr_sync_command(worker_fridge,
				  fridgethr_comm_run,
				  120);

  if (rc != 0)
    {
      LogMajor(COMPONENT_DISPATCH,
	       "Failed resuming worker threads: %d",
	       rc);
    }
  return rc;
}

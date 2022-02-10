/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 * Copyright CEA/DAM/DIF  (2008)
 * contributeur : Philippe DENIEL   philippe.deniel@cea.fr
 *                Thomas LEIBOVICI  thomas.leibovici@cea.fr
 *                William Allen Simpson <william.allen.simpson@gmail.com>
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
#include <sys/file.h>		/* for having FNDELAY */
#include <sys/signal.h>
#include <poll.h>
#include "hashtable.h"
#include "abstract_atomic.h"
#include "log.h"
#include "fsal.h"
#include "rquota.h"
#include "nfs_init.h"
#include "nfs_convert.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_dupreq.h"
#include "nfs_file_handle.h"
#include "client_mgr.h"
#include "export_mgr.h"
#include "server_stats.h"
#include "uid2grp.h"

#ifdef USE_LTTNG
#include "gsh_lttng/nfs_rpc.h"
#endif

#define NFS_pcp nfs_param.core_param
#define NFS_options NFS_pcp.core_options
#define NFS_program NFS_pcp.program

const nfs_function_desc_t invalid_funcdesc = {
	.service_function = nfs_null,
	.free_function = nfs_null_free,
	.xdr_decode_func = (xdrproc_t) xdr_void,
	.xdr_encode_func = (xdrproc_t) xdr_void,
	.funcname = "invalid_function",
	.dispatch_behaviour = NOTHING_SPECIAL
};

#ifdef _USE_NFS3
const nfs_function_desc_t nfs3_func_desc[] = {
	{
	 .service_function = nfs_null,
	 .free_function = nfs_null_free,
	 .xdr_decode_func = (xdrproc_t) xdr_void,
	 .xdr_encode_func = (xdrproc_t) xdr_void,
	 .funcname = "NFS3_NULL",
	 .dispatch_behaviour = NOTHING_SPECIAL},
	{
	 .service_function = nfs3_getattr,
	 .free_function = nfs3_getattr_free,
	 .xdr_decode_func = (xdrproc_t) xdr_GETATTR3args,
	 .xdr_encode_func = (xdrproc_t) xdr_GETATTR3res,
	 .funcname = "NFS3_GETATTR",
	 .dispatch_behaviour = NEEDS_CRED | SUPPORTS_GSS},
	{
	 .service_function = nfs3_setattr,
	 .free_function = nfs3_setattr_free,
	 .xdr_decode_func = (xdrproc_t) xdr_SETATTR3args,
	 .xdr_encode_func = (xdrproc_t) xdr_SETATTR3res,
	 .funcname = "NFS3_SETATTR",
	 .dispatch_behaviour =
	 (MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS)
	 },
	{
	 .service_function = nfs3_lookup,
	 .free_function = nfs3_lookup_free,
	 .xdr_decode_func = (xdrproc_t) xdr_LOOKUP3args,
	 .xdr_encode_func = (xdrproc_t) xdr_LOOKUP3res,
	 .funcname = "NFS3_LOOKUP",
	 .dispatch_behaviour = NEEDS_CRED | SUPPORTS_GSS},
	{
	 .service_function = nfs3_access,
	 .free_function = nfs3_access_free,
	 .xdr_decode_func = (xdrproc_t) xdr_ACCESS3args,
	 .xdr_encode_func = (xdrproc_t) xdr_ACCESS3res,
	 .funcname = "NFS3_ACCESS",
	 .dispatch_behaviour = NEEDS_CRED | SUPPORTS_GSS},
	{
	 .service_function = nfs3_readlink,
	 .free_function = nfs3_readlink_free,
	 .xdr_decode_func = (xdrproc_t) xdr_READLINK3args,
	 .xdr_encode_func = (xdrproc_t) xdr_READLINK3res,
	 .funcname = "NFS3_READLINK",
	 .dispatch_behaviour = NEEDS_CRED | SUPPORTS_GSS},
	{
	 .service_function = nfs3_read,
	 .free_function = nfs3_read_free,
	 .xdr_decode_func = (xdrproc_t) xdr_READ3args,
	 .xdr_encode_func = (xdrproc_t) xdr_READ3res,
	 .funcname = "NFS3_READ",
	 .dispatch_behaviour =
	 NEEDS_CRED | SUPPORTS_GSS | MAKES_IO},
	{
	 .service_function = nfs3_write,
	 .free_function = nfs3_write_free,
	 .xdr_decode_func = (xdrproc_t) xdr_WRITE3args,
	 .xdr_encode_func = (xdrproc_t) xdr_WRITE3res,
	 .funcname = "NFS3_WRITE",
	 .dispatch_behaviour =
	 (MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS |
	  MAKES_IO)
	 },
	{
	 .service_function = nfs3_create,
	 .free_function = nfs3_create_free,
	 .xdr_decode_func = (xdrproc_t) xdr_CREATE3args,
	 .xdr_encode_func = (xdrproc_t) xdr_CREATE3res,
	 .funcname = "NFS3_CREATE",
	 .dispatch_behaviour =
	 (MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS)
	 },
	{
	 .service_function = nfs3_mkdir,
	 .free_function = nfs3_mkdir_free,
	 .xdr_decode_func = (xdrproc_t) xdr_MKDIR3args,
	 .xdr_encode_func = (xdrproc_t) xdr_MKDIR3res,
	 .funcname = "NFS3_MKDIR",
	 .dispatch_behaviour =
	 (MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS)
	 },
	{
	 .service_function = nfs3_symlink,
	 .free_function = nfs3_symlink_free,
	 .xdr_decode_func = (xdrproc_t) xdr_SYMLINK3args,
	 .xdr_encode_func = (xdrproc_t) xdr_SYMLINK3res,
	 .funcname = "NFS3_SYMLINK",
	 .dispatch_behaviour =
	 (MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS)
	 },
	{
	 .service_function = nfs3_mknod,
	 .free_function = nfs3_mknod_free,
	 .xdr_decode_func = (xdrproc_t) xdr_MKNOD3args,
	 .xdr_encode_func = (xdrproc_t) xdr_MKNOD3res,
	 .funcname = "NFS3_MKNOD",
	 .dispatch_behaviour =
	 (MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS)
	 },
	{
	 .service_function = nfs3_remove,
	 .free_function = nfs3_remove_free,
	 .xdr_decode_func = (xdrproc_t) xdr_REMOVE3args,
	 .xdr_encode_func = (xdrproc_t) xdr_REMOVE3res,
	 .funcname = "NFS3_REMOVE",
	 .dispatch_behaviour =
	 (MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS)
	 },
	{
	 .service_function = nfs3_rmdir,
	 .free_function = nfs3_rmdir_free,
	 .xdr_decode_func = (xdrproc_t) xdr_RMDIR3args,
	 .xdr_encode_func = (xdrproc_t) xdr_RMDIR3res,
	 .funcname = "NFS3_RMDIR",
	 .dispatch_behaviour =
	 (MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS)
	 },
	{
	 .service_function = nfs3_rename,
	 .free_function = nfs3_rename_free,
	 .xdr_decode_func = (xdrproc_t) xdr_RENAME3args,
	 .xdr_encode_func = (xdrproc_t) xdr_RENAME3res,
	 .funcname = "NFS3_RENAME",
	 .dispatch_behaviour =
	 (MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS)
	 },
	{
	 .service_function = nfs3_link,
	 .free_function = nfs3_link_free,
	 .xdr_decode_func = (xdrproc_t) xdr_LINK3args,
	 .xdr_encode_func = (xdrproc_t) xdr_LINK3res,
	 .funcname = "NFS3_LINK",
	 .dispatch_behaviour =
	 (MAKES_WRITE | NEEDS_CRED | CAN_BE_DUP | SUPPORTS_GSS)
	 },
	{
	 .service_function = nfs3_readdir,
	 .free_function = nfs3_readdir_free,
	 .xdr_decode_func = (xdrproc_t) xdr_READDIR3args,
	 .xdr_encode_func = (xdrproc_t) xdr_READDIR3res,
	 .funcname = "NFS3_READDIR",
	 .dispatch_behaviour = (NEEDS_CRED | SUPPORTS_GSS)
	 },
	{
	 .service_function = nfs3_readdirplus,
	 .free_function = nfs3_readdirplus_free,
	 .xdr_decode_func = (xdrproc_t) xdr_READDIRPLUS3args,
	 .xdr_encode_func = (xdrproc_t) xdr_READDIRPLUS3res,
	 .funcname = "NFS3_READDIRPLUS",
	 .dispatch_behaviour = (NEEDS_CRED | SUPPORTS_GSS)
	 },
	{
	 .service_function = nfs3_fsstat,
	 .free_function = nfs3_fsstat_free,
	 .xdr_decode_func = (xdrproc_t) xdr_FSSTAT3args,
	 .xdr_encode_func = (xdrproc_t) xdr_FSSTAT3res,
	 .funcname = "NFS3_FSSTAT",
	 .dispatch_behaviour = (NEEDS_CRED | SUPPORTS_GSS)
	 },
	{
	 .service_function = nfs3_fsinfo,
	 .free_function = nfs3_fsinfo_free,
	 .xdr_decode_func = (xdrproc_t) xdr_FSINFO3args,
	 .xdr_encode_func = (xdrproc_t) xdr_FSINFO3res,
	 .funcname = "NFS3_FSINFO",
	 .dispatch_behaviour = (NEEDS_CRED)
	 },
	{
	 .service_function = nfs3_pathconf,
	 .free_function = nfs3_pathconf_free,
	 .xdr_decode_func = (xdrproc_t) xdr_PATHCONF3args,
	 .xdr_encode_func = (xdrproc_t) xdr_PATHCONF3res,
	 .funcname = "NFS3_PATHCONF",
	 .dispatch_behaviour = (NEEDS_CRED | SUPPORTS_GSS)
	 },
	{
	 .service_function = nfs3_commit,
	 .free_function = nfs3_commit_free,
	 .xdr_decode_func = (xdrproc_t) xdr_COMMIT3args,
	 .xdr_encode_func = (xdrproc_t) xdr_COMMIT3res,
	 .funcname = "NFS3_COMMIT",
	 .dispatch_behaviour = (MAKES_WRITE | NEEDS_CRED | SUPPORTS_GSS)}
};
#endif /* _USE_NFS3 */

/* Remeber that NFSv4 manages authentication though junction crossing, and
 * so does it for RO FS management (for each operation) */
const nfs_function_desc_t nfs4_func_desc[] = {
	{
	 .service_function = nfs_null,
	 .free_function = nfs_null_free,
	 .xdr_decode_func = (xdrproc_t) xdr_void,
	 .xdr_encode_func = (xdrproc_t) xdr_void,
	 .funcname = "NFS_NULL",
	 .dispatch_behaviour = NOTHING_SPECIAL},
	{
	 .service_function = nfs4_Compound,
	 .free_function = nfs4_Compound_Free,
	 .xdr_decode_func = (xdrproc_t) xdr_COMPOUND4args,
	 .xdr_encode_func = (xdrproc_t) xdr_COMPOUND4res_extended,
	 .funcname = "NFS4_COMP",
	 .dispatch_behaviour = CAN_BE_DUP}
};

#ifdef _USE_NFS3
const nfs_function_desc_t mnt1_func_desc[] = {
	{
	 .service_function = mnt_Null,
	 .free_function = mnt_Null_Free,
	 .xdr_decode_func = (xdrproc_t) xdr_void,
	 .xdr_encode_func = (xdrproc_t) xdr_void,
	 .funcname = "MNT_NULL",
	 .dispatch_behaviour = NOTHING_SPECIAL},
	{
	 .service_function = mnt_Mnt,
	 .free_function = mnt1_Mnt_Free,
	 .xdr_decode_func = (xdrproc_t) xdr_dirpath,
	 .xdr_encode_func = (xdrproc_t) xdr_fhstatus2,
	 .funcname = "MNT_MNT",
	 .dispatch_behaviour = NEEDS_CRED},
	{
	 .service_function = mnt_Dump,
	 .free_function = mnt_Dump_Free,
	 .xdr_decode_func = (xdrproc_t) xdr_void,
	 .xdr_encode_func = (xdrproc_t) xdr_mountlist,
	 .funcname = "MNT_DUMP",
	 .dispatch_behaviour = NOTHING_SPECIAL},
	{
	 .service_function = mnt_Umnt,
	 .free_function = mnt_Umnt_Free,
	 .xdr_decode_func = (xdrproc_t) xdr_dirpath,
	 .xdr_encode_func = (xdrproc_t) xdr_void,
	 .funcname = "MNT_UMNT",
	 .dispatch_behaviour = NOTHING_SPECIAL},
	{
	 .service_function = mnt_UmntAll,
	 .free_function = mnt_UmntAll_Free,
	 .xdr_decode_func = (xdrproc_t) xdr_void,
	 .xdr_encode_func = (xdrproc_t) xdr_void,
	 .funcname = "MNT_UMNTALL",
	 .dispatch_behaviour = NOTHING_SPECIAL},
	{
	 .service_function = mnt_Export,
	 .free_function = mnt_Export_Free,
	 .xdr_decode_func = (xdrproc_t) xdr_void,
	 .xdr_encode_func = (xdrproc_t) xdr_exports,
	 .funcname = "MNT_EXPORT",
	 .dispatch_behaviour = NOTHING_SPECIAL}
};

const nfs_function_desc_t mnt3_func_desc[] = {
	{
	 .service_function = mnt_Null,
	 .free_function = mnt_Null_Free,
	 .xdr_decode_func = (xdrproc_t) xdr_void,
	 .xdr_encode_func = (xdrproc_t) xdr_void,
	 .funcname = "MNT_NULL",
	 .dispatch_behaviour = NOTHING_SPECIAL},
	{
	 .service_function = mnt_Mnt,
	 .free_function = mnt3_Mnt_Free,
	 .xdr_decode_func = (xdrproc_t) xdr_dirpath,
	 .xdr_encode_func = (xdrproc_t) xdr_mountres3,
	 .funcname = "MNT_MNT",
	 .dispatch_behaviour = NOTHING_SPECIAL},
	{
	 .service_function = mnt_Dump,
	 .free_function = mnt_Dump_Free,
	 .xdr_decode_func = (xdrproc_t) xdr_void,
	 .xdr_encode_func = (xdrproc_t) xdr_mountlist,
	 .funcname = "MNT_DUMP",
	 .dispatch_behaviour = NOTHING_SPECIAL},
	{
	 .service_function = mnt_Umnt,
	 .free_function = mnt_Umnt_Free,
	 .xdr_decode_func = (xdrproc_t) xdr_dirpath,
	 .xdr_encode_func = (xdrproc_t) xdr_void,
	 .funcname = "MNT_UMNT",
	 .dispatch_behaviour = NOTHING_SPECIAL},
	{
	 .service_function = mnt_UmntAll,
	 .free_function = mnt_UmntAll_Free,
	 .xdr_decode_func = (xdrproc_t) xdr_void,
	 .xdr_encode_func = (xdrproc_t) xdr_void,
	 .funcname = "MNT_UMNTALL",
	 .dispatch_behaviour = NOTHING_SPECIAL},
	{
	 .service_function = mnt_Export,
	 .free_function = mnt_Export_Free,
	 .xdr_decode_func = (xdrproc_t) xdr_void,
	 .xdr_encode_func = (xdrproc_t) xdr_exports,
	 .funcname = "MNT_EXPORT",
	 .dispatch_behaviour = NOTHING_SPECIAL}
};
#endif

#ifdef _USE_NLM

#define nlm4_Unsupported nlm_Null
#define nlm4_Unsupported_Free nlm_Null_Free

const nfs_function_desc_t nlm4_func_desc[] = {
	[NLMPROC4_NULL] = {
			   .service_function = nlm_Null,
			   .free_function = nlm_Null_Free,
			   .xdr_decode_func = (xdrproc_t) xdr_void,
			   .xdr_encode_func = (xdrproc_t) xdr_void,
			   .funcname = "NLM_NULL",
			   .dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_TEST] = {
			   .service_function = nlm4_Test,
			   .free_function = nlm4_Test_Free,
			   .xdr_decode_func = (xdrproc_t) xdr_nlm4_testargs,
			   .xdr_encode_func = (xdrproc_t) xdr_nlm4_testres,
			   .funcname = "NLM4_TEST",
			   .dispatch_behaviour = NEEDS_CRED},
	[NLMPROC4_LOCK] = {
			   .service_function = nlm4_Lock,
			   .free_function = nlm4_Lock_Free,
			   .xdr_decode_func = (xdrproc_t) xdr_nlm4_lockargs,
			   .xdr_encode_func = (xdrproc_t) xdr_nlm4_res,
			   .funcname = "NLM4_LOCK",
			   .dispatch_behaviour = NEEDS_CRED},
	[NLMPROC4_CANCEL] = {
			     .service_function = nlm4_Cancel,
			     .free_function = nlm4_Cancel_Free,
			     .xdr_decode_func = (xdrproc_t) xdr_nlm4_cancargs,
			     .xdr_encode_func = (xdrproc_t) xdr_nlm4_res,
			     .funcname = "NLM4_CANCEL",
			     .dispatch_behaviour = NEEDS_CRED},
	[NLMPROC4_UNLOCK] = {
			     .service_function = nlm4_Unlock,
			     .free_function = nlm4_Unlock_Free,
			     .xdr_decode_func = (xdrproc_t) xdr_nlm4_unlockargs,
			     .xdr_encode_func = (xdrproc_t) xdr_nlm4_res,
			     .funcname = "NLM4_UNLOCK",
			     .dispatch_behaviour = NEEDS_CRED},
	[NLMPROC4_GRANTED] = {
			      .service_function = nlm4_Unsupported,
			      .free_function = nlm4_Unsupported_Free,
			      .xdr_decode_func = (xdrproc_t) xdr_void,
			      .xdr_encode_func = (xdrproc_t) xdr_void,
			      .funcname = "NLM4_GRANTED",
			      .dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_TEST_MSG] = {
			       .service_function = nlm4_Test_Message,
			       .free_function = nlm4_Test_Free,
			       .xdr_decode_func = (xdrproc_t) xdr_nlm4_testargs,
			       .xdr_encode_func = (xdrproc_t) xdr_void,
			       .funcname = "NLM4_TEST_MSG",
			       .dispatch_behaviour = NEEDS_CRED},
	[NLMPROC4_LOCK_MSG] = {
			       .service_function = nlm4_Lock_Message,
			       .free_function = nlm4_Lock_Free,
			       .xdr_decode_func = (xdrproc_t) xdr_nlm4_lockargs,
			       .xdr_encode_func = (xdrproc_t) xdr_void,
			       .funcname = "NLM4_LOCK_MSG",
			       .dispatch_behaviour = NEEDS_CRED},
	[NLMPROC4_CANCEL_MSG] = {
				 .service_function = nlm4_Cancel_Message,
				 .free_function = nlm4_Cancel_Free,
				 .xdr_decode_func =
				 (xdrproc_t) xdr_nlm4_cancargs,
				 .xdr_encode_func = (xdrproc_t) xdr_void,
				 .funcname = "NLM4_CANCEL_MSG",
				 .dispatch_behaviour =
				 NEEDS_CRED},
	[NLMPROC4_UNLOCK_MSG] = {
				 .service_function = nlm4_Unlock_Message,
				 .free_function = nlm4_Unlock_Free,
				 .xdr_decode_func =
				 (xdrproc_t) xdr_nlm4_unlockargs,
				 .xdr_encode_func = (xdrproc_t) xdr_void,
				 .funcname = "NLM4_UNLOCK_MSG",
				 .dispatch_behaviour =
				 NEEDS_CRED},
	[NLMPROC4_GRANTED_MSG] = {
				  .service_function = nlm4_Unsupported,
				  .free_function = nlm4_Unsupported_Free,
				  .xdr_decode_func = (xdrproc_t) xdr_void,
				  .xdr_encode_func = (xdrproc_t) xdr_void,
				  .funcname = "NLM4_GRANTED_MSG",
				  .dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_TEST_RES] = {
			       .service_function = nlm4_Unsupported,
			       .free_function = nlm4_Unsupported_Free,
			       .xdr_decode_func = (xdrproc_t) xdr_void,
			       .xdr_encode_func = (xdrproc_t) xdr_void,
			       .funcname = "NLM4_TEST_RES",
			       .dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_LOCK_RES] = {
			       .service_function = nlm4_Unsupported,
			       .free_function = nlm4_Unsupported_Free,
			       .xdr_decode_func = (xdrproc_t) xdr_void,
			       .xdr_encode_func = (xdrproc_t) xdr_void,
			       .funcname = "NLM4_LOCK_RES",
			       .dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_CANCEL_RES] = {
				 .service_function = nlm4_Unsupported,
				 .free_function = nlm4_Unsupported_Free,
				 .xdr_decode_func = (xdrproc_t) xdr_void,
				 .xdr_encode_func = (xdrproc_t) xdr_void,
				 .funcname = "NLM4_CANCEL_RES",
				 .dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_UNLOCK_RES] = {
				 .service_function = nlm4_Unsupported,
				 .free_function = nlm4_Unsupported_Free,
				 .xdr_decode_func = (xdrproc_t) xdr_void,
				 .xdr_encode_func = (xdrproc_t) xdr_void,
				 .funcname = "NLM4_UNLOCK_RES",
				 .dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_GRANTED_RES] = {
				  .service_function = nlm4_Granted_Res,
				  .free_function = nlm4_Granted_Res_Free,
				  .xdr_decode_func = (xdrproc_t) xdr_nlm4_res,
				  .xdr_encode_func = (xdrproc_t) xdr_void,
				  .funcname = "NLM4_GRANTED_RES",
				  .dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_SM_NOTIFY] = {
				.service_function = nlm4_Sm_Notify,
				.free_function = nlm4_Sm_Notify_Free,
				.xdr_decode_func =
				(xdrproc_t) xdr_nlm4_sm_notifyargs,
				.xdr_encode_func = (xdrproc_t) xdr_void,
				.funcname = "NLM4_SM_NOTIFY",
				.dispatch_behaviour = NOTHING_SPECIAL},
	[17] = {
		.service_function = nlm4_Unsupported,
		.free_function = nlm4_Unsupported_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "NLM4_GRANTED_RES",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[18] = {
		.service_function = nlm4_Unsupported,
		.free_function = nlm4_Unsupported_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "NLM4_GRANTED_RES",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[19] = {
		.service_function = nlm4_Unsupported,
		.free_function = nlm4_Unsupported_Free,
		.xdr_decode_func = (xdrproc_t) xdr_void,
		.xdr_encode_func = (xdrproc_t) xdr_void,
		.funcname = "NLM4_GRANTED_RES",
		.dispatch_behaviour = NOTHING_SPECIAL},
	[NLMPROC4_SHARE] = {
			    .service_function = nlm4_Share,
			    .free_function = nlm4_Share_Free,
			    .xdr_decode_func = (xdrproc_t) xdr_nlm4_shareargs,
			    .xdr_encode_func = (xdrproc_t) xdr_nlm4_shareres,
			    .funcname = "NLM4_SHARE",
			    .dispatch_behaviour = NEEDS_CRED},
	[NLMPROC4_UNSHARE] = {
			      .service_function = nlm4_Unshare,
			      .free_function = nlm4_Unshare_Free,
			      .xdr_decode_func = (xdrproc_t) xdr_nlm4_shareargs,
			      .xdr_encode_func = (xdrproc_t) xdr_nlm4_shareres,
			      .funcname = "NLM4_UNSHARE",
			      .dispatch_behaviour = NEEDS_CRED},
	[NLMPROC4_NM_LOCK] = {
			      /* NLM_NM_LOCK uses the same handling as NLM_LOCK
			       * except for monitoring, nlm4_Lock will make
			       * that determination.
			       */
			      .service_function = nlm4_Lock,
			      .free_function = nlm4_Lock_Free,
			      .xdr_decode_func = (xdrproc_t) xdr_nlm4_lockargs,
			      .xdr_encode_func = (xdrproc_t) xdr_nlm4_res,
			      .funcname = "NLM4_NM_LOCK",
			      .dispatch_behaviour = NEEDS_CRED},
	[NLMPROC4_FREE_ALL] = {
			       .service_function = nlm4_Free_All,
			       .free_function = nlm4_Free_All_Free,
			       .xdr_decode_func =
			       (xdrproc_t) xdr_nlm4_free_allargs,
			       .xdr_encode_func = (xdrproc_t) xdr_void,
			       .funcname = "NLM4_FREE_ALL",
			       .dispatch_behaviour = NOTHING_SPECIAL},
};
#endif /* _USE_NLM */

#ifdef _USE_RQUOTA
const nfs_function_desc_t rquota1_func_desc[] = {
	[0] = {
	       .service_function = rquota_Null,
	       .free_function = rquota_Null_Free,
	       .xdr_decode_func = (xdrproc_t) xdr_void,
	       .xdr_encode_func = (xdrproc_t) xdr_void,
	       .funcname = "RQUOTA_NULL",
	       .dispatch_behaviour = NOTHING_SPECIAL},
	[RQUOTAPROC_GETQUOTA] = {
				 .service_function = rquota_getquota,
				 .free_function = rquota_getquota_Free,
				 .xdr_decode_func =
				 (xdrproc_t) xdr_getquota_args,
				 .xdr_encode_func =
				 (xdrproc_t) xdr_getquota_rslt,
				 .funcname = "RQUOTA_GETQUOTA",
				 .dispatch_behaviour = NEEDS_CRED},
	[RQUOTAPROC_GETACTIVEQUOTA] = {
				       .service_function =
				       rquota_getactivequota,
				       .free_function =
				       rquota_getactivequota_Free,
				       .xdr_decode_func =
				       (xdrproc_t) xdr_getquota_args,
				       .xdr_encode_func =
				       (xdrproc_t) xdr_getquota_rslt,
				       .funcname = "RQUOTA_GETACTIVEQUOTA",
				       .dispatch_behaviour = NEEDS_CRED},
	[RQUOTAPROC_SETQUOTA] = {
				 .service_function = rquota_setquota,
				 .free_function = rquota_setquota_Free,
				 .xdr_decode_func =
				 (xdrproc_t) xdr_setquota_args,
				 .xdr_encode_func =
				 (xdrproc_t) xdr_setquota_rslt,
				 .funcname = "RQUOTA_SETACTIVEQUOTA",
				 .dispatch_behaviour = NEEDS_CRED},
	[RQUOTAPROC_SETACTIVEQUOTA] = {
				       .service_function =
				       rquota_setactivequota,
				       .free_function =
				       rquota_setactivequota_Free,
				       .xdr_decode_func =
				       (xdrproc_t) xdr_setquota_args,
				       .xdr_encode_func =
				       (xdrproc_t) xdr_setquota_rslt,
				       .funcname = "RQUOTA_GETACTIVEQUOTA",
				       .dispatch_behaviour = NEEDS_CRED}
};

const nfs_function_desc_t rquota2_func_desc[] = {
	[0] = {
	       .service_function = rquota_Null,
	       .free_function = rquota_Null_Free,
	       .xdr_decode_func = (xdrproc_t) xdr_void,
	       .xdr_encode_func = (xdrproc_t) xdr_void,
	       .funcname = "RQUOTA_NULL",
	       .dispatch_behaviour = NOTHING_SPECIAL},
	[RQUOTAPROC_GETQUOTA] = {
				 .service_function = rquota_getquota,
				 .free_function = rquota_getquota_Free,
				 .xdr_decode_func =
				 (xdrproc_t) xdr_ext_getquota_args,
				 .xdr_encode_func =
				 (xdrproc_t) xdr_getquota_rslt,
				 .funcname = "RQUOTA_EXT_GETQUOTA",
				 .dispatch_behaviour = NEEDS_CRED},
	[RQUOTAPROC_GETACTIVEQUOTA] = {
				       .service_function =
				       rquota_getactivequota,
				       .free_function =
				       rquota_getactivequota_Free,
				       .xdr_decode_func =
				       (xdrproc_t) xdr_ext_getquota_args,
				       .xdr_encode_func =
				       (xdrproc_t) xdr_getquota_rslt,
				       .funcname = "RQUOTA_EXT_GETACTIVEQUOTA",
				       .dispatch_behaviour = NEEDS_CRED},
	[RQUOTAPROC_SETQUOTA] = {
				 .service_function = rquota_setquota,
				 .free_function = rquota_setquota_Free,
				 .xdr_decode_func =
				 (xdrproc_t) xdr_ext_setquota_args,
				 .xdr_encode_func =
				 (xdrproc_t) xdr_setquota_rslt,
				 .funcname = "RQUOTA_EXT_SETACTIVEQUOTA",
				 .dispatch_behaviour = NEEDS_CRED},
	[RQUOTAPROC_SETACTIVEQUOTA] = {
				       .service_function =
				       rquota_setactivequota,
				       .free_function =
				       rquota_setactivequota_Free,
				       .xdr_decode_func =
				       (xdrproc_t) xdr_ext_setquota_args,
				       .xdr_encode_func =
				       (xdrproc_t) xdr_setquota_rslt,
				       .funcname = "RQUOTA_EXT_GETACTIVEQUOTA",
				       .dispatch_behaviour = NEEDS_CRED}
};
#endif

#ifdef USE_NFSACL3
const nfs_function_desc_t nfsacl_func_desc[] = {
	[0] = {
	       .service_function = nfsacl_Null,
	       .free_function = nfsacl_Null_Free,
	       .xdr_decode_func = (xdrproc_t) xdr_void,
	       .xdr_encode_func = (xdrproc_t) xdr_void,
	       .funcname = "NFSACL_NULL",
	       .dispatch_behaviour = NOTHING_SPECIAL},
	[NFSACLPROC_GETACL] = {
				 .service_function = nfsacl_getacl,
				 .free_function = nfsacl_getacl_Free,
				 .xdr_decode_func =
				 (xdrproc_t) xdr_getaclargs,
				 .xdr_encode_func =
				 (xdrproc_t) xdr_getaclres,
				 .funcname = "NFSACL_GETACL",
				 .dispatch_behaviour = NEEDS_CRED},
	[NFSACLPROC_SETACL] = {
				 .service_function = nfsacl_setacl,
				 .free_function = nfsacl_setacl_Free,
				 .xdr_decode_func =
				 (xdrproc_t) xdr_setaclargs,
				 .xdr_encode_func =
				 (xdrproc_t) xdr_setaclres,
				 .funcname = "NFSACL_SETACL",
				 .dispatch_behaviour = NEEDS_CRED}
};
#endif

void auth_failure(nfs_request_t *reqdata, enum auth_stat auth_rc)
{
	svcerr_auth(&reqdata->svc, auth_rc);
	/* nb, a no-op when req is uncacheable */
	nfs_dupreq_delete(reqdata, NFS_REQ_AUTH_ERR);
}

void free_args(nfs_request_t *reqdata)
{
	const nfs_function_desc_t *reqdesc = reqdata->funcdesc;

	/* Free the allocated resources once the work is done */
	/* Free the arguments */
	if ((reqdata->svc.rq_msg.cb_vers == 2)
	 || (reqdata->svc.rq_msg.cb_vers == 3)
	 || (reqdata->svc.rq_msg.cb_vers == 4)) {
		if (!xdr_free(reqdesc->xdr_decode_func,
			      &reqdata->arg_nfs)) {
			LogCrit(COMPONENT_DISPATCH,
				"%s FAILURE: Bad xdr_free for %s",
				__func__,
				reqdesc->funcname);
		}
	}

	/* Finalize the request. */
	nfs_dupreq_rele(reqdata);

	SetClientIP(NULL);

	/* Clean up the op_ctx */
	if (op_ctx->client != NULL) {
		put_gsh_client(op_ctx->client);
		op_ctx->client = NULL;
	}

	clean_credentials();
	release_op_context();

#ifdef USE_LTTNG
	tracepoint(nfs_rpc, end, reqdata);
#endif
}

enum nfs_req_result complete_request(nfs_request_t *reqdata,
				     enum nfs_req_result rc)
{
	SVCXPRT *xprt = reqdata->svc.rq_xprt;
	const nfs_function_desc_t *reqdesc = reqdata->funcdesc;

	/* NFSv4 stats are handled in nfs4_compound() */
	if (reqdata->svc.rq_msg.cb_prog != NFS_program[P_NFS]
	    || reqdata->svc.rq_msg.cb_vers != NFS_V4)
		server_stats_nfs_done(reqdata, rc, false);

	/* If request is dropped, no return to the client */
	if (rc == NFS_REQ_DROP) {
		/* The request was dropped */
		LogDebug(COMPONENT_DISPATCH,
			 "Drop request rpc_xid=%" PRIu32
			 ", program %" PRIu32
			 ", version %" PRIu32
			 ", function %" PRIu32,
			 reqdata->svc.rq_msg.rm_xid,
			 reqdata->svc.rq_msg.cb_prog,
			 reqdata->svc.rq_msg.cb_vers,
			 reqdata->svc.rq_msg.cb_proc);

		/* If the request is not normally cached, then the entry
		 * will be removed later.  We only remove a reply that is
		 * normally cached that has been dropped.
		 */
		nfs_dupreq_delete(reqdata, rc);
		return rc;
	}

	LogFullDebug(COMPONENT_DISPATCH,
		     "Before svc_sendreply on socket %d", xprt->xp_fd);

	reqdata->svc.rq_msg.RPCM_ack.ar_results.where = reqdata->res_nfs;
	reqdata->svc.rq_msg.RPCM_ack.ar_results.proc =
				reqdesc->xdr_encode_func;

#ifdef USE_LTTNG
	tracepoint(nfs_rpc, before_reply, __func__, __LINE__, xprt);
#endif
	if (svc_sendreply(&reqdata->svc) >= XPRT_DIED) {
		LogDebug(COMPONENT_DISPATCH,
			 "NFS DISPATCHER: FAILURE: Error while calling svc_sendreply on a new request. rpcxid=%"
			 PRIu32
			 " socket=%d function:%s client:%s program:%"
			 PRIu32
			 " nfs version:%" PRIu32
			 " proc:%" PRIu32
			 " errno: %d",
			 reqdata->svc.rq_msg.rm_xid,
			 xprt->xp_fd,
			 reqdesc->funcname,
			 op_ctx->client->hostaddr_str,
			 reqdata->svc.rq_msg.cb_prog,
			 reqdata->svc.rq_msg.cb_vers,
			 reqdata->svc.rq_msg.cb_proc,
			 errno);
		SVC_DESTROY(xprt);
		rc = NFS_REQ_XPRT_DIED;
	}

	LogFullDebug(COMPONENT_DISPATCH,
		     "After svc_sendreply on socket %d", xprt->xp_fd);

	/* Finish any request not already deleted */
	nfs_dupreq_finish(reqdata, rc);
	return rc;
}

void complete_request_instrumentation(nfs_request_t *reqdata)
{
#ifdef USE_LTTNG
	tracepoint(nfs_rpc, op_end, reqdata);
#endif
}

/** @brief Completion of async RPC dispatch
 *
 * This function is called by an RPC op handler that has previously returned
 * NFS_REQ_ASYNC to complete the operation and send the reply and clean up the
 * RPC transport.
 *
 * @param[in] reqdata    The request data for the operation
 * @param[in] rc         NFS_REQ_OK or NFS_REQ_DROP
 */
void nfs_rpc_complete_async_request(nfs_request_t *reqdata,
				    enum nfs_req_result rc)
{
	complete_request_instrumentation(reqdata);
	rc = complete_request(reqdata, rc);
	free_args(reqdata);
}

enum nfs_req_result process_dupreq(nfs_request_t *reqdata,
				   const char *client_ip)
{
	enum xprt_stat xprt_rc;

	/* Found the request in the dupreq cache.
	 * Send cached reply. */
	LogFullDebug(COMPONENT_DISPATCH,
		     "DUP: DupReq Cache Hit: using previous reply, rpcxid=%"
		     PRIu32,
		     reqdata->svc.rq_msg.rm_xid);

	LogFullDebug(COMPONENT_DISPATCH,
		     "Before svc_sendreply on socket %d (dup req)",
		     reqdata->svc.rq_xprt->xp_fd);

	reqdata->svc.rq_msg.RPCM_ack.ar_results.where = reqdata->res_nfs;
	reqdata->svc.rq_msg.RPCM_ack.ar_results.proc =
				reqdata->funcdesc->xdr_encode_func;

#ifdef USE_LTTNG
	tracepoint(nfs_rpc, before_reply, __func__, __LINE__,
		   reqdata->svc.rq_xprt);
#endif
	xprt_rc = svc_sendreply(&reqdata->svc);

	if (xprt_rc >= XPRT_DIED) {
		LogDebug(COMPONENT_DISPATCH,
			 "NFS DISPATCHER: FAILURE: Error while calling svc_sendreply on a duplicate request. rpcxid=%"
			 PRIu32
			 " socket=%d function:%s client:%s program:%"
			 PRIu32
			 " nfs version:%" PRIu32
			 " proc:%" PRIu32
			 " errno: %d",
			 reqdata->svc.rq_msg.rm_xid,
			 reqdata->svc.rq_xprt->xp_fd,
			 reqdata->funcdesc->funcname,
			 client_ip,
			 reqdata->svc.rq_msg.cb_prog,
			 reqdata->svc.rq_msg.cb_vers,
			 reqdata->svc.rq_msg.cb_proc,
			 errno);
		svcerr_systemerr(&reqdata->svc);
		return NFS_REQ_XPRT_DIED;
	}

	return NFS_REQ_OK;
}

/**
 * @brief Main RPC dispatcher routine
 *
 * @param[in,out] reqdata	NFS request
 * @param[in]     retry         True if a dupe request is coming back in
 *
 */
static enum xprt_stat nfs_rpc_process_request(nfs_request_t *reqdata,
					      bool retry)
{
	const char *client_ip = "<unknown client>";
	const char *progname = "unknown";
	const nfs_function_desc_t *reqdesc = reqdata->funcdesc;
	nfs_arg_t *arg_nfs = &reqdata->arg_nfs;
	SVCXPRT *xprt = reqdata->svc.rq_xprt;
	XDR *xdrs = reqdata->svc.rq_xdrs;
	dupreq_status_t dpq_status;
	enum auth_stat auth_rc = AUTH_OK;
	int port;
	int rc = NFS_REQ_OK;
#ifdef _USE_NFS3
	int exportid = -1;
#endif /* _USE_NFS3 */
	bool no_dispatch = false;

	if (retry)
		goto retry_after_drc_suspend;

#ifdef USE_LTTNG
	tracepoint(nfs_rpc, start, reqdata);
#endif

	LogFullDebug(COMPONENT_DISPATCH,
		     "About to authenticate Prog=%" PRIu32
		     ", vers=%" PRIu32
		     ", proc=%" PRIu32
		     ", xid=%" PRIu32
		     ", SVCXPRT=%p, fd=%d",
		     reqdata->svc.rq_msg.cb_prog,
		     reqdata->svc.rq_msg.cb_vers,
		     reqdata->svc.rq_msg.cb_proc,
		     reqdata->svc.rq_msg.rm_xid,
		     xprt, xprt->xp_fd);

	/* If authentication is AUTH_NONE or AUTH_UNIX, then the value of
	 * no_dispatch remains false and the request proceeds normally.
	 *
	 * If authentication is RPCSEC_GSS, no_dispatch may have value true,
	 * this means that gc->gc_proc != RPCSEC_GSS_DATA and that the message
	 * is in fact an internal negotiation message from RPCSEC_GSS using
	 * GSSAPI. It should not be processed by the worker and SVC_STAT
	 * should be returned to the dispatcher.
	 */
	auth_rc = svc_auth_authenticate(&reqdata->svc, &no_dispatch);
	if (auth_rc != AUTH_OK) {
		LogInfo(COMPONENT_DISPATCH,
			"Could not authenticate request... rejecting with AUTH_STAT=%s",
			auth_stat2str(auth_rc));
		return svcerr_auth(&reqdata->svc, auth_rc);
#ifdef _HAVE_GSSAPI
	} else if (reqdata->svc.rq_msg.RPCM_ack.ar_verf.oa_flavor
		   == RPCSEC_GSS) {
		struct rpc_gss_cred *gc = (struct rpc_gss_cred *)
			reqdata->svc.rq_msg.rq_cred_body;

		LogFullDebug(COMPONENT_DISPATCH,
			     "RPCSEC_GSS no_dispatch=%d gc->gc_proc=(%"
			     PRIu32 ") %s",
			     no_dispatch, gc->gc_proc,
			     str_gc_proc(gc->gc_proc));
		if (no_dispatch)
			return SVC_STAT(xprt);
	} else if (no_dispatch) {
		LogFullDebug(COMPONENT_DISPATCH,
			     "RPCSEC_GSS no_dispatch=%d", no_dispatch);
		if (reqdata->svc.rq_msg.cb_cred.oa_flavor
			== RPCSEC_GSS) {
			struct rpc_gss_cred *gc = (struct rpc_gss_cred *)
				reqdata->svc.rq_msg.rq_cred_body;

			if (gc->gc_proc == RPCSEC_GSS_DATA)
				return svcerr_auth(&reqdata->svc,
						   RPCSEC_GSS_CREDPROBLEM);
		}
		return SVC_STAT(xprt);
#endif
	}

	/*
	 * Extract RPC argument.
	 */
	LogFullDebug(COMPONENT_DISPATCH,
		     "Before SVCAUTH_CHECKSUM on SVCXPRT %p fd %d",
		     xprt, xprt->xp_fd);

	memset(arg_nfs, 0, sizeof(nfs_arg_t));
	reqdata->svc.rq_msg.rm_xdr.where = arg_nfs;
	reqdata->svc.rq_msg.rm_xdr.proc = reqdesc->xdr_decode_func;
	xdrs->x_public = &reqdata->lookahead;

	if (!SVCAUTH_CHECKSUM(&reqdata->svc)) {
		LogInfo(COMPONENT_DISPATCH,
			"SVCAUTH_CHECKSUM failed for Program %" PRIu32
			", Version %" PRIu32
			", Function %" PRIu32
			", xid=%" PRIu32
			", SVCXPRT=%p, fd=%d",
			reqdata->svc.rq_msg.cb_prog,
			reqdata->svc.rq_msg.cb_vers,
			reqdata->svc.rq_msg.cb_proc,
			reqdata->svc.rq_msg.rm_xid,
			xprt, xprt->xp_fd);

		if (!xdr_free(reqdesc->xdr_decode_func, arg_nfs)) {
			LogCrit(COMPONENT_DISPATCH,
				"%s FAILURE: Bad xdr_free for %s",
				__func__,
				reqdesc->funcname);
		}
		return svcerr_decode(&reqdata->svc);
	}

	/* set up the request context
	 */
	init_op_context(&reqdata->op_context, NULL, NULL,
			(sockaddr_t *)svc_getrpccaller(xprt),
			reqdata->svc.rq_msg.cb_vers, 0, NFS_REQUEST);

	/* Set up initial export permissions that don't allow anything. */
	export_check_access();

	/* start the processing clock
	 * we measure all time stats as intervals (elapsed nsecs) from
	 * server boot time.  This gets high precision with simple 64 bit math.
	 */
	now(&op_ctx->start_time);

	/* Initialized user_credentials */
	init_credentials();

	/* XXX must hold lock when calling any TI-RPC channel function,
	 * including svc_sendreply2 and the svcerr_* calls */

	/* XXX also, need to check UDP correctness, this may need some more
	 * TI-RPC work (for UDP, if we -really needed it-, we needed to
	 * capture hostaddr at SVC_RECV).  For TCP, if we intend to use
	 * this, we should sprint a buffer once, in when we're setting up
	 * xprt private data. */

	op_ctx->client = get_gsh_client(op_ctx->caller_addr, false);

	if (op_ctx->client == NULL) {
		LogDebug(COMPONENT_DISPATCH,
			 "Cannot get client block for Program %" PRIu32
			 ", Version %" PRIu32
			 ", Function %" PRIu32,
			 reqdata->svc.rq_msg.cb_prog,
			 reqdata->svc.rq_msg.cb_vers,
			 reqdata->svc.rq_msg.cb_proc);
	} else {
		/* Set the Client IP for this thread */
		SetClientIP(op_ctx->client->hostaddr_str);
		client_ip = op_ctx->client->hostaddr_str;
		LogDebug(COMPONENT_DISPATCH,
			 "Request from %s for Program %" PRIu32
			 ", Version %" PRIu32
			 ", Function %" PRIu32
			 " has xid=%" PRIu32,
			 client_ip,
			 reqdata->svc.rq_msg.cb_prog,
			 reqdata->svc.rq_msg.cb_vers,
			 reqdata->svc.rq_msg.cb_proc,
			 reqdata->svc.rq_msg.rm_xid);
	}

	/* If req is uncacheable, or if req is v41+, nfs_dupreq_start will do
	 * nothing but allocate a result object and mark the request (ie, the
	 * path is short, lockless, and does no hash/search). */
	dpq_status = nfs_dupreq_start(reqdata);

	if (dpq_status == DUPREQ_SUCCESS) {
		/* A new request, continue processing it. */
		LogFullDebug(COMPONENT_DISPATCH,
			     "Current request is not duplicate or not cacheable.");
	} else {
		switch (dpq_status) {
		case DUPREQ_EXISTS:
			(void) process_dupreq(reqdata, client_ip);
			break;

			/* Another thread owns the request */
		case DUPREQ_BEING_PROCESSED:
			LogFullDebug(COMPONENT_DISPATCH,
				     "Suspending DUP: Request xid=%" PRIu32
				     " is already being processed; the active thread will reply",
				     reqdata->svc.rq_msg.rm_xid);
			/* The request is suspended, don't touch the request in
			 * any way because the resume may already be scheduled
			 * and running on nother thread. The xp_resume_cb has
			 * already been set up before we started proecessing
			 * ops on this request at all.
			 */
			suspend_op_context();
			return XPRT_SUSPEND;

		case DUPREQ_DROP:
			LogFullDebug(COMPONENT_DISPATCH,
				     "DUP: Request xid=%" PRIu32
				     " is already being processed and has too many dupes queued",
				     reqdata->svc.rq_msg.rm_xid);
			/* Free the arguments */
			/* Ignore the request, send no error */
			break;

		default:
			LogCrit(COMPONENT_DISPATCH,
				"DUP: Unknown duplicate request cache status. This should never be reached!");
			svcerr_systemerr(&reqdata->svc);
			break;
		}
		server_stats_nfs_done(reqdata, rc, true);
		goto freeargs;
	}

retry_after_drc_suspend:
	/* If we come here on a retry after drc suspend, then we already did
	 * the stuff above.
	 */

	/* We need the port below. */
	port = get_port(op_ctx->caller_addr);

	/* Don't waste time for null or invalid ops
	 * null op code in all valid protos == 0
	 * and invalid protos all point to invalid_funcdesc
	 * NFS v2 is set to invalid_funcdesc in nfs_rpc_get_funcdesc()
	 */

	if (reqdesc == &invalid_funcdesc
	    || reqdata->svc.rq_msg.cb_proc == NFSPROC_NULL)
		goto null_op;
	/* Get the export entry */
	if (reqdata->svc.rq_msg.cb_prog == NFS_program[P_NFS]
#ifdef USE_NFSACL3
	    || reqdata->svc.rq_msg.cb_prog == NFS_program[P_NFSACL]
#endif
	    ) {
		/* The NFSv3 functions' arguments always begin with the file
		 * handle (but not the NULL function).  This hook is used to
		 * get the fhandle with the arguments and so determine the
		 * export entry to be used.  In NFSv4, junction traversal
		 * is managed by the protocol.
		 */

		progname = "NFS";
#ifdef _USE_NFS3
		if (reqdata->svc.rq_msg.cb_vers == NFS_V3) {
			exportid = nfs3_FhandleToExportId((nfs_fh3 *) arg_nfs);

			if (exportid < 0) {
				LogInfo(COMPONENT_DISPATCH,
					"NFS3 Request from client %s has badly formed handle",
					client_ip);

				/* Bad handle, report to client */
				reqdata->res_nfs->res_getattr3.status =
							NFS3ERR_BADHANDLE;
				rc = NFS_REQ_OK;
				goto req_error;
			}

			set_op_context_export(get_gsh_export(exportid));

			if (op_ctx->ctx_export == NULL) {
				LogInfoAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
					"NFS3 Request from client %s has invalid export %d",
					client_ip, exportid);

				/* Bad export, report to client */
				reqdata->res_nfs->res_getattr3.status =
								NFS3ERR_STALE;
				rc = NFS_REQ_OK;
				goto req_error;
			}

			LogMidDebugAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
				    "Found export entry for path=%s as exportid=%d",
				    op_ctx_export_path(op_ctx),
				    op_ctx->ctx_export->export_id);
		}
#endif /* _USE_NFS3 */
		/* NFS V4 gets its own export id from the ops
		 * in the compound */
#ifdef _USE_NLM
	} else if (reqdata->svc.rq_msg.cb_prog == NFS_program[P_NLM]) {
		netobj *pfh3 = NULL;

		progname = "NLM";

		switch (reqdata->svc.rq_msg.cb_proc) {
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
		if (pfh3 != NULL) {
			exportid = nlm4_FhandleToExportId(pfh3);

			if (exportid < 0) {
				/* We need to send a NLM4_STALE_FH response
				 * (NLM doesn't have an error code for
				 * BADHANDLE), but we don't know how to do that
				 * here, we will send a NULL pexport to NLM
				 * routine to let it know what to do since it
				 * can respond to ASYNC calls.
				 */
				LogInfo(COMPONENT_DISPATCH,
					"NLM4 Request from client %s has badly formed handle",
					client_ip);

			} else {
				set_op_context_export(get_gsh_export(exportid));

				if (op_ctx->ctx_export == NULL) {
					LogInfoAlt(COMPONENT_DISPATCH,
						   COMPONENT_EXPORT,
						   "NLM4 Request from client %s has invalid export %d",
						   client_ip,
						   exportid);

					/* We need to send a NLM4_STALE_FH
					 * response (NLM doesn't have an error
					 * code for BADHANDLE), but we don't
					 * know how to do that here, we will
					 * send a NULL pexport to NLM routine
					 * to let it know what to do since it
					 * can respond to ASYNC calls.
					 */
				} else {
					LogMidDebugAlt(COMPONENT_DISPATCH,
						COMPONENT_EXPORT,
						"Found export entry for dirname=%s as exportid=%d",
						ctx_export_path(op_ctx),
						op_ctx->ctx_export->export_id);
				}
			}
		}
#endif /* _USE_NLM */
#ifdef _USE_NFS3
	} else if (reqdata->svc.rq_msg.cb_prog == NFS_program[P_MNT]) {
		progname = "MNT";
#endif /* _USE_NFS3 */
	}

	/* Only do access check if we have an export. */
	if (op_ctx->ctx_export != NULL) {
		/* We ONLY get here for NFS v3 or NLM requests with a handle */
		xprt_type_t xprt_type = svc_get_xprt_type(xprt);

		LogMidDebugAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
			    "%s about to call nfs_export_check_access for client %s",
			    __func__, client_ip);

		export_check_access();

		if ((op_ctx->export_perms.options &
		     EXPORT_OPTION_ACCESS_MASK) == 0) {
			LogInfoAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
				"Client %s is not allowed to access Export_Id %d %s, vers=%"
				PRIu32 ", proc=%" PRIu32,
				client_ip,
				op_ctx->ctx_export->export_id,
				op_ctx_export_path(op_ctx),
				reqdata->svc.rq_msg.cb_vers,
				reqdata->svc.rq_msg.cb_proc);

			auth_failure(reqdata, AUTH_TOOWEAK);
			goto freeargs;
		}

		if ((op_ctx->export_perms.options &
		     EXPORT_OPTION_NFSV3) == 0) {
			LogInfoAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
				"%s Version %" PRIu32
				" not allowed on Export_Id %d %s for client %s",
				progname,
				reqdata->svc.rq_msg.cb_vers,
				op_ctx->ctx_export->export_id,
				op_ctx_export_path(op_ctx),
				client_ip);

			auth_failure(reqdata, AUTH_FAILED);
			goto freeargs;
		}

		/* Check transport type */
		if (((xprt_type == XPRT_UDP)
		     && ((op_ctx->export_perms.options &
			  EXPORT_OPTION_UDP) == 0))
		    || ((xprt_type == XPRT_TCP)
			&& ((op_ctx->export_perms.options &
			     EXPORT_OPTION_TCP) == 0))) {
			LogInfoAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
				"%s Version %" PRIu32
				" over %s not allowed on Export_Id %d %s for client %s",
				progname,
				reqdata->svc.rq_msg.cb_vers,
				xprt_type_to_str(xprt_type),
				op_ctx->ctx_export->export_id,
				op_ctx_export_path(op_ctx),
				client_ip);

			auth_failure(reqdata, AUTH_FAILED);
			goto freeargs;
		}

		/* Test if export allows the authentication provided */
		if ((reqdesc->dispatch_behaviour & SUPPORTS_GSS)
		 && !export_check_security(&reqdata->svc)) {
			LogInfoAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
				"%s Version %" PRIu32
				" auth not allowed on Export_Id %d %s for client %s",
				progname,
				reqdata->svc.rq_msg.cb_vers,
				op_ctx->ctx_export->export_id,
				op_ctx_export_path(op_ctx),
				client_ip);

			auth_failure(reqdata, AUTH_TOOWEAK);
			goto freeargs;
		}

		/* Check if client is using a privileged port,
		 * but only for NFS protocol */
		if ((reqdata->svc.rq_msg.cb_prog == NFS_program[P_NFS])
		    && (op_ctx->export_perms.options &
			EXPORT_OPTION_PRIVILEGED_PORT)
		    && (port >= IPPORT_RESERVED)) {
			LogInfoAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
				"Non-reserved Port %d is not allowed on Export_Id %d %s for client %s",
				port, op_ctx->ctx_export->export_id,
				op_ctx_export_path(op_ctx),
				client_ip);

			auth_failure(reqdata, AUTH_TOOWEAK);
			goto freeargs;
		}
	}

	/*
	 * It is now time for checking if export list allows the machine
	 * to perform the request
	 */
	if (op_ctx->ctx_export != NULL
	    && (reqdesc->dispatch_behaviour & MAKES_IO)
	    && !(op_ctx->export_perms.options & EXPORT_OPTION_RW_ACCESS)) {
		/* Request of type MDONLY_RO were rejected at the
		 * nfs_rpc_dispatcher level.
		 * This is done by replying EDQUOT
		 * (this error is known for not disturbing
		 * the client's requests cache)
		 */
		if (reqdata->svc.rq_msg.cb_prog == NFS_program[P_NFS])
			switch (reqdata->svc.rq_msg.cb_vers) {
#ifdef _USE_NFS3
			case NFS_V3:
				LogDebugAlt(COMPONENT_DISPATCH,
					    COMPONENT_EXPORT,
					    "Returning NFS3ERR_DQUOT because request is on an MD Only export");
				reqdata->res_nfs->res_getattr3.status =
								NFS3ERR_DQUOT;
				rc = NFS_REQ_OK;
				break;
#endif /* _USE_NFS3 */

			default:
				LogDebugAlt(COMPONENT_DISPATCH,
					    COMPONENT_EXPORT,
					    "Dropping IO request on an MD Only export");
				rc = NFS_REQ_DROP;
				break;
		} else {
			LogDebugAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
				 "Dropping IO request on an MD Only export");
			rc = NFS_REQ_DROP;
		}
	} else if (op_ctx->ctx_export != NULL
		   && (reqdesc->dispatch_behaviour & MAKES_WRITE)
		   && (op_ctx->export_perms.options
		       & (EXPORT_OPTION_WRITE_ACCESS
			| EXPORT_OPTION_MD_WRITE_ACCESS)) == 0) {
		if (reqdata->svc.rq_msg.cb_prog == NFS_program[P_NFS])
			switch (reqdata->svc.rq_msg.cb_vers) {
#ifdef _USE_NFS3
			case NFS_V3:
				LogDebugAlt(COMPONENT_DISPATCH,
					    COMPONENT_EXPORT,
					    "Returning NFS3ERR_ROFS because request is on a Read Only export");
				reqdata->res_nfs->res_getattr3.status =
								NFS3ERR_ROFS;
				rc = NFS_REQ_OK;
				break;
#endif /* _USE_NFS3 */

			default:
				LogDebugAlt(COMPONENT_DISPATCH,
					    COMPONENT_EXPORT,
					    "Dropping request on a Read Only export");
				rc = NFS_REQ_DROP;
				break;
		} else {
			LogDebugAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
				 "Dropping request on a Read Only export");
			rc = NFS_REQ_DROP;
		}
	} else if (op_ctx->ctx_export != NULL
		   && (op_ctx->export_perms.options
		       & (EXPORT_OPTION_READ_ACCESS
			 | EXPORT_OPTION_MD_READ_ACCESS)) == 0) {
		LogInfoAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
			"Client %s is not allowed to access Export_Id %d %s, vers=%"
			PRIu32 ", proc=%" PRIu32,
			client_ip, op_ctx->ctx_export->export_id,
			op_ctx_export_path(op_ctx),
			reqdata->svc.rq_msg.cb_vers,
			reqdata->svc.rq_msg.cb_proc);
		auth_failure(reqdata, AUTH_TOOWEAK);
		goto freeargs;
	} else {
		/* Get user credentials */
		if (reqdesc->dispatch_behaviour & NEEDS_CRED) {
			/* If we don't have an export, don't squash */
			if (op_ctx->fsal_export == NULL) {
				op_ctx->export_perms.options &=
					~EXPORT_OPTION_SQUASH_TYPES;
			} else if (nfs_req_creds(&reqdata->svc) != NFS4_OK) {
				LogInfoAlt(COMPONENT_DISPATCH, COMPONENT_EXPORT,
					"could not get uid and gid, rejecting client %s",
					client_ip);

				auth_failure(reqdata, AUTH_TOOWEAK);
				goto freeargs;
			}
		}

		/* processing
		 * At this point, op_ctx->ctx_export has one of the following
		 * conditions:
		 * non-NULL - valid handle for NFS v3 or NLM functions
		 *            that take handles
		 * NULL - For NULL RPC calls
		 * NULL - for RQUOTAD calls
		 * NULL - for NFS v4 COMPOUND call
		 * NULL - for MOUNT calls
		 * NULL - for NLM calls where handle is bad, NLM must handle
		 *        response in the case of async "MSG" calls, so we
		 *        just defer to NLM routines to respond with
		 *        NLM4_STALE_FH (NLM doesn't have a BADHANDLE code)
		 */

#ifdef _ERROR_INJECTION
		if (worker_delay_time != 0)
			sleep(worker_delay_time);
		else if (next_worker_delay_time != 0) {
			sleep(next_worker_delay_time);
			next_worker_delay_time = 0;
		}
#endif

 null_op:

#ifdef USE_LTTNG
		tracepoint(nfs_rpc, op_start, reqdata,
			   reqdesc->funcname,
			   (op_ctx->ctx_export != NULL
			    ? op_ctx->ctx_export->export_id : -1));
#endif

		rc = reqdesc->service_function(arg_nfs, &reqdata->svc,
					       reqdata->res_nfs);

		if (rc == NFS_REQ_ASYNC_WAIT) {
			/* The request is suspended, don't touch the request in
			 * any way because the resume may already be scheduled
			 * and running on nother thread. The xp_resume_cb has
			 * already been set up before we started proecessing
			 * ops on this request at all.
			 */
			suspend_op_context();
			return XPRT_SUSPEND;
		}

		complete_request_instrumentation(reqdata);
	}

#ifdef _USE_NFS3
 req_error:
#endif /* _USE_NFS3 */

	rc = complete_request(reqdata, rc);

 freeargs:

	free_args(reqdata);

	/* Make sure no-one called init_op_context() without calling
	 * release_op_context() */
	assert(op_ctx == NULL);
	/* Make sure we return to ntirpc without op_ctx set, or saved_op_ctx can
	 * point to freed memory */
	op_ctx = NULL;
	return SVC_STAT(xprt);
}

/** @brief Resume a duplicate request
 *
 * When a duplicate request comes in while still processing the original request
 * we suspend it, and queue it in the dupreq_entry_t. Now the original request
 * has been finished and we are ready to respond to the duplicate request.
 *
 */
enum xprt_stat drc_resume(struct svc_req *req)
{
	nfs_request_t *reqdata = container_of(req, nfs_request_t, svc);
	int rc = NFS_REQ_OK;

	/* Restore the op_ctx */
	resume_op_context(&reqdata->op_context);

	/* Get the result of sending the response. Note that we are ONLY here
	 * if this request came in as a dupe while we were processing the
	 * original reqyest and it has now completed. So either we just sent a
	 * response and there is no need to respond to this dupe OR something
	 * went wrong that warrants further attention.
	 */
	rc = nfs_dupreq_reply_rc(reqdata);

	server_stats_nfs_done(reqdata, rc, true);

	switch (rc) {
	case NFS_REQ_OK:
	case NFS_REQ_ERROR:
		/* In these cases, we don't need to respond */
		LogFullDebug(COMPONENT_DISPATCH,
			     "Suspended DUP: Request xid=%" PRIu32
			     " was processed and replied to",
			     reqdata->svc.rq_msg.rm_xid);
		break;

	case NFS_REQ_DROP:
		/* The original request was dropped for some reason, we actually
		 * need to process now... Resubmit this request.
		 */
		return nfs_rpc_process_request(reqdata, true);

	case NFS_REQ_REPLAY:
	case NFS_REQ_ASYNC_WAIT:
		/* These cases can never happen... */
		break;

	case NFS_REQ_XPRT_DIED:
		/* The attempt to send a response failed because the XPRT died
		 * so now we need to actually resend the response here.
		 */
		rc = process_dupreq(reqdata, op_ctx->client != NULL
					     ? op_ctx->client->hostaddr_str
					     : "<unknown client>");

		/* And now we need to try and finish this request. If we had
		 * another XPRT_DIED and there's yet another duplicate request
		 * queued, we will be back around with that request shortly...
		 */
		nfs_dupreq_finish(reqdata, rc);
		break;

	case NFS_REQ_AUTH_ERR:
		/* The original request failed due to an auth error. This retry
		 * will probably also similarly fail, but we will re-submit
		 * anyway.
		 */
		return nfs_rpc_process_request(reqdata, true);
	}

	free_args(reqdata);

	/* Make sure no-one called init_op_context() without calling
	 * release_op_context() */
	assert(op_ctx == NULL);
	/* Make sure we return to ntirpc without op_ctx set, or saved_op_ctx can
	 * point to freed memory */
	op_ctx = NULL;
	return SVC_STAT(reqdata->svc.rq_xprt);
}

/**
 * @brief Report Invalid Program number
 *
 * @param[in] reqnfs	NFS request
 *
 */
static enum xprt_stat nfs_rpc_noprog(nfs_request_t *reqdata)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "Invalid Program number %" PRIu32,
		     reqdata->svc.rq_msg.cb_prog);
	return svcerr_noprog(&reqdata->svc);
}

/**
 * @brief Report Invalid protocol Version
 *
 * @param[in] reqnfs	NFS request
 *
 */
static enum xprt_stat nfs_rpc_novers(nfs_request_t *reqdata,
				     int lo_vers, int hi_vers)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "Invalid protocol Version %" PRIu32
		     " for Program number %" PRIu32,
		     reqdata->svc.rq_msg.cb_vers,
		     reqdata->svc.rq_msg.cb_prog);
	return svcerr_progvers(&reqdata->svc, lo_vers, hi_vers);
}

/**
 * @brief Report Invalid Procedure
 *
 * @param[in] reqnfs	NFS request
 *
 */
static enum xprt_stat nfs_rpc_noproc(nfs_request_t *reqdata)
{
	LogFullDebug(COMPONENT_DISPATCH,
		     "Invalid Procedure %" PRIu32
		     " in protocol Version %" PRIu32
		     " for Program number %" PRIu32,
		     reqdata->svc.rq_msg.cb_proc,
		     reqdata->svc.rq_msg.cb_vers,
		     reqdata->svc.rq_msg.cb_prog);
	return svcerr_noproc(&reqdata->svc);
}

/**
 * @brief Validate rpc calls, extract nfs function descriptor.
 *
 * Validate the rpc call program, version, and procedure within range.
 * Send svcerr_* reply on errors.
 *
 * Choose the function descriptor, either a valid one or the default
 * invalid handler.
 *
 * @param[in,out] req service request
 *
 * @return whether the request is valid.
 */
enum xprt_stat nfs_rpc_valid_NFS(struct svc_req *req)
{
	nfs_request_t *reqdata =
			container_of(req, struct nfs_request, svc);
	int lo_vers;
	int hi_vers;
#ifdef USE_LTTNG
	SVCXPRT *xprt = reqdata->svc.rq_xprt;

	tracepoint(nfs_rpc, valid, __func__, __LINE__, xprt,
		   (unsigned int) req->rq_msg.cb_prog,
		   (unsigned int) req->rq_msg.cb_vers,
		   (unsigned int) reqdata->svc.rq_msg.cb_proc);
#endif

	reqdata->funcdesc = &invalid_funcdesc;

#ifdef USE_NFSACL3
	if (req->rq_msg.cb_prog == NFS_program[P_NFSACL]) {
		if (req->rq_msg.cb_vers == NFSACL_V3 && CORE_OPTION_NFSV3) {
			if (req->rq_msg.cb_proc <= NFSACLPROC_SETACL) {
				reqdata->funcdesc =
					&nfsacl_func_desc[req->rq_msg.cb_proc];
				return nfs_rpc_process_request(reqdata, false);
			}
		}
	}
#endif /* USE_NFSACL3 */

	if (req->rq_msg.cb_prog != NFS_program[P_NFS]) {
		return nfs_rpc_noprog(reqdata);
	}

	if (req->rq_msg.cb_vers == NFS_V4 && NFS_options & CORE_OPTION_NFSV4) {
		if (req->rq_msg.cb_proc <= NFSPROC4_COMPOUND) {
			reqdata->funcdesc =
				&nfs4_func_desc[req->rq_msg.cb_proc];
			return nfs_rpc_process_request(reqdata, false);
		}
		return nfs_rpc_noproc(reqdata);
	}

#ifdef _USE_NFS3
	if (req->rq_msg.cb_vers == NFS_V3 && NFS_options & CORE_OPTION_NFSV3) {
		if (req->rq_msg.cb_proc <= NFSPROC3_COMMIT) {
			reqdata->funcdesc =
				&nfs3_func_desc[req->rq_msg.cb_proc];
			return nfs_rpc_process_request(reqdata, false);
		}
		return nfs_rpc_noproc(reqdata);
	}
#endif /* _USE_NFS3 */

	/* Unsupported version! Set low and high versions correctly */
	if (NFS_options & CORE_OPTION_NFSV4)
		hi_vers = NFS_V4;
	else
		hi_vers = NFS_V3;
#ifdef _USE_NFS3
	if (NFS_options & CORE_OPTION_NFSV3)
		lo_vers = NFS_V3;
	else
		lo_vers = NFS_V4;
#else
	lo_vers = NFS_V4;
#endif

	return nfs_rpc_novers(reqdata, lo_vers, hi_vers);
}

#ifdef _USE_NLM
enum xprt_stat nfs_rpc_valid_NLM(struct svc_req *req)
{
	nfs_request_t *reqdata =
			container_of(req, struct nfs_request, svc);

	reqdata->funcdesc = &invalid_funcdesc;

	if (req->rq_msg.cb_prog == NFS_program[P_NLM]
	     && (NFS_options & CORE_OPTION_NFSV3)) {
		if (req->rq_msg.cb_vers == NLM4_VERS) {
			if (req->rq_msg.cb_proc <= NLMPROC4_FREE_ALL) {
				reqdata->funcdesc =
					&nlm4_func_desc[req->rq_msg.cb_proc];
				return nfs_rpc_process_request(reqdata, false);
			}
			return nfs_rpc_noproc(reqdata);
		}
		return nfs_rpc_novers(reqdata, NLM4_VERS, NLM4_VERS);
	}
	return nfs_rpc_noprog(reqdata);
}
#endif /* _USE_NLM */

#ifdef _USE_NFS3
enum xprt_stat nfs_rpc_valid_MNT(struct svc_req *req)
{
	nfs_request_t *reqdata =
			container_of(req, struct nfs_request, svc);

	reqdata->funcdesc = &invalid_funcdesc;

	if (req->rq_msg.cb_prog == NFS_program[P_MNT]
	    && (NFS_options & CORE_OPTION_NFSV3)) {
		reqdata->lookahead.flags |= NFS_LOOKAHEAD_MOUNT;

		/* Some clients may use the wrong mount version to
		 * umount, so always allow umount. Otherwise, only allow
		 * request if the appropriate mount version is enabled.
		 * Also need to allow dump and export, so just disallow
		 * mount if version not supported.
		 */
		if (req->rq_msg.cb_vers == MOUNT_V3) {
			if (req->rq_msg.cb_proc <= MOUNTPROC3_EXPORT) {
				reqdata->funcdesc =
					&mnt3_func_desc[req->rq_msg.cb_proc];
				return nfs_rpc_process_request(reqdata, false);
			}
			return nfs_rpc_noproc(reqdata);
		}
		if (req->rq_msg.cb_vers == MOUNT_V1) {
			if (req->rq_msg.cb_proc <= MOUNTPROC2_EXPORT
			    && req->rq_msg.cb_proc != MOUNTPROC2_MNT) {
				reqdata->funcdesc =
					&mnt1_func_desc[req->rq_msg.cb_proc];
				return nfs_rpc_process_request(reqdata, false);
			}
			return nfs_rpc_noproc(reqdata);
		}
		return nfs_rpc_novers(reqdata, MOUNT_V1, MOUNT_V3);
	}
	return nfs_rpc_noprog(reqdata);
}
#endif

#ifdef _USE_RQUOTA
enum xprt_stat nfs_rpc_valid_RQUOTA(struct svc_req *req)
{
	nfs_request_t *reqdata =
			container_of(req, struct nfs_request, svc);

	reqdata->funcdesc = &invalid_funcdesc;

	if (req->rq_msg.cb_prog == NFS_program[P_RQUOTA]) {
		if (req->rq_msg.cb_vers == EXT_RQUOTAVERS) {
			if (req->rq_msg.cb_proc <= RQUOTAPROC_SETACTIVEQUOTA) {
				reqdata->funcdesc =
					&rquota2_func_desc[req->rq_msg.cb_proc];
				return nfs_rpc_process_request(reqdata, false);
			}
			return nfs_rpc_noproc(reqdata);
		}
		if (req->rq_msg.cb_vers == RQUOTAVERS) {
			if (req->rq_msg.cb_proc <= RQUOTAPROC_SETACTIVEQUOTA) {
				reqdata->funcdesc =
					&rquota1_func_desc[req->rq_msg.cb_proc];
				return nfs_rpc_process_request(reqdata, false);
			}
			return nfs_rpc_noproc(reqdata);
		}
		return nfs_rpc_novers(reqdata, RQUOTAVERS, EXT_RQUOTAVERS);
	}
	return nfs_rpc_noprog(reqdata);
}
#endif

#ifdef USE_NFSACL3
enum xprt_stat nfs_rpc_valid_NFSACL(struct svc_req *req)
{
	nfs_request_t *reqdata =
			container_of(req, struct nfs_request, svc);

	reqdata->funcdesc = &invalid_funcdesc;

	if (req->rq_msg.cb_prog == NFS_program[P_NFSACL]) {
		if (req->rq_msg.cb_vers == NFSACL_V3) {
			if (req->rq_msg.cb_proc <= NFSACLPROC_SETACL) {
				reqdata->funcdesc =
					&nfsacl_func_desc[req->rq_msg.cb_proc];
				return nfs_rpc_process_request(reqdata, false);
			}
			return nfs_rpc_noproc(reqdata);
		}
		return nfs_rpc_novers(reqdata, NFSACL_V3, NFSACL_V3);
	}
	return nfs_rpc_noprog(reqdata);
}
#endif

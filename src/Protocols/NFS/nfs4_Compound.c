/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
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
 * @file    nfs4_Compound.c
 * @brief   Routines used for managing the NFS4 COMPOUND functions.
 *
 * Routines used for managing the NFS4 COMPOUND functions.
 *
 */
#include "config.h"
#include "fsal.h"
#include "sal_functions.h"
#include "nfs_convert.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "server_stats.h"
#include "export_mgr.h"
#include "nfs_creds.h"

#ifdef USE_LTTNG
#include "gsh_lttng/nfs_rpc.h"
#endif

static enum nfs_req_result nfs4_default_resume(struct nfs_argop4 *op,
					       compound_data_t *data,
					       struct nfs_resop4 *resp)
{
	return NFS_REQ_OK;
}

/**
 * #brief Structure to map out how each compound op is managed.
 *
 */
struct nfs4_op_desc {
	/** Operation name */
	char *name;
	/** Function to process the operation */
	nfs4_function_t funct;
	/** Function to resume a suspended operation */
	nfs4_function_t resume;

	/** Function to free the results of the operation.
	 *
	 * Note this function is called whether the operation succeeds or
	 * fails. It may be called as a result of higher level operation
	 * completion (depending on DRC handling) or it may be called as part
	 * of NFS v4.1 slot cache management.
	 *
	 * Note that entries placed into the NFS v4.1 slot cache are marked so
	 * the higher level operation completion will not release them. A deep
	 * copy is made when the slot cache is replayed. If sa_cachethis
	 * indicates a response will not be cached, the higher level operaiton
	 * completion will call the free_res, HOWEVER, a shallow copy of the
	 * SEQUENCE op and first operation responses are made. If the first
	 * operation resulted in an error (other than NFS4_DENIED for LOCK and
	 * LOCKT) the shallow copy preserves that error rather than replacing
	 * it with NFS4ERR_RETRY_UNCACHED_REP. For this reason for any response
	 * that includes dyanmically allocated data on NFS4_OK MUST check the
	 * response status before freeing any memory since the shallow copy will
	 * mean the cached NFS4ERR_RETRY_UNCACHED_REP response will have copied
	 * those pointers. It should only free data if the status is NFS4_OK
	 * (or NFS4ERR_DENIED in the case of LOCK and LOCKT). Note that
	 * SETCLIENTID also has dunamic data on a non-NFS4_OK status, and the
	 * free_res function for that checks, howwever, we will never see
	 * SETCLIENTID in NFS v4.1+, or if we do, it will get an error.
	 *
	 * At this time, LOCK and LOCKT are the only NFS v4.1 or v4.2 operations
	 * that have dynamic data on a non-NFS4_OK response. Should any others
	 * be added, checks for that MUST be added to the shallow copy code
	 * below.
	 *
	 */
	void (*free_res)(nfs_resop4 *);
	/** Default response size */
	uint32_t resp_size;
	/** Export permissions required flags */
	int exp_perm_flags;
};

/**
 * @brief  NFSv4 and 4.1 ops table.
 * indexed by opcode
 */

static const struct nfs4_op_desc optabv4[] = {
	[0] = { /* all out of bounds illegals go here to die */
		.name = "OP_ILLEGAL",
		.funct = nfs4_op_illegal,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_illegal_Free,
		.resp_size = sizeof(ILLEGAL4res),
		.exp_perm_flags = 0},
	[1] = {
		.name = "OP_ILLEGAL",
		.funct = nfs4_op_illegal,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_illegal_Free,
		.resp_size = sizeof(ILLEGAL4res),
		.exp_perm_flags = 0},
	[2] = {
		.name = "OP_ILLEGAL",
		.funct = nfs4_op_illegal,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_illegal_Free,
		.resp_size = sizeof(ILLEGAL4res),
		.exp_perm_flags = 0},
	[NFS4_OP_ACCESS] = {
		.name = "OP_ACCESS",
		.funct = nfs4_op_access,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_access_Free,
		.resp_size = sizeof(ACCESS4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_CLOSE] = {
		.name = "OP_CLOSE",
		.funct = nfs4_op_close,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_close_Free,
		.resp_size = sizeof(CLOSE4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_COMMIT] = {
		.name = "OP_COMMIT",
		.funct = nfs4_op_commit,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_commit_Free,
		.resp_size = sizeof(COMMIT4res),
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_CREATE] = {
		.name = "OP_CREATE",
		.funct = nfs4_op_create,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_create_Free,
		.resp_size = sizeof(CREATE4res),
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_DELEGPURGE] = {
		.name = "OP_DELEGPURGE",
		.funct = nfs4_op_delegpurge,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_delegpurge_Free,
		.resp_size = sizeof(DELEGPURGE4res),
		.exp_perm_flags = 0},
	[NFS4_OP_DELEGRETURN] = {
		.name = "OP_DELEGRETURN",
		.funct = nfs4_op_delegreturn,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_delegreturn_Free,
		.resp_size = sizeof(DELEGRETURN4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_GETATTR] = {
		.name = "OP_GETATTR",
		.funct = nfs4_op_getattr,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_getattr_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_GETFH] = {
		.name = "OP_GETFH",
		.funct = nfs4_op_getfh,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_getfh_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = 0},
	[NFS4_OP_LINK] = {
		.name = "OP_LINK",
		.funct = nfs4_op_link,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_link_Free,
		.resp_size = sizeof(LINK4res),
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_LOCK] = {
		.name = "OP_LOCK",
		.funct = nfs4_op_lock,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_lock_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LOCKT] = {
		.name = "OP_LOCKT",
		.funct = nfs4_op_lockt,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_lockt_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LOCKU] = {
		.name = "OP_LOCKU",
		.funct = nfs4_op_locku,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_locku_Free,
		.resp_size = sizeof(LOCKU4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LOOKUP] = {
		.name = "OP_LOOKUP",
		.funct = nfs4_op_lookup,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_lookup_Free,
		.resp_size = sizeof(LOOKUP4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LOOKUPP] = {
		.name = "OP_LOOKUPP",
		.funct = nfs4_op_lookupp,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_lookupp_Free,
		.resp_size = sizeof(LOOKUPP4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_NVERIFY] = {
		.name = "OP_NVERIFY",
		.funct = nfs4_op_nverify,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_nverify_Free,
		.resp_size = sizeof(NVERIFY4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_OPEN] = {
		.name = "OP_OPEN",
		.funct = nfs4_op_open,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_open_Free,
		.resp_size = sizeof(OPEN4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_OPENATTR] = {
		.name = "OP_OPENATTR",
		.funct = nfs4_op_openattr,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_openattr_Free,
		.resp_size = sizeof(OPENATTR4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_OPEN_CONFIRM] = {
		.name = "OP_OPEN_CONFIRM",
		.funct = nfs4_op_open_confirm,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_open_confirm_Free,
		.resp_size = sizeof(OPEN_CONFIRM4res),
		.exp_perm_flags = 0},
	[NFS4_OP_OPEN_DOWNGRADE] = {
		.name = "OP_OPEN_DOWNGRADE",
		.funct = nfs4_op_open_downgrade,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_open_downgrade_Free,
		.resp_size = sizeof(OPEN_DOWNGRADE4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_PUTFH] = {
		.name = "OP_PUTFH",
		.funct = nfs4_op_putfh,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_putfh_Free,
		.resp_size = sizeof(PUTFH4res),
		.exp_perm_flags = 0},
	[NFS4_OP_PUTPUBFH] = {
		.name = "OP_PUTPUBFH",
		.funct = nfs4_op_putpubfh,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_putpubfh_Free,
		.resp_size = sizeof(PUTPUBFH4res),
		.exp_perm_flags = 0},
	[NFS4_OP_PUTROOTFH] = {
		.name = "OP_PUTROOTFH",
		.funct = nfs4_op_putrootfh,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_putrootfh_Free,
		.resp_size = sizeof(PUTROOTFH4res),
		.exp_perm_flags = 0},
	[NFS4_OP_READ] = {
		.name = "OP_READ",
		.funct = nfs4_op_read,
		.resume = nfs4_op_read_resume,
		.free_res = nfs4_op_read_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_READ_ACCESS},
	[NFS4_OP_READDIR] = {
		.name = "OP_READDIR",
		.funct = nfs4_op_readdir,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_readdir_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_READLINK] = {
		.name = "OP_READLINK",
		.funct = nfs4_op_readlink,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_readlink_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_REMOVE] = {
		.name = "OP_REMOVE",
		.funct = nfs4_op_remove,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_remove_Free,
		.resp_size = sizeof(REMOVE4res),
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_RENAME] = {
		.name = "OP_RENAME",
		.funct = nfs4_op_rename,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_rename_Free,
		.resp_size = sizeof(RENAME4res),
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_RENEW] = {
		.name = "OP_RENEW",
		.funct = nfs4_op_renew,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_renew_Free,
		.resp_size = sizeof(RENEW4res),
		.exp_perm_flags = 0},
	[NFS4_OP_RESTOREFH] = {
		.name = "OP_RESTOREFH",
		.funct = nfs4_op_restorefh,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_restorefh_Free,
		.resp_size = sizeof(RESTOREFH4res),
		.exp_perm_flags = 0},
	[NFS4_OP_SAVEFH] = {
		.name = "OP_SAVEFH",
		.funct = nfs4_op_savefh,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_savefh_Free,
		.resp_size = sizeof(SAVEFH4res),
		.exp_perm_flags = 0},
	[NFS4_OP_SECINFO] = {
		.name = "OP_SECINFO",
		.funct = nfs4_op_secinfo,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_secinfo_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_SETATTR] = {
		.name = "OP_SETATTR",
		.funct = nfs4_op_setattr,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_setattr_Free,
		.resp_size = sizeof(SETATTR4res),
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_SETCLIENTID] = {
		.name = "OP_SETCLIENTID",
		.funct = nfs4_op_setclientid,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_setclientid_Free,
		.resp_size = sizeof(SETCLIENTID4res),
		.exp_perm_flags = 0},
	[NFS4_OP_SETCLIENTID_CONFIRM] = {
		.name = "OP_SETCLIENTID_CONFIRM",
		.funct = nfs4_op_setclientid_confirm,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_setclientid_confirm_Free,
		.resp_size = sizeof(SETCLIENTID_CONFIRM4res),
		.exp_perm_flags = 0},
	[NFS4_OP_VERIFY] = {
		.name = "OP_VERIFY",
		.funct = nfs4_op_verify,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_verify_Free,
		.resp_size = sizeof(VERIFY4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_WRITE] = {
		.name = "OP_WRITE",
		.funct = nfs4_op_write,
		.resume = nfs4_op_write_resume,
		.free_res = nfs4_op_write_Free,
		.resp_size = sizeof(WRITE4res),
		.exp_perm_flags = EXPORT_OPTION_WRITE_ACCESS},
	[NFS4_OP_RELEASE_LOCKOWNER] = {
		.name = "OP_RELEASE_LOCKOWNER",
		.funct = nfs4_op_release_lockowner,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_release_lockowner_Free,
		.resp_size = sizeof(RELEASE_LOCKOWNER4res),
		.exp_perm_flags = 0},
	[NFS4_OP_BACKCHANNEL_CTL] = {
		.name = "OP_BACKCHANNEL_CTL",
		.funct = nfs4_op_illegal,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_illegal_Free,
		.resp_size = sizeof(BACKCHANNEL_CTL4res),
		.exp_perm_flags = 0	/* tbd */},
	[NFS4_OP_BIND_CONN_TO_SESSION] = {
		.name = "OP_BIND_CONN_TO_SESSION",
		.funct = nfs4_op_bind_conn,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_nfs4_op_bind_conn_Free,
		.resp_size = sizeof(BIND_CONN_TO_SESSION4res),
		.exp_perm_flags = 0	/* tbd */},
	[NFS4_OP_EXCHANGE_ID] = {
		.name = "OP_EXCHANGE_ID",
		.funct = nfs4_op_exchange_id,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_exchange_id_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = 0},
	[NFS4_OP_CREATE_SESSION] = {
		.name = "OP_CREATE_SESSION",
		.funct = nfs4_op_create_session,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_create_session_Free,
		.resp_size = sizeof(CREATE_SESSION4res),
		.exp_perm_flags = 0},
	[NFS4_OP_DESTROY_SESSION] = {
		.name = "OP_DESTROY_SESSION",
		.funct = nfs4_op_destroy_session,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_reclaim_complete_Free,
		.resp_size = sizeof(DESTROY_SESSION4res),
		.exp_perm_flags = 0},
	[NFS4_OP_FREE_STATEID] = {
		.name = "OP_FREE_STATEID",
		.funct = nfs4_op_free_stateid,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_free_stateid_Free,
		.resp_size = sizeof(FREE_STATEID4res),
		.exp_perm_flags = 0},
	[NFS4_OP_GET_DIR_DELEGATION] = {
		.name = "OP_GET_DIR_DELEGATION",
		.funct = nfs4_op_illegal,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_illegal_Free,
		.resp_size = sizeof(GET_DIR_DELEGATION4res),
		.exp_perm_flags = 0	/* tbd */},
	[NFS4_OP_GETDEVICEINFO] = {
		.name = "OP_GETDEVICEINFO",
		.funct = nfs4_op_getdeviceinfo,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_getdeviceinfo_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = 0},
	[NFS4_OP_GETDEVICELIST] = {
		.name = "OP_GETDEVICELIST",
		.funct = nfs4_op_getdevicelist,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_getdevicelist_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LAYOUTCOMMIT] = {
		.name = "OP_LAYOUTCOMMIT",
		.funct = nfs4_op_layoutcommit,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_reclaim_complete_Free,
		.resp_size = sizeof(LAYOUTCOMMIT4res),
		.exp_perm_flags = 0},
	[NFS4_OP_LAYOUTGET] = {
		.name = "OP_LAYOUTGET",
		.funct = nfs4_op_layoutget,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_reclaim_complete_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LAYOUTRETURN] = {
		.name = "OP_LAYOUTRETURN",
		.funct = nfs4_op_layoutreturn,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_reclaim_complete_Free,
		.resp_size = sizeof(LAYOUTRETURN4res),
		.exp_perm_flags = 0},
	[NFS4_OP_SECINFO_NO_NAME] = {
		.name = "OP_SECINFO_NO_NAME",
		.funct = nfs4_op_secinfo_no_name,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_secinfo_no_name_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_SEQUENCE] = {
		.name = "OP_SEQUENCE",
		.funct = nfs4_op_sequence,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_sequence_Free,
		.resp_size = sizeof(SEQUENCE4res),
		.exp_perm_flags = 0},
	[NFS4_OP_SET_SSV] = {
		.name = "OP_SET_SSV",
		.funct = nfs4_op_set_ssv,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_set_ssv_Free,
		.resp_size = sizeof(SET_SSV4res),
		.exp_perm_flags = 0},
	[NFS4_OP_TEST_STATEID] = {
		.name = "OP_TEST_STATEID",
		.funct = nfs4_op_test_stateid,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_test_stateid_Free,
		.resp_size = sizeof(TEST_STATEID4res),
		.exp_perm_flags = 0},
	[NFS4_OP_WANT_DELEGATION] = {
		.name = "OP_WANT_DELEGATION",
		.funct = nfs4_op_illegal,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_illegal_Free,
		.resp_size = sizeof(WANT_DELEGATION4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS	/* tbd */},
	[NFS4_OP_DESTROY_CLIENTID] = {
		.name = "OP_DESTROY_CLIENTID",
		.funct = nfs4_op_destroy_clientid,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_destroy_clientid_Free,
		.resp_size = sizeof(DESTROY_CLIENTID4res),
		.exp_perm_flags = 0	/* tbd */},
	[NFS4_OP_RECLAIM_COMPLETE] = {
		.name = "OP_RECLAIM_COMPLETE",
		.funct = nfs4_op_reclaim_complete,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_reclaim_complete_Free,
		.resp_size = sizeof(RECLAIM_COMPLETE4res),
		.exp_perm_flags = 0},

	/* NFSv4.2 */
	[NFS4_OP_ALLOCATE] = {
		.name = "OP_ALLOCATE",
		.funct = nfs4_op_allocate,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_write_Free,
		.resp_size = sizeof(ALLOCATE4res),
		.exp_perm_flags = 0},
	[NFS4_OP_COPY] = {
		.name = "OP_COPY",
		.funct = nfs4_op_notsupp,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_notsupp_Free,
		.resp_size = sizeof(COPY4res),
		.exp_perm_flags = 0},
	[NFS4_OP_COPY_NOTIFY] = {
		.name = "OP_COPY_NOTIFY",
		.funct = nfs4_op_notsupp,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_notsupp_Free,
		.resp_size = sizeof(COPY_NOTIFY4res),
		.exp_perm_flags = 0},
	[NFS4_OP_DEALLOCATE] = {
		.name = "OP_DEALLOCATE",
		.funct = nfs4_op_deallocate,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_write_Free,
		.resp_size = sizeof(DEALLOCATE4res),
		.exp_perm_flags = 0},
	[NFS4_OP_IO_ADVISE] = {
		.name = "OP_IO_ADVISE",
		.funct = nfs4_op_io_advise,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_io_advise_Free,
		.resp_size = sizeof(IO_ADVISE4res),
		.exp_perm_flags = 0},
	[NFS4_OP_LAYOUTERROR] = {
		.name = "OP_LAYOUTERROR",
		.funct = nfs4_op_layouterror,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_layouterror_Free,
		.resp_size = sizeof(LAYOUTERROR4res),
		.exp_perm_flags = 0},
	[NFS4_OP_LAYOUTSTATS] = {
		.name = "OP_LAYOUTSTATS",
		.funct = nfs4_op_layoutstats,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_layoutstats_Free,
		.resp_size = sizeof(LAYOUTSTATS4res),
		.exp_perm_flags = 0},
	[NFS4_OP_OFFLOAD_CANCEL] = {
		.name = "OP_OFFLOAD_CANCEL",
		.funct = nfs4_op_notsupp,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_notsupp_Free,
		.resp_size = sizeof(OFFLOAD_ABORT4res),
		.exp_perm_flags = 0},
	[NFS4_OP_OFFLOAD_STATUS] = {
		.name = "OP_OFFLOAD_STATUS",
		.funct = nfs4_op_notsupp,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_notsupp_Free,
		.resp_size = sizeof(OFFLOAD_STATUS4res),
		.exp_perm_flags = 0},
	[NFS4_OP_READ_PLUS] = {
		.name = "OP_READ_PLUS",
		.funct = nfs4_op_read_plus,
		.resume = nfs4_op_read_plus_resume,
		.free_res = nfs4_op_read_plus_Free,
		.resp_size = sizeof(READ_PLUS4res),
		.exp_perm_flags = 0},
	[NFS4_OP_SEEK] = {
		.name = "OP_SEEK",
		.funct = nfs4_op_seek,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_write_Free,
		.resp_size = sizeof(SEEK4res),
		.exp_perm_flags = 0},
	[NFS4_OP_WRITE_SAME] = {
		.name = "OP_WRITE_SAME",
		.funct = nfs4_op_write_same,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_write_same_Free,
		.resp_size = sizeof(WRITE_SAME4res),
		.exp_perm_flags = 0},
	[NFS4_OP_CLONE] = {
		.name = "OP_CLONE",
		.funct = nfs4_op_notsupp,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_notsupp_Free,
		.resp_size = sizeof(ILLEGAL4res),
		.exp_perm_flags = 0},

	/* NFSv4.3 */
	[NFS4_OP_GETXATTR] = {
		.name = "OP_GETXATTR",
		.funct = nfs4_op_getxattr,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_getxattr_Free,
		.resp_size = sizeof(GETXATTR4res),
		.exp_perm_flags = 0},
	[NFS4_OP_SETXATTR] = {
		.name = "OP_SETXATTR",
		.funct = nfs4_op_setxattr,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_setxattr_Free,
		.resp_size = sizeof(SETXATTR4res),
		.exp_perm_flags = 0},
	[NFS4_OP_LISTXATTR] = {
		.name = "OP_LISTXATTR",
		.funct = nfs4_op_listxattr,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_listxattr_Free,
		.resp_size = sizeof(LISTXATTR4res),
		.exp_perm_flags = 0},
	[NFS4_OP_REMOVEXATTR] = {
		.name = "OP_REMOVEXATTR",
		.funct = nfs4_op_removexattr,
		.resume = nfs4_default_resume,
		.free_res = nfs4_op_removexattr_Free,
		.resp_size = sizeof(REMOVEXATTR4res),
		.exp_perm_flags = 0},
};

/** Define the last valid NFS v4 op for each minor version.
 *
 */
nfs_opnum4 LastOpcode[] = {
	NFS4_OP_RELEASE_LOCKOWNER,
	NFS4_OP_RECLAIM_COMPLETE,
	NFS4_OP_REMOVEXATTR
};

void copy_tag(utf8str_cs *dest, utf8str_cs *src)
{
	/* Keeping the same tag as in the arguments */
	dest->utf8string_len = src->utf8string_len;

	if (dest->utf8string_len > 0) {

		dest->utf8string_val = gsh_malloc(dest->utf8string_len + 1);

		memcpy(dest->utf8string_val,
		       src->utf8string_val,
		       dest->utf8string_len);

		dest->utf8string_val[dest->utf8string_len] = '\0';
	} else {
		dest->utf8string_val = NULL;
	}
}

enum nfs_req_result complete_op(compound_data_t *data, nfsstat4 *status,
				enum nfs_req_result result)
{
	nfs_resop4 *thisres = &data->resarray[data->oppos];
	COMPOUND4res *res_compound4;

	res_compound4 = &data->res->res_compound4_extended->res_compound4;

	if (result == NFS_REQ_REPLAY) {
		/* Replay cache, only true for SEQUENCE. Since will only be set
		 * in those cases, no need to check operation or anything. This
		 * result will be converted to NFS_REQ_OK before we actually
		 * return from the compound.
		 */

		/* Free the reply allocated originally */
		release_nfs4_res_compound(data->res->res_compound4_extended);

		/* Copy the reply from the cache (the reference is already
		 * taken by SEQUENCE.
		 */
		data->res->res_compound4_extended = data->slot->cached_result;

		*status = ((COMPOUND4res *) data->slot->cached_result)->status;

		LogFullDebug(COMPONENT_SESSIONS,
			     "Use session replay cache %p result %s",
			     data->slot->cached_result,
			     nfsstat4_to_str(*status));

		/* Will exit the for loop since result is not NFS_REQ_OK */
		goto out;
	}

	/* All the operations, like NFS4_OP_ACCESS, have a first replied
	 * field called .status
	 */
	*status = thisres->nfs_resop4_u.opaccess.status;

#ifdef USE_LTTNG
	tracepoint(nfs_rpc, v4op_end, data->oppos, data->opcode,
		   data->opname, nfsstat4_to_str(*status));
#endif

	LogCompoundFH(data);

	/* Tally the response size */
	if (*status != NFS4_OK &&
	    (optabv4[data->opcode].resp_size != VARIABLE_RESP_SIZE ||
	     data->op_resp_size == VARIABLE_RESP_SIZE)) {
		/* If the op failed and has a static response size, or
		 * it has a variable size that hasn't been set, use the
		 * sizeof nfsstat4 instead.
		 */
		data->op_resp_size = sizeof(nfsstat4);
	}

	data->resp_size += sizeof(nfs_opnum4) + data->op_resp_size;

	LogDebug(COMPONENT_NFS_V4,
		 "Status of %s in position %d = %s, op response size is %"
		 PRIu32" total response size is %"PRIu32,
		 data->opname, data->oppos, nfsstat4_to_str(*status),
		 data->op_resp_size, data->resp_size);

	if (result == NFS_REQ_ERROR) {
		/* An error occurred, we do not manage the other requests
		 * in the COMPOUND, this may be a regular behavior
		 */
		res_compound4->resarray.resarray_len = data->oppos + 1;
	} else {
		/* Continue for loop (result will be NFS_REQ_OK since
		 * NFS_REQ_ERROR, NFS_REQ_REPLAY, and NFS_REQ_ASYNC_WAIT have
		 * already been handled (we don't even get into this function
		 * with NFS_REQ_ASYNC_WAIT) and NFS_REQ_DROP is not returned by
		 * any nfs4_op.
		 */
	}

out:

	server_stats_nfsv4_op_done(data->opcode, data->op_start_time, *status);

	return result;
}

enum nfs_req_result process_one_op(compound_data_t *data, nfsstat4 *status)
{
	const char *bad_op_state_reason = "";
	struct timespec ts;
	int perm_flags;
	log_components_t alt_component = COMPONENT_NFS_V4;
	nfs_argop4 *thisarg = &data->argarray[data->oppos];
	nfs_resop4 *thisres = &data->resarray[data->oppos];
	enum nfs_req_result result;
	COMPOUND4res *res_compound4;

	res_compound4 = &data->res->res_compound4_extended->res_compound4;

	/* Used to check if OP_SEQUENCE is the first operation */
	data->op_resp_size = sizeof(nfsstat4);
	data->opcode = thisarg->argop;

	/* Handle opcode overflow */
	if (data->opcode > LastOpcode[data->minorversion])
		data->opcode = 0;

	data->opname = optabv4[data->opcode].name;

	LogDebug(COMPONENT_NFS_V4, "Request %d: opcode %d is %s",
		 data->oppos, data->opcode, data->opname);

	/* Verify BIND_CONN_TO_SESSION is not used in a compound
	 * with length > 1. This check is NOT redundant with the
	 * checks in nfs4_Compound().
	 */
	if (data->oppos > 0 && data->opcode == NFS4_OP_BIND_CONN_TO_SESSION) {
		*status = NFS4ERR_NOT_ONLY_OP;
		bad_op_state_reason =
				"BIND_CONN_TO_SESSION past position 1";
		goto bad_op_state;
	}

	/* OP_SEQUENCE is always the first operation of the request */
	if (data->oppos > 0 && data->opcode == NFS4_OP_SEQUENCE) {
		*status = NFS4ERR_SEQUENCE_POS;
		bad_op_state_reason =
				"SEQUENCE past position 1";
		goto bad_op_state;
	}

	/* If a DESTROY_SESSION not the only operation, and it matches
	 * the session specified in the SEQUENCE op (since the compound
	 * has more than one op, we already know it MUST start with
	 * SEQUENCE), then it MUST be the final op in the compound.
	 */
	if (data->oppos > 0 && data->opcode == NFS4_OP_DESTROY_SESSION) {
		bool session_compare;
		bool bad_pos;

		session_compare = memcmp(
		    data->argarray[0].nfs_argop4_u.opsequence.sa_sessionid,
		    thisarg->nfs_argop4_u.opdestroy_session.dsa_sessionid,
		    NFS4_SESSIONID_SIZE) == 0;

		bad_pos = session_compare &&
				data->oppos != (data->argarray_len - 1);

		LogAtLevel(COMPONENT_SESSIONS,
			   bad_pos ? NIV_INFO : NIV_DEBUG,
			   "DESTROY_SESSION in position %u out of 0-%"
			   PRIi32 " %s is %s",
			   data->oppos, data->argarray_len - 1,
			   session_compare
				? "same session as SEQUENCE"
				: "different session from SEQUENCE",
			   bad_pos ? "not last op in compound" : "opk");

		if (bad_pos) {
			*status = NFS4ERR_NOT_ONLY_OP;
			bad_op_state_reason =
			    "DESTROY_SESSION not last op in compound";
			goto bad_op_state;
		}
	}

	/* time each op */
	now(&ts);
	data->op_start_time = timespec_diff(&nfs_ServerBootTime, &ts);

	if (data->minorversion > 0 && data->session != NULL &&
	    data->session->fore_channel_attrs.ca_maxoperations ==
							data->oppos) {
		*status = NFS4ERR_TOO_MANY_OPS;
		bad_op_state_reason = "Too many operations";
		goto bad_op_state;
	}

	perm_flags = optabv4[data->opcode].exp_perm_flags &
		     EXPORT_OPTION_ACCESS_MASK;

	if (perm_flags != 0) {
		*status = nfs4_Is_Fh_Empty(&data->currentFH);
		if (*status != NFS4_OK) {
			bad_op_state_reason = "Empty or NULL handle";
			goto bad_op_state;
		}

		/* Operation uses a CurrentFH, so we can check export
		 * perms. Perms should even be set reasonably for pseudo
		 * file system.
		 */
		LogMidDebugAlt(COMPONENT_NFS_V4, COMPONENT_EXPORT,
			       "Check export perms export = %08x req = %08x",
			       op_ctx->export_perms->options &
					EXPORT_OPTION_ACCESS_MASK,
			       perm_flags);
		if ((op_ctx->export_perms->options &
		     perm_flags) != perm_flags) {
			/* Export doesn't allow requested
			 * access for this client.
			 */
			if ((perm_flags & EXPORT_OPTION_MODIFY_ACCESS)
			    != 0)
				*status = NFS4ERR_ROFS;
			else
				*status = NFS4ERR_ACCESS;

			bad_op_state_reason = "Export permission failure";
			alt_component = COMPONENT_EXPORT;
			goto bad_op_state;
		}
	}

	/* Set up the minimum/default response size and check if there
	 * is room for it.
	*/
	data->op_resp_size = optabv4[data->opcode].resp_size;

	*status = check_resp_room(data, data->op_resp_size);

	if (*status != NFS4_OK) {
		bad_op_state_reason = "op response size";

 bad_op_state:
		/* Tally the response size */
		data->resp_size += sizeof(nfs_opnum4) + sizeof(nfsstat4);

		LogDebugAlt(COMPONENT_NFS_V4, alt_component,
			    "Status of %s in position %d due to %s is %s, op response size = %"
			    PRIu32" total response size = %"PRIu32,
			    data->opname, data->oppos,
			    bad_op_state_reason,
			    nfsstat4_to_str(*status),
			    data->op_resp_size, data->resp_size);

		/* All the operation, like NFS4_OP_ACCESS, have
		 * a first replied field called .status
		 */
		thisres->nfs_resop4_u.opaccess.status = *status;
		thisres->resop = data->opcode;

		/* Do not manage the other requests in the COMPOUND. */
		res_compound4->resarray.resarray_len = data->oppos + 1;
		return NFS_REQ_ERROR;
	}

	/***************************************************************
	 * Make the actual op call                                     *
	 **************************************************************/
#ifdef USE_LTTNG
	tracepoint(nfs_rpc, v4op_start, data->oppos,
		   data->opcode, data->opname);
#endif

	result = (optabv4[data->opcode].funct) (thisarg, data, thisres);

	if (result != NFS_REQ_ASYNC_WAIT) {
		/* Complete the operation, otherwise return without doing
		 * anything else.
		 */
		result = complete_op(data, status, result);
	}

	return result;
}

void complete_nfs4_compound(compound_data_t *data, int status,
			    enum nfs_req_result result)
{
	COMPOUND4res *res_compound4;

	res_compound4 = &data->res->res_compound4_extended->res_compound4;

	server_stats_compound_done(data->argarray_len, status);

	/* Complete the reply, in particular, tell where you stopped if
	 * unsuccessful COMPOUD
	 */
	res_compound4->status = status;

	/* Manage session's DRC: keep NFS4.1 replay for later use, but don't
	 * save a replayed result again.
	 */
	if (data->sa_cachethis) {
		/* Pointer has been set by nfs4_op_sequence and points to slot
		 * to cache result in.
		 */
		LogFullDebug(COMPONENT_SESSIONS,
			     "Save result in session replay cache %p sizeof nfs_res_t=%d",
			     data->slot->cached_result, (int)sizeof(nfs_res_t));

		/* Save the result pointer in the slot cache (the correct slot
		 * is pointed to by data->cached_result).
		 */
		data->slot->cached_result = data->res->res_compound4_extended;

		/* Take a reference to indicate that this reply is cached. */
		atomic_inc_int32_t(&data->slot->cached_result->res_refcnt);
	} else if (data->minorversion > 0 &&
		   result != NFS_REQ_REPLAY &&
		   data->argarray[0].argop == NFS4_OP_SEQUENCE &&
		   data->slot != NULL) {
		/* We need to cache an "uncached" response. The length is
		 * 1 if only one op processed, otherwise 2. */
		struct COMPOUND4res *c_res;
		u_int resarray_len =
		    res_compound4->resarray.resarray_len == 1 ? 1 : 2;
		struct nfs_resop4 *res0;

		/* If the slot happened to be in use, release it. */
		release_slot(data->slot);

		/* Allocate (and zero) a new COMPOUND4res_extended */
		data->slot->cached_result =
			gsh_calloc(1, sizeof(*data->slot->cached_result));

		/* Take initial reference to response. */
		data->slot->cached_result->res_refcnt = 1;

		c_res = &data->slot->cached_result->res_compound4;

		c_res->resarray.resarray_len = resarray_len;
		c_res->resarray.resarray_val =
			gsh_calloc(resarray_len, sizeof(struct nfs_resop4));
		copy_tag(&c_res->tag, &res_compound4->tag);
		res0 = c_res->resarray.resarray_val;

		/* Copy the sequence result. */
		*res0 = res_compound4->resarray.resarray_val[0];
		c_res->status = res0->nfs_resop4_u.opillegal.status;

		if (resarray_len == 2) {
			struct nfs_resop4 *res1 = res0 + 1;

			/* Shallow copy response since we will override any
			 * resok or any negative response that might have
			 * allocated data.
			 */
			*res1 = res_compound4->resarray.resarray_val[1];

			/* Override NFS4_OK and NFS4ERR_DENIED. We MUST override
			 * NFS4_OK since we aren't caching a full response and
			 * we MUST override NFS4ERR_DENIED because LOCK and
			 * LOCKT allocate data that we did not deep copy.
			 *
			 * If any new operations are added with dynamically
			 * allocated data associated with a non-NFS4_OK
			 * status are added in some future minor version, they
			 * will likely need special handling here also.
			 *
			 * Note that we COULD get fancy and if we had a 2 op
			 * compound that had an NFS4_OK status and no dynamic
			 * data was allocated then go ahead and cache the
			 * full response since it wouldn't take any more
			 * memory. However, that would add a lot more special
			 * handling here.
			 */
			if (res1->nfs_resop4_u.opillegal.status == NFS4_OK ||
			    res1->nfs_resop4_u.opillegal.status ==
							NFS4ERR_DENIED) {
				res1->nfs_resop4_u.opillegal.status =
						NFS4ERR_RETRY_UNCACHED_REP;
			}

			c_res->status = res1->nfs_resop4_u.opillegal.status;
		}

		/* NOTE: We just built a 2nd "uncached" response and put that
		 * in the slot cache with 1 reference. The actual response is
		 * whatever it is, but is different and has it's OWN 1 refcount.
		 * It can't have more than 1 reference since this is NOT a
		 * replay.
		 */
	}

	/* If we have reserved a lease, update it and release it */
	if (data->preserved_clientid != NULL) {
		/* Update and release lease */
		PTHREAD_MUTEX_lock(&data->preserved_clientid->cid_mutex);

		update_lease(data->preserved_clientid);

		PTHREAD_MUTEX_unlock(&data->preserved_clientid->cid_mutex);
	}

	if (status != NFS4_OK)
		LogDebug(COMPONENT_NFS_V4, "End status = %s lastindex = %d",
			 nfsstat4_to_str(status), data->oppos);

	/* release current active export in op_ctx. */
	if (op_ctx->ctx_export) {
		put_gsh_export(op_ctx->ctx_export);
		op_ctx->ctx_export = NULL;
		op_ctx->fsal_export = NULL;
	}
}

static enum xprt_stat nfs4_compound_resume(struct svc_req *req)
{
	SVCXPRT *xprt = req->rq_xprt;
	nfs_request_t *reqdata = xprt->xp_u1;
	nfsstat4 status = NFS4_OK;
	compound_data_t *data = reqdata->proc_data;
	enum nfs_req_result result;

	op_ctx = &reqdata->req_ctx;

	/* Start by resuming the operation that suspended. */
	result = (optabv4[data->opcode].resume)
	    (&data->argarray[data->oppos], data, &data->resarray[data->oppos]);

	if (result != NFS_REQ_ASYNC_WAIT) {
		/* Complete the operation (will fill in status). */
		result = complete_op(data, &status, result);
	} else {
		/* The request is suspended, don't touch the request in
		 * any way because the resume may already be scheduled
		 * and running on nother thread. The xp_resume_cb has
		 * already been set up before we started proecessing
		 * ops on this request at all.
		 */
		return XPRT_SUSPEND;
	}

	/* Skip the resumed op and continue through the rest of the compound. */
	for (data->oppos += 1;
	     result == NFS_REQ_OK && data->oppos < data->argarray_len;
	     data->oppos++) {
		result = process_one_op(data, &status);

		if (result == NFS_REQ_ASYNC_WAIT) {
			/* The request is suspended, don't touch the request in
			 * any way because the resume may already be scheduled
			 * and running on nother thread. The xp_resume_cb has
			 * already been set up before we started proecessing
			 * ops on this request at all.
			 */
			return XPRT_SUSPEND;
		}
	}

	complete_nfs4_compound(data, status, result);

	compound_data_Free(data);

	nfs_rpc_complete_async_request(reqdata, NFS_REQ_OK);

	return XPRT_IDLE;
}

/**
 * @brief The NFS PROC4 COMPOUND
 *
 * Implements the NFS PROC4 COMPOUND.  This routine processes the
 * content of the nfsv4 operation list and composes the result.  On
 * this aspect it is a little similar to a dispatch routine.
 * Operation and functions necessary to process them are defined in
 * the optabv4 array.
 *
 *
 *  @param[in]  arg        Generic nfs arguments
 *  @param[in]  req        NFSv4 request structure
 *  @param[out] res        NFSv4 reply structure
 *
 *  @see nfs4_op_<*> functions
 *  @see nfs4_GetPseudoFs
 *
 * @retval NFS_REQ_OKAY if a result is sent.
 * @retval NFS_REQ_DROP if we pretend we never saw the request.
 */

int nfs4_Compound(nfs_arg_t *arg, struct svc_req *req, nfs_res_t *res)
{
	nfsstat4 status = NFS4_OK;
	compound_data_t *data = NULL;
	const uint32_t compound4_minor = arg->arg_compound4.minorversion;
	const uint32_t argarray_len = arg->arg_compound4.argarray.argarray_len;
	/* Array of op arguments */
	nfs_argop4 * const argarray = arg->arg_compound4.argarray.argarray_val;
	bool drop = false;
	nfs_request_t *reqdata = container_of(req, nfs_request_t, svc);
	SVCXPRT *xprt = req->rq_xprt;
	struct COMPOUND4res *res_compound4;
	enum nfs_req_result result = NFS_REQ_OK;

	/* Allocate (and zero) the COMPOUND4res_extended */
	res->res_compound4_extended =
			gsh_calloc(1, sizeof(*res->res_compound4_extended));
	res_compound4 = &res->res_compound4_extended->res_compound4;

	/* Take initial reference to response. */
	res->res_compound4_extended->res_refcnt = 1;

	if (compound4_minor > 2) {
		LogCrit(COMPONENT_NFS_V4, "Bad Minor Version %d",
			compound4_minor);

		res_compound4->status = NFS4ERR_MINOR_VERS_MISMATCH;
		res_compound4->resarray.resarray_len = 0;
		goto out;
	}

	if ((nfs_param.nfsv4_param.minor_versions &
			(1 << compound4_minor)) == 0) {
		LogInfo(COMPONENT_NFS_V4, "Unsupported minor version %d",
			compound4_minor);
		res_compound4->status = NFS4ERR_MINOR_VERS_MISMATCH;
		res_compound4->resarray.resarray_len = 0;
		goto out;
	}

	/* Initialisation of the compound request internal's data */
	data = gsh_calloc(1, sizeof(*data));

	data->req = req;
	data->argarray_len = argarray_len;
	data->argarray = arg->arg_compound4.argarray.argarray_val;
	data->res = res;
	reqdata->proc_data = data;

	/* Minor version related stuff */
	op_ctx->nfs_minorvers = compound4_minor;
	data->minorversion = compound4_minor;

	/* Keeping the same tag as in the arguments */
	copy_tag(&res_compound4->tag, &arg->arg_compound4.tag);

	if (res_compound4->tag.utf8string_len > 0) {
		/* Check if the tag is a valid utf8 string */
		if (nfs4_utf8string2dynamic(
				&res_compound4->tag,
				UTF8_SCAN_ALL, &data->tagname) != 0) {
			char str[LOG_BUFF_LEN];
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_opaque_bytes(
				&dspbuf,
				res_compound4->tag.utf8string_val,
				res_compound4->tag.utf8string_len);

			LogCrit(COMPONENT_NFS_V4,
				"COMPOUND: bad tag %p len %d bytes %s",
				res_compound4->tag.utf8string_val,
				res_compound4->tag.utf8string_len,
				str);

			res_compound4->status = NFS4ERR_INVAL;
			res_compound4->resarray.resarray_len = 0;
			goto out;
		}
	} else {
		/* No tag */
		data->tagname = gsh_strdup("NO TAG");
	}

	/* Managing the operation list */
	LogDebug(COMPONENT_NFS_V4,
		 "COMPOUND: There are %d operations, res = %p, tag = %s",
		 argarray_len, res, data->tagname);

	/* Check for empty COMPOUND request */
	if (argarray_len == 0) {
		LogMajor(COMPONENT_NFS_V4,
			 "An empty COMPOUND (no operation in it) was received");

		res_compound4->status = NFS4_OK;
		res_compound4->resarray.resarray_len = 0;
		goto out;
	}

	/* Check for too long request */
	if (argarray_len > 100) {
		LogMajor(COMPONENT_NFS_V4,
			 "A COMPOUND with too many operations (%d) was received",
			 argarray_len);

		res_compound4->status = NFS4ERR_RESOURCE;
		res_compound4->resarray.resarray_len = 0;
		goto out;
	}

	/* Initialize response size with size of compound response size. */
	data->resp_size = sizeof(COMPOUND4res) - sizeof(nfs_resop4 *);

	/* Building the client credential field */
	if (nfs_rpc_req2client_cred(req, &data->credential) == -1) {
		/* Malformed credential */
		drop = true;
		goto out;
	}

	/* Keeping the same tag as in the arguments */
	res_compound4->tag.utf8string_len =
	    arg->arg_compound4.tag.utf8string_len;

	/* Allocating the reply nfs_resop4 */
	data->resarray = gsh_calloc(argarray_len, sizeof(struct nfs_resop4));

	res_compound4->resarray.resarray_len = argarray_len;
	res_compound4->resarray.resarray_val = data->resarray;

	/* Manage errors NFS4ERR_OP_NOT_IN_SESSION and NFS4ERR_NOT_ONLY_OP.
	 * These checks apply only to 4.1 */
	if (compound4_minor > 0) {
		/* Check for valid operation to start an NFS v4.1 COMPOUND:
		 */
		if (argarray[0].argop != NFS4_OP_ILLEGAL
		    && argarray[0].argop != NFS4_OP_SEQUENCE
		    && argarray[0].argop != NFS4_OP_EXCHANGE_ID
		    && argarray[0].argop != NFS4_OP_CREATE_SESSION
		    && argarray[0].argop != NFS4_OP_DESTROY_SESSION
		    && argarray[0].argop != NFS4_OP_BIND_CONN_TO_SESSION
		    && argarray[0].argop != NFS4_OP_DESTROY_CLIENTID) {
			res_compound4->status = NFS4ERR_OP_NOT_IN_SESSION;
			res_compound4->resarray.resarray_len = 0;
			goto out;
		}

		if (argarray_len > 1) {
			/* If not prepended by OP4_SEQUENCE, OP4_EXCHANGE_ID
			 * should be the only request in the compound see
			 * 18.35.3. and test EID8 for details
			 *
			 * If not prepended bu OP4_SEQUENCE, OP4_CREATE_SESSION
			 * should be the only request in the compound see
			 * 18.36.3 and test CSESS23 for details
			 *
			 * If the COMPOUND request does not start with SEQUENCE,
			 * and if DESTROY_SESSION is not the sole operation,
			 * then server MUST return  NFS4ERR_NOT_ONLY_OP. See
			 * 18.37.3 nd test DSESS9005 for details
			 */
			if (argarray[0].argop == NFS4_OP_EXCHANGE_ID ||
			    argarray[0].argop == NFS4_OP_CREATE_SESSION ||
			    argarray[0].argop == NFS4_OP_DESTROY_CLIENTID ||
			    argarray[0].argop == NFS4_OP_DESTROY_SESSION ||
			    argarray[0].argop == NFS4_OP_BIND_CONN_TO_SESSION) {
				res_compound4->status = NFS4ERR_NOT_ONLY_OP;
				res_compound4->resarray.resarray_len = 0;
				goto out;
			}
		}
	}

	/* Before we start running, we must prepare to be suspended. We must do
	 * this now because after we have been suspended, it's too late, the
	 * request might have already been resumed on another worker thread.
	 */
	xprt->xp_resume_cb = nfs4_compound_resume;
	xprt->xp_u1 = reqdata;

	/**********************************************************************
	 * Now start processing the compound ops.
	 **********************************************************************/
	for (data->oppos = 0;
	     result == NFS_REQ_OK && data->oppos < data->argarray_len;
	     data->oppos++) {
		result = process_one_op(data, &status);

		if (result == NFS_REQ_ASYNC_WAIT) {
			/* The request is suspended, don't touch the request in
			 * any way because the resume may already be scheduled
			 * and running on nother thread. The xp_resume_cb has
			 * already been set up before we started proecessing
			 * ops on this request at all.
			 */
			return result;
		}
	}

	complete_nfs4_compound(data, status, result);

out:

	compound_data_Free(data);

	return drop ? NFS_REQ_DROP : NFS_REQ_OK;
}				/* nfs4_Compound */

/**
 *
 * @brief Free the result for one NFS4_OP
 *
 * This function frees any memory allocated for the result of an NFSv4
 * operation.
 *
 * @param[in,out] res The result to be freed
 *
 */
void nfs4_Compound_FreeOne(nfs_resop4 *res)
{
	int opcode;

	opcode = (res->resop != NFS4_OP_ILLEGAL)
	    ? res->resop : 0;	/* opcode 0 for illegals */
	optabv4[opcode].free_res(res);
}

void release_nfs4_res_compound(struct COMPOUND4res_extended *res_compound4_ex)
{
	unsigned int i = 0;
	int32_t refcnt = atomic_dec_int32_t(&res_compound4_ex->res_refcnt);
	struct COMPOUND4res *res_compound4 = &res_compound4_ex->res_compound4;

	if (refcnt > 0) {
		LogFullDebugAlt(COMPONENT_NFS_V4, COMPONENT_SESSIONS,
			     "Skipping free of NFS4 result %p refcnt %"PRIi32,
			     res_compound4_ex, refcnt);
		return;
	}

	LogFullDebugAlt(COMPONENT_NFS_V4, COMPONENT_SESSIONS,
		     "Compound Free %p (resarraylen=%i)",
		     res_compound4_ex, res_compound4->resarray.resarray_len);

	for (i = 0; i < res_compound4->resarray.resarray_len; i++) {
		nfs_resop4 *val = &res_compound4->resarray.resarray_val[i];

		if (val) {
			/* !val is an error case, but it can occur, so avoid
			 * indirect on NULL
			 */
			nfs4_Compound_FreeOne(val);
		}
	}

	gsh_free(res_compound4->resarray.resarray_val);
	res_compound4->resarray.resarray_val = NULL;

	gsh_free(res_compound4->tag.utf8string_val);
	res_compound4->tag.utf8string_val = NULL;

	gsh_free(res_compound4_ex);
}

/**
 *
 * @brief Free the result for NFS4PROC_COMPOUND
 *
 * This function frees the result for one NFS4PROC_COMPOUND.
 *
 * @param[in] res The result
 *
 */
void nfs4_Compound_Free(nfs_res_t *res)
{
	release_nfs4_res_compound(res->res_compound4_extended);
}

/**
 * @brief Free a compound data structure
 *
 * This function frees one compound data structure.
 *
 * @param[in,out] data The compound_data_t to be freed
 *
 */
void compound_data_Free(compound_data_t *data)
{
	if (data == NULL)
		return;

	/* Release refcounted cache entries */
	set_current_entry(data, NULL);
	set_saved_entry(data, NULL);

	gsh_free(data->tagname);

	if (data->session) {
		if (data->slotid != UINT32_MAX) {
			nfs41_session_slot_t *slot;

			/* Release the slot if in use */
			slot = &data->session->fc_slots[data->slotid];
			PTHREAD_MUTEX_unlock(&slot->lock);
		}

		dec_session_ref(data->session);
		data->session = NULL;
	}

	/* Release SavedFH reference to export. */
	if (data->saved_export) {
		put_gsh_export(data->saved_export);
		data->saved_export = NULL;
	}

	if (data->currentFH.nfs_fh4_val != NULL)
		gsh_free(data->currentFH.nfs_fh4_val);

	if (data->savedFH.nfs_fh4_val != NULL)
		gsh_free(data->savedFH.nfs_fh4_val);

	gsh_free(data);
}				/* compound_data_Free */

/**
 *
 * @brief Copy the result for one NFS4_OP
 *
 * This function copies the result structure for a single NFSv4
 * operation.
 *
 * @param[out] res_dst Buffer to which to copy the result
 * @param[in]  res_src The result to copy
 *
 */
void nfs4_Compound_CopyResOne(nfs_resop4 *res_dst, nfs_resop4 *res_src)
{
	/* Copy base data structure */
	memcpy(res_dst, res_src, sizeof(*res_dst));

	/* Do deep copy where necessary */
	switch (res_src->resop) {
	case NFS4_OP_ACCESS:
		break;

	case NFS4_OP_CLOSE:
		nfs4_op_close_CopyRes(&res_dst->nfs_resop4_u.opclose,
				      &res_src->nfs_resop4_u.opclose);
		return;

	case NFS4_OP_COMMIT:
	case NFS4_OP_CREATE:
	case NFS4_OP_DELEGPURGE:
	case NFS4_OP_DELEGRETURN:
	case NFS4_OP_GETATTR:
	case NFS4_OP_GETFH:
	case NFS4_OP_LINK:
		break;

	case NFS4_OP_LOCK:
		nfs4_op_lock_CopyRes(&res_dst->nfs_resop4_u.oplock,
				     &res_src->nfs_resop4_u.oplock);
		return;

	case NFS4_OP_LOCKT:
		break;

	case NFS4_OP_LOCKU:
		nfs4_op_locku_CopyRes(&res_dst->nfs_resop4_u.oplocku,
				      &res_src->nfs_resop4_u.oplocku);
		return;

	case NFS4_OP_LOOKUP:
	case NFS4_OP_LOOKUPP:
	case NFS4_OP_NVERIFY:
		break;

	case NFS4_OP_OPEN:
		nfs4_op_open_CopyRes(&res_dst->nfs_resop4_u.opopen,
				     &res_src->nfs_resop4_u.opopen);
		return;

	case NFS4_OP_OPENATTR:
		break;

	case NFS4_OP_OPEN_CONFIRM:
		nfs4_op_open_confirm_CopyRes(
			&res_dst->nfs_resop4_u.opopen_confirm,
			&res_src->nfs_resop4_u.opopen_confirm);
		return;

	case NFS4_OP_OPEN_DOWNGRADE:
		nfs4_op_open_downgrade_CopyRes(
			&res_dst->nfs_resop4_u.opopen_downgrade,
			&res_src->nfs_resop4_u.opopen_downgrade);
		return;

	case NFS4_OP_PUTFH:
	case NFS4_OP_PUTPUBFH:
	case NFS4_OP_PUTROOTFH:
	case NFS4_OP_READ:
	case NFS4_OP_READDIR:
	case NFS4_OP_READLINK:
	case NFS4_OP_REMOVE:
	case NFS4_OP_RENAME:
	case NFS4_OP_RENEW:
	case NFS4_OP_RESTOREFH:
	case NFS4_OP_SAVEFH:
	case NFS4_OP_SECINFO:
	case NFS4_OP_SETATTR:
	case NFS4_OP_SETCLIENTID:
	case NFS4_OP_SETCLIENTID_CONFIRM:
	case NFS4_OP_VERIFY:
	case NFS4_OP_WRITE:
	case NFS4_OP_RELEASE_LOCKOWNER:
		break;

	case NFS4_OP_EXCHANGE_ID:
	case NFS4_OP_CREATE_SESSION:
	case NFS4_OP_SEQUENCE:
	case NFS4_OP_GETDEVICEINFO:
	case NFS4_OP_GETDEVICELIST:
	case NFS4_OP_BACKCHANNEL_CTL:
	case NFS4_OP_BIND_CONN_TO_SESSION:
	case NFS4_OP_DESTROY_SESSION:
	case NFS4_OP_FREE_STATEID:
	case NFS4_OP_GET_DIR_DELEGATION:
	case NFS4_OP_LAYOUTCOMMIT:
	case NFS4_OP_LAYOUTGET:
	case NFS4_OP_LAYOUTRETURN:
	case NFS4_OP_SECINFO_NO_NAME:
	case NFS4_OP_SET_SSV:
	case NFS4_OP_TEST_STATEID:
	case NFS4_OP_WANT_DELEGATION:
	case NFS4_OP_DESTROY_CLIENTID:
	case NFS4_OP_RECLAIM_COMPLETE:

	/* NFSv4.2 */
	case NFS4_OP_ALLOCATE:
	case NFS4_OP_COPY:
	case NFS4_OP_COPY_NOTIFY:
	case NFS4_OP_DEALLOCATE:
	case NFS4_OP_IO_ADVISE:
	case NFS4_OP_LAYOUTERROR:
	case NFS4_OP_LAYOUTSTATS:
	case NFS4_OP_OFFLOAD_CANCEL:
	case NFS4_OP_OFFLOAD_STATUS:
	case NFS4_OP_READ_PLUS:
	case NFS4_OP_SEEK:
	case NFS4_OP_WRITE_SAME:
	case NFS4_OP_CLONE:

	/* NFSv4.3 */
	case NFS4_OP_GETXATTR:
	case NFS4_OP_SETXATTR:
	case NFS4_OP_LISTXATTR:
	case NFS4_OP_REMOVEXATTR:

	case NFS4_OP_LAST_ONE:
		break;

	case NFS4_OP_ILLEGAL:
		break;
	}			/* switch */

	LogFatal(COMPONENT_NFS_V4,
		 "Copy one result not implemented for %d",
		 res_src->resop);
}

/**
 * @brief Handle the xdr encode of the COMPOUND response
 *
 * @param(in) xdrs  The XDR object
 * @param(in) objp  The response pointer
 *
 */

bool xdr_COMPOUND4res_extended(XDR *xdrs, struct COMPOUND4res_extended **objp)
{
	/* Since the response in nfs_res_t is a pointer, we must dereference it
	 * to complete the encode.
	 */
	struct COMPOUND4res_extended *res_compound4_extended = *objp;

	/* And we must pass the actual COMPOUND4res */
	return xdr_COMPOUND4res(xdrs, &res_compound4_extended->res_compound4);
}

/* @} */

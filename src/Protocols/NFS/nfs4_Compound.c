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

/**
 * #brief Structure to map out how each compound op is managed.
 *
 */
struct nfs4_op_desc {
	/** Operation name */
	char *name;
	/** Function to process the operation */
	int (*funct)(struct nfs_argop4 *,
		     compound_data_t *,
		     struct nfs_resop4 *);

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
		.free_res = nfs4_op_illegal_Free,
		.resp_size = sizeof(ILLEGAL4res),
		.exp_perm_flags = 0},
	[1] = {
		.name = "OP_ILLEGAL",
		.funct = nfs4_op_illegal,
		.free_res = nfs4_op_illegal_Free,
		.resp_size = sizeof(ILLEGAL4res),
		.exp_perm_flags = 0},
	[2] = {
		.name = "OP_ILLEGAL",
		.funct = nfs4_op_illegal,
		.free_res = nfs4_op_illegal_Free,
		.resp_size = sizeof(ILLEGAL4res),
		.exp_perm_flags = 0},
	[NFS4_OP_ACCESS] = {
		.name = "OP_ACCESS",
		.funct = nfs4_op_access,
		.free_res = nfs4_op_access_Free,
		.resp_size = sizeof(ACCESS4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_CLOSE] = {
		.name = "OP_CLOSE",
		.funct = nfs4_op_close,
		.free_res = nfs4_op_close_Free,
		.resp_size = sizeof(CLOSE4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_COMMIT] = {
		.name = "OP_COMMIT",
		.funct = nfs4_op_commit,
		.free_res = nfs4_op_commit_Free,
		.resp_size = sizeof(COMMIT4res),
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_CREATE] = {
		.name = "OP_CREATE",
		.funct = nfs4_op_create,
		.free_res = nfs4_op_create_Free,
		.resp_size = sizeof(CREATE4res),
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_DELEGPURGE] = {
		.name = "OP_DELEGPURGE",
		.funct = nfs4_op_delegpurge,
		.free_res = nfs4_op_delegpurge_Free,
		.resp_size = sizeof(DELEGPURGE4res),
		.exp_perm_flags = 0},
	[NFS4_OP_DELEGRETURN] = {
		.name = "OP_DELEGRETURN",
		.funct = nfs4_op_delegreturn,
		.free_res = nfs4_op_delegreturn_Free,
		.resp_size = sizeof(DELEGRETURN4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_GETATTR] = {
		.name = "OP_GETATTR",
		.funct = nfs4_op_getattr,
		.free_res = nfs4_op_getattr_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_GETFH] = {
		.name = "OP_GETFH",
		.funct = nfs4_op_getfh,
		.free_res = nfs4_op_getfh_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = 0},
	[NFS4_OP_LINK] = {
		.name = "OP_LINK",
		.funct = nfs4_op_link,
		.free_res = nfs4_op_link_Free,
		.resp_size = sizeof(LINK4res),
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_LOCK] = {
		.name = "OP_LOCK",
		.funct = nfs4_op_lock,
		.free_res = nfs4_op_lock_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LOCKT] = {
		.name = "OP_LOCKT",
		.funct = nfs4_op_lockt,
		.free_res = nfs4_op_lockt_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LOCKU] = {
		.name = "OP_LOCKU",
		.funct = nfs4_op_locku,
		.free_res = nfs4_op_locku_Free,
		.resp_size = sizeof(LOCKU4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LOOKUP] = {
		.name = "OP_LOOKUP",
		.funct = nfs4_op_lookup,
		.free_res = nfs4_op_lookup_Free,
		.resp_size = sizeof(LOOKUP4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LOOKUPP] = {
		.name = "OP_LOOKUPP",
		.funct = nfs4_op_lookupp,
		.free_res = nfs4_op_lookupp_Free,
		.resp_size = sizeof(LOOKUPP4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_NVERIFY] = {
		.name = "OP_NVERIFY",
		.funct = nfs4_op_nverify,
		.free_res = nfs4_op_nverify_Free,
		.resp_size = sizeof(NVERIFY4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_OPEN] = {
		.name = "OP_OPEN",
		.funct = nfs4_op_open,
		.free_res = nfs4_op_open_Free,
		.resp_size = sizeof(OPEN4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_OPENATTR] = {
		.name = "OP_OPENATTR",
		.funct = nfs4_op_openattr,
		.free_res = nfs4_op_openattr_Free,
		.resp_size = sizeof(OPENATTR4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_OPEN_CONFIRM] = {
		.name = "OP_OPEN_CONFIRM",
		.funct = nfs4_op_open_confirm,
		.free_res = nfs4_op_open_confirm_Free,
		.resp_size = sizeof(OPEN_CONFIRM4res),
		.exp_perm_flags = 0},
	[NFS4_OP_OPEN_DOWNGRADE] = {
		.name = "OP_OPEN_DOWNGRADE",
		.funct = nfs4_op_open_downgrade,
		.free_res = nfs4_op_open_downgrade_Free,
		.resp_size = sizeof(OPEN_DOWNGRADE4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_PUTFH] = {
		.name = "OP_PUTFH",
		.funct = nfs4_op_putfh,
		.free_res = nfs4_op_putfh_Free,
		.resp_size = sizeof(PUTFH4res),
		.exp_perm_flags = 0},
	[NFS4_OP_PUTPUBFH] = {
		.name = "OP_PUTPUBFH",
		.funct = nfs4_op_putpubfh,
		.free_res = nfs4_op_putpubfh_Free,
		.resp_size = sizeof(PUTPUBFH4res),
		.exp_perm_flags = 0},
	[NFS4_OP_PUTROOTFH] = {
		.name = "OP_PUTROOTFH",
		.funct = nfs4_op_putrootfh,
		.free_res = nfs4_op_putrootfh_Free,
		.resp_size = sizeof(PUTROOTFH4res),
		.exp_perm_flags = 0},
	[NFS4_OP_READ] = {
		.name = "OP_READ",
		.funct = nfs4_op_read,
		.free_res = nfs4_op_read_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_READ_ACCESS},
	[NFS4_OP_READDIR] = {
		.name = "OP_READDIR",
		.funct = nfs4_op_readdir,
		.free_res = nfs4_op_readdir_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_READLINK] = {
		.name = "OP_READLINK",
		.funct = nfs4_op_readlink,
		.free_res = nfs4_op_readlink_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_REMOVE] = {
		.name = "OP_REMOVE",
		.funct = nfs4_op_remove,
		.free_res = nfs4_op_remove_Free,
		.resp_size = sizeof(REMOVE4res),
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_RENAME] = {
		.name = "OP_RENAME",
		.funct = nfs4_op_rename,
		.free_res = nfs4_op_rename_Free,
		.resp_size = sizeof(RENAME4res),
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_RENEW] = {
		.name = "OP_RENEW",
		.funct = nfs4_op_renew,
		.free_res = nfs4_op_renew_Free,
		.resp_size = sizeof(RENEW4res),
		.exp_perm_flags = 0},
	[NFS4_OP_RESTOREFH] = {
		.name = "OP_RESTOREFH",
		.funct = nfs4_op_restorefh,
		.free_res = nfs4_op_restorefh_Free,
		.resp_size = sizeof(RESTOREFH4res),
		.exp_perm_flags = 0},
	[NFS4_OP_SAVEFH] = {
		.name = "OP_SAVEFH",
		.funct = nfs4_op_savefh,
		.free_res = nfs4_op_savefh_Free,
		.resp_size = sizeof(SAVEFH4res),
		.exp_perm_flags = 0},
	[NFS4_OP_SECINFO] = {
		.name = "OP_SECINFO",
		.funct = nfs4_op_secinfo,
		.free_res = nfs4_op_secinfo_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_SETATTR] = {
		.name = "OP_SETATTR",
		.funct = nfs4_op_setattr,
		.free_res = nfs4_op_setattr_Free,
		.resp_size = sizeof(SETATTR4res),
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_SETCLIENTID] = {
		.name = "OP_SETCLIENTID",
		.funct = nfs4_op_setclientid,
		.free_res = nfs4_op_setclientid_Free,
		.resp_size = sizeof(SETCLIENTID4res),
		.exp_perm_flags = 0},
	[NFS4_OP_SETCLIENTID_CONFIRM] = {
		.name = "OP_SETCLIENTID_CONFIRM",
		.funct = nfs4_op_setclientid_confirm,
		.free_res = nfs4_op_setclientid_confirm_Free,
		.resp_size = sizeof(SETCLIENTID_CONFIRM4res),
		.exp_perm_flags = 0},
	[NFS4_OP_VERIFY] = {
		.name = "OP_VERIFY",
		.funct = nfs4_op_verify,
		.free_res = nfs4_op_verify_Free,
		.resp_size = sizeof(VERIFY4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_WRITE] = {
		.name = "OP_WRITE",
		.funct = nfs4_op_write,
		.free_res = nfs4_op_write_Free,
		.resp_size = sizeof(WRITE4res),
		.exp_perm_flags = EXPORT_OPTION_WRITE_ACCESS},
	[NFS4_OP_RELEASE_LOCKOWNER] = {
		.name = "OP_RELEASE_LOCKOWNER",
		.funct = nfs4_op_release_lockowner,
		.free_res = nfs4_op_release_lockowner_Free,
		.resp_size = sizeof(RELEASE_LOCKOWNER4res),
		.exp_perm_flags = 0},
	[NFS4_OP_BACKCHANNEL_CTL] = {
		.name = "OP_BACKCHANNEL_CTL",
		.funct = nfs4_op_illegal,
		.free_res = nfs4_op_illegal_Free,
		.resp_size = sizeof(BACKCHANNEL_CTL4res),
		.exp_perm_flags = 0	/* tbd */},
	[NFS4_OP_BIND_CONN_TO_SESSION] = {
		.name = "OP_BIND_CONN_TO_SESSION",
		.funct = nfs4_op_bind_conn,
		.free_res = nfs4_op_nfs4_op_bind_conn_Free,
		.resp_size = sizeof(BIND_CONN_TO_SESSION4res),
		.exp_perm_flags = 0	/* tbd */},
	[NFS4_OP_EXCHANGE_ID] = {
		.name = "OP_EXCHANGE_ID",
		.funct = nfs4_op_exchange_id,
		.free_res = nfs4_op_exchange_id_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = 0},
	[NFS4_OP_CREATE_SESSION] = {
		.name = "OP_CREATE_SESSION",
		.funct = nfs4_op_create_session,
		.free_res = nfs4_op_create_session_Free,
		.resp_size = sizeof(CREATE_SESSION4res),
		.exp_perm_flags = 0},
	[NFS4_OP_DESTROY_SESSION] = {
		.name = "OP_DESTROY_SESSION",
		.funct = nfs4_op_destroy_session,
		.free_res = nfs4_op_reclaim_complete_Free,
		.resp_size = sizeof(DESTROY_SESSION4res),
		.exp_perm_flags = 0},
	[NFS4_OP_FREE_STATEID] = {
		.name = "OP_FREE_STATEID",
		.funct = nfs4_op_free_stateid,
		.free_res = nfs4_op_free_stateid_Free,
		.resp_size = sizeof(FREE_STATEID4res),
		.exp_perm_flags = 0},
	[NFS4_OP_GET_DIR_DELEGATION] = {
		.name = "OP_GET_DIR_DELEGATION",
		.funct = nfs4_op_illegal,
		.free_res = nfs4_op_illegal_Free,
		.resp_size = sizeof(GET_DIR_DELEGATION4res),
		.exp_perm_flags = 0	/* tbd */},
	[NFS4_OP_GETDEVICEINFO] = {
		.name = "OP_GETDEVICEINFO",
		.funct = nfs4_op_getdeviceinfo,
		.free_res = nfs4_op_getdeviceinfo_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = 0},
	[NFS4_OP_GETDEVICELIST] = {
		.name = "OP_GETDEVICELIST",
		.funct = nfs4_op_getdevicelist,
		.free_res = nfs4_op_getdevicelist_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LAYOUTCOMMIT] = {
		.name = "OP_LAYOUTCOMMIT",
		.funct = nfs4_op_layoutcommit,
		.free_res = nfs4_op_reclaim_complete_Free,
		.resp_size = sizeof(LAYOUTCOMMIT4res),
		.exp_perm_flags = 0},
	[NFS4_OP_LAYOUTGET] = {
		.name = "OP_LAYOUTGET",
		.funct = nfs4_op_layoutget,
		.free_res = nfs4_op_reclaim_complete_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LAYOUTRETURN] = {
		.name = "OP_LAYOUTRETURN",
		.funct = nfs4_op_layoutreturn,
		.free_res = nfs4_op_reclaim_complete_Free,
		.resp_size = sizeof(LAYOUTRETURN4res),
		.exp_perm_flags = 0},
	[NFS4_OP_SECINFO_NO_NAME] = {
		.name = "OP_SECINFO_NO_NAME",
		.funct = nfs4_op_secinfo_no_name,
		.free_res = nfs4_op_secinfo_no_name_Free,
		.resp_size = VARIABLE_RESP_SIZE,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_SEQUENCE] = {
		.name = "OP_SEQUENCE",
		.funct = nfs4_op_sequence,
		.free_res = nfs4_op_sequence_Free,
		.resp_size = sizeof(SEQUENCE4res),
		.exp_perm_flags = 0},
	[NFS4_OP_SET_SSV] = {
		.name = "OP_SET_SSV",
		.funct = nfs4_op_set_ssv,
		.free_res = nfs4_op_set_ssv_Free,
		.resp_size = sizeof(SET_SSV4res),
		.exp_perm_flags = 0},
	[NFS4_OP_TEST_STATEID] = {
		.name = "OP_TEST_STATEID",
		.funct = nfs4_op_test_stateid,
		.free_res = nfs4_op_test_stateid_Free,
		.resp_size = sizeof(TEST_STATEID4res),
		.exp_perm_flags = 0},
	[NFS4_OP_WANT_DELEGATION] = {
		.name = "OP_WANT_DELEGATION",
		.funct = nfs4_op_illegal,
		.free_res = nfs4_op_illegal_Free,
		.resp_size = sizeof(WANT_DELEGATION4res),
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS	/* tbd */},
	[NFS4_OP_DESTROY_CLIENTID] = {
		.name = "OP_DESTROY_CLIENTID",
		.funct = nfs4_op_destroy_clientid,
		.free_res = nfs4_op_destroy_clientid_Free,
		.resp_size = sizeof(DESTROY_CLIENTID4res),
		.exp_perm_flags = 0	/* tbd */},
	[NFS4_OP_RECLAIM_COMPLETE] = {
		.name = "OP_RECLAIM_COMPLETE",
		.funct = nfs4_op_reclaim_complete,
		.free_res = nfs4_op_reclaim_complete_Free,
		.resp_size = sizeof(RECLAIM_COMPLETE4res),
		.exp_perm_flags = 0},

	/* NFSv4.2 */
	[NFS4_OP_ALLOCATE] = {
		.name = "OP_ALLOCATE",
		.funct = nfs4_op_allocate,
		.free_res = nfs4_op_write_Free,
		.resp_size = sizeof(ALLOCATE4res),
		.exp_perm_flags = 0},
	[NFS4_OP_COPY] = {
		.name = "OP_COPY",
		.funct = nfs4_op_notsupp,
		.free_res = nfs4_op_notsupp_Free,
		.resp_size = sizeof(COPY4res),
		.exp_perm_flags = 0},
	[NFS4_OP_COPY_NOTIFY] = {
		.name = "OP_COPY_NOTIFY",
		.funct = nfs4_op_notsupp,
		.free_res = nfs4_op_notsupp_Free,
		.resp_size = sizeof(COPY_NOTIFY4res),
		.exp_perm_flags = 0},
	[NFS4_OP_DEALLOCATE] = {
		.name = "OP_DEALLOCATE",
		.funct = nfs4_op_deallocate,
		.free_res = nfs4_op_write_Free,
		.resp_size = sizeof(DEALLOCATE4res),
		.exp_perm_flags = 0},
	[NFS4_OP_IO_ADVISE] = {
		.name = "OP_IO_ADVISE",
		.funct = nfs4_op_io_advise,
		.free_res = nfs4_op_io_advise_Free,
		.resp_size = sizeof(IO_ADVISE4res),
		.exp_perm_flags = 0},
	[NFS4_OP_LAYOUTERROR] = {
		.name = "OP_LAYOUTERROR",
		.funct = nfs4_op_layouterror,
		.free_res = nfs4_op_layouterror_Free,
		.resp_size = sizeof(LAYOUTERROR4res),
		.exp_perm_flags = 0},
	[NFS4_OP_LAYOUTSTATS] = {
		.name = "OP_LAYOUTSTATS",
		.funct = nfs4_op_layoutstats,
		.free_res = nfs4_op_layoutstats_Free,
		.resp_size = sizeof(LAYOUTSTATS4res),
		.exp_perm_flags = 0},
	[NFS4_OP_OFFLOAD_CANCEL] = {
		.name = "OP_OFFLOAD_CANCEL",
		.funct = nfs4_op_notsupp,
		.free_res = nfs4_op_notsupp_Free,
		.resp_size = sizeof(OFFLOAD_ABORT4res),
		.exp_perm_flags = 0},
	[NFS4_OP_OFFLOAD_STATUS] = {
		.name = "OP_OFFLOAD_STATUS",
		.funct = nfs4_op_notsupp,
		.free_res = nfs4_op_notsupp_Free,
		.resp_size = sizeof(OFFLOAD_STATUS4res),
		.exp_perm_flags = 0},
	[NFS4_OP_READ_PLUS] = {
		.name = "OP_READ_PLUS",
		.funct = nfs4_op_read_plus,
		.free_res = nfs4_op_read_plus_Free,
		.resp_size = sizeof(READ_PLUS4res),
		.exp_perm_flags = 0},
	[NFS4_OP_SEEK] = {
		.name = "OP_SEEK",
		.funct = nfs4_op_seek,
		.free_res = nfs4_op_write_Free,
		.resp_size = sizeof(SEEK4res),
		.exp_perm_flags = 0},
	[NFS4_OP_WRITE_SAME] = {
		.name = "OP_WRITE_SAME",
		.funct = nfs4_op_write_same,
		.free_res = nfs4_op_write_same_Free,
		.resp_size = sizeof(WRITE_SAME4res),
		.exp_perm_flags = 0},
	[NFS4_OP_CLONE] = {
		.name = "OP_CLONE",
		.funct = nfs4_op_notsupp,
		.free_res = nfs4_op_notsupp_Free,
		.resp_size = sizeof(ILLEGAL4res),
		.exp_perm_flags = 0},

	/* NFSv4.3 */
	[NFS4_OP_GETXATTR] = {
		.name = "OP_GETXATTR",
		.funct = nfs4_op_getxattr,
		.free_res = nfs4_op_getxattr_Free,
		.resp_size = sizeof(GETXATTR4res),
		.exp_perm_flags = 0},
	[NFS4_OP_SETXATTR] = {
		.name = "OP_SETXATTR",
		.funct = nfs4_op_setxattr,
		.free_res = nfs4_op_setxattr_Free,
		.resp_size = sizeof(SETXATTR4res),
		.exp_perm_flags = 0},
	[NFS4_OP_LISTXATTR] = {
		.name = "OP_LISTXATTR",
		.funct = nfs4_op_listxattr,
		.free_res = nfs4_op_listxattr_Free,
		.resp_size = sizeof(LISTXATTR4res),
		.exp_perm_flags = 0},
	[NFS4_OP_REMOVEXATTR] = {
		.name = "OP_REMOVEXATTR",
		.funct = nfs4_op_removexattr,
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
	unsigned int i = 0;
	int status = NFS4_OK;
	compound_data_t data;
	nfs_opnum4 opcode;
	const uint32_t compound4_minor = arg->arg_compound4.minorversion;
	const uint32_t argarray_len = arg->arg_compound4.argarray.argarray_len;
	/* Array of op arguments */
	nfs_argop4 * const argarray = arg->arg_compound4.argarray.argarray_val;
	nfs_resop4 *resarray;
	nsecs_elapsed_t op_start_time;
	struct timespec ts;
	int perm_flags;
	char *notag = "NO TAG";
	char *tagname = notag;
	const char *bad_op_state_reason = "";
	log_components_t alt_component = COMPONENT_NFS_V4;

	if (compound4_minor > 2) {
		LogCrit(COMPONENT_NFS_V4, "Bad Minor Version %d",
			compound4_minor);

		res->res_compound4.status = NFS4ERR_MINOR_VERS_MISMATCH;
		res->res_compound4.resarray.resarray_len = 0;
		return NFS_REQ_OK;
	}

	if ((nfs_param.nfsv4_param.minor_versions &
			(1 << compound4_minor)) == 0) {
		LogInfo(COMPONENT_NFS_V4, "Unsupported minor version %d",
			compound4_minor);
		res->res_compound4.status = NFS4ERR_MINOR_VERS_MISMATCH;
		res->res_compound4.resarray.resarray_len = 0;
		return NFS_REQ_OK;
	}

	/* Keeping the same tag as in the arguments */
	copy_tag(&res->res_compound4.tag, &arg->arg_compound4.tag);

	if (res->res_compound4.tag.utf8string_len > 0) {
		/* Check if the tag is a valid utf8 string */
		status =
		    nfs4_utf8string2dynamic(&(res->res_compound4.tag),
					    UTF8_SCAN_ALL, &tagname);
		if (status != 0) {
			char str[LOG_BUFF_LEN];
			struct display_buffer dspbuf = {sizeof(str), str, str};

			display_opaque_bytes(
				&dspbuf,
				res->res_compound4.tag.utf8string_val,
				res->res_compound4.tag.utf8string_len);

			LogCrit(COMPONENT_NFS_V4,
				"COMPOUND: bad tag %p len %d bytes %s",
				res->res_compound4.tag.utf8string_val,
				res->res_compound4.tag.utf8string_len,
				str);

			status = NFS4ERR_INVAL;
			res->res_compound4.status = status;
			res->res_compound4.resarray.resarray_len = 0;
			return NFS_REQ_OK;
		}
	}

	/* Managing the operation list */
	LogDebug(COMPONENT_NFS_V4,
		 "COMPOUND: There are %d operations, res = %p, tag = %s",
		 argarray_len, res, tagname);

	if (tagname != notag)
		gsh_free(tagname);

	/* Check for empty COMPOUND request */
	if (argarray_len == 0) {
		LogMajor(COMPONENT_NFS_V4,
			 "An empty COMPOUND (no operation in it) was received");

		res->res_compound4.status = NFS4_OK;
		res->res_compound4.resarray.resarray_len = 0;
		return NFS_REQ_OK;
	}

	/* Check for too long request */
	if (argarray_len > 100) {
		LogMajor(COMPONENT_NFS_V4,
			 "A COMPOUND with too many operations (%d) was received",
			 argarray_len);

		res->res_compound4.status = NFS4ERR_RESOURCE;
		res->res_compound4.resarray.resarray_len = 0;
		return NFS_REQ_OK;
	}

	/* Initialisation of the compound request internal's data */
	memset(&data, 0, sizeof(data));
	op_ctx->nfs_minorvers = compound4_minor;

	/* Minor version related stuff */
	data.minorversion = compound4_minor;
	data.req = req;

	/* Initialize response size with size of compound response size. */
	data.resp_size = sizeof(COMPOUND4res) - sizeof(nfs_resop4 *);

	/* Building the client credential field */
	if (nfs_rpc_req2client_cred(req, &(data.credential)) == -1)
		return NFS_REQ_DROP;	/* Malformed credential */

	/* Keeping the same tag as in the arguments */
	res->res_compound4.tag.utf8string_len =
	    arg->arg_compound4.tag.utf8string_len;

	/* Allocating the reply nfs_resop4 */
	res->res_compound4.resarray.resarray_val =
		gsh_calloc(argarray_len, sizeof(struct nfs_resop4));

	res->res_compound4.resarray.resarray_len = argarray_len;
	resarray = res->res_compound4.resarray.resarray_val;

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
			status = NFS4ERR_OP_NOT_IN_SESSION;
			res->res_compound4.status = status;
			res->res_compound4.resarray.resarray_len = 0;
			return NFS_REQ_OK;
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
				status = NFS4ERR_NOT_ONLY_OP;
				res->res_compound4.status = status;
				res->res_compound4.resarray.resarray_len = 0;
				return NFS_REQ_OK;
			}
		}
	}

	for (i = 0; i < argarray_len; i++) {
		/* Used to check if OP_SEQUENCE is the first operation */
		data.oppos = i;
		data.op_resp_size = sizeof(nfsstat4);
		opcode = argarray[i].argop;

		/* Handle opcode overflow */
		if (opcode > LastOpcode[compound4_minor])
			opcode = 0;

		data.opname = optabv4[opcode].name;

		LogDebug(COMPONENT_NFS_V4, "Request %d: opcode %d is %s", i,
			 argarray[i].argop, data.opname);

		/* Verify BIND_CONN_TO_SESSION is not used in a compound
		 * with length > 1. This check is NOT redundant with the
		 * checks above.
		 */
		if (i > 0 &&
		    argarray[i].argop == NFS4_OP_BIND_CONN_TO_SESSION) {
			status = NFS4ERR_NOT_ONLY_OP;
			bad_op_state_reason =
					"BIND_CONN_TO_SESSION past position 1";
			goto bad_op_state;
		}

		/* OP_SEQUENCE is always the first operation of the request */
		if (i > 0 && argarray[i].argop == NFS4_OP_SEQUENCE) {
			status = NFS4ERR_SEQUENCE_POS;
			bad_op_state_reason =
					"SEQUENCE past position 1";
			goto bad_op_state;
		}

		/* If a DESTROY_SESSION not the only operation, and it matches
		 * the session specified in the SEQUENCE op (since the compound
		 * has more than one op, we already know it MUST start with
		 * SEQUENCE), then it MUST be the final op in the compound.
		 */
		if (i > 0 && argarray[i].argop == NFS4_OP_DESTROY_SESSION) {
			bool session_compare;
			bool bad_pos;

			session_compare = memcmp(
			    argarray[0].nfs_argop4_u.opsequence.sa_sessionid,
			    argarray[i]
				.nfs_argop4_u.opdestroy_session.dsa_sessionid,
			    NFS4_SESSIONID_SIZE) == 0;

			bad_pos = session_compare && i != (argarray_len - 1);

			LogAtLevel(COMPONENT_SESSIONS,
				   bad_pos ? NIV_INFO : NIV_DEBUG,
				   "DESTROY_SESSION in position %u out of 0-%"
				   PRIi32 " %s is %s",
				   i, argarray_len - 1, session_compare
					? "same session as SEQUENCE"
					: "different session from SEQUENCE",
				   bad_pos ? "not last op in compound" : "opk");

			if (bad_pos) {
				status = NFS4ERR_NOT_ONLY_OP;
				bad_op_state_reason =
				    "DESTROY_SESSION not last op in compound";
				goto bad_op_state;
			}
		}

		/* time each op */
		now(&ts);
		op_start_time = timespec_diff(&nfs_ServerBootTime, &ts);

		if (compound4_minor > 0 && data.session != NULL &&
		    data.session->fore_channel_attrs.ca_maxoperations == i) {
			status = NFS4ERR_TOO_MANY_OPS;
			bad_op_state_reason = "Too many operations";
			goto bad_op_state;
		}

		perm_flags =
		    optabv4[opcode].exp_perm_flags & EXPORT_OPTION_ACCESS_MASK;

		if (perm_flags != 0) {
			status = nfs4_Is_Fh_Empty(&data.currentFH);
			if (status != NFS4_OK) {
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
					status = NFS4ERR_ROFS;
				else
					status = NFS4ERR_ACCESS;

				bad_op_state_reason =
						"Export permission failure";
				alt_component = COMPONENT_EXPORT;
				goto bad_op_state;
			}
		}

		/* Set up the minimum/default response size and check if there
		 * is room for it.
		*/
		data.op_resp_size = optabv4[opcode].resp_size;

		status = check_resp_room(&data, data.op_resp_size);

		if (status != NFS4_OK) {
			bad_op_state_reason = "op response size";

 bad_op_state:
			/* Tally the response size */
			data.resp_size += sizeof(nfs_opnum4) + sizeof(nfsstat4);

			LogDebugAlt(COMPONENT_NFS_V4, alt_component,
				    "Status of %s in position %d due to %s is %s, op response size = %"
				    PRIu32" total response size = %"PRIu32,
				    data.opname, i, bad_op_state_reason,
				    nfsstat4_to_str(status),
				    data.op_resp_size, data.resp_size);

			/* All the operation, like NFS4_OP_ACCESS, have
			 * a first replied field called .status
			 */
			resarray[i].nfs_resop4_u.opaccess.status = status;
			resarray[i].resop = argarray[i].argop;

			/* Do not manage the other requests in the COMPOUND. */
			res->res_compound4.resarray.resarray_len = i + 1;
			break;
		}

		/***************************************************************
		 * Make the actual op call                                     *
		 **************************************************************/
#ifdef USE_LTTNG
		tracepoint(nfs_rpc, v4op_start, i, argarray[i].argop,
			   data.opname);
#endif

		status = (optabv4[opcode].funct) (&argarray[i],
						  &data,
						  &resarray[i]);

#ifdef USE_LTTNG
		tracepoint(nfs_rpc, v4op_end, i, argarray[i].argop,
			   data.opname, nfsstat4_to_str(status));
#endif

		LogCompoundFH(&data);

		/* All the operation, like NFS4_OP_ACESS, have a first replyied
		 * field called .status
		 */
		resarray[i].nfs_resop4_u.opaccess.status = status;

		server_stats_nfsv4_op_done(opcode, op_start_time, status);

		/* Tally the response size */
		if (status != NFS4_OK &&
		    (optabv4[opcode].resp_size != VARIABLE_RESP_SIZE ||
		     data.op_resp_size == VARIABLE_RESP_SIZE)) {
			/* If the op failed and has a static response size, or
			 * it has a variable size that hasn't been set, use the
			 * sizeof nfsstat4 instead.
			 */
			data.op_resp_size = sizeof(nfsstat4);
		}

		data.resp_size += sizeof(nfs_opnum4) + data.op_resp_size;

		LogDebug(COMPONENT_NFS_V4,
			 "Status of %s in position %d = %s, op response size is %"
			 PRIu32" total response size is %"PRIu32,
			 data.opname, i, nfsstat4_to_str(status),
			 data.op_resp_size, data.resp_size);

		if (status != NFS4_OK) {
			/* An error occured, we do not manage the other requests
			 * in the COMPOUND, this may be a regular behavior
			 */
			res->res_compound4.resarray.resarray_len = i + 1;
			break;
		}

		/* NFS_V4.1 specific stuff */
		if (data.use_slot_cached_result) {
			/* Replay cache, only true for SEQUENCE or
			 * CREATE_SESSION w/o SEQUENCE. Since will only be set
			 * in those cases, no need to check operation or
			 * anything.
			 */

			/* Free the reply allocated above */
			gsh_free(res->res_compound4.resarray.resarray_val);

			/* Copy the reply from the cache */
			res->res_compound4_extended = *data.cached_result;
			status = ((COMPOUND4res *) data.cached_result)->status;
			LogFullDebug(COMPONENT_SESSIONS,
				     "Use session replay cache %p result %s",
				     data.cached_result,
				     nfsstat4_to_str(status));
			break;	/* Exit the for loop */
		}
	}			/* for */

	server_stats_compound_done(argarray_len, status);

	/* Complete the reply, in particular, tell where you stopped if
	 * unsuccessfull COMPOUD
	 */
	res->res_compound4.status = status;

	/* Manage session's DRC: keep NFS4.1 replay for later use, but don't
	 * save a replayed result again.
	 */
	if (data.sa_cachethis) {
		/* Pointer has been set by nfs4_op_sequence and points to slot
		 * to cache result in.
		 */
		LogFullDebug(COMPONENT_SESSIONS,
			     "Save result in session replay cache %p sizeof nfs_res_t=%d",
			     data.cached_result, (int)sizeof(nfs_res_t));

		/* Indicate to nfs4_Compound_Free that this reply is cached. */
		res->res_compound4_extended.res_cached = true;

		/* Save the result in the cache (copy out of the result array
		 * into the slot cache (which is pointed to by
		 * data.cached_result).
		 */
		*data.cached_result = res->res_compound4_extended;
	} else if (compound4_minor > 0 && !data.use_slot_cached_result &&
		   argarray[0].argop == NFS4_OP_SEQUENCE &&
		   data.cached_result != NULL) {
		/* We need to cache an "uncached" response. The length is
		 * 1 if only one op processed, otherwise 2. */
		struct COMPOUND4res *c_res = &data.cached_result->res_compound4;
		u_int resarray_len =
			res->res_compound4.resarray.resarray_len == 1 ? 1 : 2;
		struct nfs_resop4 *res0;

		c_res->resarray.resarray_len = resarray_len;
		c_res->resarray.resarray_val =
			gsh_calloc(resarray_len, sizeof(struct nfs_resop4));
		copy_tag(&c_res->tag, &res->res_compound4.tag);
		res0 = c_res->resarray.resarray_val;

		/* Copy the sequence result. */
		*res0 = res->res_compound4.resarray.resarray_val[0];
		c_res->status = res0->nfs_resop4_u.opillegal.status;

		if (resarray_len == 2) {
			struct nfs_resop4 *res1 = res0 + 1;

			/* Shallow copy response since we will override any
			 * resok or any negative response that might have
			 * allocated data.
			 */
			*res1 = res->res_compound4.resarray.resarray_val[1];

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

		/* Indicate that this reply is cached in slot cache. */
		data.cached_result->res_cached = true;
	}

	/* If we have reserved a lease, update it and release it */
	if (data.preserved_clientid != NULL) {
		/* Update and release lease */
		PTHREAD_MUTEX_lock(&data.preserved_clientid->cid_mutex);

		update_lease(data.preserved_clientid);

		PTHREAD_MUTEX_unlock(&data.preserved_clientid->cid_mutex);
	}

	if (status != NFS4_OK)
		LogDebug(COMPONENT_NFS_V4, "End status = %s lastindex = %d",
			 nfsstat4_to_str(status), i);

	compound_data_Free(&data);

	/* release current active export in op_ctx. */
	if (op_ctx->ctx_export) {
		put_gsh_export(op_ctx->ctx_export);
		op_ctx->ctx_export = NULL;
		op_ctx->fsal_export = NULL;
	}

	return NFS_REQ_OK;
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
	unsigned int i = 0;
	log_components_t component = COMPONENT_NFS_V4;

	if (isFullDebug(COMPONENT_SESSIONS))
		component = COMPONENT_SESSIONS;

	if (res->res_compound4_extended.res_cached) {
		LogFullDebug(component,
			     "Skipping free of NFS4 result %p",
			     res);
		return;
	}

	LogFullDebug(component,
		     "Compound Free %p (resarraylen=%i)",
		     res, res->res_compound4.resarray.resarray_len);

	for (i = 0; i < res->res_compound4.resarray.resarray_len; i++) {
		nfs_resop4 *val = &res->res_compound4.resarray.resarray_val[i];

		if (val) {
			/* !val is an error case, but it can occur, so avoid
			 * indirect on NULL
			 */
			nfs4_Compound_FreeOne(val);
		}
	}

	gsh_free(res->res_compound4.resarray.resarray_val);
	res->res_compound4.resarray.resarray_val = NULL;

	gsh_free(res->res_compound4.tag.utf8string_val);
	res->res_compound4.tag.utf8string_val = NULL;
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
	/* Release refcounted cache entries */
	set_current_entry(data, NULL);
	set_saved_entry(data, NULL);

	if (data->session) {
		if (data->slot != UINT32_MAX) {
			nfs41_session_slot_t *slot;

			/* Release the slot if in use */
			slot = &data->session->fc_slots[data->slot];
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
 *
 * @brief Copy the result for NFS4PROC_COMPOUND
 *
 * This function copies a single COMPOUND result.
 *
 * @param[out] res_dst Buffer to which to copy the result
 * @param[in]  res_src Result to copy
 *
 */
void nfs4_Compound_CopyRes(nfs_res_t *res_dst, nfs_res_t *res_src)
{
	unsigned int i = 0;

	LogFullDebug(COMPONENT_NFS_V4,
		     "Copy result of %p to %p (resarraylen : %i)",
		     res_src, res_dst,
		     res_src->res_compound4.resarray.resarray_len);

	for (i = 0; i < res_src->res_compound4.resarray.resarray_len; i++)
		nfs4_Compound_CopyResOne(
			&res_dst->res_compound4.resarray.resarray_val[i],
			&res_src->res_compound4.resarray.resarray_val[i]);
}

/* @} */

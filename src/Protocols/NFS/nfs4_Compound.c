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

struct nfs4_op_desc {
	char *name;
	int (*funct) (struct nfs_argop4 *, compound_data_t *,
		      struct nfs_resop4 *);
	void (*free_res) (nfs_resop4 *);
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
		.exp_perm_flags = 0},
	[1] = {
		.name = "OP_ILLEGAL",
		.funct = nfs4_op_illegal,
		.free_res = nfs4_op_illegal_Free,
		.exp_perm_flags = 0},
	[2] = {
		.name = "OP_ILLEGAL",
		.funct = nfs4_op_illegal,
		.free_res = nfs4_op_illegal_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_ACCESS] = {
		.name = "OP_ACCESS",
		.funct = nfs4_op_access,
		.free_res = nfs4_op_access_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_CLOSE] = {
		.name = "OP_CLOSE",
		.funct = nfs4_op_close,
		.free_res = nfs4_op_close_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_COMMIT] = {
		.name = "OP_COMMIT",
		.funct = nfs4_op_commit,
		.free_res = nfs4_op_commit_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_CREATE] = {
		.name = "OP_CREATE",
		.funct = nfs4_op_create,
		.free_res = nfs4_op_create_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_DELEGPURGE] = {
		.name = "OP_DELEGPURGE",
		.funct = nfs4_op_delegpurge,
		.free_res = nfs4_op_delegpurge_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_DELEGRETURN] = {
		.name = "OP_DELEGRETURN",
		.funct = nfs4_op_delegreturn,
		.free_res = nfs4_op_delegreturn_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_GETATTR] = {
		.name = "OP_GETATTR",
		.funct = nfs4_op_getattr,
		.free_res = nfs4_op_getattr_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_GETFH] = {
		.name = "OP_GETFH",
		.funct = nfs4_op_getfh,
		.free_res = nfs4_op_getfh_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_LINK] = {
		.name = "OP_LINK",
		.funct = nfs4_op_link,
		.free_res = nfs4_op_link_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_LOCK] = {
		.name = "OP_LOCK",
		.funct = nfs4_op_lock,
		.free_res = nfs4_op_lock_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LOCKT] = {
		.name = "OP_LOCKT",
		.funct = nfs4_op_lockt,
		.free_res = nfs4_op_lockt_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LOCKU] = {
		.name = "OP_LOCKU",
		.funct = nfs4_op_locku,
		.free_res = nfs4_op_locku_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LOOKUP] = {
		.name = "OP_LOOKUP",
		.funct = nfs4_op_lookup,
		.free_res = nfs4_op_lookup_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LOOKUPP] = {
		.name = "OP_LOOKUPP",
		.funct = nfs4_op_lookupp,
		.free_res = nfs4_op_lookupp_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_NVERIFY] = {
		.name = "OP_NVERIFY",
		.funct = nfs4_op_nverify,
		.free_res = nfs4_op_nverify_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_OPEN] = {
		.name = "OP_OPEN",
		.funct = nfs4_op_open,
		.free_res = nfs4_op_open_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_OPENATTR] = {
		.name = "OP_OPENATTR",
		.funct = nfs4_op_openattr,
		.free_res = nfs4_op_openattr_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_OPEN_CONFIRM] = {
		.name = "OP_OPEN_CONFIRM",
		.funct = nfs4_op_open_confirm,
		.free_res = nfs4_op_open_confirm_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_OPEN_DOWNGRADE] = {
		.name = "OP_OPEN_DOWNGRADE",
		.funct = nfs4_op_open_downgrade,
		.free_res = nfs4_op_open_downgrade_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_PUTFH] = {
		.name = "OP_PUTFH",
		.funct = nfs4_op_putfh,
		.free_res = nfs4_op_putfh_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_PUTPUBFH] = {
		.name = "OP_PUTPUBFH",
		.funct = nfs4_op_putpubfh,
		.free_res = nfs4_op_putpubfh_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_PUTROOTFH] = {
		.name = "OP_PUTROOTFH",
		.funct = nfs4_op_putrootfh,
		.free_res = nfs4_op_putrootfh_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_READ] = {
		.name = "OP_READ",
		.funct = nfs4_op_read,
		.free_res = nfs4_op_read_Free,
		.exp_perm_flags = EXPORT_OPTION_READ_ACCESS},
	[NFS4_OP_READDIR] = {
		.name = "OP_READDIR",
		.funct = nfs4_op_readdir,
		.free_res = nfs4_op_readdir_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_READLINK] = {
		.name = "OP_READLINK",
		.funct = nfs4_op_readlink,
		.free_res = nfs4_op_readlink_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_REMOVE] = {
		.name = "OP_REMOVE",
		.funct = nfs4_op_remove,
		.free_res = nfs4_op_remove_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_RENAME] = {
		.name = "OP_RENAME",
		.funct = nfs4_op_rename,
		.free_res = nfs4_op_rename_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_RENEW] = {
		.name = "OP_RENEW",
		.funct = nfs4_op_renew,
		.free_res = nfs4_op_renew_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_RESTOREFH] = {
		.name = "OP_RESTOREFH",
		.funct = nfs4_op_restorefh,
		.free_res = nfs4_op_restorefh_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_SAVEFH] = {
		.name = "OP_SAVEFH",
		.funct = nfs4_op_savefh,
		.free_res = nfs4_op_savefh_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_SECINFO] = {
		.name = "OP_SECINFO",
		.funct = nfs4_op_secinfo,
		.free_res = nfs4_op_secinfo_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_SETATTR] = {
		.name = "OP_SETATTR",
		.funct = nfs4_op_setattr,
		.free_res = nfs4_op_setattr_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_WRITE_ACCESS},
	[NFS4_OP_SETCLIENTID] = {
		.name = "OP_SETCLIENTID",
		.funct = nfs4_op_setclientid,
		.free_res = nfs4_op_setclientid_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_SETCLIENTID_CONFIRM] = {
		.name = "OP_SETCLIENTID_CONFIRM",
		.funct = nfs4_op_setclientid_confirm,
		.free_res = nfs4_op_setclientid_confirm_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_VERIFY] = {
		.name = "OP_VERIFY",
		.funct = nfs4_op_verify,
		.free_res = nfs4_op_verify_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_WRITE] = {
		.name = "OP_WRITE",
		.funct = nfs4_op_write,
		.free_res = nfs4_op_write_Free,
		.exp_perm_flags = EXPORT_OPTION_WRITE_ACCESS},
	[NFS4_OP_RELEASE_LOCKOWNER] = {
		.name = "OP_RELEASE_LOCKOWNER",
		.funct = nfs4_op_release_lockowner,
		.free_res = nfs4_op_release_lockowner_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_BACKCHANNEL_CTL] = {
		.name = "OP_BACKCHANNEL_CTL",
		.funct = nfs4_op_illegal,
		.free_res = nfs4_op_illegal_Free,
		.exp_perm_flags = 0	/* tbd */},
	[NFS4_OP_BIND_CONN_TO_SESSION] = {
		.name = "OP_BIND_CONN_TO_SESSION",
		.funct = nfs4_op_illegal,
		.free_res = nfs4_op_illegal_Free,
		.exp_perm_flags = 0	/* tbd */},
	[NFS4_OP_EXCHANGE_ID] = {
		.name = "OP_EXCHANGE_ID",
		.funct = nfs4_op_exchange_id,
		.free_res = nfs4_op_exchange_id_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_CREATE_SESSION] = {
		.name = "OP_CREATE_SESSION",
		.funct = nfs4_op_create_session,
		.free_res = nfs4_op_create_session_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_DESTROY_SESSION] = {
		.name = "OP_DESTROY_SESSION",
		.funct = nfs4_op_destroy_session,
		.free_res = nfs4_op_reclaim_complete_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_FREE_STATEID] = {
		.name = "OP_FREE_STATEID",
		.funct = nfs4_op_free_stateid,
		.free_res = nfs4_op_free_stateid_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_GET_DIR_DELEGATION] = {
		.name = "OP_GET_DIR_DELEGATION",
		.funct = nfs4_op_illegal,
		.free_res = nfs4_op_illegal_Free,
		.exp_perm_flags = 0	/* tbd */},
	[NFS4_OP_GETDEVICEINFO] = {
		.name = "OP_GETDEVICEINFO",
		.funct = nfs4_op_getdeviceinfo,
		.free_res = nfs4_op_getdeviceinfo_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_GETDEVICELIST] = {
		.name = "OP_GETDEVICELIST",
		.funct = nfs4_op_getdevicelist,
		.free_res = nfs4_op_getdevicelist_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LAYOUTCOMMIT] = {
		.name = "OP_LAYOUTCOMMIT",
		.funct = nfs4_op_layoutcommit,
		.free_res = nfs4_op_reclaim_complete_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_LAYOUTGET] = {
		.name = "OP_LAYOUTGET",
		.funct = nfs4_op_layoutget,
		.free_res = nfs4_op_reclaim_complete_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_LAYOUTRETURN] = {
		.name = "OP_LAYOUTRETURN",
		.funct = nfs4_op_layoutreturn,
		.free_res = nfs4_op_reclaim_complete_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_SECINFO_NO_NAME] = {
		.name = "OP_SECINFO_NO_NAME",
		.funct = nfs4_op_secinfo_no_name,
		.free_res = nfs4_op_secinfo_no_name_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS},
	[NFS4_OP_SEQUENCE] = {
		.name = "OP_SEQUENCE",
		.funct = nfs4_op_sequence,
		.free_res = nfs4_op_sequence_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_SET_SSV] = {
		.name = "OP_SET_SSV",
		.funct = nfs4_op_set_ssv,
		.free_res = nfs4_op_reclaim_complete_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_TEST_STATEID] = {
		.name = "OP_TEST_STATEID",
		.funct = nfs4_op_test_stateid,
		.free_res = nfs4_op_test_stateid_Free,
		.exp_perm_flags = 0},
	[NFS4_OP_WANT_DELEGATION] = {
		.name = "OP_WANT_DELEGATION",
		.funct = nfs4_op_illegal,
		.free_res = nfs4_op_illegal_Free,
		.exp_perm_flags = EXPORT_OPTION_MD_READ_ACCESS	/* tbd */},
	[NFS4_OP_DESTROY_CLIENTID] = {
		.name = "OP_DESTROY_CLIENTID",
		.funct = nfs4_op_destroy_clientid,
		.free_res = nfs4_op_destroy_clientid_Free,
		.exp_perm_flags = 0	/* tbd */},
	[NFS4_OP_RECLAIM_COMPLETE] = {
		.name = "OP_RECLAIM_COMPLETE",
		.funct = nfs4_op_reclaim_complete,
		.free_res = nfs4_op_reclaim_complete_Free,
		.exp_perm_flags = 0},

	/* NFSv4.2 */
	[NFS4_OP_ALLOCATE] = {
				.name = "OP_ALLOCATE",
				.funct = nfs4_op_allocate,
				.free_res = nfs4_op_write_Free,
				.exp_perm_flags = 0},
	[NFS4_OP_COPY] = {
				.name = "OP_COPY",
				.funct = nfs4_op_notsupp,
				.free_res = nfs4_op_notsupp_Free,
				.exp_perm_flags = 0},
	[NFS4_OP_COPY_NOTIFY] = {
				.name = "OP_COPY_NOTIFY",
				.funct = nfs4_op_notsupp,
				.free_res = nfs4_op_notsupp_Free,
				.exp_perm_flags = 0},
	[NFS4_OP_DEALLOCATE] = {
				.name = "OP_DEALLOCATE",
				.funct = nfs4_op_deallocate,
				.free_res = nfs4_op_write_Free,
				.exp_perm_flags = 0},
	[NFS4_OP_IO_ADVISE] = {
				.name = "OP_IO_ADVISE",
				.funct = nfs4_op_io_advise,
				.free_res = nfs4_op_io_advise_Free,
				.exp_perm_flags = 0},
	[NFS4_OP_LAYOUTERROR] = {
				.name = "OP_LAYOUTERROR",
				.funct = nfs4_op_layouterror,
				.free_res = nfs4_op_layouterror_Free,
				.exp_perm_flags = 0},
	[NFS4_OP_LAYOUTSTATS] = {
				.name = "OP_LAYOUTSTATS",
				.funct = nfs4_op_layoutstats,
				.free_res = nfs4_op_layoutstats_Free,
				.exp_perm_flags = 0},
	[NFS4_OP_OFFLOAD_CANCEL] = {
				.name = "OP_OFFLOAD_CANCEL",
				.funct = nfs4_op_notsupp,
				.free_res = nfs4_op_notsupp_Free,
				.exp_perm_flags = 0},
	[NFS4_OP_OFFLOAD_STATUS] = {
				.name = "OP_OFFLOAD_STATUS",
				.funct = nfs4_op_notsupp,
				.free_res = nfs4_op_notsupp_Free,
				.exp_perm_flags = 0},
	[NFS4_OP_READ_PLUS] = {
				.name = "OP_READ_PLUS",
				.funct = nfs4_op_read_plus,
				.free_res = nfs4_op_read_plus_Free,
				.exp_perm_flags = 0},
	[NFS4_OP_SEEK] = {
				.name = "OP_SEEK",
				.funct = nfs4_op_seek,
				.free_res = nfs4_op_write_Free,
				.exp_perm_flags = 0},
	[NFS4_OP_WRITE_SAME] = {
				.name = "OP_WRITE_SAME",
				.funct = nfs4_op_write_plus,
				.free_res = nfs4_op_write_Free,
				.exp_perm_flags = 0},
};

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
 *  @param[in]  export     The full export list
 *  @param[in]  worker     Worker thread data
 *  @param[in]  req        NFSv4 request structure
 *  @param[out] res        NFSv4 reply structure
 *
 *  @see nfs4_op_<*> functions
 *  @see nfs4_GetPseudoFs
 *
 * @retval NFS_REQ_OKAY if a result is sent.
 * @retval NFS_REQ_DROP if we pretend we never saw the request.
 */

int nfs4_Compound(nfs_arg_t *arg,
		  nfs_worker_data_t *worker,
		  struct svc_req *req, nfs_res_t *res)
{
	unsigned int i = 0;
	int status = NFS4_OK;
	compound_data_t data;
	int opcode;
	const uint32_t compound4_minor = arg->arg_compound4.minorversion;
	const uint32_t argarray_len = arg->arg_compound4.argarray.argarray_len;
	nfs_argop4 * const argarray = arg->arg_compound4.argarray.argarray_val;
	nfs_resop4 *resarray;
	nsecs_elapsed_t op_start_time;
	struct timespec ts;
	int perm_flags;
	char *tagname = NULL;

	if (compound4_minor > 2) {
		LogCrit(COMPONENT_NFS_V4, "Bad Minor Version %d",
			compound4_minor);

		res->res_compound4.status = NFS4ERR_MINOR_VERS_MISMATCH;
		res->res_compound4.resarray.resarray_len = 0;
		return NFS_REQ_OK;
	}

	/* Keeping the same tag as in the arguments */
	res->res_compound4.tag.utf8string_len =
	    arg->arg_compound4.tag.utf8string_len;
	if (res->res_compound4.tag.utf8string_len > 0) {

		res->res_compound4.tag.utf8string_val =
		    gsh_malloc(res->res_compound4.tag.utf8string_len + 1);

		if (!res->res_compound4.tag.utf8string_val)
			return NFS_REQ_DROP;

		memcpy(res->res_compound4.tag.utf8string_val,
		       arg->arg_compound4.tag.utf8string_val,
		       res->res_compound4.tag.utf8string_len);

		res->res_compound4.tag.utf8string_val[res->res_compound4.tag.
						      utf8string_len] = '\0';

		/* Check if the tag is a valid utf8 string */
		status =
		    nfs4_utf8string2dynamic(&(res->res_compound4.tag),
					    UTF8_SCAN_ALL, &tagname);
		if (status != 0) {
			status = NFS4ERR_INVAL;
			res->res_compound4.status = status;
			res->res_compound4.resarray.resarray_len = 0;
			return NFS_REQ_OK;
		}
		gsh_free(tagname);

	} else {
		res->res_compound4.tag.utf8string_val = NULL;
	}

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
	data.worker = worker;
	data.req = req;

	/* Building the client credential field */
	if (nfs_rpc_req2client_cred(req, &(data.credential)) == -1)
		return NFS_REQ_DROP;	/* Malformed credential */

	/* Keeping the same tag as in the arguments */
	res->res_compound4.tag.utf8string_len =
	    arg->arg_compound4.tag.utf8string_len;

	/* Allocating the reply nfs_resop4 */
	res->res_compound4.resarray.resarray_val =
		gsh_calloc(argarray_len, sizeof(struct nfs_resop4));

	if (res->res_compound4.resarray.resarray_val == NULL)
		return NFS_REQ_DROP;

	res->res_compound4.resarray.resarray_len = argarray_len;
	resarray = res->res_compound4.resarray.resarray_val;

	/* Managing the operation list */
	LogDebug(COMPONENT_NFS_V4,
		 "COMPOUND: There are %d operations",
		 argarray_len);

	/* Manage errors NFS4ERR_OP_NOT_IN_SESSION and NFS4ERR_NOT_ONLY_OP.
	 * These checks apply only to 4.1 */
	if (compound4_minor > 0) {

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
			if (argarray[0].argop == NFS4_OP_EXCHANGE_ID
			    || argarray[0].argop == NFS4_OP_CREATE_SESSION
			    || argarray[0].argop == NFS4_OP_DESTROY_CLIENTID
			    || argarray[0].argop == NFS4_OP_DESTROY_SESSION) {
				status = NFS4ERR_NOT_ONLY_OP;
				res->res_compound4.status = status;
				res->res_compound4.resarray.resarray_len = 0;
				return NFS_REQ_OK;
			}
		}

		/* If the COMPOUND request starts with SEQUENCE, and if the
		 * sessionids specified in SEQUENCE and DESTROY_SESSION are the
		 * same, then DESTROY_SESSION MUST be the final operation in the
		 * COMPOUND request.
		 */
		if (argarray_len > 2 && argarray[0].argop == NFS4_OP_SEQUENCE
		    && argarray[1].argop == NFS4_OP_DESTROY_SESSION
		    && strncmp(argarray[0].nfs_argop4_u.opsequence.sa_sessionid,
			       argarray[1].nfs_argop4_u.opdestroy_session.
			       dsa_sessionid,
			       NFS4_SESSIONID_SIZE) == 0) {
			status = NFS4ERR_NOT_ONLY_OP;
			res->res_compound4.status = status;
			res->res_compound4.resarray.resarray_len = 0;
			return NFS_REQ_OK;
		}
	}

	for (i = 0; i < argarray_len; i++) {
		/* Used to check if OP_SEQUENCE is the first operation */
		data.oppos = i;

		/* time each op */
		now(&ts);
		op_start_time = timespec_diff(&ServerBootTime, &ts);
		opcode = argarray[i].argop;
		if (compound4_minor == 0) {
			if (opcode > NFS4_OP_RELEASE_LOCKOWNER)
				opcode = 0;
		} else {
			/* already range checked for minor version mismatch,
			 * must be 4.1
			 */
			if (data.session != NULL) {
				if (data.session->fore_channel_attrs.
				    ca_maxoperations == i) {
					status = NFS4ERR_TOO_MANY_OPS;
					goto bad_op_state;
				}
			}

			if (opcode >= NFS4_OP_LAST_ONE)
				opcode = 0;
		}
		LogDebug(COMPONENT_NFS_V4, "Request %d: opcode %d is %s", i,
			 argarray[i].argop, optabv4[opcode].name);
		perm_flags =
		    optabv4[opcode].exp_perm_flags & EXPORT_OPTION_ACCESS_TYPE;

		if (perm_flags != 0) {
			status = nfs4_Is_Fh_Empty(&data.currentFH);
			if (status != NFS4_OK) {
				LogDebug(COMPONENT_NFS_V4,
					 "Status of %s for CurrentFH in position %d = %s",
					 optabv4[opcode].name,
					 i,
					 nfsstat4_to_str(status));
				goto bad_op_state;
			}

			/* Operation uses a CurrentFH, so we can check export
			 * perms. Perms should even be set reasonably for pseudo
			 * file system.
			 */
			LogMidDebugAlt(COMPONENT_NFS_V4, COMPONENT_EXPORT,
				       "Check export perms export = %08x req = %08x",
				       op_ctx->export_perms->options &
						EXPORT_OPTION_ACCESS_TYPE,
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

				LogDebugAlt(COMPONENT_NFS_V4, COMPONENT_EXPORT,
					    "Status of %s due to export permissions in position %d = %s",
					    optabv4[opcode].name, i,
					    nfsstat4_to_str(status));
 bad_op_state:
				/* All the operation, like NFS4_OP_ACESS, have
				 * a first replied field called .status
				 */
				resarray[i].nfs_resop4_u.opaccess.status =
				    status;
				resarray[i].resop = argarray[i].argop;

				/* Do not manage the other requests in the
				 * COMPOUND.
				 */
				res->res_compound4.resarray.resarray_len =
					i + 1;
				break;
			}
		}

		status = (optabv4[opcode].funct) (&argarray[i],
						  &data,
						  &resarray[i]);

		LogCompoundFH(&data);

		/* All the operation, like NFS4_OP_ACESS, have a first replyied
		 * field called .status
		 */
		resarray[i].nfs_resop4_u.opaccess.status = status;

		server_stats_nfsv4_op_done(opcode,
					   op_start_time, status == NFS4_OK);

		if (status != NFS4_OK) {
			/* An error occured, we do not manage the other requests
			 * in the COMPOUND, this may be a regular behavior
			 */
			LogDebug(COMPONENT_NFS_V4,
				 "Status of %s in position %d = %s",
				 optabv4[opcode].name, i,
				 nfsstat4_to_str(status));

			res->res_compound4.resarray.resarray_len = i + 1;

			break;
		}

		/* Check Req size */

		/* NFS_V4.1 specific stuff */
		if (data.use_drc) {
			/* Replay cache, only true for SEQUENCE or
			 * CREATE_SESSION w/o SEQUENCE. Since will only be set
			 * in those cases, no need to check operation or
			 * anything.
			 */

			/* Free the reply allocated above */
			gsh_free(res->res_compound4.resarray.resarray_val);

			/* Copy the reply from the cache */
			res->res_compound4_extended = *data.cached_res;
			status = ((COMPOUND4res *) data.cached_res)->status;
			LogFullDebug(COMPONENT_SESSIONS,
				     "Use session replay cache %p result %s",
				     data.cached_res, nfsstat4_to_str(status));
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
	if (data.cached_res != NULL && !data.use_drc) {
		/* Pointer has been set by nfs4_op_sequence and points to slot
		 * to cache result in.
		 */
		LogFullDebug(COMPONENT_SESSIONS,
			     "Save result in session replay cache %p sizeof nfs_res_t=%d",
			     data.cached_res, (int)sizeof(nfs_res_t));

		/* Indicate to nfs4_Compound_Free that this reply is cached. */
		res->res_compound4_extended.res_cached = true;

		/* If the cache is already in use, free it. */
		if (data.cached_res->res_cached) {
			data.cached_res->res_cached = false;
			nfs4_Compound_Free((nfs_res_t *) data.cached_res);
		}

		/* Save the result in the cache. */
		*data.cached_res = res->res_compound4_extended;
	}

	/* If we have reserved a lease, update it and release it */
	if (data.preserved_clientid != NULL) {
		/* Update and release lease */
		pthread_mutex_lock(&data.preserved_clientid->cid_mutex);

		update_lease(data.preserved_clientid);

		pthread_mutex_unlock(&data.preserved_clientid->cid_mutex);
	}

	if (status != NFS4_OK)
		LogDebug(COMPONENT_NFS_V4, "End status = %s lastindex = %d",
			 nfsstat4_to_str(status), i);

	compound_data_Free(&data);

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
		     "nfs4_Compound_Free %p (resarraylen=%i)",
		     res,
		     res->res_compound4.resarray.resarray_len);

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

	if (res->res_compound4.tag.utf8string_val)
		gsh_free(res->res_compound4.tag.utf8string_val);

	return;
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
	if (data->current_entry)
		cache_inode_put(data->current_entry);

	if (data->saved_entry)
		cache_inode_put(data->saved_entry);

	if (data->current_ds) {
		ds_put(data->current_ds);
		data->current_ds = NULL;
	}

	if (data->saved_ds) {
		ds_put(data->saved_ds);
		data->saved_ds = NULL;
	}

	if (data->session) {
		dec_session_ref(data->session);
		data->session = NULL;
	}

	/* Release CurrentFH reference to export. */
	if (op_ctx->export) {
		put_gsh_export(op_ctx->export);
		op_ctx->export = NULL;
		op_ctx->fsal_export = NULL;
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
	case NFS4_OP_LAST_ONE:
		break;

	case NFS4_OP_ILLEGAL:
		break;
	}			/* switch */

	LogFatal(COMPONENT_NFS_V4,
		 "nfs4_Compound_CopyResOne not implemented for %d",
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
		     "nfs4_Compound_CopyRes of %p to %p (resarraylen : %i)",
		     res_src, res_dst,
		     res_src->res_compound4.resarray.resarray_len);

	for (i = 0; i < res_src->res_compound4.resarray.resarray_len; i++)
		nfs4_Compound_CopyResOne(
			&res_dst->res_compound4.resarray.resarray_val[i],
			&res_src->res_compound4.resarray.resarray_val[i]);
}

/* @} */

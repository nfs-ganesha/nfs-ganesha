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
 * --------------------------------------- */
#include "config.h"
#include "log.h"
#include "gsh_rpc.h"
#include "fsal.h"
#include "nfs_core.h"
#include "nfs_exports.h"
#include "nfs_creds.h"
#include "nfs_proto_functions.h"
#include "nfs_proto_tools.h"
#include "nfs_file_handle.h"
#include "nfs_convert.h"
#include "export_mgr.h"

/**
 * @brief Opaque bookkeeping structure for NFSv4 readdir
 *
 * This structure keeps track of the process of writing out an NFSv4
 * READDIR response between calls to nfs4_readdir_callback.
 */
struct nfs4_readdir_cb_data {
	XDR xdr;	/*< The xdrmem to serialize the entries */
	char *entries;	/*< The array holding individual entries */
	size_t mem_avail;	/*< The amount of memory remaining before we
				   hit maxcount */
	bool has_entries;	/*< Track if at least one entry fit  */
	nfsstat4 error;		/*< Set to a value other than NFS4_OK if the
				   callback function finds a fatal error. */
	struct bitmap4 *req_attr;	/*< The requested attributes */
	compound_data_t *data;	/*< The compound data, so we can produce
				   nfs_fh4s. */
	struct saved_export_context saved;
};

static void restore_data(struct nfs4_readdir_cb_data *tracker)
{
	if (tracker->saved.saved_export == NULL) {
		LogCrit(COMPONENT_NFS_READDIR, "Nothing to restore!");
		return;
	}

	/* Restore export stuff */
	restore_op_context_export(&tracker->saved);

	/* Restore creds */
	if (nfs_req_creds(tracker->data->req) != NFS4_OK) {
		LogCrit(COMPONENT_EXPORT,
			"Failure to restore creds");
	}
}

/* The base size of a readdir entry includes cookie, name length, and the
 * indicator if a next entry follows.
 */
#define BASE_ENTRY_SIZE (sizeof(nfs_cookie4) + 2 * sizeof(uint32_t))

/**
 * @brief Populate entry4s when called from fsal_readdir
 *
 * This function is a callback passed to fsal_readdir.  It
 * fills in a pre-allocated array of entry4 structures and allocates
 * space for the name and attributes.  This space must be freed.
 *
 * @param[in,out] opaque A struct nfs4_readdir_cb_data that stores the
 *                       location of the array and other bookeeping
 *                       information
 * @param[in]     obj	 Current file
 * @param[in]     attrs  The current file's attributes
 * @param[in]     cookie The readdir cookie for the current entry
 */

fsal_errors_t nfs4_readdir_callback(void *opaque,
				    struct fsal_obj_handle *obj,
				    const struct fsal_attrlist *attr,
				    uint64_t mounted_on_fileid,
				    uint64_t cookie,
				    enum cb_state cb_state)
{
	struct fsal_readdir_cb_parms *cb_parms = opaque;
	struct nfs4_readdir_cb_data *tracker = cb_parms->opaque;
	char val_fh[NFS4_FHSIZE];
	nfs_fh4 entryFH = {
		.nfs_fh4_len = 0,
		.nfs_fh4_val = val_fh
	};
	struct xdr_attrs_args args;
	compound_data_t *data = tracker->data;
	fsal_status_t fsal_status;
	fsal_accessflags_t access_mask_attr = 0;
	u_int pos_start = xdr_getpos(&tracker->xdr);
	u_int mem_left = tracker->mem_avail - pos_start;
	component4 name;
	bool_t res_false = false;
	bool_t lock_dir = false;

	LogFullDebug(COMPONENT_NFS_READDIR,
		     "Entry %s pos %d mem_left %d",
		     cb_parms->name, (int) pos_start, (int) mem_left);

	memset(&args, 0, sizeof(args));

	/* Cleanup after problem with junction processing. */
	if (cb_state == CB_PROBLEM) {
		/* Restore the export. */
		LogDebug(COMPONENT_NFS_READDIR,
			 "Cleanup after problem with junction processing.");
		restore_data(tracker);
		return ERR_FSAL_NO_ERROR;
	}

	/* Test if this is a junction.
	 *
	 * NOTE: If there is a junction within a file system (perhaps setting
	 *       up different permissions for part of the file system), the
	 *       junction inode will ALSO be the root of the nested export.
	 *       By testing cb_state, we allow the call back to process
	 *       that root inode to proceed rather than getting stuck in a
	 *       junction crossing infinite loop.
	 */
	if (obj->type == DIRECTORY && cb_parms->attr_allowed &&
	    cb_state == CB_ORIGINAL) {
		lock_dir = true;
		PTHREAD_RWLOCK_rdlock(&obj->state_hdl->jct_lock);

		if (obj->state_hdl->dir.junction_export == NULL)
			goto not_junction;

		/* This is a junction. Code used to not recognize this
		 * which resulted in readdir giving different attributes
		 * (including FH, FSid, etc...) to clients from a
		 * lookup. AIX refused to list the directory because of
		 * this. Now we go to the junction to get the
		 * attributes.
		 */
		LogDebugAlt(COMPONENT_EXPORT, COMPONENT_NFS_READDIR,
			    "Offspring DIR %s is a junction Export_id %d Pseudo %s",
			    cb_parms->name,
			    obj->state_hdl->dir.junction_export->export_id,
			    JCT_PSEUDOPATH(obj->state_hdl));

		/* Get a reference to the export and stash it in
		 * compound data.
		 */
		if (!export_ready(obj->state_hdl->dir.junction_export)) {
			/* Export is in the process of being released.
			 * Pretend it's not actually a junction.
			 */
			goto not_junction;
		}

		get_gsh_export_ref(obj->state_hdl->dir.junction_export);

		/* Save the compound data context and cross the junction */
		save_op_context_export_and_set_export(
			&tracker->saved, obj->state_hdl->dir.junction_export);

		/* Build the credentials */
		args.rdattr_error = nfs4_export_check_access(data->req);

		if (args.rdattr_error == NFS4ERR_ACCESS) {
			/* If return is NFS4ERR_ACCESS then this client
			 * doesn't have access to this export, quietly
			 * skip the export.
			 */
			LogDebugAlt(COMPONENT_EXPORT, COMPONENT_NFS_READDIR,
				    "NFS4ERR_ACCESS Skipping Export_Id %d Pseudo %s",
				    op_ctx->ctx_export->export_id,
				    CTX_PSEUDOPATH(op_ctx));

			/* Restore export and creds */
			restore_data(tracker);

			/* Indicate success without adding another entry */
			cb_parms->in_result = true;
			PTHREAD_RWLOCK_unlock(&obj->state_hdl->jct_lock);
			return ERR_FSAL_NO_ERROR;
		}

		if (args.rdattr_error == NFS4ERR_WRONGSEC) {
			/* Client isn't using the right SecType for this export,
			 * we will report NFS4ERR_WRONGSEC in
			 * FATTR4_RDATTR_ERROR.
			 *
			 * If the ONLY attributes requested are
			 * FATTR4_RDATTR_ERROR and FATTR4_MOUNTED_ON_FILEID we
			 * will not return an error and instead will return
			 * success with FATTR4_MOUNTED_ON_FILEID. AIX clients
			 * make this request and expect it to succeed.
			 */

			if (check_for_wrongsec_ok_attr(tracker->req_attr)) {
				/* Client is requesting attr that are allowed
				 * when NFS4ERR_WRONGSEC occurs.
				 */
				LogDebugAlt(COMPONENT_EXPORT,
					    COMPONENT_NFS_READDIR,
					    "Ignoring NFS4ERR_WRONGSEC (only asked for MOUNTED_IN_FILEID) On ReadDir Export_Id %d Path %s",
					    op_ctx->ctx_export->export_id,
					    CTX_PSEUDOPATH(op_ctx));

				/* Because we are not asking for any attributes
				 * which are a property of the exported file
				 * system's root, really just asking for
				 * MOUNTED_ON_FILEID, we can just get the attr
				 * for this node since it will result in the
				 * correct value for MOUNTED_ON_FILEID since
				 * the fileid of the junction node is the
				 * MOUNTED_ON_FILEID of the root across the
				 * junction, and the mounted_on_filed passed
				 * is the fileid of the junction (since the
				 * node can't be the root of the current
				 * export).
				 *
				 * Go ahead and proceed without an error.
				 */
				args.rdattr_error = NFS4_OK;
			} else {
				/* We really must report the NFS4ERR_WRONGSEC.
				 * We will report it below, but we need to get
				 * the name into the entry.
				 */
				LogDebugAlt(COMPONENT_EXPORT,
					    COMPONENT_NFS_READDIR,
					    "NFS4ERR_WRONGSEC On ReadDir Export_Id %d Pseudo %s",
					    op_ctx->ctx_export->export_id,
					    CTX_PSEUDOPATH(op_ctx));
			}
		} else if (args.rdattr_error == NFS4_OK) {
			/* Now we must traverse the junction to get the
			 * attributes. We have already set up the compound data.
			 *
			 * Signal to populate_dirent to call back with the
			 * root node of the export across the junction. Also
			 * signal to ourselves that the call back will be
			 * across the junction.
			 */
			LogDebugAlt(COMPONENT_EXPORT, COMPONENT_NFS_READDIR,
				    "Need to cross junction to Export_Id %d Pseudo %s",
				    op_ctx->ctx_export->export_id,
				    CTX_PSEUDOPATH(op_ctx));
			PTHREAD_RWLOCK_unlock(&obj->state_hdl->jct_lock);
			return ERR_FSAL_CROSS_JUNCTION;
		}

		/* An error occurred and we will report it, but we need to get
		 * the name into the entry to proceed.
		 *
		 * Restore export and creds.
		 */
		LogDebugAlt(COMPONENT_EXPORT, COMPONENT_NFS_READDIR,
			    "Need to report error for junction to Export_Id %d Pseudo %s",
			    op_ctx->ctx_export->export_id,
			    CTX_PSEUDOPATH(op_ctx));
		restore_data(tracker);
	}

not_junction:
	if (lock_dir)
		PTHREAD_RWLOCK_unlock(&obj->state_hdl->jct_lock);

	args.attrs = (struct fsal_attrlist *)attr;
	args.data = data;
	args.hdl4 = &entryFH;
	args.mounted_on_fileid = mounted_on_fileid;
	args.fileid = obj->fileid;
	args.fsid = obj->fsid;

	/* Now process the entry */
	memset(val_fh, 0, NFS4_FHSIZE);

	/* Bits that don't require allocation */
	if (mem_left < BASE_ENTRY_SIZE) {
		if (!tracker->has_entries) {
			tracker->error = NFS4ERR_TOOSMALL;
		}
		LogDebug(COMPONENT_NFS_READDIR,
			 "Skipping because too small for BASE_ENTRY_SIZE %d",
			 (int) BASE_ENTRY_SIZE);
		goto failure;
	}

	mem_left -= BASE_ENTRY_SIZE;

	/* The filename.  We don't use str2utf8 because that has an
	 * additional copy into a buffer before copying into the
	 * destination.
	 */
	name.utf8string_len = strlen(cb_parms->name);
	name.utf8string_val = (char *) cb_parms->name;

	if (mem_left < RNDUP(name.utf8string_len)) {
		if (!tracker->has_entries) {
			tracker->error = NFS4ERR_TOOSMALL;
		}
		LogDebug(COMPONENT_NFS_READDIR,
			 "Skipping because of name %s too long %d",
			 (char *) cb_parms->name, (int) name.utf8string_len);
		goto failure;
	}


	/* If we carried an error from above, now that we have
	 * the name set up, go ahead and try and put error in
	 * results.
	 */
	if (args.rdattr_error != NFS4_OK) {
		LogDebug(COMPONENT_NFS_READDIR,
			 "Skipping because of %s",
			 nfsstat4_to_str(args.rdattr_error));
		goto skip;
	}

	if (cb_parms->attr_allowed &&
	    attribute_is_set(tracker->req_attr, FATTR4_FILEHANDLE) &&
	    !nfs4_FSALToFhandle(false, &entryFH, obj, op_ctx->ctx_export)) {
		LogDebug(COMPONENT_NFS_READDIR,
			 "Skipping because of problem with handle");
		goto server_fault;
	}

	if (!cb_parms->attr_allowed) {
		/* fsal_readdir is signaling us that client didn't have
		 * search permission in this directory, so we can't return any
		 * attributes, but must indicate NFS4ERR_ACCESS.
		 */
		args.rdattr_error = NFS4ERR_ACCESS;
		LogDebug(COMPONENT_NFS_READDIR,
			 "Skipping because of %s",
			 nfsstat4_to_str(args.rdattr_error));
		goto skip;
	}

	/* Adjust access mask if ACL is asked for.
	 * NOTE: We intentionally do NOT check ACE4_READ_ATTR.
	 */
	if (attribute_is_set(tracker->req_attr, FATTR4_ACL))
		access_mask_attr |= FSAL_ACE4_MASK_SET(FSAL_ACE_PERM_READ_ACL);

	/* Attrs were refreshed before call */
	fsal_status = obj->obj_ops->test_access(obj, access_mask_attr, NULL,
					       NULL, false);
	if (FSAL_IS_ERROR(fsal_status)) {
		LogDebug(COMPONENT_NFS_READDIR,
			 "permission check for attributes status=%s",
			 msg_fsal_err(fsal_status.major));

		args.rdattr_error = nfs4_Errno_status(fsal_status);
		LogDebug(COMPONENT_NFS_READDIR,
			 "Skipping because of %s",
			 nfsstat4_to_str(args.rdattr_error));
		goto skip;
	}

	/* Tell is_referral to not cache attrs as it will affect readdir
	 * performance
	 */
	if (obj->obj_ops->is_referral(obj, (struct fsal_attrlist *) attr,
				      false)) {
		args.rdattr_error = NFS4ERR_MOVED;
		LogDebug(COMPONENT_NFS_READDIR,
			 "Skipping because of %s",
			 nfsstat4_to_str(args.rdattr_error));
		goto skip;
	}

	if (!xdr_encode_entry4(&tracker->xdr, &args, tracker->req_attr,
			       cookie, &name) ||
	    (xdr_getpos(&tracker->xdr) + BYTES_PER_XDR_UNIT)
	    >= tracker->mem_avail) {
		/* We had an overflow */
		LogFullDebug(COMPONENT_NFS_READDIR,
			     "Overflow of buffer after xdr_encode_entry4 - pos = %d",
			     xdr_getpos(&tracker->xdr));
		goto failure;
	}

 skip:

	if (args.rdattr_error != NFS4_OK) {
		if (!attribute_is_set(tracker->req_attr,
			FATTR4_RDATTR_ERROR) &&
		    !attribute_is_set(tracker->req_attr,
			FATTR4_FS_LOCATIONS)) {

			tracker->error = args.rdattr_error;
			LogDebug(COMPONENT_NFS_READDIR,
				 "Skipping because of %s and didn't ask for FATTR4_RDATTR_ERROR or FATTR4_FS_LOCATIONS",
				 nfsstat4_to_str(args.rdattr_error));
			goto failure;
		}

		if (!xdr_nfs4_fattr_fill_error(&tracker->xdr,
					       tracker->req_attr,
					       cookie, &name, &args) ||
		    (xdr_getpos(&tracker->xdr) + BYTES_PER_XDR_UNIT)
		    >= tracker->mem_avail) {
			/* We had an overflow */
			LogFullDebug(COMPONENT_NFS_READDIR,
				     "Overflow of buffer after xdr_nfs4_fattr_fill_error - pos = %d",
				     xdr_getpos(&tracker->xdr));
			goto failure;
		}
	}

	tracker->has_entries = true;
	cb_parms->in_result = true;
	goto out;

 server_fault:

	tracker->error = NFS4ERR_SERVERFAULT;

 failure:

	/* Reset to where we started this entry and encode a boolean
	 * false instead (entry_follows is false).
	 */
	if (!xdr_setpos(&tracker->xdr, pos_start) ||
	    !xdr_bool(&tracker->xdr, &res_false)) {
		/* Oops, what broke... */
		LogCrit(COMPONENT_NFS_READDIR,
			"Unexpected XDR failure processing readdir result");
		tracker->error = NFS4ERR_SERVERFAULT;
	}

	cb_parms->in_result = false;

 out:

	return ERR_FSAL_NO_ERROR;
}

void xdr_dirlist4_uio_release(struct xdr_uio *uio, u_int flags)
{
	int ix;

	LogFullDebug(COMPONENT_NFS_V4,
		     "Releasing %p, references %"PRIi32", count %d",
		     uio, uio->uio_references, (int) uio->uio_count);

	if (!(--uio->uio_references)) {
		for (ix = 0; ix < uio->uio_count; ix++) {
			gsh_free(uio->uio_vio[ix].vio_base);
		}
		gsh_free(uio);
	}
}

/* Base response size includes nfsstat4, eof, verifier and termination of
 * entries list.
 */
#define READDIR_RESP_BASE_SIZE (sizeof(nfsstat4) + sizeof(verifier4) + \
				2 * BYTES_PER_XDR_UNIT)

/**
 * @brief NFS4_OP_READDIR
 *
 * Implements the NFS4_OP_READDIR opeartion. If fh is a pseudo FH,
 * then call is routed to routine nfs4_op_readdir_pseudo
 *
 * @param[in]     op   Arguments for nfs4_op
 * @param[in,out] data Compound request's data
 * @param[out]    resp Results for nfs4_op
 *
 * @return per RFC5661, pp. 371-2
 *
 */
enum nfs_req_result nfs4_op_readdir(struct nfs_argop4 *op,
				    compound_data_t *data,
				    struct nfs_resop4 *resp)
{
	READDIR4args * const arg_READDIR4 = &op->nfs_argop4_u.opreaddir;
	READDIR4res *res_READDIR4 = &resp->nfs_resop4_u.opreaddir;
	READDIR4resok *resok = &res_READDIR4->READDIR4res_u.resok4;
	struct fsal_obj_handle *dir_obj = NULL;
	bool eod_met = false;
	unsigned long dircount = 0;
	unsigned long maxcount = 0;
	verifier4 cookie_verifier;
	uint64_t cookie = 0;
	unsigned int num_entries = 0;
	struct nfs4_readdir_cb_data tracker;
	fsal_status_t fsal_status = {0, 0};
	attrmask_t attrmask;
	bool use_cookie_verifier = op_ctx_export_has_option(
					EXPORT_OPTION_USE_COOKIE_VERIFIER);

	resp->resop = NFS4_OP_READDIR;
	res_READDIR4->status = NFS4_OK;

	res_READDIR4->status = nfs4_sanity_check_FH(data, DIRECTORY, false);

	memset(&tracker, 0, sizeof(tracker));

	if (res_READDIR4->status != NFS4_OK)
		goto out;

	dir_obj = data->current_obj;

	/* get the characteristic value for readdir operation */
	dircount = arg_READDIR4->dircount;
	cookie = arg_READDIR4->cookie;

	/* Dont over flow V4.1 maxresponsesize or maxcachedsize */
	maxcount = resp_room(data);
	if (nfs_param.core_param.readdir_res_size != 0 &&
		nfs_param.core_param.readdir_res_size < maxcount) {
		maxcount = nfs_param.core_param.readdir_res_size;
	}

	if (maxcount > (arg_READDIR4->maxcount + sizeof(nfsstat4)))
		maxcount = arg_READDIR4->maxcount + sizeof(nfsstat4);

	/* Dircount is considered meaningless by many nfsv4 client (like the
	 * CITI one).  we use maxcount instead.
	 */

	LogDebug(COMPONENT_NFS_READDIR,
		 "dircount=%lu maxcount=%lu cookie=%" PRIu64,
		 dircount, maxcount, cookie);

	/* Since we never send a cookie of 1 or 2, we shouldn't ever get
	 * them back.
	 */
	if (cookie == 1 || cookie == 2) {
		res_READDIR4->status = NFS4ERR_BAD_COOKIE;
		LogDebug(COMPONENT_NFS_READDIR, "Bad cookie");
		goto out;
	}

	/* Get only attributes that are allowed to be read */
	if (!nfs4_Fattr_Check_Access_Bitmap
	    (&arg_READDIR4->attr_request, FATTR4_ATTR_READ)) {
		res_READDIR4->status = NFS4ERR_INVAL;
		LogDebug(COMPONENT_NFS_READDIR, "Requested invalid attributes");
		goto out;
	}

	/* If maxcount is way too small return NFS4ERR_TOOSMALL */
	if (maxcount < READDIR_RESP_BASE_SIZE) {
		res_READDIR4->status = NFS4ERR_TOOSMALL;
		LogInfo(COMPONENT_NFS_READDIR,
			"Response too small maxcount = %lu need at least %llu",
			maxcount, (unsigned long long) READDIR_RESP_BASE_SIZE);
		goto out;
	}

	/* To make or check the cookie verifier */
	memset(cookie_verifier, 0, sizeof(cookie_verifier));

	/* If cookie verifier is used, then an non-trivial value is
	 * returned to the client This value is the change attribute of the
	 * directory. If verifier is unused (as in many NFS Servers) then
	 * only a set of zeros is returned (trivial value)
	 */
	if (use_cookie_verifier) {
		struct fsal_attrlist attrs;

		fsal_prepare_attrs(&attrs, ATTR_CHANGE);

		fsal_status = data->current_obj->obj_ops->getattrs(
						data->current_obj, &attrs);

		if (FSAL_IS_ERROR(fsal_status)) {
			res_READDIR4->status = nfs4_Errno_status(fsal_status);
			LogDebug(COMPONENT_NFS_READDIR,
				 "getattrs returned %s",
				 msg_fsal_err(fsal_status.major));
			goto out;
		}

		memcpy(cookie_verifier, &attrs.change,
		       MIN(sizeof(cookie_verifier), sizeof(attrs.change)));

		/* Done with the attrs */
		fsal_release_attrs(&attrs);
	}

	/* Cookie delivered by the server and used by the client SHOULD
	 * not be 0, 1 or 2 because these values are reserved (see RFC
	 * 3530, p. 192/RFC 5661, p468).
	 *
	 * 0 - cookie for first READDIR
	 * 1 - reserved for . on client
	 * 2 - reserved for .. on client
	 *
	 * '.' and '..' are not returned, so all cookies will be offset by 2
	 */

	if (cookie != 0 && use_cookie_verifier) {
		if (memcmp(cookie_verifier,
			   arg_READDIR4->cookieverf,
			   NFS4_VERIFIER_SIZE) != 0) {
			res_READDIR4->status = NFS4ERR_BAD_COOKIE;
			LogDebug(COMPONENT_NFS_READDIR, "Bad cookie");
			goto out;
		}
	}

	/* Prepare to read the entries */
	tracker.mem_avail = maxcount - READDIR_RESP_BASE_SIZE;
	tracker.entries = gsh_malloc(tracker.mem_avail);
	tracker.error = NFS4_OK;
	tracker.req_attr = &arg_READDIR4->attr_request;
	tracker.data = data;

	xdrmem_create(&tracker.xdr, tracker.entries, tracker.mem_avail,
		      XDR_ENCODE);

	/* Assume we need at least the NFS v3 attr.
	 * Any attr is sufficient for permission checking.
	 */
	attrmask = ATTRS_NFS3;

	/* If ACL is requested, we need to add that for permission checking. */
	if (attribute_is_set(tracker.req_attr, FATTR4_ACL))
		attrmask |= ATTR_ACL;

	/* If seclabel requested, then fetch it too */
	if (attribute_is_set(tracker.req_attr, FATTR4_SEC_LABEL) &&
	    op_ctx_export_has_option(EXPORT_OPTION_SECLABEL_SET))
		attrmask |= ATTR4_SEC_LABEL;

	/* Perform the readdir operation */
	fsal_status = fsal_readdir(dir_obj,
				   cookie,
				   &num_entries,
				   &eod_met,
				   attrmask,
				   nfs4_readdir_callback,
				   &tracker);

	if (FSAL_IS_ERROR(fsal_status)) {
		res_READDIR4->status = nfs4_Errno_status(fsal_status);
		LogDebug(COMPONENT_NFS_READDIR,
			 "fsal_readdir returned %s",
			 msg_fsal_err(fsal_status.major));
		goto out_destroy;
	}

	LogDebug(COMPONENT_NFS_READDIR,
		 "fsal_readdir returned %s",
		 msg_fsal_err(fsal_status.major));

	res_READDIR4->status = tracker.error;

	if (res_READDIR4->status != NFS4_OK) {
		LogDebug(COMPONENT_NFS_READDIR, "Tracker error");
		goto out_destroy;
	}

	/* Set the op response size to be accounted for, maxcount was the
	 * maximum space that mem_avail started from before we deducted
	 * READDIR_RESP_BASE_SIZE and the space used for each entry.
	 *
	 * So the formula below is equivalent to:
	 *
	 *     maxcount - maxcount + READDIR_RESP_BASE_SIZE + space for entries
	 *
	 * So maxcount disappears and all that is left is the actual size.
	 */
	data->op_resp_size = maxcount - tracker.mem_avail;

	if (tracker.has_entries) {
		struct xdr_uio *uio;
		u_int pos_end;
		bool_t tmp;

		if (eod_met) {
			/* If we hit end of directory, then the dirlist4 we have
			 * encoded so far has a complete entry (we MUST have
			 * consumed the last entry and encoded it). Now we need
			 * to encode a FALSE to indicate no next entry.
			 */
			tmp = FALSE;
			if (!xdr_bool(&tracker.xdr, &tmp)) {
				/* Oops... */
				LogCrit(COMPONENT_NFS_READDIR,
					"Encode of no next entry failed.");
				res_READDIR4->status = NFS4ERR_SERVERFAULT;
				goto out_destroy;
			}
		}

		/* Serialize eod_met into the entries buffer */
		tmp = eod_met;
		if (!xdr_bool(&tracker.xdr, &tmp)) {
			/* Oops... */
			LogCrit(COMPONENT_NFS_READDIR,
				"Encode of EOD failed.");
			res_READDIR4->status = NFS4ERR_SERVERFAULT;
			goto out_destroy;
		}

		pos_end = xdr_getpos(&tracker.xdr);

		/* Get an xdr_uio and fill it in */
		uio = gsh_calloc(
			1, sizeof(struct xdr_uio) + sizeof(struct xdr_uio));
		uio->uio_release = xdr_dirlist4_uio_release;
		uio->uio_count = 1;
		uio->uio_vio[0].vio_base = tracker.entries;
		uio->uio_vio[0].vio_head = tracker.entries;
		uio->uio_vio[0].vio_tail = tracker.entries + pos_end;
		uio->uio_vio[0].vio_wrap = tracker.entries + pos_end;
		uio->uio_vio[0].vio_length = pos_end;
		uio->uio_vio[0].vio_type = VIO_DATA;

		/* Take over entries buffer */
		tracker.entries = NULL;
		resok->reply.uio = uio;
	} else {
		/* Directory was empty - need to register eof */
		/* This slight bit of oddness is caused by most booleans
		 * throughout Ganesha being of C99's bool type (taking the
		 * values true and false), but fields in XDR being of the older
		 * bool_t type i(taking the values TRUE and FALSE)
		 */
		if (eod_met)
			res_READDIR4->READDIR4res_u.resok4.reply.eof = TRUE;
		else
			res_READDIR4->READDIR4res_u.resok4.reply.eof = FALSE;
	}

	/* Do not forget to set the verifier */
	memcpy(resok->cookieverf, cookie_verifier, NFS4_VERIFIER_SIZE);

	/* We never use reply.entries */
	resok->reply.entries = NULL;

	res_READDIR4->status = NFS4_OK;

 out_destroy:

	xdr_destroy(&tracker.xdr);

 out:

	/* If we allocated but didn't consume entries, free it now. */
	gsh_free(tracker.entries);

	LogDebug(COMPONENT_NFS_READDIR,
		 "Returning %s",
		 nfsstat4_to_str(res_READDIR4->status));

	return nfsstat4_to_nfs_req_result(res_READDIR4->status);
}				/* nfs4_op_readdir */

/**
 * @brief Free memory allocated for READDIR result
 *
 * This function frees any memory allocated for the results of the
 * NFS4_OP_READDIR operation.
 *
 * @param[in,out] resp nfs4_op results
 */
void nfs4_op_readdir_Free(nfs_resop4 *res)
{
	/* Nothing to do, entries has been handed off or already freed. */
}

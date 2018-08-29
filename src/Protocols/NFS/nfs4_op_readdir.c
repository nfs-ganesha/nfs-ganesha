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
	entry4 *entries;	/*< The array holding individual entries */
	size_t mem_left;	/*< The amount of memory remaining before we
				   hit maxcount */
	size_t count;		/*< The count of complete entries stored in the
				   buffer */
	size_t total_entries;	/*< The total number of entries available
				   in the array */
	nfsstat4 error;		/*< Set to a value other than NFS4_OK if the
				   callback function finds a fatal error. */
	struct bitmap4 *req_attr;	/*< The requested attributes */
	compound_data_t *data;	/*< The compound data, so we can produce
				   nfs_fh4s. */
	struct export_perms save_export_perms;	/*< Saved export perms. */
	struct gsh_export *saved_gsh_export;	/*< Saved export */
};

static void restore_data(struct nfs4_readdir_cb_data *tracker)
{
	/* Restore export stuff */
	if (op_ctx->ctx_export)
		put_gsh_export(op_ctx->ctx_export);

	*op_ctx->export_perms = tracker->save_export_perms;
	op_ctx->ctx_export = tracker->saved_gsh_export;
	op_ctx->fsal_export = op_ctx->ctx_export->fsal_export;
	tracker->saved_gsh_export = NULL;

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
				    const struct attrlist *attr,
				    uint64_t mounted_on_fileid,
				    uint64_t cookie,
				    enum cb_state cb_state)
{
	struct fsal_readdir_cb_parms *cb_parms = opaque;
	struct nfs4_readdir_cb_data *tracker = cb_parms->opaque;
	size_t namelen = 0;
	char val_fh[NFS4_FHSIZE];
	nfs_fh4 entryFH = {
		.nfs_fh4_len = 0,
		.nfs_fh4_val = val_fh
	};
	struct xdr_attrs_args args;
	compound_data_t *data = tracker->data;
	nfsstat4 rdattr_error = NFS4_OK;
	entry4 *tracker_entry = tracker->entries + tracker->count;
	fsal_status_t fsal_status;
	fsal_accessflags_t access_mask_attr = 0;
	size_t initial_mem_left = tracker->mem_left;

	/* Cleanup after problem with junction processing. */
	if (cb_state == CB_PROBLEM) {
		/* Restore the export. */
		restore_data(tracker);
		return ERR_FSAL_NO_ERROR;
	}

	if (tracker->total_entries == tracker->count)
		goto not_inresult;

	/* Test if this is a junction.
	 *
	 * NOTE: If there is a junction within a file system (perhaps setting
	 *       up different permissions for part of the file system), the
	 *       junction inode will ALSO be the root of the nested export.
	 *       By testing cb_state, we allow the call back to process
	 *       that root inode to proceed rather than getting stuck in a
	 *       junction crossing infinite loop.
	 */
	PTHREAD_RWLOCK_rdlock(&obj->state_hdl->state_lock);
	if (cb_parms->attr_allowed &&
	    obj->type == DIRECTORY &&
	    obj->state_hdl->dir.junction_export != NULL &&
	    cb_state == CB_ORIGINAL) {
		/* This is a junction. Code used to not recognize this
		 * which resulted in readdir giving different attributes
		 * (including FH, FSid, etc...) to clients from a
		 * lookup. AIX refused to list the directory because of
		 * this. Now we go to the junction to get the
		 * attributes.
		 */
		LogDebug(COMPONENT_EXPORT,
			 "Offspring DIR %s is a junction Export_id %d Pseudo %s",
			 cb_parms->name,
			 obj->state_hdl->dir.junction_export->export_id,
			 obj->state_hdl->dir.junction_export->pseudopath);

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

		/* Save the compound data context */
		tracker->save_export_perms = *op_ctx->export_perms;
		tracker->saved_gsh_export = op_ctx->ctx_export;

		/* Cross the junction */
		op_ctx->ctx_export = obj->state_hdl->dir.junction_export;
		op_ctx->fsal_export = op_ctx->ctx_export->fsal_export;

		/* Build the credentials */
		rdattr_error = nfs4_export_check_access(data->req);

		if (rdattr_error == NFS4ERR_ACCESS) {
			/* If return is NFS4ERR_ACCESS then this client
			 * doesn't have access to this export, quietly
			 * skip the export.
			 */
			LogDebug(COMPONENT_EXPORT,
				 "NFS4ERR_ACCESS Skipping Export_Id %d Pseudo %s",
				 op_ctx->ctx_export->export_id,
				 op_ctx->ctx_export->pseudopath);

			/* Restore export and creds */
			restore_data(tracker);

			/* Indicate success without adding another entry */
			cb_parms->in_result = true;
			PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);
			return ERR_FSAL_NO_ERROR;
		}

		if (rdattr_error == NFS4ERR_WRONGSEC) {
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
				LogDebug(COMPONENT_EXPORT,
					 "Ignoring NFS4ERR_WRONGSEC (only asked for MOUNTED_IN_FILEID) On ReadDir Export_Id %d Path %s",
					 op_ctx->ctx_export->export_id,
					 op_ctx->ctx_export->pseudopath);

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
				rdattr_error = NFS4_OK;
			} else {
				/* We really must report the NFS4ERR_WRONGSEC.
				 * We will report it below, but we need to get
				 * the name into the entry.
				 */
				LogDebug(COMPONENT_EXPORT,
					 "NFS4ERR_WRONGSEC On ReadDir Export_Id %d Pseudo %s",
					 op_ctx->ctx_export->export_id,
					 op_ctx->ctx_export->pseudopath);
			}
		} else if (rdattr_error == NFS4_OK) {
			/* Now we must traverse the junction to get the
			 * attributes. We have already set up the compound data.
			 *
			 * Signal to populate_dirent to call back with the
			 * root node of the export across the junction. Also
			 * signal to ourselves that the call back will be
			 * across the junction.
			 */
			LogDebug(COMPONENT_EXPORT,
				 "Need to cross junction to Export_Id %d Pseudo %s",
				op_ctx->ctx_export->export_id,
				op_ctx->ctx_export->pseudopath);
			PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);
			return ERR_FSAL_CROSS_JUNCTION;
		}

		/* An error occurred and we will report it, but we need to get
		 * the name into the entry to proceed.
		 *
		 * Restore export and creds.
		 */
		LogDebug(COMPONENT_EXPORT,
			 "Need to report error for junction to Export_Id %d Pseudo %s",
			 op_ctx->ctx_export->export_id,
			 op_ctx->ctx_export->pseudopath);
		restore_data(tracker);
	}

not_junction:
	PTHREAD_RWLOCK_unlock(&obj->state_hdl->state_lock);

	memset(&args, 0, sizeof(args));
	args.attrs = (struct attrlist *)attr;
	args.data = data;
	args.hdl4 = &entryFH;
	args.mounted_on_fileid = mounted_on_fileid;
	args.fileid = obj->fileid;
	args.fsid = obj->fsid;

	/* Now process the entry */
	memset(val_fh, 0, NFS4_FHSIZE);

	/* Bits that don't require allocation */
	if (tracker->mem_left < BASE_ENTRY_SIZE) {
		if (tracker->count == 0)
			tracker->error = NFS4ERR_TOOSMALL;

		goto failure;
	}

	tracker->mem_left -= BASE_ENTRY_SIZE;
	tracker_entry->cookie = cookie;
	tracker_entry->nextentry = NULL;

	/* The filename.  We don't use str2utf8 because that has an
	 * additional copy into a buffer before copying into the
	 * destination.
	 */
	namelen = strlen(cb_parms->name);

	if (tracker->mem_left < RNDUP(namelen)) {
		if (tracker->count == 0)
			tracker->error = NFS4ERR_TOOSMALL;

		goto failure;
	}

	tracker->mem_left -= RNDUP(namelen);
	tracker_entry->name.utf8string_len = namelen;
	tracker_entry->name.utf8string_val = gsh_strdup(cb_parms->name);

	/* If we carried an error from above, now that we have
	 * the name set up, go ahead and try and put error in
	 * results.
	 */
	if (rdattr_error != NFS4_OK) {
		LogDebug(COMPONENT_NFS_READDIR,
			 "Skipping because of %s",
			 nfsstat4_to_str(rdattr_error));
		goto skip;
	}

	if (cb_parms->attr_allowed &&
	    attribute_is_set(tracker->req_attr, FATTR4_FILEHANDLE) &&
	    !nfs4_FSALToFhandle(false, &entryFH, obj, op_ctx->ctx_export))
		goto server_fault;

	if (!cb_parms->attr_allowed) {
		/* fsal_readdir is signaling us that client didn't have
		 * search permission in this directory, so we can't return any
		 * attributes, but must indicate NFS4ERR_ACCESS.
		 */
		rdattr_error = NFS4ERR_ACCESS;
		LogDebug(COMPONENT_NFS_READDIR,
			 "Skipping because of %s",
			 nfsstat4_to_str(rdattr_error));
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

		rdattr_error = nfs4_Errno_status(fsal_status);
		LogDebug(COMPONENT_NFS_READDIR,
			 "Skipping because of %s",
			 nfsstat4_to_str(rdattr_error));
		goto skip;
	}

	if (nfs4_FSALattr_To_Fattr(&args,
				   tracker->req_attr,
				   &tracker_entry->attrs) != 0) {
		LogCrit(COMPONENT_NFS_READDIR,
			"nfs4_FSALattr_To_Fattr failed to convert attr");
		goto server_fault;
	}

	/* Do not cache attrs, it will affect readdir performance */
	if (obj->obj_ops->is_referral(obj, (struct attrlist *) attr,
		false /*cache_attrs*/)) {
		rdattr_error = NFS4ERR_MOVED;
		LogDebug(COMPONENT_NFS_READDIR,
			 "Skipping because of %s",
			 nfsstat4_to_str(rdattr_error));
		/* Discard the attributes we had retrieved. */
		nfs4_Fattr_Free(&tracker_entry->attrs);
	}

 skip:

	if (rdattr_error != NFS4_OK) {
		if (!attribute_is_set(tracker->req_attr,
			FATTR4_RDATTR_ERROR) &&
		    !attribute_is_set(tracker->req_attr,
			FATTR4_FS_LOCATIONS)) {

			tracker->error = rdattr_error;
			goto failure;
		}

		if (nfs4_Fattr_Fill_Error(data, &tracker_entry->attrs,
					  rdattr_error,
					  tracker->req_attr, &args) == -1)
			goto server_fault;
	}

	if (tracker->mem_left <
	    ((tracker_entry->attrs.attrmask.bitmap4_len * sizeof(uint32_t))
	     + (tracker_entry->attrs.attr_vals.attrlist4_len))) {
		if (tracker->count == 0)
			tracker->error = NFS4ERR_TOOSMALL;

		goto failure;
	}

	tracker->mem_left -= tracker_entry->attrs.attrmask.bitmap4_len *
			     sizeof(uint32_t);

	tracker->mem_left -= tracker_entry->attrs.attr_vals.attrlist4_len;

	if (tracker->count != 0)
		tracker->entries[tracker->count - 1].nextentry = tracker_entry;

	++(tracker->count);
	cb_parms->in_result = true;
	goto out;

 server_fault:

	tracker->error = NFS4ERR_SERVERFAULT;

 failure:

	tracker->mem_left = initial_mem_left;
	nfs4_Fattr_Free(&tracker_entry->attrs);

	if (tracker_entry->name.utf8string_val != NULL) {
		gsh_free(tracker_entry->name.utf8string_val);
		tracker_entry->name.utf8string_val = NULL;
	}

 not_inresult:

	cb_parms->in_result = false;

 out:

	return ERR_FSAL_NO_ERROR;
}

/* Base response size includes nfsstat4, eof, verifier and termination of
 * entries list.
 */
#define READDIR_RESP_BASE_SIZE (sizeof(nfsstat4) + sizeof(verifier4) + \
				2 * BYTES_PER_XDR_UNIT)

/**
 * @brief Free a list of entry4s
 *
 * This function frees a list of entry4s and all dependent strctures.
 *
 * @param[in,out] entries The entries to be freed
 */

static void free_entries(entry4 *entries)
{
	entry4 *entry = NULL;

	for (entry = entries; entry != NULL; entry = entry->nextentry) {
		if (entry->attrs.attr_vals.attrlist4_val != NULL)
			gsh_free(entry->attrs.attr_vals.attrlist4_val);

		if (entry->name.utf8string_val != NULL)
			gsh_free(entry->name.utf8string_val);
	}
	gsh_free(entries);
}

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
int nfs4_op_readdir(struct nfs_argop4 *op, compound_data_t *data,
		    struct nfs_resop4 *resp)
{
	READDIR4args * const arg_READDIR4 = &op->nfs_argop4_u.opreaddir;
	READDIR4res * const res_READDIR4 = &resp->nfs_resop4_u.opreaddir;
	struct fsal_obj_handle *dir_obj = NULL;
	bool eod_met = false;
	unsigned long dircount = 0;
	unsigned long maxcount = 0;
	entry4 *entries = NULL;
	verifier4 cookie_verifier;
	uint64_t cookie = 0;
	unsigned int estimated_num_entries = 0;
	unsigned int num_entries = 0;
	struct nfs4_readdir_cb_data tracker;
	fsal_status_t fsal_status = {0, 0};
	attrmask_t attrmask;
	bool use_cookie_verifier = op_ctx_export_has_option(
					EXPORT_OPTION_USE_COOKIE_VERIFIER);

	resp->resop = NFS4_OP_READDIR;
	res_READDIR4->status = NFS4_OK;

	res_READDIR4->status = nfs4_sanity_check_FH(data, DIRECTORY, false);

	if (res_READDIR4->status != NFS4_OK)
		goto out;

	memset(&tracker, 0, sizeof(tracker));

	dir_obj = data->current_obj;

	/* get the characteristic value for readdir operation */
	dircount = arg_READDIR4->dircount;
	cookie = arg_READDIR4->cookie;

	/* Dont over flow V4.1 maxresponsesize or maxcachedsize */
	maxcount = resp_room(data);

	if (maxcount > (arg_READDIR4->maxcount + sizeof(nfsstat4)))
		maxcount = arg_READDIR4->maxcount + sizeof(nfsstat4);

	/* Dircount is considered meaningless by many nfsv4 client (like the
	 * CITI one).  we use maxcount instead.
	 */
	/** @todo FSF - I think we could increase this, alll it does is
	 *              control how many entry4 we allocate. They aren't
	 *              that big:
	 *              cookie
	 *              name length
	 *              name pointer
	 *              pointer to next entry
	 *              bitmap4
	 *              attr_vals length
	 *              attr_vals pointer
	 */
	estimated_num_entries = 50;
	tracker.total_entries = estimated_num_entries;

	LogDebug(COMPONENT_NFS_READDIR,
		 "dircount=%lu maxcount=%lu cookie=%" PRIu64
		 " estimated_num_entries=%u",
		 dircount, maxcount, cookie, estimated_num_entries);

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

	/* If maxcount is too short (14 should be enough for an empty
	 * directory) return NFS4ERR_TOOSMALL
	 */
	if (maxcount < READDIR_RESP_BASE_SIZE || estimated_num_entries == 0) {
		res_READDIR4->status = NFS4ERR_TOOSMALL;
		LogInfo(COMPONENT_NFS_READDIR,
			"Response too small maxcount = %lu, estimated_num_entries = %u",
			maxcount - sizeof(nfsstat4), estimated_num_entries);
		goto out;
	}

	/* If a cookie verifier is used, then a non-trivial value is
	 * returned to the client.  This value is the change_time of the
	 * directory.  If verifier is unused (as in many NFS Servers)
	 * then only a set of zeros is returned (trivial value)
	 */
	memset(cookie_verifier, 0, NFS4_VERIFIER_SIZE);
	if (use_cookie_verifier) {
		time_t change_time;
		struct attrlist attrs;

		fsal_prepare_attrs(&attrs, ATTR_CHGTIME);

		fsal_status =
			data->current_obj->obj_ops->getattrs(data->current_obj,
							    &attrs);

		if (FSAL_IS_ERROR(fsal_status)) {
			res_READDIR4->status = nfs4_Errno_status(fsal_status);
			LogDebug(COMPONENT_NFS_READDIR,
				 "getattrs returned %s",
				 msg_fsal_err(fsal_status.major));
			goto out;
		}

		change_time = timespec_to_nsecs(&attrs.chgtime);
		memcpy(cookie_verifier, &change_time, sizeof(change_time));

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
	entries = gsh_calloc(estimated_num_entries, sizeof(entry4));
	tracker.entries = entries;
	tracker.mem_left = maxcount - READDIR_RESP_BASE_SIZE;
	tracker.count = 0;
	tracker.error = NFS4_OK;
	tracker.req_attr = &arg_READDIR4->attr_request;
	tracker.data = data;

	/* Assume we need at least the NFS v3 attr.
	 * Any attr is sufficient for permission checking.
	 */
	attrmask = ATTRS_NFS3;

	/* If ACL is requested, we need to add that for permission checking. */
	if (attribute_is_set(tracker.req_attr, FATTR4_ACL))
		attrmask |= ATTR_ACL;

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
		goto out;
	}

	LogDebug(COMPONENT_NFS_READDIR,
		 "fsal_readdir returned %s",
		 msg_fsal_err(fsal_status.major));

	res_READDIR4->status = tracker.error;

	if (res_READDIR4->status != NFS4_OK) {
		LogDebug(COMPONENT_NFS_READDIR, "Tracker error");
		goto out;
	}

	/* Set the op response size to be accounted for, maxcount was the
	 * maximum space that mem_left started from before we deducted
	 * READDIR_RESP_BASE_SIZE and the space used for each entry.
	 *
	 * So the formula below is equivalent to:
	 *
	 *     maxcount - maxcount + READDIR_RESP_BASE_SIZE + space for entries
	 *
	 * So maxcount disappears and all that is left is the actual size.
	 */
	data->op_resp_size = maxcount - tracker.mem_left;

	if (tracker.count != 0) {
		/* Put the entry's list in the READDIR reply if
		 * there were any.
		 */
		res_READDIR4->READDIR4res_u.resok4.reply.entries = entries;
	} else {
		gsh_free(entries);
		entries = NULL;
		res_READDIR4->READDIR4res_u.resok4.reply.entries = NULL;
	}

	/* This slight bit of oddness is caused by most booleans
	 * throughout Ganesha being of C99's bool type (taking the values
	 * true and false), but fields in XDR being of the older bool_t
	 * type i(taking the values TRUE and FALSE)
	 */
	if (eod_met)
		res_READDIR4->READDIR4res_u.resok4.reply.eof = TRUE;
	else
		res_READDIR4->READDIR4res_u.resok4.reply.eof = FALSE;

	/* Do not forget to set the verifier */
	memcpy(res_READDIR4->READDIR4res_u.resok4.cookieverf,
	       cookie_verifier,
	       NFS4_VERIFIER_SIZE);

	res_READDIR4->status = NFS4_OK;

 out:
	if ((res_READDIR4->status != NFS4_OK) && (entries != NULL))
		free_entries(entries);

	LogDebug(COMPONENT_NFS_READDIR,
		 "Returning %s",
		 nfsstat4_to_str(res_READDIR4->status));

	return res_READDIR4->status;
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
	READDIR4res *resp = &res->nfs_resop4_u.opreaddir;

	free_entries(resp->READDIR4res_u.resok4.reply.entries);
}				/* nfs4_op_readdir_Free */
